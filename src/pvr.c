#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "pvr.h"

static char* PVR_PixelFormatToString(PVR_PixelFormat pf) {
	switch (pf) {
		case PVR_PF_ARGB1555: return "ARGB1555";
		case PVR_PF_RGB565:   return "RGB565";
		case PVR_PF_ARGB4444: return "ARGB4444";
		case PVR_PF_YUV422:   return "YUV422";
		case PVR_PF_BUMP88:   return "BUMP88";
		case PVR_PF_RGB555:   return "RGB555";
		case PVR_PF_ARGB8888: return "ARGB8888";
		default:              return "UNKNOWN";
	}
}

static char* PVR_DataFormatToString(PVR_DataFormat df) {
	switch (df) {
		case PVR_DF_RAW:                             return "RAW";
		case PVR_DF_SQUARE_TWIDDLED:                 return "SQUARE_TWIDDLED";
		case PVR_DF_SQUARE_TWIDDLED_MIPMAP:          return "SQUARE_TWIDDLED_MIPMAP";
		case PVR_DF_VECTOR_QUANTIZATION:             return "VECTOR_QUANTIZATION";
		case PVR_DF_VECTOR_QUANTIZATION_MIPMAP:      return "VECTOR_QUANTIZATION_MIPMAP";
		case PVR_DF_PALETTIZE_4BIT:                  return "PALETTIZE_4BIT";
		case PVR_DF_PALETTIZE_4BIT_MIPMAP:           return "PALETTIZE_4BIT_MIPMAP";
		case PVR_DF_PALETTIZE_8BIT:                  return "PALETTIZE_8BIT";
		case PVR_DF_PALETTIZE_8BIT_MIPMAP:           return "PALETTIZE_8BIT_MIPMAP";
		case PVR_DF_RECTANGLE:                       return "RECTANGLE";
		case PVR_DF_RECTANGLE_MIPMAP:                return "RECTANGLE_MIPMAP";
		case PVR_DF_RECTANGLE_STRIDE:                return "RECTANGLE_STRIDE";
		case PVR_DF_RECTANGLE_STRIDE_MIPMAP:         return "RECTANGLE_STRIDE_MIPMAP";
		case PVR_DF_RECTANGLE_TWIDDLED:              return "RECTANGLE_TWIDDLED";
		case PVR_DF_BMP:                             return "BMP";
		case PVR_DF_BMP_MIPMAP:                      return "BMP_MIPMAP";
		case PVR_DF_VECTOR_QUANTIZATION_SMALL:       return "VECTOR_QUANTIZATION_SMALL";
		case PVR_DF_VECTOR_QUANTIZATION_SMALL_MIPMAP:return "VECTOR_QUANTIZATION_SMALL_MIPMAP";
		case PVR_DF_SQUARE_TWIDDLED_MIPMAP_ALT:      return "SQUARE_TWIDDLED_MIPMAP_ALT";
		default:                                     return "UNKNOWN";
	}
}

static size_t MortonToX(size_t morton) {
	size_t x = 0;
	for (size_t bit = 0; bit < 16; bit++) {
		x |= ((morton >> (bit * 2 + 1)) & 1) << bit;
	}
	return x;
}

static size_t MortonToY(size_t morton) {
	size_t y = 0;
	for (size_t bit = 0; bit < 16; bit++) {
		y |= ((morton >> (bit * 2)) & 1) << bit;
	}
	return y;
}

// Rectangular textures are a row (or column) of twiddled square blocks, the size of the shortest side
static size_t Untwiddle(size_t i, size_t width, size_t height) {
	size_t size = width < height ? width : height;
	size_t block = i / (size * size);
	size_t x = MortonToX(i % (size * size));
	size_t y = MortonToY(i % (size * size));
	if (width > height)
		x += block * size;
	else
		y += block * size;
	y = height - 1 - y; // Invert Y to match the image coordinate system
	assert(x < width && y < height && "Untwiddle coordinates out of bounds");
	assert(i < width * height && "Untwiddle index out of bounds");
	return y * width + x;
}

static size_t GetBpp(PVR_PixelFormat pf) {
	switch (pf) {
		case PVR_PF_ARGB1555:
		case PVR_PF_RGB565:
		case PVR_PF_ARGB4444:
		case PVR_PF_RGB555:
			return 2; // 16 bits per pixel
		case PVR_PF_ARGB8888:
			return 4; // 32 bits per pixel
		default:
			assert(false && "[GetBpp]: Unsupported pixel format");
			return 0; // Should never reach here
	}
}

