#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <raylib.h>
#include <raymath.h>
#include "raygui.h"
#include "core/log.h"
#include "app/gameDB.h"
#include "core/path.h"
#include "core/text.h"
#include "formats/animation/auth.h"
#include "formats/animation/motn.h"
#include "formats/archive/pkf.h"
#include "formats/archive/pks.h"
#include "formats/map/mapinfo.h"
#include "formats/script/chrt.h"
#include "scenario/scnCutscene.h"
#include "render/bounds.h"
#include "render/freeCamera.h"
#include "render/mt5Pose.h"
#include "render/mt5Render.h"
#include "render/scene3D.h"
#include "ui/properties.h"
#include "ui/toolbar.h"
#include "ui/tree.h"
#include "views/auth/authSounds.h"
#include "views/auth/authView.h"
#include "views/auth/authMusic.h"
#include "views/auth/authViewInternal.h"
#include "game/mapProgram.h"
#include "game/mapFiles.h"
#include "views/mapinfo/mapinfoView.h"

#define AUTHVIEW_LIST_WIDTH     460.0f
#define AUTHVIEW_TIMELINE_HEIGHT 64.0f
#define AUTHVIEW_SOUND_TIME     0.5f    // Seconds a sound with no length is shown
#define AUTHVIEW_NEAR           0.05    // Planes of the camera of the cutscene, and of the free camera with the map
#define AUTHVIEW_FAR            2000.0

// Groups of the tree: the characters, then the commands in the lanes of the timeline
typedef enum {
    GROUP_CHARACTERS,
    GROUP_MOTIONS,
    GROUP_VOICES,
    GROUP_SOUNDS,
    GROUP_OTHER,
    GROUP_PROGRAM,
    GROUP_COUNT,
} Group;

static const char* g_groupNames[GROUP_COUNT] = { "Characters", "Motions", "Voices", "Sounds", "Other", "Program" };

// Kept when another cutscene is opened
typedef struct {
    bool            cutsceneCamera; // Show the scene from the camera of the cutscene
    bool            fly;
    bool            expanded[GROUP_COUNT];
    bool            charactersShown;
    bool            labels;         // Name, sound and motion over the characters
    bool            map;            // The models of the map of the cutscene, as its view shows them
    bool            lights;         // The characters are lit by the lights of the map, as the program changes them
} AUTHView_Options;

AUTHView_State g_auth;
static AUTHView_Options g_options = {
    .cutsceneCamera = true,
    .labels = true,
    .map = true,
    .lights = true,
    .expanded = { true, true, true, true, true, true },
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
static const Color g_programColor = { 80, 200, 220, 255 };

#define COLOR_COUNT (sizeof(g_characterColors) / sizeof(g_characterColors[0]))

static void OpenMap(void);

static float Duration(void) {
    return g_auth.sequence.duration / AUTH_FRAME_RATE;
}

static Color CharacterColor(int character) {
    return character >= 0 ? g_characterColors[character % COLOR_COUNT] : LIGHTGRAY;
}

// Name of the character of an ID value, its ID if it has none
static const char* CharacterText(uint32_t id) {
    return GameDB_CharacterDisplay(IDText(id));
}

// File name of a sound path, without the extension
static const char* SoundName(uint32_t sound) {
    if (sound >= g_auth.sequence.soundCount)
        return TextFormat("sound %u", sound);
    return GetFileNameWithoutExt(g_auth.sequence.sounds[sound]);
}

static const char* MotionName(uint32_t id) {
    for (uint32_t i = 0; i < g_auth.motionCount; i++) {
        if (g_auth.motions[i].id == id && g_auth.motions[i].name != NULL)
            return g_auth.motions[i].name;
    }
    for (uint32_t i = 0; i < g_auth.bankCount; i++) {
        const char* name = MOTN_FindName(&g_auth.banks[i], id);
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
    const char* id = CharacterText(Arg(command, 0));
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
                CharacterText(Arg(command, 3)), AUTH_ArgFloat(command, 8));
        return TextFormat("Sound %s %s 0x%08X", id, SoundName(Arg(command, 2)), Arg(command, 1));
    default:
        return TextFormat("Command %u (%u arguments)", command->opcode, command->argCount);
    }
}

// What the command does, and to what: its character and its motion or sound, without the other arguments
static const char* CommandName(const AUTH_Command* command) {
    const char* id = CharacterText(Arg(command, 0));
    switch (command->opcode) {
    case AUTH_COMMAND_START:
        return "Start";
    case AUTH_COMMAND_CHARACTER:
        return TextFormat("Character %s", id);
    case AUTH_COMMAND_MOTION: {
        const char* name = MotionName(Arg(command, 1));
        return name != NULL ? TextFormat("Motion %s %s", id, name) : TextFormat("Motion %s 0x%X", id, Arg(command, 1));
    }
    case AUTH_COMMAND_UNKNOWN:
        return TextFormat("Unknown %s", id);
    case AUTH_COMMAND_VOICE:
        return TextFormat("Voice %s %s", id, SoundName(Arg(command, 1)));
    case AUTH_COMMAND_SOUND:
        return TextFormat("Sound %s %s", id, SoundName(Arg(command, 2)));
    default:
        return TextFormat("Command %u", command->opcode);
    }
}

// The frame and the name of the commands; the ID and the name of the characters
static bool CreateLabels(void) {
    const AUTH_Sequence* sequence = &g_auth.sequence;
    g_auth.labels = malloc((sequence->commandCount + 1) * sizeof(*g_auth.labels));
    g_auth.characterLabels = malloc((sequence->characterCount + 1) * sizeof(*g_auth.characterLabels));
    g_auth.shownCharacters = malloc((sequence->characterCount + 1) * sizeof(bool));
    if (g_auth.labels == NULL || g_auth.characterLabels == NULL || g_auth.shownCharacters == NULL)
        return false;
    for (uint32_t i = 0; i < sequence->commandCount; i++) {
        const AUTH_Command* command = &sequence->commands[i];
        snprintf(g_auth.labels[i], AUTHVIEW_LABEL_SIZE, "%5u  %s", command->frame, CommandName(command));
    }
    for (uint32_t i = 0; i < sequence->characterCount; i++) {
        snprintf(g_auth.characterLabels[i], AUTHVIEW_LABEL_SIZE, "%s", GameDB_CharacterLabel(sequence->characters[i].id));
        g_auth.shownCharacters[i] = true;
    }
    return true;
}

