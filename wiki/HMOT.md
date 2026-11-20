# HMOT — Hand motions

An HMOT file holds motions of the detailed hands: for each node of the skeleton of the [HM](HM) files, its angles over the frames of the motion. The motions are numbered: each file has a range of IDs, and the programs of the maps load the files and play the motions by ID (see [MAPINFO](MAPINFO#functions-of-the-game)).

- **Extension:** `.bin` (`hmot_xxx.bin` or `hmotNNNN.bin`, in the folders of the maps and their packages, e.g. `scene/01/d000/hmot_yka.bin`, `scene/01/op00/hmot0102.bin`)
- **Magic:** none
- **Compression:** none

There are 46 HMOT files (10 different contents), with 915 motions (95 different names). The motions are of Ryo (`HMN_YKA_...`, e.g. `HMN_YKA_GOO_LP`, the fist) or of other characters (`HMN_KOK_ENGETU_R`, Lan Di). They play on the hands of any character, as the skeleton is the same.

## Layout

```
+------------------------+ 0x00
| Last ID (uint16)       |
| First ID (uint16)      |
| Motion offsets         |  uint32 per motion, from the first ID to the last one
+------------------------+
| Motions                |
+------------------------+
```

| Offset | Size | Type     | Name      | Description |
|-------:|-----:|----------|-----------|-------------|
| 0x00   | 2    | uint16   | `lastId`  | ID of the last motion (e.g. 79) |
| 0x02   | 2    | uint16   | `firstId` | ID of the first motion (e.g. 1) |
| 0x04   | 4 × n | uint32[] | `offsets` | Position of each motion, `n` = `lastId − firstId + 1` |

The IDs of the files of the game: 1–79 (`hmot_yka.bin`, the motions of Ryo), 541–546 (`hmot_can.bin`, the cans of the vending machines), 581–588 (`hmot_tel.bin`, the telephones), 601, 611–612, 621, 831–832, 851–852 and 9501–9504.

## Motion

| Offset | Size | Type     | Name        | Description |
|-------:|-----:|----------|-------------|-------------|
| 0x00   | 32   | char[32] | `name`      | e.g. `HMN_YKA_GOO_LP`, padded with zeros |
| 0x20   | 4    | int32    | `frames`    | Number of frames (2 to 292), 30 per second |
| 0x24   | 4    | int32    | `nodeCount` | Always 71, the nodes of the [HM](HM) files |
| 0x28   | 8 × 3 × `nodeCount` | | `channels` | For each node (in the order of the nodes of the HM files) and each axis X, Y and Z: the number of keys (int32) and the position of the first one from offset 0x28 of the motion (uint32) |

The value of a channel is the rotation of the node around the axis in degrees. A channel without keys (count 0) keeps the angle of the node at rest; a motion has keys in 25 to 84 of its 213 channels.

### Key (16 bytes)

| Offset | Size | Type  | Name       | Description |
|-------:|-----:|-------|------------|-------------|
| 0x00   | 4    | int32 | `frame`    | Frame of the key |
| 0x04   | 4    | float | `slopeIn`  | Slope of the curve into the key, per frame |
| 0x08   | 4    | float | `slopeOut` | Slope of the curve out of the key, per frame |
| 0x0C   | 4    | float | `value`    | Angle in degrees |

The keys are in the order of their frames. Some keys are before the first frame of the motion or after its last one (3,200 of the 324,646 keys), and the last key of a few channels is there twice. The value at a frame is the one of the first key before the first key, of the last key after the last one, and else a cubic Hermite curve between the key `a` before the frame and the key `b` after it (code at `0x0C0904E0` of `1ST_READ.BIN`, PAL v1.001): with `t` the fraction of the way from `a.frame` to `b.frame` and `d` the number of frames between them,

```
value = a.value × (1 − 3t² + 2t³) + b.value × (3t² − 2t³) + d × (a.slopeOut × (t³ − 2t² + t) + b.slopeIn × (t³ − t²))
```

The same curve gives the poses of the faces in the [TALK](TALK) files.
