# HM — Hand skeleton and skin

An HM file holds the skeleton and the skin of the detailed hands of a character: the two hand models with the same three letters (`_TL` and `_TR` [MT5](MT5#faces-and-hands) files, e.g. `ykb_hm.bin` for `ykb_tl.mt5` and `ykb_tr.mt5`, Ryo). The skeleton has 71 nodes; each vertex of the mesh of a hand is moved by up to 6 of them with weights. The motions of the [HMOT](HMOT) files turn the nodes, which bends the fingers.

- **Extension:** `.bin` (`xxx_hm.bin`, in the `model/hand` folder of each scene, e.g. `scene/01/model/hand/ykb_hm.bin`; also `misc/cold_s/YKB_HM.BIN` and `misc/coldinit/YKB_HM.BIN`)
- **Magic:** none
- **Compression:** none

There are 152 HM files (17 different contents), 50 per scene, one per character with detailed hands. The `model/hand` folders also have the arms of Ryo of the close-ups (`_UL`, `_UR`), which the game poses with the same files, and another model of each hand of Ryo (`ykb_xl.mt5`, `ykb_xr.mt5`).

## Layout

```
+------------------------+ 0x00
| Header (0x18 bytes)    |
+------------------------+ orderOffset (0x18)
| Order of the nodes     |  71 x 2 bytes, then 2 bytes of padding
+------------------------+ nodesOffset (0xA8)
| Nodes (71 x 0x50)      |
+------------------------+ indicesOffset (0x16D8)
| Node indices           |  uint8[total]
+------------------------+ weightsOffset
| Weights                |  float[total]
+------------------------+ countsOffset
| Influence counts       |  uint8 per vertex of the hand mesh
+------------------------+ endOffset (the size of the file)
```

`total` is the sum of the influence counts (e.g. 619 in `ykb_hm.bin`). The number of vertices isn't stored: it is the vertex count of the hand model (299 to 306 in the files of the game); the sections end with a few unused values (e.g. `ykb_hm.bin` has 312 counts for the 306 vertices of `ykb_tr.mt5`).

## Header (0x18 bytes)

| Offset | Size | Type   | Name            | Description |
|-------:|-----:|--------|-----------------|-------------|
| 0x00   | 4    | uint32 | `orderOffset`   | Position of the order of the nodes. Always 0x18 |
| 0x04   | 4    | uint32 | `nodesOffset`   | Position of the nodes. Always 0xA8 |
| 0x08   | 4    | uint32 | `countsOffset`  | Position of the influence counts |
| 0x0C   | 4    | uint32 | `indicesOffset` | Position of the node indices of the influences. Always 0x16D8 |
| 0x10   | 4    | uint32 | `weightsOffset` | Position of the weights |
| 0x14   | 4    | uint32 | `endOffset`     | Size of the file |

## Order of the nodes (2 bytes each)

The nodes are listed depth first: each node is followed by its children, each with its own children.

| Offset | Size | Type  | Name       | Description |
|-------:|-----:|-------|------------|-------------|
| 0x00   | 1    | uint8 | `joint`    | Index of the joint (0 to 70, each once). 0 is the root |
| 0x01   | 1    | uint8 | `children` | Number of children |

The order is the same in all the files: the root (joint 0) has one child, joint 1, whose child, joint 2, has 6 branches. The branches of the fingers are chains of three nodes per joint of the finger (joints `k`, `k + 21` and `k + 42`, e.g. 23, 44 and 65).

## Node (0x50 bytes)

The nodes are in the order of the list above (node `n` is the `n`-th entry, whatever its joint).

| Offset | Size | Type      | Name     | Description |
|-------:|-----:|-----------|----------|-------------|
| 0x00   | 4    | float     | `length` | Translation along the X axis of the parent to the node. Unused by the root |
| 0x04   | 12   | float[3]  | `angles` | Rotation around X, Y and Z at rest, in radians (e.g. −0.2269, 0, π for node 1 of Ryo). For the root: its translation |
| 0x10   | 64   | float[16] | `rest`   | World matrix of the node at rest, by columns (the 4 floats of the X axis, of Y, of Z, then the position and 1) |

The matrix of a node is the matrix of its parent, then a translation (`length`, 0, 0) (for the root, `angles`), then the rotations around Z, Y and X in this order (code at `0x0C0905DC` of `1ST_READ.BIN`, PAL v1.001). With the angles of the file, every node gets its `rest` matrix.

## Skin

For each vertex, in the order of the vertices of the mesh of the root of the hand model: `count` (1 to 6) influences, each a node index (the position of the node in the list, not its `joint`) and a weight. The weights of a vertex add up to 1.

The skeleton is the one of the right hand, with the fingers along −Y; the hand models have them along +X. A vertex (x, y, z) of a hand model is the point (y, −x, z) of the skeleton, and (y, −x, −z) for the left hand, whose model is the mirror image. The game moves each point `p` by its nodes (code at `0x0C09082C`):

```
p' = p + Σ weight × (posed × rest⁻¹ × p − p)
```

where `rest` is the matrix of the node in the file and `posed` its matrix with the angles of the motion. The normals are moved the same way (as the point `p + normal`). With no motion the hand keeps the shape of its model.