// Box around the keys of the camera and of the characters
static void ComputeBounds(void) {
    const AUTH_Sequence* sequence = &g_auth.sequence;
    Bounds bounds = Bounds_Empty();
    for (int c = 0; c <= AUTH_CAMERA_TARGET_X; c += AUTH_CAMERA_TARGET_X) {
        const AUTH_Curve* curves = &sequence->camera[c];
        for (uint32_t k = 0; k < curves[0].count; k++) {
            float time = curves[0].times[k];
            Bounds_AddPoint(&bounds, (Vector3) { AUTH_CurveValue(&curves[0], time, 0.0f), AUTH_CurveValue(&curves[1], time, 0.0f),
                AUTH_CurveValue(&curves[2], time, 0.0f) });
        }
    }
    for (uint32_t i = 0; i < sequence->characterCount; i++) {
        const AUTH_Curve* curves = sequence->characters[i].curves;
        for (uint32_t k = 0; k < curves[AUTH_CHARACTER_X].count; k++) {
            float time = curves[AUTH_CHARACTER_X].times[k];
            Bounds_AddPoint(&bounds, (Vector3) { AUTH_CurveValue(&curves[AUTH_CHARACTER_X], time, 0.0f),
                AUTH_CurveValue(&curves[AUTH_CHARACTER_Y], time, 0.0f), AUTH_CurveValue(&curves[AUTH_CHARACTER_Z], time, 0.0f) });
        }
    }
    if (Bounds_IsEmpty(&bounds))
        bounds = (Bounds) { { -1.0f, 0.0f, -1.0f }, { 1.0f, 2.0f, 1.0f } };
    g_auth.center = Bounds_Center(&bounds);
    g_auth.radius = Bounds_Radius(&bounds, 2.0f);
}

static void ResetCamera(void) {
    FreeCamera_Reset(&g_auth.camera, g_auth.center, (Vector3) { sinf(0.6f), 0.0f, cosf(0.6f) }, g_auth.radius, SCENE3D_FOVY);
    g_auth.camera.pitch = 0.6f;
}

void AUTHView_Close(void) {
    if (g_auth.open)
        g_options.fly = g_auth.camera.fly;
    AUTHSounds_Free();
    for (uint32_t i = 0; i < g_auth.modelCount; i++) {
        if (g_auth.models[i].loaded)
            MT5Render_FreeModel(&g_auth.models[i].model);
        free(g_auth.models[i].pose);
    }
    for (uint32_t i = 0; i < g_auth.motionCount; i++) {
        if (g_auth.motions[i].valid)
            MOTN_FreeMotion(&g_auth.motions[i].motion);
    }
    free(g_auth.motions);
    for (uint32_t i = 0; g_auth.characterModels != NULL && i < g_auth.sequence.characterCount; i++)
        free((char*)g_auth.characterModels[i].source);
    for (uint32_t i = 0; g_auth.humanFiles != NULL && i < g_auth.sequence.characterCount; i++)
        IPAC_FreeFiles(&g_auth.humanFiles[i]);
    free(g_auth.humanFiles);
    for (uint32_t i = 0; i < g_auth.placedCount; i++) {
        free((char*)g_auth.placed[i].source);
        IPAC_FreeFiles(&g_auth.placed[i].files);
    }
    free(g_auth.placed);
    if (g_auth.open)
        MT5Render_Free(&g_auth.render);
    free(g_auth.models);
    free(g_auth.characterModels);
    free(g_auth.instances);
    AUTH_Free(&g_auth.sequence);
    for (uint32_t i = 0; i < g_auth.bankCount; i++)
        MOTN_Free(&g_auth.banks[i]);
    free(g_auth.banks);
    free(g_auth.labels);
    free(g_auth.characterLabels);
    free(g_auth.shownCharacters);
    SCNCutscene_Free(&g_auth.program);
    free(g_auth.programItems);
    free(g_auth.programLabels);
    MapFiles_Free(&g_auth.mapFiles);
    memset(&g_auth, 0, sizeof(g_auth));
}

