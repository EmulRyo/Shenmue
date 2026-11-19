// The detailed faces of the characters of a cutscene: the face model (_F) of a character replaces the head of its body,
// and its skin (FTBL) is posed with the poses of the TALK files: the expressions of the face commands, the shapes of the
// mouth of the lip-sync records of the voices, and blinking
#include "core/log.h"
#include "formats/sound/lipsync.h"
#include "views/auth/authViewInternal.h"

#define AUTHFACES_FOLDER        "model/face"
#define AUTHFACES_CHAIN         "talk_n00.bin"  // The sets s, m, b and ss, by the talk set of the FTBL files
#define AUTHFACES_KC            "talk_kc.bin"   // The face KC1 (the game gives it its own set)
#define AUTHFACES_MOUTH_FRAMES  4.0f            // Game frames (1/60 s) to go from a shape of the mouth to the next
#define AUTHFACES_BLINK_CLOSE   2.0f            // Frames (1/30 s) to close the eyes, and to open them
#define AUTHFACES_BLINK_OPEN    4.0f
#define AUTHFACES_BLINK_SEED    0x2545F491u

// The face that the game gives to some bodies, by the first three letters of their model (code at 0x0C0BBB60)
static const char* g_faceOfBody[][2] = {
    { "YKC", "YKB" }, { "YKE", "YKB" }, { "YKF", "YKD" }, { "FUB", "FUK" }, { "FUD", "FUC" },
};

// Face model of a character: its Face command, or the face of the first three letters of its body, in the face folder
// of the scene. Returns false if there is none
static bool FacePath(const char* face, const char* body, char* path, size_t size) {
    char name[16] = { 0 };
    if (face != NULL && face[0] != '\0')
        snprintf(name, sizeof(name), "%.3s", face);
    else if (body != NULL && strlen(GetFileName(body)) >= 3) {
        snprintf(name, sizeof(name), "%.3s", GetFileName(body));
        for (size_t i = 0; i < sizeof(g_faceOfBody) / sizeof(g_faceOfBody[0]); i++) {
            if (Text_EqualsIgnoreCase(name, g_faceOfBody[i][0]))
                snprintf(name, sizeof(name), "%s", g_faceOfBody[i][1]);
        }
    }
    if (name[0] == '\0' || g_auth.sceneFolder[0] == '\0')
        return false;
    snprintf(path, size, "%s/%s/%s_f.mt5", g_auth.sceneFolder, AUTHFACES_FOLDER, TextToLower(name));
    return FileExists(path);
}

static const TALK_Set* TalkSet(const char* facePath, int32_t set) {
    bool kc = Text_EqualsIgnoreCase(TextSubtext(GetFileName(facePath), 0, 3), "kc1");
    TALK_File* file = &g_auth.talks[kc ? 1 : 0];
    if (file->setCount == 0) {
        char path[AUTHVIEW_PATH_SIZE];
        snprintf(path, sizeof(path), "%s/%s/%s", g_auth.sceneFolder, AUTHFACES_FOLDER, kc ? AUTHFACES_KC : AUTHFACES_CHAIN);
        if (!FileExists(path) || !TALK_Load(path, file))
            return NULL;
    }
    if (kc)
        set = 0;
    return set >= 0 && (uint32_t)set < file->setCount ? &file->sets[set] : NULL;
}

bool AUTHFaces_Load(uint32_t character, const char* face, const char* body, const IPAC_File* packed) {
    AUTHView_Model* model = &g_auth.models[g_auth.characterModels[character].model];
    AUTHView_Face* result = &g_auth.faces[character];
    char path[AUTHVIEW_PATH_SIZE], skinPath[AUTHVIEW_PATH_SIZE];
    if (!model->body || model->bodyNodes.head < 0 || !FacePath(face, body, path, sizeof(path)))
        return false;
    snprintf(skinPath, sizeof(skinPath), "%.*s_ftbl.bin", (int)strlen(path) - 6, path);

    // The node of the body whose mesh the negative indices of the face use: the parent of the head
    MT5_Model source;
    if (!(packed != NULL ? MT5_LoadFromMemory(packed->data, packed->size, packed->filename, &source) : MT5_Load(body, &source)))
        return false;
    int32_t head = model->bodyNodes.head;
    int32_t neck = (uint32_t)head < source.nodeCount ? MT5_ParentMeshNode(&source, source.nodes[head].parent) : -1;
    bool loaded = neck >= 0 && MT5Render_LoadPart(path, &source.nodes[neck], &result->model);
    MT5_Free(&source);
    if (!loaded)
        return false;
    result->loaded = true;
    result->head = head;
    result->neck = neck;
    snprintf(result->name, sizeof(result->name), "%s", GetFileName(path));
    if (FileExists(skinPath) && FTBL_Load(skinPath, &result->skin))
        result->set = TalkSet(path, result->skin.talkSet);
    uint32_t nodeCount = result->model.mesh.nodeCount;
    result->pose = malloc((nodeCount + 2) * sizeof(Matrix));
    result->hidden = calloc(model->model.mesh.nodeCount + 1, sizeof(bool));
    result->offsets = calloc(result->skin.vertexCount * 3 + 1, sizeof(float));
    if (result->pose == NULL || result->hidden == NULL || result->offsets == NULL) {
        AUTHFaces_Free(character);
        return false;
    }
    result->hidden[head] = true;
    Log_Debug("    face %s%s\n", result->name, result->set != NULL ? "" : " (not animated)");
    return true;
}

