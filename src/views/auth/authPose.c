// The poses and the transforms of the characters of a cutscene: their motions, the nodes that the program of the map
// moves, and the characters that it places or attaches
#include "views/auth/authViewInternal.h"

static Matrix PlacedTransform(const AUTHView_Placed* placed, const float position[3], const float angle[3]);

// The last motion that the character started at or before the current frame: it keeps the pose of its last frame after
// its end. NULL if none
static const AUTH_Command* PoseCommand(uint32_t idValue) {
    const AUTH_Command* motion = NULL;
    uint32_t frame = CurrentFrame();
    for (uint32_t i = 0; i < g_auth.sequence.commandCount && g_auth.sequence.commands[i].frame <= frame; i++) {
        const AUTH_Command* command = &g_auth.sequence.commands[i];
        if (command->opcode == AUTH_COMMAND_MOTION && Arg(command, 0) == idValue)
            motion = command;
    }
    return motion;
}

// The values of a node at a frame of the program, from those of the model
static void ProgramNode(uint32_t character, const MT5Mesh_Node* node, uint32_t frame, SCNCutscene_Transform* transform) {
    *transform = (SCNCutscene_Transform) {
        { (int32_t)lroundf(node->rotation.x * 65536.0f / 360.0f), (int32_t)lroundf(node->rotation.y * 65536.0f / 360.0f),
            (int32_t)lroundf(node->rotation.z * 65536.0f / 360.0f) },
        { node->position.x, node->position.y, node->position.z }, { node->scale.x, node->scale.y, node->scale.z } };
    SCNCutscene_NodeAt(&g_auth.program, character, (uint16_t)node->id, frame, transform);
}

// Puts the model of a character in the pose that the program of the map gives to its nodes at the current time (e.g.
// the doors and the wheels of the bus of scene/01/d000), between the values of two frames. Returns false if the
// program doesn't move them
static bool ProgramPose(uint32_t character, AUTHView_Model* model) {
    const SCNCutscene* program = &g_auth.program;
    const MT5Mesh* mesh = &model->model.mesh;
    if (!SCNCutscene_MovesCharacter(program, character) || mesh->nodeCount == 0)
        return false;
    if (model->pose == NULL && (model->pose = malloc((mesh->nodeCount + 1) * sizeof(Matrix))) == NULL)
        return false;
    bool* moved = malloc(mesh->nodeCount * sizeof(bool));
    if (moved == NULL)
        return false;
    float frame = fmaxf(g_auth.time * AUTH_FRAME_RATE, 0.0f), whole = floorf(frame), t = frame - whole;
    bool any = false;
    // The parents are before their children
    for (uint32_t i = 0; i < mesh->nodeCount; i++) {
        const MT5Mesh_Node* node = &mesh->nodes[i];
        SCNCutscene_Transform a, b;
        bool own = SCNCutscene_NodeAt(program, character, (uint16_t)node->id, (uint32_t)whole, &(SCNCutscene_Transform) { 0 });
        moved[i] = own || (node->parent >= 0 && moved[node->parent]);
        if (!moved[i]) {
            model->pose[i] = mesh->pose[i];
            continue;
        }
        MT5Mesh_Node posed = *node;
        if (own) {
            ProgramNode(character, node, (uint32_t)whole, &a);
            ProgramNode(character, node, (uint32_t)whole + 1, &b);
            for (int v = 0; v < 3; v++) {
                float rotation = (a.rotation[v] + (float)(b.rotation[v] - a.rotation[v]) * t) * 360.0f / 65536.0f;
                float position = Lerp(a.position[v], b.position[v], t), scale = Lerp(a.scale[v], b.scale[v], t);
                (&posed.rotation.x)[v] = rotation;
                (&posed.position.x)[v] = position;
                (&posed.scale.x)[v] = scale;
            }
            any = true;
        }
        Matrix local = MT5Mesh_NodeMatrix(&posed);
        if (node->parent >= 0)
            model->pose[i] = MatrixMultiply(local, model->pose[node->parent]);
        else    // As the root in the pose of the file
            model->pose[i] = MatrixMultiply(local, MatrixMultiply(MatrixInvert(MT5Mesh_NodeMatrix(node)), mesh->pose[i]));
    }
    free(moved);
    if (any)
        MT5Render_Pose(&model->model, model->pose);
    return any;
}

