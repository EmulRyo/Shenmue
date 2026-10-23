#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include "raygui.h"
#include "file.h"
#include "pvr.h"
#include "mt5.h"
#include "mt5View.h"

#define MT5VIEW_FOVY 45.0f

// Triangles that share texture and list type, drawn with one mesh
typedef struct {
	uint16_t	texture;
	uint8_t		listType;
	uint32_t	triangleCount;
	uint32_t	filled; // Triangles copied to the mesh while it is built
	Mesh		mesh;
} MT5View_Group;

typedef struct {
	bool			open;
	char			filename[256];
	uint32_t		nodeCount;
	uint32_t		triangleCount;
	uint32_t		skippedTriangles; // Triangles with vertex indices that can't be resolved
	uint32_t		hiddenNodes;      // Alternative meshes
	uint32_t		textureCount;
	uint32_t		loadedTextures;
	Texture*		textures; // id 0 if the texture couldn't be loaded
	uint32_t		groupCount;
	MT5View_Group*	groups;
	Shader			shader;
	int				lightDirLoc;
	int				modeLoc;
	Material		material;
	Vector3			center; // Bounding sphere of the model
	float			radius;
	Vector3			target; // Orbit camera
	float			yaw, pitch, distance;
	int				dragButton; // -1 if the mouse isn't dragging
	bool			wireframe;
} MT5View_State;

static MT5View_State g_view = { .dragButton = -1 };

// Default raylib attributes; lit from the camera, with both faces lit because the culling mode of the polygons isn't used
static const char* g_vertexShader =
	"#version 330\n"
	"in vec3 vertexPosition;\n"
	"in vec2 vertexTexCoord;\n"
	"in vec3 vertexNormal;\n"
	"in vec4 vertexColor;\n"
	"uniform mat4 mvp;\n"
	"out vec2 fragTexCoord;\n"
	"out vec3 fragNormal;\n"
	"out vec4 fragColor;\n"
	"void main() {\n"
	"    fragTexCoord = vertexTexCoord;\n"
	"    fragNormal = vertexNormal;\n"
	"    fragColor = vertexColor;\n"
	"    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
	"}\n";

// mode: 0 opaque (alpha ignored), 1 punch-through (alpha test), 2 translucent (alpha blending)
static const char* g_fragmentShader =
	"#version 330\n"
	"in vec2 fragTexCoord;\n"
	"in vec3 fragNormal;\n"
	"in vec4 fragColor;\n"
	"uniform sampler2D texture0;\n"
	"uniform vec4 colDiffuse;\n"
	"uniform vec3 lightDir;\n"
	"uniform int mode;\n"
	"out vec4 finalColor;\n"
	"void main() {\n"
	"    vec4 color = texture(texture0, fragTexCoord) * fragColor * colDiffuse;\n"
	"    if (mode == 1 && color.a < 0.5) discard;\n"
	"    float light = 0.45 + 0.55 * abs(dot(normalize(fragNormal), lightDir));\n"
	"    finalColor = vec4(color.rgb * light, mode == 0 ? 1.0 : color.a);\n"
	"}\n";

// Scale, rotation around X, Y and Z, and translation, relative to the parent
static Matrix NodeMatrix(const MT5_Node* node) {
	Matrix matrix = MatrixScale(node->scale[0], node->scale[1], node->scale[2]);
	matrix = MatrixMultiply(matrix, MatrixRotateX(node->rotation[0] * DEG2RAD));
	matrix = MatrixMultiply(matrix, MatrixRotateY(node->rotation[1] * DEG2RAD));
	matrix = MatrixMultiply(matrix, MatrixRotateZ(node->rotation[2] * DEG2RAD));
	return MatrixMultiply(matrix, MatrixTranslate(node->position[0], node->position[1], node->position[2]));
}