void AUTHFaces_Free(uint32_t character) {
    AUTHView_Face* face = &g_auth.faces[character];
    if (face->loaded)
        MT5Render_FreeModel(&face->model);
    FTBL_Free(&face->skin);
    free(face->pose);
    free(face->hidden);
    free(face->offsets);
    memset(face, 0, sizeof(*face));
}

const bool* AUTHFaces_HiddenNodes(uint32_t character) {
    return g_auth.faces != NULL && g_auth.faces[character].loaded ? g_auth.faces[character].hidden : NULL;
}

// The expression of the face of a character at a frame (the face commands), and the one before it. *alpha goes from 0
// to 1 over the frames of the command
static void ExpressionAt(uint32_t idValue, float frame, int32_t* previous, int32_t* current, float* alpha) {
    *previous = *current = 0;
    *alpha = 1.0f;
    for (uint32_t i = 0; i < g_auth.sequence.commandCount && g_auth.sequence.commands[i].frame <= frame; i++) {
        const AUTH_Command* command = &g_auth.sequence.commands[i];
        if (command->opcode != AUTH_COMMAND_EXPRESSION || Arg(command, 0) != idValue)
            continue;
        *previous = *current;
        *current = (int32_t)Arg(command, 1);
        float frames = fmaxf((float)Arg(command, 3), 1.0f);
        *alpha = Clamp((frame - (float)command->frame) / frames, 0.0f, 1.0f);
    }
}

// The shape of the mouth of a character at a frame (the lip-sync of its voices, else rest), the one before it, and
// *alpha from 0 to 1 while it goes from one to the other. Returns false if it isn't speaking
static bool MouthAt(uint32_t idValue, float frame, int32_t* previous, int32_t* current, float* alpha) {
    *previous = *current = 0;
    *alpha = 1.0f;
    for (uint32_t i = 0; i < g_auth.sequence.commandCount && g_auth.sequence.commands[i].frame <= frame; i++) {
        const AUTH_Command* command = &g_auth.sequence.commands[i];
        const LIPSYNC_Record* record = command->opcode == AUTH_COMMAND_VOICE && Arg(command, 0) == idValue ? AUTHSounds_LipSync(i) : NULL;
        if (record == NULL)
            continue;
        float time = (frame - (float)command->frame) / AUTH_FRAME_RATE * LIPSYNC_FRAME_RATE, start;
        if (time > (float)record->duration + AUTHFACES_MOUTH_FRAMES)
            continue;
        uint16_t before;
        *current = LIPSYNC_MouthAt(record, time, &start, &before);
        *previous = before;
        *alpha = Clamp((time - start) / AUTHFACES_MOUTH_FRAMES, 0.0f, 1.0f);
        return true;
    }
    return false;
}

// How closed the eyes are at a frame (0 open, 1 closed): the game blinks every 60 to 90 frames, a random time, closing
// the eyes in 2 frames and opening them in 4
static float BlinkAt(uint32_t character, float frame) {
    uint32_t seed = AUTHFACES_BLINK_SEED ^ (character * 0x9E3779B9u);
    float start = 0.0f;
    for (;;) {
        seed = seed * 1664525u + 1013904223u;
        start += 60.0f + 10.0f * (float)((seed >> 16) & 3);
        if (frame < start)
            return 0.0f;
        float t = frame - start;
        if (t < AUTHFACES_BLINK_CLOSE)
            return t / AUTHFACES_BLINK_CLOSE;
        if (t < AUTHFACES_BLINK_CLOSE + AUTHFACES_BLINK_OPEN)
            return 1.0f - (t - AUTHFACES_BLINK_CLOSE) / AUTHFACES_BLINK_OPEN;
        start += AUTHFACES_BLINK_CLOSE + AUTHFACES_BLINK_OPEN;
    }
}

