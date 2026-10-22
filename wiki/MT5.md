# MT5 / CHRM — Model

An MT5 file is a 3D model: a hierarchy of nodes (bones), each one with an optional mesh, and the textures used by the meshes. CHRM files use the same format, but their textures are stored outside the model: they only keep the IDs of the textures, which are stored in the `TEXN` blocks of the [PKF](PKF) of the same scene (e.g. the models of `atrk.pks` use the textures of `atrk.pkf`).

- **Extension:** `.mt5`, `.chrm` (CHRM files are found inside the [IPAC](IPAC) blocks of [PKS](PKS) files)
- **Magic:** `HRCM` (`48 52 43 4D`)
- **Compression:** the whole file can be [gzipped](Home#compression)
- **Source:** [`src/mt5.c`](https://github.com/EmulRyo/Shenmue/blob/master/src/mt5.c)

The `.mt6` files also start with `HRCM`, but their meshes (`polyType` 0x80 and above) use a different face format that isn't known yet.

## Layout

All offsets are absolute (from the start of the file); an offset of 0 means none.

```
+------------------------+ 0x00
| Header (12 bytes)      |
+------------------------+ 0x0C
| Faces of mesh 0        |  usually: faces, vertices and mesh header of each mesh
| Vertices of mesh 0     |
| Mesh header 0          |
| ...                    |
+------------------------+ firstNodeOffset
| Node                   |  0x40 bytes each
| ...                    |
+------------------------+ texturesOffset
| TEXD block             |
| TEXN blocks (MT5)      |  or a NAME block (CHRM)
| TEXL block             |
| PTRL block             |
+------------------------+
```

## Header (12 bytes)

| Offset | Size | Type    | Name              | Description                     |
|-------:|-----:|---------|-------------------|---------------------------------|
| 0x00   | 4    | char[4] | `id`              | Magic `HRCM`                    |
| 0x04   | 4    | uint32  | `texturesOffset`  | Position of the `TEXD` block    |
| 0x08   | 4    | uint32  | `firstNodeOffset` | Position of the root node       |

## Node (0x40 bytes)

The nodes form a tree: each node points to its first child and to its next sibling.

| Offset | Size | Type      | Name            | Description                                                                 |
|-------:|-----:|-----------|-----------------|-----------------------------------------------------------------------------|
| 0x00   | 4    | uint32    | `id`            | Node (bone) ID. The lowest byte looks like the bone number; the next byte is `0x00` or `0xFF` and the upper 16 bits `0` or `1` (unknown meaning) |
| 0x04   | 4    | uint32    | `meshOffset`    | Position of the [mesh header](#mesh-header-0x20-bytes), 0 if the node has no mesh |
| 0x08   | 12   | int32[3]  | `rotation`      | Rotation around X, Y, Z. `0x10000` = 360°                                   |
| 0x14   | 12   | float[3]  | `scale`         | Scale                                                                       |
| 0x20   | 12   | float[3]  | `position`      | Position, relative to the parent                                            |
| 0x2C   | 4    | uint32    | `childOffset`   | First child node                                                            |
| 0x30   | 4    | uint32    | `siblingOffset` | Next sibling node                                                           |
| 0x34   | 4    | uint32    | `parentOffset`  | Parent node. It is 0 in the root and in some nodes that are not the root (see [alternative meshes](#alternative-meshes)) |
| 0x38   | 4    | char[4]   | `name`          | 4 characters in the root node of some characters (e.g. `HOSL` in `HOS_L.chrm`), 0 otherwise |
| 0x3C   | 4    | uint32    | `unknown`       | Unknown, always 0                                                           |

## Mesh header (0x20 bytes)

| Offset | Size | Type     | Name             | Description                                                    |
|-------:|-----:|----------|------------------|----------------------------------------------------------------|
| 0x00   | 4    | uint32   | `polyType`       | Unknown. Values 0–5, 0x20 and 0x24 in MT5 files; 0 or 4 in CHRM files |
| 0x04   | 4    | uint32   | `verticesOffset` | Position of the vertices                                       |
| 0x08   | 4    | int32    | `vertexCount`    | Number of vertices                                             |
| 0x0C   | 4    | uint32   | `facesOffset`    | Position of the face data                                      |
| 0x10   | 12   | float[3] | `center`         | Center of the bounding sphere                                  |
| 0x1C   | 4    | float    | `radius`         | Radius of the bounding sphere                                  |

## Vertex (24 bytes)

| Offset | Size | Type     | Name       | Description          |
|-------:|-----:|----------|------------|----------------------|
| 0x00   | 12   | float[3] | `position` | Position             |
| 0x0C   | 12   | float[3] | `normal`   | Normal (unit length) |

## Face data

The faces of a mesh are a list of chunks. Each chunk starts with a `uint16` type; chunks with a size store it in the next `uint16`, which counts the bytes after the size field. The list is padded to 4 bytes and ends with the `0x8000` chunk.

| Type     | Size after the type | Content                                                                                       |
|----------|--------------------|-----------------------------------------------------------------------------------------------|
| `0x0000` | 0                  | Padding                                                                                       |
| `0x0002` | 2 + `size`         | `uint16 size` (0x10) + [polygon parameters](#polygon-parameters-16-bytes). Opaque polygons    |
| `0x0003` | 2 + `size`         | Same as `0x0002`. Translucent polygons                                                        |
| `0x0004` | 2 + `size`         | Same as `0x0002`. Punch-through polygons (only seen in MT5 files)                              |
| `0x0008` | 2                  | `uint16` unknown. Usually 0; also 0x1E, 0x22, 0x32, 0xB4, 0x12C                              |
| `0x0009` | 2                  | `uint16` index of the texture used by the next strips, in the [texture list](#textures)       |
| `0x000B` | 2                  | `uint16` UV divisor: UV = value / divisor. Usually 0x400; also 0x200 and 0x100                |
| `0x000E` | 2 + `size`         | `uint16 size` (8) + [colors](#colors-8-bytes) of the next strips                              |
| `0x0010` | 2 + `size`         | [Strips](#strips): vertex index                                                               |
| `0x0011` | 2 + `size`         | [Strips](#strips): vertex index, U, V                                                         |
| `0x0013` | 2 + `size`         | [Strips](#strips): vertex index (difference with `0x0010` unknown)                            |
| `0x001A` | 2 + `size`         | [Strips](#strips): vertex index, color                                                        |
| `0x001C` | 2 + `size`         | [Strips](#strips): vertex index, U, V, color                                                  |
| `0x8000` | 0                  | End of the face data                                                                          |

The values set by `0x0002`–`0x000E` apply to the strip chunks that follow them. CHRM files only use the chunks `0x0002`, `0x0003`, `0x0008`, `0x0009`, `0x000B`, `0x000E`, `0x0011` and `0x8000`.

### Polygon parameters (16 bytes)

They look like the polygon header of the Dreamcast PowerVR (CLX2) graphics chip.

| Offset | Size | Type   | Name     | Description                                                                                       |
|-------:|-----:|--------|----------|---------------------------------------------------------------------------------------------------|
| 0x00   | 4    | uint32 | `pcw`    | Parameter control word. Bits 24–26 are the list type: 0 opaque, 2 translucent, 4 punch-through (it matches the chunk type). E.g. `0x808C002E` |
| 0x04   | 4    | uint32 | `isp`    | ISP/TSP instruction word (depth compare and culling). Usually `0xC8000000`                        |
| 0x08   | 4    | uint32 | `tsp`    | TSP instruction word: blending (`0x20...` one/zero for opaque, `0x94...` source alpha/inverse source alpha for translucent), UV clamp and flip, filter, texture instruction |
| 0x0C   | 4    | uint32 | `tcw`    | Texture control word (pixel format in bits 27–29). The texture address is 0                      |

### Colors (8 bytes)

| Offset | Size | Type   | Name     | Description                                                       |
|-------:|-----:|--------|----------|-------------------------------------------------------------------|
| 0x00   | 4    | uint32 | `base`   | Base color, ARGB (e.g. `0xFFFFFFFF`, `0xFFB2B2B2`)                |
| 0x04   | 4    | uint32 | `offset` | Offset (specular) color, ARGB. Usually `0xFF000000` or `0xFFFFFFFF` |

### Strips

| Offset | Size | Type     | Name     | Description                                              |
|-------:|-----:|----------|----------|----------------------------------------------------------|
| 0x00   | 2    | uint16   | `type`   | `0x0010`–`0x001C`                                        |
| 0x02   | 2    | uint16   | `size`   | Size of the rest of the chunk, padded to 4 bytes         |
| 0x04   | 2    | uint16   | `count`  | Number of strips                                         |
| 0x06   | …    | Strip[N] | `strips` | Strips                                                   |

Each strip is an `int16` vertex count followed by that many entries. The count is always negative; its absolute value is the number of entries.

| Field    | Size | Type   | Present in              | Description                                                       |
|----------|-----:|--------|-------------------------|-------------------------------------------------------------------|
| `vertex` | 2    | int16  | all                     | Vertex index (see below)                                          |
| `u`      | 2    | int16  | `0x0011`, `0x001C`      | U × UV divisor                                                    |
| `v`      | 2    | int16  | `0x0011`, `0x001C`      | V × UV divisor                                                    |
| `color`  | 4    | uint32 | `0x001A`, `0x001C`      | Vertex color, ARGB                                                |

The entries form a triangle strip: entries *i*, *i*+1, *i*+2 make a triangle. Taking the usual strip order (*i*, *i*+1, *i*+2 for even *i* and *i*+1, *i*, *i*+2 for odd *i*), the triangles are clockwise when looking at the side the vertex normals point to.

A negative vertex index `-n` points to the vertex `vertexCount - n` of the mesh of the parent node: it joins the mesh to its parent (e.g. a forearm to the arm). Some models use negative indices that point to a mesh of another model: the faces (`_F`) and hands (`_TL`, `_TR`) of a character attach to its body (`_L`). Some MT5 files also use indices equal to or greater than `vertexCount`; their meaning is unknown.

## Alternative meshes

Some character models have a second branch under the root with copies of a node that have no mesh (same `id` as the head) and children with `parentOffset` = 0 and the same `id` as the face. They look like alternative faces (e.g. expressions) that replace the face mesh. Their negative vertex indices point to the mesh of the node with the same `id` as their parent (the head).

## Textures

### TEXD block (12 bytes)

| Offset | Size | Type    | Name           | Description               |
|-------:|-----:|---------|----------------|---------------------------|
| 0x00   | 4    | char[4] | `id`           | `TEXD`                    |
| 0x04   | 4    | uint32  | `size`         | Size of the block (12)    |
| 0x08   | 4    | uint32  | `textureCount` | Number of textures (can be 0) |

It is followed by blocks with an 8-byte header (`char[4] id`, `uint32 size` including the header):

### TEXN block (MT5)

One per texture, with the same structure as in [PKF](PKF#texn-block-texture) files.

| Offset | Size    | Type     | Name        | Description                         |
|-------:|--------:|----------|-------------|-------------------------------------|
| 0x00   | 4       | char[4]  | `id`        | `TEXN`                              |
| 0x04   | 4       | uint32   | `size`      | Size of the block                   |
| 0x08   | 8       | uint8[8] | `textureID` | Texture identifier                  |
| 0x10   | size−16 | uint8[]  | `data`      | Texture: [PVR](PVR) or DDS          |

### NAME block (CHRM)

| Offset | Size | Type        | Name         | Description                                                   |
|-------:|-----:|-------------|--------------|---------------------------------------------------------------|
| 0x00   | 4    | char[4]     | `id`         | `NAME`                                                        |
| 0x04   | 4    | uint32      | `size`       | 8 + 8 × `textureCount`                                        |
| 0x08   | 8×N  | uint8[8][N] | `textureIDs` | ID of each texture, the `textureID` of a TEXN block of the [PKF](PKF) |

### TEXL block

| Offset | Size | Type         | Name           | Description                                     |
|-------:|-----:|--------------|----------------|-------------------------------------------------|
| 0x00   | 4    | char[4]      | `id`           | `TEXL`                                          |
| 0x04   | 4    | uint32       | `size`         | 16 + 12 × `count`                               |
| 0x08   | 4    | uint32       | `entriesOffset`| Position of the entries (right after this header) |
| 0x0C   | 4    | uint32       | `count`        | Number of entries, equal to `textureCount`      |
| 0x10   | 12×N | Entry[N]     | `entries`      | One entry per texture                           |

| Offset | Size | Type   | Name        | Description                                                         |
|-------:|-----:|--------|-------------|---------------------------------------------------------------------|
| 0x00   | 4    | uint32 | `idOffset`  | Position of the `textureID` of the texture (in its TEXN or NAME block) |
| 0x04   | 4    | uint32 | `unknown1`  | Unknown, always 0 (probably filled at runtime)                      |
| 0x08   | 4    | uint32 | `unknown2`  | Unknown, always 0 (probably filled at runtime)                      |

### PTRL block

A relocation table: the positions of the pointer fields of the file (offsets of the mesh headers, nodes and TEXL entries), sorted, so they can be converted to memory addresses when the file is loaded. The offsets of the header and `entriesOffset` of TEXL are not in the list.

| Offset | Size | Type      | Name       | Description                                 |
|-------:|-----:|-----------|------------|---------------------------------------------|
| 0x00   | 4    | char[4]   | `id`       | `PTRL`                                      |
| 0x04   | 4    | uint32    | `size`     | 12 + 4 × `count`                            |
| 0x08   | 4    | uint32    | `count`    | Number of pointers                          |
| 0x0C   | 4×N  | uint32[N] | `pointers` | Position of each pointer field              |

## Notes

- A CHRM file and the MT5 file of the same model are identical up to `texturesOffset` (e.g. `ATRS500G.chrm` and `model/item/atrs500g.mt5`).
- Opening a model prints its node tree, the meshes (vertices and triangles) and the textures to the console.
- Extracted files with the `HRCM` magic get the `.mt5` extension, except the [IPAC](IPAC) entries with the `CHRM` extension, which keep it.
