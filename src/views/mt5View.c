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
#include "core/file.h"
#include "formats/texture/pvr.h"
#include "formats/model/mt5.h"
#include "views/mt5Mesh.h"
#include "views/mt5View.h"

#define MT5VIEW_FOVY 45.0f
#define MT5VIEW_FRAME_RATE 15.0f

typedef struct {
	float		depth;
	uint32_t	triangle;
} MT5View_SortEntry;

typedef struct {
	bool			open;
	char			filename[256];
	uint32_t		nodeCount;
	uint32_t		textureCount;
	uint32_t		loadedTextures;
	Texture*		textures; // id 0 if the texture couldn't be loaded
	MT5Mesh			mesh;
	Shader			shader;
	int				lightDirLoc;
	int				modeLoc;
	Material		material;
	Vector3			target; // Orbit camera
	float			yaw, pitch, distance;
	int				dragButton; // -1 if the mouse isn't dragging
	bool			wireframe;
	bool			noCulling; // Draw the back faces too
	uint32_t		frame;
	float			frameTime;  // Time since the current frame was shown
	bool			paused;
	bool			sorted;    // The translucent triangles are sorted for the camera at sortedEye, sortedTarget
	Vector3			sortedEye;
	Vector3			sortedTarget;
} MT5View_State;

static MT5View_State g_view = { .dragButton = -1 };

// Default raylib attributes; lit from the camera, with both faces lit for when the back faces are drawn
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

// mode: 0 opaque (alpha ignored), 1 punch-through (alpha test), 2 opaque pixels of translucent polygons, 3 the rest of
// the pixels of translucent polygons (alpha blending), see DrawGroups
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
	"    if ((mode == 1 && color.a < 0.5) || (mode == 2 && color.a < 0.99) ||\n"
	"        (mode == 3 && (color.a >= 0.99 || color.a < 0.004))) discard;\n"
	"    float light = 0.45 + 0.55 * abs(dot(normalize(fragNormal), lightDir));\n"
	"    finalColor = vec4(color.rgb * light, mode == 0 ? 1.0 : color.a);\n"
	"}\n";


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
	g_view.target = g_view.mesh.center;
	g_view.yaw = atan2f(g_view.mesh.front.x, g_view.mesh.front.z); // Look at the front of the model
	g_view.pitch = 0.2f;
	g_view.distance = g_view.mesh.radius / sinf(MT5VIEW_FOVY * 0.5f * DEG2RAD);
}

void MT5View_Close(void) {
	MT5Mesh_Free(&g_view.mesh);
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

	bool success = MT5Mesh_Build(&model, &g_view.mesh) && LoadTextures(&model, filepath);
	MT5_Free(&model);
	if (!success) {
		fputs("[MT5View_Open]: Memory error\n", stderr);
		MT5View_Close();
		return false;
	}
	if (g_view.mesh.hiddenNodes > 0)
		printf("[MT5View_Open]: %u alternative meshes are not shown\n", g_view.mesh.hiddenNodes);
	if (g_view.mesh.skippedTriangles > 0)
		printf("[MT5View_Open]: %u triangles with unresolved vertex indices are not shown\n", g_view.mesh.skippedTriangles);

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
			g_view.distance = fmaxf(g_view.distance * powf(0.85f, wheel), g_view.mesh.radius * 0.01f);
	}
	if (IsKeyPressed(KEY_R))
		ResetCamera();
	if (IsKeyPressed(KEY_W))
		g_view.wireframe = !g_view.wireframe;
	if (IsKeyPressed(KEY_C))
		g_view.noCulling = !g_view.noCulling;

	if (g_view.mesh.frameCount > 0) {
		if (IsKeyPressed(KEY_SPACE))
			g_view.paused = !g_view.paused;
		if (IsKeyPressed(KEY_RIGHT)) {
			g_view.paused = true;
			g_view.frame = (g_view.frame + 1) % g_view.mesh.frameCount;
		}
		if (IsKeyPressed(KEY_LEFT)) {
			g_view.paused = true;
			g_view.frame = (g_view.frame + g_view.mesh.frameCount - 1) % g_view.mesh.frameCount;
		}
		if (!g_view.paused) {
			g_view.frameTime += fminf(GetFrameTime(), 0.25f); // Without skipping frames after a long one (loading)
			for (; g_view.frameTime >= 1.0f / MT5VIEW_FRAME_RATE; g_view.frameTime -= 1.0f / MT5VIEW_FRAME_RATE)
				g_view.frame = (g_view.frame + 1) % g_view.mesh.frameCount;
		}
	}
}