static uint32_t PixelFormatToRGBA8888(const uint8_t* pixelData, PVR_PixelFormat pf) {
	uint8_t r, g, b, a;
	switch (pf) {
		case PVR_PF_ARGB1555: {
			uint16_t pixel = *(uint16_t*)pixelData;
			a = ((pixel >> 15) & 0x01) * 255; // Alpha bit
			r = ((pixel >> 10) & 0x1F) * 8; // Blue
			g = ((pixel >>  5) & 0x1F) * 8; // Green
			b = ((pixel >>  0) & 0x1F) * 8; // Red
			}
			break;
		case PVR_PF_RGB565: {
			uint16_t pixel = *(uint16_t*)pixelData;
			a = 255;                        // No alpha in RGB565
			r = ((pixel >> 11) & 0x1F) * 8; // Red
			g = ((pixel >>  5) & 0x3F) * 4; // Green
			b = ((pixel >>  0) & 0x1F) * 8; // Blue
			}
			break;
		case PVR_PF_ARGB4444: {
			uint16_t pixel = *(uint16_t*)pixelData;
			a = ((pixel >> 12) & 0x0F) * 17; // Alpha
			r = ((pixel >>  8) & 0x0F) * 17; // Red
			g = ((pixel >>  4) & 0x0F) * 17; // Green
			b = ((pixel >>  0) & 0x0F) * 17; // Blue
			}
			break;
		default:
			assert(false && "[PixelFormatToRGBA8888]: Unsupported pixel format");
			r = g = b = a = 0; // Should never reach here
	}
	return (a << 24) | (b << 16) | (g << 8) | r;
}

static bool IsFormatTwiddled(PVR_DataFormat df) {
	return df == PVR_DF_SQUARE_TWIDDLED || df == PVR_DF_SQUARE_TWIDDLED_MIPMAP ||
		   df == PVR_DF_RECTANGLE_TWIDDLED || df == PVR_DF_SQUARE_TWIDDLED_MIPMAP_ALT;
}

static void DebugCodebook(const PVR_Header* pvrHeader, uint32_t* outPixels) {
	size_t bpp = GetBpp(pvrHeader->pixelFormat);
	const uint8_t* codebook = pvrHeader->data;
	size_t dstX = 0;
	size_t dstY = 0;
	for (size_t i = 0; i < 256; i++) {
		const uint8_t* cbPixels = codebook + i * 4 * bpp;
		uint32_t rgba0 = PixelFormatToRGBA8888((cbPixels + 0*bpp), pvrHeader->pixelFormat);
		uint32_t rgba1 = PixelFormatToRGBA8888((cbPixels + 1*bpp), pvrHeader->pixelFormat);
		uint32_t rgba2 = PixelFormatToRGBA8888((cbPixels + 2*bpp), pvrHeader->pixelFormat);
		uint32_t rgba3 = PixelFormatToRGBA8888((cbPixels + 3*bpp), pvrHeader->pixelFormat);

		outPixels[((dstY + 0) * pvrHeader->width + dstX + 0)] = rgba0;
		outPixels[((dstY + 0) * pvrHeader->width + dstX + 1)] = rgba1;
		outPixels[((dstY + 1) * pvrHeader->width + dstX + 0)] = rgba2;
		outPixels[((dstY + 1) * pvrHeader->width + dstX + 1)] = rgba3;

		dstX += 2;
		if (dstX >= pvrHeader->width) {
			dstX = 0;
			dstY += 2;
		}
	}
}

/* 
 * Decode Vector Quantization
 * Codebook is 256 entries of 4 pixels each, like a palette of 2x2 pixels, at the start of the data.
 * After the codebook, there is a data array that contains indices into the codebook.
 * These indices are twiddled in a Morton order, therefore we need to untwiddle them.
 */
static void DecodeVQ(const PVR_Header* pvrHeader, uint32_t* outPixels) {
	size_t bpp = GetBpp(pvrHeader->pixelFormat);
	const uint8_t* codebook = pvrHeader->data;
	const uint8_t* data = pvrHeader->data + 256 * 4 * bpp;

	size_t blocksWidth = pvrHeader->width / 2;
	size_t blocksHeight = pvrHeader->height / 2;
	for (size_t blockIdx = 0; blockIdx < blocksWidth * blocksHeight; blockIdx++) {
		size_t blockX = MortonToX(blockIdx);
		size_t blockY = MortonToY(blockIdx);

		blockY = blocksHeight - 1 - blockY;

		// index on the codebook
		uint8_t cbIdx = data[blockIdx];
		// pixels on this block
		const uint8_t* blockPixels = codebook + cbIdx * 4 * bpp;

		uint32_t rgba0 = PixelFormatToRGBA8888((blockPixels + 0 * bpp), pvrHeader->pixelFormat);
		uint32_t rgba1 = PixelFormatToRGBA8888((blockPixels + 1 * bpp), pvrHeader->pixelFormat);
		uint32_t rgba2 = PixelFormatToRGBA8888((blockPixels + 2 * bpp), pvrHeader->pixelFormat);
		uint32_t rgba3 = PixelFormatToRGBA8888((blockPixels + 3 * bpp), pvrHeader->pixelFormat);

		size_t dstX = blockX * 2;
		size_t dstY = blockY * 2;
		outPixels[((dstY + 0) * pvrHeader->width + dstX + 0)] = rgba1;
		outPixels[((dstY + 0) * pvrHeader->width + dstX + 1)] = rgba3;
		outPixels[((dstY + 1) * pvrHeader->width + dstX + 0)] = rgba0;
		outPixels[((dstY + 1) * pvrHeader->width + dstX + 1)] = rgba2;
	}
}

