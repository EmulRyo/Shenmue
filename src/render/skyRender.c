#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include "formats/texture/spr.h"
#include "render/skyRender.h"

// The direction of each pixel gives the point of the textures: its angle around the vertical axis (the game counts it
// from +Z of its coordinates, -Z of the scene) in quarter turns, and its height, with the offset of the map added to the
// angle from the horizon: v is 0 at the first row of the first texture, 5.625 degrees below the horizon, and 1 at its
// last row, 45 degrees higher (the textures are drawn upside down), then the band goes on over 1255.5 / 480 of that,
// repeated 9 times (the texture coordinates of its quad go to 9). The colors of the background go from the bottom one
// at v = 0 to the top one at v = 1, the top one kept above (unlike the game, which draws them only over the first
// texture: without it, the band or the color above show an edge). The decoded textures have their rows from the last
// one (as OpenGL takes them), so the texture coordinate is 1 - v
static const char* const g_fragmentShader =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "uniform sampler2D texture0;\n"
    "uniform sampler2D band;\n"
    "uniform int hasBand;\n"
    "uniform vec3 forward;\n"
    "uniform vec3 right;\n"
    "uniform vec3 up;\n"
    "uniform vec2 tangents;\n"
    "uniform vec2 rows;\n"
    "uniform vec3 below;\n"
    "uniform vec3 above;\n"
    "uniform vec4 backTop;\n"
    "uniform vec4 backBottom;\n"
    "uniform float offset;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec2 ndc = vec2(fragTexCoord.x * 2.0 - 1.0, 1.0 - fragTexCoord.y * 2.0);\n"
    "    vec3 d = normalize(forward + ndc.x * tangents.x * right + ndc.y * tangents.y * up);\n"
    "    float yaw = atan(d.x, -d.z);\n"
    "    float pitch = asin(clamp(d.y, -1.0, 1.0));\n"
    "    float u = 0.5 + yaw / 1.5707963;\n"
    "    float v = 0.125 + (pitch + offset) / 0.7853982;\n"
    "    vec3 color = below;\n"
    "    if (v >= 0.0 && v <= 1.0)\n"
    "        color = texture(texture0, vec2(u, 1.0 - clamp(v, 0.5 / rows.x, 1.0 - 0.5 / rows.x))).rgb;\n"
    "    else if (v > 1.0) {\n"
    "        float b = (v - 1.0) * 480.0 / 1255.5;\n"
    "        color = hasBand == 1 && b <= 1.0 ? texture(band, vec2(u, -b * 9.0)).rgb : above;\n"
    "    }\n"
    "    vec4 back = mix(backBottom, backTop, clamp(v, 0.0, 1.0));\n"
    "    color = mix(color, back.rgb, back.a);\n"
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
    sky->belowLoc = GetShaderLocation(sky->shader, "below");
    sky->aboveLoc = GetShaderLocation(sky->shader, "above");
    sky->backTopLoc = GetShaderLocation(sky->shader, "backTop");
    sky->backBottomLoc = GetShaderLocation(sky->shader, "backBottom");
    sky->bandLoc = GetShaderLocation(sky->shader, "band");
    sky->hasBandLoc = GetShaderLocation(sky->shader, "hasBand");
    sky->offsetLoc = GetShaderLocation(sky->shader, "offset");
}

void SkyRender_Unload(SkyRender* sky) {
    if (sky->texture.id > 0)
        UnloadTexture(sky->texture);
    if (sky->band.id > 0)
        UnloadTexture(sky->band);
    sky->texture = (Texture) { 0 };
    sky->band = (Texture) { 0 };
    sky->path[0] = '\0';
    sky->failed = false;
}

void SkyRender_Free(SkyRender* sky) {
    SkyRender_Unload(sky);
    if (sky->shader.id > 0 && sky->shader.id != rlGetShaderIdDefault())
        UnloadShader(sky->shader);
    memset(sky, 0, sizeof(*sky));
}

