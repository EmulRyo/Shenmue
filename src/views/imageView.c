#include <stdio.h>
#include <stdint.h>
#include <raylib.h>
#include "raygui.h"
#include "formats/texture/pvr.h"
#include "formats/texture/spr.h"
#include "ui/properties.h"
#include "ui/toolbar.h"
#include "ui/tree.h"
#include "views/imageView.h"

#define IMAGEVIEW_LIST_WIDTH    300.0f
#define IMAGEVIEW_MARGIN        10.0f

static Image g_image;
static Texture g_texture;
static char g_path[1024];

// The textures of the open SPR file, shown in a list when it has several
static struct {
    SPR_File    file;
    int         selected;   // -1 if none
    bool        failed;     // The selected one can't be decoded
    bool        expanded;
    Tree        tree;
    Properties  properties;
} g_spr = { .selected = -1, .expanded = true };

static void Unload(void) {
    if (g_image.data != NULL)
        UnloadImage(g_image);
    g_image = (Image) { 0 };
    if (g_texture.id > 0)
        UnloadTexture(g_texture);
    g_texture = (Texture) { 0 };
}

// Takes the ownership of pixels
static void Show(uint32_t* pixels, uint16_t width, uint16_t height) {
    Unload();
    g_image.data = pixels;
    g_image.width = width;
    g_image.height = height;
    g_image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    g_image.mipmaps = 1;

    float ar = (float)width / (float)height;
    // Draw the image on top of the checkered pattern
    Image img2 = GenImageChecked(width, height, width / 8, (int)(ar * height / 8), (Color) { 100, 100, 100, 255 }, (Color) { 150, 150, 150, 255 });
    ImageDrawImage(&img2, g_image, 0, 0, WHITE);
    g_texture = LoadTextureFromImage(img2);
    UnloadImage(img2);
}

static bool HasList(void) {
    return g_spr.file.count > 1;
}

// Shows the texture index of the SPR file; on error, the view stays open with nothing shown. reveal scrolls the list to it
static void SelectTexture(int index, bool reveal) {
    if (index < 0 || index >= (int)g_spr.file.count)
        return;
    g_spr.selected = index;
    uint32_t* data = NULL;
    uint16_t width = 0, height = 0;
    g_spr.failed = !SPR_Decode(&g_spr.file, (uint32_t)index, &width, &height, &data);
    if (g_spr.failed)
        Unload();
    else
        Show(data, width, height);
    if (reveal)
        Tree_Reveal(&g_spr.tree, true);
}

static void CloseSPR(void) {
    SPR_Close(&g_spr.file);
    g_spr.selected = -1;
    g_spr.failed = false;
    g_spr.tree = (Tree) { 0 };
    g_spr.properties = (Properties) { 0 };
}

bool ImageView_OpenPVR(const char* filepath) {
    uint32_t* data = NULL;
    uint16_t width = 0, height = 0;
    if (!PVR_Load(filepath, &width, &height, &data))
        return false;
    CloseSPR();
    Show(data, width, height);
    snprintf(g_path, sizeof(g_path), "%s", filepath);
    return true;
}

bool ImageView_OpenSPR(const char* filepath) {
    SPR_File file;
    if (!SPR_Open(filepath, &file))
        return false;
    // Without any texture that can be decoded, the file isn't opened
    uint32_t first = 0;
    while (first < file.count && !file.textures[first].pvr)
        first++;
    if (first == file.count) {
        SPR_Close(&file);
        return false;
    }
    CloseSPR();
    g_spr.file = file;
    SelectTexture((int)first, false);
    if (g_spr.failed && !HasList()) {
        CloseSPR();
        return false;
    }
    snprintf(g_path, sizeof(g_path), "%s", filepath);
    return true;
}

void ImageView_Close(void) {
    Unload();
    CloseSPR();
}

bool ImageView_IsOpen(void) {
    return g_texture.id > 0 || g_spr.file.count > 0;
}

bool ImageView_HasToolbar(void) {
    return HasList();
}

