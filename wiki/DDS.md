# DDS — DirectDraw Surface texture

DDS is the DirectX texture format, used by the PC version for the textures that it adds or replaces: the interface, the replacements of the textures of the models and the cube maps of the reflections. A DDS file is a header followed by the data of each face, each with its chain of mipmaps (each level half the size of the previous one).

The [PKF](PKF) files and the [MT5](MT5) models of the PC version also store some textures as DDS, inside their TEXN blocks.

- **Extension:** `.dds`, but also `.png` and `.tga` (see [Wrong extensions](#wrong-extensions))
- **Magic:** `DDS ` (`44 44 53 20`), except the [textures without header](#textures-without-header)

## Layout

```
+---------------------+ 0x00
| Magic "DDS "        |  4 bytes
+---------------------+ 0x04
| Header              |  124 bytes (pixel format at 0x4C)
+---------------------+ 0x80
| DX10 header         |  20 bytes, only if the FourCC is "DX10" (none in the game)
+---------------------+ 0x80
| Face 0: level 0     |
|         level 1     |
|         ...         |
+---------------------+
| Face 1 (cube maps)  |
| ...                 |
+---------------------+
| Trailer             |  36 bytes, only in some files (see below)
+---------------------+
```

## Header (0x80 bytes, with the magic)

| Offset | Size | Type     | Name                | Description                                                     |
|-------:|-----:|----------|---------------------|-----------------------------------------------------------------|
| 0x00   | 4    | char[4]  | `magic`             | `DDS `                                                          |
| 0x04   | 4    | uint32   | `size`              | Size of the header without the magic: 124 (0x7C)                |
| 0x08   | 4    | uint32   | `flags`             | Valid fields (see below)                                        |
| 0x0C   | 4    | uint32   | `height`            | Height in pixels                                                |
| 0x10   | 4    | uint32   | `width`             | Width in pixels                                                 |
| 0x14   | 4    | uint32   | `pitchOrLinearSize` | Compressed: size of the first level. Uncompressed: 0 in the game |
| 0x18   | 4    | uint32   | `depth`             | Depth of volume textures: always 0                              |
| 0x1C   | 4    | uint32   | `mipMapCount`       | Levels of each face, including the first one. Only valid with the flag `0x20000`: the files without it have 0 |
| 0x20   | 44   | uint32[11] | `reserved1`       | Unused. In 586 of the 822 files it ends with `NVTT` (`4E 56 54 54`) at 0x44 and the version `0x00020008` at 0x48: written by NVIDIA Texture Tools 2.0.8 |
| 0x4C   | 32   | struct   | `pixelFormat`       | [Pixel format](#pixel-format)                                   |
| 0x6C   | 4    | uint32   | `caps`              | `0x1000` texture, `0x8` complex (several surfaces), `0x400000` mipmaps, `0x2` alpha (only in cube maps) |
| 0x70   | 4    | uint32   | `caps2`             | `0x200` cube map, `0xFC00` its 6 faces (see [Cube maps](#cube-maps)) |
| 0x74   | 4    | uint32   | `caps3`             | Unused (0)                                                      |
| 0x78   | 4    | uint32   | `caps4`             | Unused (0)                                                      |
| 0x7C   | 4    | uint32   | `reserved2`         | Unused (0)                                                      |

### Flags

| Value     | Name        | Description                         |
|----------:|-------------|-------------------------------------|
| 0x1       | CAPS        | `caps` is valid                     |
| 0x2       | HEIGHT      | `height` is valid                   |
| 0x4       | WIDTH       | `width` is valid                    |
| 0x8       | PITCH       | `pitchOrLinearSize` is a pitch      |
| 0x1000    | PIXELFORMAT | `pixelFormat` is valid              |
| 0x20000   | MIPMAPCOUNT | `mipMapCount` is valid              |
| 0x80000   | LINEARSIZE  | `pitchOrLinearSize` is a linear size |
| 0x800000  | DEPTH       | `depth` is valid                    |

The game files use `0xA1007` (compressed, with mipmaps), `0x81007` (compressed, a single level) and `0x21007` (uncompressed cube maps, with mipmaps).

## Pixel format (0x20 bytes, at 0x4C)

| Offset | Size | Type    | Name          | Description                                            |
|-------:|-----:|---------|---------------|--------------------------------------------------------|
| 0x00   | 4    | uint32  | `size`        | 32 (0x20)                                              |
| 0x04   | 4    | uint32  | `flags`       | `0x1` has alpha, `0x4` compressed (FourCC), `0x40` uncompressed RGB, `0x20000` luminance |
| 0x08   | 4    | char[4] | `fourCC`      | Compression: `DXT1`, `DXT3`, `DXT5`, `DX10`... 0 if uncompressed |
| 0x0C   | 4    | uint32  | `rgbBitCount` | Bits per pixel of the uncompressed formats (32 in the game), 0 if compressed |
| 0x10   | 4    | uint32  | `rBitMask`    | Bits of the red channel in a pixel                     |
| 0x14   | 4    | uint32  | `gBitMask`    | Bits of the green channel                              |
| 0x18   | 4    | uint32  | `bBitMask`    | Bits of the blue channel                               |
| 0x1C   | 4    | uint32  | `aBitMask`    | Bits of the alpha channel                              |

Formats found in the game files:

| `flags` | `fourCC` | Bits | Masks R / G / B / A                         | Format                              |
|--------:|----------|-----:|---------------------------------------------|-------------------------------------|
| 0x4     | `DXT1`   | —    | —                                           | BC1, 8 bytes per block of 4×4 pixels |
| 0x4     | `DXT5`   | —    | —                                           | BC3, 16 bytes per block of 4×4 pixels |
| 0x41    | —        | 32   | `FF0000` / `FF00` / `FF` / `FF000000`       | B8G8R8A8: bytes B, G, R, A          |
| 0x40    | —        | 32   | `FF0000` / `FF00` / `FF` / 0                | B8G8R8X8: bytes B, G, R, unused     |

`DXT2`/`DXT3` (BC2, 4 bits of alpha per pixel) and `DXT4` are standard but not used in the game.

## DX10 header (0x14 bytes, at 0x80)

Only present when the FourCC is `DX10`; none of the game files has it.

| Offset | Size | Type   | Name                | Description                                           |
|-------:|-----:|--------|---------------------|-------------------------------------------------------|
| 0x00   | 4    | uint32 | `dxgiFormat`        | DXGI_FORMAT: 70–72 BC1, 73–75 BC2, 76–78 BC3, 28 R8G8B8A8, 87 B8G8R8A8... |
| 0x04   | 4    | uint32 | `resourceDimension` | 3 for 2D textures                                     |
| 0x08   | 4    | uint32 | `miscFlag`          | `0x4` cube map                                        |
| 0x0C   | 4    | uint32 | `arraySize`         | Number of textures of an array                        |
| 0x10   | 4    | uint32 | `miscFlags2`        | Alpha mode                                            |

## Data

The faces follow each other; each face is its levels, from the largest to 1×1 (or only the first one), without padding:

- **Compressed:** the level is divided in blocks of 4×4 pixels, stored by rows of blocks from the top left. A level of `w`×`h` pixels has `ceil(w/4) × ceil(h/4)` blocks: the levels smaller than 4 pixels still take a whole block.
- **Uncompressed:** `w × h` pixels from the top left, `rgbBitCount / 8` bytes each.

### BC1 block (DXT1, 8 bytes)

| Offset | Size | Type   | Name      | Description                                          |
|-------:|-----:|--------|-----------|------------------------------------------------------|
| 0x00   | 2    | uint16 | `color0`  | RGB565: 5 bits red (high), 6 green, 5 blue (low)     |
| 0x02   | 2    | uint16 | `color1`  | RGB565                                               |
| 0x04   | 4    | uint32 | `indices` | 2 bits per pixel, pixel 0 (top left) in the lowest bits, by rows |

If `color0 > color1` the 4 colors are `color0`, `color1`, `(2·color0 + color1) / 3` and `(color0 + 2·color1) / 3`. Otherwise they are `color0`, `color1`, `(color0 + color1) / 2` and transparent black.

### BC3 block (DXT5, 16 bytes)

| Offset | Size | Type     | Name           | Description                                         |
|-------:|-----:|----------|----------------|-----------------------------------------------------|
| 0x00   | 1    | uint8    | `alpha0`       | First alpha                                         |
| 0x01   | 1    | uint8    | `alpha1`       | Second alpha                                        |
| 0x02   | 6    | uint8[6] | `alphaIndices` | 3 bits per pixel (48 bits, little-endian), pixel 0 in the lowest bits |
| 0x08   | 8    | struct   | `color`        | A [BC1 block](#bc1-block-dxt1-8-bytes), always with 4 colors |

If `alpha0 > alpha1` the 8 alphas are `alpha0`, `alpha1` and 6 interpolated between them (`((6−i)·alpha0 + (1+i)·alpha1) / 7`). Otherwise they are `alpha0`, `alpha1`, 4 interpolated (`((4−i)·alpha0 + (1+i)·alpha1) / 5`), 0 and 255.

## Orientation

The rows of a level go from the top of the image to the bottom, but the files of `tex/assets/textureoverride/` (204 `.dds` and 7 `.png`) are stored **upside down**: their first row is the bottom of the image. They replace the textures of the [MT5](MT5) models, and they are the vertical flip of the original [PVR](PVR) textures of those models: e.g. `b01mp06g_0` (a street map of `scene/01/d000/b01mp06g.mt5`, whose texts read upside down) or `bikk3p0g_2` (`scene/01/jhd0/bikk3p0g.mt5`). The header doesn't say it: these files have the same flags as the others, and 7 of them have the `NVTT` signature of the interface textures.

The other textures are stored the right way up:

- `ui/`: the interface, with readable texts and the buttons of the controller.
- `modeloverride/`: the textures without header have the orientation of the original PVR textures of their models (e.g. `gacrrisg_0` to `_2`, `gacrgggg_0`), and so do their versions with a DDS header.
- The cube maps: the sky is in the +Y face and the ground in the −Y face.

Among the files with an unknown name, those with the `NVTT` signature are interface textures the right way up, but some of the 11 compressed ones without it are upside down like those of `textureoverride/` (maps of streets with upside-down texts, the buttons X, Y, A, B of the controller).

## Cube maps

The 12 uncompressed DDS files (with an unknown name) are cube maps: `caps2` is `0xFE00` (cube map with its 6 faces) and the faces are stored in the order +X, −X, +Y, −Y, +Z, −Z, each with all its mipmaps. Their faces are 32×32, 128×128, 512×512 or 1024×1024 pixels, and their size is exactly the header plus the 6 chains of levels.

In 6 of the 9 files with an alpha channel (`flags` 0x41) the alpha is 0 in all the pixels, and in the other 3 it is 255: it isn't transparency. The 3 files without alpha channel (0x40) also store a fourth byte in each pixel: 0 in two of them and 255 in the third.

## Trailer

12 BC1 files with a single level (9 in `tex/assets/modeloverride/`, 3 with an unknown name) have 36 bytes after their data, always the same: a uint32 `0x30`, 28 zero bytes and another uint32 `0x30`. Its meaning is unknown. The other files end with their data.

## Wrong extensions

In `tex/assets/` many DDS files don't have the extension `.dds`:

| Extension | Files | Contents                                                                 |
|-----------|------:|--------------------------------------------------------------------------|
| `.tga`    | 305   | DDS DXT5 with mipmaps (`ui/`)                                            |
| `.png`    | 159   | DDS DXT5 with mipmaps (152 in `ui/`, 7 in `textureoverride/`)            |
| `.png`    | 29    | [BC blocks without header](#textures-without-header) (`modeloverride/`)  |

None of the `.png` and `.tga` files of the game is a PNG or TGA image. The names end with 8 hexadecimal digits before the extension (`a_off.3cc1a28a.png`), which aren't the CRC32 of the file; the same name can appear with several extensions (`a_off.3cc1a28a.png` and `a_off.f9af2f10.tga`).

## Textures without header

The 29 `.png` files of `tex/assets/modeloverride/<model>/` have neither a magic nor a header: they are only the BC blocks of the texture, with all its mipmaps down to 1×1. Their format and size are given only by the size of the file, which is that of exactly one chain of levels:

| File size | Format | Size     | Files |
|----------:|--------|----------|------:|
| 56        | BC1    | 8×8      | 1     |
| 696       | BC1    | 32×32    | 3     |
| 1384      | BC1    | 64×32 or 32×64 | 2 (one of each) |
| 2744      | BC1    | 64×64    | 12    |
| 5480      | BC1    | 128×64   | 2     |
| 5488      | BC3    | 64×64    | 1     |
| 10936     | BC1    | 128×128  | 6     |
| 10960     | BC3    | 128×64   | 1     |
| 43704     | BC1    | 256×256  | 1     |

A size of `w`×`h` with mipmaps fits the same number of bytes as `h`×`w`, so the orientation of the textures that aren't square isn't given by the file; with the wrong one, the rows of blocks don't match their neighbours. For example, `gacrnt1i_7` and `gacrnt1i_8` have the same size, and they are 64×32 and 32×64, as their versions with a DDS header in `modeloverride/gacrnt1i/` show.

Each folder of a model has a file `.fbx` (that despite its extension starts with `12 98 EE 51` and isn't an FBX file) and the textures `<model>_<n>.<hash>.png`. Some folders have DDS versions of the same textures, with a single level and a header, in another folder (`gacrckng/` and `gacrckni/`).

## Statistics

Over the 851 DDS files of Shenmue I (822 with a header, 29 without):

| Format              | Mipmaps | Files |
|---------------------|---------|------:|
| BC3 (DXT5)          | Yes     | 751   |
| BC3 (DXT5)          | No      | 7     |
| BC1 (DXT1)          | Yes     | 19    |
| BC1 (DXT1)          | No      | 33    |
| B8G8R8A8 cube map   | Yes     | 9     |
| B8G8R8X8 cube map   | Yes     | 3     |
| BC1 without header  | Yes     | 27    |
| BC3 without header  | Yes     | 2     |

The sizes aren't always powers of two, mostly in the interface (1920×1080, 480×308, 324×324...) but also in `textureoverride/` (504×512).
