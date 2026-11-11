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
| `FOG ` | 78    | 0x58 or 0xA8  | [Fog and background](#fog--fog) |
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

The program of the map. In 119 files it is **native SH-4 machine code** (the CPU of the Dreamcast): version `0x20000`. In the other 17 (`scene/01/0000`, `betd`, `jd99`, `mf99`...) it is a bytecode: version `0x100`. All the offsets are relative to the start of the chunk. The `scenario/` folder has the program of every map as a bytecode, in a separate [SCN](SCN) file.

| Offset | Size | Type   | Name        | Description |
|-------:|-----:|--------|-------------|-------------|
| 0x00   | 8    | —      | header      | [Chunk header](#chunk-header-8-bytes) (`SCN3`) |
| 0x08   | 4    | uint32 | `version`   | `0x20000` (SH-4 code) or `0x100` (bytecode) |
| 0x0C   | 4    | uint32 | `entry`     | Entry point. In version `0x20000` the code there always starts with `E6 2D` (`mov.l r14,@-r13`). In version `0x100` it can be an odd offset (`0x30`, `0xCC5`, `0x3C62`...) |
| 0x10   | 4    | uint32 | `dataStart` | Start of the data that follows the code |
| 0x14   | 4    | uint32 | `end`       | End of the data. In version `0x20000`, the size of the chunk; in version `0x100`, the size of the chunk minus 8 (16 of 17 files) |
| 0x18   | 4    | uint32 | `unknown`   | Unknown: 0 to `0xF50`. 0 in 11 files |
| 0x1C   | 4    | uint32 | —           | Always 0 |
| 0x20   | 4    | uint32 | `codeStart` | Start of the code. Always `0x30` |
| 0x24   | 4    | uint32 | `unknown`   | Version `0x100`: same as `end`. Version `0x20000`: 0 |
| 0x28   | 8    | —      | —           | Always 0 |
| 0x30   | —    | —      | code        | Code, up to `dataStart`. In the 119 SH-4 files it starts with the same bytes `86 2F 96 2F A6 2F D6 2F` (`mov.l r8,@-r15` … `mov.l r13,@-r15`) |
| `dataStart` | — | —   | data        | Data used by the code: floats, colors, tables of offsets and null terminated strings (paths such as `scene/02/model`, model names such as `C1305S3G.MT5`, sprite names such as `SCROLL27.SPR`) |
| `end`  | —    | —      | relocations | Version `0x100` only: list of [relocations](SCN#relocations), ended by 8 null bytes. Only `scene/03/door` has entries (49); the other 16 chunks have only the 8 null bytes |

The data of 129 files contains the values `20.0, 800.0, 1.0, 0xFFFFFFFF`, the same as the default [fog](#fog--fog).

### Strings of the code

In version `0x20000`, the code loads the strings of the data with `mov.l @(disp,PC),Rn` (`Dndd`): the 32-bit literal at `(pc & ~3) + 4 + disp × 4` is the offset of the string from `dataStart`. Each function ends with an `rts` (`000B`), followed by its literals. The functions of `scene/01/d000` that load the cutscenes load, in this way, the path of the scene (`/scene/01/D000/`), the name of each PKS of the cutscenes, their `seqdataN.bin` files (e.g. `D0W0` and `seqdata1.bin`…`seqdataC.bin`), the name of the [AFS](AFS) of their voices (`E1020`) and the sound banks of the [AUTH](AUTH#sound-bank) files (`a1_yamaw.snd`, with the music `bgm085.snd` and `battle_1.snd`). The family of PKS `djhn_01`…`djhn_07` and `djhn_mot` is loaded by one function, its `seqdata1.bin`…`seqdata7.bin` by another one, and the bank `a1_yanji.snd` with the AFS `01JUCEA` by a third one.

### Door table

The data of 97 files (96 of version `0x20000` and the bytecode of `scene/03/door`) contains a table with the doors of the map and other models placed with them: a list of models followed by the list of placements. No field of the header points to it; it is preceded by the names of its models. It has 1 to 36 models and 1 to 120 placements (`scene/0x/d000`); there are 861 placements in all.

| Offset    | Size   | Type             | Name       | Description |
|----------:|-------:|------------------|------------|-------------|
| 0x00      | 16×M   | DoorModel[M]     | models     | Models |
| 16×M      | 36×N   | DoorPlacement[N] | placements | Placements. The list ends where the data is not a valid placement |

#### Door model (16 bytes)

| Offset | Size | Type   | Name      | Description |
|-------:|-----:|--------|-----------|-------------|
| 0x00   | 4    | uint32 | `name`    | Offset of the null terminated name of the model, relative to the start of the chunk |
| 0x04   | 4    | uint32 | `type`    | 0 to 37. 0 for the models that are not doors (44 models); for the doors it is often the number of the `DRnn_` model (2 for `DR02_017`, 15 for `DR15_012`...) |
| 0x08   | 4    | uint32 | `unknown` | `0xFFFFFFFF` for the type 0. For the doors, `0xnn02AB` (*probably* a sound: e.g. `0x1402AB`, `0x1602AB`) in all the models but one (`0x514AB` in `scene/03/door`) |
| 0x0C   | 4    | uint32 | `unknown` | Same as the previous one. For the doors, usually the next number (278 of 283 models: `0x1502AB` after `0x1402AB`, `0x1702AB` after `0x1602AB`); the same number in 4 and the previous one in 1. *Probably* the sounds of opening and closing |

The name is either:

- The name of a model file of the map (MT5 or CHRM, in the [PKF](PKF) of the map, e.g. `mpk00.pkf`): the doors `DR01_011`, `DR15_012`..., the dark boxes behind the doors `DR00_002`..., and other models (`GNKS2STG`, `B010425G`, `B49I2B1G`...).
- The name of a `DefImage` of the [CHRT](CHRT) scripts of the map, which gives the model file: `DOOR` (46 files), `TOKOYA_DOOR`, `AUTO_DOOR`, `SOBA_DOOR`, `DOR1`, `DOR2`, `MS8S_DOOR`. E.g. in `scene/01/daza` the `CHARA.chrt` of `mpk00.pkf` has `DefImage("DOOR") { Image(1, "DR13_010.MT5") }`, and in `scene/01/dpiz` the [CHRD](#chrd--characters) chunk has `DefImage("DOOR") { Image(1, "$DR01_009.MT5") }`.

#### Door placement (36 bytes)

The model is scaled, rotated around the vertical axis and moved to its position, as the nodes of the [MT5](MT5) models. The coordinates are those of the map models and the cameras (Z is not negated as in the [shapes](#shape)). The door models are made around the origin (e.g. the leaves of a sliding door side by side along X, from the floor), and the placed doors fill the doorways of the map models, on the lines of the [COLI](#group) walls.

| Offset | Size | Type     | Name       | Description |
|-------:|-----:|----------|------------|-------------|
| 0x00   | 4    | uint32   | `type`     | 2: door (531 placements). 1: a model that is not a door (330): the dark boxes `DR00` (312) and the objects of `scene/0x/mksg` (18) |
| 0x04   | 4    | uint32   | `model`    | Index of the model in the list of models |
| 0x08   | 12   | float[3] | `scale`    | Scale. Not 1 in 126 placements (e.g. `(0.45, 1, 1)` for a box behind a narrow door) |
| 0x14   | 12   | float[3] | `position` | Position X, Y, Z |
| 0x20   | 4    | int32    | `angle`    | Rotation around the vertical axis in units of 1/65536 of a turn (`0x4000` = 90°). It can be negative (`0xFFFFC001`): 354 placements |

A door and a dark box are often placed together, at the same or almost the same position, with the same angle or with angles 180° apart.

### Jukebox

The program of the 13 bars with a jukebox (`dbhb`, `dgct` and `dsli` of `scene/01`, `02` and `03`, `mfsy` and `mkyu` of `scene/02` and `03`) uses the jukebox as a character: its code has the IDs `JBOX` (the jukebox) and `JCNT` as 4-character literals, next to other character IDs such as `AKIR`, `ASTD` or `MO01`, and its data has strings of the jukebox such as `HMOT_JBX.BIN` and the list of its songs (`bgm045.snd`, `bgm015.snd`...). The jukebox isn't placed by the program nor by the chunks of the file: it is the character `JBOX` of a [CHRT](CHRT) script (`CHARA`) of a [PKF](PKF) of the map folder, with the model of its `DefImage("JBOX")`:

| Maps   | PKF           | Model          | Position                   | Angle |
|--------|---------------|----------------|----------------------------|------:|
| `dbhb` | `jukebox.pkf` | `JUKK202G.MT5` | (4.5, 0, -8.09)            | 0     |
| `dgct` | `mpk00.pkf`   | `JUKK203G.MT5` | (4.3, 0, -0.69)            | 229.99 |
| `mkyu` | `mpk00.pkf`   | `JUKK204G.MT5` | (-79.1523, 0.0007, 46.2496) | -45  |
| `mfsy` | `alpha.pkf`   | `JUKK205G.MT5` | (-29.419, 0, 76.4475)      | 44.99 |
| `dsli` | `mpk00.pkf`   | `JUKK206G.MT5` | (-0.754, 0, -0.086)        | 180   |

The model file is in the PKS of the same name (e.g. `jukebox.pks`). The package that isn't `mpk00` is named in the data of the program: `JUKEBOX` after `scene/01/DBHB` in `scene/01/dbhb`, `ALPHA` after `scene/02/MFSY` in `scene/02/mfsy`. The character is a standing object with a collision: `Image("JBOX")`, `OBJECT`, `FLAGS("DISP")`, `Position`, `Angle` (none in `dbhb`), `SHADOWOFF`, `TinyMot`, and in all but `dsli` `Coli(8)`, `Size(1.0, 1.5, 0.55)` and `Level(1.0)`. The position and the angle are in the coordinates of the map models and the cameras; the model is made around the origin, on the floor, with its front towards -Z at angle 0, and the placed jukebox stands with its back against a wall of the bar.

The same scripts have the characters `JCNT`, `ASTD` and `D_TE` (with `Position(0, 0, 0)`, or without a position in `dgct`) and `YKUR` (the model `YKB_UR.MT5` of the `DefImage("YKB_UDE_R")`, a right arm, placed next to the jukebox). In `mfsy` they are in another package, `alp_juke.pkf` (named `ALP_JUKE` in the program). `scene/03/m3fb` and `scene/03/ma00` also have `JBOX` in their program, but no jukebox character in their folder.

## CHRD — Characters

Holds a [CHRT](CHRT) script: the characters and objects of the map, with their models, positions and flags.

| Offset | Size | Type | Name   | Description |
|-------:|-----:|------|--------|-------------|
| 0x00   | 8    | —    | header | [Chunk header](#chunk-header-8-bytes) (`CHRD`) |
| 0x08   | —    | —    | script | `CHRS` block followed by its `STRG` block, exactly as in a [CHRT](CHRT) file. `CHRD.size` = 8 + `CHRS.size` + size of the `STRG` block |

26 files have no characters in this chunk: their `CHRD` chunk, the smallest one (0x4C bytes), holds the [empty script](CHRT#empty-script) `Character('____', 3) { Flags("Kill") }`. 21 of them (`dgct`, `dkpa`, `dmaj`, `drht`, `dsba` and `dsus` of `scene/01`, `02` and `03`, `ydb1` of `scene/02` and `03`, and `scene/03/tery`) have exactly that script; the other 5 (`scene/01/dhqb`, `dslt` of `scene/01`, `02` and `03`, and `scene/03/mb9q`) have the variant `Character('....', 3) { Flags("KILL") }`. All 26 maps have their characters in the script of their base package (see below).

### Characters of the base package

Most map folders have a package named `mpk00` (`mpk00.pkf` and `mpk00.pks`, 96 of the 136 maps). In 87 of them, the [IPAC](IPAC) block of `mpk00.pkf` has a [CHRT](CHRT) script named `CHARA`, with more characters and objects of the map than the `CHRD` chunk: 1294 characters in those 87 scripts, against 395 in the `CHRD` chunks of 110 maps. Counting only the characters with an image, without the flag `SLEEP` and with a position in the scene (no coordinate of ±1000 or more), there are 490 in the `CHARA` scripts of 63 maps and 242 in the `CHRD` chunks.

The positions are in the coordinates of the map models and of the cameras, as those of the `CHRD` chunk. In `scene/01/dslt` (a slot machine hall), the `CHRD` chunk is the empty script and the `CHARA` script has the 38 characters of the map: `AKIR`, the slot machines `SLT1` to `SLTG` (all with the model `$SLT02LWX.MT5`) placed in rows along the walls (e.g. `SLT1` to `SLT4` at x = 0 and z from -6.9 to -4.65, at angle 90, and `SLT5` to `SLT8` at x = 5.7, at angle -90), the ceiling fans `FAN1` to `FAN4`, `DRAM` (`$DRM02S2G.MT5`, a set of reels), `RMP1` to `RMP3`, `RMPY`, `RMPR` and `RMPG` (`RAMP`, *probably* lamps), and others. `SLTM` (`$SLT02HXG.MT5`) is placed at (0, -10, 0), under the floor.

Ryo (`AKIR`) has the image `YKB_M`, his body, but only 3 of the 136 map folders have its model: `YKB_M.chrm` in the `mpk00` of `dgct`, `dhqb` and `dkpa` of `scene/01`, with the other parts of Ryo (`YKB_B`, `YKB_F`, the hands `YKB_TL` and `YKB_TR`, the arms `YKB_UL` and `YKB_UR`). The other maps reference `YKB_M` without the model, e.g. `scene/01/daza`, whose `mpk00` only has the arms. The model of the body is in the model folder of each scene, `scene/01/model/chara/ykb_m.mt5` (also in `scene/02` and `scene/03`), and in `misc/coldinit/YKB_M.chrm`.

Other characters also reference models that are only in that folder. In `scene/01/jd99`, which has no packages, the `CHRD` chunk has `AKIR` and `TAXI`, an `Object` with the image `@C85N310G.MT5`, *probably* the taxi: the map folder only has the map models, and the model is `scene/01/model/chara/c85n310g.mt5`.

Not every character of the script is in the map when it is loaded:

- Many have the flag `SLEEP` and no position, e.g. the 6 objects of `scene/01/dbhb` (`SHAKER`, `GLASS`, `SAKE_SET`...). Others have `SLEEP` and a position in the map, e.g. `TAXI` of `scene/01/jd99` at (10.9, -0.1, -2.6).
- Many wait off the scene, e.g. most characters of `scene/01/dcbn`, at (0, 0, -1000). Some wait at positions below 1000, outside the map models: `ZASI` of `scene/01/daza` has a single `Position(300, 0, 0)`.

### Characters of the other packages

Other packages of the folder have their own `CHARA` script, with the characters of other situations of the map: `scene/01/d000` has 17 more (`yoru`, `omg`, `yq14`, `buss`, `auth`...), `scene/01/dcbn` has `dcb0` to `dcb4`, and some bars have the [jukebox](#jukebox) in another package (`jukebox`, `alp_juke`). The data of the [scene program](#scn3--scene-program) usually has their names in uppercase (`COMMON01`, `NINJIN`, `DCB0`...): *probably* the program loads them when needed.

The Hazuki house (`jomo` of `scene/01`, `02` and `03`) has no `CHARA` script in `mpk00`, which only has the map models (`MAP.MAPM` to `MAP08.MAPM` in `mpk00.pks`). The furniture parts that open are in the `CHARA` script of `common01`, `common02` or `common03` (141 characters, the models in the PKS of the same name), as objects with `Flags("SLEEP")`, a first `Position(0, 0, -1000)` and a second `POSITION` and `ANGLE` in the map:

| Characters        | Models                                   | Position (scene/01) | Object |
|-------------------|------------------------------------------|---------------------|--------|
| `DDR1`, `DDR2`    | `REIO101G`, `REIO102G`                   | (12.81, 1.55 and 0.62, -1.8), -90 | Upper and lower doors of the fridge |
| `ATS1` to `ATS7`  | `TANM4w3G` (4), `TANM4w4G` (2), `TANM402G` | x 18.02 to 18.59, z 2.44 | Drawers of the chest of Ryo's room |
| `AKD2` to `AKD4`  | `DESM402G` (2), `DESM403G`               | (16.87, 0.1 to 0.56, 3.77), 90 | Drawers of Ryo's desk |
| `ITS1` to `ITS7`  | as `ATS1` to `ATS7`                      | x 20.26, z -4.15 to -3.58, -90 | Drawers of the chest of Iwao's room |
| `IKD1`, `IKD2`    | `HIKW101G`                               | (20.35, 0.24, -2.26 and -2.55), -90 | Drawers of Iwao's room |
| `BMB1` to `BMB5`, `BMBO` | `BUTM401G` to `BUTM403G`, `BUTK300G` | x 2.63 to 2.69 | Drawers and doors of the Buddhist altar |
| `D1C1` to `D1C4`, `IMC1` to `IMC4` | `CHDS500G`, `CHSS500G`, `CHSS501G` | x 12.89 and x 14.15 | Doors and drawers of two cupboards |
| `DND1`, `DND2`    | `TUDT202G`, `TUDT203G`                   | (11.92, 1.92, -4.84) | Doors of a wall cupboard of the kitchen |
| `itbL`/`itbR`, `btbL`/`btbR`, `ktbL`/`ktbR` | `TNBR102G`, `TNBR101G` | y 2.2 | Left and right doors of the upper closets |

The same script has other objects of the house (the lamps `LGWS2nnG`, the telephone `TEL_`, clocks, pictures, futons, a radio cassette...) and the `DefImage` of the doors of the door table (`DR15_026`, `DR23_000`, `DR01_015`...). Some objects have positions near the origin, e.g. `pwkm` (`WALKMAN`) at (0.02, 0.01, -0.2) and `IDEN` at (0, 0.05, -0.2): *probably* relative to something that holds them. The package `ninjin` of `scene/01/jomo` has a meal for two (rice `GOHR`/`GOHI`, miso soup `MISR`/`MSOI`, chopsticks `HSR1`/`HSR2` and `HSI1`/`HSI2`, the carrots `NINJ`...) on the table at about (10.7, 0.75, -2.5).

Some packages define the same character IDs as another one: they are *probably* alternatives, of which the program loads one. `dcb0` to `dcb4` of `dcbn` and `mky0` to `mky4` of `mkyu` share their 20 IDs (`cnc1`..., `mnc1`..., objects waiting at (0, 0, -1000) with a different image in each package, e.g. `mnc1` has the image `mnc1`, `mnc8`, `mncc`, `mncg` or `mnck`), and `pk_box` and `pk_box2` of `scene/03/ma00` and `mfsy` share the boxes `BOX1`, `BOX2`... (images `BOX_M` and `BOX2_M`). Fewer IDs are shared elsewhere: `HAUK` in `mpm1` and `mpm2` of `jhd0`, `SINF` in `bebf` and `houmon` of `scene/02/jomo`, and the can `CAN1` in `coke`, `fatg`, `fato` and `sprt` of `d000` (`COKS520G`, `FAGS520G`, `FAOS520G`, `SPRS520G`, one per drink).

## ECAM — Cameras

A list of fixed or animated cameras, each one with a 4-digit ID. The game files have 4924 cameras, 1 to 261 per file. The chunk header is followed by the cameras, one after the other, until the end of the chunk.

### Camera

| Offset | Size | Type    | Name    | Description |
|-------:|-----:|---------|---------|-------------|
| 0x00   | 4    | char[4] | `id`    | Camera ID: 4 ASCII digits (`0000`, `0101`, `5010`...) |
| 0x04   | 4    | uint32  | `size`  | Size of the camera in bytes, from `id`. The next camera starts at `offset + size` |
| 0x08   | 4    | uint32  | `flags` | Bit `0x20`: there is a roll curve. Bit `0x40`: there is a field of view curve. Bit `0x400000`: [transition from the current camera](#transitions-from-the-current-camera). The rest of the bits (`0x80` and the higher bits: values such as `0x197660`) are unknown. The usual values of the low byte are `0x60` (4381 cameras), `0x40` (409) and `0x00` (134) |
| 0x0C   | —    | —       | curves  | 6 to 8 curves, with the same format as the [curves of AUTH](AUTH#animation-curves): `uint32 count`, `float times[count]`, `float values[count]`, `float tangents[count]` (`4 + 12 × count` bytes) |

| Curve | Present          | Description |
|------:|------------------|-------------|
| 0–2   | Always           | Position X, Y, Z |
| 3–5   | Always           | Target X, Y, Z (the point the camera looks at) |
| next  | If `flags & 0x20` | Roll in degrees (usually 0) |
| next  | If `flags & 0x40` | Field of view in degrees (usually 60) |

Most cameras have curves of one key (a fixed camera). The camera of the `ACAM` chunk of [AUTH](AUTH) has the same curves, with the value `0x61` in the place of `flags`.

### Transitions from the current camera

Bit `0x400000` of `flags` is set in 362 cameras, always together with bit `0x200000` (neither appears without the other). In all of them every key at time 0 has the value 0, even in the field of view curve: the keys at time 0 are *probably* a placeholder of the camera at the start, and the camera is a transition from the current one to the values of the last keys. The roll and field of view curves often have no key at time 0, only one key at the end (for example cameras `0150` and `0151` of the shops `dbhb`, `dgct`, `dsli`, `mfsy` and `mkyu`: one key at 1 s with a roll of 5 and a field of view of 15). A few curves have only the placeholder key (the target of camera `7211` of `dgct` and `mkyu`, all the curves of cameras `1100` and `1200` of `scene/02/d000`).

The values of the transitions depend on bit `0x200` (see [Cameras near the origin](#cameras-near-the-origin)): 189 of the 203 transitions with it stay within 5 units of the origin (142 of 156 in the maps with a camera beyond 30 units), and only 32 of the 159 without it (3 of 63 in those maps). The transitions without bit `0x200` end at a place of the map; those with it *probably* end at a place relative to a character.

### Cameras near the origin

Bit `0x200` (almost always together with bit `0x2000`) is set in many cameras whose values stay near the origin, even in maps whose other cameras are 100 or more units away. In the maps with a camera beyond 30 units, 985 of the 1200 cameras with bit `0x200` stay within 5 units of the origin, against 24 of the 1087 cameras without it. Those positions are *probably* relative to a character or an object, not to the map. Cameras `0150` to `0152` of the shops are an example: their targets are about 0.25 units from the origin, with a field of view of 15 (a close-up). The 13 files that have them (`dbhb`, `dgct`, `dsli`, `mfsy` and `mkyu`) are the bars with a jukebox, and their [scene program](#scn3--scene-program) contains the strings `JBOX` and `JCNT`: they are *probably* the cameras of the jukebox, relative to it or to Ryo in front of it. Its position isn't in the chunks of the file but in a CHRT script of the map folder (see [Jukebox](#jukebox)); only `scene/03/m3fb` and `scene/03/ma00` also contain `JBOX`, without these cameras.

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

| Group  | Meaning | Prefix | Description |
|--------|---------|--------|-------------|
| `COLI` | COLIsion | `uint32 type` (0, 9, 100, 1, 10, 7, 200...) | Collision walls and obstacles |
| `HGHT` | HeiGHT | `float height` | *Probably* areas with a different floor height (-1.8, 5.4...) |
| `EVNT` | EVeNT | `uint32 unknown` (`0xFF`, `0x100FF`, `0x40001`, `0x40002`...) | *Probably* event areas |
| `UNDU` | UNDUlation | `float heights[4]` | Sloped floor ("undulation"): *probably* the height at each corner of the shape (the fourth one is usually 0 for triangles) |
| `SOND` | SOuND | `uint32 unknown` | *Probably* sound areas |
| `PROP` | PROPerty | `char[4] type` + `uint32 unknown[4]` (20 bytes) | Areas with a property: `CAM2` (609), `STIR` (260), `WALK` (129), `CAM3` (127), `CNT0` (98), `PRIV` (45), `INSH` (39), `CAM1`, `TALK`, `STOP` |

### Shape

All the coordinates are in the horizontal plane (X, Z). Z has the opposite sign to the Z of the map models, the cameras and the lights: a shape point `(x, z)` is at `(x, -z)` in the map (the collisions follow the walls and streets of the models when Z is negated).

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

Contains one set (78 chunks) or two sets (2 chunks, size `0xA8`: `scene/02/mksg` and `scene/03/mfbt`) of three chunks `DEMO`, `FOG ` and `BACK`, each one with a [chunk header](#chunk-header-8-bytes). The 80 `FOG ` chunks are in 78 files: `daza` of `scene/01` and `scene/02` have two identical `FOG ` chunks of 0x58 bytes.

| Chunk  | Size | Data | Description |
|--------|-----:|------|-------------|
| `DEMO` | 0xC  | `uint32` | Unknown. Always 1 |
| `FOG ` | 0x1C | `float near`, `float far`, `float density`, `uint32 color`, `uint32 unknown` | Fog: distance where it starts and where it is complete (e.g. 0.1 and 150), *probably* its density (0.08 to 1) and its color (ARGB, e.g. `0xFF605010`). The last value is always 1. The default values are `20, 800, 1, 0xFFFFFFFF` |
| `BACK` | 0x28 | `uint32 colors[4]`, `uint32`, `uint32`, `uint32[2]` | *Probably* the colors of the background. The fifth value is always 0, the sixth 0 or 1, the last two 0 |

## SCEX — Cutscenes

Contains `TRCK` chunks: [AUTH](AUTH) cutscenes, with their `ASEQ`, `ACAM`, `AMOV` and optional `ASTR` chunks. `scene/01/op00` also has two `MLWD` chunks of 0x2C bytes, unknown, between the `TRCK` chunks (after the 10th and the 22nd). An `SCEX` chunk can be empty (size 8): `jabe` of the three discs and `scene/03/tery`.

The game files have 259 `TRCK` chunks in 18 files, but 74 of them are empty: only the 8-byte header (`TRCK`, size 8), with no `ASEQ`. They are only found in the three `jomo` files (the Hazuki house), which have 49 `TRCK` chunks each, empty or not, as if each disc filled its own cutscenes in a table of 49:

| File | `TRCK` | Cutscenes | Empty |
|------|-------:|----------:|------:|
| `scene/01/jomo` | 49 | 21 | 28 |
| `scene/02/jomo` | 49 | 30 | 19 |
| `scene/03/jomo` | 49 | 22 | 27 |
| `scene/01/op00` | 47 | 47 | 0 |
| `scene/03/nbik` | 23 | 23 | 0 |
| `scene/01/op02` | 7 | 7 | 0 |
| `scene/02/arar`, `scene/03/dxms` | 5 | 5 | 0 |
| `scene/02/gmct` | 4 | 4 | 0 |
| `betd` (3 discs), `scene/01/tatq` | 3 | 3 | 0 |
| `scene/01/yq14`, `scene/02/dski`, `scene/03/msbs`, `scene/03/ydma` | 2 | 2 | 0 |
| `scene/03/arar` | 1 | 1 | 0 |

The empty chunks are scattered among the others (e.g. `scene/01/jomo` starts with two empty chunks, then a cutscene, two empty chunks...).

### The map of scene/01/op00

The folder of `scene/01/op00` (the prologue, 47 cutscenes) has no model of the map: its map and its characters are in `op00/op99.afs`, an [AFS](AFS) without [IDX](IDX) whose 32 entries are 16 pairs of a [PKF](PKF) (the textures and a `CHARA.CHRT`) and a [PKS](PKS) (the models). The program of the file names them by the AFS and the indices of each pair (`OP99.afs#000#001`... `OP99.afs#030#031`), several times, next to the names `OPEN1`, `OPEN2`, `A0114` and `A01103`.

| Entries | Models of the PKS |
|---------|-------------------|
| 0–23 | Characters and objects (e.g. Iwao `IWA_M`, 16–17; Lan Di `KOK_M`, 18–19; Fukusan `FUK_M`, 8–9; `BLA_M` and `BLB_M` (`KURA` and `KURB` of the cutscenes), 12–13; Ine `INE_M`, 4–5) |
| 24–25 | `OMO.MAPM`, and `YKD_M` |
| 26–27 | `JIMENHAL.MAPM`, `NAIB.MAPM`, `NIWAKAL.MAPM`, `OMADO.MAPM`, and `COLLI.COLI` |
| 28–29 | `OOSAKI.MAPM` |
| 30–31 | `JYUU.MAPM` |

The map models are the `MAPM` files (the extension in the [IPAC](IPAC) dictionary; their data is an [MT5](MT5) model), which are not named `MAP*` as in the other maps: these are the only `MAPM` files with another name (none of the PKF/PKS files of the scene folders has one). They are in the same space as the cutscenes of the file: e.g. Iwao and Lan Di fight inside the dojo in the cutscene 18, and the characters of the cutscenes are those of the `CHARA.CHRT` scripts of the pairs (`SORY`, Lan Di, is the `KOK_M` of the pair 18–19).

### Motions and characters

The cutscenes of a map play the motions ([MOTN](MOTN)) of the map, as the [AUTH](AUTH) files of a PKS named as a map (see [Motions of the maps](MOTN#motions-of-the-maps)): the `m_*.bin` files of the folder of the map (e.g. `scene/03/nbik/m_01bike.bin`), the MOTN files of the [PKS](PKS) files of the folder of the map (e.g. `M_01JA` of `scene/01/jomo/common01.pks`, `M_0605` of `scene/01/op02/shd2.pks`), or the [common motions](MOTN#common-motions) for the small IDs. Of the 450 motions played by the cutscenes (each motion counted once per cutscene, with the last frame played by its commands), 447 are found with that length: 260 in the `m_*.bin` files, 172 in the PKS files of the map (`jomo`, `op02`, `tatq`, `scene/03/arar`, `dxms`) and 15 in the common motions. The other three are `0x1913` in `scene/01/op00`, `0x1004` in `scene/01/yq14` (the same ID that is missing in `scene/01/d000/yq14.pks`) and `0x1CB1` in `scene/02/jomo`.

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

| Chunk  | Type   | Probable meaning | Description |
|--------|--------|------------------|-------------|
| `MAPR` | int32  | number of MAP models | 1 (97 files), -1 (19), 0 (6), or 2 to 22. In the 26 folders with loose `MAP.MT5`, `MAP01.MT5`, `MAP02.MT5`... models and no `mpk*.pkf` packages, *probably* the number of numbered models: equal to it in 17 (0 with only `MAP.MT5`, 11 in `betd`, 22 in `nbik`...), lower in 6 (12 of 13 in `dnoz`, 15 of 17 in `mbqc` and `mc5q`, 0 of 1 in `toki` of `scene/02`), and -1 in `jhd0` (1). -1 in the folders without map models (`yd01`, `op00`, `ms08`...) and in `jomo`. 1 in all the files with `MAPT` |
| `MAPT` | int32  | MAP Type | Always 1. Present in 93 of the 96 folders whose map models are in `mpk*.pkf` packages (all but the 3 `jomo`), and in no other |
| `DIRT` | int32  | DIRecTion | *Probably* a direction in degrees: 0 to 343 (90, 220, 335, 260, 180...). The same in a map of every disc |
| `SCRL` | int32  | SCRoLl | Unknown: -1 (57 files), 0, 1 or 23 |
| `SCOF` | float  | — | Unknown: -4 (6 files), -6 (3) or -2 (3) |
| `BLKS` | float  | BLocKS | Unknown: 100 |

## List chunks

Lists of `int32` values ended by -1.

| Chunk  | Example | Description |
|--------|---------|-------------|
| `MAPD` | `4, 8, 12, 13, 14, 15, -1` | Unknown (`mkyu`, `m3fb`, `ma00`, `mbqc`, `mc5q`) |
| `SCLM` | `0, 7, 1, 28, -1` | Unknown (`scene/01/op00`, `scene/03/msbs`) |
