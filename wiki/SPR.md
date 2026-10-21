# SPR — Sprite texture

An SPR file is a single `TEXN` block (the same one used by [PKF](PKF)) that wraps a [PVR](PVR) texture.

- **Extension:** `.spr`
- **Magic:** `TEXN` (`54 45 58 4E`), at offset 0x00 or 0x10
- **Source:** [`src/spr.c`](https://github.com/EmulRyo/Shenmue/blob/main/src/spr.c), [`src/spr.h`](https://github.com/EmulRyo/Shenmue/blob/main/src/spr.h)

## Layout

```
+---------------------+ 0x00
| Unknown header      |  optional, 16 bytes
+---------------------+ 0x00 / 0x10
| TEXN header         |  16 bytes
+---------------------+
| PVR texture         |
+---------------------+
```

## TEXN header (16 bytes)

| Offset | Size     | Type    | Name       | Description                                    |
|-------:|---------:|---------|------------|------------------------------------------------|
| 0x00   | 4        | char[4] | `id`       | `TEXN`                                         |
| 0x04   | 4        | uint32  | `size`     | Size of the block, including this header       |
| 0x08   | 8        | char[8] | `filename` | Texture name / identifier                      |
| 0x10   | size−16  | uint8[] | `pvrData`  | [PVR](PVR) texture (`GBIX`/`PVRT`)             |

## Notes

- When the file doesn't start with `TEXN`, the first 16 bytes are skipped and the `TEXN` block is expected at 0x10. The meaning of those 16 bytes is unknown.
