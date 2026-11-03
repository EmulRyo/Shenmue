#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include "raygui.h"
#include "formats/animation/auth.h"
#include "formats/animation/motn.h"
#include "render/freeCamera.h"
#include "render/scene3D.h"
#include "ui/toolbar.h"
#include "ui/tree.h"
#include "views/authView.h"

#define AUTHVIEW_LABEL_SIZE     128
#define AUTHVIEW_LIST_WIDTH     460.0f
#define AUTHVIEW_TIMELINE_HEIGHT 64.0f
#define AUTHVIEW_FOVY           45.0f
#define AUTHVIEW_ASPECT         (4.0f / 3.0f) // Of the screen of the game, for the camera of the cutscene
#define AUTHVIEW_HEIGHT         1.7f    // Of the characters, drawn as cylinders
#define AUTHVIEW_RADIUS         0.25f
#define AUTHVIEW_SOUND_TIME     0.5f    // Seconds a sound with no length is shown

// Groups of the tree: the characters, then the commands in the lanes of the timeline
typedef enum {
    GROUP_CHARACTERS,
    GROUP_MOTIONS,
    GROUP_VOICES,
    GROUP_SOUNDS,
    GROUP_OTHER,
    GROUP_COUNT,
} Group;

static const char* g_groupNames[GROUP_COUNT] = { "Characters", "Motions", "Voices", "Sounds", "Other" };

typedef struct {
    bool            open;
    char            path[1024];  // Of the open file
    AUTH_Sequence   sequence;
    uint32_t        bankCount;
    MOTN_Bank*      banks;      // MOTN files of the folder, for the names of the motions
    char            (*labels)[AUTHVIEW_LABEL_SIZE]; // Of the commands
    char            (*characterLabels)[AUTHVIEW_LABEL_SIZE];
    bool*           shownCharacters; // Their eye in the tree
    Tree            tree;
    int             current;    // Command of the current frame in the tree, to follow it while playing
    float           time;       // Seconds
    bool            playing;
    bool            seeking;    // Dragging on the timeline
    Vector3         center;     // Of the scene
    float           radius;
    FreeCamera     camera;     // Free camera
} AUTHView_State;

// Kept when another cutscene is opened
typedef struct {
    bool            cutsceneCamera; // Show the scene from the camera of the cutscene
    bool            fly;
    bool            expanded[GROUP_COUNT];
    bool            charactersShown;
} AUTHView_Options;

static AUTHView_State g_view;
static AUTHView_Options g_options = {
    .cutsceneCamera = true,
    .expanded = { true, true, true, true, true },
    .charactersShown = true,
};

static const Color g_characterColors[] = {
    { 230, 120, 80, 255 }, { 90, 170, 255, 255 }, { 120, 210, 110, 255 }, { 230, 200, 80, 255 },
    { 200, 120, 230, 255 }, { 80, 210, 200, 255 }, { 240, 140, 170, 255 }, { 170, 170, 170, 255 },
};

static const Color g_cameraColor = { 255, 255, 255, 255 };
static const Color g_motionColor = { 90, 140, 220, 255 };
static const Color g_voiceColor = { 240, 160, 60, 255 };
static const Color g_soundColor = { 110, 200, 110, 255 };
static const Color g_otherColor = { 170, 120, 220, 255 };

#define COLOR_COUNT (sizeof(g_characterColors) / sizeof(g_characterColors[0]))

static float Duration(void) {
    return g_view.sequence.duration / AUTH_FRAME_RATE;
}

static uint32_t CurrentFrame(void) {
    return (uint32_t)(g_view.time * AUTH_FRAME_RATE + 0.001f);
}

static Color CharacterColor(int character) {
    return character >= 0 ? g_characterColors[character % COLOR_COUNT] : LIGHTGRAY;
}

static uint32_t Arg(const AUTH_Command* command, uint32_t arg) {
    return arg < command->argCount ? command->args[arg] : 0;
}

static const char* IDText(uint32_t id) {
    char text[5];
    memcpy(text, &id, 4);
    text[4] = '\0';
    for (int i = 0; i < 4; i++) {
        if (text[i] < 0x20 || text[i] > 0x7E)
            return TextFormat("0x%08X", id);
    }
    return TextFormat("%s", text);
}

