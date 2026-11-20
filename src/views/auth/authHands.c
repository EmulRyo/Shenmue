// The detailed hands of the characters of a cutscene: the hand models (_TL, _TR) of a character replace the hands of its
// body (nodes 0xFFBE and 0xFFBF) while the program of the map shows them (HandMotion, 0x0DF), and their skin (HM file)
// is posed with the motions of the HMOT files of the map, or at rest
#include "core/log.h"
#include "views/auth/authViewInternal.h"

#define AUTHHANDS_FOLDER    "model/hand"

static const char* g_handSuffixes[AUTHVIEW_HAND_SIDES] = { "tl", "tr" };

// The HMOT files of the folder of the map
static void LoadMotions(void) {
    if (g_auth.handMotionsLoaded)
        return;
    g_auth.handMotionsLoaded = true;
    FilePathList files = LoadDirectoryFilesEx(GetDirectoryPath(g_auth.mapinfoPath), ".bin", false);
    for (unsigned int i = 0; i < files.count; i++) {
        const char* name = GetFileName(files.paths[i]);
        if (!Text_EqualsIgnoreCase(TextSubtext(name, 0, 4), "hmot"))
            continue;
        HMOT_File* grown = realloc(g_auth.handMotions, (g_auth.handMotionCount + 1) * sizeof(HMOT_File));
        if (grown == NULL)
            break;
        g_auth.handMotions = grown;
        if (HMOT_Load(files.paths[i], &g_auth.handMotions[g_auth.handMotionCount]))
            Log_Debug("    hand motions %s: %u\n", name, g_auth.handMotions[g_auth.handMotionCount++].motionCount);
    }
    UnloadDirectoryFiles(files);
}

void AUTHHands_FreeMotions(void) {
    for (uint32_t i = 0; i < g_auth.handMotionCount; i++)
        HMOT_Free(&g_auth.handMotions[i]);
    free(g_auth.handMotions);
    g_auth.handMotions = NULL;
    g_auth.handMotionCount = 0;
    g_auth.handMotionsLoaded = false;
}

static const HMOT_Motion* FindMotion(uint32_t id) {
    for (uint32_t i = 0; i < g_auth.handMotionCount; i++) {
        const HMOT_Motion* motion = HMOT_Find(&g_auth.handMotions[i], id);
        if (motion != NULL)
            return motion;
    }
    return NULL;
}

// A hand model of the character, which replaces a node of the body (the parent of node is the node of the body whose
// mesh its negative vertex indices use)
static bool LoadHand(AUTHView_Hand* hand, const char* path, const MT5_Model* body, int32_t node) {
    int32_t arm = (uint32_t)node < body->nodeCount ? MT5_ParentMeshNode(body, body->nodes[node].parent) : -1;
    MT5_Model source;
    if (arm < 0 || !FileExists(path) || !MT5_Load(path, &source))
        return false;
    bool loaded = source.nodeCount > 0 && source.nodes[0].vertexCount > 0;
    if (loaded) {
        hand->vertexCount = source.nodes[0].vertexCount;
        hand->positions = malloc(hand->vertexCount * 3 * sizeof(float));
        hand->offsets = calloc(hand->vertexCount * 3, sizeof(float));
        loaded = hand->positions != NULL && hand->offsets != NULL;
        for (uint32_t v = 0; loaded && v < hand->vertexCount; v++)
            memcpy(&hand->positions[v * 3], source.nodes[0].vertices[v].position, 3 * sizeof(float));
    }
    MT5_Free(&source);
    loaded = loaded && MT5Render_LoadPart(path, &body->nodes[arm], &hand->model);
    if (loaded) {
        hand->pose = malloc((hand->model.mesh.nodeCount + 2) * sizeof(Matrix));
        loaded = hand->pose != NULL;
    }
    if (!loaded) {
        if (hand->model.mesh.nodeCount > 0)
            MT5Render_FreeModel(&hand->model);
        free(hand->positions);
        free(hand->offsets);
        free(hand->pose);
        memset(hand, 0, sizeof(*hand));
        return false;
    }
    hand->loaded = true;
    hand->node = node;
    hand->arm = arm;
    return true;
}

