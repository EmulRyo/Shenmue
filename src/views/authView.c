#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include "raygui.h"
#include "formats/animation/auth.h"
#include "formats/animation/motn.h"
#include "views/authView.h"

#define AUTHVIEW_LABEL_SIZE     128
#define AUTHVIEW_LIST_WIDTH     460.0f
#define AUTHVIEW_TIMELINE_HEIGHT 64.0f
#define AUTHVIEW_FOVY           45.0f
#define AUTHVIEW_ASPECT         (4.0f / 3.0f) // Of the screen of the game, for the camera of the cutscene
#define AUTHVIEW_HEIGHT         1.7f    // Of the characters, drawn as cylinders
#define AUTHVIEW_RADIUS         0.25f
#define AUTHVIEW_SOUND_TIME     0.5f    // Seconds a sound with no length is shown

typedef struct {
    bool            open;
    char            filename[256];
    AUTH_Sequence   sequence;
    uint32_t        bankCount;
    MOTN_Bank*      banks;      // MOTN files of the folder, for the names of the motions
    char            (*labels)[AUTHVIEW_LABEL_SIZE];
    const char**    labelPointers;
    int             listScroll;
    int             listFocus;
    float           time;       // Seconds
    bool            playing;
    bool            cutsceneCamera; // Show the scene from the camera of the cutscene
    bool            seeking;    // Dragging on the timeline
    Vector3         center;     // Of the scene
    float           radius;
    Vector3         target;     // Free orbit camera
    float           yaw, pitch, distance;
    int             dragButton; // -1 if the mouse isn't dragging
} AUTHView_State;

static AUTHView_State g_view = { .dragButton = -1 };

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

static bool CreateLabels(void) {
    uint32_t count = g_view.sequence.commandCount;
    g_view.labels = malloc((count + 1) * sizeof(*g_view.labels));
    g_view.labelPointers = malloc((count + 1) * sizeof(*g_view.labelPointers));
    if (g_view.labels == NULL || g_view.labelPointers == NULL)
        return false;
    for (uint32_t i = 0; i < count; i++) {
        const AUTH_Command* command = &g_view.sequence.commands[i];
        snprintf(g_view.labels[i], AUTHVIEW_LABEL_SIZE, "%5u  %s", command->frame, CommandText(command));
        g_view.labelPointers[i] = g_view.labels[i];
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
    g_view.target = g_view.center;
    g_view.yaw = 0.6f;
    g_view.pitch = 0.6f;
    g_view.distance = g_view.radius / sinf(AUTHVIEW_FOVY * 0.5f * DEG2RAD);
}

void AUTHView_Close(void) {
    AUTH_Free(&g_view.sequence);
    for (uint32_t i = 0; i < g_view.bankCount; i++)
        MOTN_Free(&g_view.banks[i]);
    free(g_view.banks);
    free(g_view.labels);
    free(g_view.labelPointers);
    memset(&g_view, 0, sizeof(g_view));
    g_view.dragButton = -1;
}

bool AUTHView_Open(const char* filepath) {
    AUTH_Sequence sequence;
    if (!AUTH_Load(filepath, &sequence))
        return false;

    AUTHView_Close();
    snprintf(g_view.filename, sizeof(g_view.filename), "%s", GetFileName(filepath));
    g_view.sequence = sequence;
    g_view.listFocus = -1;
    LoadMotionNames(filepath);
    if (!CreateLabels()) {
        fputs("[AUTHView_Open]: Memory error\n", stderr);
        AUTHView_Close();
        return false;
    }
    ComputeBounds();
    ResetCamera();
    g_view.cutsceneCamera = true;
    g_view.playing = true;

    printf("%s: cutscene, %u frames, %u characters, %u commands, %u sounds, %u MOTN files in the folder\n", g_view.filename,
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

static Vector3 FreeCameraPosition(void) {
    Vector3 direction = {
        cosf(g_view.pitch) * sinf(g_view.yaw),
        sinf(g_view.pitch),
        cosf(g_view.pitch) * cosf(g_view.yaw),
    };
    return Vector3Add(g_view.target, Vector3Scale(direction, g_view.distance));
}

static void UpdateFreeCamera(Rectangle area) {
    bool inside = CheckCollisionPointRec(GetMousePosition(), area);
    if (g_view.dragButton < 0 && inside) {
        const int buttons[3] = { MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_MIDDLE };
        for (int i = 0; i < 3; i++) {
            if (IsMouseButtonPressed(buttons[i]))
                g_view.dragButton = buttons[i];
        }
    }
    if (g_view.dragButton >= 0) {
        Vector2 delta = GetMouseDelta();
        if (!IsMouseButtonDown(g_view.dragButton))
            g_view.dragButton = -1;
        else if (g_view.dragButton == MOUSE_BUTTON_LEFT) {
            g_view.yaw -= delta.x * 0.01f;
            g_view.pitch = Clamp(g_view.pitch + delta.y * 0.01f, -1.55f, 1.55f);
        }
        else {
            // Move the target with the mouse, in the plane of the screen
            Vector3 forward = Vector3Normalize(Vector3Subtract(g_view.target, FreeCameraPosition()));
            Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, (Vector3) { 0.0f, 1.0f, 0.0f }));
            Vector3 up = Vector3CrossProduct(right, forward);
            float scale = 2.0f * g_view.distance * tanf(AUTHVIEW_FOVY * 0.5f * DEG2RAD) / area.height;
            g_view.target = Vector3Add(g_view.target, Vector3Scale(right, -delta.x * scale));
            g_view.target = Vector3Add(g_view.target, Vector3Scale(up, delta.y * scale));
        }
    }
    if (inside) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
            g_view.distance = fmaxf(g_view.distance * powf(0.85f, wheel), 0.1f);
    }
}

