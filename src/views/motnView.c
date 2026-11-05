#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include "raygui.h"
#include "formats/animation/motn.h"
#include "render/freeCamera.h"
#include "render/mt5Pose.h"
#include "render/mt5Render.h"
#include "render/scene3D.h"
#include "ui/properties.h"
#include "ui/toolbar.h"
#include "ui/tree.h"
#include "views/motnView.h"

#define MOTNVIEW_PATH_SIZE      1024
#define MOTNVIEW_LABEL_SIZE     128
#define MOTNVIEW_LIST_WIDTH     460.0f
#define MOTNVIEW_TIMELINE_HEIGHT 24.0f
#define MOTNVIEW_FOVY           45.0f
#define MOTNVIEW_RYO            "model/chara/ykb_m.mt5" // In the folder of the scene
#define MOTNVIEW_RYO_PREFIX     "AKI_"

typedef enum {
    GROUP_MOTIONS,
    GROUP_MODELS,
    GROUP_COUNT,
} Group;

static const char* g_groupNames[GROUP_COUNT] = { "Motions", "Models" };

typedef struct {
    bool            open;
    char            path[MOTNVIEW_PATH_SIZE];
    MOTN_Bank       bank;
    char            (*labels)[MOTNVIEW_LABEL_SIZE]; // Of the motions
    int             selected;       // Motion, -1 if none
    MOTN_Motion     motion;         // The selected one
    bool            decoded;
    float           frame;
    bool            playing;
    bool            seeking;
    // The models that can show the motions: those of the folder, and Ryo
    uint32_t        modelCount;
    char            (*modelPaths)[MOTNVIEW_PATH_SIZE];
    int             model;          // Loaded one, -1 if none
    bool            modelLoaded;
    MT5Render_Model render3D;
    bool            body;           // The model is the body of a character
    MT5Pose_Body    bodyNodes;
    Matrix*         pose;
    bool            posed;          // The model is in the pose of the motion at the current frame
    bool            centered;       // The camera looks at the hips of the selected motion
    MT5Render       render;
    FreeCamera      camera;
    Tree            tree;
    Properties      properties;
} MOTNView_State;

// Kept when another file is opened
typedef struct {
    bool            fly;
    bool            follow;     // The camera follows the hips
    bool            targets;    // Draw the positions of the motion
    bool            expanded[GROUP_COUNT];
} MOTNView_Options;

static MOTNView_State g_view;
static MOTNView_Options g_options = { .follow = true, .expanded = { true, true } };

static const Color g_targetColor = { 255, 200, 60, 255 };

static float LastFrame(void) {
    return g_view.decoded ? (float)g_view.motion.frames : 0.0f;
}

static bool EqualsIgnoreCase(const char* a, const char* b) {
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
    }
    return *a == *b;
}

// Folder of the scene that contains the file (e.g. .../scene/01 for .../scene/01/d000/auth/M_01TE.motn): the folder
// whose parent is named "scene". Empty if the file isn't in a scene folder
static void FindSceneFolder(const char* filepath, char* folder, size_t size) {
    char path[MOTNVIEW_PATH_SIZE];
    snprintf(path, sizeof(path), "%s", filepath);
    for (char* c = path; *c != '\0'; c++) {
        if (*c == '\\')
            *c = '/';
    }
    folder[0] = '\0';
    for (char* slash = strrchr(path, '/'); slash != NULL; slash = strrchr(path, '/')) {
        *slash = '\0';
        char* parent = strrchr(path, '/');
        if (parent != NULL && EqualsIgnoreCase(parent + 1, "scene")) {
            snprintf(folder, size, "%s/%s", path, slash + 1);
            return;
        }
    }
}

static void FreeModel(void) {
    if (g_view.modelLoaded)
        MT5Render_FreeModel(&g_view.render3D);
    free(g_view.pose);
    g_view.pose = NULL;
    g_view.modelLoaded = false;
    g_view.body = false;
    g_view.posed = false;
    g_view.model = -1;
}

