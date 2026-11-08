#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/bytes.h"
#include "formats/texture/dds.h"

// Flags of the header
#define DDSD_MIPMAPCOUNT    0x20000
// Flags of the pixel format
#define DDPF_ALPHAPIXELS    0x1
#define DDPF_FOURCC         0x4
#define DDPF_RGB            0x40
#define DDPF_LUMINANCE      0x20000
// Caps 2
#define DDSCAPS2_CUBEMAP    0x200
#define DDSCAPS2_FACES      0xFC00  // +X, -X, +Y, -Y, +Z, -Z
// Misc flag of the DX10 header
#define DDS_RESOURCE_MISC_TEXTURECUBE 0x4

// Size of the levels guessed for the data without header: from 1 to 4096 pixels
#define DDS_RAW_MAX_LOG2    12

const char* DDS_FormatToString(DDS_Format format) {
    switch (format) {
    case DDS_FORMAT_BC1: return "BC1";
    case DDS_FORMAT_BC2: return "BC2";
    case DDS_FORMAT_BC3: return "BC3";
    case DDS_FORMAT_RGB: return "Uncompressed";
    default:             return "UNKNOWN";
    }
}

static size_t BlockSize(DDS_Format format) {
    return format == DDS_FORMAT_BC1 ? 8 : 16;
}

static bool IsBlockFormat(DDS_Format format) {
    return format == DDS_FORMAT_BC1 || format == DDS_FORMAT_BC2 || format == DDS_FORMAT_BC3;
}

size_t DDS_LevelSize(const DDS_Info* info, uint32_t width, uint32_t height) {
    if (IsBlockFormat(info->format))
        return (size_t)((width + 3) / 4) * ((height + 3) / 4) * BlockSize(info->format);
    return (size_t)width * height * (info->bitCount / 8);
}

size_t DDS_ChainSize(const DDS_Info* info, uint32_t width, uint32_t height, uint32_t mipmaps) {
    size_t size = 0;
    for (uint32_t i = 0; i < mipmaps; i++) {
        size += DDS_LevelSize(info, width, height);
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }
    return size;
}

// The formats of the DX10 header: BC1-BC3 and the RGBA/BGRA ones of 8 bits per channel (also sRGB and typeless)
static DDS_Format FormatFromDXGI(uint32_t dxgi, DDS_Info* info) {
    switch (dxgi) {
    case 70: case 71: case 72: return DDS_FORMAT_BC1;
    case 73: case 74: case 75: return DDS_FORMAT_BC2;
    case 76: case 77: case 78: return DDS_FORMAT_BC3;
    case 27: case 28: case 29:
        info->bitCount = 32;
        info->masks[0] = 0x000000FF; info->masks[1] = 0x0000FF00; info->masks[2] = 0x00FF0000; info->masks[3] = 0xFF000000;
        return DDS_FORMAT_RGB;
    case 87: case 88: case 90: case 91: case 92: case 93:
        info->bitCount = 32;
        info->masks[0] = 0x00FF0000; info->masks[1] = 0x0000FF00; info->masks[2] = 0x000000FF;
        info->masks[3] = (dxgi == 88 || dxgi == 92 || dxgi == 93) ? 0 : 0xFF000000;
        return DDS_FORMAT_RGB;
    default: return DDS_FORMAT_UNKNOWN;
    }
}