static Vector3 TransformNormal(const float normal[3], Matrix normalMatrix) {
	Vector3 result = {
		normal[0] * normalMatrix.m0 + normal[1] * normalMatrix.m4 + normal[2] * normalMatrix.m8,
		normal[0] * normalMatrix.m1 + normal[1] * normalMatrix.m5 + normal[2] * normalMatrix.m9,
		normal[0] * normalMatrix.m2 + normal[1] * normalMatrix.m6 + normal[2] * normalMatrix.m10,
	};
	return Vector3Normalize(result);
}

// Node and vertex of a corner: negative indices point to the vertices of the parent mesh
static bool ResolveVertex(const MT5_Model* model, uint32_t node, int32_t vertex, uint32_t* outNode, uint32_t* outVertex) {
	if (vertex < 0) {
		int32_t parent = MT5_ParentMeshNode(model, model->nodes[node].parent);
		if (parent < 0)
			return false;
		node = (uint32_t)parent;
		vertex += (int32_t)model->nodes[node].vertexCount;
		if (vertex < 0)
			return false;
	}
	if ((uint32_t)vertex >= model->nodes[node].vertexCount)
		return false;
	*outNode = node;
	*outVertex = (uint32_t)vertex;
	return true;
}

static bool IsDescendant(const MT5_Model* model, uint32_t node, uint32_t ancestor) {
	for (int32_t parent = model->nodes[node].parent; parent >= 0; parent = model->nodes[parent].parent) {
		if ((uint32_t)parent == ancestor)
			return true;
	}
	return false;
}

// Copy without mesh of a node of another branch that has one: the root of a branch of alternative meshes (e.g. the
// expressions of the face of a character), which are not placed on the node they replace. The nodes of the maps share
// the same ID (0xFFFF), the root included
static bool IsAlternativeRoot(const MT5_Model* model, uint32_t node) {
	if (model->nodes[node].hasMesh || model->nodes[node].parent < 0)
		return false;
	for (uint32_t i = 0; i < model->nodeCount; i++) {
		if (model->nodes[i].id == model->nodes[node].id && model->nodes[i].hasMesh && !IsDescendant(model, i, node))
			return true;
	}
	return false;
}

static bool IsTriangleValid(const MT5_Model* model, uint32_t node, const MT5_Triangle* triangle) {
	uint32_t owner, vertex;
	for (int c = 0; c < 3; c++) {
		if (!ResolveVertex(model, node, triangle->corners[c].vertex, &owner, &vertex))
			return false;
	}
	return true;
}

static MT5View_Group* FindGroup(uint16_t texture, uint8_t listType) {
	for (uint32_t i = 0; i < g_view.groupCount; i++) {
		if (g_view.groups[i].texture == texture && g_view.groups[i].listType == listType)
			return &g_view.groups[i];
	}
	return NULL;
}

static bool AddGroup(uint16_t texture, uint8_t listType) {
	MT5View_Group* groups = realloc(g_view.groups, (g_view.groupCount + 1) * sizeof(MT5View_Group));
	if (groups == NULL)
		return false;
	g_view.groups = groups;
	MT5View_Group* group = &g_view.groups[g_view.groupCount++];
	memset(group, 0, sizeof(MT5View_Group));
	group->texture = texture;
	group->listType = listType;
	return true;
}

static void AddCorner(Mesh* mesh, uint32_t index, const MT5_Corner* corner, Vector3 position, Vector3 normal) {
	memcpy(&mesh->vertices[index * 3], &position, sizeof(Vector3));
	memcpy(&mesh->normals[index * 3], &normal, sizeof(Vector3));
	mesh->texcoords[index * 2] = corner->u;
	mesh->texcoords[index * 2 + 1] = 1.0f - corner->v; // The rows of the decoded PVR textures are bottom to top
	mesh->colors[index * 4] = (corner->color >> 16) & 0xFF;
	mesh->colors[index * 4 + 1] = (corner->color >> 8) & 0xFF;
	mesh->colors[index * 4 + 2] = corner->color & 0xFF;
	mesh->colors[index * 4 + 3] = (corner->color >> 24) & 0xFF;
}

