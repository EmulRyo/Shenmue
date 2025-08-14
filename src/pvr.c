#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "pvr.h"

static size_t MortonToX(size_t morton) {
	size_t x = 0;
	for (size_t bit = 0; bit < 16; bit++) {
		x |= ((morton >> (bit * 2)) & 1) << bit;
	}
	return x;
}

static size_t MortonToY(size_t morton) {
	size_t y = 0;
	for (size_t bit = 0; bit < 16; bit++) {
		y |= ((morton >> (bit * 2 + 1)) & 1) << bit;
	}
	return y;
}

static size_t Untwiddle(size_t i, size_t width) {
	size_t x = MortonToX(i);
	size_t y = MortonToY(i);
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
			fprintf(stderr, "[GetBpp]: Unsupported pixel format %d\n", pf);
			exit(5);
	}
}

static uint32_t PixelFormat16ToRGBA8888(uint16_t pixel, PVR_PixelFormat pf) {
	uint8_t r, g, b, a;
	switch (pf) {
		case PVR_PF_ARGB1555:
			a = ((pixel >> 15) & 0x01) * 255; // Alpha bit
			r = ((pixel >> 10) & 0x1F) * 8; // Blue
			g = ((pixel >>  5) & 0x1F) * 8; // Green
			b = ((pixel >>  0) & 0x1F) * 8; // Red
			break;
		case PVR_PF_RGB565:
			r = ((pixel >> 11) & 0x1F) * 8; // Red
			g = ((pixel >>  5) & 0x3F) * 4; // Green
			b =  (pixel        & 0x1F) * 8; // Blue
			a = 255;                        // No alpha in RGB565
			break;
		case PVR_PF_ARGB4444:
			a = ((pixel >> 12) & 0x0F) * 17; // Alpha
			r = ((pixel >>  8) & 0x0F) * 17; // Red
			g = ((pixel >>  4) & 0x0F) * 17; // Green
			b =  (pixel        & 0x0F) * 17; // Blue
			break;
		default:
			fprintf(stderr, "[FormatToRGBA8888]: Unsupported pixel format %d\n", pf);
			exit(6);
	}
	return (a << 24) | (b << 16) | (g << 8) | r;
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

		// index on the codebook
		uint8_t cbIdx = data[blockIdx];
		// pixels on this block
		const uint8_t* blockPixels = codebook + cbIdx * 4 * bpp;

		for (int py = 0; py < 2; py++) {
			for (int px = 0; px < 2; px++) {
				int dst_x = blockX * 2 + px;
				int dst_y = blockY * 2 + py;

				uint16_t pixel = *(uint16_t*)(blockPixels + (py * 2 + px) * bpp);
				uint32_t rgba = PixelFormat16ToRGBA8888(pixel, pvrHeader->pixelFormat);
				outPixels[(dst_y * pvrHeader->width + dst_x)] = rgba;
			}
		}
	}
}

bool PVR_Load(const char* filepath, uint16_t* width, uint16_t* height, uint32_t** pixels) {
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

	if (success) {
		return true;
	}
	else {
		free(*pixels);
		return false;
	}
}

bool PVR_LoadFromMemory(uint8_t* buffer, uint16_t* width, uint16_t* height, uint32_t** pixels) {
	GBIX_Header* gbixHeader = (GBIX_Header*)buffer;
	PVR_Header* pvrHeader = (PVR_Header*)(gbixHeader->content + gbixHeader->size);

	printf("[PVR_LoadFromMemory]: GBIX Header ID: %.4s, Size: %u\n", gbixHeader->id, gbixHeader->size);
	printf("[PVR_LoadFromMemory]: PVR Header ID: %.4s, Length: %u, Width: %u, Height: %u, Pixel Format: %u, Data Format: %u\n",
		pvrHeader->id, pvrHeader->length, pvrHeader->width, pvrHeader->height, pvrHeader->pixelFormat, pvrHeader->dataFormat);

	*pixels = malloc(pvrHeader->width * pvrHeader->height * sizeof(uint32_t));
	if (*pixels == NULL) {
		fputs("[PVR_LoadFromMemory]: Memory allocation error 2", stderr);
		exit(3);
	}

	*width = pvrHeader->width;
	*height = pvrHeader->height;

	size_t bpp = GetBpp(pvrHeader->pixelFormat);
	bool success = false;

	if (pvrHeader->dataFormat == PVR_DF_VECTOR_QUANTIZATION) {
		DecodeVQ(pvrHeader, *pixels);
		success = true;
	}
	else if (pvrHeader->dataFormat == PVR_DF_SQUARE_TWIDDLED || pvrHeader->dataFormat == PVR_DF_SQUARE_TWIDDLED_MIPMAP) {
		if (bpp == 2) {
			for (size_t i = 0; i < pvrHeader->width * pvrHeader->height; i++) {
				uint16_t pixel = *(uint16_t*)(pvrHeader->data + i * 2);
				uint32_t rgba = PixelFormat16ToRGBA8888(pixel, pvrHeader->pixelFormat);
				size_t index = Untwiddle(i, pvrHeader->width);
				(*pixels)[index] = rgba;
			}
			success = true;
		}
	}

	if (!success)
		fprintf(stderr, "[PVR_LoadFromMemory]: Unsupported pixel format %d or data format %d\n", pvrHeader->pixelFormat, pvrHeader->dataFormat);

	return success;
}