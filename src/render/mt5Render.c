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
#include "core/log.h"
#include "formats/archive/ipac.h"
#include "formats/texture/pvr.h"
#include "formats/model/mt5.h"
#include "render/mt5Mesh.h"
#include "render/mt5Render.h"

// OpenGL 1.1 functions that rlgl doesn't wrap, exported by the OpenGL library of every platform
#if defined(_WIN32)
#define MT5RENDER_GLAPI __stdcall
#else
#define MT5RENDER_GLAPI
#endif
#define MT5RENDER_GL_LEQUAL     0x0203
#define MT5RENDER_GL_ALWAYS     0x0207
#define MT5RENDER_GL_VIEWPORT   0x0BA2
#define MT5RENDER_GL_TEXTURE_2D 0x0DE1
void MT5RENDER_GLAPI glDepthFunc(unsigned int func);
void MT5RENDER_GLAPI glGetIntegerv(unsigned int name, int* data);
void MT5RENDER_GLAPI glBindTexture(unsigned int target, unsigned int texture);
void MT5RENDER_GLAPI glCopyTexSubImage2D(unsigned int target, int level, int xOffset, int yOffset, int x, int y, int width,
    int height);

typedef struct {
    float       depth;
    uint32_t    index;  // Of the triangle or the instance
} MT5Render_SortEntry;

// Default raylib attributes, and the position in the scene for the lights
static const char* g_vertexShader =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "in vec3 vertexNormal;\n"
    "in vec4 vertexColor;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matModel;\n"
    "uniform mat4 matNormal;\n"
    "out vec2 fragTexCoord;\n"
    "out vec3 fragNormal;\n"
    "out vec4 fragColor;\n"
    "out vec3 fragPosition;\n"
    "void main() {\n"
    "    fragTexCoord = vertexTexCoord;\n"
    "    fragNormal = (matNormal * vec4(vertexNormal, 0.0)).xyz;\n"
    "    fragColor = vertexColor;\n"
    "    fragPosition = (matModel * vec4(vertexPosition, 1.0)).xyz;\n"
    "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
    "}\n";

// mode: 0 opaque (alpha ignored), 1 punch-through (alpha test), 2 opaque pixels of translucent polygons, 3 the rest of
// the pixels of translucent polygons (alpha blending), see MT5Render_DrawOpaque and MT5Render_DrawTranslucent; and for
// a faded instance (MT5Render_Instance.fade), with the opacity of the instance: 4 opaque, 5 punch-through, 6 translucent.
// Lit from the camera, with both faces lit for when the back faces are drawn; or, for an instance with lights (lit), as
// the game lights the vertices (code at 0x0C1F3AA0 in 1ST_READ.BIN PAL): the ambient color, a directional light and the
// point lights, each one by the cosine of its angle with the normal, fully up to its full range, then as (full range /
// distance)^8 up to its range, where it ends; the sum saturates at 1. The triangles with vertex colors (prelit) aren't
// lit by the lights. Then the fog, by the depth from the camera (fog: start, end and density, 0 for none): as the fog table of
// the PowerVR that the game fills, indexed by the logarithm of the depth, it goes linearly with the logarithm of the
// depth from the start to the end, times the density, at most 1
static const char* g_fragmentShader =
    "#version 330\n"
    "#define LIGHTS 8\n"
    "in vec2 fragTexCoord;\n"
    "in vec3 fragNormal;\n"
    "in vec4 fragColor;\n"
    "in vec3 fragPosition;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform vec3 lightDir;\n"
    "uniform int mode;\n"
    "uniform float opacity;\n"
    "uniform int lit;\n"
    "uniform int prelit;\n"
    "uniform vec3 ambient;\n"
    "uniform int lightCount;\n"
    "uniform vec3 lightPosition[LIGHTS];\n"
    "uniform vec3 lightColor[LIGHTS];\n"
    "uniform vec2 lightRange[LIGHTS];\n"
    "uniform vec3 sunDirection;\n"
    "uniform vec3 sunColor;\n"
    "uniform vec3 eye;\n"
    "uniform vec3 forward;\n"
    "uniform vec3 fog;\n"
    "uniform vec3 fogColor;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec4 color = texture(texture0, fragTexCoord) * fragColor * colDiffuse;\n"
    "    if ((mode == 1 && color.a < 0.5) || (mode == 2 && color.a < 0.99) ||\n"
    "        (mode == 3 && (color.a >= 0.99 || color.a < 0.004)) || (mode == 5 && color.a < 0.5) ||\n"
    "        (mode == 6 && color.a < 0.004)) discard;\n"
    "    vec3 normal = normalize(fragNormal);\n"
    "    vec3 light = vec3(0.45 + 0.55 * abs(dot(normal, lightDir)));\n"
    "    if (lit == 1 && prelit == 1)\n"
    "        light = vec3(1.0);\n"
    "    else if (lit == 1) {\n"
    "        light = ambient + sunColor * max(dot(normal, -sunDirection), 0.0);\n"
    "        for (int i = 0; i < lightCount; i++) {\n"
    "            vec3 toLight = lightPosition[i] - fragPosition;\n"
    "            float distance = max(length(toLight), 0.001);\n"
    "            float reach = distance < lightRange[i].y ? min(pow(lightRange[i].x / distance, 8.0), 1.0) : 0.0;\n"
    "            light += lightColor[i] * reach * max(dot(normal, toLight / distance), 0.0);\n"
    "        }\n"
    "        light = min(light, vec3(1.0));\n"
    "    }\n"
    "    float alpha = mode == 0 ? 1.0 : mode == 4 || mode == 5 ? opacity : mode == 6 ? color.a * opacity : color.a;\n"
    "    vec3 rgb = color.rgb * light;\n"
    "    if (fog.z > 0.0) {\n"
    "        float depth = dot(fragPosition - eye, forward);\n"
    "        float amount = clamp(log(max(depth, 0.001) / fog.x) / log(max(fog.y / fog.x, 1.001)), 0.0, 1.0) * fog.z;\n"
    "        rgb = mix(rgb, fogColor, min(amount, 1.0));\n"
    "    }\n"
    "    finalColor = vec4(rgb, alpha);\n"
    "}\n";