// File name of a sound path, without the extension
static const char* SoundName(uint32_t sound) {
    if (sound >= g_view.sequence.soundCount)
        return TextFormat("sound %u", sound);
    return GetFileNameWithoutExt(g_view.sequence.sounds[sound]);
}

static const char* MotionName(uint32_t id) {
    for (uint32_t i = 0; i < g_view.bankCount; i++) {
        const char* name = MOTN_FindName(&g_view.banks[i], id);
        if (name != NULL)
            return name;
    }
    return NULL;
}

// Seconds a voice or a sound is heard
static float CommandLength(const AUTH_Command* command) {
    if (command->opcode == AUTH_COMMAND_VOICE)
        return AUTH_ArgFloat(command, 4);
    if (command->opcode == AUTH_COMMAND_SOUND && command->argCount >= 9)
        return AUTH_ArgFloat(command, 8);
    return AUTHVIEW_SOUND_TIME;
}

static const char* CommandText(const AUTH_Command* command) {
    const char* id = IDText(Arg(command, 0));
    switch (command->opcode) {
    case AUTH_COMMAND_START:
        return "Start";
    case AUTH_COMMAND_CHARACTER:
        return TextFormat("Character %s = %u", id, Arg(command, 1));
    case AUTH_COMMAND_MOTION: {
        const char* name = MotionName(Arg(command, 1));
        return TextFormat("Motion %s 0x%X %u-%u%s%s", id, Arg(command, 1), Arg(command, 2), Arg(command, 3),
            name != NULL ? " " : "", name != NULL ? name : "");
    }
    case AUTH_COMMAND_UNKNOWN:
        return TextFormat("Unknown %s %u %u %u", id, Arg(command, 1), Arg(command, 2), Arg(command, 3));
    case AUTH_COMMAND_VOICE:
        return TextFormat("Voice %s %s %.2f s", id, SoundName(Arg(command, 1)), AUTH_ArgFloat(command, 4));
    case AUTH_COMMAND_SOUND:
        if (command->argCount >= 9)
            return TextFormat("Sound %s %s 0x%08X to %s %.2f s", id, SoundName(Arg(command, 2)), Arg(command, 1),
                IDText(Arg(command, 3)), AUTH_ArgFloat(command, 8));
        return TextFormat("Sound %s %s 0x%08X", id, SoundName(Arg(command, 2)), Arg(command, 1));
    default:
        return TextFormat("Command %u (%u arguments)", command->opcode, command->argCount);
    }
}

static void LoadMotionNames(const char* filepath) {
    FilePathList files = LoadDirectoryFilesEx(GetDirectoryPath(filepath), ".motn", false);
    g_view.banks = calloc(files.count + 1, sizeof(MOTN_Bank));
    if (g_view.banks != NULL) {
        for (unsigned int i = 0; i < files.count; i++) {
            if (MOTN_LoadNames(files.paths[i], &g_view.banks[g_view.bankCount]))
                g_view.bankCount++;
        }
    }
    UnloadDirectoryFiles(files);
}

// The frame and the text of the commands; the ID and the keys of the characters
static bool CreateLabels(void) {
    const AUTH_Sequence* sequence = &g_view.sequence;
    g_view.labels = malloc((sequence->commandCount + 1) * sizeof(*g_view.labels));
    g_view.characterLabels = malloc((sequence->characterCount + 1) * sizeof(*g_view.characterLabels));
    g_view.shownCharacters = malloc((sequence->characterCount + 1) * sizeof(bool));
    if (g_view.labels == NULL || g_view.characterLabels == NULL || g_view.shownCharacters == NULL)
        return false;
    for (uint32_t i = 0; i < sequence->commandCount; i++) {
        const AUTH_Command* command = &sequence->commands[i];
        snprintf(g_view.labels[i], AUTHVIEW_LABEL_SIZE, "%5u  %s", command->frame, CommandText(command));
    }
    for (uint32_t i = 0; i < sequence->characterCount; i++) {
        const AUTH_Character* character = &sequence->characters[i];
        uint32_t keys = character->curves[AUTH_CHARACTER_X].count;
        snprintf(g_view.characterLabels[i], AUTHVIEW_LABEL_SIZE, keys > 0 ? "%s  %u keys" : "%s  no movement", character->id, keys);
        g_view.shownCharacters[i] = true;
    }
    return true;
}

