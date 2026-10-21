# TAD / TAC — Archive of the PC version

The PC version (Shenmue I & II) stores its data in pairs of files: the **TAD** file is the index and the **TAC** file holds the data. Both have the same name (`foo.tad` + `foo.tac`).

- **Extensions:** `.tad` (index), `.tac` (data)
- **Magic:** none
- **Source:** [`src/tad.c`](https://github.com/EmulRyo/Shenmue/blob/main/src/tad.c), [`src/tac.c`](https://github.com/EmulRyo/Shenmue/blob/main/src/tac.c)

## TAD layout

```
+---------------------+ 0x00
| Header (60 bytes)   |
+---------------------+ 0x3C
| Entry table         |  fileCount × 32 bytes
+---------------------+
```

## TAD header (60 bytes)

| Offset | Size | Type      | Name            | Description                                   |
|-------:|-----:|-----------|-----------------|-----------------------------------------------|
| 0x00   | 16   | uint32[4] | `identifier`    | Identifier / version                          |
| 0x10   | 8    | int64     | `timestamp`     | Creation date (Unix time, seconds)            |
| 0x18   | 8    | char[8]   | `graphicsAPI`   | Graphics API (e.g. `DX11`)                    |
| 0x20   | 4    | uint32    | `checksum`      | Checksum                                      |
| 0x24   | 4    | uint32    | `unknown`       | Unknown                                       |
| 0x28   | 8    | uint64    | `tacFileSize`   | Size of the TAC file in bytes                 |
| 0x30   | 8    | uint64    | `fileCount`     | Number of entries                             |
| 0x38   | 4    | uint32    | `fileCountCopy` | Copy of `fileCount`                           |
| 0x3C   | 32×N | Entry[N]  | `entries`       | Entry table (see below)                       |

## TAD entry (32 bytes)

| Offset | Size | Type      | Name       | Description                                          |
|-------:|-----:|-----------|------------|------------------------------------------------------|
| 0x00   | 12   | uint32[3] | `hash`     | `hash[0]` is the hash of the file path; the other two are unknown |
| 0x0C   | 4    | uint32    | `empty`    | Always 0                                             |
| 0x10   | 4    | uint32    | `offset`   | Position of the file data in the TAC file            |
| 0x14   | 4    | uint32    | `unknown1` | Unknown (possibly the high 32 bits of `offset`)      |
| 0x18   | 4    | uint32    | `size`     | Size of the file data in bytes                       |
| 0x1C   | 4    | uint32    | `unknown2` | Unknown (possibly the high 32 bits of `size`)        |

## TAC file

The TAC file has no header: it is the data of every file, one after another. Each file is read from `offset` with `size` bytes, as given by its TAD entry.

## File names

The TAD only stores a hash of each path. The path is recovered from [`resources/FilenameDatabase.json`](https://github.com/EmulRyo/Shenmue/blob/main/resources/FilenameDatabase.json), an array of objects:

```json
{ "Hash": 1863636607, "HashPath": 0, "Path": "/0gdtex.bmp" }
```

`Hash` is compared with `hash[0]`. When a path has two extensions (`name.a.b`), they are swapped on extraction (`name.b.a`). Files with an unknown hash are saved as `/unknown/<hash>`, with the extension detected from the file magic.
