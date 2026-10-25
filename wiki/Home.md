# Shenmue file formats

This wiki describes the file formats of Shenmue, with their fields and sizes.

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
| [SCNF](SCNF)       | — (`BIN`)     | `SCNF`           | Dialogue          | Voice and subtitle of each dialogue line     |
| [Lip-sync](Lip-sync) | —           | —                | Dialogue          | Speaker, subtitle and mouth movements of each voice clip |
| [XWM](XWM)         | `.xwm`        | `RIFF` .... `XWMA` | Audio           | xWMA audio clip (voices and music)           |
| [MT5 / CHRM](MT5)  | `.mt5` `.chrm` | `HRCM`          | Model             | 3D model (CHRM: textures stored in the PKF)  |
| [PVR](PVR)         | `.pvr`        | `GBIX` / `PVRT`  | Texture           | PowerVR texture                              |
| [SPR](SPR)         | `.spr`        | `TEXN`           | Texture           | TEXN block with a PVR texture                |

## How the formats are nested

```
TAD/TAC
 ├─ AFS (+ IDX for the names)
 │   ├─ XWM (+ Lip-sync table per group of clips)
 │   ├─ PKF ─┬─ TEXN → PVR / DDS
 │   │       └─ IPAC → CHRT, PVR, ...
 │   └─ PKS ─── IPAC → CHRT, CHRM (textures in the PKF), SCNF, PVR, ...
 ├─ MT5 → TEXN → PVR / DDS
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

Most formats can be identified by their first bytes. This is useful in containers that don't store the type of their files ([AFS](AFS)) or store it wrong ([IPAC](IPAC)):

| Magic (bytes)                 | Extension | Format                  |
|-------------------------------|-----------|-------------------------|
| `RIFF` .... `WAVE`            | `.wav`    | WAVE audio              |
| `RIFF` .... `XWMA`            | `.xwm`    | [XWM](XWM)              |
| `DXBC`                        | —         | Compiled DirectX shader |
| `CHRS`                        | `.chrt`   | [CHRT](CHRT)            |
| `SCNF`                        | —         | [SCNF](SCNF)            |
| `HRCM`                        | `.mt5` `.chrm` | [MT5 / CHRM](MT5)  |
| `PAKF`                        | `.pkf`    | [PKF](PKF)              |
| `PAKS`                        | `.pks`    | [PKS](PKS)              |
| `TEXN`                        | `.spr`    | [SPR](SPR)              |
| `GBIX` / `PVRT`               | `.pvr`    | [PVR](PVR)              |
| `DDS`                         | `.dds`    | DirectDraw Surface      |
| `AFS`                         | `.afs`    | [AFS](AFS)              |
| `IDX`                         | `.idx`    | [IDX](IDX)              |

## Compression

[PKF](PKF), [PKS](PKS), [CHRT](CHRT), [MT5](MT5) files and [IPAC](IPAC) entries can be compressed with **gzip** (RFC 1952). A compressed file starts with the bytes `1F 8B 08`. The uncompressed size is the last 4 bytes of the file (gzip trailer).
