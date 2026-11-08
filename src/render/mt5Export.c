#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <cJSON.h>
#include "formats/model/mt5.h"
#include "formats/animation/motn.h"
#include "render/mt5Mesh.h"
#include "render/mt5Pose.h"
#include "render/mt5Render.h"
#include "render/mt5Export.h"

// glTF constants
#define GLTF_FLOAT                  5126
#define GLTF_UNSIGNED_BYTE          5121
#define GLTF_UNSIGNED_SHORT         5123
#define GLTF_UNSIGNED_INT           5125
#define GLTF_ARRAY_BUFFER           34962
#define GLTF_ELEMENT_ARRAY_BUFFER   34963
#define GLTF_LINEAR                 9729
#define GLTF_LINEAR_MIPMAP_LINEAR   9987
#define GLTF_REPEAT                 10497
#define GLTF_MIRRORED_REPEAT        33648
#define GLTF_CLAMP_TO_EDGE          33071

#define GLB_MAGIC       0x46546C67 // "glTF"
#define GLB_CHUNK_JSON  0x4E4F534A // "JSON"
#define GLB_CHUNK_BIN   0x004E4942 // "BIN\0"

// A vertex of the exported triangles. No padding, so that the identical vertices can be compared with memcmp
typedef struct {
    float       position[3];
    float       normal[3];
    float       uv[2];      // With the origin at the top left of the texture
    uint8_t     color[4];   // RGBA
    uint32_t    node;       // That moves the vertex
} MT5Export_Vertex;

// How the alpha of a group is exported
typedef enum {
    MT5EXPORT_ALPHA_OPAQUE,     // Ignored
    MT5EXPORT_ALPHA_MASK,       // Transparent below 0.5
    MT5EXPORT_ALPHA_BLEND,      // Blended
} MT5Export_Alpha;

// The triangles of a group, with its identical vertices shared
typedef struct {
    const MT5Mesh_Group*    group;
    MT5Export_Alpha         alpha;
    uint32_t                vertexCount;
    MT5Export_Vertex*       vertices;
    uint32_t                indexCount;
    uint32_t*               indices;
} MT5Export_Primitive;

typedef struct {
    uint32_t                count;
    MT5Export_Primitive*    primitives;
    Image*                  images;     // RGBA image of each texture of the model, data NULL if it isn't used or loaded
} MT5Export_Model;

typedef struct {
    uint8_t*    data;
    size_t      size;
    size_t      capacity;
} MT5Export_Buffer;

static uint32_t HashVertex(const MT5Export_Vertex* vertex) {
    const uint8_t* bytes = (const uint8_t*)vertex;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < sizeof(*vertex); i++)
        hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

// A vertex of the group in the pose of the file (the meshes may be in the pose of a motion), from its position and its
// normal in the space of its node. pose is the world matrix of each node, and normalMatrix its inverse transposed
static MT5Export_Vertex GroupVertex(const MT5Mesh_Group* group, uint32_t index, const Matrix* pose, const Matrix* normalMatrix) {
    const Mesh* mesh = &group->mesh;
    MT5Export_Vertex vertex = { 0 };
    vertex.node = group->nodes[index];
    const float* p = &group->positions[index * 3];
    Vector3 position = Vector3Transform((Vector3) { p[0], p[1], p[2] }, pose[vertex.node]);
    memcpy(vertex.position, &position, sizeof(position));
    const float* n = &group->normals[index * 3];
    Matrix m = normalMatrix[vertex.node];
    float x = m.m0 * n[0] + m.m4 * n[1] + m.m8 * n[2];
    float y = m.m1 * n[0] + m.m5 * n[1] + m.m9 * n[2];
    float z = m.m2 * n[0] + m.m6 * n[1] + m.m10 * n[2];
    float length = sqrtf(x * x + y * y + z * z);
    if (length > 1e-6f) {
        vertex.normal[0] = x / length;
        vertex.normal[1] = y / length;
        vertex.normal[2] = z / length;
    }
    else
        vertex.normal[1] = 1.0f;
    // The texture coordinates of the meshes are for the rows of the textures from bottom to top
    vertex.uv[0] = mesh->texcoords[index * 2];
    vertex.uv[1] = 1.0f - mesh->texcoords[index * 2 + 1];
    memcpy(vertex.color, &mesh->colors[index * 4], 4);
    return vertex;
}

// Copies the triangles of the group sharing the identical vertices
static bool BuildPrimitive(const MT5Mesh_Group* group, const Matrix* pose, const Matrix* normalMatrix, MT5Export_Primitive* primitive) {
    const Mesh* mesh = &group->mesh;
    uint32_t count = (uint32_t)mesh->vertexCount;
    uint32_t tableSize = 16;
    while (tableSize < count * 2)
        tableSize *= 2;
    primitive->group = group;
    primitive->vertices = malloc((count + 1) * sizeof(MT5Export_Vertex));
    primitive->indices = malloc((count + 1) * sizeof(uint32_t));
    uint32_t* table = malloc(tableSize * sizeof(uint32_t));
    if (primitive->vertices == NULL || primitive->indices == NULL || table == NULL) {
        free(table);
        return false;
    }
    memset(table, 0xFF, tableSize * sizeof(uint32_t));

    for (uint32_t i = 0; i < count; i++) {
        MT5Export_Vertex vertex = GroupVertex(group, i, pose, normalMatrix);
        uint32_t slot = HashVertex(&vertex) & (tableSize - 1);
        while (table[slot] != UINT32_MAX && memcmp(&primitive->vertices[table[slot]], &vertex, sizeof(vertex)) != 0)
            slot = (slot + 1) & (tableSize - 1);
        if (table[slot] == UINT32_MAX) {
            table[slot] = primitive->vertexCount;
            primitive->vertices[primitive->vertexCount++] = vertex;
        }
        primitive->indices[primitive->indexCount++] = table[slot];
    }
    free(table);
    return true;
}

