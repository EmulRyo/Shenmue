#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "core/file.h"
#include "formats/model/mt5.h"

// An MT5 file is a header, the data of the meshes (faces, vertices and mesh header of each one), the node
// hierarchy and a TEXD block followed by the textures (TEXN), or their IDs (NAME) in CHRM files, a TEXL block
// with the texture list and a PTRL block with the positions of all the pointers of the file.
// All offsets are absolute, 0 means none.
typedef struct {
	char		id[4]; // "HRCM"
	uint32_t	texturesOffset; // TEXD block
	uint32_t	firstNodeOffset;
} MT5_Header;

typedef struct {
	uint32_t	id;
	uint32_t	meshOffset;
	int32_t		rotation[3]; // 0x10000 = 360 degrees
	float		scale[3];
	float		position[3];
	uint32_t	childOffset;
	uint32_t	siblingOffset;
	uint32_t	parentOffset; // 0 in some nodes that are not the root
	uint32_t	name;
	uint32_t	unknown;
} MT5_NodeData;

typedef struct {
	uint32_t	polyType;
	uint32_t	verticesOffset;
	int32_t		vertexCount;
	uint32_t	facesOffset;
	float		center[3];
	float		radius;
} MT5_MeshData;

typedef struct {
	char		id[4];
	uint32_t	size; // Includes the header
} MT5_Block;

typedef struct {
	char		id[4]; // "TEXD"
	uint32_t	size;
	uint32_t	textureCount;
} MT5_TextureHeader;

// The faces of a mesh are a list of chunks that starts with a uint16 type and ends with MT5_CHUNK_END
typedef enum {
	MT5_CHUNK_PADDING            = 0x0000,
	MT5_CHUNK_ATTR_OPAQUE        = 0x0002, // uint16 size + PowerVR polygon header (PCW, ISP/TSP, TSP, TCW)
	MT5_CHUNK_ATTR_TRANSLUCENT   = 0x0003, // Same as MT5_CHUNK_ATTR_OPAQUE
	MT5_CHUNK_ATTR_PUNCH_THROUGH = 0x0004, // Same as MT5_CHUNK_ATTR_OPAQUE
	MT5_CHUNK_UNKNOWN_8          = 0x0008, // uint16 value
	MT5_CHUNK_TEXTURE            = 0x0009, // uint16 texture index
	MT5_CHUNK_UV_SCALE           = 0x000B, // uint16 UV divisor (256 if there is none)
	MT5_CHUNK_COLORS             = 0x000E, // uint16 size + base and offset ARGB colors. The base color is the color of
	                                       // the vertices of the next strips without vertex colors
	MT5_CHUNK_STRIP              = 0x0010, // Strips of vertex indices
	MT5_CHUNK_STRIP_UV           = 0x0011, // Strips of vertex index, u, v
	MT5_CHUNK_STRIP_13           = 0x0013, // Strips of vertex indices
	MT5_CHUNK_STRIP_COLOR        = 0x001A, // Strips of vertex index, color
	MT5_CHUNK_STRIP_UV_COLOR     = 0x001C, // Strips of vertex index, u, v, color
	MT5_CHUNK_END                = 0x8000,
} MT5_ChunkType;

typedef struct {
	uint16_t	texture;
	uint8_t		listType;
	uint8_t		uvFlags;
	uint32_t	baseColor; // Of the last colors chunk
	float		uvScale;
	uint32_t	capacity;
} MT5_FaceState;

static bool InBounds(size_t size, uint64_t offset, uint64_t length) {
	return offset <= size && length <= size - offset;
}

static uint16_t Read16(const uint8_t* data) {
	uint16_t value;
	memcpy(&value, data, sizeof(value));
	return value;
}

static uint32_t Read32(const uint8_t* data) {
	uint32_t value;
	memcpy(&value, data, sizeof(value));
	return value;
}