// Puts the model of a character in the pose of its motion at the current time (from the first frame of the command to
// its last one), or in the pose that the program of the map gives to its nodes, or in the pose of the file
void AUTHPose_PoseCharacter(uint32_t index) {
    const AUTH_Character* character = &g_auth.sequence.characters[index];
    AUTHView_Model* model = &g_auth.models[g_auth.characterModels[index].model];
    const AUTH_Command* command = model->body ? PoseCommand(character->idValue) : NULL;
    const MOTN_Motion* motion = command != NULL ? AUTHMotions_Find(Arg(command, 1)) : NULL;
    float frame = 0.0f;
    if (motion != NULL) {
        float first = (float)Arg(command, 2), last = fminf((float)Arg(command, 3), (float)motion->frames);
        frame = Clamp(first + g_auth.time * AUTH_FRAME_RATE - command->frame, first, fmaxf(first, last));
    }
    model->posed = motion != NULL && model->pose != NULL && MT5Pose_Compute(&model->model.mesh, &model->bodyNodes, motion, frame, model->pose);
    model->programPosed = !model->posed && ProgramPose(character->idValue, model);
    if (!model->programPosed)
        MT5Render_Pose(&model->model, model->posed ? model->pose : NULL);
}

// As the nodes of the models: rotation around X, Y and Z, and translation, at the current time. The models of the persons
// are raised to stand at their position, unless a motion poses them, with the feet on the floor
Matrix AUTHPose_CharacterTransform(uint32_t index) {
    const AUTH_Character* character = &g_auth.sequence.characters[index];
    const AUTHView_CharacterModel* characterModel = &g_auth.characterModels[index];
    const AUTHView_Model* model = &g_auth.models[characterModel->model];
    Vector3 position = CurvePoint(&character->curves[AUTH_CHARACTER_X], g_auth.time);
    Vector3 rotation = CurvePoint(&character->curves[AUTH_CHARACTER_ROTATION_X], g_auth.time);
    return Scene3D_PlaceTransform(characterModel->human && !model->posed ? model->lift : 0.0f,
        (const float[3]) { rotation.x, rotation.y, rotation.z }, (const float[3]) { position.x, position.y, position.z });
}

// Height of the top of a character above its position: of its model, or of the cylinder
float AUTHPose_CharacterHeight(uint32_t index) {
    const AUTHView_CharacterModel* characterModel = &g_auth.characterModels[index];
    if (characterModel->model < 0)
        return SCENE3D_CHARACTER_HEIGHT;
    const AUTHView_Model* model = &g_auth.models[characterModel->model];
    return model->top + (characterModel->human ? model->lift : 0.0f);
}

// The position and the angles of a character placed by the program at a frame: those of its script, changed by the calls
// up to the frame. Returns false if none places it yet
static bool PlacedAt(const AUTHView_Placed* placed, uint32_t frame, float position[3], float angle[3]) {
    memcpy(position, placed->position, sizeof(placed->position));
    memcpy(angle, placed->angle, sizeof(placed->angle));
    return SCNCutscene_PlaceAt(&g_auth.program, placed->idValue, frame, position, angle);
}

// Placed or attached by the program up to a frame, or a character whose nodes it moves (from the frame 0)
bool AUTHPose_PlacedDrawn(const AUTHView_Placed* placed, uint32_t frame) {
    float position[3], angle[3];
    return SCNCutscene_AttachAt(&g_auth.program, placed->idValue, frame) != NULL || PlacedAt(placed, frame, position, angle) ||
        SCNCutscene_MovesCharacter(&g_auth.program, placed->idValue);
}

// Puts the model of a character placed by the program in the pose that the program gives to its nodes at the current
// time (e.g. the flaps of the letter MALS of the cutscene 24 of scene/01/d000), or in the pose of the file
void AUTHPose_PosePlaced(const AUTHView_Placed* placed) {
    AUTHView_Model* model = &g_auth.models[placed->model];
    model->posed = false;
    model->programPosed = ProgramPose(placed->idValue, model);
    if (!model->programPosed)
        MT5Render_Pose(&model->model, NULL);
}

