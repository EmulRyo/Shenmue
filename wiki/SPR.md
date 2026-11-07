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

## Notes

- 41 of the 267 `.spr` files and 19 of the 85 [GZ](GZ) files have several `TEXN` blocks: the menus, title screens and credits (e.g. `title/title3.spr`, 8 textures; `option*.gz`, 119 textures).
- 48 `.spr` and 13 GZ files end with 4 zero bytes after the last block, and 32 `.spr` files with 8.
- 150 textures of 16×16 (ARGB4444, square twiddled) have all their pixels set to `FFFF` (opaque white): 26 in each `option*.gz` (`child`, `voice_c`, `set_c`, `amode`...) and 4 in each `title4_*.gz`. Their meaning is unknown. `sprite/scroll98.spr`, `sprite/scroll99.spr` and `sprite/swbox.spr` also have a single-colour texture of 8×8.
- The `filename` is not reliable: in 37 `.spr` and 16 GZ files some names have their first bytes overwritten by other values (`\xD0\xC5ckgrou` for `backgrou`, `\xD4ption2_`), and some GZ files have two blocks with the same name (`retry_e` in `battle*.gz`).
