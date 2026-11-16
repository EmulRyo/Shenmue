#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include "formats/texture/spr.h"
#include "render/skyRender.h"

// The direction of each pixel gives the point of the texture: its angle around the vertical axis (the game counts it from
// +Z of its coordinates, -Z of the scene) in quarter turns, and its height from the horizon in 45 degrees. Above and
// below the texture, the colors of its first and its last pixels
static const char* const g_fragmentShader =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec3 forward;\n"
    "uniform vec3 right;\n"
    "uniform vec3 up;\n"
    "uniform vec2 tangents;\n"
    "uniform float rows;\n"
    "uniform vec3 top;\n"
    "uniform vec3 bottom;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec2 ndc = vec2(fragTexCoord.x * 2.0 - 1.0, 1.0 - fragTexCoord.y * 2.0);\n"
    "    vec3 d = normalize(forward + ndc.x * tangents.x * right + ndc.y * tangents.y * up);\n"
    "    float yaw = atan(d.x, -d.z);\n"
    "    float pitch = asin(clamp(d.y, -1.0, 1.0));\n"
    "    float u = 0.5 + yaw / 1.5707963;\n"
    "    float v = 0.5 - pitch / 0.7853982;\n"
    "    vec3 color = v < 0.0 ? top : v > 1.0 ? bottom : texture(texture0, vec2(u, clamp(v, 0.5 / rows, 1.0 - 0.5 / rows))).rgb;\n"
    "    finalColor = vec4(color, 1.0);\n"
    "}\n";

void SkyRender_Init(SkyRender* sky) {
    memset(sky, 0, sizeof(*sky));
    sky->shader = LoadShaderFromMemory(NULL, g_fragmentShader);
    sky->forwardLoc = GetShaderLocation(sky->shader, "forward");
    sky->rightLoc = GetShaderLocation(sky->shader, "right");
    sky->upLoc = GetShaderLocation(sky->shader, "up");
    sky->tangentsLoc = GetShaderLocation(sky->shader, "tangents");
    sky->rowsLoc = GetShaderLocation(sky->shader, "rows");
    sky->topLoc = GetShaderLocation(sky->shader, "top");
    sky->bottomLoc = GetShaderLocation(sky->shader, "bottom");
}

void SkyRender_Unload(SkyRender* sky) {
    if (sky->texture.id > 0)
        UnloadTexture(sky->texture);
    sky->texture = (Texture) { 0 };
    sky->path[0] = '\0';
    sky->failed = false;
}

void SkyRender_Free(SkyRender* sky) {
    SkyRender_Unload(sky);
    if (sky->shader.id > 0 && sky->shader.id != rlGetShaderIdDefault())
        UnloadShader(sky->shader);
    memset(sky, 0, sizeof(*sky));
}

bool SkyRender_Load(SkyRender* sky, const char* path) {
    if (strcmp(sky->path, path) == 0)
        return !sky->failed;
    SkyRender_Unload(sky);
    snprintf(sky->path, sizeof(sky->path), "%s", path);
    SPR_File file;
    uint32_t* pixels = NULL;
    uint16_t width = 0, height = 0;
    bool ok = SPR_Open(path, &file);
    if (ok) {
        uint32_t first = 0;
        while (first < file.count && !file.textures[first].pvr)
            first++;
        ok = first < file.count && SPR_Decode(&file, first, &width, &height, &pixels);
        SPR_Close(&file);
    }
    if (ok) {
        const uint8_t* first = (const uint8_t*)pixels;
        const uint8_t* last = (const uint8_t*)&pixels[(size_t)width * height - 1];
        for (int c = 0; c < 3; c++) {
            sky->top[c] = first[c] / 255.0f;
            sky->bottom[c] = last[c] / 255.0f;
        }
        Image image = { pixels, width, height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        sky->texture = LoadTextureFromImage(image);
        free(pixels);
        ok = sky->texture.id > 0;
    }
    if (ok) {
        SetTextureWrap(sky->texture, TEXTURE_WRAP_REPEAT);
        SetTextureFilter(sky->texture, TEXTURE_FILTER_BILINEAR);
    }
    sky->failed = !ok;
    return ok;
}

void SkyRender_Draw(const SkyRender* sky, Camera3D camera, Rectangle area) {
    if (sky->texture.id == 0 || sky->shader.id == 0 || area.width <= 0.0f || area.height <= 0.0f)
        return;
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
    Vector3 up = Vector3CrossProduct(right, forward);
    float tangentY = tanf(camera.fovy * 0.5f * DEG2RAD);
    float tangents[2] = { tangentY * area.width / area.height, tangentY };
    float rows = (float)sky->texture.height;
    SetShaderValue(sky->shader, sky->forwardLoc, &forward, SHADER_UNIFORM_VEC3);
    SetShaderValue(sky->shader, sky->rightLoc, &right, SHADER_UNIFORM_VEC3);
    SetShaderValue(sky->shader, sky->upLoc, &up, SHADER_UNIFORM_VEC3);
    SetShaderValue(sky->shader, sky->tangentsLoc, tangents, SHADER_UNIFORM_VEC2);
    SetShaderValue(sky->shader, sky->rowsLoc, &rows, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sky->shader, sky->topLoc, sky->top, SHADER_UNIFORM_VEC3);
    SetShaderValue(sky->shader, sky->bottomLoc, sky->bottom, SHADER_UNIFORM_VEC3);
    BeginShaderMode(sky->shader);
    DrawTexturePro(sky->texture, (Rectangle) { 0.0f, 0.0f, (float)sky->texture.width, (float)sky->texture.height }, area,
        (Vector2) { 0.0f, 0.0f }, 0.0f, WHITE);
    EndShaderMode();
}