static MT5_Corner ReadCorner(const uint8_t* entry, bool hasUV, bool hasColor, float uvScale, uint32_t baseColor) {
	MT5_Corner corner = { (int16_t)Read16(entry), 0.0f, 0.0f, baseColor };
	entry += 2;
	if (hasUV) {
		corner.u = (int16_t)Read16(entry) / uvScale;
		corner.v = (int16_t)Read16(entry + 2) / uvScale;
		entry += 4;
	}
	if (hasColor)
		corner.color = Read32(entry);
	return corner;
}

static bool AddTriangle(MT5_Node* node, MT5_FaceState* state, MT5_Corner a, MT5_Corner b, MT5_Corner c) {
	if (a.vertex == b.vertex || a.vertex == c.vertex || b.vertex == c.vertex)
		return true; // Degenerate triangle

	if (node->triangleCount == state->capacity) {
		uint32_t capacity = state->capacity > 0 ? state->capacity * 2 : 64;
		MT5_Triangle* triangles = realloc(node->triangles, capacity * sizeof(MT5_Triangle));
		if (triangles == NULL)
			return false;
		node->triangles = triangles;
		state->capacity = capacity;
	}
	MT5_Triangle* triangle = &node->triangles[node->triangleCount++];
	triangle->corners[0] = a;
	triangle->corners[1] = b;
	triangle->corners[2] = c;
	triangle->texture = state->texture;
	triangle->listType = state->listType;
	triangle->uvFlags = state->uvFlags;
	return true;
}

// Strip chunk: uint16 type, uint16 size (from the next field to the end of the chunk, padded to 4 bytes),
// uint16 strip count, and for each strip an int16 vertex count (always negative) followed by the vertices
static bool ReadStrips(const uint8_t* buffer, size_t pos, MT5_Node* node, MT5_FaceState* state) {
	uint16_t type = Read16(buffer + pos);
	bool hasUV = type == MT5_CHUNK_STRIP_UV || type == MT5_CHUNK_STRIP_UV_COLOR;
	bool hasColor = type == MT5_CHUNK_STRIP_COLOR || type == MT5_CHUNK_STRIP_UV_COLOR;
	size_t entrySize = 2 + (hasUV ? 4 : 0) + (hasColor ? 4 : 0);

	size_t end = pos + 4 + Read16(buffer + pos + 2);
	uint16_t count = Read16(buffer + pos + 4);
	pos += 6;
	for (uint16_t i = 0; i < count; i++) {
		if (pos + 2 > end)
			return false;
		size_t length = (size_t)abs((int16_t)Read16(buffer + pos));
		pos += 2;
		if (pos + length * entrySize > end)
			return false;

		// The strips are clockwise, the triangles are stored counter-clockwise
		for (size_t j = 0; j + 2 < length; j++) {
			const uint8_t* entry = buffer + pos + j * entrySize;
			MT5_Corner c0 = ReadCorner(entry, hasUV, hasColor, state->uvScale, state->baseColor);
			MT5_Corner c1 = ReadCorner(entry + entrySize, hasUV, hasColor, state->uvScale, state->baseColor);
			MT5_Corner c2 = ReadCorner(entry + entrySize * 2, hasUV, hasColor, state->uvScale, state->baseColor);
			bool added = (j & 1) == 0 ? AddTriangle(node, state, c0, c2, c1) : AddTriangle(node, state, c0, c1, c2);
			if (!added)
				return false;
		}
		pos += length * entrySize;
	}
	return true;
}