// RGBA pixels of a texture with the rows from top to bottom
static Image TextureImage(Texture texture) {
    Image image = { 0 };
    if (texture.format < PIXELFORMAT_COMPRESSED_DXT1_RGB) {
        image = LoadImageFromTexture(texture);
        if (image.data != NULL)
            ImageFlipVertical(&image);
    }
    else {
        // The compressed textures (DDS) can't be read back: they are drawn to an RGBA texture without blending, whose
        // rows end up from top to bottom
        RenderTexture2D target = LoadRenderTexture(texture.width, texture.height);
        if (target.id == 0)
            return image;
        BeginTextureMode(target);
        ClearBackground(BLANK);
        rlDisableColorBlend();
        DrawTexture(texture, 0, 0, WHITE);
        rlDrawRenderBatchActive();
        rlEnableColorBlend();
        EndTextureMode();
        image = LoadImageFromTexture(target.texture);
        UnloadRenderTexture(target);
    }
    if (image.data != NULL)
        ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    return image;
}

static bool IsExported(const MT5Mesh_Group* group, uint32_t frame) {
    return group->triangleCount > 0 && (group->frame < 0 || (uint32_t)group->frame == frame);
}

static bool HasTexture(const MT5Export_Model* exported, const MT5Render_Model* model, const MT5Mesh_Group* group) {
    return group->texture < model->textureCount && exported->images[group->texture].data != NULL;
}

static void FreeExport(MT5Export_Model* exported, uint32_t textureCount) {
    for (uint32_t i = 0; i < exported->count; i++) {
        free(exported->primitives[i].vertices);
        free(exported->primitives[i].indices);
    }
    free(exported->primitives);
    for (uint32_t i = 0; exported->images != NULL && i < textureCount; i++) {
        if (exported->images[i].data != NULL)
            UnloadImage(exported->images[i]);
    }
    free(exported->images);
    memset(exported, 0, sizeof(*exported));
}

// Alpha of the pixels of an image: opaque if all of them are opaque, mask if they are opaque or transparent (allowing a
// few in between, e.g. from compression), blend otherwise
static MT5Export_Alpha ImageAlpha(Image image) {
    const uint8_t* pixels = image.data;
    size_t count = (size_t)image.width * image.height, transparent = 0, partial = 0;
    for (size_t i = 0; i < count; i++) {
        uint8_t alpha = pixels[i * 4 + 3];
        if (alpha < 8)
            transparent++;
        else if (alpha < 248)
            partial++;
    }
    if (partial > count / 100)
        return MT5EXPORT_ALPHA_BLEND;
    return transparent + partial > 0 ? MT5EXPORT_ALPHA_MASK : MT5EXPORT_ALPHA_OPAQUE;
}

// The opaque triangles ignore the alpha, and the punch-through ones are masked. The translucent ones are blended only if
// their texture or their vertex colors have partial alpha: many of them are solid (e.g. the face and the hair of
// ykb_m.mt5, whose texture is opaque or transparent), and the blended materials don't hide what is behind them in many
// programs, which don't write their depth
static MT5Export_Alpha GroupAlpha(const MT5Export_Primitive* primitive, const Image* image) {
    if (primitive->group->listType == MT5_LIST_OPAQUE)
        return MT5EXPORT_ALPHA_OPAQUE;
    if (primitive->group->listType == MT5_LIST_PUNCH_THROUGH)
        return MT5EXPORT_ALPHA_MASK;
    for (uint32_t i = 0; i < primitive->vertexCount; i++) {
        if (primitive->vertices[i].color[3] < 248)
            return MT5EXPORT_ALPHA_BLEND;
    }
    return image != NULL ? ImageAlpha(*image) : MT5EXPORT_ALPHA_OPAQUE;
}

// The primitives of the groups of the frame and the images of their textures
static bool BuildExport(const MT5Render_Model* model, uint32_t frame, MT5Export_Model* exported) {
    memset(exported, 0, sizeof(*exported));
    exported->primitives = calloc(model->mesh.groupCount + 1, sizeof(MT5Export_Primitive));
    exported->images = calloc(model->textureCount + 1, sizeof(Image));
    Matrix* normalMatrix = malloc((model->mesh.nodeCount + 1) * sizeof(Matrix));
    if (exported->primitives == NULL || exported->images == NULL || normalMatrix == NULL) {
        free(normalMatrix);
        return false;
    }
    for (uint32_t i = 0; i < model->mesh.nodeCount; i++)
        normalMatrix[i] = MatrixTranspose(MatrixInvert(model->mesh.pose[i]));
    for (uint32_t i = 0; i < model->mesh.groupCount; i++) {
        const MT5Mesh_Group* group = &model->mesh.groups[i];
        if (!IsExported(group, frame))
            continue;
        if (!BuildPrimitive(group, model->mesh.pose, normalMatrix, &exported->primitives[exported->count++])) {
            free(normalMatrix);
            return false;
        }
        uint16_t texture = group->texture;
        if (texture < model->textureCount && model->textures[texture].id > 0 && exported->images[texture].data == NULL) {
            exported->images[texture] = TextureImage(model->textures[texture]);
            if (exported->images[texture].data == NULL)
                fprintf(stderr, "[MT5Export_Save]: The texture %u can't be read\n", texture);
        }
    }
    free(normalMatrix);
    for (uint32_t i = 0; i < exported->count; i++) {
        MT5Export_Primitive* primitive = &exported->primitives[i];
        bool textured = HasTexture(exported, model, primitive->group);
        primitive->alpha = GroupAlpha(primitive, textured ? &exported->images[primitive->group->texture] : NULL);
    }
    return true;
}

// Appends data aligned to 4 bytes. Returns its offset, or SIZE_MAX on memory error
static size_t AppendBuffer(MT5Export_Buffer* buffer, const void* data, size_t size) {
    size_t offset = (buffer->size + 3) & ~(size_t)3;
    if (offset + size > buffer->capacity) {
        size_t capacity = buffer->capacity > 0 ? buffer->capacity : 65536;
        while (offset + size > capacity)
            capacity *= 2;
        uint8_t* grown = realloc(buffer->data, capacity);
        if (grown == NULL)
            return SIZE_MAX;
        buffer->data = grown;
        buffer->capacity = capacity;
    }
    memset(buffer->data + buffer->size, 0, offset - buffer->size);
    memcpy(buffer->data + offset, data, size);
    buffer->size = offset + size;
    return offset;
}

