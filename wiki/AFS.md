# AFS — File container

AFS is a simple archive format used by Sega games. It stores a list of files one after another, with a table of offsets and sizes at the start. It has no file names: in Shenmue the names come from the [IDX](IDX) file with the same name (`foo.afs` + `foo.idx`).

- **Extension:** `.afs`
- **Magic:** `AFS\0` (`41 46 53 00`)
- **Source:** [`src/afs.c`](https://github.com/EmulRyo/Shenmue/blob/master/src/afs.c)

## Layout

```
+---------------------+ 0x00
| Header              |
+---------------------+ 0x08
| Entry table         |  count × 8 bytes
+---------------------+
| ... (padding)       |
+---------------------+ entry[0].offset
| File 0 data         |
+---------------------+ entry[1].offset
| File 1 data         |
| ...                 |
+---------------------+
```

## Header

| Offset | Size | Type      | Name    | Description                 |
|-------:|-----:|-----------|---------|-----------------------------|
| 0x00   | 4    | char[4]   | `id`    | Magic `AFS\0`               |
| 0x04   | 4    | uint32    | `count` | Number of entries           |
| 0x08   | 8×N  | Entry[N]  | `entries` | Entry table (see below)   |

## Entry (8 bytes)

| Offset | Size | Type   | Name     | Description                                       |
|-------:|-----:|--------|----------|---------------------------------------------------|
| 0x00   | 4    | uint32 | `offset` | Position of the file data, from the start of the AFS |
| 0x04   | 4    | uint32 | `size`   | Size of the file data in bytes                   |

## Notes

- Entries with `size == 0` are empty and are skipped.
- Entries whose `offset + size` goes past the end of the file are skipped.
- The AFS format doesn't store the type of each file, so the extension of each extracted file is detected from its magic (see [Home](Home#magic-numbers)); unknown files are saved as `.bin`.
- Extracted files are named after the matching [IDX](IDX) entry, or by index (`0000.bin`, `0001.pvr`, ...) when there is no IDX file. Duplicated names get the entry index appended (`name_0012.ext`).
