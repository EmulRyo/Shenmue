# SCNF — Dialogue lines

A SCNF file holds the dialogue lines of one or more characters: for each line, the name of its voice clip, its duration and its subtitle. It also holds a script (bytecode) that isn't known yet.

Every character of `scene/01/stream/humans` has one, inside the [IPAC](IPAC) block of its [PKS](PKS) (e.g. `AKMI.pks` → `AKMI.BIN`, next to the model `HOS_L.CHRM`). Its IPAC extension is `BIN`. SCNF data is also found inside other files: `misc/cold.bin` (a [PKS](PKS) whose SCNF entry has 6 blocks with the lines Ryo says when it is late) and the end of `scene/0x/jhd0/mapinfo.bin`.

- **Extension:** none of its own (`BIN` in the [IPAC](IPAC) dictionary)
- **Magic:** `SCNF` (`53 43 4E 46`)
- **Compression:** none in the game files (the [PKS](PKS) files that contain them are [gzipped](Home#compression))

## Layout

```
+------------------------+ 0x00
| Header (16 bytes)      |
+------------------------+ 0x10
| Block 0                |
| Block 1                |
| ...                    |
+------------------------+ size
```

Each block:

```
+------------------------+ 0x00
| Block header (0x30)    |
+------------------------+ 0x30
| Unused (0x10 bytes)    |  zeros
+------------------------+ positionsOffset (0x40)
| Positions              |  positionCount × 12 bytes
+------------------------+ charactersOffset
| Characters             |  characterCount × 4 bytes
+------------------------+ voicePathOffset
| Voice path             |  null terminated, padded to 4 bytes
+------------------------+ scriptOffset
| Script                 |  bytecode, unknown
+------------------------+ linesOffset
| Lines                  |  16 bytes each
+------------------------+
| Strings                |  voice name + subtitle of each line, padded to 4 bytes
+------------------------+ size
```

## Header (16 bytes)

| Offset | Size | Type    | Name         | Description                              |
|-------:|-----:|---------|--------------|------------------------------------------|
| 0x00   | 4    | char[4] | `id`         | Magic `SCNF`                             |
| 0x04   | 4    | uint32  | `size`       | Size of the SCNF data                    |
| 0x08   | 4    | uint32  | `blockCount` | Number of blocks: 1 in the character files, 2 in `mapinfo.bin`, 6 in `cold.bin` |
| 0x0C   | 4    | uint32  | `unknown`    | Unknown, always `0x900`                  |

## Block header (0x30 bytes)

All the offsets are relative to the start of the block. The next block starts at `size`.

| Offset | Size | Type    | Name               | Description                                                       |
|-------:|-----:|---------|--------------------|-------------------------------------------------------------------|
| 0x00   | 4    | char[4] | `id`               | Block ID: in the character files, the ID of the character (e.g. `AKMI`), which is also the second character of the block (except `DORG`, which only has `AKIR`); `SY07`, `_s01`... in the other files |
| 0x04   | 4    | uint32  | `size`             | Size of the block, a multiple of 4                                |
| 0x08   | 2    | uint16  | `characterCount`   | Number of characters (1–4)                                        |
| 0x0A   | 2    | uint16  | `positionCount`    | Number of positions (0–6)                                         |
| 0x0C   | 4    | uint32  | `unknown0`         | Unknown, always 0                                                 |
| 0x10   | 4    | uint32  | `voicePathOffset`  | Folder of the voice clips, e.g. `/p38/prj38sc/Msg/voice/F1015`    |
| 0x14   | 4    | uint32  | `scriptOffset`     | Script, right after the voice path                                |
| 0x18   | 4    | uint32  | `linesOffset`      | Table of lines                                                    |
| 0x1C   | 4    | uint32  | `unknown1`         | Unknown, always 0                                                 |
| 0x20   | 4    | uint32  | `charactersOffset` | Characters, `0x40 + 12 × positionCount`                           |
| 0x24   | 4    | uint32  | `unknown2`         | Unknown, always 0                                                 |
| 0x28   | 4    | uint32  | `positionsOffset`  | Positions: `0x40`, or 0 if there are none                         |
| 0x2C   | 4    | uint32  | `unknown3`         | Unknown, always 0                                                 |

Over the 262 character files: 247 have 2 characters, 10 have 3, 4 have 4 and 1 has 1; 34 have positions.

### Characters (4 bytes each)

| Offset | Size | Type    | Name | Description                                                       |
|-------:|-----:|---------|------|-------------------------------------------------------------------|
| 0x00   | 4    | char[4] | `id` | Character ID. The first one is always `AKIR` (Ryo), the second one is the character of the block |

The index of a character is the letter of the voices it speaks: `A` for the first one, `B` for the second one...

### Position (12 bytes)

| Offset | Size | Type     | Name       | Description                                      |
|-------:|-----:|----------|------------|--------------------------------------------------|
| 0x00   | 12   | float[3] | `position` | Unknown use; they look like positions in the map (e.g. `(6, 1.5, 86)`) |

### Script

Bytecode between `scriptOffset` and `linesOffset`; its format is unknown.

### Line (16 bytes)

The table has no count: it ends where the strings start, at the lowest `voiceOffset`.

| Offset | Size | Type    | Name          | Description                                                        |
|-------:|-----:|---------|---------------|--------------------------------------------------------------------|
| 0x00   | 4    | uint32  | `textOffset`  | Subtitle, right after the voice name                               |
| 0x04   | 4    | uint32  | `voiceOffset` | Voice name, null terminated (e.g. `F1015B001`)                     |
| 0x08   | 4    | char[4] | `voiceID`     | Last 4 characters of the voice name (e.g. `B001`). The letter is the speaker (index in the characters) |
| 0x0C   | 4    | float   | `duration`    | Duration of the voice in seconds, 0 if the line has no voice      |

## Voices

The voice name is the last folder of the voice path followed by the voice ID (`F1015` + `B001`), except in 319 lines of 10 files whose voices use another prefix (e.g. `TMOS.bin`: path `X1172`, voices `X1289…`). The clips are stored with these names in the audio [AFS](AFS) files (e.g. `F1015B001` in `scene/01/stream/pc/eng/free01.afs`). The `duration` is the length of the clip (the xWMA clips of the PC version are slightly longer, as they are padded to whole packets).

Prefixes: `F` (22,054 lines), `XM`, `XD` and `X` (5,995 lines), `SY` and `SYS` (Ryo's lines of `cold.bin` and `mapinfo.bin`). 1,150 of the 28,049 lines of the character files have no voice (`duration` 0).

## Subtitles

The subtitles are null terminated strings in **EUC-JP**: the name of the speaker in Japanese followed by the text in brackets, `涼「Have any men dressed in black suits＆come into the bar recently?」`.

| Bytes          | Character | Meaning      |
|----------------|-----------|--------------|
| `A1 D6`, `A1 D7` | `「` `」` | Start and end of the text |
| `A1 F5`        | `＆`      | Line break   |
| `3D 40`        | `=@`      | Ellipsis     |

The texts are in English, except in 22 files with Japanese lines (e.g. `KZKI.bin`, `YAMO.bin`) and in `mapinfo.bin`. 7 subtitles have a space after `」`.