// Adds a buffer view of the data appended to the buffer. Returns its index, or -1 on memory error
static int AddBufferView(cJSON* views, MT5Export_Buffer* buffer, const void* data, size_t size, int target) {
    size_t offset = AppendBuffer(buffer, data, size);
    if (offset == SIZE_MAX)
        return -1;
    cJSON* view = cJSON_CreateObject();
    cJSON_AddNumberToObject(view, "buffer", 0);
    cJSON_AddNumberToObject(view, "byteOffset", (double)offset);
    cJSON_AddNumberToObject(view, "byteLength", (double)size);
    if (target != 0)
        cJSON_AddNumberToObject(view, "target", target);
    cJSON_AddItemToArray(views, view);
    return cJSON_GetArraySize(views) - 1;
}

// Adds an accessor of count elements of data (in a new buffer view), with the bounds of its components if bounds isn't
// 0 (bounds components: min, then max). Returns its index, or -1 on memory error
static int AddAccessor(cJSON* accessors, cJSON* views, MT5Export_Buffer* buffer, const void* data, size_t size, uint32_t count,
    int componentType, const char* type, int target, const float* bounds, int boundsCount) {
    int view = AddBufferView(views, buffer, data, size, target);
    if (view < 0)
        return -1;
    cJSON* accessor = cJSON_CreateObject();
    cJSON_AddNumberToObject(accessor, "bufferView", view);
    cJSON_AddNumberToObject(accessor, "componentType", componentType);
    cJSON_AddNumberToObject(accessor, "count", count);
    cJSON_AddStringToObject(accessor, "type", type);
    if (boundsCount > 0) {
        cJSON_AddItemToObject(accessor, "min", cJSON_CreateFloatArray(bounds, boundsCount));
        cJSON_AddItemToObject(accessor, "max", cJSON_CreateFloatArray(bounds + boundsCount, boundsCount));
    }
    cJSON_AddItemToArray(accessors, accessor);
    return cJSON_GetArraySize(accessors) - 1;
}

// Adds the joints (the node of each vertex, after the root joint) and the weights (1 for that node) of the skinned
// vertices. Returns false on memory error
static bool AddSkinAttributes(cJSON* attributes, cJSON* accessors, cJSON* views, MT5Export_Buffer* buffer, const MT5Export_Primitive* primitive) {
    uint16_t* joints = calloc(primitive->vertexCount * 4 + 1, sizeof(uint16_t));
    float* weights = calloc(primitive->vertexCount * 4 + 1, sizeof(float));
    bool success = joints != NULL && weights != NULL;
    for (uint32_t i = 0; success && i < primitive->vertexCount; i++) {
        joints[i * 4] = (uint16_t)(primitive->vertices[i].node + 1);
        weights[i * 4] = 1.0f;
    }
    int jointsAccessor = success ? AddAccessor(accessors, views, buffer, joints, primitive->vertexCount * 4 * sizeof(uint16_t),
        primitive->vertexCount, GLTF_UNSIGNED_SHORT, "VEC4", GLTF_ARRAY_BUFFER, NULL, 0) : -1;
    int weightsAccessor = jointsAccessor >= 0 ? AddAccessor(accessors, views, buffer, weights, primitive->vertexCount * 4 * sizeof(float),
        primitive->vertexCount, GLTF_FLOAT, "VEC4", GLTF_ARRAY_BUFFER, NULL, 0) : -1;
    free(joints);
    free(weights);
    if (weightsAccessor < 0)
        return false;
    cJSON_AddNumberToObject(attributes, "JOINTS_0", jointsAccessor);
    cJSON_AddNumberToObject(attributes, "WEIGHTS_0", weightsAccessor);
    return true;
}

// Translation, rotation and scale of a node relative to its parent
typedef struct {
    Vector3     translation;
    Quaternion  rotation;
    Vector3     scale;
} MT5Export_Transform;

// Transform of a node relative to its parent, from the world matrix of each node
static MT5Export_Transform LocalTransform(const MT5Mesh* mesh, const Matrix* world, uint32_t node) {
    int32_t parent = mesh->nodes[node].parent;
    Matrix local = parent >= 0 ? MatrixMultiply(world[node], MatrixInvert(world[parent])) : world[node];
    MT5Export_Transform transform;
    MatrixDecompose(local, &transform.translation, &transform.rotation, &transform.scale);
    transform.rotation = QuaternionNormalize(transform.rotation);
    return transform;
}

