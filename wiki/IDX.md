# IDX — AFS file names

An IDX file holds the names of the files stored in the [AFS](AFS) container with the same name (`foo.idx` → `foo.afs`). There are two known variants: **IDX0**, which has a magic, and the variant used by `humans.idx`, which has none.

- **Extension:** `.idx`
- **Magic:** `IDX0` (`49 44 58 30`) for the IDX0 variant, none for `humans.idx`

## IDX0 variant

### Header (20 bytes)

| Offset | Size | Type     | Name      | Description                         |
|-------:|-----:|----------|-----------|-------------------------------------|
| 0x00   | 4    | char[4]  | `id`      | Magic `IDX0`                        |
| 0x04   | 2    | uint16   | `afsCount` | Number of entries of the AFS (including the [lip-sync tables](Lip-sync), which have no name) |
| 0x06   | 2    | uint16   | `count`   | Number of entries                   |
| 0x08   | 12   | uint8[12]| `padding` | Unused                              |
| 0x14   | 20×N | Entry[N] | `entries` | Entry table (see below)             |

### Entry (20 bytes)

| Offset | Size | Type     | Name           | Description                                   |
|-------:|-----:|----------|----------------|-----------------------------------------------|
| 0x00   | 12   | char[12] | `filename`     | File name without extension, not always null terminated |
| 0x0C   | 2    | uint16   | `afsIndex`     | Index of the AFS entry this name belongs to   |
| 0x0E   | 2    | uint16   | `lipSyncIndex` | Index of the AFS entry with the [lip-sync table](Lip-sync) of the clip, `0xFFFF` if it has none (music) |
| 0x10   | 4    | uint32   | `lipSyncOffset` | Offset of the record of the clip in the lip-sync table, `0xFFFFFFFF` if it has none |

The IDX0 files are the indexes of the audio AFS files, whose entries are [xWMA](XWM) clips (e.g. `01BEDAA001`) and lip-sync tables. The clips of a group share the same `lipSyncIndex`. In 716 of the 738 IDX0 files whose AFS is known, `count` + the number of lip-sync tables is the number of AFS entries. In the others some entries have no name, e.g. `free01.afs` (12,262 entries, 12,135 names, 125 tables) or `jap/free02.afs` (21,362 entries, 9,059 names, 177 tables).

## `humans.idx` variant

It has no magic: the file starts directly with the number of names.

| Offset    | Size | Type        | Name      | Description                              |
|----------:|-----:|-------------|-----------|------------------------------------------|
| 0x00      | 4    | uint32      | `count`   | Number of names                          |
| 0x04      | 4×N  | char[4][N]  | `entries` | Names, 4 characters each (e.g. a character ID like `AKMI`) |
| 0x04+4×N  | 4×N  | uint32[N]   | —         | Unknown table, one value per name        |

Each name can be shared by several consecutive AFS entries (e.g. a [PKF](PKF) and a [PKS](PKS) per character). When the AFS entry count is a multiple of `count`, every name belongs to `afsCount / count` consecutive entries.
