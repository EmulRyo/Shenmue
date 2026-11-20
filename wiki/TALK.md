# TALK — Face poses

A TALK file holds poses of the faces: for each of the 25 bones of an [FTBL](FTBL) face skin, the position of its end in every pose. The poses are the expressions of the face, with the eyes open and closed and the shapes of the mouth when speaking. The same file serves all the faces that use it, whatever their model.

- **Extension:** `.bin` (`talk_*.bin`, in the `model/face` folder of each scene, e.g. `scene/01/model/face/talk_n00.bin`; also `misc/cold_s/TALK_N00.bin`)
- **Magic:** none
- **Compression:** none

## Files

A file is a set of poses, or a chain of several sets one after the other. There are five different sets:

| Set  | Poses | Files |
|------|------:|-------|
| `s`  | 79 | `talk_s.bin`, first set of `talk_n00.bin` and `talk_n01.bin` |
| `m`  | 78 | `talk_m.bin`, second set of `talk_n00.bin` and `talk_n01.bin` |
| `b`  | 66 | `talk_b.bin`, third set of `talk_n00.bin` |
| `kc` | 6  | `talk_kc.bin`, third set of `talk_n01.bin` |
| `ss` | 67 | `talk_ss.bin`, fourth set of `talk_n00.bin` and `talk_n01.bin` |

The `talkSet` of an [FTBL](FTBL) file is the index of a set in a chain: 0 for `s` (Ryo and 79 other faces), 1 for `m`, 2 for `b` (or `kc`), 3 for `ss`.

## Layout of a set

```
+------------------------+ 0x00
| Header (0x10 bytes)    |
+------------------------+ 0x10
| Keys of channel 0      |  8 bytes per key
| Keys of channel 1      |
| ...                    |
| Keys of channel 74     |
+------------------------+ table1Offset
| Channel table 1        |  75 x 4 bytes
+------------------------+ table2Offset
| Channel table 2        |  75 x 4 bytes
+------------------------+ table2Offset + 0x12C
```

All the offsets are relative to the start of the set.

### Header (0x10 bytes)

| Offset | Size | Type   | Name           | Description |
|-------:|-----:|--------|----------------|-------------|
| 0x00   | 4    | uint32 | `table1Offset` | Position of the channel table 1, right after the keys |
| 0x04   | 4    | uint32 | `table2Offset` | Position of the channel table 2 (`table1Offset` + 0x12C) |
| 0x08   | 4    | uint32 | `poseCount`    | Number of poses |
| 0x0C   | 4    | uint32 | `nextOffset`   | Size of the set: position of the next set of the chain. 0 in `talk_s.bin`, `talk_b.bin` and `talk_ss.bin` |

## Channels

There are 75 channels: the X, Y and Z of the position of the end of each of the 25 bones of the [FTBL](FTBL#bone-0x30-bytes) files (channel `3 × bone + axis`), in the space of the face. The keys of the channels are stored one channel after the other, from channel 0.

### Key (8 bytes)

| Offset | Size | Type   | Name      | Description |
|-------:|-----:|--------|-----------|-------------|
| 0x00   | 2    | uint16 | `pose`    | Pose of the key |
| 0x02   | 2    | float16* | `slopeIn`  | Slope of the curve into the key, per pose |
| 0x04   | 2    | float16* | `slopeOut` | Slope of the curve out of the key, per pose. Always the same as `slopeIn` |
| 0x06   | 2    | float16* | `value`    | Position in the pose of the key (e.g. `0x16ED`, 0.0914, the X of the end of bone 0 of Ryo at rest) |

The values are 16-bit floats of their own (`float16*`): the sign in bit 15, a 6-bit exponent in bits 9 to 14 and a 9-bit mantissa. With all the bits but the sign at 0 the value is 0; else it is the 32-bit float whose sign is bit 15, whose exponent is 112 plus the 6-bit exponent read as a signed number (−32 to 31), and whose mantissa is the 9 bits followed by zeros. In other words, the 32-bit float is `sign << 31 | (bits 0-13) << 14`, plus `0x38000000`, minus `0x10000000` if bit 14 is set (code at `0x0C090158` of `1ST_READ.BIN`, PAL v1.001).

The keys of a channel go from pose 0 to the last pose in order, but some channels skip poses (e.g. channel 0 of `talk_m.bin` has no key for the poses 27 to 29): 312 of the 375 channels of the five sets have a key for every pose, the others between 37 and 75 keys. The value at a pose is found with a binary search of the last key before it, then a cubic Hermite curve to the next key (code at `0x0C0900B8`): with `t` the fraction of the way between the poses of the keys `a` and `b`, and `d` the number of poses between them,

```
value = a.value × (1 − 3t² + 2t³) + b.value × (3t² − 2t³) + d × (a.slopeOut × (t³ − 2t² + t) + b.slopeIn × (t³ − t²))
```

### Channel table entry (4 bytes)

| Offset | Size | Type   | Name       | Description |
|-------:|-----:|--------|------------|-------------|
| 0x00   | 2    | uint16 | `keyCount` | Number of keys of the channel, or 0 if the channel isn't part of this table |
| 0x02   | 2    | uint16 | `start`    | Position of the first key of the channel divided by 16, rounded down (1 + half the number of keys of the previous channels). The same in both tables |

Each channel is in exactly one of the two tables (`keyCount` not 0), the same in all the sets: they split the face in two parts that move independently.

| Table | Bones | Part |
|-------|-------|------|
| 1 | 1–3, 12–14, 17 and 21 (24 channels) | The eyes: eyelids and eyebrows |
| 2 | 0, 4–11, 15, 16, 18–20 and 22–24 (51 channels) | The mouth: jaw, lips and cheeks |

## Poses

The poses are in groups of 6, one group per expression: the pose of an expression is `6 × expression + n` (code at `0x0C0BBB60` of `1ST_READ.BIN`, PAL v1.001). The game takes the eyes of the poses `n` = 0 (open) and 1 (closed) to blink, and the mouth of the poses `n` = 0 to 5: the rest (0) and the shapes of the mouth of the [lip-sync tables](Lip-sync#key-8-bytes) (in another order: the shapes 1 to 5 are the poses 3, 2, 5, 4 and 1), or the pose that a change of expression gives (function `0x113` of the programs, command 4 of the [AUTH](AUTH) files). `s` has 13 expressions (and one more pose), `m` 13, `b` 11, `ss` 11 (and one more pose) and `kc` only one.

The game blinks every 60 to 90 frames (a random time), closing the eyes in 2 frames and opening them in 4, and goes from a pose to another in a number of frames given by the programs of the maps (see the functions `0x075`, `0x094` and `0x113` in [MAPINFO](MAPINFO#functions-of-the-game)).
