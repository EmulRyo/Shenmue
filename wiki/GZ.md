# GZ — Gzipped sprite texture

A GZ file is an [SPR](SPR) file compressed with gzip (RFC 1952), the same compression used inside other formats (see [Compression](Home#compression)).

- **Extension:** `.gz`
- **Magic:** `1F 8B 08` (gzip, deflate method)
- **Content:** always an [SPR](SPR) file (one or more `TEXN` blocks) in the game files

The game has 85 GZ files: the 5 `battle*.gz` of `misc` (texts of the battle menus) and 80 in `sprite` (loading screens `ld*.gz`, `loadnum.gz`, `option*.gz`, `staff*.gz` and `title4_*.gz`). They compress to 1–15% of their size, from 6 KB to 4 MB uncompressed.

## Layout

```
+---------------------+ 0x00
| Header              |  10 bytes
+---------------------+ 0x0A
| Original filename   |  null-terminated string
+---------------------+
| Deflate stream      |  compressed SPR
+---------------------+ size − 8
| Trailer             |  8 bytes
+---------------------+
```

## Header (10 bytes)

| Offset | Size | Type    | Name    | Description                                                         |
|-------:|-----:|---------|---------|---------------------------------------------------------------------|
| 0x00   | 2    | uint8[2]| `id`    | `1F 8B`                                                             |
| 0x02   | 1    | uint8   | `cm`    | Compression method: `08` (deflate)                                  |
| 0x03   | 1    | uint8   | `flags` | `08` (FNAME) in all the game files: only the original filename follows the header |
| 0x04   | 4    | uint32  | `mtime` | Modification time of the original file (Unix time), between October 1999 and August 2000 |
| 0x08   | 1    | uint8   | `xfl`   | Extra flags: `00`                                                   |
| 0x09   | 1    | uint8   | `os`    | Operating system: `03` (Unix)                                       |

## Original filename

| Offset | Size     | Type   | Name    | Description                                     |
|-------:|---------:|--------|---------|-------------------------------------------------|
| 0x0A   | variable | char[] | `fname` | Uppercase name of the compressed file, null-terminated |

The name is not reliable:

- 59 files store the name without extension (`LDHAZUKI`) and 26 with `.SPR` (`BATTLE_F.SPR`); `title4_e.gz` stores `TITLE4_E.GZ.SPR`.
- Some localized files keep the name of the file they were copied from: `battle_e.gz` stores `BATTLE.SPR`, `option_e.gz` stores `OPTION.SPR`, `staff_a.gz` and `staff_s.gz` store `STAFF.SPR`.

## Deflate stream

Raw deflate data (RFC 1951) from the end of the filename to the trailer.

## Trailer (8 bytes)

| Offset   | Size | Type   | Name    | Description                                  |
|---------:|-----:|--------|---------|----------------------------------------------|
| size − 8 | 4    | uint32 | `crc32` | CRC-32 of the uncompressed data              |
| size − 4 | 4    | uint32 | `isize` | Size of the uncompressed data                |
