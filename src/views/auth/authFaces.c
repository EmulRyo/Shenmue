// The detailed faces of the characters of a cutscene: the face model (_F) of a character replaces the head of its body,
// and its skin (FTBL) is posed with the poses of the TALK files: the expressions of the face commands, the shapes of the
// mouth of the lip-sync records of the voices, and blinking
#include "core/log.h"
#include "formats/sound/lipsync.h"
#include "views/auth/authViewInternal.h"

#define AUTHFACES_FOLDER        "model/face"
#define AUTHFACES_CHAIN         "talk_n00.bin"  // The sets s, m, b and ss, by the talk set of the FTBL files
#define AUTHFACES_KC            "talk_kc.bin"   // The face KC1 (the game gives it its own set)
#define AUTHFACES_MOUTH_AHEAD   8.0f            // 1/60 s before its key that the game starts to move to a shape
#define AUTHFACES_MOUTH_OPEN    8.0f            // 1/60 s to reach a shape of the mouth of the lip-sync
#define AUTHFACES_MOUTH_CLOSE   16.0f           // To reach the shape 0 (closed), and the rest after the last key
#define AUTHFACES_KEEP          INT32_MIN       // The mouth of a change of expression: unchanged
#define AUTHFACES_BLINK_CLOSE   2               // Frames (1/30 s) to close the eyes, and to open them, when blinking
#define AUTHFACES_BLINK_OPEN    4
#define AUTHFACES_BLINK_MIN     60              // Frames between two blinks: 60 + 10 x (random & 3)
#define AUTHFACES_BLINK_STEP    10
#define AUTHFACES_EYELIDS_AUTO  3               // Frames of the transition of Eyelids mode 0
#define AUTHFACES_RANDOM_SEED   1u              // The random numbers of the game at the start: unknown, they depend
                                                // on the play
#define AUTHFACES_EYE_RIGHT     0x4D            // Nodes of the eyes of the face models
#define AUTHFACES_EYE_LEFT      0x4E
#define AUTHFACES_FRONT_FRAMES  16.0f           // Frames to turn the eyes back to the front (EyesLook mode 0)
#define AUTHFACES_EYE_LIMIT     30.0f           // Degrees the eyes turn without FTBL file

// The pose of the mouth within the expression for each shape of the lip-sync records (table at 0x0C26C708)
static const int32_t g_mouthPoses[] = { 0, 3, 2, 5, 4, 1 };

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
    free(face->eyelidFrames);
    memset(face, 0, sizeof(*face));
}

const bool* AUTHFaces_HiddenNodes(uint32_t character) {
    return g_auth.faces != NULL && g_auth.faces[character].loaded ? g_auth.faces[character].hidden : NULL;
}

// A change of the expression of a face, by the AUTH command 4 or the calls FaceExpression and FaceAnimation of the
// program of the map: the expression (poses 6 x expression of the TALK set) reached in a number of frames, and the
// pose of the mouth within it that the face keeps while it doesn't speak (-1 rest, AUTHFACES_KEEP unchanged)
typedef struct {
    float   frame;
    int32_t expression;
    int32_t mouth;
    float   frames;
} AUTHFaces_Change;

