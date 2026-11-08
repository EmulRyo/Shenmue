# SCN — Scene program (bytecode)

A SCN file is the program of a map, as a bytecode: the same structure as the [`SCN3` chunk](MAPINFO#scn3--scene-program) of the [MAPINFO](MAPINFO) files, stored alone. The file is that chunk, header included.

The Shenmue I data has 136 SCN files in the `scenario/` folder of the [TAD / TAC](TAD-TAC) archive, one per `mapinfo.bin`: `scenario/actNN_<map>.scn` is the program of `scene/NN/<map>/mapinfo.bin` (44 `act01_*`, 42 `act02_*` and 50 `act03_*`). E.g. `scenario/act01_d000.scn` is the program of `scene/01/d000`. 133 of them are different.

- **Extension:** `.scn`
- **Magic:** `SCN3`
- **Size:** 144 (`act01_mk99`, `act01_mo99`) to 416,616 bytes (`act03_mfsy`)
- **Compression:** none in the game files

## Relation with the SCN3 chunk of the map

All the SCN files are version `0x100` (bytecode). In the `mapinfo.bin` files, the `SCN3` chunk is native SH-4 code (version `0x20000`) in 119 maps and bytecode in 17:

| SCN3 chunk of the map | Maps | SCN file |
|-----------------------|-----:|----------|
| Bytecode (`0x100`)    | 13   | Identical to the chunk, byte for byte (`0000`, `betd` of the 3 folders, `mf99`, `mk99`, `mo99`, `yd8s`, `gmct`, `arar` and `door` of `scene/03`, `mc5q`, `nbik`) |
| Bytecode (`0x100`)    | 4    | Slightly different, with a size close to that of the chunk: `act01_jd99` and `act01_ju99` (16,256 bytes, chunk of 16,224), `act01_yq14` (19,004, chunk of 19,284), `act02_arar` (24,332, chunk of 24,416) |
| SH-4 code (`0x20000`) | 119  | The same program as a bytecode: about half the size of the chunk (`act01_d000`: 332,932 bytes, chunk of 729,128) |

For the 119 maps with SH-4 code, the data that follows the code is almost the same in both versions: 32,028 of the 32,396 strings of the data of the SCN files are also in the data of the chunks (paths, model, sprite and sound names, and debug messages such as `Daily practice %d: Check: in range, setting training flag to true`). On average, the first 48% of the data is identical byte for byte. The [door table](MAPINFO#door-table) and the strings of the [jukebox](MAPINFO#jukebox) are also in the data of the SCN files.

## Layout

```
+---------------------------+ 0x00
| Header (0x30 bytes)       |
+---------------------------+ 0x30 (codeStart)
| Bytecode                  |  entry point somewhere inside
+---------------------------+ dataStart
| Data                      |  floats, colors, tables, strings
+---------------------------+ end
| Relocations (8 × N)       |
+---------------------------+
| End of relocations (8)    |  8 null bytes
+---------------------------+ file size
```

All the offsets are relative to the start of the file.

## Header (0x30 bytes)

| Offset | Size | Type    | Name        | Description |
|-------:|-----:|---------|-------------|-------------|
| 0x00   | 4    | char[4] | `id`        | `SCN3` |
| 0x04   | 4    | uint32  | `size`      | Size of the file. Always equal to the file size |
| 0x08   | 4    | uint32  | `version`   | Always `0x100` (bytecode) |
| 0x0C   | 4    | uint32  | `entry`     | Entry point, inside the bytecode. Odd in 75 files; `0x30` (the start of the bytecode) in 3 |
| 0x10   | 4    | uint32  | `dataStart` | Start of the data, end of the bytecode |
| 0x14   | 4    | uint32  | `end`       | End of the data, start of the [relocations](#relocations). `size − 8` in the 39 files without relocations |
| 0x18   | 4    | uint32  | `unknown`   | Unknown: 0 to `0xF50`. 0 in 11 files |
| 0x1C   | 4    | uint32  | —           | Always 0 |
| 0x20   | 4    | uint32  | `codeStart` | Start of the bytecode. Always `0x30` |
| 0x24   | 4    | uint32  | `end2`      | Always equal to `end` |
| 0x28   | 8    | —       | —           | Always 0 |

## Bytecode

From `codeStart` to `dataStart`. The instructions of the bytecode are unknown.

## Data

From `dataStart` to `end`: floats, colors, tables of offsets and null terminated strings, as in the [SCN3 chunk](MAPINFO#scn3--scene-program). The data of 129 files contains the values `20.0, 800.0, 1.0, 0xFFFFFFFF`, the same as the default [fog](MAPINFO#fog--fog).

## Relocations

After the data, 97 files have a list of 8-byte entries (1 to 181), sorted by offset. Each entry is the offset of a 32-bit value of the data that is itself an offset in the file: *probably* the values that must be turned into addresses when the program is loaded. The list ends with 8 null bytes, which are the only bytes after `end` in the other 39 files. There are 4,265 entries in all.

| Offset | Size | Type   | Name     | Description |
|-------:|-----:|--------|----------|-------------|
| 0x00   | 4    | uint32 | `type`   | `0x2402`: the value is an offset in the bytecode (3,797 entries; e.g. tables of functions). `0x1402`: the value is an offset in the data (468 entries; e.g. the `name` of the models of the [door table](MAPINFO#door-model-16-bytes)) |
| 0x04   | 4    | uint32 | `offset` | Offset of the value, always inside the data (`dataStart` to `end`) |

The `SCN3` chunks of version `0x100` of the `mapinfo.bin` files have the same list: in `scene/03/door` the list has 49 entries, and the other 16 chunks end with only the 8 null bytes.