static void OpenModel(int index) {
    FreeModel();
    g_view.model = index;
    g_view.modelLoaded = MT5Render_LoadModel(g_view.modelPaths[index], &g_view.render3D);
    if (!g_view.modelLoaded)
        return;
    g_view.body = MT5Pose_FindBody(&g_view.render3D.mesh, &g_view.bodyNodes);
    g_view.pose = malloc((g_view.render3D.mesh.nodeCount + 1) * sizeof(Matrix));
}

// The models of the folder of the file, then Ryo
static bool FindModels(void) {
    FilePathList files = LoadDirectoryFilesEx(GetDirectoryPath(g_view.path), ".mt5;.chrm", false);
    g_view.modelPaths = calloc(files.count + 2, sizeof(*g_view.modelPaths));
    if (g_view.modelPaths == NULL) {
        UnloadDirectoryFiles(files);
        return false;
    }
    for (unsigned int i = 0; i < files.count; i++)
        snprintf(g_view.modelPaths[g_view.modelCount++], MOTNVIEW_PATH_SIZE, "%s", files.paths[i]);
    UnloadDirectoryFiles(files);
    char scene[MOTNVIEW_PATH_SIZE];
    FindSceneFolder(g_view.path, scene, sizeof(scene));
    if (scene[0] != '\0') {
        char ryo[MOTNVIEW_PATH_SIZE];
        snprintf(ryo, sizeof(ryo), "%s/%s", scene, MOTNVIEW_RYO);
        if (FileExists(ryo))
            snprintf(g_view.modelPaths[g_view.modelCount++], MOTNVIEW_PATH_SIZE, "%s", ryo);
    }
    return true;
}

static bool IsRyo(int model) {
    return model >= 0 && EqualsIgnoreCase(GetFileName(g_view.modelPaths[model]), GetFileName(MOTNVIEW_RYO));
}

// Ryo for his motions, else the first body of the folder, else Ryo
static void ChooseModel(void) {
    int ryo = -1;
    for (uint32_t i = 0; i < g_view.modelCount; i++) {
        if (IsRyo((int)i))
            ryo = (int)i;
    }
    const char* name = g_view.selected >= 0 ? g_view.bank.names[g_view.selected] : "";
    if (ryo >= 0 && strncmp(name, MOTNVIEW_RYO_PREFIX, strlen(MOTNVIEW_RYO_PREFIX)) == 0) {
        if (g_view.model != ryo)
            OpenModel(ryo);
        return;
    }
    if (g_view.body && !IsRyo(g_view.model))
        return;
    for (uint32_t i = 0; i < g_view.modelCount; i++) {
        if (IsRyo((int)i))
            continue;
        OpenModel((int)i);
        if (g_view.body)
            return;
    }
    if (ryo >= 0)
        OpenModel(ryo);
}

static void SelectMotion(int index) {
    if (g_view.decoded)
        MOTN_FreeMotion(&g_view.motion);
    g_view.selected = index;
    g_view.decoded = index >= 0 && MOTN_ReadMotion(&g_view.bank, (uint32_t)index, &g_view.motion);
    g_view.frame = 0.0f;
    g_view.playing = true;
    g_view.centered = false;
}

static void ResetCamera(void) {
    FreeCamera_Reset(&g_view.camera, (Vector3) { 0.0f, 0.9f, 0.0f }, (Vector3) { 0.0f, 0.0f, -1.0f }, 2.0f, MOTNVIEW_FOVY);
    g_view.camera.yaw += 0.6f;
    g_view.camera.pitch = 0.25f;
    g_view.centered = false;
}

void MOTNView_Close(void) {
    if (g_view.open) {
        g_options.fly = g_view.camera.fly;
        MT5Render_Free(&g_view.render);
    }
    FreeModel();
    if (g_view.decoded)
        MOTN_FreeMotion(&g_view.motion);
    MOTN_Free(&g_view.bank);
    free(g_view.labels);
    free(g_view.modelPaths);
    memset(&g_view, 0, sizeof(g_view));
}

