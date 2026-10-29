# MAPINFO — Map information

`mapinfo.bin` describes a map (an area) of the game: the program of the scene, its characters and objects, the cameras, the lights, the fog, the collisions and event areas, the sound banks and some cutscenes. Every map folder of the [TAD / TAC](TAD-TAC) archive has one, at `scene/0N/<map>/mapinfo.bin` (e.g. `scene/01/d000/mapinfo.bin`).

The Shenmue I data contains 136 `mapinfo.bin` files (44 in `scene/01`, 42 in `scene/02` and 50 in `scene/03`), from 452 to 1,053,576 bytes. 134 of them are different: some maps have the same file in two discs.

- **Extension:** `.bin` (always named `mapinfo.bin`)
- **Magic:** none. The file starts with its first chunk: `ATTR` in 120 files, `LGHT` in 14 and `DYNA` in 2
- **Compression:** none in the game files

Several chunks hold formats that are also found as separate files: [CHRT](CHRT) (`CHRD`), [AUTH](AUTH) (`SCEX`) and [SCNF](SCNF).

## Layout

The file is a list of chunks, one after the other, that ends with an `END` chunk. The order of the chunks changes from one file to another (`LGHT` and `DYNA` can even come before `ATTR`), and most of them are optional.

```
+------------------------+ 0x00
| Chunk                  |  ATTR, LGHT, DYNA...
+------------------------+
| Chunk                  |  SCN3
+------------------------+
| ...                    |
+------------------------+
| END chunk (8 bytes)    |
+------------------------+ file size
```

## Chunk header (8 bytes)

| Offset | Size | Type    | Name   | Description                                                   |
|-------:|-----:|---------|--------|---------------------------------------------------------------|
| 0x00   | 4    | char[4] | `id`   | Chunk ID (see the table below). `FOG ` ends with a space and `END` with a null byte |
| 0x04   | 4    | uint32  | `size` | Size of the chunk in bytes, header included. The next chunk starts at `offset + size` |

## Chunks

Counts are the number of files (of 136) that contain the chunk.