bool DDS_ReadInfo(const uint8_t* data, size_t size, DDS_Info* info) {
    *info = (DDS_Info) { 0 };
    if (size < DDS_HEADER_SIZE || memcmp(data, "DDS ", 4) != 0)
        return false;
    info->flags = Bytes_ReadU32(data + 0x08);
    info->height = Bytes_ReadU32(data + 0x0C);
    info->width = Bytes_ReadU32(data + 0x10);
    uint32_t mipmaps = Bytes_ReadU32(data + 0x1C);
    info->mipmaps = (info->flags & DDSD_MIPMAPCOUNT) && mipmaps > 0 ? mipmaps : 1;
    uint32_t pfFlags = Bytes_ReadU32(data + 0x50);
    info->caps[0] = Bytes_ReadU32(data + 0x6C);
    info->caps[1] = Bytes_ReadU32(data + 0x70);
    info->dataOffset = DDS_HEADER_SIZE;
    if (info->width == 0 || info->height == 0 || info->width > 16384 || info->height > 16384 || info->mipmaps > 32) {
        fprintf(stderr, "[DDS_ReadInfo]: Invalid size %ux%u, %u mipmaps\n", info->width, info->height, info->mipmaps);
        return false;
    }

    uint32_t arraySize = 1;
    bool cube = (info->caps[1] & DDSCAPS2_CUBEMAP) != 0;
    if (pfFlags & DDPF_FOURCC) {
        memcpy(info->fourCC, data + 0x54, 4);
        if (memcmp(info->fourCC, "DXT1", 4) == 0)
            info->format = DDS_FORMAT_BC1;
        else if (memcmp(info->fourCC, "DXT2", 4) == 0 || memcmp(info->fourCC, "DXT3", 4) == 0)
            info->format = DDS_FORMAT_BC2;
        else if (memcmp(info->fourCC, "DXT4", 4) == 0 || memcmp(info->fourCC, "DXT5", 4) == 0)
            info->format = DDS_FORMAT_BC3;
        else if (memcmp(info->fourCC, "DX10", 4) == 0) {
            if (size < DDS_HEADER_SIZE + DDS_DX10_HEADER_SIZE)
                return false;
            const uint8_t* dx10 = data + DDS_HEADER_SIZE;
            info->dxgiFormat = Bytes_ReadU32(dx10);
            info->format = FormatFromDXGI(info->dxgiFormat, info);
            cube = cube || (Bytes_ReadU32(dx10 + 8) & DDS_RESOURCE_MISC_TEXTURECUBE) != 0;
            arraySize = Bytes_ReadU32(dx10 + 12) > 0 ? Bytes_ReadU32(dx10 + 12) : 1;
            info->dataOffset += DDS_DX10_HEADER_SIZE;
        }
    }
    else if (pfFlags & (DDPF_RGB | DDPF_LUMINANCE)) {
        info->format = DDS_FORMAT_RGB;
        info->bitCount = Bytes_ReadU32(data + 0x58);
        for (int i = 0; i < 4; i++)
            info->masks[i] = Bytes_ReadU32(data + 0x5C + i * 4);
        if (pfFlags & DDPF_LUMINANCE)
            info->masks[1] = info->masks[2] = info->masks[0];
        if (!(pfFlags & DDPF_ALPHAPIXELS))
            info->masks[3] = 0;
        if (info->bitCount != 8 && info->bitCount != 16 && info->bitCount != 24 && info->bitCount != 32)
            info->format = DDS_FORMAT_UNKNOWN;
    }
    if (info->format == DDS_FORMAT_UNKNOWN) {
        fprintf(stderr, "[DDS_ReadInfo]: Unsupported pixel format: flags 0x%X, FourCC %.4s, DXGI %u\n", pfFlags,
            data + 0x54, info->dxgiFormat);
        return false;
    }

    // The faces of a cube map are those of its flags (all of them with a DX10 header)
    info->faces = 1;
    if (cube) {
        info->cubeFaces = info->dxgiFormat != 0 ? 0x3F : (info->caps[1] & DDSCAPS2_FACES) >> 10;
        info->faces = 0;
        for (int i = 0; i < 6; i++)
            info->faces += (info->cubeFaces >> i) & 1;
        if (info->faces == 0) {
            info->cubeFaces = 0;
            info->faces = 1;
        }
    }
    info->dataSize = DDS_ChainSize(info, info->width, info->height, info->mipmaps) * info->faces * arraySize;
    if (info->dataOffset + DDS_LevelSize(info, info->width, info->height) > size) {
        fprintf(stderr, "[DDS_ReadInfo]: The data of the first level is missing\n");
        return false;
    }
    return true;
}

static uint32_t RGBA(uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    return r | (g << 8) | (b << 16) | (a << 24);
}

// The 4 colors of a BC1 block (also the color part of BC2/BC3, always with 4 colors)
static void BlockColors(const uint8_t* block, bool bc1, uint32_t colors[4]) {
    uint16_t c0 = Bytes_ReadU16(block), c1 = Bytes_ReadU16(block + 2);
    uint32_t r0 = ((c0 >> 11) & 0x1F) * 255 / 31, g0 = ((c0 >> 5) & 0x3F) * 255 / 63, b0 = (c0 & 0x1F) * 255 / 31;
    uint32_t r1 = ((c1 >> 11) & 0x1F) * 255 / 31, g1 = ((c1 >> 5) & 0x3F) * 255 / 63, b1 = (c1 & 0x1F) * 255 / 31;
    colors[0] = RGBA(r0, g0, b0, 255);
    colors[1] = RGBA(r1, g1, b1, 255);
    if (c0 > c1 || !bc1) {
        colors[2] = RGBA((2 * r0 + r1) / 3, (2 * g0 + g1) / 3, (2 * b0 + b1) / 3, 255);
        colors[3] = RGBA((r0 + 2 * r1) / 3, (g0 + 2 * g1) / 3, (b0 + 2 * b1) / 3, 255);
    }
    else {
        // 3 colors and a transparent black
        colors[2] = RGBA((r0 + r1) / 2, (g0 + g1) / 2, (b0 + b1) / 2, 255);
        colors[3] = 0;
    }
}

