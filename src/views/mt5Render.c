#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include "core/file.h"
#include "formats/texture/pvr.h"
#include "formats/model/mt5.h"
#include "views/mt5Mesh.h"
#include "views/mt5Render.h"

typedef struct {
    float       depth;
    uint32_t    triangle;
} MT5Render_SortEntry;

// Default raylib attributes; lit from the camera, with both faces lit for when the back faces are drawn
static const char* g_vertexShader =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "in vec3 vertexNormal;\n"
    "in vec4 vertexColor;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matNormal;\n"
    "out vec2 fragTexCoord;\n"
    "out vec3 fragNormal;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    fragTexCoord = vertexTexCoord;\n"
    "    fragNormal = (matNormal * vec4(vertexNormal, 0.0)).xyz;\n"
    "    fragColor = vertexColor;\n"
    "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
    "}\n";

// mode: 0 opaque (alpha ignored), 1 punch-through (alpha test), 2 opaque pixels of translucent polygons, 3 the rest of
// the pixels of translucent polygons (alpha blending), see MT5Render_DrawOpaque and MT5Render_DrawTranslucent
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

void MT5Render_Init(MT5Render* render) {
    render->shader = LoadShaderFromMemory(g_vertexShader, g_fragmentShader);
    render->lightDirLoc = GetShaderLocation(render->shader, "lightDir");
    render->modeLoc = GetShaderLocation(render->shader, "mode");
    render->material = LoadMaterialDefault();
    render->material.shader = render->shader;
}