typedef float AUTHFaces_Offsets[FTBL_BONES][3];

static void PoseOffsets(const AUTHView_Face* face, TALK_Layer layer, int32_t pose, AUTHFaces_Offsets offsets) {
    memset(offsets, 0, sizeof(AUTHFaces_Offsets));
    pose = pose < 0 ? 0 : pose >= (int32_t)face->set->poseCount ? (int32_t)face->set->poseCount - 1 : pose;
    TALK_BoneOffsets(face->set, &face->skin, layer, pose, offsets);
}

// a + (b - a) * t, into a
static void LerpOffsets(AUTHFaces_Offsets a, const AUTHFaces_Offsets b, float t) {
    for (int i = 0; i < FTBL_BONES; i++) {
        for (int v = 0; v < 3; v++)
            a[i][v] += (b[i][v] - a[i][v]) * t;
    }
}

// Offsets of a part of the face for an expression: between two poses of the expression
static void ExpressionOffsets(const AUTHView_Face* face, TALK_Layer layer, int32_t expression, int32_t from, int32_t to, float t,
    AUTHFaces_Offsets offsets) {
    AUTHFaces_Offsets other;
    int32_t base = expression * TALK_POSES_PER_EXPRESSION;
    PoseOffsets(face, layer, base + from, offsets);
    if (t > 0.0f && to != from) {
        PoseOffsets(face, layer, base + to, other);
        LerpOffsets(offsets, other, t);
    }
}

MT5Render_Model* AUTHFaces_Pose(uint32_t character, const Matrix* body) {
    AUTHView_Face* face = g_auth.faces != NULL ? &g_auth.faces[character] : NULL;
    if (face == NULL || !face->loaded)
        return NULL;
    // The face on the head, its eyes as in the file, and the node of the body that the face joins
    const MT5Mesh* mesh = &face->model.mesh;
    face->pose[0] = body[face->head];
    for (uint32_t i = 1; i < mesh->nodeCount; i++) {
        int32_t parent = mesh->nodes[i].parent;
        face->pose[i] = MatrixMultiply(MT5Mesh_NodeMatrix(&mesh->nodes[i]), face->pose[parent >= 0 ? parent : 0]);
    }
    face->pose[mesh->nodeCount] = body[face->neck];
    if (face->set == NULL) {
        MT5Render_Pose(&face->model, face->pose);
        return &face->model;
    }

    uint32_t idValue = g_auth.sequence.characters[character].idValue;
    float frame = fmaxf(g_auth.time * AUTH_FRAME_RATE, 0.0f);
    int32_t previous, current, mouthFrom, mouthTo;
    float expressionAlpha, mouthAlpha;
    ExpressionAt(idValue, frame, &previous, &current, &expressionAlpha);
    if (!MouthAt(idValue, frame, &mouthFrom, &mouthTo, &mouthAlpha))
        mouthFrom = mouthTo = 0;
    float blink = BlinkAt(character, frame);

    // Each bone is moved by one part of the face: the eyes or the mouth
    AUTHFaces_Offsets eyes, eyesNext, mouth, mouthNext, total;
    ExpressionOffsets(face, TALK_LAYER_EYES, previous, 0, 1, blink, eyes);
    ExpressionOffsets(face, TALK_LAYER_MOUTH, previous, mouthFrom, mouthTo, mouthAlpha, mouth);
    if (current != previous) {
        ExpressionOffsets(face, TALK_LAYER_EYES, current, 0, 1, blink, eyesNext);
        ExpressionOffsets(face, TALK_LAYER_MOUTH, current, mouthFrom, mouthTo, mouthAlpha, mouthNext);
        LerpOffsets(eyes, eyesNext, expressionAlpha);
        LerpOffsets(mouth, mouthNext, expressionAlpha);
    }
    for (int b = 0; b < FTBL_BONES; b++) {
        for (int v = 0; v < 3; v++)
            total[b][v] = eyes[b][v] + mouth[b][v];
    }
    FTBL_Deform(&face->skin, (const float (*)[3])total, face->skin.vertexCount, face->offsets);
    MT5Mesh_PoseOptions options = { NULL, 0, face->skin.vertexCount, face->offsets };
    MT5Render_PoseWith(&face->model, face->pose, &options);
    face->expression = current;
    face->mouth = mouthTo;
    return &face->model;
}