static bool ReadFaces(const uint8_t* buffer, size_t size, size_t pos, MT5_Node* node) {
	MT5_FaceState state = { 0, MT5_LIST_OPAQUE, 0, 0xFFFFFFFF, 256.0f, 0 }; // Without UV divisor chunk (e.g. KOB_L.chrm) the UVs go from 0 to 256
	while (InBounds(size, pos, 2)) {
		uint16_t type = Read16(buffer + pos);
		if (type == MT5_CHUNK_END)
			return true;
		if (type == MT5_CHUNK_PADDING) {
			pos += 2;
			continue;
		}
		if (!InBounds(size, pos, 4))
			break;

		uint16_t value = Read16(buffer + pos + 2);
		switch (type) {
		case MT5_CHUNK_ATTR_OPAQUE:
		case MT5_CHUNK_ATTR_TRANSLUCENT:
		case MT5_CHUNK_ATTR_PUNCH_THROUGH:
			if (value < 4 || !InBounds(size, pos + 4, value))
				return false;
			state.listType = (buffer[pos + 7] & 7); // List type of the PCW (bits 24-26)
			if (value >= 12) {
				// Flip U and V (bits 18, 17) and clamp U and V (bits 16, 15) of the TSP
				uint32_t tsp = Read32(buffer + pos + 12);
				state.uvFlags = ((tsp >> 18) & 1 ? MT5_UV_FLIP_U : 0) | ((tsp >> 17) & 1 ? MT5_UV_FLIP_V : 0) |
					((tsp >> 16) & 1 ? MT5_UV_CLAMP_U : 0) | ((tsp >> 15) & 1 ? MT5_UV_CLAMP_V : 0);
			}
			pos += 4 + value;
			break;
		case MT5_CHUNK_COLORS:
			if (!InBounds(size, pos + 4, value))
				return false;
			if (value >= 4)
				state.baseColor = Read32(buffer + pos + 4);
			pos += 4 + value;
			break;
		case MT5_CHUNK_UNKNOWN_8:
			pos += 4;
			break;
		case MT5_CHUNK_TEXTURE:
			state.texture = value;
			pos += 4;
			break;
		case MT5_CHUNK_UV_SCALE:
			if (value == 0)
				return false;
			state.uvScale = (float)value;
			pos += 4;
			break;
		case MT5_CHUNK_STRIP:
		case MT5_CHUNK_STRIP_UV:
		case MT5_CHUNK_STRIP_13:
		case MT5_CHUNK_STRIP_COLOR:
		case MT5_CHUNK_STRIP_UV_COLOR:
			if (value < 2 || !InBounds(size, pos + 4, value) || !ReadStrips(buffer, pos, node, &state))
				return false;
			pos += 4 + value;
			break;
		default:
			fprintf(stderr, "[MT5_Load]: Unknown face chunk 0x%04X at 0x%04zX\n", type, pos);
			return false;
		}
	}
	return false;
}

static bool ReadMesh(const uint8_t* buffer, size_t size, uint32_t offset, MT5_Node* node) {
	if (!InBounds(size, offset, sizeof(MT5_MeshData)))
		return false;
	MT5_MeshData mesh;
	memcpy(&mesh, buffer + offset, sizeof(mesh));
	if (mesh.vertexCount < 0 || !InBounds(size, mesh.verticesOffset, (uint64_t)mesh.vertexCount * sizeof(MT5_Vertex)))
		return false;

	node->hasMesh = true;
	node->polyType = mesh.polyType;
	memcpy(node->center, mesh.center, sizeof(node->center));
	node->radius = mesh.radius;
	node->vertexCount = (uint32_t)mesh.vertexCount;
	if (node->vertexCount > 0) {
		node->vertices = malloc(node->vertexCount * sizeof(MT5_Vertex));
		if (node->vertices == NULL)
			return false;
		memcpy(node->vertices, buffer + mesh.verticesOffset, node->vertexCount * sizeof(MT5_Vertex));
	}

	if (!ReadFaces(buffer, size, mesh.facesOffset, node)) {
		fprintf(stderr, "[MT5_Load]: Invalid faces of the mesh at 0x%04X\n", offset);
		return false;
	}
	return true;
}