// The node of a body to which a bone of the skeleton of the game is attached (SCNCutscene_Attach.node): the bones of the
// skeletons of the persons, from the hips (bone 0), are the torso (1 to 3), the head (4 to 6), the arms of the −Z side
// (7 to 12, the hand) and +Z side (13 to 18), and the legs, so the bones 11 and 12 are the left hand (0xFFBE) and 17 and
// 18 the right hand (0xFFBF). -1 for the others, and for the models that aren't bodies: the character itself
static int AttachNode(const AUTHView_Model* model, uint16_t bone) {
    if (!model->body)
        return -1;
    if (bone == 11 || bone == 12)
        return model->bodyNodes.arms[0][3];
    if (bone == 17 || bone == 18)
        return model->bodyNodes.arms[1][3];
    if (bone >= 4 && bone <= 6)
        return model->bodyNodes.head;
    return -1;
}

// Where a character attached to a character of the cutscene is at a frame: the position and the angles of the call,
// relative to the node of the bone in the pose of that frame (as the nodes of the models: rotation around X, Y and Z,
// then the position). Returns false if the character it is attached to isn't drawn with a model
static bool AttachedTransform(const SCNCutscene_Attach* attach, uint32_t frame, Matrix* transform) {
    int parent = -1;
    for (uint32_t i = 0; i < g_auth.sequence.characterCount && parent < 0; i++) {
        if (g_auth.sequence.characters[i].idValue == attach->parent)
            parent = (int)i;
    }
    if (parent < 0 || g_auth.characterModels[parent].model < 0)
        return false;
    const AUTHView_Model* model = &g_auth.models[g_auth.characterModels[parent].model];
    // The pose of the character at that frame, then back to the current time
    float time = g_auth.time;
    bool other = frame != CurrentFrame();
    if (other)
        g_auth.time = frame / AUTH_FRAME_RATE;
    if (other || !AUTHView_IsCharacterShown((uint32_t)parent))
        AUTHPose_PoseCharacter((uint32_t)parent);
    int node = AttachNode(model, attach->node);
    Matrix world = AUTHPose_CharacterTransform((uint32_t)parent);
    if (node >= 0)
        world = MatrixMultiply(model->posed || model->programPosed ? model->pose[node] : model->model.mesh.pose[node], world);
    if (other) {
        g_auth.time = time;
        AUTHPose_PoseCharacter((uint32_t)parent);
    }
    MT5Mesh_Node local = { 0 };
    local.scale = (Vector3) { 1.0f, 1.0f, 1.0f };
    for (int v = 0; v < 3; v++) {
        memcpy(&(&local.position.x)[v], &attach->position[v], sizeof(float));
        (&local.rotation.x)[v] = attach->angles[v] * 360.0f / 65536.0f;
    }
    *transform = MatrixMultiply(MT5Mesh_NodeMatrix(&local), world);
    return true;
}

// The transform of a character placed or attached by the program at the current frame: attached, where it is attached
// to; detached, where it was when it was detached; else where the calls place it, or where its script places it if the
// program only moves its nodes. Returns false if it isn't drawn yet
bool AUTHPose_ProgramTransform(const AUTHView_Placed* placed, Matrix* transform) {
    uint32_t frame = CurrentFrame();
    const SCNCutscene_Attach* attach = SCNCutscene_AttachAt(&g_auth.program, placed->idValue, frame);
    if (attach != NULL && attach->parent != 0)
        return AttachedTransform(attach, frame, transform);
    const SCNCutscene_Attach* before = attach != NULL && attach->frame > 0 ?
        SCNCutscene_AttachAt(&g_auth.program, placed->idValue, attach->frame - 1) : NULL;
    if (before != NULL && before->parent != 0)
        return AttachedTransform(before, attach->frame, transform);
    float position[3], angle[3];
    if (!PlacedAt(placed, frame, position, angle) && !SCNCutscene_MovesCharacter(&g_auth.program, placed->idValue))
        return false;
    *transform = PlacedTransform(placed, position, angle);
    return true;
}

// As the characters of the map: rotation around X, Y and Z, and translation. The models of the persons are raised to
// stand at their position
static Matrix PlacedTransform(const AUTHView_Placed* placed, const float position[3], const float angle[3]) {
    const AUTHView_Model* model = &g_auth.models[placed->model];
    return Scene3D_PlaceTransform(placed->human ? model->lift : 0.0f, angle, position);
}