// Builds one mesh per group with the vertices in model space (the pose of the node hierarchy)
static bool BuildMeshes(const MT5_Model* model) {
	Matrix* world = malloc(model->nodeCount * (2 * sizeof(Matrix) + sizeof(bool)));
	if (world == NULL)
		return false;
	Matrix* normalMatrix = world + model->nodeCount;
	bool* hidden = (bool*)(normalMatrix + model->nodeCount);
	// The nodes are in depth first order, the parents are before their children
	for (uint32_t i = 0; i < model->nodeCount; i++) {
		int32_t parent = model->nodes[i].parent;
		Matrix local = NodeMatrix(&model->nodes[i]);
		world[i] = parent >= 0 ? MatrixMultiply(local, world[parent]) : local;
		normalMatrix[i] = MatrixTranspose(MatrixInvert(world[i]));
		hidden[i] = IsAlternativeRoot(model, i) || (parent >= 0 && hidden[parent]);
		if (hidden[i] && model->nodes[i].hasMesh)
			g_view.hiddenNodes++;
	}

	// Count the triangles of each group
	for (uint32_t n = 0; n < model->nodeCount; n++) {
		const MT5_Node* node = &model->nodes[n];
		if (hidden[n])
			continue;
		for (uint32_t t = 0; t < node->triangleCount; t++) {
			const MT5_Triangle* triangle = &node->triangles[t];
			if (!IsTriangleValid(model, n, triangle)) {
				g_view.skippedTriangles++;
				continue;
			}
			MT5View_Group* group = FindGroup(triangle->texture, triangle->listType);
			if (group == NULL) {
				if (!AddGroup(triangle->texture, triangle->listType)) {
					free(world);
					return false;
				}
				group = &g_view.groups[g_view.groupCount - 1];
			}
			group->triangleCount++;
			g_view.triangleCount++;
		}
	}

	for (uint32_t i = 0; i < g_view.groupCount; i++) {
		Mesh* mesh = &g_view.groups[i].mesh;
		mesh->triangleCount = (int)g_view.groups[i].triangleCount;
		mesh->vertexCount = mesh->triangleCount * 3;
		mesh->vertices = MemAlloc(mesh->vertexCount * 3 * sizeof(float));
		mesh->normals = MemAlloc(mesh->vertexCount * 3 * sizeof(float));
		mesh->texcoords = MemAlloc(mesh->vertexCount * 2 * sizeof(float));
		mesh->colors = MemAlloc(mesh->vertexCount * 4);
		if (mesh->vertices == NULL || mesh->normals == NULL || mesh->texcoords == NULL || mesh->colors == NULL) {
			free(world);
			return false;
		}
	}

	Vector3 min = { INFINITY, INFINITY, INFINITY };
	Vector3 max = { -INFINITY, -INFINITY, -INFINITY };
	for (uint32_t n = 0; n < model->nodeCount; n++) {
		const MT5_Node* node = &model->nodes[n];
		if (hidden[n])
			continue;
		for (uint32_t t = 0; t < node->triangleCount; t++) {
			const MT5_Triangle* triangle = &node->triangles[t];
			if (!IsTriangleValid(model, n, triangle))
				continue;
			MT5View_Group* group = FindGroup(triangle->texture, triangle->listType);
			for (int c = 0; c < 3; c++) {
				uint32_t owner, vertex;
				ResolveVertex(model, n, triangle->corners[c].vertex, &owner, &vertex);
				const MT5_Vertex* source = &model->nodes[owner].vertices[vertex];
				Vector3 position = Vector3Transform((Vector3) { source->position[0], source->position[1], source->position[2] }, world[owner]);
				Vector3 normal = TransformNormal(source->normal, normalMatrix[owner]);
				AddCorner(&group->mesh, group->filled * 3 + c, &triangle->corners[c], position, normal);
				min = Vector3Min(min, position);
				max = Vector3Max(max, position);
			}
			group->filled++;
		}
	}
	free(world);

	for (uint32_t i = 0; i < g_view.groupCount; i++)
		UploadMesh(&g_view.groups[i].mesh, false);

	if (g_view.triangleCount > 0) {
		g_view.center = Vector3Scale(Vector3Add(min, max), 0.5f);
		g_view.radius = Vector3Distance(min, max) * 0.5f;
	}
	if (g_view.radius <= 0.0f)
		g_view.radius = 1.0f;
	return true;
}

