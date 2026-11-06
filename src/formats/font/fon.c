#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/file.h"
#include "formats/font/fon.h"

#define FON_JIS_CELLS 94

typedef struct {
    uint32_t        width;
    uint32_t        height;
    uint32_t        count;      // Glyphs of the files of this layout, 0 for any count
    FON_Encoding    encoding;
} FON_Layout;

static const FON_Layout g_layouts[] = {
    { 16, 15, 256, FON_ENCODING_BYTE },
    { 24, 15, 0,   FON_ENCODING_JIS },
};

#define FON_LAYOUT_COUNT (sizeof(g_layouts) / sizeof(g_layouts[0]))

bool FON_Load(const char* filepath, FON_Font* font) {
    memset(font, 0, sizeof(*font));
    size_t size = 0;
    uint8_t* data = File_LoadToMemoryEx(filepath, &size);
    if (data == NULL)
        return false;
    for (size_t i = 0; i < FON_LAYOUT_COUNT; i++) {
        const FON_Layout* layout = &g_layouts[i];
        uint32_t glyphSize = layout->width * layout->height / 8;
        bool match = layout->count != 0 ? size == (size_t)layout->count * glyphSize : size > 0 && size % glyphSize == 0;
        if (!match)
            continue;
        font->width = layout->width;
        font->height = layout->height;
        font->glyphSize = glyphSize;
        font->count = (uint32_t)(size / glyphSize);
        font->encoding = layout->encoding;
        font->data = data;
        return true;
    }
    fprintf(stderr, "[FON_Load]: The size of the file (%zu bytes) isn't one of a font: %s\n", size, filepath);
    free(data);
    return false;
}

void FON_Free(FON_Font* font) {
    free(font->data);
    memset(font, 0, sizeof(*font));
}

bool FON_Pixel(const FON_Font* font, uint32_t glyph, uint32_t x, uint32_t y) {
    uint32_t bit = y * font->width + x;
    return (font->data[(size_t)glyph * font->glyphSize + bit / 8] >> (bit % 8)) & 1;
}

bool FON_IsEmpty(const FON_Font* font, uint32_t glyph) {
    const uint8_t* bytes = font->data + (size_t)glyph * font->glyphSize;
    for (uint32_t i = 0; i < font->glyphSize; i++) {
        if (bytes[i] != 0)
            return false;
    }
    return true;
}

uint16_t FON_Code(const FON_Font* font, uint32_t glyph) {
    if (font->encoding == FON_ENCODING_BYTE)
        return (uint16_t)glyph;
    return (uint16_t)(((0x21 + glyph / FON_JIS_CELLS) << 8) | (0x21 + glyph % FON_JIS_CELLS));
}

uint16_t FON_JISToShiftJIS(uint16_t jis) {
    uint32_t j1 = jis >> 8, j2 = jis & 0xFF;
    uint32_t s1 = ((j1 + 1) >> 1) + (j1 <= 0x5E ? 0x70 : 0xB0);
    uint32_t s2;
    if (j1 & 1)
        s2 = j2 + (j2 >= 0x60 ? 0x20 : 0x1F);
    else
        s2 = j2 + 0x7E;
    return (uint16_t)((s1 << 8) | s2);
}
