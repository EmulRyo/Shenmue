# PKS — IPAC package

A PKS file contains only an [IPAC](IPAC) block (no textures). It usually goes together with a [PKF](PKF) file.

- **Extension:** `.pks`
- **Magic:** `PAKS` (`50 41 4B 53`)
- **Compression:** the whole file can be [gzipped](Home#compression)

## Layout

```
+---------------------+ 0x00
| Header (16 bytes)   |
+---------------------+ ipacOffset
| IPAC block          |
+---------------------+
```

## Header (16 bytes)

| Offset | Size | Type    | Name         | Description                         |
|-------:|-----:|---------|--------------|-------------------------------------|
| 0x00   | 4    | char[4] | `id`         | Magic `PAKS`                        |
| 0x04   | 4    | uint32  | `ipacOffset` | Position of the [IPAC](IPAC) block |
| 0x08   | 4    | uint32  | `unknown1`   | Unknown                             |
| 0x0C   | 4    | uint32  | `unknown2`   | Unknown                             |

## Notes

- Some files with the `.pks` extension are actually [PKF](PKF) files (magic `PAKF`).