// PVR or DDS texture
static Texture LoadTextureData(const uint8_t* data, size_t size) {
	Texture texture = { 0 };
	Image image = { 0 };
	const char* ext = File_ExtensionFromMagic(data, size);
	if (ext != NULL && strcmp(ext, "pvr") == 0) {
		uint32_t* pixels = NULL;
		uint16_t width = 0, height = 0;
		if (PVR_LoadFromMemory((uint8_t*)data, &width, &height, &pixels))
			image = (Image) { pixels, width, height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
	}
	else if (ext != NULL && strcmp(ext, "dds") == 0)
		image = LoadImageFromMemory(".dds", data, (int)size);

	if (image.data == NULL)
		return texture;
	texture = LoadTextureFromImage(image);
	if (image.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) {
		GenTextureMipmaps(&texture);
		SetTextureFilter(texture, TEXTURE_FILTER_TRILINEAR);
	}
	else
		SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
	UnloadImage(image);
	return texture;
}

// Texture of a CHRM file, extracted from the PKF of the scene to the folder of the model as tex_<index>_<ID>.<ext>
static Texture LoadExternalTexture(const FilePathList* files, const uint8_t id[8]) {
	char suffix[20];
	int length = snprintf(suffix, sizeof(suffix), "_");
	for (int i = 0; i < 8; i++)
		length += snprintf(suffix + length, sizeof(suffix) - length, "%02X", id[i]);
	snprintf(suffix + length, sizeof(suffix) - length, ".");

	for (unsigned int i = 0; i < files->count; i++) {
		const char* filename = GetFileName(files->paths[i]);
		if (strncmp(filename, "tex_", 4) != 0 || strstr(filename, suffix) == NULL)
			continue;
		size_t size = 0;
		uint8_t* data = File_LoadToMemoryDecompressed(files->paths[i], &size);
		if (data == NULL)
			continue;
		Texture texture = LoadTextureData(data, size);
		free(data);
		if (texture.id > 0)
			return texture;
	}
	return (Texture) { 0 };
}

static bool LoadTextures(const MT5_Model* model, const char* filepath) {
	g_view.textureCount = model->textureCount;
	if (model->textureCount == 0)
		return true;
	g_view.textures = calloc(model->textureCount, sizeof(Texture));
	if (g_view.textures == NULL)
		return false;

	FilePathList files = { 0 };
	bool external = false;
	for (uint32_t i = 0; i < model->textureCount; i++)
		external |= model->textures[i].data == NULL;
	if (external)
		files = LoadDirectoryFiles(GetDirectoryPath(filepath));

	for (uint32_t i = 0; i < model->textureCount; i++) {
		const MT5_Texture* texture = &model->textures[i];
		if (texture->data != NULL)
			g_view.textures[i] = LoadTextureData(texture->data, texture->size);
		else
			g_view.textures[i] = LoadExternalTexture(&files, texture->id);
		if (g_view.textures[i].id > 0)
			g_view.loadedTextures++;
	}

	if (external)
		UnloadDirectoryFiles(files);
	if (g_view.loadedTextures < g_view.textureCount)
		fprintf(stderr, "[MT5View_Open]: Loaded %u of %u textures\n", g_view.loadedTextures, g_view.textureCount);
	return true;
}

static void ResetCamera(void) {
	g_view.target = g_view.center;
	g_view.yaw = PI; // The characters look to -Z
	g_view.pitch = 0.2f;
	g_view.distance = g_view.radius / sinf(MT5VIEW_FOVY * 0.5f * DEG2RAD);
}

void MT5View_Close(void) {
	for (uint32_t i = 0; i < g_view.groupCount; i++)
		UnloadMesh(g_view.groups[i].mesh);
	free(g_view.groups);
	for (uint32_t i = 0; i < g_view.textureCount; i++) {
		if (g_view.textures[i].id > 0)
			UnloadTexture(g_view.textures[i]);
	}
	free(g_view.textures);
	if (g_view.shader.id > 0 && g_view.shader.id != rlGetShaderIdDefault())
		UnloadShader(g_view.shader);
	if (g_view.material.maps != NULL)
		MemFree(g_view.material.maps); // UnloadMaterial would also unload the shader and the textures
	memset(&g_view, 0, sizeof(g_view));
	g_view.dragButton = -1;
}

bool MT5View_Open(const char* filepath) {
	MT5_Model model;
	if (!MT5_Load(filepath, &model))
		return false;

	MT5View_Close();
	snprintf(g_view.filename, sizeof(g_view.filename), "%s", GetFileName(filepath));
	g_view.nodeCount = model.nodeCount;
	g_view.shader = LoadShaderFromMemory(g_vertexShader, g_fragmentShader);
	g_view.lightDirLoc = GetShaderLocation(g_view.shader, "lightDir");
	g_view.modeLoc = GetShaderLocation(g_view.shader, "mode");
	g_view.material = LoadMaterialDefault();
	g_view.material.shader = g_view.shader;

	bool success = BuildMeshes(&model) && LoadTextures(&model, filepath);
	MT5_Free(&model);
	if (!success) {
		fputs("[MT5View_Open]: Memory error\n", stderr);
		MT5View_Close();
		return false;
	}
	if (g_view.hiddenNodes > 0)
		printf("[MT5View_Open]: %u alternative meshes are not shown\n", g_view.hiddenNodes);
	if (g_view.skippedTriangles > 0)
		printf("[MT5View_Open]: %u triangles with unresolved vertex indices are not shown\n", g_view.skippedTriangles);

	ResetCamera();
	g_view.open = true;
	return true;
}

bool MT5View_IsOpen(void) {
	return g_view.open;
}

static Vector3 CameraPosition(void) {
	Vector3 direction = {
		cosf(g_view.pitch) * sinf(g_view.yaw),
		sinf(g_view.pitch),
		cosf(g_view.pitch) * cosf(g_view.yaw),
	};
	return Vector3Add(g_view.target, Vector3Scale(direction, g_view.distance));
}

static void UpdateInput(Rectangle area) {
	bool inside = CheckCollisionPointRec(GetMousePosition(), area);
	if (g_view.dragButton < 0 && inside) {
		const int buttons[3] = { MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_MIDDLE };
		for (int i = 0; i < 3; i++) {
			if (IsMouseButtonPressed(buttons[i]))
				g_view.dragButton = buttons[i];
		}
	}

	if (g_view.dragButton >= 0) {
		Vector2 delta = GetMouseDelta();
		if (!IsMouseButtonDown(g_view.dragButton))
			g_view.dragButton = -1;
		else if (g_view.dragButton == MOUSE_BUTTON_LEFT) {
			g_view.yaw -= delta.x * 0.01f;
			g_view.pitch = Clamp(g_view.pitch + delta.y * 0.01f, -1.55f, 1.55f);
		}
		else {
			// Move the target with the mouse, in the plane of the screen
			Vector3 forward = Vector3Normalize(Vector3Subtract(g_view.target, CameraPosition()));
			Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, (Vector3) { 0.0f, 1.0f, 0.0f }));
			Vector3 up = Vector3CrossProduct(right, forward);
			float scale = 2.0f * g_view.distance * tanf(MT5VIEW_FOVY * 0.5f * DEG2RAD) / area.height;
			g_view.target = Vector3Add(g_view.target, Vector3Scale(right, -delta.x * scale));
			g_view.target = Vector3Add(g_view.target, Vector3Scale(up, delta.y * scale));
		}
	}

	if (inside) {
		float wheel = GetMouseWheelMove();
		if (wheel != 0.0f)
			g_view.distance = fmaxf(g_view.distance * powf(0.85f, wheel), g_view.radius * 0.01f);
	}
	if (IsKeyPressed(KEY_R))
		ResetCamera();
	if (IsKeyPressed(KEY_W))
		g_view.wireframe = !g_view.wireframe;
}

