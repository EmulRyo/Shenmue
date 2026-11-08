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
    uint8_t     positions[7]; // The bones with a position: the hips and the targets, to recognize the numbering
    // Bend of the knees and the elbows (one rotation around Z), in the raw motions, which have no targets
    uint8_t     knees[2];
    uint8_t     elbows[2];
} BoneSet;

static const BoneSet g_human = { 1, 18, 20, 21, 23, { { 5, 8, 9 }, { 12, 15, 16 } }, { { 25, 26, 29, 30 }, { 31, 32, 35, 36 } }, 36,
    { 0, 8, 15, 20, 23, 29, 35 }, { 7, 14 }, { 28, 34 } };
static const BoneSet g_other = { 1, 16, 18, 19, 21, { { 5, 8, 9 }, { 11, 14, 15 } }, { { 23, 24, 27, 28 }, { 29, 30, 33, 34 } }, 34,
    { 0, 8, 14, 18, 21, 27, 33 }, { 7, 13 }, { 26, 32 } };

#define MT5POSE_BONE_ROOT 0
// Largest turns of the head from its pose on the torso: the tilt of the top of the head to the front or the back, the
// tilt to the sides, and the whole turn, mostly around the top when the head looks to a side. In the motions of Ryo the
// head tilts under 60 degrees to the front or the back (e.g. 59.6 sitting on a chair in AKI_AKI_SIT_CHAIR), mostly under
// 40 to the sides, and turns under 90 (81 looking back in AKI_AKI_KIDUKU_BACK_LP), except in a few frames of fast motions
// (fights, falls) where the rigid torso leans too much
#define MT5POSE_NECK_NOD    (60.0f * DEG2RAD)
#define MT5POSE_NECK_SIDE   (40.0f * DEG2RAD)
#define MT5POSE_NECK_TURN   (90.0f * DEG2RAD)

// The motions of a few character types (4: GAK, JAK; 5: GEN, GIW; 7: JIJ; 19: PNW) have the bones of the two arms
// swapped: the first arm of the set is the one of the +Z side, on the same side as the first leg
static bool SwappedArms(const MOTN_Motion* motion) {
    return motion->type == 4 || motion->type == 5 || motion->type == 7 || motion->type == 19;
}

// Direction of the bend of the knees and the elbows in the space of the thigh and the upper arm: the knees bend to the
// front, the elbows to the back
static const Vector3 g_kneeAxis = { 0.0f, 1.0f, 0.0f };
static const Vector3 g_elbowAxis = { 0.0f, -1.0f, 0.0f };

// Rotation of the torso (bone 18) at rest, in turns, as the node 0x01 of most models: the X axis up. Most motions of Ryo
// have (0, 0.25, 0) instead, the X axis to the front, with the same Z axis (the side): the torso is then turned to the
// position of the head (see Aim)
static const Vector3 g_torsoRest = { 0.25f, 0.0f, 0.25f };
// Rotation of the pelvis (bone 1) in the rest of the motions, in turns: the pelvis as in the models
static const Vector3 g_pelvisRest = { 0.0f, 0.25f, 0.0f };

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

// Rotation whose X axis is x and whose Y axis is y made perpendicular to x
static Matrix Basis(Vector3 x, Vector3 y) {
    x = Vector3Normalize(x);
    y = Vector3Normalize(Vector3Subtract(y, Vector3Scale(x, Vector3DotProduct(x, y))));
    Vector3 z = Vector3CrossProduct(x, y);
    Matrix m = MatrixIdentity();
    m.m0 = x.x; m.m1 = x.y; m.m2 = x.z;
    m.m4 = y.x; m.m5 = y.y; m.m6 = y.z;
    m.m8 = z.x; m.m9 = z.y; m.m10 = z.z;
    return m;
}

// Turn that points an axis of a rotation (given in its own space) to a direction, keeping another one, perpendicular to
// it, as close as it can. Used for the torso and the head, whose rotations in the motions use several conventions a
// quarter or half turn apart around their long axis, all with the same Z axis (the side of the torso, the top of the
// head): the torso is (0, 90, 0) degrees in most motions of Ryo but (90, 0, 90) in others, and the face of the head
// is to the front, to a side or to the back (e.g. (90, 0, 90) instead of the usual (-90, 0, -90) in AKI_AKI_PORI_ST/LP/EN).
// The smallest turn to the direction, which can be half a turn around any axis, would twist the torso or leave the head
// upside down
static Matrix Aim(Matrix rotation, Vector3 aimLocal, Vector3 keepLocal, Vector3 direction) {
    Vector3 aim = Rotate(rotation, aimLocal);
    Vector3 keep = Rotate(rotation, keepLocal);
    Vector3 side = Vector3CrossProduct(keep, direction);
    if (Vector3Length(direction) < 1e-5f || Vector3Length(side) < 1e-4f * Vector3Length(keep) * Vector3Length(direction))
        return Swing(aim, direction);
    return Mul(Basis(direction, keep), MatrixTranspose(Basis(aim, keep)));
}

