#ifndef __MT5MESH_H__
#define __MT5MESH_H__

#include <stdbool.h>
#include <stdint.h>
#include <raylib.h>
#include "formats/model/mt5.h"

// Meshes of an MT5/CHRM model in the pose of its node hierarchy, ready to draw with raylib

// Triangles that share texture, list type and texture wrapping, drawn with one mesh
typedef struct {
	uint16_t	texture;
	uint8_t		listType;
	uint8_t		uvFlags;
	int32_t		frame; // Animation frame of the triangles, -1 if they are always shown
	uint32_t	triangleCount;
	uint32_t	filled; // Triangles copied to the mesh while it is built
	float		depth;  // Mean depth of the triangles of a translucent group, to draw the groups from back to front
	Mesh		mesh;
} MT5Mesh_Group;

typedef struct {
	uint32_t		groupCount;
	MT5Mesh_Group*	groups;
	uint32_t		triangleCount;
	uint32_t		skippedTriangles; // Triangles with vertex indices that can't be resolved
	uint32_t		hiddenNodes;      // Alternative meshes
	uint32_t		frameCount;       // Animation frames (see FindFrames), 0 if the model is not animated
	Vector3			front;            // Direction the model looks to
	Vector3			center;           // Bounding sphere of the model
	float			radius;
} MT5Mesh;

// Builds one mesh per group with the vertices in model space and uploads them. On error, result has to be freed too
bool MT5Mesh_Build(const MT5_Model* model, MT5Mesh* result);
void MT5Mesh_Free(MT5Mesh* meshes);

#endif
