#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include "formats/animation/motn.h"
#include "render/mt5Mesh.h"
#include "render/mt5Pose.h"

// Bones of the motions that pose a body. The motions of most characters use one numbering; those of a few (types 9,
// 15 and 19) number the bones after the first leg one or two lower
typedef struct {
    uint8_t     pelvis;
    uint8_t     torso;
    uint8_t     headTarget; // Position of the head: the torso turns to reach it
    uint8_t     head;
    uint8_t     lookTarget; // A point in front of the face: the head turns to look at it
    uint8_t     legs[2][3]; // Rotation of the thigh, position of the foot, rotation of the foot
    uint8_t     arms[2][4]; // Rotation of the shoulder and of the upper arm, position of the hand, rotation of the hand
    uint8_t     last;       // Highest bone, to recognize the numbering
} BoneSet;

static const BoneSet g_human = { 1, 18, 20, 21, 23, { { 5, 8, 9 }, { 12, 15, 16 } }, { { 25, 26, 29, 30 }, { 31, 32, 35, 36 } }, 36 };
static const BoneSet g_other = { 1, 16, 18, 19, 21, { { 5, 8, 9 }, { 11, 14, 15 } }, { { 23, 24, 27, 28 }, { 29, 30, 33, 34 } }, 34 };

#define MT5POSE_BONE_ROOT 0

// The motions of a few character types (4: GAK, JAK; 5: GEN, GIW; 7: JIJ) have the bones of the two arms swapped: the
// first arm of the set is the one of the +Z side, on the same side as the first leg
static bool SwappedArms(const MOTN_Motion* motion) {
    return motion->type == 4 || motion->type == 5 || motion->type == 7;
}

// Direction of the bend of the knees and the elbows in the space of the thigh and the upper arm: the knees bend to the
// front, the elbows to the back
static const Vector3 g_kneeAxis = { 0.0f, 1.0f, 0.0f };
static const Vector3 g_elbowAxis = { 0.0f, -1.0f, 0.0f };

// Rotation of the torso (bone 18) in the rest of the motions, in turns: the X axis up
static const Vector3 g_torsoRest = { 0.25f, 0.0f, 0.25f };

// a * b with column vectors: b is applied first
static Matrix Mul(Matrix a, Matrix b) {
    return MatrixMultiply(b, a);
}

static Vector3 Origin(Matrix m) {
    return (Vector3) { m.m12, m.m13, m.m14 };
}

static Matrix RotationOf(Matrix m) {
    m.m12 = m.m13 = m.m14 = 0.0f;
    return m;
}

// Translation and rotation
static Matrix Place(Vector3 position, Matrix rotation) {
    rotation.m12 = position.x;
    rotation.m13 = position.y;
    rotation.m14 = position.z;
    return rotation;
}

static Vector3 Rotate(Matrix rotation, Vector3 v) {
    return Vector3Transform(v, RotationOf(rotation));
}

// Rotation in turns around X, then Y, then Z, as the nodes of the models
static Matrix Euler(Vector3 turns) {
    float k = 2.0f * PI;
    return Mul(Mul(MatrixRotateZ(turns.z * k), MatrixRotateY(turns.y * k)), MatrixRotateX(turns.x * k));
}

// Smallest rotation that turns the direction from to the direction to
static Matrix Swing(Vector3 from, Vector3 to) {
    from = Vector3Normalize(from);
    to = Vector3Normalize(to);
    float dot = Vector3DotProduct(from, to);
    if (dot > 0.99999f || Vector3Length(from) == 0.0f || Vector3Length(to) == 0.0f)
        return MatrixIdentity();
    if (dot < -0.99999f) {
        Vector3 axis = Vector3CrossProduct(from, fabsf(from.x) < 0.9f ? (Vector3) { 1, 0, 0 } : (Vector3) { 0, 1, 0 });
        return MatrixRotate(Vector3Normalize(axis), PI);
    }
    return QuaternionToMatrix(QuaternionFromVector3ToVector3(from, to));
}

// Local matrix of a node with another rotation: scale, rotation, translation
static Matrix LocalWith(const MT5Mesh_Node* node, Matrix rotation) {
    return Place(node->position, Mul(rotation, MatrixScale(node->scale.x, node->scale.y, node->scale.z)));
}

static Matrix LocalRotation(const MT5Mesh_Node* node) {
    return RotationOf(MT5Mesh_NodeMatrix(&(MT5Mesh_Node) { -1, 0, { 0 }, node->rotation, { 1.0f, 1.0f, 1.0f } }));
}

