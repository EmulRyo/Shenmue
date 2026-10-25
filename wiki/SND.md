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
| 0x01   | 1    | uint8     | `index`    | Index of the part in the list (in 6,849 of 7,145 parts)          |
| 0x02   | 1    | uint8     | `bank`     | Always 0                                                         |
| 0x03   | 1    | uint8     | `program`  | Index of a program of the [program bank](#section-1-program-bank) (always valid) |
| 0x04   | 4    | uint8[4]  | `unknown1` | Unknown, usually `00 7F 00 7F`                                   |
| 0x08   | 4    | uint8[4]  | `unknown2` | Unknown (e.g. `00 0C 7F 00`, `F0 0D 64 00`)                      |
| 0x0C   | 0x14 | uint8[20] | `unknown3` | Unknown, usually 0                                               |

## Section 1: Program bank

A list of offsets with one bank (in all the files), and the bank is a list of offsets to the programs (the instruments). The offsets of the programs are relative to the start of the bank.

### Program

| Offset | Size | Type       | Name           | Description                                                    |
|-------:|-----:|------------|----------------|----------------------------------------------------------------|
| 0x00   | 4    | uint32     | `flags`        | Bit 0: the layers have a key map. Values: 0, 1, 2 (only in the [effect program](#section-4-effect-program)), `80000000`, `00400000` |
| 0x04   | 4    | uint32     | `unknown`      | Unknown, usually 0 (also 10, 12, 20)                           |
| 0x08   | 8    | uint16[4]  | `layerOffsets` | Offsets of up to 4 layers, from the start of the program; 0 means no layer. The first one is always 0x10 |
| 0x10   | ...  | Layer[]    | `layers`       | Layers                                                         |

### Layer

| Offset | Size     | Type      | Name       | Description                                                     |
|-------:|---------:|-----------|------------|-----------------------------------------------------------------|
| 0x00   | 1        | uint8     | `last`     | Number of splits minus one                                      |
| 0x01   | 0x3F     | uint8[63] | `unknown`  | Unknown. Usually `00 7F` followed by `0F 7F` at 0x10 and `02 02` at 0x22 |
| 0x40   | K        | KeyMap    | `keyMap`   | Only when bit 0 of the program flags is set                     |
| 0x40+K | 0x40×N   | Split[N]  | `splits`   | Splits, ordered by key                                          |

#### Key map

| Offset | Size | Type      | Name      | Description                                                         |
|-------:|-----:|-----------|-----------|---------------------------------------------------------------------|
| 0x00   | 1    | uint8     | `lowKey`  | First key                                                           |
| 0x01   | 1    | uint8     | `highKey` | Last key                                                            |
| 0x02   | 2×M  | uint16[M] | `splits`  | For each key from `lowKey` to `highKey`, offset of its split from the start of the layer; 0 means no split |
| ...    | ...  | —         | padding   | Zeros up to a multiple of 4 bytes                                   |

#### Split (0x40 bytes)

A split plays one sample over a range of keys.

| Offset | Size | Type      | Name       | Description                                                       |
|-------:|-----:|-----------|------------|-------------------------------------------------------------------|
| 0x00   | 1    | uint8     | `highKey`  | Last key of the split (`7F` when there is only one split)         |
| 0x01   | 1    | uint8     | `flags`    | Unknown: 0 or `80`                                                |
| 0x02   | 1    | uint8     | `sample`   | Index of the sample in the [sample table](#section-7-sample-table). In 941 splits it is equal to the number of samples of the file (no sample of the file), and in 23 it is higher |
| 0x03   | 1    | uint8     | `unknown1` | Unknown: 1, or 0                                                  |
| 0x04   | 4    | uint8[4]  | `unknown2` | Unknown (e.g. `00 00 80 0C`, `00 00 80 80`, `00 00 11 0D`)        |
| 0x08   | 0x38 | uint8[56] | `unknown3` | Unknown. Probably the envelope and the pitch: it often contains `1F 00`, `FD 07 FD 07 FD 07 FD 27` and `10 D2` |

## Section 2: Curves

| Offset | Size     | Type          | Name     | Description                                        |
|-------:|---------:|---------------|----------|----------------------------------------------------|
| 0x00   | 4        | uint32        | `last`   | Number of curves minus one: 0 (712 files) or 1 (82) |
| 0x04   | 0x80×N   | uint8[N][128] | `curves` | Curves of 128 values                               |

The first curve is the same in every file: it rises from 0 (`00 00 07 0E 15 1C 24 ...`) to `FE`, quickly at first and slowly at the end. It is probably the curve that turns the velocity (0 to 127) into a level.

## Section 3: Sequence bank

The sequences (the songs, and the sound effects requested by the game) are grouped. All the offsets are relative to the start of the section.

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

### Sequence

A stream of events of variable size, in an unknown encoding. 15,139 of the 15,669 sequences start with the byte `C0`, 520 with `40` and 10 with `D1`. 9,105 sequences (the sound effects) are only 6 bytes, `C0 DF ii nn 80 FF`, where `ii` is probably the index of a split of the [effect program](#section-4-effect-program) and `nn` a value from 0 to 127.

## Section 4: Effect program

A single [program](#program) (flags 2, one layer without key map) with one split per sound effect. The first byte of each split is its index instead of a key.

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
| 0x004  | 0x24  | uint16[18]    | `outputs` | Level (bits 8-11) and pan (bits 0-4) of the 18 effect outputs. Usually `0F00` (level 15, center) |
| 0x028  | 0x200 | uint32[128]   | `coef`    | Coefficients (16-bit values with the 3 low bits at 0, e.g. `7FF8`) |
| 0x228  | 0x100 | uint32[64]    | `madrs`   | Addresses in the delay buffer (16-bit values)                    |
| 0x328  | 0x800 | uint32[128][4] | `mpro`   | Program: 128 steps of 4 words of 16 bits                         |
| 0xB28  | 0x100 | uint8[256]    | `unknown` | Always 0                                                         |

## Section 7: Sample table

| Offset | Size  | Type       | Name      | Description                   |
|-------:|------:|------------|-----------|-------------------------------|
| 0x00   | 4     | uint32     | `count`   | Number of samples (0 to 90)   |
| 0x04   | 0x10×N | Sample[N] | `samples` | Samples                       |

### Sample (0x10 bytes)

| Offset | Size | Type   | Name        | Description                                                              |
|-------:|-----:|--------|-------------|--------------------------------------------------------------------------|
| 0x00   | 4    | uint32 | `offset`    | Bits 0-22: offset of the data, from the start of the file. Bits 23-24: format of the original sample (0 16-bit PCM, 1 8-bit PCM, 2 ADPCM), which doesn't match the stored data (see below). Bit 25: loop |
| 0x04   | 2    | uint16 | `loopStart` | Start of the loop, in samples; always 0 when bit 25 isn't set            |
| 0x06   | 2    | uint16 | `loopEnd`   | End of the loop (or end of the sample), in samples                       |
| 0x08   | 4    | uint32 | `unknown`   | Unknown: 0, or `80` in 36 samples (`fre1700.snd`, `a1_end.snd`...) whose loop points count pairs of samples |
| 0x0C   | 4    | uint32 | `size`      | Size of the data in bytes                                                |

Bits 23-25 of `offset` are 0 in 359 samples, 1 (`00800000`) in 174, 2 (`01000000`) in 703, 4 (`02000000`) in 946, 5 (`02800000`) in 982 and 6 (`03000000`) in 11,150.

### Sample data

The data of the first sample starts 16 bytes after the end of the sample table (in 774 files), and the next ones follow without gaps, up to the end of the file. The sample rate is not stored in the table.

The data is **16-bit signed PCM** (mono), whatever the format given by bits 23-24: `loopEnd` is the number of samples (`size` / 2) minus 1 to 6. The exceptions:

- 116 samples (most of `system1.snd` and `f1doujou.snd`) have a `loopEnd` of about 2 × `size`: their data looks like 4-bit ADPCM (2 samples per byte).
- In 20 samples (`a1_okozu.snd`, `amb012.snd`) `loopEnd` is about `size`, more than the samples of the 16-bit data.

## Notes

- The `size` of the header always matches the size of the file.
- 941 splits refer to the sample with index `count` (one after the last sample), probably a sample of another bank loaded at the same time.
