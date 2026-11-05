# AUTH — Cutscene sequence

An AUTH file describes a scripted cutscene: a timeline of commands (which characters take part, which motions they play, their voice clips and sound effects), the animation of the camera, the movement of each character in the scene and the names of the sounds used.

They are stored in the [IPAC](IPAC) block of the [PKS](PKS) files of the scenes, with the `AUTH` extension and names such as `SEQDATA0`…`SEQDATAD` (also `SEQDATK0` and `SEQDATS1`). The same PKS holds the motions ([MOTN](MOTN); for a PKS named as a map, they are in the [map](MOTN#motions-of-the-maps); for some PKS split in a family, in [another PKS of the family](MOTN#motions-of-a-family-of-pks)), the models ([CHRM](MT5)) and the character script ([CHRT](CHRT)) used by the cutscene. The game files contain 306 AUTH files, in 119 PKS files of `scene/01`, `scene/02` and `scene/03`, from 796 to 108052 bytes. The `SCEX` chunk of [MAPINFO](MAPINFO) files holds 259 more `TRCK` chunks, and its `ECAM` cameras use the same [curves](#animation-curves).

- **Extension:** `.auth` (`AUTH` in the [IPAC](IPAC) dictionary)
- **Magic:** `TRCK` (`54 52 43 4B`)
- **Compression:** none in the game files (the [PKS](PKS) files that contain them are [gzipped](Home#compression))

The sound names in `ASTR` are paths of the authoring tool (`/p16/prj16m5/AUTH01/...`), which gives the format its name.

## Layout

The file is a `TRCK` chunk that contains four chunks, always in this order. `ASTR` is missing in 65 of the 306 files (those without voices or sound effects).

```
+------------------------+ 0x00
| TRCK header (8 bytes)  |
+------------------------+ 0x08
| ASEQ  commands         |
+------------------------+
| ACAM  camera           |
+------------------------+
| AMOV  character moves  |
+------------------------+
| ASTR  sound names      |  optional
+------------------------+ size
```

## Chunk header (8 bytes)

`TRCK` and every chunk inside it start with the same header. The size includes the header, so the next chunk starts at `offset + size`.

| Offset | Size | Type    | Name   | Description                                           |
|-------:|-----:|---------|--------|-------------------------------------------------------|
| 0x00   | 4    | char[4] | `id`   | `TRCK`, `ASEQ`, `ACAM`, `AMOV` or `ASTR`              |
| 0x04   | 4    | uint32  | `size` | Size of the chunk in bytes, header included. For `TRCK`, the size of the file |

Time is measured in two units: the commands of `ASEQ` use **frames** (30 per second) and the curves of `ACAM` and `AMOV` use **seconds**.

## ASEQ — Commands

The timeline of the cutscene. All offsets in this section are relative to the start of the `ASEQ` chunk.

| Offset | Size | Type   | Name       | Description                                    |
|-------:|-----:|--------|------------|------------------------------------------------|
| 0x00   | 8    | —      | header     | [Chunk header](#chunk-header-8-bytes) (`ASEQ`) |
| 0x08   | 4    | uint32 | `unknown`  | Always `0x500` (1280)                          |
| 0x0C   | 4    | uint32 | `duration` | Length of the cutscene in frames               |
| 0x10   | 4    | uint32 | `unknown`  | Always 1                                       |
| 0x14   | —    | —      | commands   | Command stream, up to the end marker           |

The stream is a list of 32-bit words. A word `0` is a **wait**: the next word is the frame at which the following commands run (the frames only grow). A wait to frame `0xFFFFFFFF` ends the stream, at the end of the chunk. The commands before the first wait run at frame 0, and several commands can follow the same wait.

Any other word is a command header followed by its arguments:

| Offset | Size | Type   | Name     | Description                                  |
|-------:|-----:|--------|----------|----------------------------------------------|
| 0x00   | 1    | uint8  | `opcode` | Command (see below)                          |
| 0x01   | 1    | uint8  | `count`  | Number of 32-bit arguments that follow       |
| 0x02   | 2    | uint16 | `unused` | Always 0                                     |
| 0x04   | 4×N  | —      | `args`   | Arguments                                    |

Characters are identified by a 4-character ID (`char[4]`, e.g. `AKIR` for Ryo, `YAMA`, `SMTH`), the same ones used by the other formats (see [Characters](#characters)). The commands that play a sound (5 and 6) number them in order: the first one plays sound 0, the second one sound 1, and so on; the number is stored in the command and is the index of the sound name in `ASTR`.

### Commands

| Opcode | Arguments | Count in the game files | Description |
|-------:|----------:|------------------------:|-------------|
| 1      | 2 | 306  | Start of the sequence. Always the first command |
| 2      | 2 | 996  | Declares a character |
| 3      | 5 | 2562 | Plays a motion on a character |
| 4      | 4 | 714  | Unknown, on a character |
| 5      | 5 | 1880 | Plays a voice clip |
| 6      | 3 or 9 | 1132 / 2892 | Plays a sound effect |

**1 — Start**

| Arg | Type    | Description            |
|----:|---------|------------------------|
| 0   | char[4] | Always `(^^;`          |
| 1   | uint32  | Always 0               |

**2 — Declare character.** All of them come before the first wait.

| Arg | Type    | Description                                     |
|----:|---------|-------------------------------------------------|
| 0   | char[4] | Character ID                                    |
| 1   | uint32  | Index of the character: 0, 1, 2… in order        |

**3 — Motion**

| Arg | Type    | Description                                                                 |
|----:|---------|-----------------------------------------------------------------------------|
| 0   | char[4] | Character ID (always a declared one)                                        |
| 1   | uint32  | [Motion ID](MOTN#motion-ids). Most values are `0x10xx`, `0x11xx` or `0x20xx` (e.g. `0x1003`, `0x2003`), motions of the MOTN files of the same PKS (or of the [map](MOTN#motions-of-the-maps) with the name of the PKS); a few are small (`0x59`, `0x2DF`, `0x55E`), motions of the [common motions](MOTN#common-motions) of `motion/motion.bin` (e.g. `0x2DF`, the walk `AKI_AKI_WALK_SP1_LP`) |
| 2   | uint32  | First frame of the motion                                                   |
| 3   | uint32  | Last frame of the motion (always ≥ arg 2)                                   |
| 4   | uint32  | Always 0                                                                    |

The frames of args 2 and 3 are frames of the motion (30 per second), played from the frame of the command. The position of the hips in the motion (bone 0, see [MOTN](MOTN#skeleton)) moves the character from the position of its curves, in the space of the character: while a motion moves the hips (e.g. 2.9 m for `0x100A` in `scene/01/d000/d0w0/SEQDATA4`), the curves of the character stay still, and while the curves move a character a long way (a character sitting in the bus of `scene/01/d000/buss/SEQDATA1`, 29 m), its motion keeps the hips in place.

**4 — Unknown**

| Arg | Type    | Description                                                      |
|----:|---------|------------------------------------------------------------------|
| 0   | char[4] | Character ID                                                     |
| 1   | uint32  | Unknown, 0 to 12                                                 |
| 2   | uint32  | Unknown, usually 0 (up to 6)                                     |
| 3   | uint32  | Unknown, 1 to 49; most often 10, 20 or 1 (it looks like a number of frames) |

**5 — Voice clip.** The sound name is a path in a `SOUND` folder (e.g. `.../01TEL/SOUND/01TELA001.aiff`), whose file name is the name of the [xWMA](XWM) clip in the [IDX](IDX) of a voice [AFS](AFS) of the same scene (`scene/0N/stream/pc/eng/` and `.../jap/`). The name of the AFS is the name of the clip without its last 4 characters (the speaker letter and the number), which is not always the name of the folder: `01TELA001` is in `01tel.afs`, `01JUCEAA005` in `01jucea.afs`, and the clips of the `01YAMA` folder (`E1020B001`…) in `e1020.afs`.

| Arg | Type    | Description                                                         |
|----:|---------|---------------------------------------------------------------------|
| 0   | char[4] | Character that speaks                                               |
| 1   | uint32  | Sound number (index in `ASTR`)                                      |
| 2   | int32   | Always -1                                                           |
| 3   | int32   | Always -1                                                           |
| 4   | float   | Length of the clip in seconds. It is close to, but not the same as, the length of the [XWM](XWM) clip with that name |

**6 — Sound effect.** The 3-argument form plays the sound; the 9-argument form adds a second character and a length.

| Arg | Type    | Description                                                                 |
|----:|---------|-----------------------------------------------------------------------------|
| 0   | char[4] | Character that makes the sound                                              |
| 1   | uint32  | Sound ID: the low 16 bits are the ID of a group of the [sequence bank](SND#section-3-sequence-bank) of a [SND](SND) bank (`0x04A9`, or `0x0FA9` in 13 commands), the high 16 bits the index of an [effect sequence](SND#effect-sequence) in that group (0 to 38). `0xFFFFFFFF` in 33 commands, which play no sound. See [Sound bank](#sound-bank) |
| 2   | uint32  | Sound number (index in `ASTR`)                                              |
| 3   | char[4] | *9 arguments only.* A declared character, or a value `0x1000xxxx` / `0x1001xxxx` (160 commands) |
| 4   | uint32  | *9 arguments only.* Always `0x7F` (127)                                     |
| 5   | uint32  | *9 arguments only.* Always 4                                                |
| 6   | uint32  | *9 arguments only.* Always `0x10` (16)                                      |
| 7   | uint32  | *9 arguments only.* Always 0                                                |
| 8   | float   | *9 arguments only.* Length of the sound in seconds (the same for every use of a sound name): the length of the sample of the effect plus about 0.5 s (0.498) |

See [Sound bank](#sound-bank) for the bank of the sequences.

Example (`scene/01/d000/auth.pks`, `SEQDATA0`):

```
frame   0  1 Start        "(^^;"
frame   0  2 Character    AKIR = 0
frame   0  3 Motion       AKIR 0x1418 frames 18-925
frame  59  6 Sound        AKIR 0x000004A9 sound 0  (smallobject/book1.aiff)
...
frame 489  5 Voice        AKIR sound 6 3.30 s     (SOUND/01TELA001.aiff)
...
frame 856  6 Sound        AKIR 0x000104A9 sound 9  (smallobject/bookcl.aiff)
end
```

### Sound bank

The bank of the sound commands is not named in the AUTH file: the [program](MAPINFO#scn3--scene-program) of the map that contains the cutscene (the `mapinfo.bin` of the parent folder of its PKS) loads it with the cutscene. In `scene/01/d000`, one or more functions of the program load the name of the PKS of the cutscenes, their `seqdataN.bin` files, the name of the [AFS](AFS) of their voices and the banks of the `sound` folder of the scene (see [Strings of the code](MAPINFO#strings-of-the-code)). Of the banks loaded by the functions that load the name of the PKS or of the AFS of the voices, a single one has a group with the ID of the sound commands (`0x04A9`), the bank of the cutscene; the others are music banks (groups `xxA8`, e.g. `3FA8` in `bgm085.snd`) or other banks (`76AB` and `77AB` in `battle_1.snd`):

| PKS | Names loaded with the bank | Banks loaded | Bank of the cutscene |
|-----|----------------------------|--------------|----------------------|
| `auth` | `AUTH`, `seqdata0.bin`, `01TEL` | `a1_telp.snd` | `a1_telp.snd` |
| `buss` | `BUSS`, `seqdata1.bin`, `seqdata2.bin`, `seqdata4.bin`, `seqdata5.bin` | `a1_busno.snd` | `a1_busno.snd` |
| `d0w0` | `D0W0`, `seqdata1.bin`…`seqdataC.bin`, `E1020` | `battle_1.snd`, `a1_yamaw.snd`, `bgm085.snd` | `a1_yamaw.snd` |
| `djhn`, `djhn_01`…`djhn_07` | `01JUCEA` (the PKS are loaded by other functions) | `a1_yanji.snd` | `a1_yanji.snd` |
| `drauth` | `DRAUTH`, `01REV` | `battle_1.snd`, `a1_senfk.snd`, `bgm013.snd` | `a1_senfk.snd` |
| `ybhn` | `YBHN`, `seqdata0.bin`, `DR15_012` | `bgm018.snd`, `a1_yobi.snd` | `a1_yobi.snd` |

With these banks, every sound command with a sound ID of the 33 files of these PKS with sound commands has an effect sequence in the bank, also those of the files with only 3-argument commands. The sequence is not always the effect with the same index: in `a1_busno.snd`, sequences 0 to 27 play effects 0 to 27, sequence 28 stops effect 27 (volume 0), 29 plays effect 28 and 30 stops it, and sequences 31 to 38 play effects 29 to 36 (the commands of sequences 37 and 38 of `buss/SEQDATA2` have the length of effects 35 and 36). The functions that load `YQ14` (`d000/yq14`, sound ID `0x0FA9`) load no bank, and the program of `scene/01/yd01` (`sakr`) is a bytecode.

## Characters

The file only has the IDs of the characters. Their models are those of the `Character` blocks with the same ID in the [CHRT](CHRT) scripts (the model file of their first `Image`), found in:

- the `CHARA.chrt` of the same [PKS](PKS), for the characters and objects of the cutscene (e.g. the bus driver `BUSS`, `OGM_L.chrm`, and the policeman `COP_` of `scene/01/d000`);
- the `CHARA.chrt` of the folder of the character in the scene, `scene/0N/stream/humans/<ID>` (e.g. `YAMA`, `YMG_L.chrm`; `SMTH`, `GIB_M.chrm`; the bus `BUS_`, `BUSS530G.chrm`);
- the characters of the [MAPINFO](MAPINFO) of the map that contains the cutscene (the parent folder): Ryo, `AKIR`, is `YKB_M`, whose model is `scene/01/model/chara/ykb_m.mt5`.

The persons (`Human` command) have the origin of their model at the hips; the position of their curves is the point of the floor where they stand.

## Animation curves

`ACAM` and `AMOV` store their values as curves of keys. A curve with `count` keys:

| Offset    | Size    | Type     | Name       | Description                                              |
|----------:|--------:|----------|------------|----------------------------------------------------------|
| 0x00      | 4       | uint32   | `count`    | Number of keys (can be 0: the curve is not used)         |
| 0x04      | 4×count | float[]  | `times`    | Time of each key in seconds. The first one is 0, they only grow |
| 0x04+4×count | 4×count | float[] | `values` | Value at each key                                        |
| 0x04+8×count | 4×count | float[] | `tangents` | Slope of the curve at each key, in units per second (characters). Usually 0 in the camera curves |

The size of a curve is `4 + 12 × count` bytes. Two keys with the same time make a jump in the value; the camera curves use it at the Two keys one frame apart (1/30 s) whose value changes much more than in a frame of the spans around them are also a jump: no frame of the cutscene falls between them, so the value changes from one frame to the next. The characters use them to be placed where their motion left them when the next motion starts: in `scene/01/d000/buss/SEQDATA5`, Ryo plays the walk `0x2DF` (frames 1–36) at the frames 166, 202 and 238, and between the frames 201 and 202 and 237 and 238 his position jumps 1.43 m forward, about as far as the hips move in the motion from frame 1 to frame 36 (1.44 m over the whole loop), while it stays still during each walk; in `buss/SEQDATA1` and `SEQDATA4` it jumps 1.47 m at frame 126, when his motion changes, while the spans around it move about 1 mm per frame. Keys one frame apart are also found inside a movement, as ordinary keys: the objects carried by a character have a key every 1 to 3 frames (e.g. the bottle `BIN_` of `scene/01/d000/yq14/SEQDATA2`, which stays at about 0.14 m from the wrist that the motion of `SMTH` gives to bone 29, see [MOTN](MOTN#skeleton)). In the 39 files of `scene/01` studied, the 1106 spans of one frame whose value changes in the curves of the characters split in two groups by the ratio between their change and the change per frame of the faster span around them: 853 change at most 8 times as much (most of them 1 to 2 times, the keys of a movement), and 253 more than 8 times, up to more than 1000 (194 of them between spans that keep their value, the jumps); between 4 and 8 there are only 6.

Most keys are stored twice, with the same time, value and tangent; the two keys of a jump have the same time and different values. The tangents of the curves of the characters are the slope of the curve in units per second: at the keys between two others, the tangent divided by the slope between the previous and the next key has a median of 1.00 (2710 keys; half of them between 0.48 and 1.05). The curves are *probably* cubic Hermite curves with these tangents. In the camera curves the tangents are 0 (in at least three quarters of the 2508 keys measured).h walk.

## ACAM — Camera

| Offset | Size | Type   | Name      | Description                                               |
|-------:|-----:|--------|-----------|-----------------------------------------------------------|
| 0x00   | 8    | —      | header    | [Chunk header](#chunk-header-8-bytes) (`ACAM`)            |
| 0x08   | 4    | uint32 | `unknown` | Always 0                                                  |
| 0x0C   | 4    | uint32 | `count`   | Number of cameras. Always 1                               |
| 0x10   | 4    | uint32 | `offset`  | Position of the camera, from the start of the chunk. Always `0x18` |
| 0x14   | 4    | uint32 | `end`     | End of the camera data: the size of the chunk             |
| 0x18   | 4    | uint32 | `unknown` | Always `0x61` (97)                                        |
| 0x1C   | —    | —      | curves    | 8 [curves](#animation-curves)                             |

| Curve | Description                                                    |
|------:|----------------------------------------------------------------|
| 0     | Position X                                                     |
| 1     | Position Y                                                     |
| 2     | Position Z                                                     |
| 3     | Target X (the point the camera looks at)                       |
| 4     | Target Y                                                       |
| 5     | Target Z                                                       |
| 6     | Probably the roll in degrees (0 in most files, between -17 and 25) |
| 7     | Field of view in degrees (usually 55.4; between 32 and 92)     |

The time of the last key of curve 0 is usually the `duration` of `ASEQ` (in seconds instead of frames).

## AMOV — Character movement

One entry per character, in the same number as the characters declared by command 2.

| Offset | Size      | Type     | Name      | Description                                              |
|-------:|----------:|----------|-----------|----------------------------------------------------------|
| 0x00   | 8         | —        | header    | [Chunk header](#chunk-header-8-bytes) (`AMOV`)           |
| 0x08   | 4         | uint32   | `unknown` | Always 0                                                 |
| 0x0C   | 4         | uint32   | `count`   | Number of entries                                        |
| 0x10   | 4×count   | uint32[] | `offsets` | Position of each entry, from the start of the chunk      |
| —      | 4         | uint32   | `end`     | End of the last entry: the size of the chunk             |
| —      | —         | —        | entries   | 9 [curves](#animation-curves) each                       |

| Curve | Description                                                    |
|------:|----------------------------------------------------------------|
| 0     | Position X                                                     |
| 1     | Position Y                                                     |
| 2     | Position Z                                                     |
| 3     | Rotation X in degrees                                          |
| 4     | Rotation Y in degrees (the direction the character faces)      |
| 5     | Rotation Z in degrees                                          |
| 6–8   | Unknown. Empty in 964 of the 996 entries; in the others they are a small point (X and Z between -4 and 4, Y between 0.4 and 1.7) |

## ASTR — Sound names

The names of the sounds played by commands 5 and 6, in the order they are played.

| Offset | Size        | Type     | Name      | Description                                             |
|-------:|------------:|----------|-----------|---------------------------------------------------------|
| 0x00   | 8           | —        | header    | [Chunk header](#chunk-header-8-bytes) (`ASTR`)          |
| 0x08   | 4           | uint32   | `unknown` | Always 0                                                |
| 0x0C   | 4           | uint32   | `count`   | Number of names: the number of sound commands in `ASEQ` |
| 0x10   | 4×(count+1) | uint32[] | `offsets` | Position of each name, from the start of the chunk. The extra last one is the end of the names |
| —      | —           | char[]   | names     | Null terminated strings, padded with zeros to 4 bytes   |

Every name is the path of an `.aiff` file in the authoring tool, e.g. `/p16/prj16m5/AUTH01/01TEL/SE/smallobject/book1.aiff`. The voice clips are in a `SOUND` folder; the sound effects in folders such as `foot`, `footsoil`, `cloths2`, `smallobject`, `vehicle`. A name appears once per use, so the same path can be repeated.
