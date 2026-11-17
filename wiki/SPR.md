# SPR — Sprite texture

An SPR file is a list of `TEXN` blocks (the same one used by [PKF](PKF)), each one wrapping a [PVR](PVR) texture. Most files have a single block.

- **Extension:** `.spr`
- **Magic:** `TEXN` (`54 45 58 4E`), or `SCRL` (`53 43 52 4C`) in the `scroll*.spr` files
- **Compression:** none in the `.spr` files; the [GZ](GZ) files are gzipped SPR files

## Layout

```
+---------------------+ 0x00
| TEXN block 0        |  TEXN header + PVR texture
+---------------------+
| TEXN block 1        |
+---------------------+
| ...                 |
+---------------------+
| Padding             |  optional, 4 or 8 zero bytes
+---------------------+
```

The `scroll*.spr` files wrap each `TEXN` block in an `SCLn` block, after an `SCRL` header:

```
+---------------------+ 0x00
| SCRL header         |  8 bytes
+---------------------+ 0x08
| SCL0 header         |  8 bytes
| TEXN block          |
+---------------------+
| SCL1 header         |  8 bytes
| TEXN block          |
+---------------------+
| Padding             |  8 zero bytes
+---------------------+
```

## TEXN block

| Offset | Size     | Type    | Name       | Description                                    |
|-------:|---------:|---------|------------|------------------------------------------------|
| 0x00   | 4        | char[4] | `id`       | `TEXN`                                         |
| 0x04   | 4        | uint32  | `size`     | Size of the block, including this header; the next block starts at the end of this one |
| 0x08   | 8        | char[8] | `filename` | Texture name / identifier (not reliable, see notes) |
| 0x10   | size−16  | uint8[] | `pvrData`  | [PVR](PVR) texture (`GBIX`/`PVRT`)             |

## SCRL header (8 bytes)

| Offset | Size | Type    | Name   | Description                                 |
|-------:|-----:|---------|--------|---------------------------------------------|
| 0x00   | 4    | char[4] | `id`   | `SCRL`                                      |
| 0x04   | 4    | uint32  | `size` | `8`: the size of this header only           |

## SCLn block

| Offset | Size     | Type    | Name   | Description                                           |
|-------:|---------:|---------|--------|-------------------------------------------------------|
| 0x00   | 4        | char[4] | `id`   | `SCL0`, `SCL1`...: the index of the block             |
| 0x04   | 4        | uint32  | `size` | Size of the block, including this header: 8 + the size of its `TEXN` block |
| 0x08   | size−8   | —       | `texn` | One `TEXN` block                                      |

The 7 `scroll*.spr` files (`sprite/scroll07.spr`, `scene/03/mend/scroll69.spr`...) have two `SCLn` blocks: a texture of 256×256 or 512×256 (`air07`) and a narrower one of the same width (`air07b`, 32 or 64 pixels high).

## Scroll files (skies)

The 31 `sprite/scroll*.spr` files and `scene/03/mend/scroll69.spr` are the skies of the maps (see [the sky](MAPINFO#sky)): their first texture, named `airNN` after the number of the file, is a panorama of 512×256 (or 256×256) pixels, RGB565, rectangle or twiddled. `scroll98.spr` and `scroll99.spr` have a texture of 8×8, white and black.

The game draws it behind the map as its screen of 640×480 pixels would show it (code at `0x0C09A31C` of `1ST_READ.BIN` PAL): the texture is repeated four times around the camera, each one stretched over 960 pixels, that is 90 degrees (the angle of the camera, `0x10000` for a turn, times 3840/65536), and over 480 pixels high, that is 45 degrees, with its middle row on the horizon; it moves up and down with the angle of the camera from the horizon. Above it, the screen is cleared with the color of its first pixel (the game keeps the color of the last one too). Over the texture, the game draws a quad of the same size with the 4 colors of the background at its corners (see `0x06F` in [MAPINFO](MAPINFO#functions-of-the-game)): a gradient from the top to the bottom, blended by the alpha of the colors.

| Files | Picture |
|-------|---------|
| `scroll00`, `scroll01`, `scroll02`, `scroll61` | Blue sky with white clouds (`scroll61` is the day of the weather) |
| `scroll23`, `scroll24`, `scroll28`, `scroll30` | Clouds: lilac, with rays of sun, white, white on blue |
| `scroll07`, `scroll10`, `scroll64`, `scroll65` | Grey clouds (`scroll64`, the rain of the weather) and overcast skies (`scroll65`) |
| `scroll03`, `scroll05`, `scroll06`, `scroll12`, `scroll13`, `scroll18`, `scroll29`, `scroll53`, `scroll62`, `scroll66`, `scroll67` | Sunsets: orange, golden, pink or red (`scroll66` is the sunset of the weather) |
| `scroll19`, `scroll60`, `scroll69` | Dawn and dusk: dark blue above, pink on the horizon (`scroll60`, the dawn of the weather) |
| `scroll25`, `scroll27`, `scroll63` | Night (`scroll63`, the night of the weather) |

The second texture of the files with two (`air07b`, `air30b`, `air64b`, `air65b`, `air67b`, `air69b`) is a band of clouds or stars of the same width, *probably* drawn above the first one, higher in the sky.

## Notes

- 41 of the 267 `.spr` files and 19 of the 85 [GZ](GZ) files have several `TEXN` blocks: the menus, title screens and credits (e.g. `title/title3.spr`, 8 textures; `option*.gz`, 119 textures).
- 48 `.spr` and 13 GZ files end with 4 zero bytes after the last block, and 32 `.spr` files with 8.
- 150 textures of 16×16 (ARGB4444, square twiddled) have all their pixels set to `FFFF` (opaque white): 26 in each `option*.gz` (`child`, `voice_c`, `set_c`, `amode`...) and 4 in each `title4_*.gz`. Their meaning is unknown. `sprite/scroll98.spr`, `sprite/scroll99.spr` and `sprite/swbox.spr` also have a single-colour texture of 8×8.
- The `filename` is not reliable: in 37 `.spr` and 16 GZ files some names have their first bytes overwritten by other values (`\xD0\xC5ckgrou` for `backgrou`, `\xD4ption2_`), and some GZ files have two blocks with the same name (`retry_e` in `battle*.gz`).
