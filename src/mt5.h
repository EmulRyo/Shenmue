#ifndef __MT5_H__
#define __MT5_H__

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// MT5 model (magic "HRCM"), also used by the CHRM files, whose textures are stored outside the model
// (in the PKF of the scene) and are referenced by their texture ID.

typedef struct {
	float		position[3];
	float		normal[3];
} MT5_Vertex;

typedef struct {
	int32_t		vertex; // Negative: vertex (parent vertexCount + vertex) of the mesh of the parent node
	float		u, v;
	uint32_t	color;  // ARGB, the base color of the colors chunk (or 0xFFFFFFFF) if the strip has no vertex colors
} MT5_Corner;

typedef enum {
	MT5_LIST_OPAQUE        = 0,
	MT5_LIST_TRANSLUCENT   = 2,
	MT5_LIST_PUNCH_THROUGH = 4,
} MT5_ListType;

// Texture wrapping outside [0, 1], from the TSP instruction word of the polygon parameters
typedef enum {
	MT5_UV_FLIP_U  = 1, // Mirrored repeat
	MT5_UV_FLIP_V  = 2,
	MT5_UV_CLAMP_U = 4,
	MT5_UV_CLAMP_V = 8,
} MT5_UVFlags;

// Counter-clockwise when looking at the side the vertex normals point to
typedef struct {
	MT5_Corner	corners[3];
	uint16_t	texture;  // Index in the texture list
	uint8_t		listType; // MT5_ListType
	uint8_t		uvFlags;  // MT5_UVFlags, repeat if 0
} MT5_Triangle;

typedef struct {
	uint32_t		id;
	uint32_t		name;        // 4 characters on the root node of some characters (e.g. "HOSL"), 0 otherwise
	float			rotation[3]; // Degrees
	float			scale[3];
	float			position[3];
	int32_t			parent;      // Index in the node list, -1 for the root
	bool			hasMesh;
	uint32_t		polyType;
	float			center[3];   // Bounding sphere
	float			radius;
	uint32_t		vertexCount;
	MT5_Vertex*		vertices;
	uint32_t		triangleCount;
	MT5_Triangle*	triangles;
} MT5_Node;

typedef struct {
	uint8_t			id[8];
	const uint8_t*	data; // PVR/DDS texture inside the model, NULL if the texture is external (CHRM)
	uint32_t		size;
} MT5_Texture;

typedef struct {
	uint8_t*		buffer; // Whole file, the embedded textures point into it
	size_t			size;
	uint32_t		nodeCount;
	MT5_Node*		nodes; // Depth first order, the root is the first one
	uint32_t		textureCount;
	MT5_Texture*	textures;
} MT5_Model;

// Loads an MT5/CHRM file (gzipped or not). Returns false if the file is not a valid model
bool MT5_Load(const char* filepath, MT5_Model* model);
void MT5_Free(MT5_Model* model);

// Node whose mesh is used by the negative vertex indices of the children of parent: the nearest ancestor that has a
// mesh, or another node with the same ID (e.g. the alternative faces of a character hang from a copy of the head node
// without mesh). Returns -1 if there is none
int32_t MT5_ParentMeshNode(const MT5_Model* model, int32_t parent);

// Prints the node tree, the meshes and the textures of an MT5/CHRM file
bool MT5_Print(const char* filepath);

#endif
