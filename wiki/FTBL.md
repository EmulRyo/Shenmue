# FTBL — Face skin

An FTBL file deforms the detailed face of a character (the `_F` [MT5](MT5#faces-and-hands) model with the same three letters, e.g. `ykb_ftbl.bin` for `ykb_f.mt5`, Ryo): it places 25 face bones and gives, for every vertex of the face mesh, the bones that move it and their weights. The bones are moved by the poses of the [TALK](TALK) files (the mouth when speaking, the eyelids when blinking, the expressions), and the file also holds how far the eyes can turn.

- **Extension:** `.bin` (`xxx_ftbl.bin`, in the `model/face` folder of each scene, e.g. `scene/01/model/face/ykb_ftbl.bin`; also `misc/cold_s/YKB_FTBL.bin` and `misc/coldinit/YKB_FTBL.bin`)
- **Magic:** none
- **Compression:** none

There are 161 FTBL files (47 different contents), one per face model.

## Layout

```
+------------------------+ 0x00
| Header (0x30 bytes)    |
+------------------------+ bonesOffset (0x30)
| Bones (25 x 0x30)      |
+------------------------+ indicesOffset
| Bone indices           |  int32[total]
+------------------------+ weightsOffset
| Weights                |  float[total]
+------------------------+ countsOffset
| Influence counts       |  int32 per vertex of the face mesh
+------------------------+ size
```

`total` is the sum of the influence counts. The number of vertices isn't stored: it is the vertex count of the mesh of the root node of the face model. The three sections can end with 1 or 2 more values that belong to no vertex (e.g. `acs_ftbl.bin` has 275 counts for the 274 vertices of `acs_f.mt5`).

## Header (0x30 bytes)

| Offset | Size | Type   | Name            | Description |
|-------:|-----:|--------|-----------------|-------------|
| 0x00   | 4    | int32  | `talkSet`       | Index of the set of poses in a [TALK](TALK) chain (0 to 3): 0 in 80 files (e.g. Ryo), 1 in 57, 2 in 9, 3 in 15 |
| 0x04   | 4    | uint32 | `bonesOffset`   | Position of the bones. Always 0x30 |
| 0x08   | 4    | uint32 | `skinOffset`    | Position of the three positions of the skin below (`countsOffset`). Always 0x24 |
| 0x0C   | 4    | int32  | `eyeYMax`       | Limits of the rotation of the eyes around their Y axis (positive: down), `0x10000` = 360°: maximum and minimum. Both eyes |
| 0x10   | 4    | int32  | `eyeYMin`       | |
| 0x14   | 4    | int32  | `eyeZMax1`      | Limits of the rotation of the eye `0x4D` around its Z axis (positive: to the left of the face, the side of the eye `0x4E`), maximum and minimum |
| 0x18   | 4    | int32  | `eyeZMin1`      | |
| 0x1C   | 4    | int32  | `eyeZMax2`      | Limits of the rotation of the eye `0x4E` around its Z axis, maximum and minimum |
| 0x20   | 4    | int32  | `eyeZMin2`      | |
| 0x24   | 4    | uint32 | `countsOffset`  | Position of the influence counts |
| 0x28   | 4    | uint32 | `indicesOffset` | Position of the bone indices |
| 0x2C   | 4    | uint32 | `weightsOffset` | Position of the weights |

The eyes are the nodes `0x4D` and `0x4E` of the face model, children of its root (e.g. at y = −0.031 and +0.031 in `ykb_f.mt5`). The game turns the eyes with the rotations of their nodes: around Y, 0 to the front and positive to look down (the Z axis of the face is the top of the head), then around Z, positive to the left of the face. The limits of Ryo are 12°, −2° (Y: 12° down, 2° up), 8°, −24° (`0x4D`) and 24°, −7° (`0x4E`): each eye turns about 24° outwards and 7° or 8° towards the nose. They differ from face to face (e.g. 17°, −7°, 22°, −18°, 22°, −18° in `aso_ftbl.bin`); `ykd_ftbl.bin`, another face of Ryo, has −34° instead of −2°.

## Bone (0x30 bytes)

The bones are in the space of the face model (X to the front of the face, Z to the top of the head, see [MT5](MT5#faces-and-hands)). They come in pairs, symmetric in Y, around the eyes and the mouth.

| Offset | Size | Type     | Name      | Description |
|-------:|-----:|----------|-----------|-------------|
| 0x00   | 12   | float[3] | `tip`     | A point on the surface of the face (e.g. the chin for bone 0) |
| 0x0C   | 4    | int32    | `type`    | Always 2 |
| 0x10   | 12   | float[3] | `root`    | A point behind `tip`, inside the head (e.g. near the joint of the jaw for bone 0) |
| 0x1C   | 12   | float[3] | `angles`  | Rotation around X, Y and Z of the frame of the bone at its root, in radians. Always (π/2, 0, 0) |
| 0x28   | 4    | float    | `length1` | Length of the first segment of the bone, from the root (e.g. 0.0110) |
| 0x2C   | 4    | float    | `length2` | Length of the second segment, to the end (e.g. 0.0391) |

A bone is a chain of two segments from its `root` (`type` 2; the code also handles a type 1, a single segment of `length1`). In a pose, the end of the chain goes to the position that the [TALK](TALK) file gives for the bone, or as far as it reaches in that direction: the game bends the chain like a limb, with the law of cosines (code at `0x0C090008` of `1ST_READ.BIN`, PAL v1.001), so the end is at the same direction from the root, at a distance between the difference and the sum of the two lengths. The displacement of the bone is from its `tip` to that end. In most bones `length1` and `length2` are the differences in Z and in X between `tip` and `root`, but not for bone 0 (0.0197 and 0.0989).

The bones of all the faces that use the same set of poses are at the same places: the pose 0 of each set of [TALK](TALK) has the `tip` of their bones (to 0.2 mm, the precision of the values of the keys).

Where the bones are in Ryo's face (`ykb_ftbl.bin`), from their `tip`:

| Bones | Place |
|-------|-------|
| 0 | Chin (the jaw) |
| 1–3, 12–14 | Above the eyes (eyelids and eyebrows) |
| 17, 21 | Under the eyes |
| 4–11, 22–24 | Around the mouth |
| 15, 18 | Cheeks, at the height of the mouth |
| 16, 20 | Cheeks, beside the nose |
| 19 | Tip of the nose |

## Skin

For each vertex of the face mesh, in order, `counts` gives the number of bones that move it (0 to 12, and 25 in `iwo_ftbl.bin`, where every vertex has all of them). Its bones and weights follow those of the previous vertices in the indices and weights sections. About a third of the vertices (100 of the 311 of Ryo, the back and the top of the head) have none.

| Section   | Type  | Description |
|-----------|-------|-------------|
| `indices` | int32 | Index of a bone (0 to 24) |
| `weights` | float | Weight of that bone. The weights of a vertex add up to 1 |
| `counts`  | int32 | Number of bones of the vertex |

The bones only move the vertices, they don't turn them: the game places each vertex at its position in the model plus, for each of its bones, the weight times the displacement of that bone in the current pose (code at `0x0C08FEA0` of `1ST_READ.BIN`, PAL v1.001).

## Notes

- `mgr_ftbl.bin` has 2,171 counts while `mgr_f.mt5` has 44 vertices (its root has the ID `0xFFFF` and only triangles with negative indices), and `fro_ftbl.bin` has 355 for the 338 vertices of `fro_f.mt5`: these tables don't match their model.
- The game replaces some faces by others, *probably* by the first three letters of the model of the character: `YKB`, `YKC` and `YKE` (Ryo) use `YKB_F.MT5` and `YKB_FTBL.BIN`, `YKD` and `YKF` use `YKD_F`, `FUB` and `FUK` use `FUK_F`, and `FUC` and `FUD` use `FUC_F` (code at `0x0C0BBB60`). `ykc_ftbl.bin` and `yke_ftbl.bin` are the same as `ykb_ftbl.bin`; `ykf_ftbl.bin`, `ykg_ftbl.bin` and `ykv_ftbl.bin` differ from it in a few bones and weights.
- For 8 faces (`HGY`, `HGR`, `HGM`, `HGS`, `HGC`, `OIS`, `HGN` and `KC1`) the game uses a fifth set of poses instead of `talkSet`.