// The changes of the expression of a character up to a frame, the AUTH commands and the calls of the program in the
// order of their frames (the calls first at the same frame): the last one, the one before it, and the last two that
// change the mouth. Each is expression 0 with the mouth at rest at frame 0 if there is none
static void ExpressionAt(uint32_t idValue, float frame, AUTHFaces_Change* last, AUTHFaces_Change* before, AUTHFaces_Change* mouth,
    AUTHFaces_Change* mouthBefore) {
    AUTHFaces_Change none = { 0.0f, 0, -1, 1.0f };
    *last = *before = *mouth = *mouthBefore = none;
    const SCNCutscene* program = &g_auth.program;
    uint32_t c = 0, p = 0;
    for (;;) {
        const AUTH_Command* command = c < g_auth.sequence.commandCount ? &g_auth.sequence.commands[c] : NULL;
        const SCNCutscene_Face* call = p < program->faceCount ? &program->faces[p] : NULL;
        bool useCall = call != NULL && (command == NULL || call->frame <= command->frame);
        float at = useCall ? (float)call->frame : command != NULL ? (float)command->frame : frame + 1.0f;
        if (at > frame)
            break;
        AUTHFaces_Change change = { at, 0, AUTHFACES_KEEP, 1.0f };
        if (useCall) {
            p++;
            if (call->character != idValue || (call->known & 7) != 7)
                continue;
            if (call->function == SCNCUTSCENE_EXPRESSION) {
                change.expression = (int32_t)call->values[0];
                change.mouth = (int32_t)call->values[1];
                change.frames = (float)(int32_t)call->values[2];
            }
            else if (call->function == SCNCUTSCENE_FACE && call->values[0] != 0) {
                change.expression = (int32_t)call->values[1];
                change.frames = (float)(int32_t)call->values[2];
            }
            else
                continue;
        }
        else {
            c++;
            if (command->opcode != AUTH_COMMAND_EXPRESSION || Arg(command, 0) != idValue)
                continue;
            change.expression = (int32_t)Arg(command, 1);
            change.mouth = (int32_t)Arg(command, 2);
            change.frames = (float)(int32_t)Arg(command, 3);
        }
        change.frames = fmaxf(change.frames, 1.0f);
        *before = *last;
        *last = change;
        if (change.mouth != AUTHFACES_KEEP) {
            *mouthBefore = *mouth;
            *mouth = change;
        }
    }
}

static int32_t MouthPose(uint16_t shape) {
    return shape < sizeof(g_mouthPoses) / sizeof(g_mouthPoses[0]) ? g_mouthPoses[shape] : 0;
}

// The pose of the mouth of a character at a frame by the lip-sync of its voices (within the expression), the one before
// it, and *alpha from 0 to 1 while it goes from one to the other. The game starts to move to each shape 4 frames before
// its key, and closes the mouth after the last one. Returns false if it isn't speaking; *ended receives the frame at
// which the last voice before the frame ended (-1 if none)
static bool MouthAt(uint32_t idValue, float frame, int32_t* previous, int32_t* current, float* alpha, float* ended) {
    *previous = *current = 0;
    *alpha = 1.0f;
    *ended = -1.0f;
    for (uint32_t i = 0; i < g_auth.sequence.commandCount && g_auth.sequence.commands[i].frame <= frame; i++) {
        const AUTH_Command* command = &g_auth.sequence.commands[i];
        const LIPSYNC_Record* record = command->opcode == AUTH_COMMAND_VOICE && Arg(command, 0) == idValue ? AUTHSounds_LipSync(i) : NULL;
        if (record == NULL)
            continue;
        float time = (frame - (float)command->frame) / AUTH_FRAME_RATE * LIPSYNC_FRAME_RATE, start;
        float duration = (float)record->duration;
        if (time > duration + AUTHFACES_MOUTH_CLOSE) {
            *ended = fmaxf(*ended, (float)command->frame + duration / LIPSYNC_FRAME_RATE * AUTH_FRAME_RATE);
            continue;
        }
        float ahead = time < duration ? fminf(time + AUTHFACES_MOUTH_AHEAD, duration - 0.01f) : time;
        uint16_t before;
        uint16_t shape = LIPSYNC_MouthAt(record, ahead, &start, &before);
        *current = MouthPose(shape);
        *previous = MouthPose(before);
        *alpha = Clamp((ahead - start) / (shape == 0 ? AUTHFACES_MOUTH_CLOSE : AUTHFACES_MOUTH_OPEN), 0.0f, 1.0f);
        return true;
    }
    return false;
}