// Turn that points the X axis of a head (the face) to a direction, keeping its Z axis (the top of the head)
static Matrix LookAt(Matrix head, Vector3 direction) {
    return Aim(head, (Vector3) { 1.0f, 0.0f, 0.0f }, (Vector3) { 0.0f, 0.0f, 1.0f }, direction);
}

// Turn that brings the top of a head (Z axis) back within the tilts the neck allows from the head at rest: to the front
// or the back (towards its X axis, the face), and to the sides (towards its Y axis). The tilt keeps its direction, and
// the limit goes smoothly from one to the other (an ellipse), so that the head doesn't jump between frames
static Matrix LimitTilt(Matrix head, Matrix rest) {
    Vector3 top = Vector3Normalize(Rotate(head, (Vector3) { 0.0f, 0.0f, 1.0f }));
    Vector3 axes[3];
    for (int i = 0; i < 3; i++)
        axes[i] = Vector3Normalize(Rotate(rest, (Vector3) { i == 0 ? 1.0f : 0.0f, i == 1 ? 1.0f : 0.0f, i == 2 ? 1.0f : 0.0f }));
    float x = Vector3DotProduct(top, axes[0]), y = Vector3DotProduct(top, axes[1]), z = Vector3DotProduct(top, axes[2]);
    float across = sqrtf(x * x + y * y);
    if (across < 1e-5f)
        return MatrixIdentity();
    float tilt = atan2f(across, z);
    float dx = x / across, dy = y / across;
    float limit = 1.0f / sqrtf((dx / MT5POSE_NECK_NOD) * (dx / MT5POSE_NECK_NOD) + (dy / MT5POSE_NECK_SIDE) * (dy / MT5POSE_NECK_SIDE));
    if (tilt <= limit)
        return MatrixIdentity();
    Vector3 direction = Vector3Add(Vector3Scale(axes[0], dx), Vector3Scale(axes[1], dy));
    return Swing(top, Vector3Add(Vector3Scale(axes[2], cosf(limit)), Vector3Scale(direction, sinf(limit))));
}