static void DrawGroups(bool translucent) {
	Texture white = { rlGetTextureIdDefault(), 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
	for (uint32_t i = 0; i < g_view.groupCount; i++) {
		const MT5View_Group* group = &g_view.groups[i];
		if ((group->listType == MT5_LIST_TRANSLUCENT) != translucent)
			continue;
		int mode = group->listType == MT5_LIST_TRANSLUCENT ? 2 : group->listType == MT5_LIST_PUNCH_THROUGH ? 1 : 0;
		SetShaderValue(g_view.shader, g_view.modeLoc, &mode, SHADER_UNIFORM_INT);
		bool hasTexture = group->texture < g_view.textureCount && g_view.textures[group->texture].id > 0;
		g_view.material.maps[MATERIAL_MAP_DIFFUSE].texture = hasTexture ? g_view.textures[group->texture] : white;
		DrawMesh(group->mesh, g_view.material, MatrixIdentity());
	}
}

void MT5View_Draw(Rectangle area) {
	if (!g_view.open || area.width <= 0.0f || area.height <= 0.0f)
		return;
	UpdateInput(area);

	Camera3D camera = { CameraPosition(), g_view.target, { 0.0f, 1.0f, 0.0f }, MT5VIEW_FOVY, CAMERA_PERSPECTIVE };
	DrawRectangleRec(area, (Color) { 45, 45, 48, 255 });
	BeginMode3D(camera);

	// Draw only inside the area, with its aspect ratio
	rlViewport((int)area.x, GetRenderHeight() - (int)(area.y + area.height), (int)area.width, (int)area.height);
	rlMatrixMode(RL_PROJECTION);
	rlLoadIdentity();
	double nearPlane = g_view.distance * 0.01;
	double farPlane = g_view.distance + g_view.radius * 4.0;
	double top = nearPlane * tan(MT5VIEW_FOVY * 0.5 * DEG2RAD);
	double right = top * area.width / area.height;
	rlFrustum(-right, right, -top, top, nearPlane, farPlane);
	rlMatrixMode(RL_MODELVIEW);

	Vector3 lightDir = Vector3Normalize(Vector3Subtract(camera.position, camera.target));
	SetShaderValue(g_view.shader, g_view.lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);
	rlDisableBackfaceCulling();
	if (g_view.wireframe)
		rlEnableWireMode();
	DrawGroups(false);
	rlDisableDepthMask();
	DrawGroups(true);
	rlEnableDepthMask();
	rlDisableWireMode();
	rlEnableBackfaceCulling();

	EndMode3D();
	rlViewport(0, 0, GetRenderWidth(), GetRenderHeight());

	Font font = GuiGetFont();
	float fontSize = (float)GuiGetStyle(DEFAULT, TEXT_SIZE);
	const char* lines[3] = {
		g_view.filename,
		TextFormat("%u nodes, %u triangles, %u/%u textures", g_view.nodeCount, g_view.triangleCount, g_view.loadedTextures, g_view.textureCount),
		"Left drag: rotate   Right drag: pan   Wheel: zoom   R: reset   W: wireframe",
	};
	for (int i = 0; i < 3; i++)
		DrawTextEx(font, lines[i], (Vector2) { area.x + 10.0f, area.y + 10.0f + i * fontSize * 1.2f }, fontSize, 1.0f, LIGHTGRAY);
}