// The eyelids of a face as the game moves them (at 0x44 of its FACE component; code at 0x0C0BBB60, 0x0C0BC38A and
// 0x0C0903EC): blinking by itself, closed or opened by Eyelids, held while the face is frozen
typedef struct {
    int32_t mode;       // Of Eyelids; 4 or 5 once a mode 1 to 3 is applied: the eyes stay as they are
    int32_t transition; // AUTHFACES_TRANSITION_*
    bool    closed;     // Where the eyes go
    int32_t lock;       // Argument 3 of Eyelids: at 2 or more the eyes stay as they are after the transition
    int32_t counter;    // Frames since the last blink, up to interval
    int32_t interval;
    int32_t frames;     // Of the transition
    int32_t step;
    float   from;
    float   value;      // 0 open, 1 closed
    bool    frozen;
} AUTHFaces_Eyelids;

enum { AUTHFACES_TRANSITION_NONE, AUTHFACES_TRANSITION_START, AUTHFACES_TRANSITION_RUNNING, AUTHFACES_TRANSITION_LOCKED };

// The random numbers of the game (0x0C1CFE80), shared by all the characters: 0 to 0x7FFF
static uint32_t GameRandom(uint32_t* state) {
    *state = *state * 1103515245u + 12345u;
    return (*state >> 16) & 0x7FFF;
}

static int32_t BlinkInterval(uint32_t* random) {
    return AUTHFACES_BLINK_MIN + AUTHFACES_BLINK_STEP * (int32_t)(GameRandom(random) & 3);
}

// A call of Eyelids (0x0C0BC38A), also made with the mode 0 when the character is created
static void EyelidsCall(AUTHFaces_Eyelids* e, int32_t mode, int32_t frames, int32_t lock, uint32_t* random) {
    e->mode = mode;
    e->transition = AUTHFACES_TRANSITION_START;
    e->lock = lock;
    e->counter = 0;
    e->frames = mode == SCNCUTSCENE_EYELIDS_AUTO ? AUTHFACES_EYELIDS_AUTO : frames < 1 ? 1 : frames;
    e->interval = BlinkInterval(random);
}

// A change of the expression (FaceExpression, AUTH command 4) moves the whole face again, the eyes included
static void EyelidsExpression(AUTHFaces_Eyelids* e, int32_t frames) {
    if (e->lock >= 2)
        return;
    if (e->transition == AUTHFACES_TRANSITION_NONE)
        e->frames = frames < 1 ? 1 : frames;
    e->transition = AUTHFACES_TRANSITION_START;
}

// A frame of the game: the eyes blink when the counter reaches the interval (closing in 2 frames, then opening in 4),
// and go linearly from where they are to where they go over the frames of the transition
static void EyelidsUpdate(AUTHFaces_Eyelids* e, uint32_t* random) {
    if (e->frozen) {
        e->transition = AUTHFACES_TRANSITION_NONE;
        return;
    }
    if (e->mode == SCNCUTSCENE_EYELIDS_AUTO) {
        if (e->transition == AUTHFACES_TRANSITION_NONE) {
            if (!e->closed && ++e->counter >= e->interval) {
                e->interval = BlinkInterval(random);
                e->counter = 0;
            }
            if (e->counter == 0) {
                e->transition = AUTHFACES_TRANSITION_START;
                e->closed = !e->closed;
                e->frames = e->closed ? AUTHFACES_BLINK_CLOSE : AUTHFACES_BLINK_OPEN;
            }
        }
    }
    else if (e->mode >= SCNCUTSCENE_EYELIDS_CLOSE && e->mode <= SCNCUTSCENE_EYELIDS_CLOSED) {
        e->closed = e->mode != SCNCUTSCENE_EYELIDS_OPEN;
        e->mode = e->mode == SCNCUTSCENE_EYELIDS_CLOSED ? 5 : 4;
        e->transition = AUTHFACES_TRANSITION_START;
    }
    if (e->transition == AUTHFACES_TRANSITION_START) {
        e->transition = AUTHFACES_TRANSITION_RUNNING;
        e->from = e->value;
        e->step = 0;
    }
    if (e->transition == AUTHFACES_TRANSITION_RUNNING) {
        e->step++;
        e->value = Lerp(e->from, e->closed ? 1.0f : 0.0f, fminf((float)e->step / (float)e->frames, 1.0f));
        if (e->step >= e->frames)
            e->transition = e->lock >= 2 ? AUTHFACES_TRANSITION_LOCKED : AUTHFACES_TRANSITION_NONE;
    }
}