static void AddPoint(Vector3 point, Vector3* min, Vector3* max) {
    *min = Vector3Min(*min, point);
    *max = Vector3Max(*max, point);
}

// Box around the keys of the camera and of the characters
static void ComputeBounds(void) {
    const AUTH_Sequence* sequence = &g_view.sequence;
    Vector3 min = { INFINITY, INFINITY, INFINITY }, max = { -INFINITY, -INFINITY, -INFINITY };
    for (int c = 0; c <= AUTH_CAMERA_TARGET_X; c += AUTH_CAMERA_TARGET_X) {
        const AUTH_Curve* curves = &sequence->camera[c];
        for (uint32_t k = 0; k < curves[0].count; k++) {
            float time = curves[0].times[k];
            AddPoint((Vector3) { AUTH_CurveValue(&curves[0], time, 0.0f), AUTH_CurveValue(&curves[1], time, 0.0f),
                AUTH_CurveValue(&curves[2], time, 0.0f) }, &min, &max);
        }
    }
    for (uint32_t i = 0; i < sequence->characterCount; i++) {
        const AUTH_Curve* curves = sequence->characters[i].curves;
        for (uint32_t k = 0; k < curves[AUTH_CHARACTER_X].count; k++) {
            float time = curves[AUTH_CHARACTER_X].times[k];
            AddPoint((Vector3) { AUTH_CurveValue(&curves[AUTH_CHARACTER_X], time, 0.0f),
                AUTH_CurveValue(&curves[AUTH_CHARACTER_Y], time, 0.0f), AUTH_CurveValue(&curves[AUTH_CHARACTER_Z], time, 0.0f) },
                &min, &max);
        }
    }
    if (min.x > max.x) {
        min = (Vector3) { -1.0f, 0.0f, -1.0f };
        max = (Vector3) { 1.0f, 2.0f, 1.0f };
    }
    g_view.center = Vector3Scale(Vector3Add(min, max), 0.5f);
    g_view.radius = fmaxf(Vector3Distance(min, max) * 0.5f, 2.0f);
}

static void ResetCamera(void) {
    FreeCamera_Reset(&g_view.camera, g_view.center, (Vector3) { sinf(0.6f), 0.0f, cosf(0.6f) }, g_view.radius, AUTHVIEW_FOVY);
    g_view.camera.pitch = 0.6f;
}

void AUTHView_Close(void) {
    if (g_view.open)
        g_options.fly = g_view.camera.fly;
    AUTH_Free(&g_view.sequence);
    for (uint32_t i = 0; i < g_view.bankCount; i++)
        MOTN_Free(&g_view.banks[i]);
    free(g_view.banks);
    free(g_view.labels);
    free(g_view.characterLabels);
    free(g_view.shownCharacters);
    memset(&g_view, 0, sizeof(g_view));
}

bool AUTHView_Open(const char* filepath) {
    AUTH_Sequence sequence;
    if (!AUTH_Load(filepath, &sequence))
        return false;

    AUTHView_Close();
    snprintf(g_view.path, sizeof(g_view.path), "%s", filepath);
    g_view.sequence = sequence;
    g_view.current = -1;
    LoadMotionNames(filepath);
    if (!CreateLabels()) {
        fputs("[AUTHView_Open]: Memory error\n", stderr);
        AUTHView_Close();
        return false;
    }
    ComputeBounds();
    ResetCamera();
    FreeCamera_SetFly(&g_view.camera, g_options.fly);
    g_view.playing = true;

    printf("%s: cutscene, %u frames, %u characters, %u commands, %u sounds, %u MOTN files in the folder\n", GetFileName(g_view.path),
        sequence.duration, sequence.characterCount, sequence.commandCount, sequence.soundCount, g_view.bankCount);
    g_view.open = true;
    return true;
}

bool AUTHView_IsOpen(void) {
    return g_view.open;
}

// Index of the last command at or before the current frame, -1 if none
static int CurrentCommand(void) {
    uint32_t frame = CurrentFrame();
    int current = -1;
    for (uint32_t i = 0; i < g_view.sequence.commandCount && g_view.sequence.commands[i].frame <= frame; i++)
        current = (int)i;
    return current;
}

