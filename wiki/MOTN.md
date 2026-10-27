# MOTN — Motions

A MOTN file is a bank of motions (skeletal animations) of characters, with a name for each motion. The motions animate the bones of the [MT5 / CHRM](MT5) models; the cutscenes ([AUTH](AUTH)) and the character scripts ([CHRT](CHRT)) play them by their [motion ID](#motion-ids).

They are stored in the [IPAC](IPAC) block of the [PKS](PKS) files of the scenes, with the `MOTN` extension and names that start with `M_` (e.g. `M_01TE`, `M_D000`, `M_0000`). The game files contain 291 MOTN files (185 different ones: some are repeated in several scenes, e.g. `M_01JA` in `jomo/common01.pks` and `jomo/common03.pks`) in 207 PKS files of `scene/01`, `scene/02` and `scene/03`, from 528 to 1032292 bytes, with 1 to 513 motions each (4180 in total). Most PKS files hold one MOTN; a few hold up to 18.

- **Extension:** `.motn` (`MOTN` in the [IPAC](IPAC) dictionary)
- **Magic:** none
- **Compression:** none in the game files (the [PKS](PKS) files that contain them are usually [gzipped](Home#compression))

The motion names are in Japanese written in Latin letters, with a prefix for the character (e.g. `AKI_FIND_TELBOOK_F`, `AKI_MAWASU_DAIL_LP_F`, `TMN_KOI_SUISUI_LP`). `LP` marks the loops: their first and last keys are the same.

## Layout

```
+---------------------------+ 0x00
| Header (32 bytes)         |
+---------------------------+ 0x20 (tableOffset)
| Motion table              |  count × 8 bytes
+---------------------------+
| Attribute blocks          |  one per motion, variable size
+---------------------------+ namesOffset
| Name offsets              |  count × 4 bytes
| Names                     |  null-terminated strings
+---------------------------+ dataOffset (multiple of 4)
| Motion data               |
| ...                       |
+---------------------------+ size - 0x20
| Timestamp (32 bytes)      |
+---------------------------+ size
```

`count`, the number of motions, is not stored directly: it is `lastID - firstID - 1`, and also `(name offset 0 - namesOffset) / 4`.

## Header (32 bytes)

| Offset | Size | Type   | Name          | Description                                                     |
|-------:|-----:|--------|---------------|-----------------------------------------------------------------|
| 0x00   | 4    | uint32 | `tableOffset` | Position of the motion table. Always `0x20`                     |
| 0x04   | 4    | uint32 | `namesOffset` | Position of the name offsets                                    |
| 0x08   | 4    | uint32 | `dataOffset`  | Position of the motion data. The offsets of the motions are relative to it |
| 0x0C   | 4    | uint32 | `lastID`      | End of the range of [motion IDs](#motion-ids): `firstID + count + 1` |
| 0x10   | 4    | uint32 | `size`        | Size of the file                                                |
| 0x14   | 4    | uint32 | `firstID`     | Start of the range of [motion IDs](#motion-ids) (e.g. `0x1000`, `0x1400`, `0x7000`, `0x7923`) |
| 0x18   | 4    | uint32 | `lastID2`     | Always the same as `lastID`                                     |
| 0x1C   | 4    | uint32 | `unknown`     | Always 0                                                        |

## Motion IDs

The motions are played by a number, not by their name: the motion `i` of the file (0-based) has the ID `firstID + 1 + i`, so the IDs of a file go from `firstID + 1` to `lastID - 1`. For example, `M_01TE` in `scene/01/d000/auth.pks` has `firstID = 0x1400` and 25 motions (IDs `0x1401` to `0x1419`, `lastID = 0x141A`); the cutscene `SEQDATA0` of the same PKS plays the motion `0x1418` (motion 23, `AKI_SIRABERU_DENWATYOU_ETC_TABACOYA_0100`, 925 frames) from frame 18 to frame 925.

## Motion table (8 bytes per motion)

| Offset | Size | Type   | Name         | Description                                                       |
|-------:|-----:|--------|--------------|-------------------------------------------------------------------|
| 0x00   | 4    | uint32 | `dataOffset` | Position of the [motion data](#motion-data), relative to `dataOffset` of the header |
| 0x04   | 4    | uint32 | `attributes` | Position of the [attribute block](#attribute-block) of the motion (from the start of the file) |

The motions are not stored in the order of the table, and two entries can share the same data or the same attribute block. In 3 files (`M_ZAKO` of `drauth.pks`, `M_0000` of `jd00/koji.pks`, `M_EXBS` of `mfbt/mpk00.pks`) several entries are shared; `M_ZAKO` and `M_0000` of `koji.pks` have 513 entries, most of them placeholders named `F1ERROR0`, `F1ERROR1`… that point to the data of another motion and to an empty attribute block.

## Attribute block

A variable-size block of 32-bit words for each motion, between the motion table and the names. Its size is the distance to the next block (the last one ends at `namesOffset`); it is a multiple of 4 and always ends with a word `0`. The meaning is unknown.

- 2597 of the 3753 different blocks are 16 bytes of zeros, and 569 are `0, 0, 0, 0xFFFF0001, 0`.
- The rest (up to 188 bytes) start with three words (often 0) followed by a list of words that looks like a stream of commands at given frames: e.g. `0x00000008`, then pairs such as `0x00120001 0x00050008` and `0x01120001 0x00050008` (same values for `0x0012` and `0x0112`, probably the left and right side), or `0x05040005 0x001B76AB`.

## Names

`namesOffset` points to `count` uint32 offsets (from the start of the file), one per motion in the order of the table. Each one points to a null-terminated name. The names follow the offsets, padded with zeros up to `dataOffset`.

## Motion data

Every motion starts with a 32-bit word:

| Bits  | Name     | Description                                                                     |
|------:|----------|---------------------------------------------------------------------------------|
| 0–14  | `frames` | Length of the motion in frames: the keys go from frame 0 to frame `frames`      |
| 15    | `wide`   | The keyframe counts are uint16 instead of uint8 (61 motions, from 446 to 4412 frames) |
| 16–31 | `type`   | `0xFFFF`: [sampled motion](#sampled-motions). Bit 31 set: [raw motion](#raw-motions). Otherwise a [keyframe motion](#keyframe-motions), and the value is a [character type](#character-types) (0 to 20). The raw motions use the same values in bits 16–30 |

| Variant   | Motions | Files |
|-----------|--------:|-------|
| Keyframe  | 3661    | all   |
| Sampled   | 58      | objects and animals (e.g. the fish of `M_UO`, the birds of `M_TORI`) |
| Raw       | 69      | `M_EXBS` (`mfbt/mpk00.pks`), `M_CTER` (`d000/cterbase.pks`), `M_0000` (`jd00/koji.pks`) |

(Counts of different motions, without the repeated files.)

### Keyframe motions

The usual format: the motion is a list of channels (one value of one bone), and each channel is a curve of keys stored in four sections. All the offsets are relative to the start of the motion.

```
+---------------------------+ 0x00
| Header (12 bytes)         |
| Bone list                 |  uint16 per bone, ends with 0
+---------------------------+ offsets[0]
| Keyframe counts           |  one per channel
+---------------------------+ offsets[1]
| Keyframe frames           |
+---------------------------+ offsets[2]
| Key types                 |  2 bits per key
+---------------------------+ offsets[3]
| Values                    |  float16
+---------------------------+
```

| Offset | Size | Type      | Name      | Description                                                |
|-------:|-----:|-----------|-----------|------------------------------------------------------------|
| 0x00   | 4    | uint32    | `frames`  | [Frame count and flags](#motion-data)                      |
| 0x04   | 8    | uint16[4] | `offsets` | Position of each section                                   |
| 0x0C   | 2×N  | uint16[]  | `bones`   | Bone list, ended by a value `0`. `offsets[0]` is just after the end |

**Bone list.** Each uint16 is a bone and the channels it animates:

| Bits | Description                                                                           |
|-----:|---------------------------------------------------------------------------------------|
| 0–2  | Never set                                                                             |
| 3    | Rotation Z                                                                            |
| 4    | Rotation Y                                                                            |
| 5    | Rotation X                                                                            |
| 6    | Position Z                                                                            |
| 7    | Position Y                                                                            |
| 8    | Position X                                                                            |
| 9–15 | Bone number (0 to 42)                                                                 |

The channels are numbered in the order of the list, and inside a bone from bit 8 down to bit 3 (the axis order X, Y, Z is likely but not verified). Almost every bone has only the rotation (`0x038`). Bone 0 (the root) has position and rotation (`0x1F8`); its position Y is the height of the body (about 1.13). A few bones have only the position (`0x1C0`), between bones with rotation (e.g. `5r 8p 9r`), probably targets for the arms and legs. The bone numbers are not the node IDs of the [MT5 / CHRM](MT5) models: a body has the nodes `0x01`, `0x04`–`0x06`, `0x09`–`0x0B`, `0x0E`, `0x10`–`0x13`, `0x15`–`0x18`… (e.g. `TAT_M.chrm`), with no node for some of the bones of the motions.

**Bone sets.** All the keyframe motions use one of three sets of bones (`p` = position, `r` = rotation, `pr` = both). A motion lists the bones of its set, and some motions leave out a few of them.

| Set | Motions | Character types | Bones |
|-----|--------:|-----------------|-------|
| Human | 3381 | 0–5, 7, 8, 10, 12, 13 | `0pr 1r 5r 8p 9r 12r 15p 16r 18r 20p 21r 23p 25r 26r 29p 30r 31r 32r 35p 36r`. 211 motions add `4r` and `11r` |
| Human, other numbering | 121 | 9, 15, 19 | `0pr 1r 5r 8p 9r 11r 14p 15r 16r 18p 19r 21p 23r 24r 27p 28r 29r 30r 33p 34r`: the same structure as the human set, from bone 11 on numbered one or two lower |
| Quadruped | 115 | 18, 20 | `0pr 1r 4r 5r 8p 9r 10r 11r 14p 15r 16r 17r 18r 19r 21p 22r 25r 27p 31r 32r 35p 36r 37r 38r 41p 42r` |

(Counts of different motions, without the repeated files. One motion of type 20 is in the second set but with the bones of the quadruped one.)

**Section 0 — Keyframe counts.** One uint8 per channel (uint16 if `wide` is set): the number of keys of the channel besides the first and the last one, which always exist. If the next section uses uint16 and the size is odd, one padding byte follows.

**Section 1 — Keyframe frames.** For each channel in order, the frames of its inner keys: uint8 if `frames` is 255 or less, uint16 otherwise. The first key is at frame 0 and the last one at frame `frames`; neither of them is stored. The frames grow, but two keys can have the same frame (683 channels), which makes a jump in the value.

**Section 2 — Key types.** For each channel in order, 2 bits per key (counting the first and the last one), 4 keys per byte starting with the highest bits; every channel starts on a new byte and the unused bits are 0. One padding byte follows if needed to make `offsets[3]` even. The type is the number of values the key stores in section 3:

| Type | Values | Description                                                                   |
|-----:|-------:|-------------------------------------------------------------------------------|
| 0    | 0      | No value. Channels with type 0 in all their keys (e.g. unused rotation axes) are common, so the value is probably 0 |
| 1    | 1      | `value`                                                                       |
| 2    | 2      | `unknown`, `value`. The unknown value is small (probably a tangent)            |
| 3    | 3      | `unknown`, `unknown`, `value` (probably two tangents)                         |

Most inner keys are of type 2; the first and last keys are usually of type 1 or 2.

**Section 3 — Values.** float16 (IEEE 754 half precision) values for each channel in order, and inside a channel for each key in order, as many as its type says. Section 3 ends exactly at the end of the motion. Rotations are in radians (99% under 1 in absolute value) and positions in the units of the models (meters).

Example (`scene/01/jomo/common01.pks`, `M_TBLK`, motion 0 `AKI_KAIWA_LP_DENWA_HADUKIKE`, 60 frames):

```
bone list    0x01F8 (bone 0, pos+rot) 0x0238 (bone 1, rot) 0x0A38 (bone 5, rot) 0x11C0 (bone 8, pos) ... 0 → 63 channels
channel 1    bone 0 position Y   counts 1  frames [0] 30 [60]  types 2 2 2
             values (-0.0011, 1.1309) (-0.0046, 1.1299) (-0.0011, 1.1309)
channel 3    bone 0 rotation X   counts 0  frames [0] [60]     types 3 1
             values (-0.0061, 0.0, -0.0010) (-0.0010)
```

### Sampled motions

`type` is `0xFFFF`. The motion stores, for each node, up to 6 channels with one signed 16-bit value per frame (`frames + 1` values). Used for objects and animals (e.g. fish and birds), not for the characters. All the offsets are relative to the start of the motion.

| Offset | Size  | Type    | Name       | Description                                    |
|-------:|------:|---------|------------|------------------------------------------------|
| 0x00   | 4     | uint32  | `frames`   | [Frame count](#motion-data) and `0xFFFF`       |
| 0x04   | 2     | uint16  | `unknown`  | Always `0x3C00`                                |
| 0x06   | 2     | uint16  | `unknown`  | Always 0                                       |
| 0x08   | 2     | uint16  | `count`    | Number of nodes                                |
| 0x0A   | 2     | uint16  | `unknown`  | Always 0                                       |
| 0x0C   | 32×N  | —       | nodes      | One entry per node                             |
| —      | —     | int16[] | samples    | `frames + 1` values for each animated channel, in the order of the entries |

Node entry (32 bytes):

| Offset | Size | Type       | Name       | Description                                                          |
|-------:|-----:|------------|------------|----------------------------------------------------------------------|
| 0x00   | 2    | uint16     | `unknown`  | Usually 0                                                            |
| 0x02   | 6    | float16[3] | `scale`    | Probably the scale X, Y, Z. Usually (1, 1, 1)                        |
| 0x08   | 24   | —          | channels   | 6 channels of 4 bytes: uint16 `offset` of the samples (0 if the channel is constant) and uint16 `value` (the constant value, or the first sample) |

The first three channels are animated in almost all the nodes, and they look like rotations (`0x10000` = 360°); the last three are rarely animated. 3 of the 58 sampled motions (motion 22 of `M_01JAN` in `jhd0/kakg.pks`, motions 5 and 6 of `M_TORI` in `op02/shdr.pks`) don't follow this layout: in `M_01JAN` the first entry has more channels and the next entry is shifted.

### Raw motions

Bit 31 of the first word is set. The motion stores a block of the same size for each frame, with no keys.

| Offset | Size          | Type      | Name        | Description                                                |
|-------:|--------------:|-----------|-------------|------------------------------------------------------------|
| 0x00   | 4             | uint32    | `frames`    | [Frame count](#motion-data); bit 31 set                     |
| 0x04   | 2             | uint16    | `frameSize` | Size of each frame in bytes: `0x62` (98) in 67 motions, `0x56` or `0x76` in the others |
| 0x06   | 6             | uint16[3] | `unknown`   | Unknown, probably a mask of the stored channels: `0x52A2, 0xD625, 0x0015` with `frameSize = 0x62` |
| 0x0C   | 2             | uint16    | `unknown`   | Always 0                                                    |
| 0x0E   | frames × size | —         | frames      | 16-bit values for each frame (meaning unknown)              |

## Character types

The value of bits 16–30 of the first word of a keyframe or raw motion depends on the character, given by the prefix of the motion name: all the motions with the same prefix have the same type, except `DMY` (types 0, 1, 5, 10 and 13). Several characters share a type, and different types use the same [bone set](#keyframe-motions), so the type is not the skeleton.

| Type | Motions | Prefixes |
|-----:|--------:|----------|
| 0    | 1711 | `AKI` (1392, Ryo), `TKA`, `TA`, `JACKY`, `WOLF`, `SAM`, `OHS`, `GRD`, `WLF`, `JEF`, `TOU`, `SEK`… |
| 1    | 18   | `BIG`, `DMY` |
| 2    | 34   | `CHA` |
| 3    | 17   | `DEB`, `BKB`, `BKF` |
| 4    | 67   | `GAK`, `JAK` |
| 5    | 36   | `GEN`, `GIW`, `DMY` |
| 7    | 108  | `JIJ`, `INE` |
| 8    | 32   | `KOD` |
| 9    | 7    | `KOK` |
| 10   | 789  | `OTH` (700), `IWA`, `LLY`, `FUK`, `BKA`, `BKG`, `BKD`, `DMY` |
| 12   | 66   | `SYP` |
| 13   | 503  | `YKI` (407), `KIS`, `CIN`, `DMY`, `KEB`, `JIM`, `BKE`, `TAT`, `BKC`, `BKH`, `SMM` |
| 15   | 107  | `SIN` |
| 18   | 21   | `DOG` |
| 19   | 6    | `PNW` |
| 20   | 95   | `KCT` |

The raw motions (prefix `EXB`) have the types 0, 7, 10, 12, 13 and 16.

The files don't reference any model or skeleton, and their names (`M_01TE`, `M_ZAKO`…) are not the names of the characters: a motion is bound to a character by its [motion ID](#motion-ids), used by the cutscenes ([AUTH](AUTH)) together with the ID of the character.

## Timestamp

The last 32 bytes of every file hold the date of creation of the file as a C `ctime` string, e.g. `Thu Sep 16 16:13:32 1999\n`, followed by a null and garbage up to the 32 bytes. It starts at a multiple of 4; up to 2 bytes of padding separate it from the last motion. The dates go from November 1998 to November 1999 (most of them September 1999).