static int32_t CharacterOf(uint32_t idValue) {
    for (uint32_t c = 0; c < g_auth.sequence.characterCount; c++) {
        if (g_auth.sequence.characters[c].idValue == idValue)
            return (int32_t)c;
    }
    return -1;
}

// The eyelids of all the faces at every frame of the cutscene, frame by frame as the game does (they share the random
// numbers): the calls of the program of the map and the AUTH commands of a frame come before its update
static void SimulateEyelids(void) {
    uint32_t count = g_auth.sequence.characterCount, frames = g_auth.sequence.duration + 1;
    AUTHFaces_Eyelids* states = calloc(count, sizeof(AUTHFaces_Eyelids));
    if (states == NULL)
        return;
    uint32_t random = AUTHFACES_RANDOM_SEED;
    for (uint32_t c = 0; c < count; c++) {
        AUTHView_Face* face = &g_auth.faces[c];
        face->eyelidFrames = face->loaded ? malloc(frames * sizeof(float)) : NULL;
        if (face->eyelidFrames != NULL)
            EyelidsCall(&states[c], SCNCUTSCENE_EYELIDS_AUTO, 0, 1, &random);
    }
    const SCNCutscene* program = &g_auth.program;
    uint32_t p = 0, a = 0;
    for (uint32_t f = 0; f < frames; f++) {
        for (; p < program->faceCount && program->faces[p].frame <= f; p++) {
            const SCNCutscene_Face* call = &program->faces[p];
            int32_t c = CharacterOf(call->character);
            if (c < 0 || g_auth.faces[c].eyelidFrames == NULL)
                continue;
            AUTHFaces_Eyelids* e = &states[c];
            if (call->function == SCNCUTSCENE_EYELIDS && (call->known & 3) == 3)
                EyelidsCall(e, (int32_t)call->values[0], (int32_t)call->values[1],
                    (call->known & 4) ? (int32_t)call->values[2] : 0, &random);
            else if (call->function == SCNCUTSCENE_EXPRESSION && (call->known & 7) == 7)
                EyelidsExpression(e, (int32_t)call->values[2]);
            else if (call->function == SCNCUTSCENE_FACE && (call->known & 1)) {
                e->frozen = call->values[0] == 0;
                e->transition = AUTHFACES_TRANSITION_NONE;
            }
        }
        for (; a < g_auth.sequence.commandCount && g_auth.sequence.commands[a].frame <= f; a++) {
            const AUTH_Command* command = &g_auth.sequence.commands[a];
            int32_t c = command->opcode == AUTH_COMMAND_EXPRESSION ? CharacterOf(Arg(command, 0)) : -1;
            if (c >= 0 && g_auth.faces[c].eyelidFrames != NULL)
                EyelidsExpression(&states[c], (int32_t)Arg(command, 3));
        }
        for (uint32_t c = 0; c < count; c++) {
            if (g_auth.faces[c].eyelidFrames == NULL)
                continue;
            EyelidsUpdate(&states[c], &random);
            g_auth.faces[c].eyelidFrames[f] = states[c].value;
        }
    }
    free(states);
    g_auth.eyelidsSimulated = true;
}

