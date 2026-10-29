# SCNF — Dialogue lines

A SCNF file holds the dialogue lines of one or more characters: for each line, the name of its voice clip, its duration and its subtitle. It also holds a script (bytecode) that chooses the lines to say.

Every character of `scene/01/stream/humans` has one, inside the [IPAC](IPAC) block of its [PKS](PKS) (e.g. `AKMI.pks` → `AKMI.BIN`, next to the model `HOS_L.CHRM`). Its IPAC extension is `BIN`. SCNF data is also found inside other files: `misc/cold.bin` (a [PKS](PKS) whose SCNF entry has 6 blocks with the lines Ryo says when it is late) and the `SCNF` chunk of `scene/0x/jhd0/mapinfo.bin` ([MAPINFO](MAPINFO)).

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
| Script                 |  bytecode
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

Bytecode between `scriptOffset` and `linesOffset` that chooses the lines to say: it checks flags, the date and the time, chooses lines at random and plays them. See [Script bytecode](#script-bytecode).

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

## Script bytecode

The script of a block (60 to 8,164 bytes in the character files) is a list of instructions run from the start. Most of its meaning comes from the lines it plays; the meaning of the procedures, of some variables and of some instructions is unknown.

### Encoding

The highest 4 bits of the first byte are the opcode; the rest of the instruction is the argument, **big-endian** (unlike the rest of the file):

| First byte    | Size    | Argument                                   |
|---------------|--------:|--------------------------------------------|
| `8X`, `FX`    | 1 byte  | The lowest 4 bits                          |
| `F2`, `F5`    | 4 bytes | The next 3 bytes (24 bits)                 |
| `DX`          | 3 bytes | The lowest 4 bits and the next 2 bytes (20 bits) |
| The rest      | 2 bytes | The lowest 4 bits and the next byte (12 bits) |

The jumps (`1X`, `4X`, `5X`, `9X`, `DX`, `F2`, `F5`) are relative to the end of the instruction and only go forward. Over the 262 character files, 60,809 of the 60,836 jumps land on an instruction; the rest are in 4 files with data that isn't code (e.g. a table of `5X` jumps in `FUKU.bin`). The scripts end with 1 to 4 `FF` bytes, so that their size is a multiple of 4.

### Instructions

| Opcode    | Name     | Count  | Description                                                                 |
|-----------|----------|-------:|-----------------------------------------------------------------------------|
| `F0`      | If       | 13,763 | Start of a condition: an [expression](#expressions) that ends with `80`, followed by a `4X` jump |
| `4X n`    | If not   | 13,762 | Jumps `n` bytes if the condition is false                                   |
| `5X n`    | Jump     | 46,918 | Jumps `n` bytes                                                            |
| `DX n`    | Call     | 7,957  | Calls the subroutine `n` bytes ahead; the target is always a `1X` instruction |
| `1X n`    | Block    | 4,372  | Start of a subroutine that ends `n` bytes ahead, at a `50 00` jump in 4,342 of them. The subroutines are inside an `if (0)`, so they only run when they are called |
| `2X n`    | Line     | 61,621 | Plays the line `n` of the [table of lines](#line-16-bytes)                  |
| `EX n`    | Random   | 11,785 | Plays one of the next `n` options, chosen at random                         |
| `0X n`    | Option   | 33,058 | Start of an option of `EX`: `n` is its size in bytes. The option ends with a jump to the end of the `EX` |
| `FD`      | —        | 1,068  | Empty option (the only instruction of the option before its jump)           |
| `6X n`    | Set flag | 655    | Sets the flag `n` (e.g. after a character explains something to Ryo)        |
| `3X n`    | Procedure| 20,020 | Unknown action of the game. `30 28` (8,503), `30 10` (6,789) and `30 32` (3,682) surround almost every conversation |
| `CX n`    | —        | 2,497  | Unknown action (e.g. `C2 41`, `C2 42`, `C7 0B`), usually between lines      |
| `F1`      | —        | 4,493  | Start of a part of a conversation                                            |
| `9X n`    | —        | 2,668  | After `F1`: jumps to the end of the part (an `F6` in 2,615 of them); probably skips the part when it was already said |
| `F2 n`    | —        | 8,164  | Usually `F2 00 00 00` (jump 0) at the start of a part. With another value, it points to a subroutine (`1X`), probably the conversation to use the next time |
| `F5 n`    | —        | 1,709  | Points to a subroutine (`1X`), always followed by `F1`                      |
| `F3`      | —        | 66     | Start of a greeting, before `30 28`                                         |
| `F6`, `F8`, `F9` | — | 2,676, 1,219, 474 | End of a part or a conversation, usually after `30 32` (`F6`, `F8`) or after a line (`F9`) |
| `FF`      | End      | 656    | End of the script, repeated up to a multiple of 4 bytes                     |

`AX` and `F4` appear once as instructions.

### Expressions

The expressions are in postfix notation (operands first, then the operator) between `F0` and `80`:

| Opcode | Operand            | Description                                                              |
|--------|--------------------|--------------------------------------------------------------------------|
| `0X n` | Flag `n`           | True if the flag `n` is set; most scripts start by checking the progress of the story with them |
| `6X n` | Constant `n`       | e.g. `60 01` = 1                                                         |
| `5X n` | Constant `n − 0x1000` | Only `5F FF` = −1                                                     |
| `CX n` | Variable `n`       | `C0 00` is compared with 0–5 at the start of most scripts (unknown meaning) |
| `AX n` | Date and time      | `A0 00` year (compared with 86 and 87), `A0 01` month, `A0 02` day, `A0 04` hour, `A0 05` minute |
| `EX n`, `4X n` | Unknown    | Compared with 0–2 (`EX` also alone, as a condition)                     |

| Opcode | Operator      | Description                                                             |
|--------|---------------|-------------------------------------------------------------------------|
| `84`   | `!`           | Not, after a flag                                                       |
| `85`   | `&&`          | And                                                                     |
| `86`   | `\|\|`        | Or                                                                      |
| `87`   | `==`          | Equal                                                                   |
| `88`   | `!=`          | Not equal                                                               |
| `89`   | `>` (probably)| Used with the hour (`hour 19 89`)                                       |
| `8A`   | `<`           | e.g. `hour >= 10 && hour < 19` (daytime), `hour >= 19 \|\| hour < 7` (night) |
| `8C`   | `>=`          |                                                                         |
| `82`   | Unknown       | Unary, once                                                             |

### Example

The script of `BOB_.bin`:

```
0000  F0 01 90 80 40 05     if (flag[0x190]) {
0006  D0 00 1E                  call 0027
0009  50 00                 }
000B  ...                   (the same with flag[0x281] and with 1)
0021  F0 60 00 80 40 51     if (0) {
0027  10 4D                     block (ends at 0076)
0029  F2 00 00 00
002D  30 10                     procedure 0x010
002F  30 28                     procedure 0x028
0031  E0 02                     random 2:
0033  00 04 20 00 50 06             line 0  (涼「Um...」)
0039  00 04 20 01 50 00             line 1  (涼「Excuse me.」)
003F  E0 03                     random 3: lines 2, 3, 4
0053  E0 05                     random 5: lines 5 to 9
0073  30 32                     procedure 0x032
0075  F8
0076  50 00                 }
0078  FF FF FF FF
```