// The same rotation, by at most the given angle (radians)
static Matrix LimitTurn(Matrix turn, float limit) {
    Quaternion q = QuaternionNormalize(QuaternionFromMatrix(turn));
    if (q.w < 0.0f)
        q = QuaternionScale(q, -1.0f);
    float angle = 2.0f * acosf(Clamp(q.w, -1.0f, 1.0f));
    if (angle <= limit)
        return RotationOf(turn);
    return QuaternionToMatrix(QuaternionSlerp(QuaternionIdentity(), q, limit / angle));
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

static bool IsPositionBone(const BoneSet* set, uint32_t bone) {
    for (size_t i = 0; i < sizeof(set->positions); i++) {
        if (set->positions[i] == bone)
            return true;
    }
    return false;
}

// Whether every channel of the motion is in the set: the positions are those of its targets, and the bones are not past
// its last one
static bool FitsBoneSet(const MOTN_Motion* motion, const BoneSet* set) {
    for (uint32_t i = 0; i < motion->channelCount; i++) {
        const MOTN_Channel* channel = &motion->channels[i];
        bool position = channel->axis <= MOTN_POSITION_Z;
        if (channel->bone > set->last || (position != IsPositionBone(set, channel->bone) && channel->bone != MT5POSE_BONE_ROOT))
            return false;
    }
    return motion->channelCount > 0;
}

// Some motions leave out bones: the rotations of the hands (e.g. AKI_AKI_L_STAND), or every bone but those of an arm or
// of the upper body (e.g. AKI_UDE_AISATU), so the set is recognized by the bones the motion has. The animals (types 16
// to 18 and 20) have other bones and fit none
static const BoneSet* FindBoneSet(const MOTN_Motion* motion) {
    if (motion->kind == MOTN_SAMPLED)
        return NULL;
    // The bones of a part of the body can be in both sets: the character types of the second one try it first
    bool other = motion->type == 9 || motion->type == 15 || motion->type == 19;
    const BoneSet* sets[2] = { other ? &g_other : &g_human, other ? &g_human : &g_other };
    for (int i = 0; i < 2; i++) {
        if (FitsBoneSet(motion, sets[i]))
            return sets[i];
    }
    return NULL;
}

// Number of nodes of a sampled motion, 0 if it is not one or it is not decoded
static uint32_t SampledNodes(const MOTN_Motion* motion) {
    if (motion->kind != MOTN_SAMPLED)
        return 0;
    uint32_t nodes = 0;
    for (uint32_t i = 0; i < motion->channelCount; i++) {
        if (motion->channels[i].bone + 1u > nodes)
            nodes = motion->channels[i].bone + 1u;
    }
    return nodes;
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

// Rotation of a bone, or the given one if the motion doesn't have it
static Matrix BoneRotationOr(const MOTN_Motion* motion, uint32_t bone, float frame, Matrix rest) {
    bool found;
    Vector3 turns = BoneValue(motion, bone, false, frame, &found);
    return found ? Euler(turns) : rest;
}

// Rotation of a bone, or the rotation of the node in the file if the motion doesn't have it
static Matrix BoneRotation(const MOTN_Motion* motion, uint32_t bone, float frame, const MT5Mesh_Node* node) {
    return BoneRotationOr(motion, bone, frame, LocalRotation(node));
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
    // The limb bends around the hinge axis of the upper node, perpendicular to the limb and to the bend axis. Only that
    // axis of the rotation counts: the motions turn the upper arm by any angle around it (e.g. about 0 instead of the
    // usual half turn in JIJ_WAN_AT1_WANTOJIDOUHANBAIKI_0100), which would twist the limb if it were swung to the target
    Vector3 limbLocal = Vector3Normalize(midLocal);
    Vector3 hingeLocal = Vector3CrossProduct(limbLocal, bendAxis);
    Vector3 hinge = Rotate(rotation, hingeLocal);
    hinge = Vector3Subtract(hinge, Vector3Scale(direction, Vector3DotProduct(hinge, direction)));
    bool hinged = Vector3Length(hingeLocal) > 1e-4f && Vector3Length(hinge) > 1e-4f;
    Vector3 bend;
    if (hinged) {
        hinge = Vector3Normalize(hinge);
        bend = Vector3CrossProduct(hinge, direction);
    }
    else {
        // The hinge points to the target: the bend goes to the axis, without its part along the direction to the target
        bend = Rotate(Mul(Swing(Rotate(rotation, midLocal), direction), rotation), bendAxis);
        bend = Vector3Subtract(bend, Vector3Scale(direction, Vector3DotProduct(bend, direction)));
        if (Vector3Length(bend) < 1e-4f)
            bend = Vector3Perpendicular(direction);
        bend = Vector3Normalize(bend);
    }
    float along = (a * a - b * b + distance * distance) / (2.0f * distance);
    float height = sqrtf(fmaxf(a * a - along * along, 0.0f));
    Vector3 knee = Vector3Add(joint, Vector3Add(Vector3Scale(direction, along), Vector3Scale(bend, height)));

    // The upper node points to the knee, with its hinge axis on the hinge
    if (hinged)
        rotation = Mul(Mul(Basis(Vector3Subtract(knee, joint), hinge),
            MatrixTranspose(Basis(Rotate(rotation, limbLocal), Rotate(rotation, hingeLocal)))), rotation);
    else
        rotation = Mul(Swing(Rotate(rotation, midLocal), Vector3Subtract(knee, joint)), rotation);
    world[upper] = Place(joint, Mul(rotation, MatrixScale(mesh->nodes[upper].scale.x, mesh->nodes[upper].scale.y, mesh->nodes[upper].scale.z)));
    Vector3 midPosition = Vector3Add(joint, Rotate(rotation, midLocal));
    Matrix midRotation = Mul(rotation, LocalRotation(&mesh->nodes[mid]));
    Vector3 reached = Vector3Add(joint, Vector3Scale(direction, distance));
    midRotation = Mul(Swing(Rotate(midRotation, endLocal), Vector3Subtract(reached, midPosition)), midRotation);
    world[mid] = Place(midPosition, midRotation);
}

// The pose at a frame, from the values of the channels at that frame
static bool ComputeFrame(const MT5Mesh* mesh, const MT5Pose_Body* body, const MOTN_Motion* motion, const BoneSet* bones,
    float frame, Matrix* world) {
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
    // The motions of a part of the body don't place the hips: they stay at the height of the torso in the file
    float torsoHeight = bind[body->torso].m13;
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

    bool placed;
    Vector3 hips = BoneValue(motion, MT5POSE_BONE_ROOT, true, frame, &placed);
    if (!placed)
        hips = (Vector3) { 0.0f, torsoHeight, 0.0f };
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
            // At the hips, turned so that the head reaches its position, keeping the side of the torso (Z axis) of the motion
            Matrix rotation = Mul(root, BoneRotationOr(motion, bones->torso, frame, Euler(g_torsoRest)));
            bool found;
            Vector3 headTarget = BoneValue(motion, bones->headTarget, true, frame, &found);
            Vector3 head = Vector3Transform(Origin(bind[body->head]), MatrixInvert(bind[i]));
            if (found)
                rotation = Mul(Aim(rotation, head, (Vector3) { 0.0f, 0.0f, 1.0f }, Vector3Subtract(headTarget, hips)), rotation);
            world[i] = Place(hips, rotation);
        }
        else if (index == body->pelvis) {
            // The rotation of the pelvis of the motions is a quarter turn around Y in the pose of the file
            Matrix rest = Mul(MatrixRotateY(-PI / 2.0f), RotationOf(bind[i]));
            Matrix rotation = Mul(Mul(root, BoneRotationOr(motion, bones->pelvis, frame, Euler(g_pelvisRest))), rest);
            world[i] = Place(Origin(world[i]), rotation);
        }
        else if (index == body->head) {
            // The face looks to the X axis of the head
            world[i] = Mul(parentWorld, LocalWith(node, BoneRotation(motion, bones->head, frame, node)));
            // The look position is in front of the position of the head of the motion, which the head of the model doesn't
            // always reach: the torso is rigid, and the distance from the hips to the head shrinks between the keys (e.g.
            // from 0.38 to 0.26 in AK_TA_SY_F), so the head ends past it and would look up
            bool found, placed;
            Vector3 lookTarget = BoneValue(motion, bones->lookTarget, true, frame, &found);
            Vector3 headTarget = BoneValue(motion, bones->headTarget, true, frame, &placed);
            Vector3 head = Origin(world[i]);
            if (found)
                world[i] = Place(head, Mul(LookAt(world[i], Vector3Subtract(lookTarget, placed ? headTarget : head)), RotationOf(world[i])));
            // The torso can't bend: when the head is closer to the hips than the length of the torso, the torso leans more
            // than the motion wants (e.g. 50 degrees to the front in AK_TA_SY_F while the face looks 40 degrees up), and
            // the neck would bend as no one's can. The head turns at most so much from its pose on the torso in the file
            Matrix rest = RotationOf(Mul(parentWorld, MT5Mesh_NodeMatrix(node)));
            world[i] = Place(head, Mul(LimitTilt(world[i], rest), RotationOf(world[i])));
            world[i] = Place(head, Mul(LimitTurn(Mul(RotationOf(world[i]), MatrixInvert(rest)), MT5POSE_NECK_TURN), rest));
        }

        for (int side = 0; side < 2; side++) {
            const int32_t* leg = body->legs[side];
            const int32_t* arm = body->arms[side];
            const uint8_t* armBones = bones->arms[swappedArms ? 1 - side : side];
            if (index == leg[0]) {
                world[i] = Mul(parentWorld, LocalWith(node, BoneRotation(motion, bones->legs[side][0], frame, node)));
                bool found;
                Vector3 target = BoneValue(motion, bones->legs[side][1], true, frame, &found);
                const MT5Mesh_Node* shin = &mesh->nodes[leg[1]];
                if (found)
                    SolveLimb(mesh, world, leg[0], leg[1], leg[2], target, g_kneeAxis);
                else
                    world[leg[1]] = Mul(world[i], LocalWith(shin, BoneRotation(motion, bones->knees[side], frame, shin)));
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
                const MT5Mesh_Node* forearm = &mesh->nodes[arm[2]];
                if (found)
                    SolveLimb(mesh, world, arm[1], arm[2], arm[3], target, g_elbowAxis);
                else
                    world[arm[2]] = Mul(world[i], LocalWith(forearm, BoneRotation(motion, bones->elbows[swappedArms ? 1 - side : side], frame, forearm)));
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

// Mix of two world matrices: the position and the scale of the axes go straight, the rotation by the shortest way
static Matrix Blend(Matrix a, Matrix b, float t) {
    Vector3 axesA[3] = { { a.m0, a.m1, a.m2 }, { a.m4, a.m5, a.m6 }, { a.m8, a.m9, a.m10 } };
    Vector3 axesB[3] = { { b.m0, b.m1, b.m2 }, { b.m4, b.m5, b.m6 }, { b.m8, b.m9, b.m10 } };
    float scale[3];
    for (int i = 0; i < 3; i++) {
        float lengthA = Vector3Length(axesA[i]), lengthB = Vector3Length(axesB[i]);
        scale[i] = Lerp(lengthA, lengthB, t);
        if (lengthA > 0.0f)
            axesA[i] = Vector3Scale(axesA[i], 1.0f / lengthA);
        if (lengthB > 0.0f)
            axesB[i] = Vector3Scale(axesB[i], 1.0f / lengthB);
    }
    // A mirrored node (negative scale) keeps its third axis reversed
    if (Vector3DotProduct(Vector3CrossProduct(axesA[0], axesA[1]), axesA[2]) < 0.0f)
        scale[2] = -scale[2];
    Matrix rotationA = Basis(axesA[0], axesA[1]), rotationB = Basis(axesB[0], axesB[1]);
    Quaternion q = QuaternionSlerp(QuaternionFromMatrix(rotationA), QuaternionFromMatrix(rotationB), t);
    Matrix result = Mul(QuaternionToMatrix(q), MatrixScale(scale[0], scale[1], scale[2]));
    return Place(Vector3Lerp(Origin(a), Origin(b), t), result);
}

// The pose of the nodes of the model at a frame from the values of the channels of a sampled motion: the first nodes of
// the model, in the order of the file, take the rotation and the position of the motion
static bool ComputeNodesFrame(const MT5Mesh* mesh, const MOTN_Motion* motion, float frame, Matrix* world) {
    uint32_t nodes = SampledNodes(motion);
    for (uint32_t i = 0; i < mesh->nodeCount; i++) {
        const MT5Mesh_Node* node = &mesh->nodes[i];
        Matrix local = MT5Mesh_NodeMatrix(node);
        if (i < nodes) {
            Matrix rotation = Mul(Euler(BoneValue(motion, i, false, frame, NULL)), MatrixScale(node->scale.x, node->scale.y, node->scale.z));
            local = Place(BoneValue(motion, i, true, frame, NULL), rotation);
        }
        world[i] = node->parent >= 0 ? Mul(world[node->parent], local) : local;
    }
    return true;
}

typedef bool (*ComputeFunction)(const MT5Mesh* mesh, const void* context, const MOTN_Motion* motion, float frame, Matrix* world);

// The game shows whole frames. Between two, the channels can go a long way (e.g. the rotation of the hips of
// AK_KOUTEN_KI_01 goes from (-0.09, -0.444, -0.029) turns at frame 6 to (0.41, -0.056, 0.471) at frame 7, the same
// rotation written another way; the rotations of the sampled motions go from -0.5 to 0.5 turns): the pose between two
// frames is a mix of their poses
static bool ComputeBetweenFrames(const MT5Mesh* mesh, const void* context, const MOTN_Motion* motion, float frame,
    Matrix* world, ComputeFunction compute) {
    float whole = floorf(frame), t = frame - whole;
    if (t < 1e-3f || whole + 1.0f > (float)motion->frames)
        return compute(mesh, context, motion, t < 1e-3f ? whole : frame, world);
    Matrix* next = malloc((mesh->nodeCount + 1) * sizeof(Matrix));
    bool computed = next != NULL && compute(mesh, context, motion, whole, world) && compute(mesh, context, motion, whole + 1.0f, next);
    if (computed) {
        for (uint32_t i = 0; i < mesh->nodeCount; i++)
            world[i] = Blend(world[i], next[i], t);
    }
    free(next);
    return computed;
}

static bool ComputeBody(const MT5Mesh* mesh, const void* body, const MOTN_Motion* motion, float frame, Matrix* world) {
    return ComputeFrame(mesh, body, motion, FindBoneSet(motion), frame, world);
}

static bool ComputeNodes(const MT5Mesh* mesh, const void* context, const MOTN_Motion* motion, float frame, Matrix* world) {
    (void)context;
    return ComputeNodesFrame(mesh, motion, frame, world);
}

bool MT5Pose_Compute(const MT5Mesh* mesh, const MT5Pose_Body* body, const MOTN_Motion* motion, float frame, Matrix* world) {
    return FindBoneSet(motion) != NULL && ComputeBetweenFrames(mesh, body, motion, frame, world, ComputeBody);
}

bool MT5Pose_CanPoseNodes(const MT5Mesh* mesh, const MOTN_Motion* motion) {
    uint32_t nodes = SampledNodes(motion);
    return nodes > 0 && nodes <= mesh->nodeCount;
}

bool MT5Pose_ComputeNodes(const MT5Mesh* mesh, const MOTN_Motion* motion, float frame, Matrix* world) {
    return MT5Pose_CanPoseNodes(mesh, motion) && ComputeBetweenFrames(mesh, NULL, motion, frame, world, ComputeNodes);
}