// Writes back the depth saved before drawing a faded instance (texture1, the depth of the viewport from its origin)
static const char* g_restoreShader =
    "#version 330\n"
    "uniform sampler2D texture1;\n"
    "uniform ivec2 origin;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    gl_FragDepth = texelFetch(texture1, ivec2(gl_FragCoord.xy) - origin, 0).r;\n"
    "    finalColor = vec4(0.0);\n"
    "}\n";

void MT5Render_Init(MT5Render* render) {
    render->shader = LoadShaderFromMemory(g_vertexShader, g_fragmentShader);
    render->lightDirLoc = GetShaderLocation(render->shader, "lightDir");
    render->modeLoc = GetShaderLocation(render->shader, "mode");
    render->opacityLoc = GetShaderLocation(render->shader, "opacity");
    render->litLoc = GetShaderLocation(render->shader, "lit");
    render->prelitLoc = GetShaderLocation(render->shader, "prelit");
    render->ambientLoc = GetShaderLocation(render->shader, "ambient");
    render->lightCountLoc = GetShaderLocation(render->shader, "lightCount");
    render->lightPositionLoc = GetShaderLocation(render->shader, "lightPosition");
    render->lightColorLoc = GetShaderLocation(render->shader, "lightColor");
    render->lightRangeLoc = GetShaderLocation(render->shader, "lightRange");
    render->sunDirectionLoc = GetShaderLocation(render->shader, "sunDirection");
    render->sunColorLoc = GetShaderLocation(render->shader, "sunColor");
    render->eyeLoc = GetShaderLocation(render->shader, "eye");
    render->forwardLoc = GetShaderLocation(render->shader, "forward");
    render->fogLoc = GetShaderLocation(render->shader, "fog");
    render->fogColorLoc = GetShaderLocation(render->shader, "fogColor");
    render->material = LoadMaterialDefault();
    render->material.shader = render->shader;
    render->restoreShader = LoadShaderFromMemory(g_vertexShader, g_restoreShader);
    render->restoreShader.locs[SHADER_LOC_MAP_SPECULAR] = GetShaderLocation(render->restoreShader, "texture1");
    render->originLoc = GetShaderLocation(render->restoreShader, "origin");
    render->restoreMaterial = LoadMaterialDefault();
    render->restoreMaterial.shader = render->restoreShader;
    MT5Render_SetFog(render, NULL);
}