bool AUTHHands_Load(uint32_t character, const char* body, const IPAC_File* packed) {
    AUTHView_Model* model = &g_auth.models[g_auth.characterModels[character].model];
    AUTHView_Hands* result = &g_auth.hands[character];
    if (!model->body || body == NULL || strlen(GetFileName(body)) < 3 || g_auth.sceneFolder[0] == '\0')
        return false;
    char name[4], path[AUTHVIEW_PATH_SIZE];
    snprintf(name, sizeof(name), "%.3s", TextToLower(GetFileName(body)));
    snprintf(path, sizeof(path), "%s/%s/%s_hm.bin", g_auth.sceneFolder, AUTHHANDS_FOLDER, name);
    if (!FileExists(path) || !HM_Load(path, &result->skin))
        return false;
    MT5_Model source;
    if (!(packed != NULL ? MT5_LoadFromMemory(packed->data, packed->size, packed->filename, &source) : MT5_Load(body, &source))) {
        HM_Free(&result->skin);
        return false;
    }
    for (int side = 0; side < AUTHVIEW_HAND_SIDES; side++) {
        snprintf(path, sizeof(path), "%s/%s/%s_%s.mt5", g_auth.sceneFolder, AUTHHANDS_FOLDER, name, g_handSuffixes[side]);
        int32_t node = model->bodyNodes.arms[side][3];
        if (node >= 0)
            LoadHand(&result->hands[side], path, &source, node);
    }
    MT5_Free(&source);
    result->hidden = calloc(model->model.mesh.nodeCount + 1, sizeof(bool));
    if (result->hidden == NULL || (!result->hands[0].loaded && !result->hands[1].loaded)) {
        AUTHHands_Free(character);
        return false;
    }
    result->loaded = true;
    snprintf(result->name, sizeof(result->name), "%s", name);
    LoadMotions();
    Log_Debug("    hands %s%s%s\n", name, result->hands[0].loaded ? " left" : "", result->hands[1].loaded ? " right" : "");
    return true;
}

void AUTHHands_Free(uint32_t character) {
    AUTHView_Hands* hands = &g_auth.hands[character];
    for (int side = 0; side < AUTHVIEW_HAND_SIDES; side++) {
        AUTHView_Hand* hand = &hands->hands[side];
        if (hand->loaded)
            MT5Render_FreeModel(&hand->model);
        free(hand->positions);
        free(hand->offsets);
        free(hand->pose);
    }
    HM_Free(&hands->skin);
    free(hands->hidden);
    memset(hands, 0, sizeof(*hands));
}

// The frame of a motion played by a call at a frame of the cutscene (code at 0x0C0DEADA and 0x0C0DDEFC of 1ST_READ.BIN):
// from the current frame of the call between its first and last ones, in a loop or stopping at the end
static float MotionFrame(const SCNCutscene_Hand* call, const HMOT_Motion* motion, float frame) {
    int32_t frames = motion->frames > 0 ? motion->frames : 1;
    int32_t first = call->first - 1, last = call->last - 1, current = call->current - 1;
    first = first < 0 ? 0 : first >= frames ? frames - 1 : first;
    last = last < 0 || last >= frames ? frames - 1 : last;
    if (first > last)
        first = last = 0;
    bool back = (call->flags & SCNCUTSCENE_HAND_BACK) != 0;
    if (current < first || current > last)
        current = back ? last : first;
    float m = (float)current + (frame - (float)call->frame) * call->speed * (back ? -1.0f : 1.0f);
    if (call->flags & SCNCUTSCENE_HAND_LOOP) {
        float span = (float)(last - first + 1);
        m = (float)first + fmodf(fmodf(m - (float)first, span) + span, span);
        return fminf(m, (float)last);
    }
    return Clamp(m, (float)first, (float)last);
}

// Whether a hand of a character is shown at a frame, and the call that plays its motion (NULL if none)
static bool HandAt(uint32_t idValue, uint32_t side, float frame, const SCNCutscene_Hand** motionCall) {
    const SCNCutscene* program = &g_auth.program;
    bool shown = false;
    *motionCall = NULL;
    for (uint32_t i = 0; i < program->handCount && (float)program->hands[i].frame <= frame; i++) {
        const SCNCutscene_Hand* call = &program->hands[i];
        if (call->character != idValue || call->side != side)
            continue;
        if (call->function == SCNCUTSCENE_HAND)
            shown = call->on > 0;
        *motionCall = call;
    }
    return shown;
}