bool MOTNView_Open(const char* filepath) {
    MOTN_Bank bank;
    if (!MOTN_Load(filepath, &bank))
        return false;
    MOTNView_Close();
    g_view.bank = bank;
    g_view.model = -1;
    snprintf(g_view.path, sizeof(g_view.path), "%s", filepath);
    g_view.labels = malloc((bank.count + 1) * sizeof(*g_view.labels));
    if (g_view.labels == NULL || !FindModels()) {
        fputs("[MOTNView_Open]: Memory error\n", stderr);
        MOTNView_Close();
        return false;
    }
    for (uint32_t i = 0; i < bank.count; i++) {
        MOTN_Motion motion;
        bool valid = MOTN_ReadMotion(&bank, i, &motion);
        snprintf(g_view.labels[i], MOTNVIEW_LABEL_SIZE, "%3u  %s%s", i, bank.names[i], valid ? TextFormat("  %u", motion.frames) : "  (invalid)");
        if (valid)
            MOTN_FreeMotion(&motion);
    }
    printf("%s: %u motions (IDs 0x%X-0x%X), %u models to show them\n", GetFileName(filepath), bank.count, bank.firstID + 1,
        bank.firstID + bank.count, g_view.modelCount);
    MT5Render_Init(&g_view.render);
    g_view.open = true;
    SelectMotion(bank.count > 0 ? 0 : -1);
    ChooseModel();
    ResetCamera();
    FreeCamera_SetFly(&g_view.camera, g_options.fly);
    return true;
}

bool MOTNView_IsOpen(void) {
    return g_view.open;
}

static void Seek(float frame) {
    g_view.frame = Clamp(frame, 0.0f, LastFrame());
}

static void StepMotion(int direction) {
    if (g_view.bank.count == 0)
        return;
    int next = g_view.selected + direction;
    if (next < 0 || next >= (int)g_view.bank.count)
        return;
    SelectMotion(next);
    ChooseModel();
    Tree_Reveal(&g_view.tree, true);
}

void MOTNView_Toolbar(void) {
    if (Toolbar_Button(g_view.playing ? ICON_PLAYER_PAUSE : ICON_PLAYER_PLAY, NULL, "Play or pause", KEY_SPACE))
        g_view.playing = !g_view.playing;
    if (Toolbar_Button(ICON_PLAYER_PREVIOUS, NULL, "Go to the start", KEY_HOME))
        Seek(0.0f);
    if (Toolbar_Button(ICON_ARROW_LEFT_FILL, NULL, "Previous frame", TOOLBAR_REPEAT | KEY_LEFT)) {
        g_view.playing = false;
        Seek(floorf(g_view.frame - 0.5f));
    }
    if (Toolbar_Button(ICON_ARROW_RIGHT_FILL, NULL, "Next frame", TOOLBAR_REPEAT | KEY_RIGHT)) {
        g_view.playing = false;
        Seek(floorf(g_view.frame + 1.0f));
    }
    if (Toolbar_Button(ICON_ARROW_UP_FILL, NULL, "Previous motion", TOOLBAR_REPEAT | KEY_UP))
        StepMotion(-1);
    if (Toolbar_Button(ICON_ARROW_DOWN_FILL, NULL, "Next motion", TOOLBAR_REPEAT | KEY_DOWN))
        StepMotion(1);
    Toolbar_Separator();
    if (Toolbar_Button(ICON_RESTART, "Reset", "Reset the camera", KEY_R))
        ResetCamera();
    bool fly = g_view.camera.fly;
    if (Toolbar_Toggle(ICON_TARGET_MOVE, "Fly", "Fly with the camera", KEY_F, &fly))
        FreeCamera_SetFly(&g_view.camera, fly);
    Toolbar_Toggle(ICON_TARGET, "Follow", "The camera follows the character", KEY_G, &g_options.follow);
    Toolbar_Toggle(ICON_TARGET_POINT, "Targets", "Show the positions of the motion: hips, head, hands and feet", KEY_T, &g_options.targets);
}

// Position of the hips at the current frame
static Vector3 Hips(void) {
    Vector3 hips = { 0.0f, 0.9f, 0.0f };
    if (!g_view.decoded)
        return hips;
    const MOTN_Channel* x = MOTN_FindChannel(&g_view.motion, 0, MOTN_POSITION_X);
    const MOTN_Channel* z = MOTN_FindChannel(&g_view.motion, 0, MOTN_POSITION_Z);
    hips.x = x != NULL ? MOTN_ChannelValue(x, g_view.frame) : 0.0f;
    hips.z = z != NULL ? MOTN_ChannelValue(z, g_view.frame) : 0.0f;
    return hips;
}