| Chunk  | Files | Size          | Description |
|--------|------:|---------------|-------------|
| `ATTR` | 136   | 8             | Empty (header only) |
| `SCN3` | 136   | 0x90–0xE4A54  | [Program of the scene](#scn3--scene-program) |
| `REGD` | 136   | 8             | Empty |
| `CHRD` | 136   | 0x4C–0x1954   | [Characters and objects](#chrd--characters) ([CHRT](CHRT) script) |
| `END\0`| 136   | 8             | End of the file. Always the last chunk |
| `ECAM` | 135   | 0x84–0x10AFC  | [Cameras](#ecam--cameras) |
| `MAPR` | 135   | 0xC           | [Single value](#single-value-chunks) |
| `LGHT` | 122   | 0x40–0x2C70   | [Lights](#lght--lights) |
| `COLS` | 100   | 0x248–0x1551C | [Collisions and areas](#cols--collisions-and-areas) |
| `SNDD` | 100   | 0x3C–0x2B0    | [Sound banks](#sndd--sound-banks) |
| `MAPT` | 93    | 0xC           | [Single value](#single-value-chunks) |
| `LSCN` | 86    | 8             | Empty |
| `DIRT` | 86    | 0xC           | [Single value](#single-value-chunks) |
| `FOG ` | 80    | 0x58 or 0xA8  | [Fog and background](#fog--fog) |
| `SCRL` | 62    | 0xC           | [Single value](#single-value-chunks) |
| `SCEX` | 22    | 8–0x48BC4     | [Cutscenes](#scex--cutscenes) |
| `CMPS` | 17    | 8             | Empty |
| `SCOF` | 12    | 0xC           | [Single value](#single-value-chunks) |
| `DYNA` | 10    | 8–0x51DCC     | [Unknown animation data](#dyna--unknown) |
| `MAPD` | 6     | 0x10–0x24     | [List of values](#list-chunks) |
| `SCNF` | 3     | 0x26C         | [Dialogue lines](SCNF) (only in `scene/0N/jhd0`: Ryo's lines with the voices `SYS01` and `SYS02`) |
| `SCLM` | 2     | 0x14–0x1C     | [List of values](#list-chunks) |
| `MVTR` | 1     | 8             | Empty |
| `HGML` | 1     | 8             | Empty |
| `BLKS` | 1     | 0xC           | [Single value](#single-value-chunks) (`scene/03/fbgg`) |

The empty chunks (`ATTR`, `REGD`, `LSCN`, `CMPS`, `MVTR`, `HGML`) have no data: *probably* their presence is a flag.

## SCN3 — Scene program

The program of the map. In 119 files it is **native SH-4 machine code** (the CPU of the Dreamcast): version `0x20000`. In the other 17 (`scene/01/0000`, `betd`, `jd99`, `mf99`...) it is a bytecode: version `0x100`. All the offsets are relative to the start of the chunk.

| Offset | Size | Type   | Name        | Description |
|-------:|-----:|--------|-------------|-------------|
| 0x00   | 8    | —      | header      | [Chunk header](#chunk-header-8-bytes) (`SCN3`) |
| 0x08   | 4    | uint32 | `version`   | `0x20000` (SH-4 code) or `0x100` (bytecode) |
| 0x0C   | 4    | uint32 | `entry`     | Entry point. In version `0x20000` the code there always starts with `E6 2D` (`mov.l r14,@-r13`). In version `0x100` it can be an odd offset (`0x30`, `0xCC5`, `0x3C62`...) |
| 0x10   | 4    | uint32 | `dataStart` | Start of the data that follows the code |
| 0x14   | 4    | uint32 | `end`       | End of the data. In version `0x20000`, the size of the chunk; in version `0x100`, the size of the chunk minus 8 (16 of 17 files) |
| 0x18   | 4    | uint32 | `unknown`   | Unknown: 0 to `0x8F4` |
| 0x1C   | 4    | uint32 | —           | Always 0 |
| 0x20   | 4    | uint32 | `codeStart` | Start of the code. Always `0x30` |
| 0x24   | 4    | uint32 | `unknown`   | Version `0x100`: same as `end`. Version `0x20000`: 0 |
| 0x28   | 8    | —      | —           | Always 0 |
| 0x30   | —    | —      | code        | Code, up to `dataStart`. In the 119 SH-4 files it starts with the same bytes `86 2F 96 2F A6 2F D6 2F` (`mov.l r8,@-r15` … `mov.l r13,@-r15`) |
| `dataStart` | — | —   | data        | Data used by the code: floats, colors, tables of offsets and null terminated strings (paths such as `scene/02/model`, model names such as `C1305S3G.MT5`, sprite names such as `SCROLL27.SPR`) |

The data of 129 files contains the values `20.0, 800.0, 1.0, 0xFFFFFFFF`, the same as the default [fog](#fog--fog).

## CHRD — Characters

Holds a [CHRT](CHRT) script: the characters and objects of the map, with their models, positions and flags.

| Offset | Size | Type | Name   | Description |
|-------:|-----:|------|--------|-------------|
| 0x00   | 8    | —    | header | [Chunk header](#chunk-header-8-bytes) (`CHRD`) |
| 0x08   | —    | —    | script | `CHRS` block followed by its `STRG` block, exactly as in a [CHRT](CHRT) file. `CHRD.size` = 8 + `CHRS.size` + size of the `STRG` block |

## ECAM — Cameras

A list of fixed or animated cameras, each one with a 4-digit ID. The game files have 4924 cameras, 1 to 261 per file. The chunk header is followed by the cameras, one after the other, until the end of the chunk.

### Camera

| Offset | Size | Type    | Name    | Description |
|-------:|-----:|---------|---------|-------------|
| 0x00   | 4    | char[4] | `id`    | Camera ID: 4 ASCII digits (`0000`, `0101`, `5010`...) |
| 0x04   | 4    | uint32  | `size`  | Size of the camera in bytes, from `id`. The next camera starts at `offset + size` |
| 0x08   | 4    | uint32  | `flags` | Bit `0x20`: there is a roll curve. Bit `0x40`: there is a field of view curve. The rest of the bits (`0x80` and the higher bits: values such as `0x197660`) are unknown. The usual values of the low byte are `0x60` (4381 cameras), `0x40` (409) and `0x00` (134) |
| 0x0C   | —    | —       | curves  | 6 to 8 curves, with the same format as the [curves of AUTH](AUTH#animation-curves): `uint32 count`, `float times[count]`, `float values[count]`, `float tangents[count]` (`4 + 12 × count` bytes) |

| Curve | Present          | Description |
|------:|------------------|-------------|
| 0–2   | Always           | Position X, Y, Z |
| 3–5   | Always           | Target X, Y, Z (the point the camera looks at) |
| next  | If `flags & 0x20` | Roll in degrees (usually 0) |
| next  | If `flags & 0x40` | Field of view in degrees (usually 60) |

Most cameras have curves of one key (a fixed camera). The camera of the `ACAM` chunk of [AUTH](AUTH) has the same curves, with the value `0x61` in the place of `flags`.

## LGHT — Lights

The chunk contains 1 to 17 inner `LGHT` chunks (74 files have one, 26 have two), one after the other. One file (`scene/03/mb9q`) has bytes `0xFF` after its inner chunks, up to the end of the outer chunk. Most of the values of the lights are **half floats** (IEEE 754 binary16).

### Inner LGHT

| Offset | Size  | Type     | Name      | Description |
|-------:|------:|----------|-----------|-------------|
| 0x00   | 8     | —        | header    | [Chunk header](#chunk-header-8-bytes) (`LGHT`) |
| 0x08   | 4     | float    | `unknown` | 1.2 or 1.1 |
| 0x0C   | 2     | uint16   | `count`   | Number of lights |
| 0x0E   | 2     | uint16   | `unknown` | 1, 2 or 3 |
| 0x10   | 8     | half[4]  | `unknown` | *Probably* the ambient color (0.3 in most chunks) |
| 0x18   | 32×N  | Light[N] | lights    | `count` lights |
| —      | —     | —        | unused    | In 11 chunks the size is bigger than `0x18 + 32 × count`. The rest is unused: zeros, `0xFF` bytes or leftover lights |

### Light (32 bytes)

| Offset | Size | Type    | Name        | Description |
|-------:|-----:|---------|-------------|-------------|
| 0x00   | 2    | uint16  | `id`        | Number of the light |
| 0x02   | 2    | uint16  | `unknown`   | 1 (2728 lights) or 0 (334) |
| 0x04   | 2    | uint16  | `flags`     | Unknown: 11 (1680), 3 (1151), 2, 4, 12 or 10 |
| 0x06   | 6    | half[3] | `color`     | Red, green and blue, from 0 to 1 |
| 0x0C   | 2    | half    | `intensity` | *Probably* the intensity: usually 1 (0.5 to 1.3) |
| 0x0E   | 2    | half    | `unknown`   | Usually 2 to 6 (*probably* a distance) |
| 0x10   | 2    | half    | `unknown`   | Usually about 0.65 |
| 0x12   | 6    | half[3] | `position`  | Position X, Y, Z. Y is the height, usually 1 to 4 |
| 0x18   | 6    | half[3] | `direction` | `(0, -1, 0)` (pointing down) in 2776 of 3062 lights |
| 0x1E   | 2    | uint16  | `angle`     | Angle in units of 1/65536 of a turn: `0x071C` (10°) in 2922 lights |

## COLS — Collisions and areas

The chunk header is followed by 1 to 3 blocks with a 4-digit ID (`0000`, `0001`...). 80 files have one block. After the blocks there are 8 (93 files) or 12 (7 files) zero bytes, up to the end of the chunk.

### Block

| Offset | Size | Type     | Name     | Description |
|-------:|-----:|----------|----------|-------------|
| 0x00   | 4    | char[4]  | `id`     | Block ID: 4 ASCII digits |
| 0x04   | 4    | uint32   | `size`   | Size of the block in bytes, from `id` |
| 0x08   | —    | Group[6] | groups   | Always the 6 groups `COLI`, `HGHT`, `EVNT`, `UNDU`, `SOND` and `PROP`, in this order. A group can be empty |
| —      | 8    | uint32[2] | —       | Always 0 |
| —      | 24   | int32[6] | `grids`  | Offset of the grid of each group (same order), from the end of this table; -1 if the group has no grid (always for the empty groups) |
| —      | —    | Grid[]   | grids    | The grids of the groups, in the same order |

### Group

| Offset | Size | Type    | Name   | Description |
|-------:|-----:|---------|--------|-------------|
| 0x00   | 4    | char[4] | `id`   | `COLI`, `HGHT`, `EVNT`, `UNDU`, `SOND` or `PROP` |
| 0x04   | 4    | uint32  | `size` | Size of the records in bytes, **header not included** (0 for an empty group) |
| 0x08   | —    | —       | records | Records, one after the other. Each record is a prefix that depends on the group, followed by a shape |

| Group  | Prefix | Description |
|--------|--------|-------------|
| `COLI` | `uint32 type` (0, 9, 100, 1, 10, 7, 200...) | Collision walls and obstacles |
| `HGHT` | `float height` | *Probably* areas with a different floor height (-1.8, 5.4...) |
| `EVNT` | `uint32 unknown` (`0xFF`, `0x100FF`, `0x40001`, `0x40002`...) | *Probably* event areas |
| `UNDU` | `float heights[4]` | Sloped floor ("undulation"): *probably* the height at each corner of the shape (the fourth one is usually 0 for triangles) |
| `SOND` | `uint32 unknown` | *Probably* sound areas |
| `PROP` | `char[4] type` + `uint32 unknown[4]` (20 bytes) | Areas with a property: `CAM2` (609), `STIR` (260), `WALK` (129), `CAM3` (127), `CNT0` (98), `PRIV` (45), `INSH` (39), `CAM1`, `TALK`, `STOP` |

### Shape

All the coordinates are in the horizontal plane (X, Z).

| Offset | Size | Type   | Name   | Description |
|-------:|-----:|--------|--------|-------------|
| 0x00   | 4    | uint32 | `type` | Type of shape (see below) |
| 0x04   | —    | —      | data   | Depends on the type |
| —      | 4    | uint32 | —      | `0xFFFFFFFF`: end of the shape |

| Type | Data | Shape | Count |
|-----:|------|-------|------:|
| 0    | `float x, z` | Point | 9 |
| 1    | `float x1, z1, x2, z2` | Segment | 4375 |
| 2    | `uint32 n` + `n × (float x, z)` | Polyline | 2234 |
| 3    | `float radius, x, z` | Circle | 874 |
| 4    | `3 × (float x, z)` | Triangle | 1935 |
| 5    | `float x, z, ax, az, bx, bz` | Rectangle: *probably* the center and two half axes (the two vectors are perpendicular in 95% of the rectangles) | 3610 |
| 6    | `uint32 n` + `n × (float x, z)` | Polygon | 3284 |

### Grid

Divides the area of a group in cells, and lists the records found in each cell.

| Offset | Size  | Type     | Name      | Description |
|-------:|------:|----------|-----------|-------------|
| 0x00   | 4     | char[4]  | `id`      | Name of the group in lower case: `coli`, `hght`, `evnt`, `undu`, `sond` or `prop` |
| 0x04   | 4     | uint32   | `size`    | Size of the grid header without `id` and `size`: always `0x40` |
| 0x08   | 4     | uint32   | `cells`   | Number of cells: `columns × rows` (1, 4, 16, 256...) |
| 0x0C   | 4     | uint32   | `columns` | Number of cells in X |
| 0x10   | 4     | uint32   | `rows`    | Number of cells in Z |
| 0x14   | 16    | float[4] | `bounds`  | *Probably* the limits of the area (minimum X, maximum X, minimum Z, maximum Z) |
| 0x24   | 16    | float[4] | `unknown` | *Probably* sizes of the cells (e.g. `7, 6, 7, 6`) |
| 0x34   | 4     | float    | `unknown` | Always 1 |
| 0x38   | 16    | int32[4] | `unknown` | The first two are always -1; the others are 0, -4, or values such as 8, 12, 24 |
| 0x48   | —     | —        | cells     | For each cell, a list of `uint32` offsets of the records of the group (from the group header) that are in the cell, ended by `0xFFFFFFFF` |

## SNDD — Sound banks

A list of commands that end with a 0. The commands load the [SND](SND) sound banks of the map in numbered slots (0 to 7). `FREE` frees a slot.

| Command | Size | Arguments | Description |
|--------:|-----:|-----------|-------------|
| 0       | 4    | —         | End of the list |
| 1       | 24   | `uint32 slot`, `char[16] name` | Loads the bank `name` (`bgm087.snd`, `amb028.snd`, `f1tomato.snd`...) in `slot`. `FREE` empties the slot, and `SELECT` is found twice |
| 2       | 16   | `uint32 unknown`, `uint32 slot`, `uint32 unknown` | Unknown. The first argument always has `0xA8` in its low byte (`0x3FA8` in 83 of 91 commands); the third one is 80 to 127 (*probably* a volume) |
| 3       | 16   | `uint32 unknown`, `uint32 slot`, `uint32 unknown` | Unknown. The first argument is always `0x9A0`; the third one is 15 or 30 |

5 files (`scene/01/d000`, `scene/02/mksg`, `scene/03/ms08`...) also have blocks that start with `0xFFFFFFFF`, contain values and `0xFFFFFFFE`, and are not understood.

## FOG — Fog

Contains one set (78 files) or two sets (2 files, size `0xA8`) of three chunks `DEMO`, `FOG ` and `BACK`, each one with a [chunk header](#chunk-header-8-bytes).

| Chunk  | Size | Data | Description |
|--------|-----:|------|-------------|
| `DEMO` | 0xC  | `uint32` | Unknown. Always 1 |
| `FOG ` | 0x1C | `float near`, `float far`, `float density`, `uint32 color`, `uint32 unknown` | Fog: distance where it starts and where it is complete (e.g. 0.1 and 150), *probably* its density (0.08 to 1) and its color (ARGB, e.g. `0xFF605010`). The last value is always 1. The default values are `20, 800, 1, 0xFFFFFFFF` |
| `BACK` | 0x28 | `uint32 colors[4]`, `uint32`, `uint32`, `uint32[2]` | *Probably* the colors of the background. The fifth value is always 0, the sixth 0 or 1, the last two 0 |

## SCEX — Cutscenes

Contains `TRCK` chunks: [AUTH](AUTH) cutscenes (259 in the game files), with their `ASEQ`, `ACAM`, `AMOV` and optional `ASTR` chunks. `scene/01/op00` also has two `MLWD` chunks of 0x2C bytes, unknown. An `SCEX` chunk can be empty (size 8).

## DYNA — Unknown

Contains chunks with 4-character IDs, many of them character IDs (`BIKA`, `BIKB`, `HAKA`, `KADY`, `BC0D`, `BLKS`, `BDAG`...), from 0x3930 to 0x1D04C bytes. A `DYNA` chunk can be empty (size 8). The inner chunks start like this:

| Offset | Size | Type    | Name      | Description |
|-------:|-----:|---------|-----------|-------------|
| 0x00   | 4    | char[4] | `id`      | ID |
| 0x04   | 4    | uint32  | `size`    | Size of the chunk, header included |
| 0x08   | 4    | uint32  | `unknown` | e.g. `0x130C`, `0x1D5C` |
| 0x0C   | 4    | uint32  | `unknown` | e.g. 101, 156 |
| 0x10   | 8    | —       | —         | 0 |
| 0x18   | —    | —       | data      | Unknown: *probably* pairs of a `uint32` (a frame or index: 2, 4, 6, 7, 10...) and a `float` |

## Single value chunks

Chunks of 0xC bytes: the header and one 4-byte value.

| Chunk  | Type   | Description |
|--------|--------|-------------|
| `MAPR` | int32  | Unknown: 1 (97 files), -1 (19), 0 (6), or 2 to 22 |
| `MAPT` | int32  | Unknown. Always 1 |
| `DIRT` | int32  | *Probably* a direction in degrees: 0 to 343 (90, 220, 335, 260, 180...) |
| `SCRL` | int32  | Unknown: -1 (57 files), 0, 1 or 23 |
| `SCOF` | float  | Unknown: -4 (6 files), -6 (3) or -2 (3) |
| `BLKS` | float  | Unknown: 100 |

## List chunks

Lists of `int32` values ended by -1.

| Chunk  | Example | Description |
|--------|---------|-------------|
| `MAPD` | `4, 8, 12, 13, 14, 15, -1` | Unknown (`mkyu`, `m3fb`, `ma00`, `mbqc`, `mc5q`) |
| `SCLM` | `0, 7, 1, 28, -1` | Unknown (`scene/01/op00`, `scene/03/msbs`) |