// Shows a loaded cutscene, of the AUTH file filepath or of the TRCK chunk cutscene of the SCEX chunk of the mapinfo.bin
// filepath (cutscene -1 for an AUTH file), replacing the current one. package and file: the package and the AUTH file
// that the program of the map loads in the slot cutscene (e.g. "BUSS", "seqdata1.bin"), NULL if none
static bool Start(const AUTH_Sequence* sequence, const char* filepath, int cutscene, const char* package, const char* file) {
    AUTHView_Close();
    snprintf(g_auth.path, sizeof(g_auth.path), "%s", filepath);
    g_auth.sequence = *sequence;
    g_auth.cutscene = cutscene;
    g_auth.current = -1;
    g_auth.selectedCharacter = -1;
    Path_ToSlashes(g_auth.path);
    // The map of an AUTH file is the parent folder of its folder (of its PKS)
    char mapFolder[AUTHVIEW_PATH_SIZE];
    snprintf(mapFolder, sizeof(mapFolder), "%s", GetDirectoryPath(g_auth.path));
    if (cutscene < 0)
        Path_ToParent(mapFolder);
    snprintf(g_auth.mapinfoPath, sizeof(g_auth.mapinfoPath), "%s", cutscene < 0 ? TextFormat("%s/mapinfo.bin", mapFolder) : g_auth.path);
    g_auth.hasMapinfo = FileExists(g_auth.mapinfoPath);
    Path_FindSceneFolder(g_auth.path, g_auth.sceneFolder, sizeof(g_auth.sceneFolder));
    if (cutscene >= 0 && !MapFiles_Load(mapFolder, ".mt5;.chrm;.chrt;.motn", &g_auth.mapFiles)) {
        Log_Error("[AUTHView_Open]: Memory error\n");
        AUTHView_Close();
        return false;
    }
    AUTHMotions_LoadBanks(g_auth.path, package);
    Log_Info("%s%s: cutscene, %u frames, %u characters, %u commands, %u sounds, %u MOTN files\n", GetFileName(g_auth.path),
        cutscene >= 0 ? TextFormat(" #%d", cutscene) : "", sequence->duration, sequence->characterCount, sequence->commandCount,
        sequence->soundCount, g_auth.bankCount);
    if (!AUTHMotions_Load() || !CreateLabels() || !AUTHModels_LoadCharacters() || !AUTHProgram_Run(cutscene, file)) {
        Log_Error("[AUTHView_Open]: Memory error\n");
        AUTHView_Close();
        return false;
    }
    MT5Render_Init(&g_auth.render);
    g_auth.open = true;
    if (g_options.map)
        OpenMap();
    // The program of the map loads the bank of the sounds with the name of the PKS of an AUTH file (the package it loads
    // the cutscene from, e.g. a1_busno.snd with BUSS for the cutscene 16 of scene/01/d000), or of the map
    AUTHSounds_Load(g_auth.sceneFolder[0] != '\0' ? g_auth.sceneFolder : NULL, g_auth.mapinfoPath,
        package != NULL && package[0] != '\0' ? package : GetFileName(GetDirectoryPath(g_auth.path)), &g_auth.sequence,
        g_auth.program.found ? &g_auth.program : NULL);
    ComputeBounds();
    ResetCamera();
    FreeCamera_SetFly(&g_auth.camera, g_options.fly);
    g_auth.playing = true;
    g_auth.jumped = true;
    return true;
}

bool AUTHView_Open(const char* filepath) {
    AUTH_Sequence sequence;
    return AUTH_Load(filepath, &sequence) && Start(&sequence, filepath, -1, NULL, NULL);
}

bool AUTHView_OpenMapCutscene(const char* filepath, uint32_t cutscene) {
    MAPINFO_File file;
    if (!MAPINFO_Load(filepath, &file))
        return false;
    AUTH_Sequence sequence;
    bool loaded = cutscene < file.cutsceneCount && AUTH_LoadFromMemory(file.data + file.cutscenes[cutscene].offset,
        file.cutscenes[cutscene].size, TextFormat("%s #%u", filepath, cutscene), &sequence);
    MAPINFO_Free(&file);
    return loaded && Start(&sequence, filepath, (int)cutscene, NULL, NULL);
}

bool AUTHView_OpenMapSequence(const char* filepath, const SD_Reference* sequence) {
    size_t size = 0;
    uint8_t* data = MapProgram_LoadSequence(filepath, sequence, &size);
    AUTH_Sequence loaded;
    bool ok = data != NULL && AUTH_LoadFromMemory(data, size, TextFormat("%s #%d", filepath, sequence->number), &loaded);
    free(data);
    // The package, of the map (e.g. "BUSS") or the last folder of a scene folder (e.g. "SCENE/01/DNOZ")
    const char* package = sequence->folder;
    for (const char* c = sequence->folder; *c != '\0'; c++) {
        if ((*c == '/' || *c == '\\') && c[1] != '\0')
            package = c + 1;
    }
    char name[SD_NAME_SIZE];
    snprintf(name, sizeof(name), "%s", package);
    size_t length = strlen(name);
    if (length > 0 && (name[length - 1] == '/' || name[length - 1] == '\\'))
        name[length - 1] = '\0';
    return ok && Start(&loaded, filepath, sequence->number, name, sequence->name);
}

bool AUTHView_IsMapCutscene(void) {
    return g_auth.open && g_auth.cutscene >= 0;
}

bool AUTHView_UsesMap(void) {
    const char* map = MAPINFOView_Path();
    return g_auth.open && map != NULL && Path_Equals(map, g_auth.mapinfoPath);
}

// Opens the view of the map of an AUTH file (its parent folder) if it isn't open, behind this one. The view of the map of
// a cutscene of a mapinfo.bin is already open
static void OpenMap(void) {
    if (!AUTHView_UsesMap() && g_auth.hasMapinfo && !MAPINFOView_Open(g_auth.mapinfoPath))
        Log_Error("[AUTHView_Open]: Failed to load the map %s\n", g_auth.mapinfoPath);
}

// The fade that the program of the map gives to a character at the current frame (CharacterOpacity): 0 if none
static float ProgramFade(uint32_t idValue) {
    float opacity = 1.0f;
    return SCNCutscene_OpacityAt(&g_auth.program, idValue, CurrentFrame(), &opacity) ? 1.0f - opacity : 0.0f;
}

// The characters of the map that are in the cutscene aren't drawn with the map: the cutscene places them; nor those that
// the program of the map has placed at the current frame, drawn by the cutscene, nor those that it hides or puts to
// sleep (e.g. the machine VM_0 of scene/01/d000, replaced by JIHN for the cutscene 22 of its vending machine). The
// others are drawn with the opacity that the program gives them (e.g. the picture GAKU and the doors DOOR and DOR2 of
// scene/02/toki, which fade out from the frame 1800 to 1900 of the cutscene of seqdata0.bin)
static float MapCharacterFade(const char* id) {
    for (uint32_t i = 0; i < g_auth.sequence.characterCount; i++) {
        if (strcmp(g_auth.sequence.characters[i].id, id) == 0)
            return 1.0f;
    }
    uint32_t idValue = 0;
    memcpy(&idValue, id, strlen(id) < 4 ? strlen(id) : 4);
    bool shown = true;
    if (SCNCutscene_ShownAt(&g_auth.program, idValue, CurrentFrame(), &shown) && !shown)
        return 1.0f;
    for (uint32_t i = 0; i < g_auth.placedCount; i++) {
        if (strcmp(g_auth.placed[i].id, id) == 0 && g_auth.placed[i].model >= 0 && AUTHPose_PlacedDrawn(&g_auth.placed[i], CurrentFrame()))
            return 1.0f;
    }
    return ProgramFade(idValue);
}