// Reads a node, its children and its next siblings
static bool ReadNodes(MT5_Model* model, uint32_t offset, int32_t parent, uint32_t* capacity) {
	while (offset != 0) {
		// A file can't have more nodes than this, the limit also stops loops in the hierarchy
		if (model->nodeCount >= model->size / sizeof(MT5_NodeData) || !InBounds(model->size, offset, sizeof(MT5_NodeData))) {
			fprintf(stderr, "[MT5_Load]: Invalid node at 0x%04X\n", offset);
			return false;
		}
		MT5_NodeData data;
		memcpy(&data, model->buffer + offset, sizeof(data));

		if (model->nodeCount == *capacity) {
			uint32_t newCapacity = *capacity > 0 ? *capacity * 2 : 32;
			MT5_Node* nodes = realloc(model->nodes, newCapacity * sizeof(MT5_Node));
			if (nodes == NULL)
				return false;
			model->nodes = nodes;
			*capacity = newCapacity;
		}

		// The node list may be reallocated by the children, so the node is accessed by its index
		int32_t index = (int32_t)model->nodeCount++;
		MT5_Node* node = &model->nodes[index];
		memset(node, 0, sizeof(MT5_Node));
		node->id = data.id;
		node->name = data.name;
		for (int i = 0; i < 3; i++)
			node->rotation[i] = data.rotation[i] * 360.0f / 65536.0f;
		memcpy(node->scale, data.scale, sizeof(node->scale));
		memcpy(node->position, data.position, sizeof(node->position));
		node->parent = parent;

		if (data.meshOffset != 0 && !ReadMesh(model->buffer, model->size, data.meshOffset, node))
			return false;
		if (!ReadNodes(model, data.childOffset, index, capacity))
			return false;
		offset = data.siblingOffset;
	}
	return true;
}

// TEXD block followed by one TEXN block per texture (MT5) or a NAME block with the texture IDs (CHRM)
static bool ReadTextures(MT5_Model* model, uint32_t offset) {
	MT5_TextureHeader header;
	if (!InBounds(model->size, offset, sizeof(header)))
		return false;
	memcpy(&header, model->buffer + offset, sizeof(header));
	if (memcmp(header.id, "TEXD", 4) != 0 || header.textureCount > model->size / 8)
		return false;

	model->textureCount = header.textureCount;
	if (header.textureCount > 0) {
		model->textures = calloc(header.textureCount, sizeof(MT5_Texture));
		if (model->textures == NULL)
			return false;
	}

	uint32_t found = 0;
	size_t pos = (size_t)offset + header.size;
	while (InBounds(model->size, pos, sizeof(MT5_Block))) {
		MT5_Block block;
		memcpy(&block, model->buffer + pos, sizeof(block));
		if (block.size < sizeof(MT5_Block) || !InBounds(model->size, pos, block.size))
			break;

		const uint8_t* data = model->buffer + pos + sizeof(MT5_Block);
		uint32_t dataSize = block.size - sizeof(MT5_Block);
		if (memcmp(block.id, "TEXN", 4) == 0 && dataSize >= 8 && found < model->textureCount) {
			MT5_Texture* texture = &model->textures[found++];
			memcpy(texture->id, data, 8);
			texture->data = data + 8;
			texture->size = dataSize - 8;
		}
		else if (memcmp(block.id, "NAME", 4) == 0) {
			for (uint32_t i = 0; i + 8 <= dataSize && found < model->textureCount; i += 8)
				memcpy(model->textures[found++].id, data + i, 8);
		}
		pos += block.size;
	}

	if (found != model->textureCount)
		fprintf(stderr, "[MT5_Load]: Found %u of %u textures\n", found, model->textureCount);
	return true;
}

void MT5_Free(MT5_Model* model) {
	for (uint32_t i = 0; i < model->nodeCount; i++) {
		free(model->nodes[i].vertices);
		free(model->nodes[i].triangles);
	}
	free(model->nodes);
	free(model->textures);
	free(model->buffer);
	memset(model, 0, sizeof(MT5_Model));
}

bool MT5_Load(const char* filepath, MT5_Model* model) {
	memset(model, 0, sizeof(MT5_Model));
	model->buffer = File_LoadToMemoryDecompressed(filepath, &model->size);
	if (model->buffer == NULL)
		return false;

	MT5_Header header;
	if (model->size < sizeof(MT5_Header) || memcmp(model->buffer, "HRCM", 4) != 0) {
		fprintf(stderr, "[MT5_Load]: Invalid MT5 file %s\n", filepath);
		MT5_Free(model);
		return false;
	}
	memcpy(&header, model->buffer, sizeof(header));

	uint32_t capacity = 0;
	if (!ReadNodes(model, header.firstNodeOffset, -1, &capacity) || !ReadTextures(model, header.texturesOffset)) {
		fprintf(stderr, "[MT5_Load]: Invalid MT5 file %s\n", filepath);
		MT5_Free(model);
		return false;
	}
	return true;
}