// The clamp takes priority over the flip
static int WrapMode(uint8_t uvFlags, uint8_t flip, uint8_t clamp) {
	if (uvFlags & clamp)
		return RL_TEXTURE_WRAP_CLAMP;
	return (uvFlags & flip) ? RL_TEXTURE_WRAP_MIRROR_REPEAT : RL_TEXTURE_WRAP_REPEAT;
}

static int CompareDepth(const void* a, const void* b) {
	float depthA = ((const MT5View_SortEntry*)a)->depth;
	float depthB = ((const MT5View_SortEntry*)b)->depth;
	return depthA < depthB ? 1 : depthA > depthB ? -1 : 0; // Farthest first
}

// Reorders the data of the triangles of a mesh attribute
static void PermuteTriangles(void* data, size_t triangleSize, const MT5View_SortEntry* entries, uint32_t count, uint8_t* temp) {
	for (uint32_t i = 0; i < count; i++)
		memcpy(temp + i * triangleSize, (const uint8_t*)data + entries[i].triangle * triangleSize, triangleSize);
	memcpy(data, temp, count * triangleSize);
}

// Sorts the triangles of a translucent group from back to front, and computes the depth of the group
static void SortTriangles(MT5Mesh_Group* group, Vector3 eye, Vector3 forward) {
	Mesh* mesh = &group->mesh;
	uint32_t count = group->triangleCount;
	MT5View_SortEntry* entries = malloc(count * sizeof(MT5View_SortEntry));
	uint8_t* temp = malloc(count * 9 * sizeof(float)); // The biggest attribute: 3 vertices with 3 floats
	if (entries == NULL || temp == NULL) {
		free(entries);
		free(temp);
		return;
	}

	double depthSum = 0.0;
	for (uint32_t t = 0; t < count; t++) {
		const float* v = &mesh->vertices[t * 9];
		Vector3 center = { (v[0] + v[3] + v[6]) / 3.0f, (v[1] + v[4] + v[7]) / 3.0f, (v[2] + v[5] + v[8]) / 3.0f };
		entries[t] = (MT5View_SortEntry) { Vector3DotProduct(Vector3Subtract(center, eye), forward), t };
		depthSum += entries[t].depth;
	}
	group->depth = count > 0 ? (float)(depthSum / count) : 0.0f;
	qsort(entries, count, sizeof(MT5View_SortEntry), CompareDepth);

	PermuteTriangles(mesh->vertices, 9 * sizeof(float), entries, count, temp);
	PermuteTriangles(mesh->texcoords, 6 * sizeof(float), entries, count, temp);
	PermuteTriangles(mesh->normals, 9 * sizeof(float), entries, count, temp);
	PermuteTriangles(mesh->colors, 12, entries, count, temp);
	UpdateMeshBuffer(*mesh, RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION, mesh->vertices, mesh->vertexCount * 3 * sizeof(float), 0);
	UpdateMeshBuffer(*mesh, RL_DEFAULT_SHADER_ATTRIB_LOCATION_TEXCOORD, mesh->texcoords, mesh->vertexCount * 2 * sizeof(float), 0);
	UpdateMeshBuffer(*mesh, RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL, mesh->normals, mesh->vertexCount * 3 * sizeof(float), 0);
	UpdateMeshBuffer(*mesh, RL_DEFAULT_SHADER_ATTRIB_LOCATION_COLOR, mesh->colors, mesh->vertexCount * 4, 0);
	free(entries);
	free(temp);
}

// The PowerVR of the Dreamcast sorts the translucent polygons; they are sorted again when the camera moves
static void SortTranslucent(Camera3D camera) {
	if (g_view.sorted && Vector3Equals(camera.position, g_view.sortedEye) && Vector3Equals(camera.target, g_view.sortedTarget))
		return;
	Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
	for (uint32_t i = 0; i < g_view.mesh.groupCount; i++) {
		if (g_view.mesh.groups[i].listType == MT5_LIST_TRANSLUCENT)
			SortTriangles(&g_view.mesh.groups[i], camera.position, forward);
	}
	g_view.sorted = true;
	g_view.sortedEye = camera.position;
	g_view.sortedTarget = camera.target;
}