// The texture index of the file, with the colors of its first and its last pixels in the file (NULL to ignore them):
// the decoded rows start with the last one. id 0 if it can't be decoded
static Texture LoadSprTexture(const SPR_File* file, uint32_t index, float first[3], float last[3]) {
    uint32_t* pixels = NULL;
    uint16_t width = 0, height = 0;
    if (index >= file->count || !file->textures[index].pvr || !SPR_Decode(file, index, &width, &height, &pixels))
        return (Texture) { 0 };
    const uint8_t* firstPixel = (const uint8_t*)&pixels[(size_t)width * (height - 1)];
    const uint8_t* lastPixel = (const uint8_t*)&pixels[width - 1];
    for (int c = 0; c < 3; c++) {
        if (first != NULL)
            first[c] = firstPixel[c] / 255.0f;
        if (last != NULL)
            last[c] = lastPixel[c] / 255.0f;
    }
    Image image = { pixels, width, height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    Texture texture = LoadTextureFromImage(image);
    free(pixels);
    if (texture.id > 0) {
        SetTextureWrap(texture, TEXTURE_WRAP_REPEAT);
        SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
    }
    return texture;
}

bool SkyRender_Load(SkyRender* sky, const char* path) {
    if (strcmp(sky->path, path) == 0)
        return !sky->failed;
    SkyRender_Unload(sky);
    snprintf(sky->path, sizeof(sky->path), "%s", path);
    SPR_File file;
    if (SPR_Open(path, &file)) {
        uint32_t first = 0;
        while (first < file.count && !file.textures[first].pvr)
            first++;
        sky->texture = LoadSprTexture(&file, first, sky->below, sky->above);
        if (sky->texture.id > 0)
            sky->band = LoadSprTexture(&file, first + 1, NULL, sky->above);
        SPR_Close(&file);
    }
    sky->failed = sky->texture.id == 0;
    return !sky->failed;
}

// An ARGB color as red, green, blue and alpha
static void ColorValues(uint32_t color, float values[4]) {
    values[0] = ((color >> 16) & 0xFF) / 255.0f;
    values[1] = ((color >> 8) & 0xFF) / 255.0f;
    values[2] = (color & 0xFF) / 255.0f;
    values[3] = (color >> 24) / 255.0f;
}

void SkyRender_Draw(const SkyRender* sky, Camera3D camera, Rectangle area, float offset, uint32_t backTop, uint32_t backBottom) {
    if (sky->texture.id == 0 || sky->shader.id == 0 || area.width <= 0.0f || area.height <= 0.0f)
        return;
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
    Vector3 up = Vector3CrossProduct(right, forward);
    float tangentY = tanf(camera.fovy * 0.5f * DEG2RAD);
    float tangents[2] = { tangentY * area.width / area.height, tangentY };
    float rows[2] = { (float)sky->texture.height, sky->band.id > 0 ? (float)sky->band.height : 1.0f };
    int hasBand = sky->band.id > 0 ? 1 : 0;
    float top[4], bottom[4];
    ColorValues(backTop, top);
    ColorValues(backBottom, bottom);
    SetShaderValue(sky->shader, sky->forwardLoc, &forward, SHADER_UNIFORM_VEC3);
    SetShaderValue(sky->shader, sky->rightLoc, &right, SHADER_UNIFORM_VEC3);
    SetShaderValue(sky->shader, sky->upLoc, &up, SHADER_UNIFORM_VEC3);
    SetShaderValue(sky->shader, sky->tangentsLoc, tangents, SHADER_UNIFORM_VEC2);
    SetShaderValue(sky->shader, sky->rowsLoc, rows, SHADER_UNIFORM_VEC2);
    SetShaderValue(sky->shader, sky->belowLoc, sky->below, SHADER_UNIFORM_VEC3);
    SetShaderValue(sky->shader, sky->aboveLoc, sky->above, SHADER_UNIFORM_VEC3);
    SetShaderValue(sky->shader, sky->backTopLoc, top, SHADER_UNIFORM_VEC4);
    SetShaderValue(sky->shader, sky->backBottomLoc, bottom, SHADER_UNIFORM_VEC4);
    SetShaderValue(sky->shader, sky->hasBandLoc, &hasBand, SHADER_UNIFORM_INT);
    float radians = offset * DEG2RAD;
    SetShaderValue(sky->shader, sky->offsetLoc, &radians, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(sky->shader);
    if (hasBand)
        SetShaderValueTexture(sky->shader, sky->bandLoc, sky->band);
    DrawTexturePro(sky->texture, (Rectangle) { 0.0f, 0.0f, (float)sky->texture.width, (float)sky->texture.height }, area,
        (Vector2) { 0.0f, 0.0f }, 0.0f, WHITE);
    EndShaderMode();
}
