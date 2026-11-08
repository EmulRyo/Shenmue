#ifndef __MT5EXPORT_H__
#define __MT5EXPORT_H__

#include <stdbool.h>
#include <stdint.h>
#include "formats/animation/motn.h"
#include "render/mt5Pose.h"
#include "render/mt5Render.h"

// Export of the MT5/CHRM models shown in the views to formats that other 3D programs open. The triangles are exported
// in model space and in the pose of the file (not in the pose of a motion), only those of the animation frame (and those
// that are always shown), with their vertex colors and their textures. The identical vertices of each group of triangles
// are shared. The textures are read back from the GPU, so it needs the window open

// Motions of a bank to export as animations of the body of a character
typedef struct {
    const MOTN_Bank*    bank;
    const MT5Pose_Body* body;
} MT5Export_Motions;

// Saves the model in a format given by the extension of filepath:
// - glb: binary glTF 2.0 with the textures embedded as PNG. One material per group of triangles, with the alpha mode of
//   its list (opaque, mask for the punch-through triangles; for the translucent ones, blend only if their texture or
//   their vertex colors have partial alpha, otherwise mask or opaque) and the wrapping of its texture coordinates
//   (repeat, mirrored repeat or clamp). The models of several nodes have a skeleton: a root joint with one joint per node
//   under it, in the hierarchy and the pose of the file, and every vertex is moved by the joint of its node alone. With
//   motions, each motion of the bank that can pose the body is an animation with its name, sampled at every frame
//   (MOTN_FRAME_RATE) with the joints that it moves from the pose of the file
// - obj: Wavefront OBJ with the vertex colors after the positions, an MTL file with the same name next to it and the
//   textures as PNG files named <name>_<texture index>.png. The mirrored repeat can't be written in the MTL file, and
//   it has no skeleton
// motions can be NULL. Returns false if the format is unknown or on error
bool MT5Export_Save(const MT5Render_Model* model, uint32_t frame, const MT5Export_Motions* motions, const char* filepath);

#endif