static void DrawGroup(const MT5Mesh_Group* group, int mode) {
	if (group->frame >= 0 && (uint32_t)group->frame != g_view.frame)
		return;
	Texture white = { rlGetTextureIdDefault(), 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
	SetShaderValue(g_view.shader, g_view.modeLoc, &mode, SHADER_UNIFORM_INT);
	bool hasTexture = group->texture < g_view.textureCount && g_view.textures[group->texture].id > 0;
	g_view.material.maps[MATERIAL_MAP_DIFFUSE].texture = hasTexture ? g_view.textures[group->texture] : white;
	if (hasTexture) {
		// The textures are shared by groups with different wrapping
		unsigned int id = g_view.textures[group->texture].id;
		rlTextureParameters(id, RL_TEXTURE_WRAP_S, WrapMode(group->uvFlags, MT5_UV_FLIP_U, MT5_UV_CLAMP_U));
		rlTextureParameters(id, RL_TEXTURE_WRAP_T, WrapMode(group->uvFlags, MT5_UV_FLIP_V, MT5_UV_CLAMP_V));
	}
	DrawMesh(group->mesh, g_view.material, MatrixIdentity());
}

// Opaque and punch-through groups first, then the translucent ones in two passes. Many solid parts are translucent
// polygons with opaque textures (e.g. the walls of the box of gach5rrg.mt5), and the groups can't be sorted when they
// spread over the whole model (e.g. scene/01/toki/map.mt5): the opaque pixels of the translucent polygons are drawn
// first, writing the depth, and then the rest, from back to front and without writing it, so that they don't hide
// what is behind them (e.g. the objects behind the glass of a showcase)
static void DrawGroups(void) {
	for (uint32_t i = 0; i < g_view.mesh.groupCount; i++) {
		if (g_view.mesh.groups[i].listType != MT5_LIST_TRANSLUCENT)
			DrawGroup(&g_view.mesh.groups[i], g_view.mesh.groups[i].listType == MT5_LIST_PUNCH_THROUGH ? 1 : 0);
	}
	for (uint32_t i = 0; i < g_view.mesh.groupCount; i++) {
		if (g_view.mesh.groups[i].listType == MT5_LIST_TRANSLUCENT)
			DrawGroup(&g_view.mesh.groups[i], 2);
	}

	rlDisableDepthMask();

	float lastDepth = INFINITY;
	uint32_t lastGroup = UINT32_MAX;
	for (;;) {
		// Next farthest group after the last one drawn (equal depths in index order)
		uint32_t next = UINT32_MAX;
		for (uint32_t i = 0; i < g_view.mesh.groupCount; i++) {
			const MT5Mesh_Group* group = &g_view.mesh.groups[i];
			if (group->listType != MT5_LIST_TRANSLUCENT)
				continue;
			bool after = group->depth < lastDepth || (group->depth == lastDepth && (lastGroup == UINT32_MAX || i > lastGroup));
			bool farther = next == UINT32_MAX || group->depth > g_view.mesh.groups[next].depth;
			if (after && farther)
				next = i;
		}
		if (next == UINT32_MAX)
			break;
		DrawGroup(&g_view.mesh.groups[next], 3);
		lastDepth = g_view.mesh.groups[next].depth;
		lastGroup = next;
	}
	rlEnableDepthMask();
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
	double farPlane = g_view.distance + g_view.mesh.radius * 4.0;
	double top = nearPlane * tan(MT5VIEW_FOVY * 0.5 * DEG2RAD);
	double right = top * area.width / area.height;
	rlFrustum(-right, right, -top, top, nearPlane, farPlane);
	rlMatrixMode(RL_MODELVIEW);

	Vector3 lightDir = Vector3Normalize(Vector3Subtract(camera.position, camera.target));
	SetShaderValue(g_view.shader, g_view.lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);
	// The faces are counter-clockwise, and the models have explicit back faces where both sides are seen (e.g. the
	// posters): the culling mode of the polygon parameters is always "small polygons", so the game culls them itself
	if (g_view.noCulling)
		rlDisableBackfaceCulling();
	if (g_view.wireframe)
		rlEnableWireMode();
	SortTranslucent(camera);
	DrawGroups();
	rlDisableWireMode();
	rlEnableBackfaceCulling();

	EndMode3D();
	rlViewport(0, 0, GetRenderWidth(), GetRenderHeight());

	Font font = GuiGetFont();
	float fontSize = (float)GuiGetStyle(DEFAULT, TEXT_SIZE);
	const char* lines[4] = {
		g_view.filename,
		TextFormat("%u nodes, %u triangles, %u/%u textures", g_view.nodeCount, g_view.mesh.triangleCount, g_view.loadedTextures, g_view.textureCount),
		"Left drag: rotate   Right drag: pan   Wheel: zoom   R: reset   W: wireframe   C: back faces",
		TextFormat("Frame %u/%u   Space: play/pause   Left/Right: previous/next frame", g_view.frame + 1, g_view.mesh.frameCount),
	};
	int lineCount = g_view.mesh.frameCount > 0 ? 4 : 3;
	for (int i = 0; i < lineCount; i++)
		DrawTextEx(font, lines[i], (Vector2) { area.x + 10.0f, area.y + 10.0f + i * fontSize * 1.2f }, fontSize, 1.0f, LIGHTGRAY);
}