static void Seek(float time) {
    g_view.time = Clamp(time, 0.0f, Duration());
}

static void SeekFrame(uint32_t frame) {
    Seek(frame / AUTH_FRAME_RATE);
}

// Goes to the frame of the previous (direction -1) or next (1) commands
static void SeekCommand(int direction) {
    uint32_t frame = CurrentFrame();
    const AUTH_Sequence* sequence = &g_view.sequence;
    if (direction > 0) {
        for (uint32_t i = 0; i < sequence->commandCount; i++) {
            if (sequence->commands[i].frame > frame) {
                SeekFrame(sequence->commands[i].frame);
                return;
            }
        }
        Seek(Duration());
    }
    else {
        for (uint32_t i = sequence->commandCount; i-- > 0;) {
            if (sequence->commands[i].frame < frame) {
                SeekFrame(sequence->commands[i].frame);
                return;
            }
        }
        Seek(0.0f);
    }
}

static void TogglePlay(void) {
    if (!g_view.playing && g_view.time >= Duration())
        g_view.time = 0.0f;
    g_view.playing = !g_view.playing;
}

static Vector3 CurvePoint(const AUTH_Curve* curves, float time) {
    return (Vector3) { AUTH_CurveValue(&curves[0], time, 0.0f), AUTH_CurveValue(&curves[1], time, 0.0f),
        AUTH_CurveValue(&curves[2], time, 0.0f) };
}

// The camera of the cutscene at the current time, with its roll
static Camera3D CutsceneCamera(void) {
    return Scene3D_CurveCamera(g_view.sequence.camera, g_view.time, AUTHVIEW_FOVY);
}

void AUTHView_Toolbar(void) {
    if (Toolbar_Button(g_view.playing ? ICON_PLAYER_PAUSE : ICON_PLAYER_PLAY, NULL, "Play or pause", KEY_SPACE))
        TogglePlay();
    if (Toolbar_Button(ICON_PLAYER_PREVIOUS, NULL, "Go to the start", KEY_HOME))
        Seek(0.0f);
    if (Toolbar_Button(ICON_ARROW_LEFT_FILL, NULL, "Previous command", TOOLBAR_REPEAT | KEY_LEFT))
        SeekCommand(-1);
    if (Toolbar_Button(ICON_ARROW_RIGHT_FILL, NULL, "Next command", TOOLBAR_REPEAT | KEY_RIGHT))
        SeekCommand(1);
    Toolbar_Separator();
    int mode = g_options.cutsceneCamera ? 1 : 0;
    if (Toolbar_Choice("Free camera;Cutscene camera", "Show the scene from the free camera or from the camera of the cutscene", KEY_C, &mode))
        g_options.cutsceneCamera = mode == 1;
    if (Toolbar_Button(ICON_RESTART, "Reset", "Reset the free camera", KEY_R)) {
        ResetCamera();
        g_options.cutsceneCamera = false;
    }
    bool fly = !g_options.cutsceneCamera && g_view.camera.fly;
    if (Toolbar_Toggle(ICON_TARGET_MOVE, "Fly", "Fly with the free camera, from the camera of the cutscene if it is shown", KEY_F, &fly)) {
        // Flies from the position of the camera of the cutscene, or of the free camera, looking around it
        if (fly && g_options.cutsceneCamera)
            FreeCamera_LookFrom(&g_view.camera, CutsceneCamera());
        FreeCamera_SetFly(&g_view.camera, fly);
        g_options.cutsceneCamera = false;
    }
}

static void UpdateInput(Rectangle scene) {
    if (!g_options.cutsceneCamera)
        FreeCamera_Update(&g_view.camera, scene, 0.1f);

    if (g_view.playing && !g_view.seeking) {
        g_view.time += Scene3D_FrameTime();
        if (g_view.time >= Duration()) {
            g_view.time = Duration();
            g_view.playing = false;
        }
    }
}

// Time of the first jump (a key with the same time as the next one) of the three curves after a time, INFINITY if none
static float NextJump(const AUTH_Curve* curves, float after) {
    float next = INFINITY;
    for (int c = 0; c < 3; c++) {
        for (uint32_t k = 0; k + 1 < curves[c].count; k++) {
            if (curves[c].times[k] == curves[c].times[k + 1] && curves[c].times[k] > after) {
                next = fminf(next, curves[c].times[k]);
                break;
            }
        }
    }
    return next;
}

