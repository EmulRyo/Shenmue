# IDX — AFS file names

An IDX file holds the names of the files stored in the [AFS](AFS) container with the same name (`foo.idx` → `foo.afs`). There are two known variants: **IDX0**, which has a magic, and the variant used by `humans.idx`, which has none.

- **Extension:** `.idx`
- **Magic:** `IDX0` (`49 44 58 30`) for the IDX0 variant, none for `humans.idx`

## IDX0 variant

### Header (20 bytes)

| Offset | Size | Type     | Name      | Description                         |
|-------:|-----:|----------|-----------|-------------------------------------|
| 0x00   | 4    | char[4]  | `id`      | Magic `IDX0`                        |
| 0x04   | 2    | uint16   | `count1`  | Unknown, usually related to `count` |
| 0x06   | 2    | uint16   | `count`   | Number of entries                   |
| 0x08   | 12   | uint8[12]| `padding` | Unused                              |
| 0x14   | 20×N | Entry[N] | `entries` | Entry table (see below)             |

### Entry (20 bytes)

| Offset | Size | Type     | Name           | Description                                   |
|-------:|-----:|----------|----------------|-----------------------------------------------|
| 0x00   | 12   | char[12] | `filename`     | File name without extension, not always null terminated |
| 0x0C   | 2    | uint16   | `afsIndex`     | Index of the AFS entry this name belongs to   |
| 0x0E   | 2    | uint16   | `afsLastIndex` | Unknown, looks like the last AFS index of a group |
| 0x10   | 4    | uint32   | `unknown`      | Unknown                                       |

## `humans.idx` variant

It has no magic: the file starts directly with the number of names.

| Offset    | Size | Type        | Name      | Description                              |
|----------:|-----:|-------------|-----------|------------------------------------------|
| 0x00      | 4    | uint32      | `count`   | Number of names                          |
| 0x04      | 4×N  | char[4][N]  | `entries` | Names, 4 characters each (e.g. a character ID like `AKMI`) |
| 0x04+4×N  | 4×N  | uint32[N]   | —         | Unknown table, one value per name        |

Each name can be shared by several consecutive AFS entries (e.g. a [PKF](PKF) and a [PKS](PKS) per character). When the AFS entry count is a multiple of `count`, every name belongs to `afsCount / count` consecutive entries.
