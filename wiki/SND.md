# SND (DTPK sound bank)

A SND file is a sound bank of the Dreamcast sound driver: the instruments, the sequences that play them, the settings of the DSP effects of the AICA sound chip and the sample data, in one file. It is used for the music (`bgm*.snd`), the ambient sounds (`amb*.snd`) and the sound effects of every area.

There are 794 SND files in the PC version, almost all in the `scene/01/sound`, `scene/02/sound` and `scene/03/sound` folders of the [TAD/TAC](TAD-TAC) archives (plus `misc/system1.snd`, `misc/bgm110.snd`, `misc/spp004.snd` and 3 others). They are 12,956 bytes to 5.4 MB.

- **Extension:** `.snd`
- **Magic:** `DTPK`
- **Compression:** none

## Layout

```
+-----------------------------+ 0x00
| Header                      |  0x20 bytes
+-----------------------------+ 0x20
| Section table               |  8 × uint32
+-----------------------------+ 0x40
| Zero padding                |  0x20 bytes
+-----------------------------+ 0x60
| 0: Part table               |
+-----------------------------+
| 1: Program bank             |
+-----------------------------+
| 2: Curves                   |
+-----------------------------+
| 3: Sequence bank            |  (missing in 3 files)
+-----------------------------+
| 4: Effect program           |  (optional)
+-----------------------------+
| 5: Unknown table            |  (optional)
+-----------------------------+
| 6: DSP settings             |  (optional)
+-----------------------------+
| 7: Sample table             |
+-----------------------------+
| Sample data                 |
+-----------------------------+ size
```

The sections are always stored in this order, without gaps. Inside the sections all the offsets are relative to the start of the section (or of a block of it), except the offsets of the sample data, which are relative to the start of the file.

## Header (0x20 bytes)

