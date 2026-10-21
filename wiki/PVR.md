# PVR — PowerVR texture

PVR is the texture format of the Dreamcast (PowerVR GPU). A PVR file is an optional `GBIX` header followed by a `PVRT` header and the image data.

- **Extension:** `.pvr`
- **Magic:** `GBIX` (`47 42 49 58`) or `PVRT` (`50 56 52 54`)
- **Source:** [`src/pvr.c`](https://github.com/EmulRyo/Shenmue/blob/main/src/pvr.c), [`src/pvr.h`](https://github.com/EmulRyo/Shenmue/blob/main/src/pvr.h)

## Layout

```
+---------------------+ 0x00
| GBIX header         |  optional, 8 + size bytes
+---------------------+
| PVRT header         |  16 bytes
+---------------------+
| Image data          |
+---------------------+
```

## GBIX header (optional, 8 + size bytes)

| Offset | Size   | Type     | Name      | Description                                  |
|-------:|-------:|----------|-----------|----------------------------------------------|
| 0x00   | 4      | char[4]  | `id`      | `GBIX` (global index)                        |
| 0x04   | 4      | uint32   | `size`    | Size of `content`, usually 8                 |
| 0x08   | `size` | uint8[]  | `content` | Global index of the texture (+ padding)      |

## PVRT header (16 bytes)

| Offset | Size | Type    | Name          | Description                                     |
|-------:|-----:|---------|---------------|-------------------------------------------------|
| 0x00   | 4    | char[4] | `id`          | `PVRT`                                          |
| 0x04   | 4    | uint32  | `length`      | Size of the data after this field               |
| 0x08   | 1    | uint8   | `pixelFormat` | Pixel format (see below)                        |
| 0x09   | 1    | uint8   | `dataFormat`  | Data format (see below)                         |
| 0x0A   | 2    | uint16  | `unused`      | Unused                                          |
| 0x0C   | 2    | uint16  | `width`       | Width in pixels                                 |
| 0x0E   | 2    | uint16  | `height`      | Height in pixels                                |
| 0x10   | —    | uint8[] | `data`        | Image data                                      |

### Pixel formats

| Value | Name        | Bits | Description                                         | Supported |
|------:|-------------|-----:|-----------------------------------------------------|:---------:|
| 0x00  | ARGB1555    | 16   | 1 bit alpha, 5 bits per RGB channel                 | ✅ |
| 0x01  | RGB565      | 16   | No alpha, 5 bits red, 6 bits green, 5 bits blue     | ✅ |
| 0x02  | ARGB4444    | 16   | 4 bits per channel                                  | ✅ |
| 0x03  | YUV422      | 16   | YUV 4:2:2                                           | ❌ |
| 0x04  | BUMP88      | 16   | Bump map                                            | ❌ |
| 0x05  | RGB555      | 16   | PCX compatible only                                 | ❌ |
| 0x06  | ARGB8888    | 32   | Palettes only                                       | ❌ |
| 0x80  | DDS DXT1    | —    | DDS DXT1 RGB, no transparency (PC version)          | ❌ |
| 0x81  | DDS DXT3    | —    | DDS DXT3 RGBA (PC version)                          | ❌ |

### Data formats

| Value | Name                             | Supported |
|------:|----------------------------------|:---------:|
| 0x00  | RAW                              | ❌ |
| 0x01  | SQUARE_TWIDDLED                  | ✅ |
| 0x02  | SQUARE_TWIDDLED_MIPMAP           | ✅ |
| 0x03  | VECTOR_QUANTIZATION              | ✅ |
| 0x04  | VECTOR_QUANTIZATION_MIPMAP       | ❌ |
| 0x05  | PALETTIZE_4BIT                   | ❌ |
| 0x06  | PALETTIZE_4BIT_MIPMAP            | ❌ |
| 0x07  | PALETTIZE_8BIT                   | ❌ |
| 0x08  | PALETTIZE_8BIT_MIPMAP            | ❌ |
| 0x09  | RECTANGLE                        | ✅ |
| 0x0A  | RECTANGLE_MIPMAP                 | ❌ |
| 0x0B  | RECTANGLE_STRIDE                 | ❌ |
| 0x0C  | RECTANGLE_STRIDE_MIPMAP          | ❌ |
| 0x0D  | RECTANGLE_TWIDDLED               | ✅ |
| 0x0E  | BMP                              | ❌ |
| 0x0F  | BMP_MIPMAP                       | ❌ |
| 0x10  | VECTOR_QUANTIZATION_SMALL        | ❌ |
| 0x11  | VECTOR_QUANTIZATION_SMALL_MIPMAP | ❌ |
| 0x12  | SQUARE_TWIDDLED_MIPMAP_ALT       | ❌ |
| 0x80  | DDS                              | ❌ |
| 0x87  | DDS_2                            | ❌ |

## Image data

### Twiddled

The pixels are stored in Morton order (Z-order): the bits of the index alternate between Y (even bits) and X (odd bits). For rectangular textures the image is split in squares of `height × height` placed side by side.

### Vector quantization (VQ)

| Size                         | Description                                                          |
|------------------------------|----------------------------------------------------------------------|
| 256 × 4 × 2 = 2048 bytes     | Codebook: 256 entries of 2×2 pixels (16-bit pixels)                  |
| (width/2) × (height/2) bytes | One codebook index per 2×2 block, in twiddled order                  |

The 4 pixels of a codebook entry are stored in twiddled order: top-left, bottom-left, top-right, bottom-right.

## Notes

- Mipmapped formats store the smaller mipmaps before the full size image. The decoder doesn't skip them yet, so mipmapped textures may not be decoded correctly.
- PVR textures are also found inside `TEXN` blocks of [PKF](PKF) and [SPR](SPR) files.
