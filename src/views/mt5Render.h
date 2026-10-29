#ifndef __MT5RENDER_H__
#define __MT5RENDER_H__

#include <stdbool.h>
#include <stdint.h>
#include <raylib.h>
#include "views/mt5Mesh.h"

// Drawing of MT5/CHRM models with their textures, for the views that show models. The textures of CHRM files are
// searched in the folder of the model, where the PKF of the scene is extracted (tex_<index>_<texture ID>.pvr files)

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

void MT5Render_Init(MT5Render* render);
void MT5Render_Free(MT5Render* render);

// Loads a model file with its textures and uploads its meshes. Returns false if the file is not a valid model
bool MT5Render_LoadModel(const char* filepath, MT5Render_Model* model);
void MT5Render_FreeModel(MT5Render_Model* model);

// Lights the next models from the camera
void MT5Render_SetCamera(MT5Render* render, Camera3D camera);
// Opaque and punch-through triangles, and the opaque pixels of the translucent ones. The frame is the animation frame
// of animated models
void MT5Render_DrawOpaque(MT5Render* render, const MT5Render_Model* model, Matrix transform, uint32_t frame);
// The rest of the pixels of the translucent triangles, from back to front and without writing the depth. It has to be
// called after drawing the opaque part of all the models, from the farthest model to the nearest one
void MT5Render_DrawTranslucent(MT5Render* render, MT5Render_Model* model, Matrix transform, uint32_t frame, Camera3D camera);

#endif
