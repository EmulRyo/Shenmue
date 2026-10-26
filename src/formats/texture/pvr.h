#ifndef __PVR_H__
#define __PVR_H__

#include <stdint.h>

// Global Index
// This header is optional
typedef struct GBIX_Header {
	char		id[4]; // "GBIX"
	uint32_t	size;
	uint8_t     content[];
} GBIX_Header;

typedef struct PVR_Header {
    char		id[4]; // "PVRT"
    uint32_t	length;
    uint8_t     pixelFormat;
    uint8_t     dataFormat;
    uint16_t	unused;
    uint16_t    width;
    uint16_t    height;
    uint8_t     data[];
} PVR_Header;

// Pvr Pixel Formats
typedef enum PVR_PixelFormat {
	PVR_PF_ARGB1555        = 0x00, //Format consisting of one bit of alpha value and five bits of RGB values. The alpha value indicates transparent when it is 0 and opaque when it is 1.
	PVR_PF_RGB565          = 0x01, //Format without alpha value and consisting of five bits of RB values and six bits of G value.
	PVR_PF_ARGB4444        = 0x02, //Format consisting of four bits of alpha value and four bits of RGB values. The alpha value indicates completely transparent when it is 0x0 and completely opaque when it is 0xF.
	PVR_PF_YUV422          = 0x03, //YUV422 format
	PVR_PF_BUMP88          = 0x04, //Bump map with positiv only normal vectors (S and R direction angles)
	PVR_PF_RGB555          = 0x05, //for PCX compatible only
	PVR_PF_ARGB8888        = 0x06, //Palettize only!
	PVR_PF_DDS_DXT1_RGB24  = 0x80, //DDS DXT1 RGB, no transparency
	PVR_PF_DDS_DXT3_RGBA32 = 0x81, //DDS DXT3 RGBA, transparency
	PVR_PF_UNKNOWN         = 0xFF
} PVR_PixelFormat;

// Pvr Data Formats
typedef enum PVR_DataFormat {
    PVR_DF_RAW                                 = 0x00,
    PVR_DF_SQUARE_TWIDDLED                     = 0x01,
    PVR_DF_SQUARE_TWIDDLED_MIPMAP              = 0x02,
    PVR_DF_VECTOR_QUANTIZATION                 = 0x03,
    PVR_DF_VECTOR_QUANTIZATION_MIPMAP          = 0x04,
    PVR_DF_PALETTIZE_4BIT                      = 0x05,
    PVR_DF_PALETTIZE_4BIT_MIPMAP               = 0x06,
    PVR_DF_PALETTIZE_8BIT                      = 0x07,
    PVR_DF_PALETTIZE_8BIT_MIPMAP               = 0x08,
    PVR_DF_RECTANGLE                           = 0x09,
    PVR_DF_RECTANGLE_MIPMAP                    = 0x0A,
    PVR_DF_RECTANGLE_STRIDE                    = 0x0B,
    PVR_DF_RECTANGLE_STRIDE_MIPMAP             = 0x0C,
    PVR_DF_RECTANGLE_TWIDDLED                  = 0x0D,
    PVR_DF_BMP                                 = 0x0E,
    PVR_DF_BMP_MIPMAP                          = 0x0F,
    PVR_DF_VECTOR_QUANTIZATION_SMALL           = 0x10,
    PVR_DF_VECTOR_QUANTIZATION_SMALL_MIPMAP    = 0x11,
    PVR_DF_SQUARE_TWIDDLED_MIPMAP_ALT          = 0x12,
    PVR_DF_DDS                                 = 0x80,
    PVR_DF_DDS_2                               = 0x87,
    PVR_DF_UNKNOWN                             = 0xFF
} PVR_DataFormat;

bool PVR_Load(const char* filepath, uint16_t* width, uint16_t* height, uint32_t** pixels);
bool PVR_LoadFromMemory(uint8_t* buffer, uint16_t* width, uint16_t* height, uint32_t** pixels);

#endif