// Path of a position over the whole cutscene, one point per frame, broken at the jumps
static void DrawPath(const AUTH_Curve* curves, Color color) {
    if (curves[0].count == 0)
        return;
    float step = 1.0f / AUTH_FRAME_RATE;
    Vector3 previous = CurvePoint(curves, 0.0f);
    float jump = NextJump(curves, 0.0f);
    for (float time = step; time <= Duration() + step * 0.5f; time += step) {
        Vector3 point = CurvePoint(curves, time);
        if (time >= jump)
            jump = NextJump(curves, time);
        else
            DrawLine3D(previous, point, color);
        previous = point;
    }
}

// Direction a character faces: rotation Y of 0 faces -Z
static Vector3 CharacterForward(const AUTH_Character* character) {
    float yaw = AUTH_CurveValue(&character->curves[AUTH_CHARACTER_ROTATION_Y], g_view.time, 0.0f) * DEG2RAD;
    return (Vector3) { -sinf(yaw), 0.0f, -cosf(yaw) };
}

static bool HasMovement(const AUTH_Character* character) {
    return character->curves[AUTH_CHARACTER_X].count > 0 || character->curves[AUTH_CHARACTER_Z].count > 0;
}

// With movement, and shown in the tree
static bool IsShown(uint32_t character) {
    return HasMovement(&g_view.sequence.characters[character]) && g_options.charactersShown && g_view.shownCharacters[character];
}

static void DrawCameraGizmo(void) {
    Camera3D camera = CutsceneCamera();
    Scene3D_DrawCameraGizmo(camera, fmaxf(g_view.radius * 0.15f, 0.5f), AUTHVIEW_ASPECT, true, g_cameraColor);
    DrawLine3D(camera.position, camera.target, Fade(g_cameraColor, 0.3f));
    DrawSphere(camera.target, 0.05f, g_cameraColor);
}

static void DrawGround(void) {
    int half = (int)ceilf(g_view.radius) + 2;
    float x0 = floorf(g_view.center.x), z0 = floorf(g_view.center.z);
    Color color = { 70, 70, 75, 255 };
    for (int i = -half; i <= half; i++) {
        DrawLine3D((Vector3) { x0 + i, 0.0f, z0 - half }, (Vector3) { x0 + i, 0.0f, z0 + half }, color);
        DrawLine3D((Vector3) { x0 - half, 0.0f, z0 + i }, (Vector3) { x0 + half, 0.0f, z0 + i }, color);
    }
}

static void DrawCharacters(void) {
    for (uint32_t i = 0; i < g_view.sequence.characterCount; i++) {
        const AUTH_Character* character = &g_view.sequence.characters[i];
        if (!IsShown(i))
            continue;
        Color color = CharacterColor((int)i);
        Vector3 position = CurvePoint(&character->curves[AUTH_CHARACTER_X], g_view.time);
        DrawCylinder(position, AUTHVIEW_RADIUS, AUTHVIEW_RADIUS, AUTHVIEW_HEIGHT, 12, Fade(color, 0.6f));
        DrawCylinderWires(position, AUTHVIEW_RADIUS, AUTHVIEW_RADIUS, AUTHVIEW_HEIGHT, 12, color);
        Vector3 eyes = Vector3Add(position, (Vector3) { 0.0f, AUTHVIEW_HEIGHT * 0.9f, 0.0f });
        Vector3 front = Vector3Add(eyes, Vector3Scale(CharacterForward(character), 0.6f));
        DrawLine3D(eyes, front, color);
        DrawSphere(front, 0.06f, color);
    }
}

// Is the character speaking, or making a sound, at the current frame: text of the last voice or sound
static const char* CharacterSound(uint32_t idValue) {
    const char* text = NULL;
    float time = g_view.time;
    for (uint32_t i = 0; i < g_view.sequence.commandCount; i++) {
        const AUTH_Command* command = &g_view.sequence.commands[i];
        float start = command->frame / AUTH_FRAME_RATE;
        if (start > time)
            break;
        if (Arg(command, 0) != idValue || time > start + CommandLength(command))
            continue;
        if (command->opcode == AUTH_COMMAND_VOICE)
            text = TextFormat("\"%s\"", SoundName(Arg(command, 1)));
        else if (command->opcode == AUTH_COMMAND_SOUND)
            text = SoundName(Arg(command, 2));
    }
    return text;
}