// The 16 alphas of a BC2 (4 bits each) or BC3 (interpolated) block
static void BlockAlphas(const uint8_t* block, DDS_Format format, uint8_t alphas[16]) {
    if (format == DDS_FORMAT_BC2) {
        for (int i = 0; i < 16; i++)
            alphas[i] = (uint8_t)(((block[i / 2] >> ((i & 1) * 4)) & 0xF) * 17);
        return;
    }
    uint32_t a0 = block[0], a1 = block[1];
    uint8_t palette[8] = { (uint8_t)a0, (uint8_t)a1 };
    if (a0 > a1) {
        for (int i = 0; i < 6; i++)
            palette[2 + i] = (uint8_t)(((6 - i) * a0 + (1 + i) * a1) / 7);
    }
    else {
        for (int i = 0; i < 4; i++)
            palette[2 + i] = (uint8_t)(((4 - i) * a0 + (1 + i) * a1) / 5);
        palette[6] = 0;
        palette[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; i++)
        bits |= (uint64_t)block[2 + i] << (8 * i);
    for (int i = 0; i < 16; i++)
        alphas[i] = palette[(bits >> (3 * i)) & 7];
}

void DDS_DecodeBlocks(const uint8_t* data, DDS_Format format, uint32_t width, uint32_t height, uint32_t* pixels) {
    size_t blockSize = BlockSize(format);
    for (uint32_t by = 0; by < (height + 3) / 4; by++) {
        for (uint32_t bx = 0; bx < (width + 3) / 4; bx++, data += blockSize) {
            const uint8_t* color = format == DDS_FORMAT_BC1 ? data : data + 8;
            uint32_t colors[4];
            BlockColors(color, format == DDS_FORMAT_BC1, colors);
            uint8_t alphas[16];
            if (format != DDS_FORMAT_BC1)
                BlockAlphas(data, format, alphas);
            uint32_t indices = Bytes_ReadU32(color + 4);
            for (uint32_t i = 0; i < 16; i++) {
                uint32_t x = bx * 4 + i % 4, y = by * 4 + i / 4;
                if (x >= width || y >= height)
                    continue;
                uint32_t pixel = colors[(indices >> (2 * i)) & 3];
                if (format != DDS_FORMAT_BC1)
                    pixel = (pixel & 0x00FFFFFF) | ((uint32_t)alphas[i] << 24);
                pixels[(size_t)y * width + x] = pixel;
            }
        }
    }
}

// The value of a channel of an uncompressed pixel scaled to 8 bits, def if it has no mask
static uint32_t Channel(uint32_t pixel, uint32_t mask, uint32_t def) {
    if (mask == 0)
        return def;
    int shift = 0;
    while (((mask >> shift) & 1) == 0)
        shift++;
    uint32_t max = mask >> shift;
    return (uint32_t)(((uint64_t)((pixel & mask) >> shift) * 255 + max / 2) / max);
}

static void DecodeUncompressed(const uint8_t* data, const DDS_Info* info, uint32_t width, uint32_t height, uint32_t* pixels) {
    uint32_t bytes = info->bitCount / 8;
    for (size_t i = 0; i < (size_t)width * height; i++, data += bytes) {
        uint32_t pixel = 0;
        for (uint32_t b = 0; b < bytes; b++)
            pixel |= (uint32_t)data[b] << (8 * b);
        pixels[i] = RGBA(Channel(pixel, info->masks[0], 0), Channel(pixel, info->masks[1], 0),
            Channel(pixel, info->masks[2], 0), Channel(pixel, info->masks[3], 255));
    }
}

static void DecodeLevel(const uint8_t* data, const DDS_Info* info, uint32_t width, uint32_t height, uint32_t* pixels) {
    if (IsBlockFormat(info->format)) {
        DDS_DecodeBlocks(data, info->format, width, height, pixels);
        return;
    }
    DecodeUncompressed(data, info, width, height, pixels);
    // Some cube maps with an alpha channel have it at 0 in all the pixels: it isn't transparency, so they are opaque
    size_t count = (size_t)width * height;
    for (size_t i = 0; i < count; i++) {
        if (pixels[i] >> 24)
            return;
    }
    for (size_t i = 0; i < count; i++)
        pixels[i] |= 0xFF000000u;
}

bool DDS_Decode(const uint8_t* data, size_t size, const DDS_Info* info, uint32_t* width, uint32_t* height, uint32_t** pixels) {
    *pixels = NULL;
    uint32_t w = info->width, h = info->height;
    size_t levelSize = DDS_LevelSize(info, w, h);
    if (info->dataOffset + levelSize > size)
        return false;
    if (info->cubeFaces == 0) {
        *pixels = (uint32_t*)malloc((size_t)w * h * sizeof(uint32_t));
        if (*pixels == NULL)
            return false;
        DecodeLevel(data + info->dataOffset, info, w, h, *pixels);
        *width = w;
        *height = h;
        return true;
    }

    // A horizontal cross:     +Y
    //                     -X  +Z  +X  -Z
    //                         -Y
    static const uint32_t column[6] = { 2, 0, 1, 1, 1, 3 }, row[6] = { 1, 1, 0, 2, 1, 1 };
    *width = w * 4;
    *height = h * 3;
    *pixels = (uint32_t*)calloc((size_t)*width * *height, sizeof(uint32_t));
    uint32_t* face = (uint32_t*)malloc((size_t)w * h * sizeof(uint32_t));
    if (*pixels == NULL || face == NULL) {
        free(*pixels);
        free(face);
        *pixels = NULL;
        return false;
    }
    size_t offset = info->dataOffset, chain = DDS_ChainSize(info, w, h, info->mipmaps);
    for (int i = 0; i < 6; i++) {
        if (!((info->cubeFaces >> i) & 1))
            continue;
        if (offset + levelSize > size) {
            fprintf(stderr, "[DDS_Decode]: The data of the face %d is missing\n", i);
            break;
        }
        DecodeLevel(data + offset, info, w, h, face);
        for (uint32_t y = 0; y < h; y++)
            memcpy(*pixels + (size_t)(row[i] * h + y) * *width + column[i] * w, face + (size_t)y * w, w * sizeof(uint32_t));
        offset += chain;
    }
    free(face);
    return true;
}

// How much the colors change across the edges of the blocks, relative to inside them: the blocks of a wrong width are
// placed in the wrong rows, and those of a wrong format are noise
static float Discontinuity(const uint32_t* pixels, uint32_t width, uint32_t height) {
    double edge = 0.0, inside = 0.0;
    size_t edges = 0, insides = 0;
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            const uint8_t* p = (const uint8_t*)&pixels[(size_t)y * width + x];
            for (int d = 0; d < 2; d++) {
                if ((d == 0 && x == 0) || (d == 1 && y == 0))
                    continue;
                const uint8_t* q = (const uint8_t*)&pixels[d == 0 ? (size_t)y * width + x - 1 : (size_t)(y - 1) * width + x];
                int diff = abs(p[0] - q[0]) + abs(p[1] - q[1]) + abs(p[2] - q[2]) + abs(p[3] - q[3]);
                if ((d == 0 ? x : y) % 4 == 0) {
                    edge += diff;
                    edges++;
                }
                else {
                    inside += diff;
                    insides++;
                }
            }
        }
    }
    return (float)((edge / (edges > 0 ? edges : 1) + 1.0) / (inside / (insides > 0 ? insides : 1) + 1.0));
}