// How closed the eyes of a face are at a frame (0 open, 1 closed), between the frames of the game
static float EyelidsAt(const AUTHView_Face* face, float frame) {
    if (!g_auth.eyelidsSimulated)
        SimulateEyelids();
    if (face->eyelidFrames == NULL)
        return 0.0f;
    uint32_t last = g_auth.sequence.duration;
    uint32_t f = (uint32_t)fminf(fmaxf(frame, 0.0f), (float)last);
    float t = Clamp(frame - (float)f, 0.0f, 1.0f);
    return Lerp(face->eyelidFrames[f], face->eyelidFrames[f < last ? f + 1 : last], t);
}

// The frame at which the face of a character is: the frame of the last call of FaceAnimation that froze it, if it is
// frozen, else the frame
static float FaceFrame(uint32_t idValue, float frame, bool* frozen) {
    const SCNCutscene* program = &g_auth.program;
    float at = -1.0f;
    for (uint32_t i = 0; i < program->faceCount && (float)program->faces[i].frame <= frame; i++) {
        const SCNCutscene_Face* call = &program->faces[i];
        if (call->function == SCNCUTSCENE_FACE && call->character == idValue && (call->known & 1))
            at = call->values[0] == 0 ? (float)call->frame : -1.0f;
    }
    *frozen = at >= 0.0f;
    return *frozen ? at : frame;
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

// Limits of the rotation of an eye in degrees, from the FTBL file: around Y (both eyes) and around Z (its own)
static void EyeLimits(const AUTHView_Face* face, bool left, float limits[4]) {
    const int32_t* l = face->skin.eyeLimits;
    if (face->skin.vertexCount == 0) {
        limits[0] = limits[2] = AUTHFACES_EYE_LIMIT;
        limits[1] = limits[3] = -AUTHFACES_EYE_LIMIT;
        return;
    }
    limits[0] = (float)l[0] * 360.0f / 65536.0f;
    limits[1] = (float)l[1] * 360.0f / 65536.0f;
    limits[2] = (float)l[left ? 4 : 2] * 360.0f / 65536.0f;
    limits[3] = (float)l[left ? 5 : 3] * 360.0f / 65536.0f;
}

// The angles of an eye (its node of the face model, whose parent is in a pose) that look at a point of the map, in
// degrees: around its Y axis (down, the top of the head is Z) and its Z axis (to the left of the face, +Y), within its
// limits. The eye looks to its X axis
static Vector2 EyeAngles(const AUTHView_Face* face, const MT5Mesh_Node* node, Matrix parent, Matrix transform, const float point[3]) {
    Matrix world = MatrixMultiply(parent, transform);
    Vector3 local = Vector3Subtract(Vector3Transform((Vector3) { point[0], point[1], point[2] }, MatrixInvert(world)), node->position);
    float limits[4];
    EyeLimits(face, (node->id & 0xFFFF) == AUTHFACES_EYE_LEFT, limits);
    float down = atan2f(-local.z, sqrtf(local.x * local.x + local.y * local.y)) * RAD2DEG;
    float side = atan2f(local.y, local.x) * RAD2DEG;
    return (Vector2) { Clamp(down, limits[1], limits[0]), Clamp(side, limits[3], limits[2]) };
}

// Where the eyes look by a call of EyesLook of the program of the map: the front (modes 0 and 1), or a point
static Vector2 CallAngles(const AUTHView_Face* face, const SCNCutscene_Face* call, const MT5Mesh_Node* node, Matrix parent,
    Matrix transform) {
    if (call == NULL || call->values[0] != SCNCUTSCENE_EYES_POINT || !(call->known & 8))
        return (Vector2) { 0.0f, 0.0f };
    return EyeAngles(face, node, parent, transform, call->point);
}

// The angles of an eye at a frame: from where the previous call of EyesLook for the character makes it look to where
// the last one does, over the frames of the last one (the eyes follow the point)
static Vector2 EyeAt(const AUTHView_Face* face, uint32_t idValue, float frame, const MT5Mesh_Node* node, Matrix parent, Matrix transform) {
    const SCNCutscene* program = &g_auth.program;
    const SCNCutscene_Face* last = NULL, * before = NULL;
    for (uint32_t i = 0; i < program->faceCount && (float)program->faces[i].frame <= frame; i++) {
        const SCNCutscene_Face* call = &program->faces[i];
        if (call->function == SCNCUTSCENE_EYES && call->character == idValue && (call->known & 1)) {
            before = last;
            last = call;
        }
    }
    Vector2 to = CallAngles(face, last, node, parent, transform);
    if (last == NULL)
        return to;
    float frames = last->values[0] == SCNCUTSCENE_EYES_POINT ? (float)last->values[2] : AUTHFACES_FRONT_FRAMES;
    float t = Clamp((frame - (float)last->frame) / fmaxf(frames, 1.0f), 0.0f, 1.0f);
    return Vector2Lerp(CallAngles(face, before, node, parent, transform), to, t);
}

MT5Render_Model* AUTHFaces_Pose(uint32_t character, const Matrix* body, Matrix transform) {
    AUTHView_Face* face = g_auth.faces != NULL ? &g_auth.faces[character] : NULL;
    if (face == NULL || !face->loaded)
        return NULL;
    // The face on the head, its eyes where the program makes them look, and the node of the body that the face joins. A
    // frozen face stays as it was when it was frozen
    uint32_t idValue = g_auth.sequence.characters[character].idValue;
    float frame = FaceFrame(idValue, fmaxf(g_auth.time * AUTH_FRAME_RATE, 0.0f), &face->frozen);
    const MT5Mesh* mesh = &face->model.mesh;
    face->pose[0] = body[face->head];
    for (uint32_t i = 1; i < mesh->nodeCount; i++) {
        int32_t parent = mesh->nodes[i].parent;
        MT5Mesh_Node node = mesh->nodes[i];
        Matrix parentWorld = face->pose[parent >= 0 ? parent : 0];
        uint32_t id = node.id & 0xFFFF;
        if (id == AUTHFACES_EYE_RIGHT || id == AUTHFACES_EYE_LEFT) {
            Vector2 angles = EyeAt(face, idValue, frame, &node, parentWorld, transform);
            node.rotation = (Vector3) { 0.0f, angles.x, angles.y };
            face->eyes[id == AUTHFACES_EYE_LEFT ? 1 : 0] = angles;
        }
        face->pose[i] = MatrixMultiply(MT5Mesh_NodeMatrix(&node), parentWorld);
    }
    face->pose[mesh->nodeCount] = body[face->neck];
    if (face->set == NULL) {
        MT5Render_Pose(&face->model, face->pose);
        return &face->model;
    }

    // The expression, the mouth (by the lip-sync while the character speaks, else the one of the last change of
    // expression, or rest if a voice ended after it), and the eyelids
    AUTHFaces_Change last, before, mouthChange, mouthBefore;
    ExpressionAt(idValue, frame, &last, &before, &mouthChange, &mouthBefore);
    int32_t previous = before.expression, current = last.expression, mouthFrom, mouthTo;
    float expressionAlpha = Clamp((frame - last.frame) / last.frames, 0.0f, 1.0f), mouthAlpha, ended;
    if (!MouthAt(idValue, frame, &mouthFrom, &mouthTo, &mouthAlpha, &ended)) {
        bool rest = ended >= mouthChange.frame;
        mouthFrom = rest || ended >= mouthBefore.frame || mouthBefore.mouth < 0 ? 0 : mouthBefore.mouth;
        mouthTo = rest || mouthChange.mouth < 0 ? 0 : mouthChange.mouth;
        mouthAlpha = rest ? 1.0f : Clamp((frame - mouthChange.frame) / mouthChange.frames, 0.0f, 1.0f);
    }
    float blink = EyelidsAt(face, frame);

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
    face->eyelids = blink;
    return &face->model;
}