// Motion of the character at the current frame, NULL if none
static const AUTH_Command* CharacterMotion(uint32_t idValue) {
    const AUTH_Command* motion = NULL;
    uint32_t frame = CurrentFrame();
    for (uint32_t i = 0; i < g_view.sequence.commandCount; i++) {
        const AUTH_Command* command = &g_view.sequence.commands[i];
        if (command->frame > frame)
            break;
        if (command->opcode == AUTH_COMMAND_MOTION && Arg(command, 0) == idValue &&
            frame - command->frame <= Arg(command, 3) - Arg(command, 2))
            motion = command;
    }
    return motion;
}

// Name, sound and motion of the characters, over the 3D view
static void DrawCharacterLabels(Camera3D camera, Rectangle area, Font font, float fontSize) {
    for (uint32_t i = 0; i < g_view.sequence.characterCount; i++) {
        const AUTH_Character* character = &g_view.sequence.characters[i];
        if (!IsShown(i))
            continue;
        Vector3 head = Vector3Add(CurvePoint(&character->curves[AUTH_CHARACTER_X], g_view.time), (Vector3) { 0.0f, AUTHVIEW_HEIGHT + 0.15f, 0.0f });
        Vector2 point;
        if (!Scene3D_ScreenPoint(camera, area, head, &point))
            continue;

        const char* lines[3] = { character->id, CharacterSound(character->idValue), NULL };
        const AUTH_Command* motion = CharacterMotion(character->idValue);
        if (motion != NULL) {
            const char* name = MotionName(Arg(motion, 1));
            lines[2] = name != NULL ? name : TextFormat("motion 0x%X", Arg(motion, 1));
        }
        // Stacked upwards, from a line over the head
        Vector2 bottom = { point.x, point.y - fontSize * 0.2f };
        for (int l = 0; l < 3; l++) {
            if (lines[l] == NULL)
                continue;
            Scene3D_DrawLabel(bottom, lines[l], font, fontSize, l == 0 ? CharacterColor((int)i) : l == 1 ? g_voiceColor : LIGHTGRAY);
            bottom.y -= fontSize * 1.2f;
        }
    }
}

static void DrawScene(Rectangle area, Font font, float fontSize) {
    DrawRectangleRec(area, (Color) { 45, 45, 48, 255 });
    Camera3D camera;
    Rectangle view = area;
    if (g_options.cutsceneCamera) {
        camera = CutsceneCamera();
        view = Scene3D_Letterbox(area, AUTHVIEW_ASPECT);
        FreeCamera_BeginCamera3D(camera, view, 0.05, 1000.0);
    }
    else
        camera = FreeCamera_Begin3D(&g_view.camera, view);

    DrawGround();
    if (!g_options.cutsceneCamera) {
        DrawPath(&g_view.sequence.camera[AUTH_CAMERA_X], Fade(g_cameraColor, 0.5f));
        for (uint32_t i = 0; i < g_view.sequence.characterCount; i++) {
            if (IsShown(i))
                DrawPath(&g_view.sequence.characters[i].curves[AUTH_CHARACTER_X], Fade(CharacterColor((int)i), 0.5f));
        }
        DrawCameraGizmo();
    }
    DrawCharacters();
    FreeCamera_End3D();
    DrawCharacterLabels(camera, view, font, fontSize);
    DrawRectangleLinesEx(view, 1.0f, (Color) { 112, 112, 112, 255 });
}