// The MAP models that the program of the map shows or hides (MapModelDisplay) and fades (MapModelOpacity) up to the
// current frame (e.g. map.mt5 of scene/02/toki, faded out from the frame 1800 to 1900 of the cutscene of seqdata0.bin,
// hidden until 2520 and faded in up to 2580)
static void MapModelChange(int slot, bool* shown, float* fade) {
    float opacity = 1.0f - *fade;
    if (SCNCutscene_MapModelAt(&g_auth.program, (uint32_t)slot, CurrentFrame(), shown, &opacity))
        *fade = 1.0f - opacity;
}

static MAPINFOView_Changes g_mapChanges = { MapCharacterFade, MapModelChange, NULL };

// The lights of the map at the current frame, as the program of the map changes them
static struct {
    uint32_t            capacity;
    MT5Render_Light*    lights;
    uint16_t*           ids;
    MT5Render_Lighting  lighting;
} g_lights;

// The lights that light the characters at the current frame, NULL if from the camera (the Lights toggle is off, the
// map has no lights, or they are of an exterior, which the game also lights by the hour and the weather): the group of lights of the map chosen by the last call to LightGroup (0 before), with the ambient
// color and the intensities that the calls to AmbientColor and LightControl after it give (e.g. the blue light 4 of
// the group 1 of scene/02/toki, which fades out from the frame 2501 to 2530 of the cutscene of seqdata0.bin)
static const MT5Render_Lighting* CutsceneLighting(void) {
    uint32_t total = MAPINFOView_LightCount();
    if (!g_options.lights || !AUTHView_UsesMap() || total == 0)
        return NULL;
    if (total > g_lights.capacity) {
        MT5Render_Light* lights = realloc(g_lights.lights, total * sizeof(MT5Render_Light));
        if (lights != NULL)
            g_lights.lights = lights;
        uint16_t* ids = realloc(g_lights.ids, total * sizeof(uint16_t));
        if (ids != NULL)
            g_lights.ids = ids;
        if (lights == NULL || ids == NULL)
            return NULL;
        g_lights.capacity = total;
    }
    const SCNCutscene* program = &g_auth.program;
    uint32_t frame = CurrentFrame(), group = 0, first = 0;
    for (uint32_t i = 0; i < program->lightCount && program->lights[i].frame <= frame; i++) {
        if (program->lights[i].function == SCNCUTSCENE_LIGHT_GROUP) {
            group = program->lights[i].light;
            first = i + 1;
        }
    }
    if (MAPINFOView_IsOutdoorGroup(group))
        return NULL;
    MT5Render_Lighting* lighting = &g_lights.lighting;
    lighting->lightCount = MAPINFOView_GroupLights(group, lighting->ambient, g_lights.lights, g_lights.ids, total);
    lighting->lights = g_lights.lights;
    for (uint32_t i = first; i < program->lightCount && program->lights[i].frame <= frame; i++) {
        const SCNCutscene_Light* call = &program->lights[i];
        if (call->function == SCNCUTSCENE_AMBIENT && (call->known & 0x0E) == 0x0E)
            memcpy(lighting->ambient, &call->values[1], sizeof(lighting->ambient));
        else if (call->function == SCNCUTSCENE_LIGHT && call->operation == SCNCUTSCENE_LIGHT_INTENSITY && (call->known & 1)) {
            for (uint32_t l = 0; l < lighting->lightCount; l++) {
                if (g_lights.ids[l] == call->light)
                    g_lights.lights[l].intensity = call->values[0];
            }
        }
    }
    return lighting;
}

static bool ShowsMap(void) {
    return g_options.map && AUTHView_UsesMap();
}

bool AUTHView_IsOpen(void) {
    return g_auth.open;
}

// Index of the last command at or before the current frame, -1 if none
static int CurrentCommand(void) {
    uint32_t frame = CurrentFrame();
    int current = -1;
    for (uint32_t i = 0; i < g_auth.sequence.commandCount && g_auth.sequence.commands[i].frame <= frame; i++)
        current = (int)i;
    return current;
}

static void Seek(float time) {
    g_auth.time = Clamp(time, 0.0f, Duration());
    g_auth.jumped = true;
}

static void SeekFrame(uint32_t frame) {
    Seek(frame / AUTH_FRAME_RATE);
}

// Goes to the frame of the previous (direction -1) or next (1) commands
static void SeekCommand(int direction) {
    uint32_t frame = CurrentFrame();
    const AUTH_Sequence* sequence = &g_auth.sequence;
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
    if (!g_auth.playing && g_auth.time >= Duration())
        Seek(0.0f);
    g_auth.playing = !g_auth.playing;
}

// The camera of the cutscene at the current time, with its roll
static Camera3D CutsceneCamera(void) {
    return Scene3D_CurveCamera(g_auth.sequence.camera, g_auth.time, SCENE3D_FOVY);
}