static bool IsUnder(const MT5Mesh* mesh, int32_t node, int32_t ancestor) {
    for (int32_t n = node; n >= 0 && (uint32_t)n < mesh->nodeCount; n = mesh->nodes[n].parent) {
        if (n == ancestor)
            return true;
    }
    return false;
}

const bool* AUTHHands_HiddenNodes(uint32_t character, const bool* faceHidden) {
    AUTHView_Hands* hands = g_auth.hands != NULL ? &g_auth.hands[character] : NULL;
    if (hands == NULL || !hands->loaded)
        return faceHidden;
    const MT5Mesh* body = &g_auth.models[g_auth.characterModels[character].model].model.mesh;
    uint32_t idValue = g_auth.sequence.characters[character].idValue;
    float frame = fmaxf(g_auth.time * AUTH_FRAME_RATE, 0.0f);
    bool any = false;
    for (int side = 0; side < AUTHVIEW_HAND_SIDES; side++) {
        AUTHView_Hand* hand = &hands->hands[side];
        const SCNCutscene_Hand* call;
        hand->shown = hand->loaded && HandAt(idValue, (uint32_t)side, frame, &call);
        hand->motion = hand->shown && call != NULL ? FindMotion(call->motion) : NULL;
        hand->motionFrame = hand->motion != NULL ? MotionFrame(call, hand->motion, frame) : 0.0f;
        any |= hand->shown;
    }
    if (!any)
        return faceHidden;
    for (uint32_t n = 0; n < body->nodeCount; n++) {
        hands->hidden[n] = faceHidden != NULL && faceHidden[n];
        for (int side = 0; side < AUTHVIEW_HAND_SIDES; side++)
            hands->hidden[n] |= hands->hands[side].shown && IsUnder(body, (int32_t)n, hands->hands[side].node);
    }
    return hands->hidden;
}

uint32_t AUTHHands_Pose(uint32_t character, const Matrix* body, MT5Render_Model* models[AUTHVIEW_HAND_SIDES]) {
    AUTHView_Hands* hands = g_auth.hands != NULL ? &g_auth.hands[character] : NULL;
    uint32_t count = 0;
    if (hands == NULL || !hands->loaded)
        return 0;
    for (int side = 0; side < AUTHVIEW_HAND_SIDES; side++) {
        AUTHView_Hand* hand = &hands->hands[side];
        if (!hand->shown)
            continue;
        // The angles of the motion at its frame, or of the rest
        static float angles[HM_NODES][3], matrices[HM_NODES][16];
        for (int n = 0; n < HM_NODES; n++) {
            for (int a = 0; a < 3; a++) {
                float rest = hands->skin.nodes[n].angles[a];
                angles[n][a] = hand->motion == NULL ? rest :
                    HMOT_Value(hand->motion, (uint32_t)(n * 3 + a), hand->motionFrame, rest * RAD2DEG) * DEG2RAD;
            }
        }
        HM_Pose(&hands->skin, (const float (*)[3])angles, matrices);
        HM_Deform(&hands->skin, (const float (*)[16])matrices, hand->positions, hand->vertexCount, side == 0, hand->offsets);
        const MT5Mesh* mesh = &hand->model.mesh;
        hand->pose[0] = body[hand->node];
        for (uint32_t i = 1; i < mesh->nodeCount; i++) {
            int32_t parent = mesh->nodes[i].parent;
            hand->pose[i] = MatrixMultiply(MT5Mesh_NodeMatrix(&mesh->nodes[i]), hand->pose[parent >= 0 ? parent : 0]);
        }
        hand->pose[mesh->nodeCount] = body[hand->arm];
        MT5Mesh_PoseOptions options = { NULL, 0, hand->vertexCount, hand->offsets };
        MT5Render_PoseWith(&hand->model, hand->pose, &options);
        models[count++] = &hand->model;
    }
    return count;
}
