# TAD / TAC — Archive of the PC version

The PC version (Shenmue I & II) stores its data in pairs of files: the **TAD** file is the index and the **TAC** file holds the data. Both have the same name (`foo.tad` + `foo.tac`).

- **Extensions:** `.tad` (index), `.tac` (data)
- **Magic:** none

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
| 0x00   | 12   | uint32[3] | `hash`     | `hash[0]` is the hash of the file path; the other two are unknown (always 0 in the `audio_*` and `disk_*` archives, other values in `common_*` and `shaders_*`) |
| 0x0C   | 4    | uint32    | `empty`    | Always 0                                             |
| 0x10   | 4    | uint32    | `offset`   | Position of the file data in the TAC file            |
| 0x14   | 4    | uint32    | `unknown1` | Unknown. Always 0; it may be the high 32 bits of `offset`, but no known TAC reaches 4 GiB, so this cannot be confirmed |
| 0x18   | 4    | uint32    | `size`     | Size of the file data in bytes                       |
| 0x1C   | 4    | uint32    | `unknown2` | Unknown. `0` or `0x20000`; it seems to mark a duplicate entry (see [Duplicate entries](#duplicate-entries)) |

## Duplicate entries

Several entries can point to the same data (same `offset` and `size`), so identical files are stored only once in the TAC. In every such group in the Shenmue I and II archives, the first entry in the table has `unknown2 = 0` and every other entry has `unknown2 = 0x20000`. Entries whose data is not shared always have `unknown2 = 0`. `0x20000` therefore seems to mark an entry that reuses the data of a previous one.

## TAC file

The TAC file has no header: it is the data of every file, one after another, except for [duplicate entries](#duplicate-entries), which share data. Each file is read from `offset` with `size` bytes, as given by its TAD entry. The size of the TAC file matches `tacFileSize` in the TAD header.

## File names

The TAD doesn't store the file names, only a 32-bit hash of each path (`hash[0]`), so a name can only be recovered from a list of known paths and their hashes (e.g. `/0gdtex.bmp` → `1863636607`). The type of a file whose path is unknown can be identified from the [magic](Home#magic-numbers) of its data.