void AUTHView_Toolbar(void) {
    // The view of the map is open behind the cutscene
    if (AUTHView_UsesMap()) {
        if (Toolbar_Button(ICON_ARROW_LEFT, "Back", "Close the cutscene and go to its map", KEY_BACKSPACE)) {
            AUTHView_Close();
            return;
        }
        Toolbar_Separator();
    }
    if (Toolbar_Button(g_auth.playing ? ICON_PLAYER_PAUSE : ICON_PLAYER_PLAY, NULL, "Play or pause", KEY_SPACE))
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
    bool fly = !g_options.cutsceneCamera && g_auth.camera.fly;
    if (Toolbar_Toggle(ICON_TARGET_MOVE, "Fly", "Fly with the free camera, from the camera of the cutscene if it is shown", KEY_F, &fly)) {
        // Flies from the position of the camera of the cutscene, or of the free camera, looking around it
        if (fly && g_options.cutsceneCamera)
            FreeCamera_LookFrom(&g_auth.camera, CutsceneCamera());
        FreeCamera_SetFly(&g_auth.camera, fly);
        g_options.cutsceneCamera = false;
    }
    Toolbar_Toggle(ICON_TEXT_A, "Labels", "Show the name, sound and motion over the characters", KEY_L, &g_options.labels);
    Toolbar_Toggle(ICON_STAR, "Lights", "Light the characters with the lights of the map, as the program of the map changes "
        "them (LightGroup, AmbientColor, LightControl), else from the camera. The exteriors are always lit from the camera: "
        "the game also lights them by the hour and the weather", KEY_I, &g_options.lights);
    if (!g_auth.hasMapinfo)
        GuiDisable();
    if (Toolbar_Toggle(ICON_MAPS, "Map", "Show the models of the map of the cutscene, as its view shows them", KEY_M, &g_options.map) &&
        g_options.map)
        OpenMap();
    GuiEnable();
}

static void UpdateInput(Rectangle scene) {
    if (!g_options.cutsceneCamera)
        FreeCamera_Update(&g_auth.camera, scene, 0.1f);

    if (g_auth.playing && !g_auth.seeking) {
        g_auth.time += Scene3D_FrameTime();
        if (g_auth.time >= Duration()) {
            g_auth.time = Duration();
            g_auth.playing = false;
        }
    }
    AUTHSounds_Update(g_auth.time, g_auth.playing && !g_auth.seeking, g_auth.jumped);
    g_auth.jumped = false;
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
    float yaw = AUTH_CurveValue(&character->curves[AUTH_CHARACTER_ROTATION_Y], g_auth.time, 0.0f) * DEG2RAD;
    return (Vector3) { -sinf(yaw), 0.0f, -cosf(yaw) };
}

// With movement, shown in the tree, and not hidden nor put to sleep by the program of the map at the current frame (e.g.
// SINF, HOSI, SEZ1, SEZ2 and PNXM, hidden at the frame 1 of the cutscene of seqdata0.bin of scene/02/toki), nor made
// invisible by it (CharacterOpacity 0)
bool AUTHView_IsCharacterShown(uint32_t character) {
    const AUTH_Character* c = &g_auth.sequence.characters[character];
    bool shown = true;
    return HasMovement(c) && g_options.charactersShown && g_auth.shownCharacters[character] &&
        (!SCNCutscene_ShownAt(&g_auth.program, c->idValue, CurrentFrame(), &shown) || shown) && ProgramFade(c->idValue) < 1.0f;
}

static void DrawCameraGizmo(void) {
    Camera3D camera = CutsceneCamera();
    Scene3D_DrawCameraGizmo(camera, fmaxf(g_auth.radius * 0.15f, 0.5f), SCENE3D_GAME_ASPECT, true, g_cameraColor);
    DrawLine3D(camera.position, camera.target, Fade(g_cameraColor, 0.3f));
    DrawSphere(camera.target, 0.05f, g_cameraColor);
}

static void DrawGround(void) {
    int half = (int)ceilf(g_auth.radius) + 2;
    float x0 = floorf(g_auth.center.x), z0 = floorf(g_auth.center.z);
    Color color = { 70, 70, 75, 255 };
    for (int i = -half; i <= half; i++) {
        DrawLine3D((Vector3) { x0 + i, 0.0f, z0 - half }, (Vector3) { x0 + i, 0.0f, z0 + half }, color);
        DrawLine3D((Vector3) { x0 - half, 0.0f, z0 + i }, (Vector3) { x0 + half, 0.0f, z0 + i }, color);
    }
}

// With their model, or as cylinders with a line to the direction they face if it wasn't found; then the characters placed
// or attached by the program, from the first call that places or attaches them
static void DrawCharacters(Camera3D camera) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < g_auth.sequence.characterCount; i++) {
        int model = g_auth.characterModels[i].model;
        if (!AUTHView_IsCharacterShown(i) || model < 0)
            continue;
        AUTHPose_PoseCharacter(i);
        Matrix transform = AUTHPose_CharacterTransform(i);
        MT5Render_Model* instanced = &g_auth.models[model].model;
        g_auth.instances[count++] = (MT5Render_Instance) { instanced, transform, Vector3Transform(instanced->mesh.center, transform), 0,
            ProgramFade(g_auth.sequence.characters[i].idValue), g_mapChanges.lighting };
    }
    for (uint32_t i = 0; i < g_auth.placedCount && g_options.charactersShown; i++) {
        const AUTHView_Placed* placed = &g_auth.placed[i];
        bool shown = true;
        Matrix transform;
        if (placed->model < 0 || (SCNCutscene_ShownAt(&g_auth.program, placed->idValue, CurrentFrame(), &shown) && !shown) ||
            !AUTHPose_ProgramTransform(placed, &transform))
            continue;
        float fade = ProgramFade(placed->idValue);
        if (fade >= 1.0f)
            continue;
        AUTHPose_PosePlaced(placed);
        MT5Render_Model* instanced = &g_auth.models[placed->model].model;
        g_auth.instances[count++] = (MT5Render_Instance) { instanced, transform, Vector3Transform(instanced->mesh.center, transform), 0,
            fade, g_mapChanges.lighting };
    }
    MT5Render_DrawInstances(&g_auth.render, g_auth.instances, count, camera);

    for (uint32_t i = 0; i < g_auth.sequence.characterCount; i++) {
        const AUTH_Character* character = &g_auth.sequence.characters[i];
        if (!AUTHView_IsCharacterShown(i) || g_auth.characterModels[i].model >= 0)
            continue;
        Color color = CharacterColor((int)i);
        Vector3 position = CurvePoint(&character->curves[AUTH_CHARACTER_X], g_auth.time);
        DrawCylinder(position, SCENE3D_CHARACTER_RADIUS, SCENE3D_CHARACTER_RADIUS, SCENE3D_CHARACTER_HEIGHT, 12, Fade(color, 0.6f));
        DrawCylinderWires(position, SCENE3D_CHARACTER_RADIUS, SCENE3D_CHARACTER_RADIUS, SCENE3D_CHARACTER_HEIGHT, 12, color);
        Vector3 eyes = Vector3Add(position, (Vector3) { 0.0f, SCENE3D_CHARACTER_HEIGHT * 0.9f, 0.0f });
        Vector3 front = Vector3Add(eyes, Vector3Scale(CharacterForward(character), 0.6f));
        DrawLine3D(eyes, front, color);
        DrawSphere(front, 0.06f, color);
    }
}