void ImageView_Toolbar(void) {
    if (!HasList())
        return;
    if (Toolbar_Button(ICON_ARROW_UP_FILL, NULL, "Previous texture", TOOLBAR_REPEAT | KEY_UP))
        SelectTexture(g_spr.selected - 1, true);
    if (Toolbar_Button(ICON_ARROW_DOWN_FILL, NULL, "Next texture", TOOLBAR_REPEAT | KEY_DOWN))
        SelectTexture(g_spr.selected + 1, true);
}

static void DrawList(Rectangle bounds) {
    Tree* tree = &g_spr.tree;
    int clicked = -1;
    Tree_Begin(tree, bounds);
    Tree_BeginGroup(tree, "Textures", &g_spr.expanded, NULL);
    for (uint32_t i = 0; i < g_spr.file.count; i++) {
        const SPR_Texture* texture = &g_spr.file.textures[i];
        bool selected = (int)i == g_spr.selected;
        int flags = (selected ? TREE_SELECTED : 0) | (!texture->pvr || (selected && g_spr.failed) ? TREE_FAILED : 0);
        const char* label = texture->pvr ? TextFormat("%u  %s  (%ux%u)", i, texture->name, texture->width, texture->height)
            : TextFormat("%u  %s", i, texture->name);
        if (Tree_Item(tree, label, flags, NULL))
            clicked = (int)i;
    }
    Tree_EndGroup(tree);
    Tree_End(tree);
    if (clicked >= 0 && clicked != g_spr.selected)
        SelectTexture(clicked, false);
}

static void DrawProperties(Rectangle bounds) {
    Properties* properties = &g_spr.properties;
    if (g_spr.selected < 0) {
        Properties_Begin(properties, bounds, NULL);
        Properties_Note(properties, "Select a texture");
        Properties_End(properties);
        return;
    }
    const SPR_Texture* texture = &g_spr.file.textures[g_spr.selected];
    Properties_Begin(properties, bounds, TextFormat("Texture %d", g_spr.selected));
    Properties_Text(properties, "Name", texture->name);
    Properties_Text(properties, "Offset", TextFormat("0x%X", texture->offset));
    Properties_Text(properties, "Size", TextFormat("0x%X", texture->size));
    if (!texture->pvr)
        Properties_Note(properties, "It has no PVR texture");
    else {
        Properties_Section(properties, "PVR");
        Properties_Int(properties, "Width", texture->width);
        Properties_Int(properties, "Height", texture->height);
        Properties_Text(properties, "Pixel format", TextFormat("%u (%s)", texture->pixelFormat, PVR_PixelFormatToString(texture->pixelFormat)));
        Properties_Text(properties, "Data format", TextFormat("%u (%s)", texture->dataFormat, PVR_DataFormatToString(texture->dataFormat)));
        if (g_spr.failed)
            Properties_Note(properties, "It can't be decoded");
    }
    Properties_End(properties);
}

static void DrawImage(Rectangle area) {
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

void ImageView_Draw(Rectangle area) {
    if (!HasList()) {
        DrawImage(area);
        return;
    }
    // The list of the textures at the left, the selected one at the right
    float margin = IMAGEVIEW_MARGIN;
    Rectangle column = { area.x + margin, area.y + margin, IMAGEVIEW_LIST_WIDTH, area.height - margin * 2.0f };
    Rectangle image = { column.x + column.width + margin, area.y + margin, area.width - column.width - margin * 3.0f, area.height - margin * 2.0f };
    if (column.height <= 0.0f || image.width <= 0.0f)
        return;
    Rectangle list, properties;
    Properties_Split(column, &list, &properties);
    DrawList(list);
    DrawProperties(properties);
    DrawImage(image);
}

bool ImageView_Export(const char* filepath) {
    if (g_image.data == NULL)
        return false;
    return ExportImage(g_image, filepath);
}

void ImageView_Status(ViewStatus* status) {
    snprintf(status->file, sizeof(status->file), "%s", g_path);
    if (HasList() && g_spr.selected >= 0) {
        const SPR_Texture* texture = &g_spr.file.textures[g_spr.selected];
        snprintf(status->info, sizeof(status->info), "%u textures, %d: %s, %dx%d pixels", g_spr.file.count, g_spr.selected, texture->name,
            g_image.width, g_image.height);
    }
    else
        snprintf(status->info, sizeof(status->info), "%dx%d pixels", g_image.width, g_image.height);
}
