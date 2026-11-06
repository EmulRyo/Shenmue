#ifndef __FON_H__
#define __FON_H__

#include <stdbool.h>
#include <stdint.h>

// Bitmap fonts (.fon): glyphs of the same size one after another, without a header. 1 bit per pixel, the first pixel in
// the least significant bit, rows without padding. The size of the glyphs and their encoding are given by the size of
// the file: 256 glyphs of 16x15 indexed by a single-byte code (ASCII and JIS X 0201 katakana), or glyphs of 24x15 in
// the order of the JIS X 0208 table

typedef enum {
    FON_ENCODING_BYTE,  // The index of a glyph is its single-byte code
    FON_ENCODING_JIS,   // JIS X 0208, 94 glyphs per row from 0x2121: index = (row - 1) * 94 + (cell - 1)
} FON_Encoding;

typedef struct {
    uint32_t        width;      // Of a glyph, in pixels
    uint32_t        height;
    uint32_t        glyphSize;  // Bytes of a glyph
    uint32_t        count;
    FON_Encoding    encoding;
    uint8_t*        data;
} FON_Font;

// Returns false if the file can't be read or its size isn't one of a font
bool FON_Load(const char* filepath, FON_Font* font);
void FON_Free(FON_Font* font);
// The pixel has ink
bool FON_Pixel(const FON_Font* font, uint32_t glyph, uint32_t x, uint32_t y);
// The glyph has no pixel with ink
bool FON_IsEmpty(const FON_Font* font, uint32_t glyph);
// The code of the glyph: the byte, or the JIS X 0208 code (e.g. 0x2121)
uint16_t FON_Code(const FON_Font* font, uint32_t glyph);
// The Shift-JIS code of a JIS X 0208 code
uint16_t FON_JISToShiftJIS(uint16_t jis);

#endif