// Is the character speaking, or making a sound, at the current frame: text of the last voice or sound
static const char* CharacterSound(uint32_t idValue) {
    const char* text = NULL;
    float time = g_auth.time;
    for (uint32_t i = 0; i < g_auth.sequence.commandCount; i++) {
        const AUTH_Command* command = &g_auth.sequence.commands[i];
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
    for (uint32_t i = 0; i < g_auth.sequence.commandCount; i++) {
        const AUTH_Command* command = &g_auth.sequence.commands[i];
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
    for (uint32_t i = 0; i < g_auth.sequence.characterCount; i++) {
        const AUTH_Character* character = &g_auth.sequence.characters[i];
        if (!AUTHView_IsCharacterShown(i))
            continue;
        Vector3 head = Vector3Add(CurvePoint(&character->curves[AUTH_CHARACTER_X], g_auth.time), (Vector3) { 0.0f, AUTHPose_CharacterHeight(i) + 0.15f, 0.0f });
        Vector2 point;
        if (!Scene3D_ScreenPoint(camera, area, head, &point))
            continue;

        const char* lines[3] = { GameDB_CharacterDisplay(character->id), CharacterSound(character->idValue), NULL };
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
    bool map = ShowsMap();
    if (g_options.cutsceneCamera) {
        camera = CutsceneCamera();
        view = Scene3D_Letterbox(area, SCENE3D_GAME_ASPECT);
        FreeCamera_BeginCamera3D(camera, view, AUTHVIEW_NEAR, AUTHVIEW_FAR);
    }
    else if (map) {
        // The planes of the free camera are those of the cutscene, which can be much smaller than the map
        camera = FreeCamera_Camera3D(&g_auth.camera);
        FreeCamera_BeginCamera3D(camera, view, AUTHVIEW_NEAR, AUTHVIEW_FAR);
    }
    else
        camera = FreeCamera_Begin3D(&g_auth.camera, view);

    g_mapChanges.lighting = CutsceneLighting();
    if (map)
        MAPINFOView_DrawMap(camera, &g_mapChanges);
    else
        DrawGround();
    if (!g_options.cutsceneCamera) {
        DrawPath(&g_auth.sequence.camera[AUTH_CAMERA_X], Fade(g_cameraColor, 0.5f));
        for (uint32_t i = 0; i < g_auth.sequence.characterCount; i++) {
            if (AUTHView_IsCharacterShown(i))
                DrawPath(&g_auth.sequence.characters[i].curves[AUTH_CHARACTER_X], Fade(CharacterColor((int)i), 0.5f));
        }
        DrawCameraGizmo();
    }
    DrawCharacters(camera);
    FreeCamera_End3D();
    if (g_options.labels) {
        // Clipped to the rendered view, inside the letterbox of the cutscene camera
        BeginScissorMode((int)view.x, (int)view.y, (int)view.width, (int)view.height);
        DrawCharacterLabels(camera, view, font, fontSize);
        EndScissorMode();
    }
    DrawRectangleLinesEx(view, 1.0f, (Color) { 112, 112, 112, 255 });
}

// Lanes of the timeline: motions, voices, sounds, other commands; the cuts of the camera over them
static void DrawTimeline(Rectangle area, Font font, float fontSize) {
    const AUTH_Sequence* sequence = &g_auth.sequence;
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
    // The calls of the program, in the lane of the other commands
    for (uint32_t i = 0; i < g_auth.programItemCount; i++) {
        const AUTHView_ProgramItem* item = &g_auth.programItems[i];
        float x = area.x + item->firstFrame / AUTH_FRAME_RATE * scale;
        float width = fmaxf((item->lastFrame - item->firstFrame) / AUTH_FRAME_RATE * scale, 2.0f);
        bool sound = item->kind == PROGRAM_SOUND || item->kind == PROGRAM_BANK;
        float y = area.y + 3.0f * laneHeight + (sound ? 2.0f : laneHeight * 0.5f);
        if (item->kind == PROGRAM_DISPLAY || item->kind == PROGRAM_ATTACH)
            width = 2.0f;
        DrawRectangleRec((Rectangle) { x, y, width, laneHeight * 0.5f - 2.0f }, Fade(g_programColor, sound ? 1.0f : 0.6f));
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
    float x = area.x + g_auth.time * scale;
    DrawLineEx((Vector2) { x, area.y }, (Vector2) { x, area.y + area.height }, 2.0f, RED);
    DrawRectangleLinesEx(area, 1.0f, (Color) { 112, 112, 112, 255 });
}

static void UpdateTimeline(Rectangle area) {
    Vector2 mouse = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, area))
        g_auth.seeking = true;
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        g_auth.seeking = false;
    if (g_auth.seeking && area.width > 0.0f)
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

// The characters, whose eye shows or hides them in the scene; a click on one selects it and looks at it with the free
// camera, or unselects it. Then the commands by lane of the timeline, with the one of the current frame selected while
// no character is; a click on one goes to its frame. The current command is followed while playing or seeking, without
// expanding its group
static void DrawTree(Rectangle bounds) {
    const AUTH_Sequence* sequence = &g_auth.sequence;
    Tree* tree = &g_auth.tree;
    int current = CurrentCommand();
    if (current != g_auth.current && (g_auth.playing || g_auth.seeking))
        Tree_Reveal(tree, false);
    g_auth.current = current;

    int clickedCharacter = -1, clickedCommand = -1;
    Tree_Begin(tree, bounds);
    Tree_BeginGroup(tree, g_groupNames[GROUP_CHARACTERS], &g_options.expanded[GROUP_CHARACTERS], &g_options.charactersShown);
    for (uint32_t i = 0; i < sequence->characterCount; i++) {
        bool moves = HasMovement(&sequence->characters[i]);
        int flags = ((int)i == g_auth.selectedCharacter ? TREE_SELECTED : 0) | (moves ? 0 : TREE_DIMMED);
        if (Tree_Item(tree, g_auth.characterLabels[i], flags, moves ? &g_auth.shownCharacters[i] : NULL))
            clickedCharacter = (int)i;
    }
    Tree_EndGroup(tree);
    for (int g = GROUP_MOTIONS; g < GROUP_PROGRAM; g++) {
        Tree_BeginGroup(tree, g_groupNames[g], &g_options.expanded[g], NULL);
        for (uint32_t i = 0; i < sequence->commandCount; i++) {
            if ((int)CommandGroup(&sequence->commands[i]) == g &&
                Tree_Item(tree, g_auth.labels[i], (int)i == current && g_auth.selectedCharacter < 0 ? TREE_SELECTED : 0, NULL))
                clickedCommand = (int)i;
        }
        Tree_EndGroup(tree);
    }
    int clickedProgram = -1;
    if (g_auth.programItemCount > 0) {
        Tree_BeginGroup(tree, g_groupNames[GROUP_PROGRAM], &g_options.expanded[GROUP_PROGRAM], NULL);
        for (uint32_t i = 0; i < g_auth.programItemCount; i++) {
            if (Tree_Item(tree, g_auth.programLabels[i], (int)i == g_auth.selectedProgram ? TREE_SELECTED : 0, NULL))
                clickedProgram = (int)i;
        }
        Tree_EndGroup(tree);
    }
    Tree_End(tree);

    if (clickedCommand >= 0) {
        SeekFrame(sequence->commands[clickedCommand].frame);
        g_auth.selectedCharacter = -1;
        g_auth.selectedProgram = -1;
    }
    if (clickedProgram >= 0) {
        SeekFrame(g_auth.programItems[clickedProgram].firstFrame);
        g_auth.selectedCharacter = -1;
        g_auth.selectedProgram = clickedProgram == g_auth.selectedProgram ? -1 : clickedProgram;
    }
    if (clickedCharacter >= 0 && clickedCharacter == g_auth.selectedCharacter)
        g_auth.selectedCharacter = -1;
    else if (clickedCharacter >= 0) {
        g_auth.selectedCharacter = clickedCharacter;
        g_auth.selectedProgram = -1;
        const AUTH_Character* character = &sequence->characters[clickedCharacter];
        if (!HasMovement(character))
            return;
        g_options.cutsceneCamera = false;
        g_auth.camera.target = Vector3Add(CurvePoint(&character->curves[AUTH_CHARACTER_X], g_auth.time),
            (Vector3) { 0.0f, AUTHPose_CharacterHeight((uint32_t)clickedCharacter) * 0.5f, 0.0f });
        g_auth.camera.distance = 6.0f;
    }
}

static const char* g_characterCurves[AUTH_CHARACTER_CURVES] = { "X", "Y", "Z", "Rotation X", "Rotation Y", "Rotation Z", "Unknown 6",
    "Unknown 7", "Unknown 8" };

// Its values at the current time, what it does, and the keys of its curves
static void CharacterProperties(Properties* properties, uint32_t index) {
    const AUTH_Character* character = &g_auth.sequence.characters[index];
    const AUTHView_CharacterModel* characterModel = &g_auth.characterModels[index];
    Properties_Text(properties, "ID", character->id);
    Properties_Hex(properties, "ID value", character->idValue);
    GameDB_CharacterProperties(properties, character->id);
    if (HasMovement(character)) {
        Properties_Text(properties, "Model", characterModel->model >= 0 ? GetFileName(g_auth.models[characterModel->model].path) :
            characterModel->source != NULL ? "invalid model file" : "not found");
        if (characterModel->source != NULL)
            Properties_Text(properties, "Found in", characterModel->source);
        Properties_Bool(properties, "Human", characterModel->human);
    }
    if (HasMovement(character)) {
        Properties_Section(properties, "Now");
        Vector3 position = CurvePoint(&character->curves[AUTH_CHARACTER_X], g_auth.time);
        Vector3 rotation = CurvePoint(&character->curves[AUTH_CHARACTER_ROTATION_X], g_auth.time);
        Properties_Vector(properties, "Position", (const float[]) { position.x, position.y, position.z }, 3);
        Properties_Vector(properties, "Rotation", (const float[]) { rotation.x, rotation.y, rotation.z }, 3);
        const AUTH_Command* motion = CharacterMotion(character->idValue);
        const char* name = motion != NULL ? MotionName(Arg(motion, 1)) : NULL;
        Properties_Text(properties, "Motion", motion == NULL ? "none" : name != NULL ? name : TextFormat("0x%X", Arg(motion, 1)));
        if (characterModel->model >= 0) {
            const AUTHView_Model* model = &g_auth.models[characterModel->model];
            Properties_Text(properties, "Pose", model->posed ? "of the motion" : model->programPosed ? "nodes moved by the program" :
                !model->body ? "of the file: not a body" : "of the file: no motion found");
        }
        const char* sound = CharacterSound(character->idValue);
        Properties_Text(properties, "Sound", sound != NULL ? sound : "none");
    }
    Properties_Section(properties, "Keys of the curves");
    for (int c = 0; c < AUTH_CHARACTER_CURVES; c++)
        Properties_Int(properties, g_characterCurves[c], character->curves[c].count);
}

// Its frame, its text and its arguments, as integers and as floats
static void CommandProperties(Properties* properties, uint32_t index) {
    const AUTH_Command* command = &g_auth.sequence.commands[index];
    Properties_Int(properties, "Frame", command->frame);
    Properties_Text(properties, "Time", Text_FormatTime(command->frame / AUTH_FRAME_RATE));
    Properties_Int(properties, "Opcode", command->opcode);
    Properties_Text(properties, "Command", CommandText(command));
    if (command->opcode == AUTH_COMMAND_VOICE || command->opcode == AUTH_COMMAND_SOUND)
        Properties_Text(properties, "Audio", AUTHSounds_HasSound(index) ? "found" : "not found");
    Properties_Section(properties, TextFormat("%u arguments", command->argCount));
    for (uint32_t i = 0; i < command->argCount; i++)
        Properties_Text(properties, TextFormat("%u", i), TextFormat("0x%08X  %d  %g", command->args[i], (int32_t)command->args[i],
            AUTH_ArgFloat(command, i)));
}

// Of the selected character or item of the program, or of the command of the current frame
static void DrawProperties(Rectangle bounds) {
    Properties* properties = &g_auth.properties;
    const AUTH_Sequence* sequence = &g_auth.sequence;
    int current = CurrentCommand();
    if (g_auth.selectedCharacter >= 0) {
        const AUTH_Character* character = &sequence->characters[g_auth.selectedCharacter];
        Properties_Begin(properties, bounds, TextFormat("Character %s", GameDB_CharacterDisplay(character->id)));
        CharacterProperties(properties, (uint32_t)g_auth.selectedCharacter);
    }
    else if (g_auth.selectedProgram >= 0) {
        Properties_Begin(properties, bounds, "Program of the map");
        AUTHProgram_Properties(properties, (uint32_t)g_auth.selectedProgram);
    }
    else if (current >= 0) {
        Properties_Begin(properties, bounds, TextFormat("Command %d", current));
        CommandProperties(properties, (uint32_t)current);
    }
    else {
        Properties_Begin(properties, bounds, NULL);
        Properties_Note(properties, "No command before the current frame");
    }
    Properties_End(properties);
}

void AUTHView_Draw(Rectangle area) {
    if (!g_auth.open || area.width <= 0.0f || area.height <= 0.0f)
        return;

    Font font = GuiGetFont();
    float fontSize = (float)GuiGetStyle(DEFAULT, TEXT_SIZE);
    const AUTH_Sequence* sequence = &g_auth.sequence;
    float margin = 10.0f;
    float controlsHeight = 30.0f;
    float top = area.y + margin;
    Rectangle column = { area.x + margin, top, AUTHVIEW_LIST_WIDTH, area.height - margin * 2.0f };
    Rectangle list, properties;
    Properties_Split(column, &list, &properties);
    float panelX = column.x + column.width + margin;
    float panelWidth = area.width - column.width - margin * 3.0f;
    Rectangle controls = { panelX, area.y + area.height - margin - controlsHeight, panelWidth, controlsHeight };
    Rectangle timeline = { panelX, controls.y - margin - AUTHVIEW_TIMELINE_HEIGHT, panelWidth, AUTHVIEW_TIMELINE_HEIGHT };
    Rectangle scene = { panelX, top, panelWidth, timeline.y - margin - top };
    if (list.height <= 0.0f || panelWidth <= 0.0f || scene.height <= 0.0f)
        return;
    UpdateTimeline(timeline);
    UpdateInput(scene);

    DrawRectangleRec(area, (Color) { 45, 45, 48, 255 });

    DrawTree(list);
    DrawProperties(properties);

    DrawScene(scene, font, fontSize);
    DrawTimeline(timeline, font, fontSize);

    float x = controls.x;
    // TextFormat uses a rotating buffer, so the two times can be formatted in the same call
    const char* status = TextFormat("Frame %u/%u   %s / %s", CurrentFrame(), sequence->duration, Text_FormatTime(g_auth.time),
        Text_FormatTime(Duration()));
    DrawTextEx(font, status, (Vector2) { x, controls.y + (controlsHeight - fontSize) * 0.5f }, fontSize, 1.0f, LIGHTGRAY);
}

// The cutscene of the mapinfo.bin and its map, if they are known, before the rest of the status
static const char* CutsceneText(void) {
    const GameDB_Map* map = GameDB_MapOfFile(g_auth.mapinfoPath);
    if (g_auth.cutscene >= 0)
        return map != NULL ? TextFormat("Cutscene %d of %s, ", g_auth.cutscene, map->name) : TextFormat("Cutscene %d, ", g_auth.cutscene);
    return map != NULL ? TextFormat("%s, ", map->name) : "";
}

void AUTHView_Status(ViewStatus* status) {
    const AUTH_Sequence* sequence = &g_auth.sequence;
    snprintf(status->file, sizeof(status->file), "%s", g_auth.path);
    AUTHSounds_Info sounds;
    AUTHSounds_GetInfo(&sounds);
    AUTHMusic_Info music;
    AUTHMusic_GetInfo(&music);
    char musicText[32] = "";
    if (music.music > 0)
        snprintf(musicText, sizeof(musicText), ", music %u/%u", music.found, music.music);
    snprintf(status->info, sizeof(status->info), "%s%s, %u characters, %u commands, voices %u/%u, effects %u/%u%s%s%s%s",
        CutsceneText(), Text_FormatTime(Duration()), sequence->characterCount, sequence->commandCount, sounds.voices, sounds.voiceCommands, sounds.effects, sounds.effectCommands,
        sounds.bank[0] != '\0' ? " in " : "", sounds.bank, g_auth.program.found ? TextFormat(", program: %u node calls, sounds %u/%u",
        g_auth.program.nodeCount, sounds.programEffects, sounds.programCommands) : "",
        musicText);
    if (g_options.cutsceneCamera)
        snprintf(status->hint, sizeof(status->hint), "Click the timeline: seek");
    else
        snprintf(status->hint, sizeof(status->hint), "%s", FreeCamera_Help(&g_auth.camera));
}