int32_t MT5_ParentMeshNode(const MT5_Model* model, int32_t parent) {
	for (; parent >= 0; parent = model->nodes[parent].parent) {
		if (model->nodes[parent].hasMesh)
			return parent;
		for (uint32_t i = 0; i < model->nodeCount; i++) {
			if (model->nodes[i].id == model->nodes[parent].id && model->nodes[i].hasMesh)
				return (int32_t)i;
		}
	}
	return -1;
}

static uint32_t ParentVertexCount(const MT5_Model* model, int32_t parent) {
	int32_t node = MT5_ParentMeshNode(model, parent);
	return node >= 0 ? model->nodes[node].vertexCount : 0;
}

bool MT5_Print(const char* filepath) {
	printf("[MT5_Print]: Reading file: %s\n", filepath);
	MT5_Model model;
	if (!MT5_Load(filepath, &model))
		return false;

	uint32_t meshCount = 0, vertexCount = 0, triangleCount = 0;
	printf("Nodes:\n");
	for (uint32_t i = 0; i < model.nodeCount; i++) {
		const MT5_Node* node = &model.nodes[i];
		int depth = 0;
		for (int32_t parent = node->parent; parent >= 0; parent = model.nodes[parent].parent)
			depth++;

		printf("%*sNode 0x%08X", 2 + depth * 2, "", node->id);
		const char* name = (const char*)&node->name;
		if (node->name != 0 && isprint((unsigned char)name[0]) && isprint((unsigned char)name[1]) && isprint((unsigned char)name[2]) && isprint((unsigned char)name[3]))
			printf(" '%.4s'", name);
		printf(": position (%g, %g, %g), rotation (%g, %g, %g), scale (%g, %g, %g)",
			node->position[0], node->position[1], node->position[2],
			node->rotation[0], node->rotation[1], node->rotation[2],
			node->scale[0], node->scale[1], node->scale[2]);

		if (node->hasMesh) {
			// Count the vertex indices that don't point to a vertex of this mesh or of the parent mesh
			uint32_t parentVertexCount = ParentVertexCount(&model, node->parent);
			uint32_t invalid = 0;
			for (uint32_t t = 0; t < node->triangleCount; t++) {
				for (int c = 0; c < 3; c++) {
					int32_t vertex = node->triangles[t].corners[c].vertex;
					if (vertex >= (int32_t)node->vertexCount || (vertex < 0 && (uint32_t)-vertex > parentVertexCount))
						invalid++;
				}
			}

			printf(", mesh: polyType %u, %u vertices, %u triangles", node->polyType, node->vertexCount, node->triangleCount);
			if (invalid > 0)
				printf(" (%u unresolved vertex indices)", invalid);
			meshCount++;
			vertexCount += node->vertexCount;
			triangleCount += node->triangleCount;
		}
		printf("\n");
	}

	printf("Textures:\n");
	for (uint32_t i = 0; i < model.textureCount; i++) {
		const MT5_Texture* texture = &model.textures[i];
		printf("  Texture %u: ", i);
		for (int j = 0; j < 8; j++)
			printf("%02X", texture->id[j]);
		if (texture->data != NULL) {
			const char* ext = File_ExtensionFromMagic(texture->data, texture->size);
			printf(", %s, 0x%04X bytes\n", ext != NULL ? ext : "unknown format", texture->size);
		}
		else
			printf(", external\n");
	}

	printf("[MT5_Print]: %u nodes, %u meshes, %u vertices, %u triangles, %u textures\n",
		model.nodeCount, meshCount, vertexCount, triangleCount, model.textureCount);
	MT5_Free(&model);
	return true;
}