static void UpdateInput(Rectangle scene) {
    if (!g_view.centered && !g_view.camera.fly) {
        Vector3 hips = Hips();
        g_view.camera.target.x = hips.x;
        g_view.camera.target.z = hips.z;
    }
    g_view.centered = true;
    Vector3 before = Hips();
    if (g_view.playing && !g_view.seeking && g_view.decoded) {
        // Loops
        g_view.frame += Scene3D_FrameTime() * MOTN_FRAME_RATE;
        if (LastFrame() <= 0.0f)
            g_view.frame = 0.0f;
        else if (g_view.frame > LastFrame())
            g_view.frame = fmodf(g_view.frame, LastFrame());
    }
    if (g_options.follow && !g_view.camera.fly) {
        Vector3 after = Hips();
        g_view.camera.target.x += after.x - before.x;
        g_view.camera.target.z += after.z - before.z;
    }
    FreeCamera_Update(&g_view.camera, scene, 0.1f);
}

static void UpdatePose(void) {
    bool posed = g_view.modelLoaded && g_view.body && g_view.decoded && g_view.pose != NULL &&
        MT5Pose_Compute(&g_view.render3D.mesh, &g_view.bodyNodes, &g_view.motion, g_view.frame, g_view.pose);
    if (posed)
        MT5Render_Pose(&g_view.render3D, g_view.pose);
    else if (g_view.modelLoaded)
        MT5Render_Pose(&g_view.render3D, NULL);
    g_view.posed = posed;
}

static void DrawGround(Vector3 center) {
    Color color = { 70, 70, 75, 255 };
    float x0 = floorf(center.x), z0 = floorf(center.z);
    for (int i = -4; i <= 4; i++) {
        DrawLine3D((Vector3) { x0 + i, 0.0f, z0 - 4 }, (Vector3) { x0 + i, 0.0f, z0 + 4 }, color);
        DrawLine3D((Vector3) { x0 - 4, 0.0f, z0 + i }, (Vector3) { x0 + 4, 0.0f, z0 + i }, color);
    }
}

static void DrawTargets(void) {
    if (!g_view.decoded)
        return;
    for (uint32_t i = 0; i < g_view.motion.channelCount; i++) {
        const MOTN_Channel* channel = &g_view.motion.channels[i];
        if (channel->axis != MOTN_POSITION_X && (channel->axis != MOTN_POSITION_Y || (i > 0 && g_view.motion.channels[i - 1].bone == channel->bone)))
            continue;
        uint32_t bone = channel->bone;
        float values[3] = { 0 };
        for (int axis = 0; axis < 3; axis++) {
            const MOTN_Channel* c = MOTN_FindChannel(&g_view.motion, bone, (MOTN_Axis)axis);
            values[axis] = c != NULL ? MOTN_ChannelValue(c, g_view.frame) : 0.0f;
        }
        DrawSphere((Vector3) { values[0], values[1], values[2] }, 0.025f, g_targetColor);
    }
}

static void DrawScene(Rectangle area) {
    DrawRectangleRec(area, (Color) { 45, 45, 48, 255 });
    UpdatePose();
    Camera3D camera = FreeCamera_Begin3D(&g_view.camera, area);
    DrawGround(Hips());
    if (g_view.modelLoaded) {
        MT5Render_Instance instance = { &g_view.render3D, MatrixIdentity(), g_view.render3D.mesh.center, 0 };
        MT5Render_DrawInstances(&g_view.render, &instance, 1, camera);
    }
    if (g_options.targets) {
        // Over the model: the batch of the spheres is drawn before the depth test is enabled again
        rlDrawRenderBatchActive();
        rlDisableDepthTest();
        DrawTargets();
        rlDrawRenderBatchActive();
        rlEnableDepthTest();
    }
    FreeCamera_End3D();
    DrawRectangleLinesEx(area, 1.0f, (Color) { 112, 112, 112, 255 });
}

static void UpdateTimeline(Rectangle area) {
    Vector2 mouse = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, area))
        g_view.seeking = true;
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        g_view.seeking = false;
    if (g_view.seeking && area.width > 0.0f)
        Seek((mouse.x - area.x) / area.width * LastFrame());
}