// First node with the given ID (lowest 16 bits), whose parent has the given ID if parentID isn't UINT32_MAX, -1 if none
static int32_t FindNode(const MT5Mesh* mesh, uint32_t id, uint32_t parentID) {
    for (uint32_t i = 0; i < mesh->nodeCount; i++) {
        int32_t parent = mesh->nodes[i].parent;
        if ((mesh->nodes[i].id & 0xFFFF) == id &&
            (parentID == UINT32_MAX || (parent >= 0 && (mesh->nodes[parent].id & 0xFFFF) == parentID)))
            return (int32_t)i;
    }
    return -1;
}

// The first of two IDs that the model has
static int32_t FindEither(const MT5Mesh* mesh, uint32_t id, uint32_t other, uint32_t parentID) {
    int32_t node = FindNode(mesh, id, UINT32_MAX);
    return node >= 0 ? node : FindNode(mesh, other, parentID);
}

static bool IsChild(const MT5Mesh* mesh, int32_t child, int32_t parent) {
    return child >= 0 && parent >= 0 && mesh->nodes[child].parent == parent;
}

bool MT5Pose_FindBody(const MT5Mesh* mesh, MT5Pose_Body* body) {
    body->torso = FindNode(mesh, 0x01, UINT32_MAX);
    body->pelvis = FindNode(mesh, 0x0E, 0x01);
    // Some old models (e.g. KOB_L.chrm) have the head 3 under the torso, and the hands 7 and 0xC
    body->head = FindEither(mesh, 0xFFBD, 0x03, 0x01);
    const uint32_t legs[2][3] = { { 0x10, 0x11, 0x12 }, { 0x15, 0x16, 0x17 } };
    const uint32_t arms[2][4] = { { 0x09, 0x0A, 0x0B, 0xFFBE }, { 0x04, 0x05, 0x06, 0xFFBF } };
    const uint32_t oldHands[2] = { 0x0C, 0x07 };
    bool found = body->torso >= 0 && body->pelvis >= 0 && body->head >= 0;
    for (int side = 0; side < 2; side++) {
        for (int i = 0; i < 3; i++) {
            body->legs[side][i] = FindNode(mesh, legs[side][i], i == 0 ? 0x0E : legs[side][i - 1]);
            found = found && body->legs[side][i] >= 0;
        }
        for (int i = 0; i < 4; i++) {
            body->arms[side][i] = i < 3 ? FindNode(mesh, arms[side][i], i == 0 ? 0x01 : arms[side][i - 1]) :
                FindEither(mesh, arms[side][i], oldHands[side], arms[side][2]);
            found = found && body->arms[side][i] >= 0;
        }
        // The IK takes the thigh, the shin and the foot as a chain, also the upper arm, the forearm and the hand
        found = found && IsChild(mesh, body->legs[side][1], body->legs[side][0]) && IsChild(mesh, body->legs[side][2], body->legs[side][1]) &&
            IsChild(mesh, body->arms[side][2], body->arms[side][1]) && IsChild(mesh, body->arms[side][3], body->arms[side][2]);
    }
    return found;
}

static const BoneSet* FindBoneSet(const MOTN_Motion* motion) {
    if (motion->kind != MOTN_KEYFRAME)
        return NULL;
    uint32_t last = 0;
    for (uint32_t i = 0; i < motion->channelCount; i++) {
        if (motion->channels[i].bone > last)
            last = motion->channels[i].bone;
    }
    // The animals (types 18 and 20) have more bones
    return last == g_human.last ? &g_human : last == g_other.last ? &g_other : NULL;
}

bool MT5Pose_CanPose(const MOTN_Motion* motion) {
    return FindBoneSet(motion) != NULL;
}

// Position (or rotation) of a bone at a frame. found is set if the motion has a channel of it
static Vector3 BoneValue(const MOTN_Motion* motion, uint32_t bone, bool position, float frame, bool* found) {
    float values[3] = { 0 };
    bool any = false;
    for (int axis = 0; axis < 3; axis++) {
        const MOTN_Channel* channel = MOTN_FindChannel(motion, bone, (MOTN_Axis)(axis + (position ? MOTN_POSITION_X : MOTN_ROTATION_X)));
        if (channel != NULL) {
            values[axis] = MOTN_ChannelValue(channel, frame);
            any = true;
        }
    }
    if (found != NULL)
        *found = any;
    return (Vector3) { values[0], values[1], values[2] };
}

// Rotation of a bone, or the rotation of the node in the file if the motion doesn't have it
static Matrix BoneRotation(const MOTN_Motion* motion, uint32_t bone, float frame, const MT5Mesh_Node* node) {
    bool found;
    Vector3 turns = BoneValue(motion, bone, false, frame, &found);
    return found ? Euler(turns) : LocalRotation(node);
}