bool PVR_Load(const char* filepath, uint16_t* width, uint16_t* height, uint32_t** pixels) {
	printf("[PVR_Load]: Loading PVR file: %s\n", filepath);
	FILE* file = fopen(filepath, "rb");
	if (file == NULL) {
		fputs("[PVR_Load] Open file error", stderr);
		exit(1);
	}
	// obtain file size:
	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	rewind(file);
	// allocate memory to contain the whole file:
	uint8_t* buffer = (uint8_t*)malloc(sizeof(uint8_t) * size);
	if (buffer == NULL) {
		fputs("[PVR_Load]: Memory allocation error", stderr);
		exit(2);
	}
	// copy the file into the buffer:
	size_t result = fread(buffer, sizeof(uint8_t), size, file);
	fclose(file);

	bool success = PVR_LoadFromMemory(buffer, width, height, pixels);

	free(buffer);

	return success;
}

bool PVR_LoadFromMemory(uint8_t* buffer, uint16_t* width, uint16_t* height, uint32_t** pixels) {
	GBIX_Header* gbixHeader = (GBIX_Header*)buffer;
	PVR_Header* pvrHeader = (PVR_Header*)buffer;
	if (strncmp(gbixHeader->id, "GBIX", 4) == 0) {
		printf("[PVR_LoadFromMemory]: GBIX Header ID: %.4s, Size: %u\n", gbixHeader->id, gbixHeader->size);
		pvrHeader = (PVR_Header*)(gbixHeader->content + gbixHeader->size);
	}

	assert(strncmp(pvrHeader->id, "PVRT", 4) == 0 && "id not valid");
	if (strncmp(pvrHeader->id, "PVRT", 4) != 0) {
		fprintf(stderr, "[PVR_LoadFromMemory]: Invalid PVR header ID: %.4s\n", pvrHeader->id);
		return false;
	}

	printf("[PVR_LoadFromMemory]: PVR Header ID: %.4s, Length: %u, Width: %u, Height: %u, Pixel Format: %u.%s, Data Format: %u.%s\n",
		pvrHeader->id, pvrHeader->length, pvrHeader->width, pvrHeader->height,
		pvrHeader->pixelFormat, PVR_PixelFormatToString(pvrHeader->pixelFormat),
		pvrHeader->dataFormat,  PVR_DataFormatToString(pvrHeader->dataFormat));

	size_t bpp = GetBpp(pvrHeader->pixelFormat);
	if (bpp == 0) {
		fprintf(stderr, "[PVR_LoadFromMemory]: Unsupported pixel format %d\n", pvrHeader->pixelFormat);
		return false;
	}

	*pixels = malloc(pvrHeader->width * pvrHeader->height * sizeof(uint32_t));
	if (*pixels == NULL) {
		fputs("[PVR_LoadFromMemory]: Memory allocation error 2", stderr);
		exit(3);
	}

	*width = pvrHeader->width;
	*height = pvrHeader->height;

	bool success = false;

	if (pvrHeader->dataFormat == PVR_DF_VECTOR_QUANTIZATION) {
		DecodeVQ(pvrHeader, *pixels);
		success = true;
	}
	else if (pvrHeader->dataFormat == PVR_DF_SQUARE_TWIDDLED || pvrHeader->dataFormat == PVR_DF_SQUARE_TWIDDLED_MIPMAP || pvrHeader->dataFormat == PVR_DF_RECTANGLE || pvrHeader->dataFormat == PVR_DF_RECTANGLE_TWIDDLED) {
		if (bpp == 2) {
			for (size_t i = 0; i < pvrHeader->width * pvrHeader->height; i++) {
				const uint8_t* pixel = pvrHeader->data + i * 2;
				uint32_t rgba = PixelFormatToRGBA8888(pixel, pvrHeader->pixelFormat);

				size_t x = i % pvrHeader->width;
				size_t y = i / pvrHeader->width;
				y = pvrHeader->height - 1 - y;
				size_t index = y * pvrHeader->width + x;
				//size_t index = i;
				if (IsFormatTwiddled(pvrHeader->dataFormat))
					index = Untwiddle(i, pvrHeader->width, pvrHeader->height);

				assert(index < pvrHeader->width * pvrHeader->height && "Index out of bounds");
				(*pixels)[index] = rgba;
			}
			success = true;
		}
	}

	if (!success) {
		free(*pixels);
		fprintf(stderr, "[PVR_LoadFromMemory]: Unsupported pixel format %d or data format %d\n", pvrHeader->pixelFormat, pvrHeader->dataFormat);
	}

	return success;
}