// The frames of the keys of the motion, and the current frame
static void DrawTimeline(Rectangle area) {
    DrawRectangleRec(area, (Color) { 30, 30, 32, 255 });
    float last = LastFrame();
    if (last > 0.0f) {
        for (uint32_t i = 0; i < g_view.motion.channelCount; i++) {
            const MOTN_Channel* channel = &g_view.motion.channels[i];
            for (uint32_t k = 1; k + 1 < channel->keyCount; k++) {
                float x = area.x + channel->keys[k].frame / last * area.width;
                DrawLineEx((Vector2) { x, area.y + 4.0f }, (Vector2) { x, area.y + area.height - 4.0f }, 1.0f, Fade(LIGHTGRAY, 0.05f));
            }
        }
        float x = area.x + g_view.frame / last * area.width;
        DrawLineEx((Vector2) { x, area.y }, (Vector2) { x, area.y + area.height }, 2.0f, RED);
    }
    DrawRectangleLinesEx(area, 1.0f, (Color) { 112, 112, 112, 255 });
}

static void DrawTree(Rectangle bounds) {
    Tree* tree = &g_view.tree;
    int clickedMotion = -1, clickedModel = -1;
    Tree_Begin(tree, bounds);
    Tree_BeginGroup(tree, g_groupNames[GROUP_MOTIONS], &g_options.expanded[GROUP_MOTIONS], NULL);
    for (uint32_t i = 0; i < g_view.bank.count; i++) {
        if (Tree_Item(tree, g_view.labels[i], (int)i == g_view.selected ? TREE_SELECTED : 0, NULL))
            clickedMotion = (int)i;
    }
    Tree_EndGroup(tree);
    Tree_BeginGroup(tree, g_groupNames[GROUP_MODELS], &g_options.expanded[GROUP_MODELS], NULL);
    for (uint32_t i = 0; i < g_view.modelCount; i++) {
        bool loaded = (int)i == g_view.model;
        int flags = (loaded ? TREE_SELECTED : 0) | (loaded && !g_view.modelLoaded ? TREE_FAILED : 0) | (loaded && g_view.modelLoaded && !g_view.body ? TREE_DIMMED : 0);
        const char* name = GetFileName(g_view.modelPaths[i]);
        if (Tree_Item(tree, IsRyo((int)i) ? TextFormat("%s (Ryo)", name) : name, flags, NULL))
            clickedModel = (int)i;
    }
    Tree_EndGroup(tree);
    Tree_End(tree);

    if (clickedMotion >= 0) {
        SelectMotion(clickedMotion);
        // Ryo for his motions, the chosen model for the rest
        if (IsRyo(g_view.model) || strncmp(g_view.bank.names[clickedMotion], MOTNVIEW_RYO_PREFIX, strlen(MOTNVIEW_RYO_PREFIX)) == 0)
            ChooseModel();
    }
    if (clickedModel >= 0 && clickedModel != g_view.model)
        OpenModel(clickedModel);
}

static const char* g_kinds[] = { "Keyframes", "Sampled", "Raw" };

