# AFS — File container

AFS is a simple archive format used by Sega games. It stores a list of files one after another, with a table of offsets and sizes at the start. It has no file names: in Shenmue the names come from the [IDX](IDX) file with the same name (`foo.afs` + `foo.idx`).

- **Extension:** `.afs`
- **Magic:** `AFS\0` (`41 46 53 00`)

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

- Entries with `size == 0` are empty.
- The AFS format doesn't store the name or the type of each file. The names are in the [IDX](IDX) file, and the type can be identified from the magic of the data (see [Home](Home#magic-numbers)).