// Bends a limb of two segments (upper, mid and end nodes, each one the child of the previous one) so that the end
// reaches the target, or gets as close as it can. The upper node is at its place with its rotation before the bend, which
// is turned to point the limb to the target: the mid one bends towards the given axis of the turned upper one (e.g. a
// knee bends to the front of the thigh, also when the leg is raised to kick). Sets the world matrices of the upper and mid
// nodes
static void SolveLimb(const MT5Mesh* mesh, Matrix* world, int32_t upper, int32_t mid, int32_t end, Vector3 target, Vector3 bendAxis) {
    Vector3 joint = Origin(world[upper]);
    Matrix rotation = RotationOf(world[upper]);
    Vector3 midLocal = mesh->nodes[mid].position;
    Vector3 endLocal = mesh->nodes[end].position;
    float a = Vector3Length(midLocal), b = Vector3Length(endLocal);
    if (a <= 0.0f || b <= 0.0f)
        return;

    Vector3 toTarget = Vector3Subtract(target, joint);
    float distance = Vector3Length(toTarget);
    Vector3 direction = distance > 1e-5f ? Vector3Scale(toTarget, 1.0f / distance) : Vector3Normalize(Rotate(rotation, midLocal));
    distance = Clamp(distance, fabsf(a - b) + 1e-4f, a + b - 1e-4f);
    // The bend goes to the axis, without its part along the direction to the target
    Vector3 bend = Rotate(Mul(Swing(Rotate(rotation, midLocal), direction), rotation), bendAxis);
    bend = Vector3Subtract(bend, Vector3Scale(direction, Vector3DotProduct(bend, direction)));
    if (Vector3Length(bend) < 1e-4f)
        bend = Vector3Perpendicular(direction);
    bend = Vector3Normalize(bend);
    float along = (a * a - b * b + distance * distance) / (2.0f * distance);
    float height = sqrtf(fmaxf(a * a - along * along, 0.0f));
    Vector3 knee = Vector3Add(joint, Vector3Add(Vector3Scale(direction, along), Vector3Scale(bend, height)));

    rotation = Mul(Swing(Rotate(rotation, midLocal), Vector3Subtract(knee, joint)), rotation);
    world[upper] = Place(joint, Mul(rotation, MatrixScale(mesh->nodes[upper].scale.x, mesh->nodes[upper].scale.y, mesh->nodes[upper].scale.z)));
    Vector3 midPosition = Vector3Add(joint, Rotate(rotation, midLocal));
    Matrix midRotation = Mul(rotation, LocalRotation(&mesh->nodes[mid]));
    Vector3 reached = Vector3Add(joint, Vector3Scale(direction, distance));
    midRotation = Mul(Swing(Rotate(midRotation, endLocal), Vector3Subtract(reached, midPosition)), midRotation);
    world[mid] = Place(midPosition, midRotation);
}