static void DrawProperties(Rectangle bounds) {
    Properties* properties = &g_view.properties;
    if (g_view.selected < 0) {
        Properties_Begin(properties, bounds, NULL);
        Properties_Note(properties, "The file has no motions");
        Properties_End(properties);
        return;
    }
    const MOTN_Motion* motion = &g_view.motion;
    Properties_Begin(properties, bounds, TextFormat("Motion %d", g_view.selected));
    Properties_Text(properties, "Name", g_view.bank.names[g_view.selected]);
    Properties_Hex(properties, "ID", g_view.bank.firstID + 1 + (uint32_t)g_view.selected);
    if (!g_view.decoded)
        Properties_Note(properties, "Its data is not valid");
    else {
        Properties_Text(properties, "Kind", g_kinds[motion->kind]);
        if (motion->kind != MOTN_SAMPLED)
            Properties_Int(properties, "Character type", motion->type);
        Properties_Int(properties, "Frames", motion->frames);
        Properties_Text(properties, "Duration", TextFormat("%.2f s", motion->frames / MOTN_FRAME_RATE));
        Properties_Text(properties, "Frame", TextFormat("%.1f", g_view.frame));
        if (motion->kind == MOTN_KEYFRAME) {
            Properties_Int(properties, "Channels", motion->channelCount);
            Properties_Section(properties, "Bones");
            for (uint32_t i = 0; i < motion->channelCount;) {
                uint32_t bone = motion->channels[i].bone;
                // e.g. "position X Y Z, rotation X Y Z"
                char axes[64] = { 0 };
                int length = 0;
                bool rotation = false;
                for (; i < motion->channelCount && motion->channels[i].bone == bone; i++) {
                    uint8_t axis = motion->channels[i].axis;
                    const char* kind = length == 0 ? (axis >= MOTN_ROTATION_X ? "rotation " : "position ") :
                        axis >= MOTN_ROTATION_X && !rotation ? ", rotation " : " ";
                    rotation = axis >= MOTN_ROTATION_X;
                    length += snprintf(axes + length, sizeof(axes) - length, "%s%c", kind, "XYZXYZ"[axis]);
                }
                Properties_Text(properties, TextFormat("Bone %u", bone), axes);
            }
        }
    }
    Properties_Section(properties, "Model");
    if (g_view.model < 0)
        Properties_Note(properties, "No model in the folder");
    else {
        Properties_Text(properties, "File", GetFileName(g_view.modelPaths[g_view.model]));
        Properties_Bool(properties, "Body", g_view.body);
        Properties_Text(properties, "Pose", g_view.posed ? "of the motion" : !g_view.modelLoaded ? "invalid model file" :
            !g_view.body ? "of the file: not a body" : "of the file: the motion can't pose a body");
    }
    Properties_End(properties);
}

void MOTNView_Draw(Rectangle area) {
    if (!g_view.open || area.width <= 0.0f || area.height <= 0.0f)
        return;
    Font font = GuiGetFont();
    float fontSize = (float)GuiGetStyle(DEFAULT, TEXT_SIZE);
    float margin = 10.0f;
    float controlsHeight = 30.0f;
    float top = area.y + margin;
    Rectangle column = { area.x + margin, top, MOTNVIEW_LIST_WIDTH, area.height - margin * 2.0f };
    Rectangle list, properties;
    Properties_Split(column, &list, &properties);
    float panelX = column.x + column.width + margin;
    float panelWidth = area.width - column.width - margin * 3.0f;
    Rectangle controls = { panelX, area.y + area.height - margin - controlsHeight, panelWidth, controlsHeight };
    Rectangle timeline = { panelX, controls.y - margin - MOTNVIEW_TIMELINE_HEIGHT, panelWidth, MOTNVIEW_TIMELINE_HEIGHT };
    Rectangle scene = { panelX, top, panelWidth, timeline.y - margin - top };
    if (list.height <= 0.0f || panelWidth <= 0.0f || scene.height <= 0.0f)
        return;
    UpdateTimeline(timeline);
    UpdateInput(scene);

    DrawRectangleRec(area, (Color) { 45, 45, 48, 255 });
    DrawTree(list);
    DrawProperties(properties);
    DrawScene(scene);
    DrawTimeline(timeline);
    const char* status = TextFormat("Frame %.0f/%.0f   %.2f / %.2f s", floorf(g_view.frame), LastFrame(), g_view.frame / MOTN_FRAME_RATE,
        LastFrame() / MOTN_FRAME_RATE);
    DrawTextEx(font, status, (Vector2) { controls.x, controls.y + (controlsHeight - fontSize) * 0.5f }, fontSize, 1.0f, LIGHTGRAY);
}

void MOTNView_Status(ViewStatus* status) {
    snprintf(status->file, sizeof(status->file), "%s", g_view.path);
    snprintf(status->info, sizeof(status->info), "%u motions, IDs 0x%X-0x%X%s%s", g_view.bank.count, g_view.bank.firstID + 1,
        g_view.bank.firstID + g_view.bank.count, g_view.selected >= 0 ? ", " : "", g_view.selected >= 0 ? g_view.bank.names[g_view.selected] : "");
    snprintf(status->hint, sizeof(status->hint), "%s", FreeCamera_Help(&g_view.camera));
}
