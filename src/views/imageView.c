#include <stdio.h>
#include <stdint.h>
#include <raylib.h>
#include "formats/texture/pvr.h"
#include "formats/texture/spr.h"
#include "views/imageView.h"

static Image g_image;
static Texture g_texture;

// Takes the ownership of pixels
static void Show(uint32_t* pixels, uint16_t width, uint16_t height) {
    if (g_image.data != NULL)
        UnloadImage(g_image);

    g_image.data = pixels;
    g_image.width = width;
    g_image.height = height;
    g_image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    g_image.mipmaps = 1;

    float ar = (float)width / (float)height;
    // Draw the image on top of the checkered pattern
    Image img2 = GenImageChecked(width, height, width / 8, (int)(ar * height / 8), (Color) { 100, 100, 100, 255 }, (Color) { 150, 150, 150, 255 });
    ImageDraw(&img2, g_image, (Rectangle) { 0, 0, width, height }, (Rectangle) { 0, 0, width, height }, WHITE);

    if (g_texture.id > 0)
        UnloadTexture(g_texture);

    g_texture = LoadTextureFromImage(img2);
    UnloadImage(img2);
}

bool ImageView_OpenPVR(const char* filepath) {
    uint32_t* data = NULL;
    uint16_t width = 0, height = 0;
    if (!PVR_Load(filepath, &width, &height, &data))
        return false;
    Show(data, width, height);
    return true;
}

bool ImageView_OpenSPR(const char* filepath) {
    uint32_t* data = NULL;
    uint16_t width = 0, height = 0;
    if (!SPR_Load(filepath, &width, &height, &data))
        return false;
    Show(data, width, height);
    return true;
}

void ImageView_Close(void) {
    if (g_image.data != NULL)
        UnloadImage(g_image);
    g_image = (Image) { 0 };
    if (g_texture.id > 0)
        UnloadTexture(g_texture);
    g_texture = (Texture) { 0 };
}

bool ImageView_IsOpen(void) {
    return g_texture.id > 0;
}

void ImageView_Draw(Rectangle area) {
    if (g_texture.id == 0)
        return;

    float ar = (float)g_texture.width / (float)g_texture.height;
    float height = area.height;
    float width = ar * height;

    if (width > area.width) {
        width = area.width;
        height = width / ar;
    }

    if (height > area.height) {
        height = area.height;
        width = ar * height;
    }

    float x = area.x + (area.width - width) / 2.0f;
    float y = area.y + (area.height - height) / 2.0f;

    DrawTexturePro(
        g_texture,
        (Rectangle) { 0, 0, (float)g_texture.width, (float)g_texture.height },
        (Rectangle) { x, y, width, height },
        (Vector2)   { 0, 0 },
        0,
        WHITE
    );
}

bool ImageView_Export(const char* filepath) {
    if (g_image.data == NULL)
        return false;
    return ExportImage(g_image, filepath);
}
