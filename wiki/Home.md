# Shenmue file formats

This wiki describes the file formats of Shenmue that the tool can read, with their fields and sizes.

## Formats

| Format             | Extension     | Magic            | Type              | Description                                  |
|--------------------|---------------|------------------|-------------------|----------------------------------------------|
| [TAD / TAC](TAD-TAC) | `.tad` `.tac` | —              | Container         | Archive of the PC version (index + data)     |
| [AFS](AFS)         | `.afs`        | `AFS\0`          | Container         | List of files without names                  |
| [IDX](IDX)         | `.idx`        | `IDX0` / —       | Index             | Names of the files of an AFS                 |
| [PKF](PKF)         | `.pkf`        | `PAKF`           | Container         | Textures + IPAC block                        |
| [PKS](PKS)         | `.pks`        | `PAKS`           | Container         | IPAC block                                   |
| [IPAC](IPAC)       | —             | `IPAC`           | Container (block) | Named files inside PKF/PKS                   |
| [CHRT](CHRT)       | `.chrt`       | `CHRS`           | Script            | Character script                             |
| [PVR](PVR)         | `.pvr`        | `GBIX` / `PVRT`  | Texture           | PowerVR texture                              |
| [SPR](SPR)         | `.spr`        | `TEXN`           | Texture           | TEXN block with a PVR texture                |

## How the formats are nested

```
TAD/TAC
 ├─ AFS (+ IDX for the names)
 │   ├─ PKF ─┬─ TEXN → PVR / DDS
 │   │       └─ IPAC → CHRT, PVR, ...
 │   └─ PKS ─── IPAC → CHRT, PVR, ...
 ├─ PVR
 ├─ SPR → PVR
 └─ ...
```

## Conventions

- All values are **little-endian**.
- Offsets and sizes are in bytes; offsets are written in hexadecimal.
- `char[N]` is a fixed size string, not necessarily null terminated.
- `N` in a size column is the number of entries given by a count field.

## Magic numbers

The extension of extracted files is detected from their first bytes:

| Magic (bytes)                 | Extension | Format                  |
|-------------------------------|-----------|-------------------------|
| `RIFF` .... `WAVE`            | `.wav`    | WAVE audio              |
| `RIFF` .... `XWMA`            | `.xwm`    | xWMA audio              |
| `DXBC`                        | `.hlsl`   | Compiled DirectX shader |
| `CHRS`                        | `.chrt`   | [CHRT](CHRT)            |
| `PAKF`                        | `.pkf`    | [PKF](PKF)              |
| `PAKS`                        | `.pks`    | [PKS](PKS)              |
| `TEXN`                        | `.spr`    | [SPR](SPR)              |
| `GBIX` / `PVRT`               | `.pvr`    | [PVR](PVR)              |
| `DDS`                         | `.dds`    | DirectDraw Surface      |
| `AFS`                         | `.afs`    | [AFS](AFS)              |
| `IDX`                         | `.idx`    | [IDX](IDX)              |

## Compression

[PKF](PKF), [PKS](PKS), [CHRT](CHRT) files and [IPAC](IPAC) entries can be compressed with **gzip** (RFC 1952). They are detected by the bytes `1F 8B 08` and decompressed before being read. The uncompressed size is the last 4 bytes of the file (gzip trailer).
