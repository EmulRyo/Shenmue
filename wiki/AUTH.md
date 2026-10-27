# AUTH — Cutscene sequence

An AUTH file describes a scripted cutscene: a timeline of commands (which characters take part, which motions they play, their voice clips and sound effects), the animation of the camera, the movement of each character in the scene and the names of the sounds used.

They are stored in the [IPAC](IPAC) block of the [PKS](PKS) files of the scenes, with the `AUTH` extension and names such as `SEQDATA0`…`SEQDATAD` (also `SEQDATK0` and `SEQDATS1`). The same PKS holds the motions (`MOTN`), the models ([CHRM](MT5)) and the character script ([CHRT](CHRT)) used by the cutscene. The game files contain 306 AUTH files, in 119 PKS files of `scene/01`, `scene/02` and `scene/03`, from 796 to 108052 bytes.

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

Characters are identified by a 4-character ID (`char[4]`, e.g. `AKIR` for Ryo, `YAMA`, `SMTH`), the same ones used by the other formats. The commands that play a sound (5 and 6) number them in order: the first one plays sound 0, the second one sound 1, and so on; the number is stored in the command and is the index of the sound name in `ASTR`.

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
| 1   | uint32  | Motion ID. Most values are `0x10xx`, `0x11xx` or `0x20xx` (e.g. `0x1003`, `0x2003`); a few are small (`0x59`, `0x2DF`, `0x55E`) |
| 2   | uint32  | First frame of the motion                                                   |
| 3   | uint32  | Last frame of the motion (always ≥ arg 2)                                   |
| 4   | uint32  | Always 0                                                                    |

**4 — Unknown**

| Arg | Type    | Description                                                      |
|----:|---------|------------------------------------------------------------------|
| 0   | char[4] | Character ID                                                     |
| 1   | uint32  | Unknown, 0 to 12                                                 |
| 2   | uint32  | Unknown, usually 0 (up to 6)                                     |
| 3   | uint32  | Unknown, 1 to 49; most often 10, 20 or 1 (it looks like a number of frames) |

**5 — Voice clip.** The sound name is a path in a `SOUND` folder (e.g. `.../01TEL/SOUND/01TELA001.aiff`), whose file name is the name of the clip in the [IDX](IDX) of the voice [AFS](AFS) (`01tel.afs`).

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
| 1   | uint32  | Sound ID: low 16 bits `0x04A9` (or `0x0FA9` in 13 commands), high 16 bits a number from 0 to 38 that changes from one file to another for the same sound. `0xFFFFFFFF` in 33 commands |
| 2   | uint32  | Sound number (index in `ASTR`)                                              |
| 3   | char[4] | *9 arguments only.* A declared character, or a value `0x1000xxxx` / `0x1001xxxx` (160 commands) |
| 4   | uint32  | *9 arguments only.* Always `0x7F` (127)                                     |
| 5   | uint32  | *9 arguments only.* Always 4                                                |
| 6   | uint32  | *9 arguments only.* Always `0x10` (16)                                      |
| 7   | uint32  | *9 arguments only.* Always 0                                                |
| 8   | float   | *9 arguments only.* Length of the sound in seconds (the same for every use of a sound name) |

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

## Animation curves

`ACAM` and `AMOV` store their values as curves of keys. A curve with `count` keys:

| Offset    | Size    | Type     | Name       | Description                                              |
|----------:|--------:|----------|------------|----------------------------------------------------------|
| 0x00      | 4       | uint32   | `count`    | Number of keys (can be 0: the curve is not used)         |
| 0x04      | 4×count | float[]  | `times`    | Time of each key in seconds. The first one is 0, they only grow |
| 0x04+4×count | 4×count | float[] | `values` | Value at each key                                        |
| 0x04+8×count | 4×count | float[] | `tangents` | Unknown; 0 for the flat parts of the curve (probably the slope at each key) |

The size of a curve is `4 + 12 × count` bytes. Two keys with the same time make a jump in the value; the camera curves use it at the cuts.

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