uint32_t DDS_GuessRaw(const uint8_t* data, size_t size, DDS_Info* candidates, uint32_t max) {
    static const DDS_Format formats[] = { DDS_FORMAT_BC1, DDS_FORMAT_BC3 };
    uint32_t count = 0;
    float scores[64];
    if (max > 64)
        max = 64;
    // A full chain of mipmaps, down to 1x1; a single level if none fits
    for (int chain = 1; chain >= 0 && count == 0; chain--) {
        for (int f = 0; f < 2; f++) {
            for (int lw = 0; lw <= DDS_RAW_MAX_LOG2; lw++) {
                for (int lh = 0; lh <= DDS_RAW_MAX_LOG2; lh++) {
                    DDS_Info info = { .raw = true, .format = formats[f], .width = 1u << lw, .height = 1u << lh, .faces = 1 };
                    info.mipmaps = chain ? (uint32_t)(lw > lh ? lw : lh) + 1 : 1;
                    info.dataSize = DDS_ChainSize(&info, info.width, info.height, info.mipmaps);
                    if (info.dataSize != size || count == max)
                        continue;
                    uint32_t* pixels = (uint32_t*)malloc((size_t)info.width * info.height * sizeof(uint32_t));
                    if (pixels == NULL)
                        continue;
                    DDS_DecodeBlocks(data, info.format, info.width, info.height, pixels);
                    float score = Discontinuity(pixels, info.width, info.height);
                    free(pixels);
                    // Insertion by score
                    uint32_t i = count++;
                    for (; i > 0 && scores[i - 1] > score; i--) {
                        scores[i] = scores[i - 1];
                        candidates[i] = candidates[i - 1];
                    }
                    scores[i] = score;
                    candidates[i] = info;
                }
            }
        }
    }
    return count;
}
