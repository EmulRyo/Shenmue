# PKF — Textures + IPAC package

A PKF file stores a list of textures followed by an [IPAC](IPAC) block with other files. They are usually found inside [AFS](AFS) containers (e.g. one per character, together with a [PKS](PKS)).

- **Extension:** `.pkf`
- **Magic:** `PAKF` (`50 41 4B 46`)
- **Compression:** the whole file can be [gzipped](Home#compression)
- **Source:** [`src/pkf.c`](https://github.com/EmulRyo/Shenmue/blob/master/src/pkf.c)

## Layout

```
+-----------------------+ 0x00
| Header (16 bytes)     |
+-----------------------+ 0x10
| Block (TEXN, DUMY...) |
| Block                 |
| ...                   |
| Empty block           |  (ends the list)
+-----------------------+ ipacOffset
| IPAC block            |
+-----------------------+
```

## Header (16 bytes)

| Offset | Size | Type    | Name           | Description                                     |
|-------:|-----:|---------|----------------|-------------------------------------------------|
| 0x00   | 4    | char[4] | `id`           | Magic `PAKF`                                    |
| 0x04   | 4    | uint32  | `ipacOffset`   | Position of the [IPAC](IPAC) block             |
| 0x08   | 4    | uint32  | `unknown`      | Unknown                                         |
| 0x0C   | 4    | uint32  | `textureCount` | Number of textures                              |

## Block header (8 bytes)

The area between the header and `ipacOffset` is a list of blocks. Each one starts with:

| Offset | Size | Type    | Name   | Description                                   |
|-------:|-----:|---------|--------|-----------------------------------------------|
| 0x00   | 4    | char[4] | `id`   | Block type: `TEXN`, `DUMY`...                 |
| 0x04   | 4    | uint32  | `size` | Size of the block, **including** this header  |

The list ends with a block whose `size` is smaller than 8 (an empty block) or at `ipacOffset`.

## TEXN block (texture)

| Offset | Size     | Type     | Name        | Description                                |
|-------:|---------:|----------|-------------|--------------------------------------------|
| 0x00   | 4        | char[4]  | `id`        | `TEXN`                                     |
| 0x04   | 4        | uint32   | `size`      | Size of the block, including the header    |
| 0x08   | 8        | uint8[8] | `textureID` | Texture identifier, referenced by the models |
| 0x10   | size−16  | uint8[]  | `data`      | Texture: [PVR](PVR) (`GBIX`/`PVRT`) or DDS  |

The same TEXN structure is used by [SPR](SPR) files.

## Notes

- Textures are extracted as `tex_<index>_<textureID in hex>.<ext>`, e.g. `tex_000_0123456789ABCDEF.pvr`.
- Other blocks (like `DUMY`) are listed but not extracted.
- Some files with the `.pkf` extension are actually [PKS](PKS) files; they are detected by their magic.
