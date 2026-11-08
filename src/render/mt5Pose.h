#ifndef __MT5POSE_H__
#define __MT5POSE_H__

#include <stdbool.h>
#include <stdint.h>
#include <raylib.h>
#include "formats/animation/motn.h"
#include "render/mt5Mesh.h"

// Poses of the bodies of the characters (MT5/CHRM models) from the keyframe motions of the MOTN files. The bones of the
// motions are not the nodes of the models: the motions give the position and the rotation of the hips (bone 0), the
// rotations of the pelvis, the torso, the head, the shoulders, the hands and the feet, the positions the head, the
// hands and the feet reach (inverse kinematics), and a point the head looks at. The upper arms and the thighs have a
// rotation that orients the limb before it is bent to reach its target: the elbows and the knees bend in the plane of
// that rotation. The positions are in the space of the character, with the floor at y = 0

// Nodes of a body that the motions move, -1 if the model doesn't have it
typedef struct {
    int32_t     torso;      // 0x01, the root of the body, with the arms and the head
    int32_t     pelvis;     // 0x0E, with the legs
    int32_t     head;       // 0xFFBD
    int32_t     legs[2][3]; // Thigh, shin and foot: 0x10-0x12 (+Z side of the pelvis), 0x15-0x17
    int32_t     arms[2][4]; // Shoulder, upper arm, forearm and hand: 0x09-0x0B and 0xFFBE (-Z side), 0x04-0x06 and 0xFFBF.
                            // The hand is the child of the forearm: 0xC and 7 in the bodies that have them
} MT5Pose_Body;

// Finds the nodes of a body. Returns false if the model is not the body of a character
bool MT5Pose_FindBody(const MT5Mesh* mesh, MT5Pose_Body* body);
// Whether the motion can pose a body: a keyframe or raw motion of a person (not the animals)
bool MT5Pose_CanPose(const MOTN_Motion* motion);
// World matrix of each node of the body (mesh->nodeCount of them) at a frame of the motion. Returns false if the motion
// can't pose it
bool MT5Pose_Compute(const MT5Mesh* mesh, const MT5Pose_Body* body, const MOTN_Motion* motion, float frame, Matrix* world);
// Whether a sampled motion can pose the nodes of a model (objects and animals): it animates the first nodes of the
// model, in the order of the file, so the model has at least as many nodes as the motion
bool MT5Pose_CanPoseNodes(const MT5Mesh* mesh, const MOTN_Motion* motion);
// World matrix of each node of the model at a frame of a sampled motion. Returns false if the motion can't pose it
bool MT5Pose_ComputeNodes(const MT5Mesh* mesh, const MOTN_Motion* motion, float frame, Matrix* world);

#endif
