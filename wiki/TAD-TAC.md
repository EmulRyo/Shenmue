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
| 0x00   | 4    | uint32    | `hash`     | Hash of the file path (see [File names](#file-names)) |
| 0x04   | 4    | uint32    | `pathHash` | The 8 hexadecimal digits that end the name of the files of `common_*` and `shaders_*` (see [Names with a hash](#names-with-a-hash)); 0 in the `audio_*` and `disk_*` archives |
| 0x08   | 4    | uint32    | `unknown0` | Unknown; 0 when `pathHash` is 0. The same name has the same value in `shaders_pc_*` and `shaders_xb1_*`, while its `hash` and `pathHash` differ |
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

The TAD doesn't store the file names, only a 32-bit hash of each path (`hash`), so a name can only be recovered by hashing candidate paths (e.g. `/0gdtex.bmp` → `1863636607`). The type of a file whose path is unknown can be identified from the [magic](Home#magic-numbers) of its data.

### Path hash

The hash is computed from the path in three steps:

1. **Normalization:** the path is converted to lowercase and every character other than `a`–`z`, `0`–`9`, `.` and `_` is removed (`/`, `-`, `?`, `=`...). `/fight/en_ryou.bin` becomes `fighten_ryou.bin`. Let `L1` be its length.
2. **Truncation:** if the normalized path ends with two groups of a dot and 8 lowercase hexadecimal digits (`.0b9c4f68.00000000`), the last group is removed: only its length counts. Let `L2` be the length after this step (`L2 = L1` when nothing is removed).
3. **Hash:** `hash = MurmurHash2(path, seed 0x066EE5D0) × 0x1003F + ((L1 × L2) mod 0xFFFFFFFF) × 0x2001F`, all in 32 bits. MurmurHash2 is the standard 32-bit version (multiplier `0x5BD1E995`, shifts 24, 13 and 15) with the length of the normalized path.

Since the normalization drops the slashes, paths that differ only in them have the same hash (`/a/bc.bin` and `/ab/c.bin`).

### Names with a hash

The files of the `audio_*` and `disk_*` archives have plain paths (`/scene/01/stream/pc/eng/btt00.afs`). Those of the `common_*` and `shaders_*` archives have the path that the game asks for, inside `/tex/assets`, followed by two groups of 8 hexadecimal digits:

```
/tex/assets/ui/common/buttons/a_off.png.3cc1a28a.xxxxxxxx
            └─ path asked by the game ──┘ pathHash  only its length is used
```

- The first group is `pathHash` (entry offset 0x04) written in lowercase hexadecimal. In most files (765 of the 912 named files of Shenmue I, 1252 of 1427 in Shenmue II) it is the MurmurHash2 (seed 0x066EE5D0) of the path asked by the game in lowercase, with its query when it has one: `/ui/common/buttons/a_off.png?usage=0` → `3cc1a28a`, `/ui/font/english.fontdef?font=0&image=1` → `6f742957`. The query is not part of the name. In the others (all the shaders of `shaders_xb1_*`, for instance) its origin is unknown.
- The second group is removed by the [truncation](#path-hash) of the hash, so its value is unknown; only its length (9 characters with the dot) counts.

Several files can share the path asked by the game and differ only in `pathHash` (e.g. one per shader variant, or `.png` and `.tga` versions of a texture).