// The skeleton: a root joint (glTF node 1) with the nodes of the model under it (glTF node 2 + index of the node), in the
// pose of the file, and the skin of the mesh (glTF node 0) with them. The joint of a node is its index + 1. Returns
// false on memory error
static bool AddSkeleton(cJSON* root, cJSON* nodes, cJSON* accessors, cJSON* views, MT5Export_Buffer* buffer, const MT5Mesh* mesh,
    const MT5Export_Transform* rest, const char* name) {
    uint32_t count = mesh->nodeCount;
    cJSON* skeleton = cJSON_CreateObject();
    cJSON_AddStringToObject(skeleton, "name", TextFormat("%s_skeleton", name));
    cJSON_AddItemToArray(nodes, skeleton);
    cJSON** joints = malloc((count + 1) * sizeof(cJSON*));
    Matrix* inverseBind = malloc((count + 2) * sizeof(Matrix));
    if (joints == NULL || inverseBind == NULL) {
        free(joints);
        free(inverseBind);
        return false;
    }
    for (uint32_t i = 0; i < count; i++) {
        const MT5Export_Transform* transform = &rest[i];
        joints[i] = cJSON_CreateObject();
        cJSON_AddStringToObject(joints[i], "name", TextFormat("node_%u_%X", i, mesh->nodes[i].id));
        cJSON_AddItemToObject(joints[i], "translation", cJSON_CreateFloatArray((const float*)&transform->translation, 3));
        cJSON_AddItemToObject(joints[i], "rotation", cJSON_CreateFloatArray((const float*)&transform->rotation, 4));
        cJSON_AddItemToObject(joints[i], "scale", cJSON_CreateFloatArray((const float*)&transform->scale, 3));
        cJSON_AddItemToArray(nodes, joints[i]);
    }
    // The children of each node, the roots under the root joint
    for (uint32_t i = 0; i < count; i++) {
        int32_t parent = mesh->nodes[i].parent;
        cJSON* owner = parent >= 0 ? joints[parent] : skeleton;
        cJSON* children = cJSON_GetObjectItem(owner, "children");
        if (children == NULL)
            children = cJSON_AddArrayToObject(owner, "children");
        cJSON_AddItemToArray(children, cJSON_CreateNumber(2 + i));
    }
    free(joints);

    inverseBind[0] = MatrixIdentity();
    for (uint32_t i = 0; i < count; i++)
        inverseBind[i + 1] = MatrixInvert(mesh->pose[i]);
    float* matrices = malloc((count + 1) * 16 * sizeof(float));
    if (matrices == NULL) {
        free(inverseBind);
        return false;
    }
    // Column-major, as the fields of the raylib matrices
    for (uint32_t i = 0; i <= count; i++)
        memcpy(&matrices[i * 16], MatrixToFloatV(inverseBind[i]).v, 16 * sizeof(float));
    free(inverseBind);
    int accessor = AddAccessor(accessors, views, buffer, matrices, (count + 1) * 16 * sizeof(float), count + 1, GLTF_FLOAT, "MAT4", 0, NULL, 0);
    free(matrices);
    if (accessor < 0)
        return false;

    cJSON* skin = cJSON_CreateObject();
    cJSON_AddStringToObject(skin, "name", name);
    cJSON_AddNumberToObject(skin, "inverseBindMatrices", accessor);
    cJSON_AddNumberToObject(skin, "skeleton", 1);
    cJSON* jointList = cJSON_AddArrayToObject(skin, "joints");
    for (uint32_t i = 0; i <= count; i++)
        cJSON_AddItemToArray(jointList, cJSON_CreateNumber(1 + i));
    cJSON_AddItemToArray(cJSON_AddArrayToObject(root, "skins"), skin);
    cJSON_AddNumberToObject(cJSON_GetArrayItem(nodes, 0), "skin", 0);
    return true;
}

