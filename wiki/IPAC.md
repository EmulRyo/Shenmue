# IPAC — File block inside PKF/PKS

IPAC is not a file on its own: it is a block stored inside [PKF](PKF) and [PKS](PKS) files. It contains a list of files (models, scripts, textures...) and a dictionary with their names, offsets and sizes.

- **Magic:** `IPAC` (`49 50 41 43`)

All offsets are relative to the start of the IPAC block.

## Layout

```
+---------------------+ 0x00
| Header (16 bytes)   |
+---------------------+ 0x10
| File data           |
| ...                 |
+---------------------+ dictionaryOffset
| Dictionary          |  count × 20 bytes
+---------------------+
```

## Header (16 bytes)

| Offset | Size | Type    | Name               | Description                                  |
|-------:|-----:|---------|--------------------|----------------------------------------------|
| 0x00   | 4    | char[4] | `id`               | Magic `IPAC`                                 |
| 0x04   | 4    | uint32  | `dictionaryOffset` | Position of the dictionary                   |
| 0x08   | 4    | uint32  | `count`            | Number of files                              |
| 0x0C   | 4    | uint32  | `size`             | Size of the IPAC block                       |

## Dictionary entry (20 bytes)

| Offset | Size | Type    | Name        | Description                                       |
|-------:|-----:|---------|-------------|---------------------------------------------------|
| 0x00   | 8    | char[8] | `filename`  | File name, padded with spaces or nulls            |
| 0x08   | 4    | char[4] | `extension` | Extension in uppercase (e.g. `BIN`, `CHRT`), padded with spaces or nulls |
| 0x0C   | 4    | uint32  | `offset`    | Position of the file data                         |
| 0x10   | 4    | uint32  | `size`      | Size of the file data in bytes                    |

## Notes

- A file can be compressed with [gzip](Home#compression).
- The `extension` field doesn't always match the content (e.g. textures are stored as `BIN`); the [magic](Home#magic-numbers) of the data is more reliable.
- The entries with the `CHRM` extension are [MT5](MT5) models (magic `HRCM`) whose textures are stored outside the model.