void MT5Render_Free(MT5Render* render) {
    if (render->shader.id > 0 && render->shader.id != rlGetShaderIdDefault())
        UnloadShader(render->shader);
    if (render->material.maps != NULL)
        MemFree(render->material.maps); // UnloadMaterial would also unload the shader and the textures
    memset(render, 0, sizeof(*render));
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

static bool LoadTextures(const MT5_Model* source, const char* filepath, MT5Render_Model* model) {
    model->textureCount = source->textureCount;
    if (source->textureCount == 0)
        return true;
    model->textures = calloc(source->textureCount, sizeof(Texture));
    if (model->textures == NULL)
        return false;

    FilePathList files = { 0 };
    bool external = false;
    for (uint32_t i = 0; i < source->textureCount; i++)
        external |= source->textures[i].data == NULL;
    if (external)
        files = LoadDirectoryFiles(GetDirectoryPath(filepath));

    for (uint32_t i = 0; i < source->textureCount; i++) {
        const MT5_Texture* texture = &source->textures[i];
        if (texture->data != NULL)
            model->textures[i] = LoadTextureData(texture->data, texture->size);
        else
            model->textures[i] = LoadExternalTexture(&files, texture->id);
        if (model->textures[i].id > 0)
            model->loadedTextures++;
    }

    if (external)
        UnloadDirectoryFiles(files);
    if (model->loadedTextures < model->textureCount)
        fprintf(stderr, "[MT5Render_LoadModel]: Loaded %u of %u textures of %s\n", model->loadedTextures, model->textureCount, GetFileName(filepath));
    return true;
}

bool MT5Render_LoadModel(const char* filepath, MT5Render_Model* model) {
    memset(model, 0, sizeof(*model));
    MT5_Model source;
    if (!MT5_Load(filepath, &source))
        return false;

    model->nodeCount = source.nodeCount;
    bool success = MT5Mesh_Build(&source, &model->mesh) && LoadTextures(&source, filepath, model);
    MT5_Free(&source);
    if (!success) {
        fputs("[MT5Render_LoadModel]: Memory error\n", stderr);
        MT5Render_FreeModel(model);
        return false;
    }
    return true;
}

void MT5Render_FreeModel(MT5Render_Model* model) {
    MT5Mesh_Free(&model->mesh);
    for (uint32_t i = 0; i < model->textureCount; i++) {
        if (model->textures[i].id > 0)
            UnloadTexture(model->textures[i]);
    }
    free(model->textures);
    memset(model, 0, sizeof(*model));
}

void MT5Render_SetCamera(MT5Render* render, Camera3D camera) {
    Vector3 lightDir = Vector3Normalize(Vector3Subtract(camera.position, camera.target));
    SetShaderValue(render->shader, render->lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);
}

// The clamp takes priority over the flip
static int WrapMode(uint8_t uvFlags, uint8_t flip, uint8_t clamp) {
    if (uvFlags & clamp)
        return RL_TEXTURE_WRAP_CLAMP;
    return (uvFlags & flip) ? RL_TEXTURE_WRAP_MIRROR_REPEAT : RL_TEXTURE_WRAP_REPEAT;
}

static int CompareDepth(const void* a, const void* b) {
    float depthA = ((const MT5Render_SortEntry*)a)->depth;
    float depthB = ((const MT5Render_SortEntry*)b)->depth;
    return depthA < depthB ? 1 : depthA > depthB ? -1 : 0; // Farthest first
}

// Reorders the data of the triangles of a mesh attribute
static void PermuteTriangles(void* data, size_t triangleSize, const MT5Render_SortEntry* entries, uint32_t count, uint8_t* temp) {
    for (uint32_t i = 0; i < count; i++)
        memcpy(temp + i * triangleSize, (const uint8_t*)data + entries[i].triangle * triangleSize, triangleSize);
    memcpy(data, temp, count * triangleSize);
}

// Sorts the triangles of a translucent group from back to front, and computes the depth of the group
static void SortTriangles(MT5Mesh_Group* group, Vector3 eye, Vector3 forward) {
    Mesh* mesh = &group->mesh;
    uint32_t count = group->triangleCount;
    MT5Render_SortEntry* entries = malloc(count * sizeof(MT5Render_SortEntry));
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
        entries[t] = (MT5Render_SortEntry) { Vector3DotProduct(Vector3Subtract(center, eye), forward), t };
        depthSum += entries[t].depth;
    }
    group->depth = count > 0 ? (float)(depthSum / count) : 0.0f;
    qsort(entries, count, sizeof(MT5Render_SortEntry), CompareDepth);

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

// The PowerVR of the Dreamcast sorts the translucent polygons; they are sorted again when the camera moves. The
// triangles are in model space, so the camera is taken to model space
static void SortTranslucent(MT5Render_Model* model, Matrix transform, Camera3D camera) {
    Matrix inverse = MatrixInvert(transform);
    Vector3 eye = Vector3Transform(camera.position, inverse);
    Vector3 target = Vector3Transform(camera.target, inverse);
    if (model->sorted && Vector3Equals(eye, model->sortedEye) && Vector3Equals(target, model->sortedTarget))
        return;
    Vector3 forward = Vector3Normalize(Vector3Subtract(target, eye));
    for (uint32_t i = 0; i < model->mesh.groupCount; i++) {
        if (model->mesh.groups[i].listType == MT5_LIST_TRANSLUCENT)
            SortTriangles(&model->mesh.groups[i], eye, forward);
    }
    model->sorted = true;
    model->sortedEye = eye;
    model->sortedTarget = target;
}

static void DrawGroup(MT5Render* render, const MT5Render_Model* model, const MT5Mesh_Group* group, int mode, Matrix transform, uint32_t frame) {
    if (group->frame >= 0 && (uint32_t)group->frame != frame)
        return;
    Texture white = { rlGetTextureIdDefault(), 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    SetShaderValue(render->shader, render->modeLoc, &mode, SHADER_UNIFORM_INT);
    bool hasTexture = group->texture < model->textureCount && model->textures[group->texture].id > 0;
    render->material.maps[MATERIAL_MAP_DIFFUSE].texture = hasTexture ? model->textures[group->texture] : white;
    if (hasTexture) {
        // The textures are shared by groups with different wrapping
        unsigned int id = model->textures[group->texture].id;
        rlTextureParameters(id, RL_TEXTURE_WRAP_S, WrapMode(group->uvFlags, MT5_UV_FLIP_U, MT5_UV_CLAMP_U));
        rlTextureParameters(id, RL_TEXTURE_WRAP_T, WrapMode(group->uvFlags, MT5_UV_FLIP_V, MT5_UV_CLAMP_V));
    }
    DrawMesh(group->mesh, render->material, transform);
}

// Opaque and punch-through groups first, then the translucent ones in two passes. Many solid parts are translucent
// polygons with opaque textures (e.g. the walls of the box of gach5rrg.mt5), and the groups can't be sorted when they
// spread over the whole model (e.g. scene/01/toki/map.mt5): the opaque pixels of the translucent polygons are drawn
// first, writing the depth, and then the rest (MT5Render_DrawTranslucent), from back to front and without writing it,
// so that they don't hide what is behind them (e.g. the objects behind the glass of a showcase)
void MT5Render_DrawOpaque(MT5Render* render, const MT5Render_Model* model, Matrix transform, uint32_t frame) {
    const MT5Mesh* mesh = &model->mesh;
    for (uint32_t i = 0; i < mesh->groupCount; i++) {
        if (mesh->groups[i].listType != MT5_LIST_TRANSLUCENT)
            DrawGroup(render, model, &mesh->groups[i], mesh->groups[i].listType == MT5_LIST_PUNCH_THROUGH ? 1 : 0, transform, frame);
    }
    for (uint32_t i = 0; i < mesh->groupCount; i++) {
        if (mesh->groups[i].listType == MT5_LIST_TRANSLUCENT)
            DrawGroup(render, model, &mesh->groups[i], 2, transform, frame);
    }
}

void MT5Render_DrawTranslucent(MT5Render* render, MT5Render_Model* model, Matrix transform, uint32_t frame, Camera3D camera) {
    SortTranslucent(model, transform, camera);
    rlDisableDepthMask();

    const MT5Mesh* mesh = &model->mesh;
    float lastDepth = INFINITY;
    uint32_t lastGroup = UINT32_MAX;
    for (;;) {
        // Next farthest group after the last one drawn (equal depths in index order)
        uint32_t next = UINT32_MAX;
        for (uint32_t i = 0; i < mesh->groupCount; i++) {
            const MT5Mesh_Group* group = &mesh->groups[i];
            if (group->listType != MT5_LIST_TRANSLUCENT)
                continue;
            bool after = group->depth < lastDepth || (group->depth == lastDepth && (lastGroup == UINT32_MAX || i > lastGroup));
            bool farther = next == UINT32_MAX || group->depth > mesh->groups[next].depth;
            if (after && farther)
                next = i;
        }
        if (next == UINT32_MAX)
            break;
        DrawGroup(render, model, &mesh->groups[next], 3, transform, frame);
        lastDepth = mesh->groups[next].depth;
        lastGroup = next;
    }
    rlEnableDepthMask();
}