static void UpdateInput(Rectangle scene) {
    if (IsKeyPressed(KEY_SPACE))
        TogglePlay();
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT))
        SeekCommand(-1);
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT))
        SeekCommand(1);
    if (IsKeyPressed(KEY_HOME))
        Seek(0.0f);
    if (IsKeyPressed(KEY_V))
        g_view.cutsceneCamera = !g_view.cutsceneCamera;
    if (IsKeyPressed(KEY_R))
        ResetCamera();
    if (!g_view.cutsceneCamera)
        UpdateFreeCamera(scene);

    if (g_view.playing && !g_view.seeking) {
        g_view.time += fminf(GetFrameTime(), 0.25f); // Without skipping frames after a long one (loading)
        if (g_view.time >= Duration()) {
            g_view.time = Duration();
            g_view.playing = false;
        }
    }
}

static Vector3 CurvePoint(const AUTH_Curve* curves, float time) {
    return (Vector3) { AUTH_CurveValue(&curves[0], time, 0.0f), AUTH_CurveValue(&curves[1], time, 0.0f),
        AUTH_CurveValue(&curves[2], time, 0.0f) };
}

// The camera of the cutscene at the current time, with its roll
static Camera3D CutsceneCamera(void) {
    const AUTH_Curve* curves = g_view.sequence.camera;
    Vector3 position = CurvePoint(&curves[AUTH_CAMERA_X], g_view.time);
    Vector3 target = CurvePoint(&curves[AUTH_CAMERA_TARGET_X], g_view.time);
    Vector3 forward = Vector3Normalize(Vector3Subtract(target, position));
    float roll = AUTH_CurveValue(&curves[AUTH_CAMERA_ROLL], g_view.time, 0.0f);
    Vector3 up = Vector3RotateByAxisAngle((Vector3) { 0.0f, 1.0f, 0.0f }, forward, roll * DEG2RAD);
    float fovy = AUTH_CurveValue(&curves[AUTH_CAMERA_FOV], g_view.time, AUTHVIEW_FOVY);
    return (Camera3D) { position, target, up, fovy > 1.0f && fovy < 179.0f ? fovy : AUTHVIEW_FOVY, CAMERA_PERSPECTIVE };
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

static void DrawCameraGizmo(void) {
    Camera3D camera = CutsceneCamera();
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
    Vector3 up = Vector3CrossProduct(right, forward);
    float depth = fmaxf(g_view.radius * 0.15f, 0.5f);
    float halfHeight = depth * tanf(camera.fovy * 0.5f * DEG2RAD);
    float halfWidth = halfHeight * AUTHVIEW_ASPECT;
    Vector3 center = Vector3Add(camera.position, Vector3Scale(forward, depth));
    Vector3 corners[4];
    for (int i = 0; i < 4; i++) {
        float sx = (i == 0 || i == 3) ? -1.0f : 1.0f;
        float sy = i < 2 ? 1.0f : -1.0f;
        corners[i] = Vector3Add(center, Vector3Add(Vector3Scale(right, sx * halfWidth), Vector3Scale(up, sy * halfHeight)));
    }
    for (int i = 0; i < 4; i++) {
        DrawLine3D(camera.position, corners[i], g_cameraColor);
        DrawLine3D(corners[i], corners[(i + 1) % 4], g_cameraColor);
    }
    // Triangle over the top edge, to show the up direction
    Vector3 top = Vector3Add(Vector3Scale(Vector3Add(corners[0], corners[1]), 0.5f), Vector3Scale(up, halfHeight * 0.4f));
    DrawLine3D(corners[0], top, g_cameraColor);
    DrawLine3D(corners[1], top, g_cameraColor);
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
        if (!HasMovement(character))
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

static void Begin3D(Camera3D camera, Rectangle area, double nearPlane, double farPlane) {
    BeginMode3D(camera);
    // Draw only inside the area, with its aspect ratio
    rlViewport((int)area.x, GetRenderHeight() - (int)(area.y + area.height), (int)area.width, (int)area.height);
    rlMatrixMode(RL_PROJECTION);
    rlLoadIdentity();
    double top = nearPlane * tan(camera.fovy * 0.5 * DEG2RAD);
    double right = top * area.width / area.height;
    rlFrustum(-right, right, -top, top, nearPlane, farPlane);
    rlMatrixMode(RL_MODELVIEW);
}

static void End3D(void) {
    EndMode3D();
    rlViewport(0, 0, GetRenderWidth(), GetRenderHeight());
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
    Vector3 forward = Vector3Subtract(camera.target, camera.position);
    for (uint32_t i = 0; i < g_view.sequence.characterCount; i++) {
        const AUTH_Character* character = &g_view.sequence.characters[i];
        if (!HasMovement(character))
            continue;
        Vector3 head = Vector3Add(CurvePoint(&character->curves[AUTH_CHARACTER_X], g_view.time), (Vector3) { 0.0f, AUTHVIEW_HEIGHT + 0.15f, 0.0f });
        if (Vector3DotProduct(Vector3Subtract(head, camera.position), forward) <= 0.0f)
            continue;
        Vector2 point = GetWorldToScreenEx(head, camera, (int)area.width, (int)area.height);
        point = (Vector2) { area.x + point.x, area.y + point.y };
        if (!CheckCollisionPointRec(point, area))
            continue;

        const char* lines[3] = { character->id, CharacterSound(character->idValue), NULL };
        const AUTH_Command* motion = CharacterMotion(character->idValue);
        if (motion != NULL) {
            const char* name = MotionName(Arg(motion, 1));
            lines[2] = name != NULL ? name : TextFormat("motion 0x%X", Arg(motion, 1));
        }
        float y = point.y - fontSize * 1.2f;
        for (int l = 0; l < 3; l++) {
            if (lines[l] == NULL)
                continue;
            Vector2 size = MeasureTextEx(font, lines[l], fontSize, 1.0f);
            Rectangle box = { point.x - size.x * 0.5f - 3.0f, y - 1.0f, size.x + 6.0f, fontSize + 2.0f };
            DrawRectangleRec(box, Fade(BLACK, 0.6f));
            DrawTextEx(font, lines[l], (Vector2) { box.x + 3.0f, y }, fontSize, 1.0f,
                l == 0 ? CharacterColor((int)i) : l == 1 ? g_voiceColor : LIGHTGRAY);
            y -= fontSize * 1.2f;
        }
    }
}

static void DrawScene(Rectangle area, Font font, float fontSize) {
    DrawRectangleRec(area, (Color) { 45, 45, 48, 255 });
    Camera3D camera;
    Rectangle view = area;
    if (g_view.cutsceneCamera) {
        // The screen of the game, letterboxed
        camera = CutsceneCamera();
        view.width = fminf(area.width, area.height * AUTHVIEW_ASPECT);
        view.height = view.width / AUTHVIEW_ASPECT;
        view.x = roundf(area.x + (area.width - view.width) * 0.5f);
        view.y = roundf(area.y + (area.height - view.height) * 0.5f);
        DrawRectangleRec(area, BLACK);
        DrawRectangleRec(view, (Color) { 45, 45, 48, 255 });
        Begin3D(camera, view, 0.05, 1000.0);
    }
    else {
        camera = (Camera3D) { FreeCameraPosition(), g_view.target, { 0.0f, 1.0f, 0.0f }, AUTHVIEW_FOVY, CAMERA_PERSPECTIVE };
        Begin3D(camera, view, g_view.distance * 0.01, g_view.distance + g_view.radius * 4.0);
    }

    DrawGround();
    if (!g_view.cutsceneCamera) {
        DrawPath(&g_view.sequence.camera[AUTH_CAMERA_X], Fade(g_cameraColor, 0.5f));
        for (uint32_t i = 0; i < g_view.sequence.characterCount; i++)
            DrawPath(&g_view.sequence.characters[i].curves[AUTH_CHARACTER_X], Fade(CharacterColor((int)i), 0.5f));
        DrawCameraGizmo();
    }
    DrawCharacters();
    End3D();
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

// Keeps the item visible in the list
static void ScrollTo(int item, Rectangle list) {
    float itemHeight = (float)(GuiGetStyle(LISTVIEW, LIST_ITEMS_HEIGHT) + GuiGetStyle(LISTVIEW, LIST_ITEMS_SPACING));
    int visible = (int)((list.height - 2.0f * GuiGetStyle(DEFAULT, BORDER_WIDTH)) / itemHeight);
    if (visible < 1 || item < 0)
        return;
    if (item < g_view.listScroll)
        g_view.listScroll = item;
    else if (item >= g_view.listScroll + visible)
        g_view.listScroll = item - visible + 1;
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
    float lineHeight = fontSize * 1.2f;
    const AUTH_Sequence* sequence = &g_view.sequence;
    char characters[64] = "";
    for (uint32_t i = 0; i < sequence->characterCount && strlen(characters) + 6 < sizeof(characters); i++)
        snprintf(characters + strlen(characters), sizeof(characters) - strlen(characters), "%s%s", i > 0 ? ", " : "", sequence->characters[i].id);
    const char* lines[3] = {
        g_view.filename,
        TextFormat("Cutscene, %u frames (%s), %u characters (%s), %u commands, %u sounds", sequence->duration,
            FormatTime(Duration()), sequence->characterCount, characters, sequence->commandCount, sequence->soundCount),
        g_view.cutsceneCamera ?
            "Space: play/pause   Left/Right: previous/next command   Home: start   V: free camera   Click on the timeline: seek" :
            "Space: play/pause   Left/Right: previous/next command   Home: start   V: cutscene camera   Left drag: rotate   Right drag: pan   Wheel: zoom   R: reset",
    };

    float margin = 10.0f;
    float controlsHeight = 30.0f;
    float top = area.y + margin * 2.0f + 3 * lineHeight;
    Rectangle list = { area.x + margin, top, AUTHVIEW_LIST_WIDTH, area.height - margin * 3.0f - 3 * lineHeight };
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
    for (int i = 0; i < 3; i++)
        DrawTextEx(font, lines[i], (Vector2) { area.x + margin, area.y + margin + i * lineHeight }, fontSize, 1.0f, LIGHTGRAY);

    int current = CurrentCommand();
    if (g_view.playing || g_view.seeking)
        ScrollTo(current, list);
    int active = current;
    int alignment = GuiGetStyle(LISTVIEW, TEXT_ALIGNMENT);
    GuiSetStyle(LISTVIEW, TEXT_ALIGNMENT, TEXT_ALIGN_LEFT);
    GuiListViewEx(list, g_view.labelPointers, (int)sequence->commandCount, &g_view.listScroll, &active, &g_view.listFocus);
    GuiSetStyle(LISTVIEW, TEXT_ALIGNMENT, alignment);
    if (active >= 0 && active != current)
        SeekFrame(sequence->commands[active].frame);

    DrawScene(scene, font, fontSize);
    DrawTimeline(timeline, font, fontSize);

    float x = controls.x;
    if (GuiButton((Rectangle) { x, controls.y, 100.0f, controlsHeight }, g_view.playing ? "Pause" : "Play"))
        TogglePlay();
    x += 110.0f;
    // The style of the application draws the pressed controls like the normal ones: highlight the selected toggle
    int pressedColor = GuiGetStyle(TOGGLE, BASE_COLOR_PRESSED);
    GuiSetStyle(TOGGLE, BASE_COLOR_PRESSED, 0x2F5F8FFF);
    int mode = g_view.cutsceneCamera ? 1 : 0;
    GuiToggleGroup((Rectangle) { x, controls.y, 140.0f, controlsHeight }, "Free camera;Cutscene camera", &mode);
    GuiSetStyle(TOGGLE, BASE_COLOR_PRESSED, pressedColor);
    g_view.cutsceneCamera = mode == 1;
    x += 2 * 140.0f + GuiGetStyle(TOGGLE, GROUP_PADDING) + margin;
    // TextFormat uses a rotating buffer, so the two times can be formatted in the same call
    const char* status = TextFormat("Frame %u/%u   %s / %s", CurrentFrame(), sequence->duration, FormatTime(g_view.time),
        FormatTime(Duration()));
    DrawTextEx(font, status, (Vector2) { x, controls.y + (controlsHeight - fontSize) * 0.5f }, fontSize, 1.0f, LIGHTGRAY);
}