// Lanes of the timeline: motions, voices, sounds, other commands; the cuts of the camera over them
static void DrawTimeline(Rectangle area, Font font, float fontSize) {
    const AUTH_Sequence* sequence = &g_view.sequence;
    float duration = Duration();
    DrawRectangleRec(area, (Color) { 30, 30, 32, 255 });
    if (duration <= 0.0f)
        return;
    float scale = area.width / duration;
    float laneHeight = area.height / 4.0f;
    for (uint32_t i = 0; i < sequence->commandCount; i++) {
        const AUTH_Command* command = &sequence->commands[i];
        float start = command->frame / AUTH_FRAME_RATE;
        float x = area.x + start * scale;
        Color color = CharacterColor(AUTH_FindCharacter(sequence, Arg(command, 0)));
        switch (command->opcode) {
        case AUTH_COMMAND_MOTION: {
            float length = (Arg(command, 3) - Arg(command, 2)) / AUTH_FRAME_RATE;
            float width = fmaxf(fminf(length, duration - start) * scale, 2.0f);
            DrawRectangleRec((Rectangle) { x, area.y + 2.0f, width, laneHeight - 4.0f }, Fade(color, 0.35f));
            DrawRectangleRec((Rectangle) { x, area.y + 2.0f, 2.0f, laneHeight - 4.0f }, color);
            break;
        }
        case AUTH_COMMAND_VOICE: {
            float width = fmaxf(CommandLength(command) * scale, 2.0f);
            DrawRectangleRec((Rectangle) { x, area.y + laneHeight + 2.0f, width, laneHeight - 4.0f }, Fade(g_voiceColor, 0.6f));
            DrawRectangleRec((Rectangle) { x, area.y + laneHeight + 2.0f, 2.0f, laneHeight - 4.0f }, color);
            break;
        }
        case AUTH_COMMAND_SOUND:
            DrawRectangleRec((Rectangle) { x, area.y + 2.0f * laneHeight + 2.0f, 2.0f, laneHeight - 4.0f }, g_soundColor);
            break;
        default:
            DrawRectangleRec((Rectangle) { x, area.y + 3.0f * laneHeight + 2.0f, 2.0f, laneHeight - 4.0f }, g_otherColor);
            break;
        }
    }
    const AUTH_Curve* camera = &sequence->camera[AUTH_CAMERA_X];
    for (uint32_t k = 0; k + 1 < camera->count; k++) {
        if (camera->times[k] == camera->times[k + 1]) {
            float x = area.x + camera->times[k] * scale;
            DrawLineEx((Vector2) { x, area.y }, (Vector2) { x, area.y + area.height }, 1.0f, Fade(g_cameraColor, 0.4f));
        }
    }
    const char* lanes[4] = { "Motions", "Voices", "Sounds", "Other" };
    for (int i = 0; i < 4; i++)
        DrawTextEx(font, lanes[i], (Vector2) { area.x + 4.0f, area.y + i * laneHeight + (laneHeight - fontSize * 0.8f) * 0.5f },
            fontSize * 0.8f, 1.0f, Fade(LIGHTGRAY, 0.5f));
    float x = area.x + g_view.time * scale;
    DrawLineEx((Vector2) { x, area.y }, (Vector2) { x, area.y + area.height }, 2.0f, RED);
    DrawRectangleLinesEx(area, 1.0f, (Color) { 112, 112, 112, 255 });
}

static void UpdateTimeline(Rectangle area) {
    Vector2 mouse = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, area))
        g_view.seeking = true;
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        g_view.seeking = false;
    if (g_view.seeking && area.width > 0.0f)
        Seek((mouse.x - area.x) / area.width * Duration());
}

static Group CommandGroup(const AUTH_Command* command) {
    switch (command->opcode) {
    case AUTH_COMMAND_MOTION:
        return GROUP_MOTIONS;
    case AUTH_COMMAND_VOICE:
        return GROUP_VOICES;
    case AUTH_COMMAND_SOUND:
        return GROUP_SOUNDS;
    default:
        return GROUP_OTHER;
    }
}