void MT5Render_Free(MT5Render* render) {
    if (render->shader.id > 0 && render->shader.id != rlGetShaderIdDefault())
        UnloadShader(render->shader);
    if (render->restoreShader.id > 0 && render->restoreShader.id != rlGetShaderIdDefault())
        UnloadShader(render->restoreShader);
    if (render->savedDepth > 0)
        rlUnloadTexture(render->savedDepth);
    if (render->restoreMaterial.maps != NULL)
        MemFree(render->restoreMaterial.maps);
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

// Whether a file is a texture extracted from the PKF of the scene with the given ID: tex_<index>_<ID>.<ext>
static bool IsTextureFile(const char* filename, const uint8_t id[8]) {
    char suffix[20];
    int length = snprintf(suffix, sizeof(suffix), "_");
    for (int i = 0; i < 8; i++)
        length += snprintf(suffix + length, sizeof(suffix) - length, "%02X", id[i]);
    snprintf(suffix + length, sizeof(suffix) - length, ".");
    return strncmp(filename, "tex_", 4) == 0 && strstr(filename, suffix) != NULL;
}

// Texture of a CHRM file, extracted from the PKF of the scene to the folder of the model
static Texture LoadExternalTexture(const FilePathList* files, const uint8_t id[8]) {
    for (unsigned int i = 0; i < files->count; i++) {
        if (!IsTextureFile(GetFileName(files->paths[i]), id))
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

// Texture of a CHRM file, from the files of the PKF of the scene loaded in memory
static Texture LoadPackedTexture(const IPAC_FileList* files, const uint8_t id[8]) {
    for (uint32_t i = 0; files != NULL && i < files->count; i++) {
        if (!IsTextureFile(files->files[i].filename, id))
            continue;
        Texture texture = LoadTextureData(files->files[i].data, files->files[i].size);
        if (texture.id > 0)
            return texture;
    }
    return (Texture) { 0 };
}

// The external textures are searched in packedFiles if it isn't NULL, in the folder of the model otherwise
static bool LoadTextures(const MT5_Model* source, const char* filepath, const IPAC_FileList* packedFiles, MT5Render_Model* model) {
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
    if (external && packedFiles == NULL)
        files = LoadDirectoryFiles(GetDirectoryPath(filepath));

    for (uint32_t i = 0; i < source->textureCount; i++) {
        const MT5_Texture* texture = &source->textures[i];
        if (texture->data != NULL)
            model->textures[i] = LoadTextureData(texture->data, texture->size);
        else if (packedFiles != NULL)
            model->textures[i] = LoadPackedTexture(packedFiles, texture->id);
        else
            model->textures[i] = LoadExternalTexture(&files, texture->id);
        if (model->textures[i].id > 0)
            model->loadedTextures++;
    }

    if (external && packedFiles == NULL)
        UnloadDirectoryFiles(files);
    if (model->loadedTextures < model->textureCount)
        Log_Warning("[MT5Render_LoadModel]: Loaded %u of %u textures of %s\n", model->loadedTextures, model->textureCount, GetFileName(filepath));
    return true;
}

static bool Build(MT5_Model* source, const char* filepath, const IPAC_FileList* packedFiles, MT5Render_Model* model) {
    model->nodeCount = source->nodeCount;
    bool success = MT5Mesh_Build(source, &model->mesh) && LoadTextures(source, filepath, packedFiles, model);
    MT5_Free(source);
    if (!success) {
        Log_Error("[MT5Render_LoadModel]: Memory error\n");
        MT5Render_FreeModel(model);
        return false;
    }
    return true;
}

bool MT5Render_LoadModel(const char* filepath, MT5Render_Model* model) {
    memset(model, 0, sizeof(*model));
    MT5_Model source;
    if (!MT5_Load(filepath, &source))
        return false;
    return Build(&source, filepath, NULL, model);
}

bool MT5Render_LoadPackedModel(const IPAC_File* file, const IPAC_FileList* files, MT5Render_Model* model) {
    memset(model, 0, sizeof(*model));
    MT5_Model source;
    if (!MT5_LoadFromMemory(file->data, file->size, file->filename, &source))
        return false;
    return Build(&source, file->filename, files, model);
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

void MT5Render_Pose(MT5Render_Model* model, const Matrix* world) {
    if (world == NULL && !model->mesh.posed)
        return;
    MT5Mesh_Pose(&model->mesh, world);
    model->sorted = false; // The translucent triangles moved
}

uint32_t MT5Render_Vertices(const MT5Render_Model* model, Matrix transform, Vector3* points) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < model->mesh.groupCount; i++) {
        const Mesh* mesh = &model->mesh.groups[i].mesh;
        for (int v = 0; v < mesh->vertexCount; v++) {
            const float* vertex = &mesh->vertices[v * 3];
            points[count++] = Vector3Transform((Vector3) { vertex[0], vertex[1], vertex[2] }, transform);
        }
    }
    return count;
}

float MT5Render_Bottom(const MT5Render_Model* model) {
    float bottom = INFINITY;
    for (uint32_t i = 0; i < model->mesh.groupCount; i++) {
        const Mesh* mesh = &model->mesh.groups[i].mesh;
        for (int v = 0; v < mesh->vertexCount; v++)
            bottom = fminf(bottom, mesh->vertices[v * 3 + 1]);
    }
    return bottom == INFINITY ? 0.0f : bottom;
}

float MT5Render_Top(const MT5Render_Model* model) {
    float top = -INFINITY;
    for (uint32_t i = 0; i < model->mesh.groupCount; i++) {
        const Mesh* mesh = &model->mesh.groups[i].mesh;
        for (int v = 0; v < mesh->vertexCount; v++)
            top = fmaxf(top, mesh->vertices[v * 3 + 1]);
    }
    return top == -INFINITY ? 0.0f : top;
}

Vector3* MT5Render_InstanceVertices(const MT5Render_Instance* instances, uint32_t count, uint32_t extra, uint32_t* vertexCount) {
    uint32_t total = extra;
    for (uint32_t i = 0; i < count; i++)
        total += instances[i].model->mesh.triangleCount * 3;
    Vector3* points = malloc((total + 1) * sizeof(Vector3));
    if (points == NULL)
        return NULL;
    *vertexCount = 0;
    for (uint32_t i = 0; i < count; i++)
        *vertexCount += MT5Render_Vertices(instances[i].model, instances[i].transform, points + *vertexCount);
    return points;
}

void MT5Render_SetCamera(MT5Render* render, Camera3D camera) {
    Vector3 lightDir = Vector3Normalize(Vector3Subtract(camera.position, camera.target));
    SetShaderValue(render->shader, render->lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);
    Vector3 forward = Vector3Negate(lightDir);
    SetShaderValue(render->shader, render->eyeLoc, &camera.position, SHADER_UNIFORM_VEC3);
    SetShaderValue(render->shader, render->forwardLoc, &forward, SHADER_UNIFORM_VEC3);
}

void MT5Render_SetFog(MT5Render* render, const MT5Render_Fog* fog) {
    float values[3] = { 0.0f, 0.0f, 0.0f }, color[3] = { 0.0f, 0.0f, 0.0f };
    if (fog != NULL && fog->on) {
        values[0] = fog->nearDistance;
        values[1] = fog->farDistance;
        values[2] = fog->density;
        memcpy(color, fog->color, sizeof(color));
    }
    SetShaderValue(render->shader, render->fogLoc, values, SHADER_UNIFORM_VEC3);
    SetShaderValue(render->shader, render->fogColorLoc, color, SHADER_UNIFORM_VEC3);
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
        memcpy(temp + i * triangleSize, (const uint8_t*)data + entries[i].index * triangleSize, triangleSize);
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
    PermuteTriangles(group->nodes, 3 * sizeof(uint16_t), entries, count, temp);
    PermuteTriangles(group->positions, 9 * sizeof(float), entries, count, temp);
    PermuteTriangles(group->normals, 9 * sizeof(float), entries, count, temp);
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
    int prelit = group->prelit ? 1 : 0;
    SetShaderValue(render->shader, render->prelitLoc, &prelit, SHADER_UNIFORM_INT);
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

// The translucent groups from back to front, in a mode
static void DrawSortedTranslucent(MT5Render* render, MT5Render_Model* model, Matrix transform, uint32_t frame, Camera3D camera,
    int mode) {
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
        DrawGroup(render, model, &mesh->groups[next], mode, transform, frame);
        lastDepth = mesh->groups[next].depth;
        lastGroup = next;
    }
    rlEnableDepthMask();
}

void MT5Render_DrawTranslucent(MT5Render* render, MT5Render_Model* model, Matrix transform, uint32_t frame, Camera3D camera) {
    DrawSortedTranslucent(render, model, transform, frame, camera, 3);
}

// Copies the depth of the viewport, to write it back after drawing a faded instance. Returns false if it can't
static bool SaveDepth(MT5Render* render) {
    int viewport[4];
    glGetIntegerv(MT5RENDER_GL_VIEWPORT, viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0)
        return false;
    if (render->savedDepth == 0 || render->savedWidth != viewport[2] || render->savedHeight != viewport[3]) {
        if (render->savedDepth > 0)
            rlUnloadTexture(render->savedDepth);
        render->savedDepth = rlLoadTextureDepth(viewport[2], viewport[3], false);
        render->savedWidth = viewport[2];
        render->savedHeight = viewport[3];
        if (render->savedDepth == 0)
            return false;
    }
    glBindTexture(MT5RENDER_GL_TEXTURE_2D, render->savedDepth);
    glCopyTexSubImage2D(MT5RENDER_GL_TEXTURE_2D, 0, 0, 0, viewport[0], viewport[1], viewport[2], viewport[3]);
    glBindTexture(MT5RENDER_GL_TEXTURE_2D, 0);
    render->savedOrigin[0] = viewport[0];
    render->savedOrigin[1] = viewport[1];
    return true;
}

// Writes back the saved depth over all the pixels of the instance, without color
static void RestoreDepth(MT5Render* render, const MT5Render_Instance* instance) {
    const MT5Mesh* mesh = &instance->model->mesh;
    rlDrawRenderBatchActive();
    rlColorMask(false, false, false, false);
    glDepthFunc(MT5RENDER_GL_ALWAYS);
    SetShaderValue(render->restoreShader, render->originLoc, render->savedOrigin, SHADER_UNIFORM_IVEC2);
    render->restoreMaterial.maps[MATERIAL_MAP_SPECULAR].texture = (Texture) { render->savedDepth, render->savedWidth,
        render->savedHeight, 1, 0 };
    for (uint32_t i = 0; i < mesh->groupCount; i++) {
        const MT5Mesh_Group* group = &mesh->groups[i];
        if (group->frame < 0 || (uint32_t)group->frame == instance->frame)
            DrawMesh(group->mesh, render->restoreMaterial, instance->transform);
    }
    rlDrawRenderBatchActive();
    glDepthFunc(MT5RENDER_GL_LEQUAL);
    rlColorMask(true, true, true, true);
}

// A faded instance, blended as a whole over what is drawn, as the game fades the characters and the models of the map
// (CharacterOpacity, MapModelOpacity): only its nearest surface is seen through, not the faces behind it (e.g. the
// back of the hair of Ryo through his face, or the walls of the room behind the front ones, in the cutscene of
// seqdata0.bin of scene/02/toki). Its depth first, without color: its opaque and punch-through groups and the opaque
// pixels of its translucent ones; then its colors at that depth with the opacity, without writing the depth: the opaque
// and punch-through groups, then the translucent ones from back to front. The depth before it is written back after it,
// so that it doesn't hide what is drawn after it, as the translucent polygons of the game don't (e.g. the stars and
// Shenhua of the vision of that cutscene, behind the ceiling and the walls of the room that fades in over them)
static void DrawFaded(MT5Render* render, const MT5Render_Instance* instance, Camera3D camera) {
    float opacity = 1.0f - instance->fade;
    SetShaderValue(render->shader, render->opacityLoc, &opacity, SHADER_UNIFORM_FLOAT);
    const MT5Mesh* mesh = &instance->model->mesh;
    rlDrawRenderBatchActive();
    bool saved = SaveDepth(render);
    rlColorMask(false, false, false, false);
    for (uint32_t i = 0; i < mesh->groupCount; i++) {
        uint8_t type = mesh->groups[i].listType;
        DrawGroup(render, instance->model, &mesh->groups[i], type == MT5_LIST_TRANSLUCENT ? 2 : type == MT5_LIST_PUNCH_THROUGH ? 5 : 4,
            instance->transform, instance->frame);
    }
    rlDrawRenderBatchActive();
    rlColorMask(true, true, true, true);
    rlDisableDepthMask();
    for (uint32_t i = 0; i < mesh->groupCount; i++) {
        if (mesh->groups[i].listType != MT5_LIST_TRANSLUCENT)
            DrawGroup(render, instance->model, &mesh->groups[i], mesh->groups[i].listType == MT5_LIST_PUNCH_THROUGH ? 5 : 4,
                instance->transform, instance->frame);
    }
    rlEnableDepthMask();
    DrawSortedTranslucent(render, instance->model, instance->transform, instance->frame, camera, 6);
    if (saved)
        RestoreDepth(render, instance);
}

// The lights of an instance, or from the camera: of its lights, the MT5RENDER_LIGHTS nearest ones whose range reaches its
// bounding sphere
static void SetLighting(MT5Render* render, const MT5Render_Instance* instance, Camera3D camera) {
    int lit = instance->lighting != NULL ? 1 : 0;
    SetShaderValue(render->shader, render->litLoc, &lit, SHADER_UNIFORM_INT);
    if (!lit)
        return;
    const MT5Render_Lighting* lighting = instance->lighting;
    float radius = instance->model->mesh.radius;
    const MT5Render_Light* nearest[MT5RENDER_LIGHTS];
    float distances[MT5RENDER_LIGHTS];
    int count = 0;
    for (uint32_t i = 0; i < lighting->lightCount; i++) {
        const MT5Render_Light* light = &lighting->lights[i];
        float distance = Vector3Distance(instance->center, (Vector3) { light->position[0], light->position[1], light->position[2] });
        if (distance > light->range + radius || (count == MT5RENDER_LIGHTS && distance >= distances[count - 1]))
            continue;
        // Inserted in the order of the distances
        int j = count < MT5RENDER_LIGHTS ? count++ : count - 1;
        for (; j > 0 && distances[j - 1] > distance; j--) {
            nearest[j] = nearest[j - 1];
            distances[j] = distances[j - 1];
        }
        nearest[j] = light;
        distances[j] = distance;
    }
    Vector3 positions[MT5RENDER_LIGHTS], colors[MT5RENDER_LIGHTS];
    Vector2 ranges[MT5RENDER_LIGHTS];
    for (int i = 0; i < count; i++) {
        positions[i] = (Vector3) { nearest[i]->position[0], nearest[i]->position[1], nearest[i]->position[2] };
        colors[i] = Vector3Scale((Vector3) { nearest[i]->color[0], nearest[i]->color[1], nearest[i]->color[2] }, nearest[i]->intensity);
        ranges[i] = (Vector2) { nearest[i]->fullRange, nearest[i]->range };
    }
    SetShaderValue(render->shader, render->ambientLoc, lighting->ambient, SHADER_UNIFORM_VEC3);
    Vector3 sunDirection = { lighting->sunDirection[0], lighting->sunDirection[1], lighting->sunDirection[2] };
    if (lighting->fromCamera)
        sunDirection = Vector3Normalize(Vector3Subtract(instance->center, camera.position));
    SetShaderValue(render->shader, render->sunDirectionLoc, &sunDirection, SHADER_UNIFORM_VEC3);
    SetShaderValue(render->shader, render->sunColorLoc, lighting->sunColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(render->shader, render->lightCountLoc, &count, SHADER_UNIFORM_INT);
    if (count > 0) {
        SetShaderValueV(render->shader, render->lightPositionLoc, positions, SHADER_UNIFORM_VEC3, count);
        SetShaderValueV(render->shader, render->lightColorLoc, colors, SHADER_UNIFORM_VEC3, count);
        SetShaderValueV(render->shader, render->lightRangeLoc, ranges, SHADER_UNIFORM_VEC2, count);
    }
}

void MT5Render_DrawInstances(MT5Render* render, const MT5Render_Instance* instances, uint32_t count, Camera3D camera) {
    MT5Render_SortEntry* order = malloc((count + 1) * sizeof(MT5Render_SortEntry));
    if (order == NULL)
        return;
    MT5Render_SetCamera(render, camera);
    for (uint32_t i = 0; i < count; i++) {
        if (instances[i].fade <= 0.0f) {
            SetLighting(render, &instances[i], camera);
            MT5Render_DrawOpaque(render, instances[i].model, instances[i].transform, instances[i].frame);
        }
        order[i] = (MT5Render_SortEntry) { Vector3DistanceSqr(instances[i].center, camera.position), i };
    }
    qsort(order, count, sizeof(MT5Render_SortEntry), CompareDepth);
    for (uint32_t i = 0; i < count; i++) {
        const MT5Render_Instance* instance = &instances[order[i].index];
        if (instance->fade >= 1.0f)
            continue;
        SetLighting(render, instance, camera);
        if (instance->fade > 0.0f)
            DrawFaded(render, instance, camera);
        else
            MT5Render_DrawTranslucent(render, instance->model, instance->transform, instance->frame, camera);
    }
    // The models drawn after these are lit from the camera
    int lit = 0;
    SetShaderValue(render->shader, render->litLoc, &lit, SHADER_UNIFORM_INT);
    free(order);
}
