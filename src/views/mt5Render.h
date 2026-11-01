#ifndef __MT5RENDER_H__
#define __MT5RENDER_H__

#include <stdbool.h>
#include <stdint.h>
#include <raylib.h>
#include "formats/archive/ipac.h"
#include "views/mt5Mesh.h"

// Drawing of MT5/CHRM models with their textures, for the views that show models. The textures of CHRM files are
// searched in the folder of the model, where the PKF of the scene is extracted (tex_<index>_<texture ID>.pvr files),
// or in the files of the PKF loaded in memory

// Shader and material shared by the models
typedef struct {
    Shader      shader;
    int         lightDirLoc;
    int         modeLoc;
    Material    material;
} MT5Render;

typedef struct {
    uint32_t    nodeCount;
    uint32_t    textureCount;
    uint32_t    loadedTextures;
    Texture*    textures; // id 0 if the texture couldn't be loaded
    MT5Mesh     mesh;
    bool        sorted;   // The translucent triangles are sorted for the camera at sortedEye, sortedTarget (model space)
    Vector3     sortedEye;
    Vector3     sortedTarget;
} MT5Render_Model;

// A model placed in a scene
typedef struct {
    MT5Render_Model*    model;
    Matrix              transform;
    Vector3             center;   // Of the bounding sphere of the model in the scene, to sort the instances
    uint32_t            frame;    // Animation frame
} MT5Render_Instance;

void MT5Render_Init(MT5Render* render);
void MT5Render_Free(MT5Render* render);

// Loads a model file with its textures and uploads its meshes. Returns false if the file is not a valid model
bool MT5Render_LoadModel(const char* filepath, MT5Render_Model* model);
// Same with a file of a PKF/PKS file loaded in memory, whose textures are searched in the files of that PKF
bool MT5Render_LoadPackedModel(const IPAC_File* file, const IPAC_FileList* files, MT5Render_Model* model);
void MT5Render_FreeModel(MT5Render_Model* model);
// Copies the vertices of the triangles of the model, placed with transform, to points (mesh.triangleCount * 3 of them).
// Returns the number of vertices
uint32_t MT5Render_Vertices(const MT5Render_Model* model, Matrix transform, Vector3* points);
// The vertices of the triangles of the instances, in a new array with room for extra more points. Returns NULL on
// memory error
Vector3* MT5Render_InstanceVertices(const MT5Render_Instance* instances, uint32_t count, uint32_t extra, uint32_t* vertexCount);

// Lights the next models from the camera
void MT5Render_SetCamera(MT5Render* render, Camera3D camera);
// Opaque and punch-through triangles, and the opaque pixels of the translucent ones. The frame is the animation frame
// of animated models
void MT5Render_DrawOpaque(MT5Render* render, const MT5Render_Model* model, Matrix transform, uint32_t frame);
// The rest of the pixels of the translucent triangles, from back to front and without writing the depth. It has to be
// called after drawing the opaque part of all the models, from the farthest model to the nearest one
void MT5Render_DrawTranslucent(MT5Render* render, MT5Render_Model* model, Matrix transform, uint32_t frame, Camera3D camera);
// Lights the instances from the camera and draws the opaque part of all of them, then the translucent part from the
// farthest instance to the nearest one
void MT5Render_DrawInstances(MT5Render* render, const MT5Render_Instance* instances, uint32_t count, Camera3D camera);

#endif