// The characters, whose eye shows or hides them in the scene; a click on one looks at it with the free camera. Then the
// commands by lane of the timeline, with the one of the current frame selected; a click on one goes to its frame. The
// current command is followed while playing or seeking, without expanding its group
static void DrawTree(Rectangle bounds) {
    const AUTH_Sequence* sequence = &g_view.sequence;
    Tree* tree = &g_view.tree;
    int current = CurrentCommand();
    if (current != g_view.current && (g_view.playing || g_view.seeking))
        Tree_Reveal(tree, false);
    g_view.current = current;

    int clickedCharacter = -1, clickedCommand = -1;
    Tree_Begin(tree, bounds);
    Tree_BeginGroup(tree, g_groupNames[GROUP_CHARACTERS], &g_options.expanded[GROUP_CHARACTERS], &g_options.charactersShown);
    for (uint32_t i = 0; i < sequence->characterCount; i++) {
        bool moves = HasMovement(&sequence->characters[i]);
        if (Tree_Item(tree, g_view.characterLabels[i], moves ? 0 : TREE_DIMMED, moves ? &g_view.shownCharacters[i] : NULL) && moves)
            clickedCharacter = (int)i;
    }
    Tree_EndGroup(tree);
    for (int g = GROUP_MOTIONS; g < GROUP_COUNT; g++) {
        Tree_BeginGroup(tree, g_groupNames[g], &g_options.expanded[g], NULL);
        for (uint32_t i = 0; i < sequence->commandCount; i++) {
            if ((int)CommandGroup(&sequence->commands[i]) == g &&
                Tree_Item(tree, g_view.labels[i], (int)i == current ? TREE_SELECTED : 0, NULL))
                clickedCommand = (int)i;
        }
        Tree_EndGroup(tree);
    }
    Tree_End(tree);

    if (clickedCommand >= 0)
        SeekFrame(sequence->commands[clickedCommand].frame);
    if (clickedCharacter >= 0) {
        const AUTH_Character* character = &sequence->characters[clickedCharacter];
        g_options.cutsceneCamera = false;
        g_view.camera.target = Vector3Add(CurvePoint(&character->curves[AUTH_CHARACTER_X], g_view.time),
            (Vector3) { 0.0f, AUTHVIEW_HEIGHT * 0.5f, 0.0f });
        g_view.camera.distance = 6.0f;
    }
}

static const char* FormatTime(float seconds) {
    int centiseconds = (int)(seconds * 100.0f);
    return TextFormat("%d:%02d.%02d", centiseconds / 6000, centiseconds / 100 % 60, centiseconds % 100);
}

void AUTHView_Draw(Rectangle area) {
    if (!g_view.open || area.width <= 0.0f || area.height <= 0.0f)
        return;

    Font font = GuiGetFont();
    float fontSize = (float)GuiGetStyle(DEFAULT, TEXT_SIZE);
    const AUTH_Sequence* sequence = &g_view.sequence;
    float margin = 10.0f;
    float controlsHeight = 30.0f;
    float top = area.y + margin;
    Rectangle list = { area.x + margin, top, AUTHVIEW_LIST_WIDTH, area.height - margin * 2.0f };
    float panelX = list.x + list.width + margin;
    float panelWidth = area.width - list.width - margin * 3.0f;
    Rectangle controls = { panelX, area.y + area.height - margin - controlsHeight, panelWidth, controlsHeight };
    Rectangle timeline = { panelX, controls.y - margin - AUTHVIEW_TIMELINE_HEIGHT, panelWidth, AUTHVIEW_TIMELINE_HEIGHT };
    Rectangle scene = { panelX, top, panelWidth, timeline.y - margin - top };
    if (list.height <= 0.0f || panelWidth <= 0.0f || scene.height <= 0.0f)
        return;
    UpdateTimeline(timeline);
    UpdateInput(scene);

    DrawRectangleRec(area, (Color) { 45, 45, 48, 255 });

    DrawTree(list);

    DrawScene(scene, font, fontSize);
    DrawTimeline(timeline, font, fontSize);

    float x = controls.x;
    // TextFormat uses a rotating buffer, so the two times can be formatted in the same call
    const char* status = TextFormat("Frame %u/%u   %s / %s", CurrentFrame(), sequence->duration, FormatTime(g_view.time),
        FormatTime(Duration()));
    DrawTextEx(font, status, (Vector2) { x, controls.y + (controlsHeight - fontSize) * 0.5f }, fontSize, 1.0f, LIGHTGRAY);
}

void AUTHView_Status(ViewStatus* status) {
    const AUTH_Sequence* sequence = &g_view.sequence;
    snprintf(status->file, sizeof(status->file), "%s", g_view.path);
    snprintf(status->info, sizeof(status->info), "%s, %u characters, %u commands, %u sounds", FormatTime(Duration()),
        sequence->characterCount, sequence->commandCount, sequence->soundCount);
    if (g_options.cutsceneCamera)
        snprintf(status->hint, sizeof(status->hint), "Click the timeline: seek");
    else
        snprintf(status->hint, sizeof(status->hint), "%s", FreeCamera_Help(&g_view.camera));
}
