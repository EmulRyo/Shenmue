# MOTN — Motions

A MOTN file is a bank of motions (skeletal animations) of characters, with a name for each motion. The motions animate the bones of the [MT5 / CHRM](MT5) models; the cutscenes ([AUTH](AUTH)) and the character scripts ([CHRT](CHRT)) play them by their [motion ID](#motion-ids).

They are stored in the [IPAC](IPAC) block of the [PKS](PKS) files of the scenes, with the `MOTN` extension and names that start with `M_` (e.g. `M_01TE`, `M_D000`, `M_0000`). The game files contain 291 MOTN files (185 different ones: some are repeated in several scenes, e.g. `M_01JA` in `jomo/common01.pks` and `jomo/common03.pks`) in 207 PKS files of `scene/01`, `scene/02` and `scene/03`, from 528 to 1032292 bytes, with 1 to 513 motions each (4180 in total). Most PKS files hold one MOTN; a few hold up to 18. The motions shared by the whole game are in another MOTN file, `motion/motion.bin` (see [Common motions](#common-motions)), and each map has its own ones (see [Motions of the maps](#motions-of-the-maps)).

## Motions of the maps

The map folders (`scene/0N/<map>/`, those with a [MAPINFO](MAPINFO)) hold MOTN files with the `.bin` extension and a name that starts with `m_` (e.g. `scene/01/yq14/m_01114.bin`, `m_0114.bin` and `m_yq14.bin`): 154 files in 45 map folders (82 different ones), not compressed, from 980 to 2769320 bytes, with 17332 motions in total. Their `firstID` is usually `0x1000` (30 files) or `0x800` (16).

The cutscenes of a PKS whose name is the name of a map play the motions of that map, not of the PKS: `scene/01/d000/yq14.pks` has no MOTN, and its `SEQDATA2` plays the motions `0x1001`, `0x1002` and `0x1003` of `scene/01/yq14/m_01114.bin` (`AKI_WHATS_0114`, `OTH_GIA_WHATS_0114` and `OTH_GIB_YOPPARAI_0114`, of 638, 642 and 1007 frames: the last frames played by the commands). The IDs of the files of a map overlap: `m_0114.bin` of the same map also has the IDs `0x1001` and `0x1002`, for other motions (`BKB_KAKOMU_AKIREN_B_0258`, 470 frames, and `BKC_KAKOMU_AKIREN_C_0258`, 296 frames), too short for the commands. The other cutscene of the PKS, `SEQDATA1`, plays the motion `0x1004` from frame 612 to frame 691, while `0x1004` of `m_01114.bin` (`AKI_AITU_MASAKA_0448`) has 80 frames, and no MOTN file of the game has a motion `0x1004` of Ryo that long.

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
| 0x18   | 4    | uint32 | `lastID2`     | The same as `lastID`, except in `motion/motion.bin`, where it is 0 |
| 0x1C   | 4    | uint32 | `unknown`     | Always 0                                                        |

## Motion IDs

The motions are played by a number, not by their name: the motion `i` of the file (0-based) has the ID `firstID + 1 + i`, so the IDs of a file go from `firstID + 1` to `lastID - 1`. For example, `M_01TE` in `scene/01/d000/auth.pks` has `firstID = 0x1400` and 25 motions (IDs `0x1401` to `0x1419`, `lastID = 0x141A`); the cutscene `SEQDATA0` of the same PKS plays the motion `0x1418` (motion 23, `AKI_SIRABERU_DENWATYOU_ETC_TABACOYA_0100`, 925 frames) from frame 18 to frame 925.

## Common motions

`motion/motion.bin` (1826800 bytes, not compressed) is a MOTN file with the motions shared by the whole game: the walks, the stands, the doors... of Ryo and of the other characters. Its header has `firstID = 0`, `lastID = 0x618` and `lastID2 = 0`: it holds 1559 motions, with the IDs `0x1` to `0x617`, the small motion IDs of the cutscenes ([AUTH](AUTH)). For example, `scene/01/d000/buss/SEQDATA5` plays the motion `0x2DF` on Ryo three times (frames 1–36 of `AKI_AKI_WALK_SP1_LP`, 37 frames) while he walks away from the bus.

- 108 entries are placeholders named `ERROR`, `ERROR_1`…, and the 1559 entries point to 1319 different motions.
- The prefixes are mostly `AKI` (751) and `YKI` (207), then `AK`, `DMY`, `TA`, `RUF`…
- Of the 1559 entries, 1531 are keyframe motions (1292 of [character type](#character-types) 0, 207 of type 13), 19 are sampled and 9 are raw.
- It ends with the same [timestamp](#timestamp) as the other files (`Fri Nov 12 16:10:23 1999`).

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

The channels are numbered in the order of the list, and inside a bone from bit 8 down to bit 3, i.e. X, Y, Z. Almost every bone has only the rotation (`0x038`). Bone 0 (the hips) has position and rotation (`0x1F8`); its position Y is the height of the hips (about 1.13). A few bones have only the position (`0x1C0`), between bones with rotation (e.g. `5r 8p 9r`): they are the positions that the head, the hands and the feet reach (see [Skeleton](#skeleton)). The bone numbers are not the node IDs of the [MT5 / CHRM](MT5) models: a body has the nodes `0x01`, `0x04`–`0x06`, `0x09`–`0x0B`, `0x0E`, `0x10`–`0x13`, `0x15`–`0x18`… (e.g. `TAT_M.chrm`), with no node for some of the bones of the motions, and no bone for some of the nodes.

**Bone sets.** All the keyframe motions use one of three sets of bones (`p` = position, `r` = rotation, `pr` = both). A motion lists the bones of its set, and some motions leave out a few of them.

| Set | Motions | Character types | Bones |
|-----|--------:|-----------------|-------|
| Human | 3381 | 0–5, 7, 8, 10, 12, 13 | `0pr 1r 5r 8p 9r 12r 15p 16r 18r 20p 21r 23p 25r 26r 29p 30r 31r 32r 35p 36r`. 211 motions add `4r` and `11r` |
| Human, other numbering | 121 | 9, 15, 19 | `0pr 1r 5r 8p 9r 11r 14p 15r 16r 18p 19r 21p 23r 24r 27p 28r 29r 30r 33p 34r`: the same structure as the human set, from bone 11 on numbered one or two lower |
| Quadruped | 115 | 18, 20 | `0pr 1r 4r 5r 8p 9r 10r 11r 14p 15r 16r 17r 18r 19r 21p 22r 25r 27p 31r 32r 35p 36r 37r 38r 41p 42r` |

(Counts of different motions, without the repeated files. One motion of type 20 is in the second set but with the bones of the quadruped one.)

#### Skeleton

The motions don't animate the nodes of the models one by one: they give the hips, the rotations of a few parts of the body, and the **positions that the head, the hands and the feet reach** (inverse kinematics). The knees and the elbows are not in the motions: their bones (e.g. 6, 7, 27, 28) are never used, and their bend comes from the distance between the hip or the shoulder and the position of the foot or the hand. The meaning of the bones below was found by comparing the motions of Ryo (`AKI_*`) with his model `YKB_M.MT5`, in which the legs measure 0.838 (thigh 0.393, shin 0.445) and the arms 0.492 (upper arm 0.243, forearm 0.249).

All the positions are in the space of the character, with the floor at y = 0 and the front at −Z: they are not relative to the hips. The rotations are relative to the bone in the Parent column.

Bones of the human set (the numbers of the second set in parentheses, when they differ):

| Bone | Channels | Parent | Part | Node of the bodies | Description |
|-----:|----------|-------:|------|--------------------|-------------|
| 0    | p, r | — | Hips | — | Position of the hips (y ≈ 1.13 for Ryo standing, 0.56 in the motions of characters sitting on a seat that is not in the motion, e.g. `OTH_TKA_KAIWA_LP_HB_A`) and rotation of the whole body. The position moves the character in the walks, up to several meters |
| 1    | r | 0 | Pelvis | `0x0E` | About a quarter turn around Y (`(0, 0.25, 0)`, or other angles of the same rotation such as `(0.25, 0.25, 0.25)`) when the pelvis is as in the models. With this rotation, the positions of the feet are within reach of the legs of `YKB_M` in 199 of the 204 motions of Ryo; with the pelvis turned as the torso, only in 106 |
| 4, 11 | r | | Unknown | | In 211 motions only |
| 5    | r | 1 | Thigh | `0x10` | Rotation of the thigh, which is not the direction of the leg: in `AKI_QOK_QOK_HIGHKICK_HB_QTE` it only turns around the axis of the thigh (X) while the ankle goes from the floor to above the head. It *probably* gives the plane in which the knee bends, once the leg points to the ankle position |
| 8    | p | | Ankle | `0x12` | Position of the ankle (y ≈ 0.12 standing), on the +Z side of the pelvis of the models |
| 9    | r | | Foot | `0x12` | Rotation of the foot, about 0 when it is flat on the floor and points to the front (*probably* in the space of the character) |
| 12 (11) | r | 1 | Thigh | `0x15` | As bone 5, for the other leg |
| 15 (14) | p | | Ankle | `0x17` | As bone 8 |
| 16 (15) | r | | Foot | `0x17` | As bone 9 |
| 18 (16) | r | 0 | Torso | `0x01` | Rotation of the torso, with the angles of the node `0x01` of the models |
| 20 (18) | p | | Head | `0xFFBD` | Position of the head. It is at 0.380 from the hips (median), the length of the torso of Ryo (0.381), but the rotation of bone 18 alone doesn't always bring the head there (it ends more than 0.5 away in 10% of the frames): *probably* the torso turns to reach it |
| 21 (19) | r | 18 | Head | `0xFFBD` | Rotation of the head, with the angles of the node `0xFFBD` |
| 23 (21) | p | | Look | — | A point 0.2 in front of the face, on the X axis of the head (where the face looks): *probably* the point the head looks at |
| 25 (23) | r | 18 | Shoulder | `0x09` | Small rotation of the shoulder (under 0.06 turns) |
| 26 (24) | r | 25 | Upper arm | `0x0A` | Rotation of the upper arm (around Z about ±0.5, the 180° of the node `0x0A`), as bone 5 for the elbow |
| 29 (27) | p | | Wrist | `0xFFBE` | Position of the wrist. It is the arm of the −Z side of the models: from the other shoulder it is out of reach in 188 of the 204 motions of Ryo |
| 30 (28) | r | — | Hand | `0xFFBE` | Rotation of the hand, small: relative to the forearm |
| 31 (29) | r | 18 | Shoulder | `0x04` | As bone 25, for the arm of the +Z side |
| 32 (30) | r | 31 | Upper arm | `0x05` | As bone 26 |
| 35 (33) | p | | Wrist | `0xFFBF` | As bone 29 |
| 36 (34) | r | — | Hand | `0xFFBF` | As bone 30 |

**Swapped arms.** In the motions of the [character types](#character-types) 4 (`GAK`, `JAK`: Ryo as a child), 5 (`GEN`, `GIW`) and 7 (`JIJ`), the two arms are swapped: bones 25, 26, 29 and 30 are the arm of the +Z side and bones 31, 32, 35 and 36 the arm of the −Z side, while the legs keep their sides. Over the motions of `scene/01` and `motion/motion.bin` (taking the side from the direction the face looks, bone 23 from bone 20), the wrist of bone 29 is on the −Z side in 1213 of the 1292 motions of type 0, 280 of 314 of type 13 and 115 of 119 of type 10, and on the +Z side in all the 5 motions of type 4, all the 37 of type 7 and 13 of the 17 of type 5; the ankle of bone 8 is on the +Z side in almost all of them, whatever the type. The rotations follow: the median X rotation of the upper arm is positive for bone 26 and negative for bone 32 in types 0 and 13 (0.068 and −0.056 in type 0), and the opposite in types 4, 5 and 7 (−0.135 and 0.114 in type 7). The models of these characters (e.g. `JKB_M.chrm`, young Ryo, in `scene/01/yd01/sakr`) have the arms as the others.

The rest of the pelvis differs from the models: the node `0x0E` is a child of the torso turned 180° around Z, while bone 1 is relative to the hips and is a quarter turn around Y in the same pose. The rotations of the motions don't depend on how the body is turned in the file: in a few models the nodes above the torso are turned (e.g. 180° around Y in `OGM_L.chrm`), or the torso itself (`BAT_L.chrm` has `(270°, 0°, 90°)`, the usual rest turned 180° around Y, with the rest of the body turned with it). Bone 3 has a position in a single motion. The toes (`0x13`, `0x18`), the fingers and the faces of the models have no bones.

**Section 0 — Keyframe counts.** One uint8 per channel (uint16 if `wide` is set): the number of keys of the channel besides the first and the last one, which always exist. If the next section uses uint16 and the size is odd, one padding byte follows.

**Section 1 — Keyframe frames.** For each channel in order, the frames of its inner keys: uint8 if `frames` is 255 or less, uint16 otherwise. The first key is at frame 0 and the last one at frame `frames`; neither of them is stored. The frames grow, but two keys can have the same frame (683 channels), which makes a jump in the value.

**Section 2 — Key types.** For each channel in order, 2 bits per key (counting the first and the last one), 4 keys per byte starting with the highest bits; every channel starts on a new byte and the unused bits are 0. One padding byte follows if needed to make `offsets[3]` even. The type is the number of values the key stores in section 3:

| Type | Values | Description                                                                   |
|-----:|-------:|-------------------------------------------------------------------------------|
| 0    | 0      | No value. Channels with type 0 in all their keys (e.g. unused rotation axes) are common, so the value is probably 0 |
| 1    | 1      | `value`                                                                       |
| 2    | 2      | `tangent`, `value`: the slope of the curve at the key                         |
| 3    | 3      | `tangent`, `tangent`, `value` (probably the slopes before and after the key)  |

Most inner keys are of type 2; the first and last keys are usually of type 1 or 2.

The tangents are in units per **second**, at 30 frames per second: over the 381980 inner keys of type 2 of the files, the tangent divided by the slope between the previous and the next key (in units per frame) has a median of 29.99. The curves between two keys are *probably* cubic Hermite curves with these tangents (and a slope of 0 at the keys of type 1).

**Section 3 — Values.** float16 (IEEE 754 half precision) values for each channel in order, and inside a channel for each key in order, as many as its type says. Section 3 ends exactly at the end of the motion. Positions are in the units of the models (meters). Rotations are in **turns** (1 = 360°), around X, then Y, then Z, as the rotations of the nodes of the [MT5](MT5) models: the torso (bone 18) of the motions of Ryo is about `(0.25, 0, 0.25)`, the rotation (90°, 0°, 90°) of the node `0x01` of his model `YKB_M.MT5`, and the head (bone 21) about `(-0.25, 0, -0.25)`, like the node `0xFFBD` (270°, 0°, 270°). The same rotation can be written with other angles (e.g. `(-0.24, 0.48, -0.25)` for `(0.25, 0, 0.25)`).

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