static float QuatDot(Quaternion a, Quaternion b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

static bool IsSameTransform(const MT5Export_Transform* a, const MT5Export_Transform* b, int path) {
    const float epsilon = 1e-4f;
    if (path == 0)
        return Vector3Distance(a->translation, b->translation) < epsilon;
    if (path == 1)
        return fabsf(QuatDot(a->rotation, b->rotation)) > 1.0f - 1e-6f;
    return Vector3Distance(a->scale, b->scale) < epsilon;
}

// Adds the channel of a path (0 translation, 1 rotation, 2 scale) of a joint if it changes from the rest pose in any
// frame, or always if force. Returns false on memory error
static bool AddChannel(cJSON* animation, cJSON* accessors, cJSON* views, MT5Export_Buffer* buffer, const MT5Export_Transform* samples,
    uint32_t frames, const MT5Export_Transform* rest, uint32_t node, int path, int times, bool force) {
    bool changes = force;
    for (uint32_t f = 0; f < frames && !changes; f++)
        changes = !IsSameTransform(&samples[f], rest, path);
    if (!changes)
        return true;
    int components = path == 1 ? 4 : 3;
    float* values = malloc(frames * components * sizeof(float) + 1);
    if (values == NULL)
        return false;
    for (uint32_t f = 0; f < frames; f++) {
        const float* value = path == 0 ? (const float*)&samples[f].translation : path == 1 ? (const float*)&samples[f].rotation :
            (const float*)&samples[f].scale;
        memcpy(&values[f * components], value, components * sizeof(float));
    }
    int output = AddAccessor(accessors, views, buffer, values, frames * components * sizeof(float), frames, GLTF_FLOAT,
        components == 4 ? "VEC4" : "VEC3", 0, NULL, 0);
    free(values);
    if (output < 0)
        return false;

    cJSON* samplers = cJSON_GetObjectItem(animation, "samplers");
    cJSON* sampler = cJSON_CreateObject();
    cJSON_AddNumberToObject(sampler, "input", times);
    cJSON_AddNumberToObject(sampler, "output", output);
    cJSON_AddStringToObject(sampler, "interpolation", "LINEAR");
    cJSON_AddItemToArray(samplers, sampler);
    cJSON* channel = cJSON_CreateObject();
    cJSON_AddNumberToObject(channel, "sampler", cJSON_GetArraySize(samplers) - 1);
    cJSON* target = cJSON_AddObjectToObject(channel, "target");
    cJSON_AddNumberToObject(target, "node", 2 + node);
    static const char* paths[3] = { "translation", "rotation", "scale" };
    cJSON_AddStringToObject(target, "path", paths[path]);
    cJSON_AddItemToArray(cJSON_GetObjectItem(animation, "channels"), channel);
    return true;
}

// Adds one animation per motion of the bank that can pose the body, sampled at every frame: the joints that the motion
// moves from the rest pose. Returns false on memory error
static bool AddAnimations(cJSON* root, cJSON* accessors, cJSON* views, MT5Export_Buffer* buffer, const MT5Mesh* mesh,
    const MT5Export_Transform* rest, const MT5Export_Motions* motions) {
    uint32_t count = mesh->nodeCount, exported = 0;
    Matrix* world = malloc((count + 1) * sizeof(Matrix));
    if (world == NULL)
        return false;
    cJSON* animations = cJSON_CreateArray();
    bool success = true;
    for (uint32_t m = 0; m < motions->bank->count && success; m++) {
        MOTN_Motion motion;
        if (!MOTN_ReadMotion(motions->bank, m, &motion))
            continue;
        if (!MT5Pose_CanPose(&motion)) {
            MOTN_FreeMotion(&motion);
            continue;
        }
        uint32_t frames = motion.frames + 1;
        MT5Export_Transform* samples = malloc((size_t)frames * count * sizeof(MT5Export_Transform) + 1);
        float* times = malloc(frames * sizeof(float) + 1);
        bool posed = samples != NULL && times != NULL;
        success = posed;
        // The samples of each node, with the rotations on the same side as the previous one so that they interpolate
        // along the shortest arc
        for (uint32_t f = 0; f < frames && posed; f++) {
            times[f] = f / MOTN_FRAME_RATE;
            posed = MT5Pose_Compute(mesh, motions->body, &motion, (float)f, world);
            for (uint32_t i = 0; i < count && posed; i++) {
                MT5Export_Transform* sample = &samples[(size_t)i * frames + f];
                *sample = LocalTransform(mesh, world, i);
                Quaternion previous = f > 0 ? samples[(size_t)i * frames + f - 1].rotation : rest[i].rotation;
                if (QuatDot(previous, sample->rotation) < 0.0f)
                    sample->rotation = QuaternionScale(sample->rotation, -1.0f);
            }
        }
        if (posed) {
            float bounds[2] = { 0.0f, times[frames - 1] };
            int input = AddAccessor(accessors, views, buffer, times, frames * sizeof(float), frames, GLTF_FLOAT, "SCALAR", 0, bounds, 1);
            cJSON* animation = cJSON_CreateObject();
            cJSON_AddStringToObject(animation, "name", motions->bank->names[m]);
            cJSON_AddArrayToObject(animation, "channels");
            cJSON_AddArrayToObject(animation, "samplers");
            success = input >= 0;
            for (uint32_t i = 0; i < count && success; i++) {
                for (int path = 0; path < 3 && success; path++)
                    success = AddChannel(animation, accessors, views, buffer, &samples[(size_t)i * frames], frames, &rest[i], i, path, input, false);
            }
            // An animation needs a channel: one that doesn't move the body keeps the rotation of the first node
            if (success && cJSON_GetArraySize(cJSON_GetObjectItem(animation, "channels")) == 0)
                success = AddChannel(animation, accessors, views, buffer, samples, frames, &rest[0], 0, 1, input, true);
            cJSON_AddItemToArray(animations, animation);
            exported++;
        }
        free(samples);
        free(times);
        MOTN_FreeMotion(&motion);
    }
    free(world);
    if (cJSON_GetArraySize(animations) > 0)
        cJSON_AddItemToObject(root, "animations", animations);
    else
        cJSON_Delete(animations);
    printf("[MT5Export_Save]: %u of %u motions exported as animations\n", exported, motions->bank->count);
    return success;
}

// Adds an accessor of the vertex attribute at offset in each vertex. Returns its index, or -1 on memory error
static int AddAttribute(cJSON* accessors, cJSON* views, MT5Export_Buffer* buffer, const MT5Export_Primitive* primitive,
    size_t offset, size_t size, int componentType, const char* type, bool bounds) {
    uint8_t* data = malloc(primitive->vertexCount * size + 1);
    if (data == NULL)
        return -1;
    for (uint32_t i = 0; i < primitive->vertexCount; i++)
        memcpy(data + i * size, (const uint8_t*)&primitive->vertices[i] + offset, size);
    int view = AddBufferView(views, buffer, data, primitive->vertexCount * size, GLTF_ARRAY_BUFFER);
    free(data);
    if (view < 0)
        return -1;

    cJSON* accessor = cJSON_CreateObject();
    cJSON_AddNumberToObject(accessor, "bufferView", view);
    cJSON_AddNumberToObject(accessor, "componentType", componentType);
    if (componentType == GLTF_UNSIGNED_BYTE)
        cJSON_AddTrueToObject(accessor, "normalized");
    cJSON_AddNumberToObject(accessor, "count", primitive->vertexCount);
    cJSON_AddStringToObject(accessor, "type", type);
    if (bounds) {
        // The positions need their bounds
        float min[3] = { INFINITY, INFINITY, INFINITY }, max[3] = { -INFINITY, -INFINITY, -INFINITY };
        for (uint32_t i = 0; i < primitive->vertexCount; i++) {
            for (int c = 0; c < 3; c++) {
                min[c] = fminf(min[c], primitive->vertices[i].position[c]);
                max[c] = fmaxf(max[c], primitive->vertices[i].position[c]);
            }
        }
        cJSON_AddItemToObject(accessor, "min", cJSON_CreateFloatArray(min, 3));
        cJSON_AddItemToObject(accessor, "max", cJSON_CreateFloatArray(max, 3));
    }
    cJSON_AddItemToArray(accessors, accessor);
    return cJSON_GetArraySize(accessors) - 1;
}

static int AddIndices(cJSON* accessors, cJSON* views, MT5Export_Buffer* buffer, const MT5Export_Primitive* primitive) {
    bool small = primitive->vertexCount <= 0xFFFF;
    size_t size = small ? sizeof(uint16_t) : sizeof(uint32_t);
    uint8_t* data = malloc(primitive->indexCount * size + 1);
    if (data == NULL)
        return -1;
    for (uint32_t i = 0; i < primitive->indexCount; i++) {
        if (small)
            ((uint16_t*)data)[i] = (uint16_t)primitive->indices[i];
        else
            ((uint32_t*)data)[i] = primitive->indices[i];
    }
    int view = AddBufferView(views, buffer, data, primitive->indexCount * size, GLTF_ELEMENT_ARRAY_BUFFER);
    free(data);
    if (view < 0)
        return -1;

    cJSON* accessor = cJSON_CreateObject();
    cJSON_AddNumberToObject(accessor, "bufferView", view);
    cJSON_AddNumberToObject(accessor, "componentType", small ? GLTF_UNSIGNED_SHORT : GLTF_UNSIGNED_INT);
    cJSON_AddNumberToObject(accessor, "count", primitive->indexCount);
    cJSON_AddStringToObject(accessor, "type", "SCALAR");
    cJSON_AddItemToArray(accessors, accessor);
    return cJSON_GetArraySize(accessors) - 1;
}

static int WrapMode(uint8_t uvFlags, uint8_t flip, uint8_t clamp) {
    if (uvFlags & clamp)
        return GLTF_CLAMP_TO_EDGE;
    return (uvFlags & flip) ? GLTF_MIRRORED_REPEAT : GLTF_REPEAT;
}

// Index of the glTF texture of the image with the wrapping of the group, added the first time
static int GLTFTexture(cJSON* textures, cJSON* samplers, int* samplerOfFlags, int image, uint8_t uvFlags) {
    uvFlags &= MT5_UV_FLIP_U | MT5_UV_FLIP_V | MT5_UV_CLAMP_U | MT5_UV_CLAMP_V;
    if (samplerOfFlags[uvFlags] < 0) {
        cJSON* sampler = cJSON_CreateObject();
        cJSON_AddNumberToObject(sampler, "magFilter", GLTF_LINEAR);
        cJSON_AddNumberToObject(sampler, "minFilter", GLTF_LINEAR_MIPMAP_LINEAR);
        cJSON_AddNumberToObject(sampler, "wrapS", WrapMode(uvFlags, MT5_UV_FLIP_U, MT5_UV_CLAMP_U));
        cJSON_AddNumberToObject(sampler, "wrapT", WrapMode(uvFlags, MT5_UV_FLIP_V, MT5_UV_CLAMP_V));
        cJSON_AddItemToArray(samplers, sampler);
        samplerOfFlags[uvFlags] = cJSON_GetArraySize(samplers) - 1;
    }
    int sampler = samplerOfFlags[uvFlags];
    int count = cJSON_GetArraySize(textures);
    for (int i = 0; i < count; i++) {
        cJSON* texture = cJSON_GetArrayItem(textures, i);
        if (cJSON_GetObjectItem(texture, "source")->valueint == image && cJSON_GetObjectItem(texture, "sampler")->valueint == sampler)
            return i;
    }
    cJSON* texture = cJSON_CreateObject();
    cJSON_AddNumberToObject(texture, "sampler", sampler);
    cJSON_AddNumberToObject(texture, "source", image);
    cJSON_AddItemToArray(textures, texture);
    return count;
}

static cJSON* GLTFMaterial(uint32_t index, const MT5Export_Primitive* primitive, int texture) {
    const MT5Mesh_Group* group = primitive->group;
    cJSON* material = cJSON_CreateObject();
    cJSON_AddStringToObject(material, "name", TextFormat("material_%u_texture_%u", index, group->texture));
    cJSON* pbr = cJSON_AddObjectToObject(material, "pbrMetallicRoughness");
    if (texture >= 0)
        cJSON_AddNumberToObject(cJSON_AddObjectToObject(pbr, "baseColorTexture"), "index", texture);
    cJSON_AddNumberToObject(pbr, "metallicFactor", 0.0);
    cJSON_AddNumberToObject(pbr, "roughnessFactor", 1.0);
    if (primitive->alpha == MT5EXPORT_ALPHA_MASK) {
        cJSON_AddStringToObject(material, "alphaMode", "MASK");
        cJSON_AddNumberToObject(material, "alphaCutoff", 0.5);
    }
    else if (primitive->alpha == MT5EXPORT_ALPHA_BLEND)
        cJSON_AddStringToObject(material, "alphaMode", "BLEND");
    return material;
}

// The JSON of the glTF file, with the vertices, the indices and the images in buffer
static cJSON* BuildGLTF(const MT5Render_Model* model, const MT5Export_Model* exported, const MT5Export_Motions* motions, const char* name,
    MT5Export_Buffer* buffer) {
    cJSON* root = cJSON_CreateObject();
    cJSON* asset = cJSON_AddObjectToObject(root, "asset");
    cJSON_AddStringToObject(asset, "version", "2.0");
    cJSON_AddStringToObject(asset, "generator", "Shenmue");
    cJSON_AddNumberToObject(root, "scene", 0);
    cJSON* scene = cJSON_CreateObject();
    cJSON_AddItemToArray(cJSON_AddArrayToObject(root, "scenes"), scene);
    cJSON_AddItemToObject(scene, "nodes", cJSON_CreateIntArray((const int[]) { 0 }, 1));
    cJSON* nodes = cJSON_AddArrayToObject(root, "nodes");
    cJSON* node = cJSON_CreateObject();
    cJSON_AddItemToArray(nodes, node);
    cJSON_AddStringToObject(node, "name", name);
    cJSON_AddNumberToObject(node, "mesh", 0);
    cJSON* mesh = cJSON_CreateObject();
    cJSON_AddItemToArray(cJSON_AddArrayToObject(root, "meshes"), mesh);
    cJSON_AddStringToObject(mesh, "name", name);
    cJSON* primitives = cJSON_AddArrayToObject(mesh, "primitives");
    cJSON* materials = cJSON_AddArrayToObject(root, "materials");
    cJSON* textures = cJSON_CreateArray();
    cJSON* samplers = cJSON_CreateArray();
    cJSON* images = cJSON_CreateArray();
    cJSON* accessors = cJSON_AddArrayToObject(root, "accessors");
    cJSON* views = cJSON_AddArrayToObject(root, "bufferViews");
    int samplerOfFlags[16];
    for (int i = 0; i < 16; i++)
        samplerOfFlags[i] = -1;
    int* imageOfTexture = malloc((model->textureCount + 1) * sizeof(int));
    if (imageOfTexture == NULL) {
        cJSON_Delete(root);
        return NULL;
    }
    for (uint32_t i = 0; i < model->textureCount; i++)
        imageOfTexture[i] = -1;
    // The models of a single node have no skeleton
    const MT5Mesh* meshes = &model->mesh;
    bool skinned = meshes->nodeCount > 1;

    bool error = false;
    for (uint32_t i = 0; i < exported->count && !error; i++) {
        const MT5Export_Primitive* primitive = &exported->primitives[i];
        const MT5Mesh_Group* group = primitive->group;
        int texture = -1;
        if (HasTexture(exported, model, group)) {
            if (imageOfTexture[group->texture] < 0) {
                int size = 0;
                uint8_t* png = ExportImageToMemory(exported->images[group->texture], ".png", &size);
                int view = png != NULL ? AddBufferView(views, buffer, png, size, 0) : -1;
                MemFree(png);
                if (view < 0) {
                    error = true;
                    break;
                }
                cJSON* image = cJSON_CreateObject();
                cJSON_AddStringToObject(image, "name", TextFormat("texture_%u", group->texture));
                cJSON_AddNumberToObject(image, "bufferView", view);
                cJSON_AddStringToObject(image, "mimeType", "image/png");
                cJSON_AddItemToArray(images, image);
                imageOfTexture[group->texture] = cJSON_GetArraySize(images) - 1;
            }
            texture = GLTFTexture(textures, samplers, samplerOfFlags, imageOfTexture[group->texture], group->uvFlags);
        }
        cJSON_AddItemToArray(materials, GLTFMaterial(i, primitive, texture));

        int position = AddAttribute(accessors, views, buffer, primitive, offsetof(MT5Export_Vertex, position), 3 * sizeof(float), GLTF_FLOAT, "VEC3", true);
        int normal = AddAttribute(accessors, views, buffer, primitive, offsetof(MT5Export_Vertex, normal), 3 * sizeof(float), GLTF_FLOAT, "VEC3", false);
        int uv = AddAttribute(accessors, views, buffer, primitive, offsetof(MT5Export_Vertex, uv), 2 * sizeof(float), GLTF_FLOAT, "VEC2", false);
        int color = AddAttribute(accessors, views, buffer, primitive, offsetof(MT5Export_Vertex, color), 4, GLTF_UNSIGNED_BYTE, "VEC4", false);
        int indices = AddIndices(accessors, views, buffer, primitive);
        if (position < 0 || normal < 0 || uv < 0 || color < 0 || indices < 0) {
            error = true;
            break;
        }
        cJSON* item = cJSON_CreateObject();
        cJSON* attributes = cJSON_AddObjectToObject(item, "attributes");
        cJSON_AddNumberToObject(attributes, "POSITION", position);
        cJSON_AddNumberToObject(attributes, "NORMAL", normal);
        cJSON_AddNumberToObject(attributes, "TEXCOORD_0", uv);
        cJSON_AddNumberToObject(attributes, "COLOR_0", color);
        if (skinned && !AddSkinAttributes(attributes, accessors, views, buffer, primitive)) {
            cJSON_Delete(item);
            error = true;
            break;
        }
        cJSON_AddNumberToObject(item, "indices", indices);
        cJSON_AddNumberToObject(item, "material", i);
        cJSON_AddItemToArray(primitives, item);
    }
    free(imageOfTexture);

    // The skeleton in the pose of the file, and the motions
    MT5Export_Transform* rest = skinned && !error ? malloc((meshes->nodeCount + 1) * sizeof(MT5Export_Transform)) : NULL;
    if (skinned && !error) {
        error = rest == NULL;
        for (uint32_t i = 0; !error && i < meshes->nodeCount; i++)
            rest[i] = LocalTransform(meshes, meshes->pose, i);
        if (!error) {
            cJSON_AddItemToArray(cJSON_GetObjectItem(scene, "nodes"), cJSON_CreateNumber(1));
            error = !AddSkeleton(root, nodes, accessors, views, buffer, meshes, rest, name);
        }
        if (!error && motions != NULL)
            error = !AddAnimations(root, accessors, views, buffer, meshes, rest, motions);
    }
    free(rest);
    if (error) {
        cJSON_Delete(textures);
        cJSON_Delete(samplers);
        cJSON_Delete(images);
        cJSON_Delete(root);
        return NULL;
    }

    // The empty arrays are not allowed
    if (cJSON_GetArraySize(images) > 0) {
        cJSON_AddItemToObject(root, "textures", textures);
        cJSON_AddItemToObject(root, "samplers", samplers);
        cJSON_AddItemToObject(root, "images", images);
    }
    else {
        cJSON_Delete(textures);
        cJSON_Delete(samplers);
        cJSON_Delete(images);
    }
    cJSON* bufferItem = cJSON_CreateObject();
    cJSON_AddNumberToObject(bufferItem, "byteLength", (double)buffer->size);
    cJSON_AddItemToArray(cJSON_AddArrayToObject(root, "buffers"), bufferItem);
    return root;
}

static void WriteU32(FILE* file, uint32_t value) {
    uint8_t bytes[4] = { value & 0xFF, (value >> 8) & 0xFF, (value >> 16) & 0xFF, value >> 24 };
    fwrite(bytes, 1, 4, file);
}

static bool SaveGLB(const MT5Render_Model* model, const MT5Export_Model* exported, const MT5Export_Motions* motions, const char* filepath) {
    MT5Export_Buffer buffer = { 0 };
    char name[256];
    snprintf(name, sizeof(name), "%s", GetFileNameWithoutExt(filepath));
    cJSON* root = BuildGLTF(model, exported, motions, name, &buffer);
    char* json = root != NULL ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    if (json == NULL) {
        free(buffer.data);
        fputs("[MT5Export_Save]: Memory error\n", stderr);
        return false;
    }

    // Header, then the JSON chunk padded with spaces and the binary chunk padded with zeros, both to 4 bytes
    size_t jsonLength = strlen(json);
    size_t jsonPadded = (jsonLength + 3) & ~(size_t)3;
    size_t binPadded = (buffer.size + 3) & ~(size_t)3;
    size_t total = 12 + 8 + jsonPadded + 8 + binPadded;
    FILE* file = fopen(filepath, "wb");
    if (file == NULL) {
        fprintf(stderr, "[MT5Export_Save]: Error creating %s\n", filepath);
        cJSON_free(json);
        free(buffer.data);
        return false;
    }
    WriteU32(file, GLB_MAGIC);
    WriteU32(file, 2);
    WriteU32(file, (uint32_t)total);
    WriteU32(file, (uint32_t)jsonPadded);
    WriteU32(file, GLB_CHUNK_JSON);
    fwrite(json, 1, jsonLength, file);
    for (size_t i = jsonLength; i < jsonPadded; i++)
        fputc(' ', file);
    WriteU32(file, (uint32_t)binPadded);
    WriteU32(file, GLB_CHUNK_BIN);
    fwrite(buffer.data, 1, buffer.size, file);
    for (size_t i = buffer.size; i < binPadded; i++)
        fputc(0, file);
    bool success = ferror(file) == 0;
    success = fclose(file) == 0 && success;
    cJSON_free(json);
    free(buffer.data);
    if (!success)
        fprintf(stderr, "[MT5Export_Save]: Error writing %s\n", filepath);
    return success;
}

// Whether any vertex has a color other than white, which the OBJ file needs to write
static bool HasVertexColors(const MT5Export_Model* exported) {
    for (uint32_t i = 0; i < exported->count; i++) {
        const MT5Export_Primitive* primitive = &exported->primitives[i];
        for (uint32_t v = 0; v < primitive->vertexCount; v++) {
            const uint8_t* color = primitive->vertices[v].color;
            if (color[0] != 0xFF || color[1] != 0xFF || color[2] != 0xFF)
                return true;
        }
    }
    return false;
}

// The MTL file and the textures next to the OBJ file
static bool SaveMaterials(const MT5Render_Model* model, const MT5Export_Model* exported, const char* filepath, const char* mtlPath) {
    char folder[1024], name[256];
    snprintf(folder, sizeof(folder), "%s", GetDirectoryPath(filepath));
    snprintf(name, sizeof(name), "%s", GetFileNameWithoutExt(filepath));
    bool success = true;
    for (uint32_t i = 0; i < model->textureCount; i++) {
        char texturePath[1400];
        snprintf(texturePath, sizeof(texturePath), "%s/%s_%u.png", folder, name, i);
        if (exported->images[i].data != NULL && !ExportImage(exported->images[i], texturePath)) {
            fprintf(stderr, "[MT5Export_Save]: Error saving the texture %u\n", i);
            success = false;
        }
    }

    FILE* file = fopen(mtlPath, "w");
    if (file == NULL) {
        fprintf(stderr, "[MT5Export_Save]: Error creating %s\n", mtlPath);
        return false;
    }
    for (uint32_t i = 0; i < exported->count; i++) {
        const MT5Mesh_Group* group = exported->primitives[i].group;
        fprintf(file, "newmtl material_%u_texture_%u\n", i, group->texture);
        fputs("Ka 0 0 0\nKd 1 1 1\nKs 0 0 0\nd 1\nillum 1\n", file);
        if (HasTexture(exported, model, group)) {
            const char* clamp = (group->uvFlags & (MT5_UV_CLAMP_U | MT5_UV_CLAMP_V)) == (MT5_UV_CLAMP_U | MT5_UV_CLAMP_V) ? "-clamp on " : "";
            fprintf(file, "map_Kd %s%s_%u.png\n", clamp, name, group->texture);
            // The alpha of the texture, unless the triangles are opaque (see GroupAlpha)
            if (exported->primitives[i].alpha != MT5EXPORT_ALPHA_OPAQUE)
                fprintf(file, "map_d %s%s_%u.png\n", clamp, name, group->texture);
        }
        fputc('\n', file);
    }
    success = ferror(file) == 0 && success;
    return fclose(file) == 0 && success;
}

static bool SaveOBJ(const MT5Render_Model* model, const MT5Export_Model* exported, const char* filepath) {
    char mtlPath[1024];
    const char* extension = GetFileExtension(filepath);
    snprintf(mtlPath, sizeof(mtlPath), "%.*s.mtl", (int)(extension - filepath), filepath);
    bool success = SaveMaterials(model, exported, filepath, mtlPath);

    FILE* file = fopen(filepath, "w");
    if (file == NULL) {
        fprintf(stderr, "[MT5Export_Save]: Error creating %s\n", filepath);
        return false;
    }
    fprintf(file, "mtllib %s\n", GetFileName(mtlPath));
    fprintf(file, "o %s\n", GetFileNameWithoutExt(filepath));
    bool colors = HasVertexColors(exported);
    uint32_t base = 1;
    // The vertices of each group, which have the same index for their position, texture coordinates and normal
    for (uint32_t i = 0; i < exported->count; i++) {
        const MT5Export_Primitive* primitive = &exported->primitives[i];
        fprintf(file, "g group_%u\n", i);
        for (uint32_t v = 0; v < primitive->vertexCount; v++) {
            const MT5Export_Vertex* vertex = &primitive->vertices[v];
            fprintf(file, "v %.7g %.7g %.7g", vertex->position[0], vertex->position[1], vertex->position[2]);
            if (colors)
                fprintf(file, " %.4g %.4g %.4g", vertex->color[0] / 255.0f, vertex->color[1] / 255.0f, vertex->color[2] / 255.0f);
            fputc('\n', file);
        }
        // The rows of the texture coordinates of OBJ are from bottom to top
        for (uint32_t v = 0; v < primitive->vertexCount; v++)
            fprintf(file, "vt %.7g %.7g\n", primitive->vertices[v].uv[0], 1.0f - primitive->vertices[v].uv[1]);
        for (uint32_t v = 0; v < primitive->vertexCount; v++) {
            const float* normal = primitive->vertices[v].normal;
            fprintf(file, "vn %.6g %.6g %.6g\n", normal[0], normal[1], normal[2]);
        }
        fprintf(file, "usemtl material_%u_texture_%u\n", i, primitive->group->texture);
        for (uint32_t t = 0; t + 2 < primitive->indexCount; t += 3) {
            uint32_t a = base + primitive->indices[t], b = base + primitive->indices[t + 1], c = base + primitive->indices[t + 2];
            fprintf(file, "f %u/%u/%u %u/%u/%u %u/%u/%u\n", a, a, a, b, b, b, c, c, c);
        }
        base += primitive->vertexCount;
    }
    success = ferror(file) == 0 && success;
    success = fclose(file) == 0 && success;
    if (!success)
        fprintf(stderr, "[MT5Export_Save]: Error writing %s\n", filepath);
    return success;
}

bool MT5Export_Save(const MT5Render_Model* model, uint32_t frame, const MT5Export_Motions* motions, const char* filepath) {
    bool glb = IsFileExtension(filepath, ".glb");
    bool obj = IsFileExtension(filepath, ".obj");
    if (!glb && !obj) {
        fprintf(stderr, "[MT5Export_Save]: Unknown format of %s, it has to be glb or obj\n", filepath);
        return false;
    }
    MT5Export_Model exported;
    if (!BuildExport(model, frame, &exported)) {
        fputs("[MT5Export_Save]: Memory error\n", stderr);
        FreeExport(&exported, model->textureCount);
        return false;
    }
    if (exported.count == 0) {
        fputs("[MT5Export_Save]: The model has no triangles to export\n", stderr);
        FreeExport(&exported, model->textureCount);
        return false;
    }
    bool success = glb ? SaveGLB(model, &exported, motions, filepath) : SaveOBJ(model, &exported, filepath);
    FreeExport(&exported, model->textureCount);
    return success;
}