bool MT5Pose_Compute(const MT5Mesh* mesh, const MT5Pose_Body* body, const MOTN_Motion* motion, float frame, Matrix* world) {
    const BoneSet* bones = FindBoneSet(motion);
    if (bones == NULL)
        return false;
    bool* done = calloc(mesh->nodeCount + 1, sizeof(bool));
    Matrix* bind = malloc((mesh->nodeCount + 1) * sizeof(Matrix));
    if (done == NULL || bind == NULL) {
        free(done);
        free(bind);
        return false;
    }
    // The pose of the file, without the changes of the meshes for the view (e.g. the arms opened), in the space of the
    // parent of the torso: the motions place the torso, and the root of a few models is turned (e.g. 180 degrees around
    // Y in OGM_L.chrm)
    for (uint32_t i = 0; i < mesh->nodeCount; i++) {
        int32_t parent = mesh->nodes[i].parent;
        Matrix local = MT5Mesh_NodeMatrix(&mesh->nodes[i]);
        bind[i] = parent >= 0 ? Mul(bind[parent], local) : local;
    }
    int32_t base = mesh->nodes[body->torso].parent;
    if (base >= 0) {
        Matrix inverse = MatrixInvert(bind[base]);
        for (uint32_t i = 0; i < mesh->nodeCount; i++)
            bind[i] = Mul(inverse, bind[i]);
    }
    // The torso of the motions rests at (90, 0, 90) degrees, as in most models. A few have another rest, the whole body
    // turned (e.g. (270, 0, 90) in BAT_L.chrm, turned 180 degrees around Y): the turn is removed as the one of the root
    Matrix turn = Mul(RotationOf(bind[body->torso]), MatrixTranspose(Euler(g_torsoRest)));
    for (uint32_t i = 0; i < mesh->nodeCount; i++)
        bind[i] = Mul(MatrixTranspose(turn), bind[i]);

    Vector3 hips = BoneValue(motion, MT5POSE_BONE_ROOT, true, frame, NULL);
    Matrix root = Euler(BoneValue(motion, MT5POSE_BONE_ROOT, false, frame, NULL));
    bool swappedArms = SwappedArms(motion);
    for (uint32_t i = 0; i < mesh->nodeCount; i++) {
        if (done[i])
            continue;
        const MT5Mesh_Node* node = &mesh->nodes[i];
        int32_t parent = node->parent;
        Matrix parentWorld = parent >= 0 ? world[parent] : MatrixIdentity();
        int32_t index = (int32_t)i;
        world[i] = Mul(parentWorld, MT5Mesh_NodeMatrix(node));

        if (index == body->torso) {
            // At the hips, turned so that the head reaches its position
            Matrix rotation = Mul(root, BoneRotation(motion, bones->torso, frame, node));
            bool found;
            Vector3 headTarget = BoneValue(motion, bones->headTarget, true, frame, &found);
            Vector3 head = Vector3Transform(Origin(bind[body->head]), MatrixInvert(bind[i]));
            if (found)
                rotation = Mul(Swing(Rotate(rotation, head), Vector3Subtract(headTarget, hips)), rotation);
            world[i] = Place(hips, rotation);
        }
        else if (index == body->pelvis) {
            // The rotation of the pelvis of the motions is a quarter turn around Y in the pose of the file
            Matrix rest = Mul(MatrixRotateY(-PI / 2.0f), RotationOf(bind[i]));
            Matrix rotation = Mul(Mul(root, BoneRotation(motion, bones->pelvis, frame, node)), rest);
            world[i] = Place(Origin(world[i]), rotation);
        }
        else if (index == body->head) {
            // The face looks to the X axis of the head
            world[i] = Mul(parentWorld, LocalWith(node, BoneRotation(motion, bones->head, frame, node)));
            bool found;
            Vector3 lookTarget = BoneValue(motion, bones->lookTarget, true, frame, &found);
            Vector3 head = Origin(world[i]);
            if (found) {
                Matrix turn = Swing(Rotate(world[i], (Vector3) { 1.0f, 0.0f, 0.0f }), Vector3Subtract(lookTarget, head));
                world[i] = Place(head, Mul(turn, RotationOf(world[i])));
            }
        }

        for (int side = 0; side < 2; side++) {
            const int32_t* leg = body->legs[side];
            const int32_t* arm = body->arms[side];
            const uint8_t* armBones = bones->arms[swappedArms ? 1 - side : side];
            if (index == leg[0]) {
                world[i] = Mul(parentWorld, LocalWith(node, BoneRotation(motion, bones->legs[side][0], frame, node)));
                bool found;
                Vector3 target = BoneValue(motion, bones->legs[side][1], true, frame, &found);
                if (found)
                    SolveLimb(mesh, world, leg[0], leg[1], leg[2], target, g_kneeAxis);
                else
                    world[leg[1]] = Mul(world[i], MT5Mesh_NodeMatrix(&mesh->nodes[leg[1]]));
                // The rotation of the foot is in the space of the character, from the pose of the file
                Matrix rotation = Mul(Euler(BoneValue(motion, bones->legs[side][2], false, frame, NULL)), RotationOf(bind[leg[2]]));
                world[leg[2]] = Place(Vector3Transform(mesh->nodes[leg[2]].position, world[leg[1]]), rotation);
                done[leg[1]] = done[leg[2]] = true;
            }
            else if (index == arm[0])
                world[i] = Mul(parentWorld, LocalWith(node, BoneRotation(motion, armBones[0], frame, node)));
            else if (index == arm[1]) {
                world[i] = Mul(parentWorld, LocalWith(node, BoneRotation(motion, armBones[1], frame, node)));
                bool found;
                Vector3 target = BoneValue(motion, armBones[2], true, frame, &found);
                if (found)
                    SolveLimb(mesh, world, arm[1], arm[2], arm[3], target, g_elbowAxis);
                else
                    world[arm[2]] = Mul(world[i], MT5Mesh_NodeMatrix(&mesh->nodes[arm[2]]));
                // The rotation of the hand is relative to the forearm
                const MT5Mesh_Node* hand = &mesh->nodes[arm[3]];
                world[arm[3]] = Mul(world[arm[2]], LocalWith(hand, BoneRotation(motion, armBones[3], frame, hand)));
                done[arm[2]] = done[arm[3]] = true;
            }
        }
    }
    free(done);
    free(bind);
    return true;
}