| Offset | Size | Type    | Name        | Description                                                        |
|-------:|-----:|---------|-------------|--------------------------------------------------------------------|
| 0x00   | 4    | char[4] | `magic`     | `DTPK`                                                             |
| 0x04   | 4    | uint32  | `unknown1`  | Unknown: 1 (456 files), 0 (315), 2 (14), 4 (5), 57 (3), 3 (1)      |
| 0x08   | 4    | uint32  | `size`      | Size of the file                                                   |
| 0x0C   | 4    | uint32  | `unknown2`  | Always 0                                                           |
| 0x10   | 1    | uint8   | `channels`  | Number of entries of the [part table](#section-0-part-table) plus one, in 784 files (9 in 490 files) |
| 0x11   | 1    | uint8   | `unknown3`  | Unknown: `FF` (698 files), 0 (70), 1 (25), 4 (1)                   |
| 0x12   | 1    | uint8   | `unknown4`  | Unknown: 0 (754 files), `FF` (34), 1 (6)                           |
| 0x13   | 1    | uint8   | `dspFlag`   | 2 when the file has the [DSP settings](#section-6-dsp-settings), 0 otherwise |
| 0x14   | 3    | uint8[3] | `unknown5` | Unknown (e.g. `06 52 01`, `17 4D 01`); 0 in 41 files               |
| 0x17   | 3    | uint8[3] | `unknown6` | Always 0                                                           |
| 0x1A   | 2    | uint16  | `tableOffset` | Always 0x20: offset of the section table                         |
| 0x1C   | 3    | uint8[3] | `unknown7` | Always 0                                                           |
| 0x1F   | 1    | uint8   | `effectFlag` | 1 in 134 of the 361 files with an [effect program](#section-4-effect-program), 0 in the rest |

## Section table (0x20 bytes)

8 `uint32` with the offset, from the start of the file, of each section. 0 means that the file doesn't have the section. The bytes 0x40 to 0x5F are always 0, so the first section always starts at 0x60.

| Index | Section                                        | Files |
|------:|------------------------------------------------|------:|
| 0     | [Part table](#section-0-part-table)            | 794   |
| 1     | [Program bank](#section-1-program-bank)        | 794   |
| 2     | [Curves](#section-2-curves)                    | 794   |
| 3     | [Sequence bank](#section-3-sequence-bank)      | 791   |
| 4     | [Effect program](#section-4-effect-program)    | 361   |
| 5     | [Unknown table](#section-5-unknown-table)      | 82    |
| 6     | [DSP settings](#section-6-dsp-settings)        | 754   |
| 7     | [Sample table](#section-7-sample-table)        | 794   |

Several sections start with a list of offsets with the same shape:

| Offset | Size | Type        | Name      | Description                                       |
|-------:|-----:|-------------|-----------|---------------------------------------------------|
| 0x00   | 2    | uint16      | `last`    | Number of entries minus one                       |
| 0x02   | 2×N  | uint16[N]   | `offsets` | Offset of each entry, from the start of the list  |

## Section 0: Part table

A list of offsets (see above) with 2 entries in 779 files (3, 5 and 1 in the rest). The first entry is a block of 0x28 bytes, and the next ones are lists of parts.

### Settings block (0x28 bytes)

Unknown content, usually (593 files) `00 7F 00 00 FF 00 00 00 83 0F 00 00 00 7F 00 7F 00 0C 7F 00` followed by zeros.

### Part list

| Offset | Size  | Type      | Name      | Description                                  |
|-------:|------:|-----------|-----------|----------------------------------------------|
| 0x00   | 1     | uint8     | `last`    | Number of parts minus one                    |
| 0x01   | 7     | uint8[7]  | `unknown` | Unknown, usually `7F 00 00 00 00 00 00`      |
| 0x08   | 0x20×N | Part[N]  | `parts`   | Parts                                        |

#### Part (0x20 bytes)

| Offset | Size | Type      | Name       | Description                                                      |
|-------:|-----:|-----------|------------|------------------------------------------------------------------|
| 0x00   | 1    | uint8     | `flags`    | Always `80`                                                      |
| 0x01   | 1    | uint8     | `channel`  | MIDI channel of the [music tracks](#music-track) played by the part (0 to 15). Usually the index of the part in the list (6,849 of 7,145 parts), but several parts can have the same channel: a note on the channel plays all of them |
| 0x02   | 1    | uint8     | `bank`     | Always 0                                                         |
| 0x03   | 1    | uint8     | `program`  | Index of a program of the [program bank](#section-1-program-bank) (always valid) |
| 0x04   | 4    | uint8[4]  | `unknown1` | Unknown, always `00 7F 00 7F`                                    |
| 0x08   | 1    | uint8     | `send`     | Effect send of the AICA: level (`IMXL`, bits 4-7, 0 to 15) and input of the DSP (`ISEL`, bits 0-3), for the splits that don't set it. 0 (3,894 parts), `90`, `80`, `A0`... |
| 0x09   | 1    | uint8     | `level`    | Direct level of the AICA (`DISDL`, 0 to 15), for the splits that don't set it. 12 (4,506 parts), 15 (2,280)... |
| 0x0A   | 1    | uint8     | `volume`   | Initial volume of the channel, like the MIDI controller 7: 127 (4,846 parts), 0 (601), 100, 120... |
| 0x0B   | 1    | uint8     | `pan`      | Pan of the AICA (`DIPAN`, see [levels and pan](#levels-and-pan)), for the splits that don't set it. 0 (5,376 parts), 15, 31, 5... |
| 0x0C   | 1    | int8      | `transpose` | Semitones added to the notes (7-bit signed number): 0, −12, 12, −24, 24... |
| 0x0D   | 1    | int8      | `fine`     | Fine tune added to the notes, in 1/64 semitone (7-bit signed number): 0 (6,967 parts), 36, −12 (`F4`), 4, −5 (`FB`)... |
| 0x0E   | 0x12 | uint8[18] | `unknown3` | Unknown, usually 0                                               |

## Section 1: Program bank

A list of offsets with one bank (in all the files), and the bank is a list of offsets to the programs (the instruments). The offsets of the programs are relative to the start of the bank.

### Program

| Offset | Size | Type       | Name           | Description                                                    |
|-------:|-----:|------------|----------------|----------------------------------------------------------------|
| 0x00   | 4    | uint32     | `flags`        | The first byte gives the kind of program: 1, the layers have a [key map](#key-map) (drum kits); 2, the [effect program](#section-4-effect-program), whose splits are taken by index; other values, a normal program. Values: 0, 1, 2, `80000000`, `00400000` |
| 0x04   | 4    | uint32     | `unknown`      | Unknown, usually 0 (also 10, 12, 20)                           |
| 0x08   | 8    | uint16[4]  | `layerOffsets` | Offsets of up to 4 layers, from the start of the program; 0 means no layer. The first one is always 0x10 |
| 0x10   | ...  | Layer[]    | `layers`       | Layers                                                         |

### Layer

| Offset | Size     | Type      | Name       | Description                                                     |
|-------:|---------:|-----------|------------|-----------------------------------------------------------------|
| 0x00   | 1        | uint8     | `last`     | Number of splits minus one                                      |
| 0x01   | 1        | uint8     | `unknown1` | Unknown, always 0                                               |
| 0x02   | 1        | uint8     | `lowVelocity` | In a normal program, the layer plays the notes whose velocity is from `lowVelocity` to `highVelocity` (always 0). Not used in the key maps and the effect program, whose splits have their own [velocity range](#velocity-range) |
| 0x03   | 1        | uint8     | `highVelocity` | `7F` (0 in 6 layers, which are never played)                 |
| 0x04   | 2        | uint8[2]  | `unknown2` | Unknown, always 0                                               |
| 0x06   | 1        | uint8     | `curve`    | Index of the velocity curve of the layer in the [curves](#section-2-curves) (the first curve when it is higher than the last index). Always 0 |
| 0x07   | 0x19     | uint8[25] | `unknown3` | Unknown. Usually 0, with `02 7F` at 0x08 (815 layers) and `0F 7F` at 0x10 |
| 0x20   | 1        | uint8     | `unknown4` | Unknown, 1 or 0                                                 |
| 0x21   | 1        | uint8     | `bendUp`   | Range of the pitch bend up, in semitones (see [pitch](#pitch))  |
| 0x22   | 1        | uint8     | `bendDown` | Range of the pitch bend down, in semitones. With `bendUp`: 2 and 2 (3,529 layers), 0 and 0 (361, the effect programs), 12 and 12 (31), 8 and 8 (3), 2 and 12 (2) |
| 0x23   | 5        | uint8[5]  | `unknown5` | Unknown, usually 0                                              |
| 0x28   | 1        | uint8     | `panPattern` | Bits 0-4: 1 to 8, the successive notes of the layer take the pans of a pattern of the sound driver (see [levels and pan](#levels-and-pan)); 0, no pattern. Always 0 |
| 0x29   | 0x17     | uint8[23] | `unknown6` | Unknown, usually 0. The sound driver keeps the position in the pan pattern at 0x2A |
| 0x40   | K        | KeyMap    | `keyMap`   | Only in the key map programs                                    |
| 0x40+K | 0x40×N   | Split[N]  | `splits`   | Splits, ordered by key                                          |

#### Key map

| Offset | Size | Type      | Name      | Description                                                         |
|-------:|-----:|-----------|-----------|---------------------------------------------------------------------|
| 0x00   | 1    | uint8     | `lowKey`  | First key                                                           |
| 0x01   | 1    | uint8     | `highKey` | Last key                                                            |
| 0x02   | 2×M  | uint16[M] | `splits`  | For each key from `lowKey` to `highKey`, offset of its split from the start of the layer; 0 means no split |
| ...    | ...  | —         | padding   | Zeros up to a multiple of 4 bytes                                   |

#### Split (0x40 bytes)

A split plays one sample over a range of keys and a [range of velocities](#velocity-range). In a layer without key map, a key plays the first split whose `highKey` is above or equal to it. In 60 programs the high keys are not in increasing order (e.g. `2A 2F 36 47 42 7F` in `bgm023.snd`, `7B 48` in `tsm004.snd`), while the pitches of the splits are: some splits are never played. Most of the fields of a split are registers of a voice of the AICA. A value of `80` in the pan, the level and the effect send means that the split doesn't set it: the value of the [part](#part-0x20-bytes) is used.

| Offset | Size | Type      | Name         | Description                                                       |
|-------:|-----:|-----------|--------------|-------------------------------------------------------------------|
| 0x00   | 1    | uint8     | `highKey`    | Last key of the split (`7F` when there is only one split). In the [effect program](#section-4-effect-program), the index of the effect |
| 0x01   | 1    | uint8     | `flags`      | Unknown. Bit 7 is set in all the splits of the effect program and in most splits of the key maps (3,773 of 4,244), never in the other splits |
| 0x02   | 1    | uint8     | `sample`     | Index of the sample in the [sample table](#section-7-sample-table). In 23 splits it is higher than the index of the last sample of the file |
| 0x03   | 1    | uint8     | `unknown1`   | Unknown: 1, or 0                                                  |
| 0x04   | 2    | uint16    | `unknown2`   | Unknown, almost always 0                                          |
| 0x06   | 1    | uint8     | `pan`        | Pan of the AICA (`DIPAN`, see [levels and pan](#levels-and-pan)), or `80` |
| 0x07   | 1    | uint8     | `level`      | Direct level of the AICA (`DISDL`, 0 to 15), or `80`              |
| 0x08   | 1    | uint8     | `velocityLevel` | Bits 1-7: scale of the level given by the velocity (see [levels and pan](#levels-and-pan)), 127 for no change: `FF` (18,450 splits), `F1` (371), `C9` (95), `DD` (79), 0 (57, silent)... |
| 0x09   | 1    | uint8     | `unknown3`   | Unknown, usually the same value as `velocityLevel`                |
| 0x0A   | 1    | int8      | `transpose`  | Pitch, in semitones (see [pitch](#pitch))                         |
| 0x0B   | 1    | int8      | `fine`       | Pitch, in 1/64 semitone (see [pitch](#pitch))                     |
| 0x0C   | 2    | uint16    | `envelope1`  | Envelope rates of the AICA: attack (`AR`, bits 0-4), decay 1 (`D1R`, bits 6-10) and decay 2 (`D2R`, bits 11-15). Usually `001F`: instant attack, no decay |
| 0x0E   | 2    | uint16    | `envelope2`  | Release rate (`RR`, bits 0-4), decay level (`DL`, bits 5-9) and key rate scaling (`KRS`, bits 10-13) of the AICA. Usually `001F` |
| 0x10   | 0x0A | uint16[5] | `filter`     | Low-pass filter of the AICA, see [filter](#filter). Usually `0000 07FD 07FD 07FD 27FD`: no filter |
| 0x1A   | 2    | uint8[2]  | `unknown4`   | Unknown, usually 0                                                |
| 0x1C   | 1    | uint8     | `detune`     | Random pitch of each note, up to `detune` × 6/256 semitone up or down: 0 (19,199 splits), 4 (169)... |
| 0x1D   | 1    | uint8     | `unknown5`   | Unknown, usually 0                                                |
| 0x1E   | 1    | uint8     | `sendFlags`  | Bit 7 set: the split doesn't set the effect send, the send of the [part](#part-0x20-bytes) is used. 0 (12,814 splits) or `80` (6,562, the splits of the normal programs) |
| 0x1F   | 1    | uint8     | `send`       | Effect send of the AICA (`IMXL`, bits 4-7, and `ISEL`, bits 0-3), when bit 7 of `sendFlags` is clear: 0 (2,501 splits), `40` (2,127), `30` (1,741), `50` (1,220)... |
| 0x20   | 1    | uint8     | `lowVelocity` | First velocity of the split, in the key maps and the effect program (see [velocity range](#velocity-range)) |
| 0x21   | 1    | uint8     | `highVelocity` | Last velocity of the split. `00 7F` in the key maps and the effect program (13,254 splits), `00 00` in the normal programs, where it is not used |
| 0x22   | 0x0E | uint8[14] | `unknown6`   | Unknown, usually 0                                                |
| 0x30   | 2    | uint16    | `lfo`        | LFO register of the AICA: reset (`LFORE`, bit 15), frequency (`LFOF`, bits 10-14), waveform and depth of the pitch (`PLFOWS`, bits 8-9, `PLFOS`, bits 5-7) and of the amplitude (`ALFOWS`, bits 3-4, `ALFOS`, bits 0-2). `8000` (13,254 splits), `D210` (4,951), `D600` (791), `6290` (168, a vibrato)...: the LFO is usually held in reset, with a frequency in the normal programs |
| 0x32   | 0x0E | uint8[14] | `unknown7`   | Unknown, usually 0                                                |

#### Velocity range

A note plays a split only when its velocity is in the range of the split: the range of the layer (0x02 and 0x03 of the [layer](#layer)) in the normal programs, the range of the split itself (0x20 and 0x21) in the key maps and the effect program. For the [effects](#effect-sequence), the velocity is the volume of the effect.

#### Filter

5 words, each with a level of the filter (bits 0-10, the 11 high bits of the 13-bit `FLV` registers of the AICA) and a rate (bits 11-15). When the first level is 0, the filter is off (`LPOFF`): 17,142 splits. The other 2,424 splits use the filter.

| Word | Level (bits 0-10)                | Rate (bits 11-15)                                                    |
|-----:|----------------------------------|----------------------------------------------------------------------|
| 0    | `FLV0`: cutoff at the key on     | `FAR`: rate of the attack of the filter envelope, towards `FLV1`     |
| 1    | `FLV1`: cutoff after the attack  | `FD1R`: rate of the decay 1, towards `FLV2`                          |
| 2    | `FLV2`: cutoff after the decay 1 | `FD2R`: rate of the decay 2, towards `FLV3`                          |
| 3    | `FLV3`: cutoff after the decay 2 | `FRR`: rate of the release, towards `FLV4`                           |
| 4    | `FLV4`: cutoff after the release | `Q`: resonance (0 to 31, −3 dB to 20.25 dB)                          |

`07FD` is the highest level (the filter open). Most of the filtered splits only set `FLV0`, with the rates at 0 and the other levels at `07FD`, and a resonance of 4 (`27FD`): e.g. `075E 07FD 07FD 07FD 27FD` (192 splits), `07B8 ...` (95), `06F2 ...` (93).

#### Pitch

The AICA plays a sample at 44100 Hz × 2^(`semitones` / 12), where `semitones` = note − 60 + `transpose` of the part + `transpose` + (`fine` of the part + `fine`) / 64, plus the random `detune` and the pitch bend. In the key maps (one key per split) and in the effect program, the pitch doesn't follow the key: the sample plays as at note 60, but the transpose and the fine tune of the part still apply. The pitch bend (0 to 127, 64 is the center) moves the pitch by (bend − 63) / 64 × `bendUp` of the [layer](#layer) semitones above the center, and by (bend − 64) / 64 × `bendDown` semitones below. So 44100 × 2^((`transpose` + `fine` / 64) / 12) is the sample rate of the recording: e.g. `transpose` −18 and `fine` 29 give 16000 Hz, −24 and 0 give 11025 Hz, −30 and 29 give 8000 Hz. The samples at [half sample rate](#sample-0x10-bytes) play twice as fast, as their data has been doubled.

#### Levels and pan

The levels of the AICA (`DISDL`, `IMXL`, `EFSDL`) go from 0 (off) to 15 (0 dB), 3 dB per step. The pan (`DIPAN`, `EFPAN`) attenuates one side by 3 dB per step of bits 0-3 (15: off): the left side when bit 4 is clear, the right side when it is set. 0 and `10` are the center, `0F` is right only and `1F` left only.

The velocity, the volume of the channel and its expression set the total level of the AICA (`TL`), which attenuates by 0.375 dB per step. The sound driver of the game (`sound/aicadrv.bin`) takes the value of the velocity in the [curve](#section-2-curves) of the layer (0 to 254) and scales it successively by `velocityLevel` of the split, the volume and the expression (0 to 127 each: `level` = `level` × `scale` × 2 / 256, plus 1 when the result is not 0). Then `TL` = (255 − `level`) / 2, or 255 (silence) when the level is 0. So the full velocity and volume give 0 dB, a velocity of 64 about −10 dB, and a volume of 64 about −24 dB.

When the split sets its pan and its layer has a `panPattern`, each note takes the next pan of a pattern of the sound driver, starting again after the last one (16 values, or before `FF`):

| Pattern | Pans                                              |
|--------:|---------------------------------------------------|
| 1       | `11 01`                                           |
| 2       | `14 04`                                           |
| 3       | `1F 0F`                                           |
| 4       | `18 08 14 04`                                     |
| 5       | `02 04 08 0F 08 04 02 00 12 14 18 1F 18 14 12 00` |
| 6       | `02 04 08 0F 1F 18 14 12`                         |
| 7       | `12 14 18 1F 0F 08 04 02`                         |
| 8       | `08 1F 02 18 08 14 02 00 14 08 02 1F 04 12 08 18` |

## Section 2: Curves

| Offset | Size     | Type          | Name     | Description                                        |
|-------:|---------:|---------------|----------|----------------------------------------------------|
| 0x00   | 4        | uint32        | `last`   | Number of curves minus one: 0 (712 files) or 1 (82) |
| 0x04   | 0x80×N   | uint8[N][128] | `curves` | Curves of 128 values                               |

The curves turn the velocity of the notes (0 to 127) into a level (0 to 254, see [levels and pan](#levels-and-pan)); each [layer](#layer) chooses its curve. The first curve is the same in every file, and the same as the default curve of the sound driver: it rises from 0 (`00 07 0E 15 1C 24 ...`) to `FE`, quickly at first and slowly at the end (64 gives `C9`).

## Section 3: Sequence bank

The sequences (the songs, and the sound effects requested by the game) are grouped. All the offsets are relative to the start of the section. Bit 7 of the first byte of a sequence gives its kind: [music](#music-sequence) (`40`) or [effect](#effect-sequence) (`C0`, `D1`).

| Offset | Size | Type         | Name     | Description                     |
|-------:|-----:|--------------|----------|---------------------------------|
| 0x00   | 4    | uint32       | `last`   | Number of groups minus one      |
| 0x04   | 4×N  | GroupRef[N]  | `groups` | Groups                          |

### Group reference (4 bytes)

| Offset | Size | Type   | Name       | Description                                              |
|-------:|-----:|--------|------------|----------------------------------------------------------|
| 0x00   | 2    | uint16 | `offset`   | Offset of the group                                      |
| 0x02   | 1    | uint8  | `unknown1` | Unknown, 0 to 127 (`3F` and 4 are the most common)       |
| 0x03   | 1    | uint8  | `unknown2` | Unknown: `A8`, `A9`, `AB` or `AA`                        |

### Group

| Offset | Size | Type       | Name        | Description                       |
|-------:|-----:|------------|-------------|-----------------------------------|
| 0x00   | 4    | uint32     | `last`      | Number of sequences minus one     |
| 0x04   | 4×N  | uint32[N]  | `sequences` | Offset of each sequence           |

There are 1 to 108 sequences per group; 415 of the 990 groups have only one.

### Events

Both kinds of sequences are streams of events close to MIDI: a status byte (`8x` to `Ex`, `x` being the channel) followed by its data bytes, with running status (the status byte is left out when it is the same as in the previous event). The differences with MIDI:

- **Bit 7 of the last data byte of an event is set when the next event happens at the same time.** Otherwise, the event is followed by a delta time: the time to wait before the next event, in **milliseconds**, as a MIDI variable-length number (7 bits per byte, bit 7 set in all the bytes but the last). The tempo is already applied to the delta times: there is no tempo event.
- The note off (`8x`) has only one data byte, the note.
- The pitch bend (`Ex`) has only one data byte: 0 to 127, 64 is the center.

### Music sequence

520 sequences (`bgm*.snd`, `amb*.snd`...): a playlist of 32-bit words, then the tracks.

| Offset | Size | Type      | Name       | Description                                                        |
|-------:|-----:|-----------|------------|--------------------------------------------------------------------|
| 0x00   | 4    | uint32    | `header`   | `00010040` (509 sequences) or `00000040` (11)                     |
| 0x04   | 4×N  | uint32[N] | `playlist` | Words played in order, see below                                  |

| Word       | Meaning                                                                                  |
|------------|------------------------------------------------------------------------------------------|
| `00oooooo` | Play the [track](#music-track) at offset `o`, from the start of the sequence             |
| `80A0ccvv` | Command `cc` with value `vv`, unknown meaning: `00` (always with value `12`, the first word), `19` (value 0 to 3, the second word), `24` (value 1, in 86 sequences) |
| `90oooooo` | Go on at the word at offset `o`: the loop of the song (e.g. `9000000C` plays the first track again and again, `90000010` plays the first track once and loops from the second one) |
| `91oooooo` | Play the last track, at offset `o`, then stop                                            |

The tracks of a playlist are played one after the other (e.g. the intro and the loop of a song). They follow the playlist, each one padded to a multiple of 4 bytes. The sequences of a group can share tracks: the offsets of a playlist can point to the tracks of another sequence.

### Music track

All the channels in a single stream of [events](#events), ending with the MIDI end of track (`FF 2F 00`). The notes play the programs of the [parts](#part-0x20-bytes) of their channel. In the 645 tracks:

| Status | Data              | Event                                                           | Count     |
|--------|-------------------|-----------------------------------------------------------------|----------:|
| `8x`   | note              | Note off                                                        | 199,386   |
| `9x`   | note, velocity    | Note on (velocity 0: note off)                                  | 1,038,357 |
| `Ax`   | 2 bytes           | Unknown (`24 02` in 111 events)                                 | 112       |
| `Bx`   | controller, value | MIDI controller: modulation (1, 44,496 events), volume (7), pan (10), expression (11), sustain (64), bank (0, 32), reverb and chorus send (91, 93), reset (121)... | 58,213 |
| `Cx`   | program           | Program change (`bgm201.snd`, `bgm202.snd`...)                  | 69        |
| `Ex`   | bend              | Pitch bend                                                      | 33,752    |
| `FF`   | `2F 00`           | End of track                                                    | 645       |

### Effect sequence

15,149 sequences: a header byte (`C0`, or `D1` in 10 sequences), then [events](#events), ending with a single `FF`.

| Status | Data                    | Event                                                                 |
|--------|-------------------------|-----------------------------------------------------------------------|
| `Dx`   | effect, volume, unknown | Plays the split `effect` of the [effect program](#section-4-effect-program) with a volume from 0 to 127 (0 stops the effect). The third byte is 0 or 1 |
| `Ax`   | controller, value       | Controller: expression (`0B`), unknown (`04`, `05`, `00`)             |

- 9,105 sequences play a single effect: `C0 DF ii vv 80 FF`. Others play several effects with delays, e.g. `C0 DF 03 3C 00 82 36 DF 04 3C 00 82 40 DF 05 3C 80 FF` in `3_tel_3.snd`: effect 3, 310 ms later effect 4, 320 ms later effect 5.
- 5,848 sequences are empty: `C0 A0 00 80 FF`.

## Section 4: Effect program

A single [program](#program) (flags 2, one layer without key map) with one split per sound effect, played by the [effect sequences](#effect-sequence). The first byte of each split is its index instead of a key, and the effects play at the rate of their recording, whatever the [pitch](#pitch) of the note.

## Section 5: Unknown table

| Offset | Size | Type      | Name      | Description                                    |
|-------:|-----:|-----------|-----------|------------------------------------------------|
| 0x00   | 4    | uint32    | `last`    | Number of entries minus one (0 to 7)           |
| 0x04   | 4×N  | uint32[N] | `offsets` | Offset of each entry, from the start of the section |

The entries have an unknown content: pairs of bytes from 0 to `7F` (e.g. `00 7F 0F 15 0F 01`), maybe curves of an LFO or of the volume.

## Section 6: DSP settings

The settings of the DSP of the AICA, always 0xC28 bytes. The values are stored as in the registers of the chip.

| Offset | Size  | Type          | Name      | Description                                                      |
|-------:|------:|---------------|-----------|------------------------------------------------------------------|
| 0x000  | 4     | uint32        | `unknown` | Always 0                                                         |
| 0x004  | 0x24  | uint16[18]    | `outputs` | Level (`EFSDL`, bits 8-11) and pan (`EFPAN`, bits 0-4) of the 16 outputs of the DSP (`EFREG`), then of the 2 external inputs. Usually `0F00` (level 15, center) |
| 0x028  | 0x200 | uint32[128]   | `coef`    | Coefficient of each step of the program: a 13-bit signed value in bits 3-15 (e.g. `7FF8`) |
| 0x228  | 0x100 | uint32[64]    | `madrs`   | Addresses in the delay memory (16-bit values). Only the first 32 can be used by the program |
| 0x328  | 0x800 | uint32[128][4] | `mpro`   | Program: 128 steps of 4 words of 16 bits, see below              |
| 0xB28  | 0x100 | uint8[256]    | `unknown` | Always 0                                                         |

A step of the program is an instruction of the AICA in 4 words of 16 bits:

| Word | Bits  | Field   | Description                                                           |
|-----:|-------|---------|-----------------------------------------------------------------------|
| 0    | 9-15  | `TRA`   | Temporary register read                                              |
| 0    | 8     | `TWT`   | Write the temporary register `TWA`                                   |
| 0    | 1-7   | `TWA`   | Temporary register written                                           |
| 1    | 15    | `XSEL`  | X input: the input register (1) or the temporary register (0)        |
| 1    | 13-14 | `YSEL`  | Y input: `FRC_REG`, the coefficient of the step, or `Y_REG`           |
| 1    | 7-12  | `IRA`   | Input register: memory registers (0-`1F`), mixed voices (`20`-`2F`), external inputs (`30`-`31`) |
| 1    | 6     | `IWT`   | Write the memory register `IWA` with the last value read from memory |
| 1    | 1-5   | `IWA`   | Memory register written                                              |
| 2    | 15    | `TABLE` | The address doesn't move with the ring buffer                        |
| 2    | 14    | `MWT`   | Write the memory                                                     |
| 2    | 13    | `MRD`   | Read the memory                                                      |
| 2    | 12    | `EWT`   | Add the result to the output `EWA`                                   |
| 2    | 8-11  | `EWA`   | Output written                                                       |
| 2    | 7     | `ADRL`  | Load `ADRS_REG`                                                      |
| 2    | 6     | `FRCL`  | Load `FRC_REG`                                                       |
| 2    | 4-5   | `SHIFT` | Shift and saturation of the result                                   |
| 2    | 3     | `YRL`   | Load `Y_REG` with the input                                          |
| 2    | 2     | `NEGB`  | Subtract B instead of adding it                                      |
| 2    | 1     | `ZERO`  | B is 0                                                               |
| 2    | 0     | `BSEL`  | B is the accumulator (1) or the temporary register (0)               |
| 3    | 15    | `NOFL`  | The memory holds 16-bit integers instead of 16-bit floats            |
| 3    | 9-13  | `MASA`  | Index of the address in `madrs`                                      |
| 3    | 8     | `ADREB` | Add `ADRS_REG` to the address                                        |
| 3    | 7     | `NXADR` | Add 1 to the address                                                 |

There are only two different programs in the 754 files. Both are a stereo reverb of 100 steps (28 delay addresses): they read the mixed voices of input 0 and write the outputs 0 and 1. In 523 files the outputs 0 and 1 are `0F1F` and `0F0F` (left and right), in 66 files `0F0F` and `0F1F`, and in 155 files all the outputs are `0F00`.

## Section 7: Sample table

| Offset | Size  | Type       | Name      | Description                   |
|-------:|------:|------------|-----------|-------------------------------|
| 0x00   | 4     | uint32     | `last`    | Index of the last sample: the number of samples minus one (0 to 90) |
| 0x04   | 0x10×N | Sample[N] | `samples` | Samples                       |

### Sample (0x10 bytes)

| Offset | Size | Type   | Name        | Description                                                              |
|-------:|-----:|--------|-------------|--------------------------------------------------------------------------|
| 0x00   | 4    | uint32 | `offset`    | Bits 0-22: offset of the data, from the start of the file. Bits 23-24: format of the original sample, as in the AICA registers (0 16-bit PCM, 1 8-bit PCM, 2 ADPCM: bit 23 is the 8-bit "half quality" and bit 24 the ADPCM), which doesn't match the stored data (see below). Bit 25: probably loop |
| 0x04   | 2    | uint16 | `loopStart` | Start of the loop, in samples; always 0 when bit 25 isn't set            |
| 0x06   | 2    | uint16 | `loopEnd`   | End of the loop (or end of the sample), in samples                       |
| 0x08   | 4    | uint32 | `rate`      | 0, or `80` for a sample at half sample rate (38 samples, see below) |
| 0x0C   | 4    | uint32 | `size`      | Size of the data in bytes                                                |

Bits 23-25 of `offset` are 0 in 403 samples, 1 (`00800000`) in 198, 2 (`01000000`) in 737, 4 (`02000000`) in 1,007, 5 (`02800000`) in 1,034 and 6 (`03000000`) in 11,729. Bit 25 is not a mono/stereo flag; when it isn't set (1,338 samples), `loopStart` is always 0.

### Sample data

The data of the first sample starts right after the sample table, and the next ones follow without gaps, up to the end of the file (in 793 files; 2 bytes remain after the last sample in the other one). There are 15,108 samples in the 794 files. The sample rate is not stored in the table.

The data is **16-bit signed PCM** (mono), whatever the format given by bits 23-24: `loopEnd` is the number of samples (`size` / 2) minus 1 to 6 (in 14,882 samples). The exceptions:

- 118 samples (all of `sound/system1.snd` and most of `f1doujou.snd`) have a `loopEnd` of about 2 × `size`: their data is the 4-bit Yamaha ADPCM of the AICA, 2 samples per byte, the low nibble first. Silence is encoded as `80` bytes. Each nibble has a sign bit (bit 3) and a magnitude `m` (bits 0-2): the signal changes by ±(2 × `m` + 1) × `step` / 8, and then `step` is multiplied by `E6`, `E6`, `E6`, `E6`, `133`, `199`, `200` or `266` (for `m` = 0 to 7) and divided by `100`, kept between `7F` and `6000`. The signal starts at 0 and `step` at `7F`. The decoded signal can keep a DC offset at the end of the sample.
- In 21 samples (`a1_okozu.snd`, `amb012.snd`) `loopEnd` is about `size`, more than the samples of the 16-bit data.
- The 38 samples at half sample rate (`rate` `80`) have about twice the samples given by `loopEnd`, which counts the samples at half rate:
  - In 25 of the 33 samples of `a1_end.snd` every sample is stored twice: the data has been converted to the full rate by repeating the samples.
  - In the 5 samples of `fre1700.snd` the even and odd samples are two different but correlated signals, like interleaved stereo.
- 9,744 samples with bit 25 set have a loop of at most 64 frames (usually 28) at the end of the data: they are sounds played once, which hold their last frames until the voice is stopped. In 8,409 of them the frames of the loop are almost constant (silence, or an offset left by the ADPCM).

## Notes

- The `size` of the header always matches the size of the file.
