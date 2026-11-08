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
#include "formats/map/mapinfo.h"
#include "formats/script/chrt.h"
#include "render/freeCamera.h"
#include "render/mt5Pose.h"
#include "render/mt5Render.h"
#include "render/scene3D.h"
#include "ui/properties.h"
#include "ui/toolbar.h"
#include "ui/tree.h"
#include "views/authSounds.h"
#include "views/authView.h"
#include "views/mapFiles.h"
#include "views/mapinfoView.h"

#define AUTHVIEW_LABEL_SIZE     128
#define AUTHVIEW_PATH_SIZE      1024
#define AUTHVIEW_LIST_WIDTH     460.0f
#define AUTHVIEW_TIMELINE_HEIGHT 64.0f
#define AUTHVIEW_FOVY           45.0f
#define AUTHVIEW_ASPECT         (4.0f / 3.0f) // Of the screen of the game, for the camera of the cutscene
#define AUTHVIEW_HEIGHT         1.7f    // Of the characters, drawn as cylinders when their model isn't found
#define AUTHVIEW_RADIUS         0.25f
#define AUTHVIEW_SOUND_TIME     0.5f    // Seconds a sound with no length is shown
#define AUTHVIEW_SCENE_MODELS   "model/chara" // Models of the characters in the folder of the scene
#define AUTHVIEW_HUMANS         "stream/humans" // Folder of each character of the scene, with its script and models
#define AUTHVIEW_COMMON_MOTIONS "/motion/motion.bin" // Motions shared by the whole game, from the parent of the scene folder

// A model file, loaded for each character that uses it: each one is in the pose of its motion
typedef struct {
    char            path[AUTHVIEW_PATH_SIZE];
    bool            loaded;
    float           lift;       // Height to raise the model of a person, whose origin is at the hips
    float           top;        // Highest height of the model, for the labels
    MT5Render_Model model;
    bool            body;       // The body of a character, that the motions can pose
    MT5Pose_Body    bodyNodes;
    Matrix*         pose;       // World matrices of the nodes
    bool            posed;      // In the pose of the motion of its character at the current frame
} AUTHView_Model;

// A motion played by the commands, decoded from the MOTN files
typedef struct {
    uint32_t        id;
    bool            valid;      // Found and decoded
    const char*     name;       // In its bank, NULL if not found
    MOTN_Motion     motion;
} AUTHView_Motion;

// The model of a character: the file of the Image of the character with its ID in the CHRT scripts of the folder of the
// cutscene, in its folder of the scene or in the characters of the map
typedef struct {
    int             model;      // Index in the models, -1 if none
    bool            human;      // Person, whose model is raised to stand at its position
    const char*     source;     // Script where the character was found, NULL if none
} AUTHView_CharacterModel;

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
    uint32_t        motionCount;
    AUTHView_Motion* motions;   // Of the motion commands
    char            (*labels)[AUTHVIEW_LABEL_SIZE]; // Of the commands
    char            (*characterLabels)[AUTHVIEW_LABEL_SIZE];
    bool*           shownCharacters; // Their eye in the tree
    Tree            tree;
    int             current;    // Command of the current frame in the tree, to follow it while playing
    int             selectedCharacter; // -1 if none: the properties are those of the current command
    Properties      properties;
    float           time;       // Seconds
    bool            playing;
    bool            seeking;    // Dragging on the timeline
    bool            jumped;     // The time was changed by a seek, not by playing
    Vector3         center;     // Of the scene
    float           radius;
    FreeCamera     camera;     // Free camera
    char            sceneFolder[AUTHVIEW_PATH_SIZE]; // Folder of the scene of the file (e.g. .../scene/01), empty if none
    char            mapinfoPath[AUTHVIEW_PATH_SIZE]; // mapinfo.bin of the map of the cutscene: of the parent folder of
                                                     // an AUTH file, or the open file for a cutscene of a mapinfo.bin
    int             cutscene;   // TRCK chunk of the SCEX chunk of the open mapinfo.bin, -1 for an AUTH file
    MapFiles        mapFiles;   // For a cutscene of a mapinfo.bin: the CHRT, model and MOTN files of its map
    MT5Render       render;
    uint32_t        modelCount;
    AUTHView_Model* models;
    AUTHView_CharacterModel* characterModels;
    MT5Render_Instance* instances; // Of the characters drawn with their model, filled before drawing them
} AUTHView_State;

// Kept when another cutscene is opened
typedef struct {
    bool            cutsceneCamera; // Show the scene from the camera of the cutscene
    bool            fly;
    bool            expanded[GROUP_COUNT];
    bool            charactersShown;
    bool            labels;         // Name, sound and motion over the characters
} AUTHView_Options;

static AUTHView_State g_view;
static AUTHView_Options g_options = {
    .cutsceneCamera = true,
    .labels = true,
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

static bool HasMovement(const AUTH_Character* character);

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
    for (uint32_t i = 0; i < g_view.motionCount; i++) {
        if (g_view.motions[i].id == id && g_view.motions[i].name != NULL)
            return g_view.motions[i].name;
    }
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

// What the command does, and to what: its character and its motion or sound, without the other arguments
static const char* CommandName(const AUTH_Command* command) {
    const char* id = IDText(Arg(command, 0));
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

// The frame and the name of the commands; the ID of the characters
static bool CreateLabels(void) {
    const AUTH_Sequence* sequence = &g_view.sequence;
    g_view.labels = malloc((sequence->commandCount + 1) * sizeof(*g_view.labels));
    g_view.characterLabels = malloc((sequence->characterCount + 1) * sizeof(*g_view.characterLabels));
    g_view.shownCharacters = malloc((sequence->characterCount + 1) * sizeof(bool));
    if (g_view.labels == NULL || g_view.characterLabels == NULL || g_view.shownCharacters == NULL)
        return false;
    for (uint32_t i = 0; i < sequence->commandCount; i++) {
        const AUTH_Command* command = &sequence->commands[i];
        snprintf(g_view.labels[i], AUTHVIEW_LABEL_SIZE, "%5u  %s", command->frame, CommandName(command));
    }
    for (uint32_t i = 0; i < sequence->characterCount; i++) {
        snprintf(g_view.characterLabels[i], AUTHVIEW_LABEL_SIZE, "%s", sequence->characters[i].id);
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

static bool EqualsIgnoreCase(const char* a, const char* b) {
    return CHRT_EqualsIgnoreCase(a, b);
}

// Removes the last component of a path with / separators. Returns false if there is none
static bool ToParent(char* path) {
    char* slash = strrchr(path, '/');
    if (slash == NULL || slash == path)
        return false;
    *slash = '\0';
    return true;
}

// Folder of the scene that contains the file (e.g. .../scene/01 for .../scene/01/d000/auth/SEQDATA0.auth): the folder
// whose parent is named "scene". Empty if the file isn't in a scene folder
static void FindSceneFolder(const char* filepath, char* folder, size_t size) {
    snprintf(folder, size, "%s", filepath);
    for (char* c = folder; *c != '\0'; c++) {
        if (*c == '\\')
            *c = '/';
    }
    while (ToParent(folder)) {
        char parent[AUTHVIEW_PATH_SIZE];
        snprintf(parent, sizeof(parent), "%s", folder);
        const char* name = ToParent(parent) ? strrchr(parent, '/') : NULL;
        if (name != NULL && EqualsIgnoreCase(name + 1, "scene"))
            return;
    }
    folder[0] = '\0';
}

// Loads a MOTN file at the end of the banks. Returns false if there is no memory
static bool AddMotionBank(const char* path) {
    MOTN_Bank* banks = realloc(g_view.banks, (g_view.bankCount + 1) * sizeof(MOTN_Bank));
    if (banks == NULL)
        return false;
    g_view.banks = banks;
    if (MOTN_Load(path, &g_view.banks[g_view.bankCount]))
        g_view.bankCount++;
    return true;
}

// Loads the MOTN files (.motn) of a folder
static void AddFolderMotionBanks(const char* folder) {
    FilePathList files = LoadDirectoryFilesEx(folder, ".motn", false);
    for (unsigned int i = 0; i < files.count; i++)
        AddMotionBank(files.paths[i]);
    UnloadDirectoryFiles(files);
}

static int CompareFolders(const void* a, const void* b) {
    return strcmp(*(const char* const*)a, *(const char* const*)b);
}

// Loads the MOTN files of the folders of the other PKS of the family of a folder named <family>_<suffix> (e.g. djhn_07):
// the folders <family>_* of the same parent, first <family>_mot, then the others in order. The cutscenes of such a PKS
// without motions play those of its family (scene/01/d000/djhn_07/SEQDATA7 plays the motions of djhn_01/M_JUCE1)
static void AddFamilyMotionBanks(const char* folder) {
    const char* name = GetFileName(folder);
    const char* underscore = strrchr(name, '_');
    if (underscore == NULL || underscore == name)
        return;
    int familyLength = (int)(underscore - name + 1); // With the underscore
    char parent[AUTHVIEW_PATH_SIZE];
    snprintf(parent, sizeof(parent), "%s", GetPrevDirectoryPath(folder));
    FilePathList folders = LoadDirectoryFilesEx(parent, "DIRS*", false); // Only the folders
    const char** family = calloc(folders.count + 1, sizeof(const char*));
    unsigned int count = 0;
    for (unsigned int i = 0; family != NULL && i < folders.count; i++) {
        const char* other = GetFileName(folders.paths[i]);
        if (DirectoryExists(folders.paths[i]) && strncmp(other, name, familyLength) == 0 && strcmp(other, name) != 0)
            family[count++] = folders.paths[i];
    }
    if (family != NULL) {
        qsort(family, count, sizeof(const char*), CompareFolders);
        for (int mot = 1; mot >= 0; mot--) {
            for (unsigned int i = 0; i < count; i++) {
                if (EqualsIgnoreCase(GetFileName(family[i]) + familyLength, "mot") == (mot == 1))
                    AddFolderMotionBanks(family[i]);
            }
        }
    }
    free(family);
    UnloadDirectoryFiles(folders);
}

// The MOTN files of the PKF/PKS files of the map of a cutscene of a mapinfo.bin, extracted or not (e.g. M_01JA of
// scene/01/jomo/common01.pks)
static void AddMapMotionBanks(void) {
    for (uint32_t i = 0; i < g_view.mapFiles.count; i++) {
        const MapFile* file = &g_view.mapFiles.files[i];
        if (!IsFileExtension(file->name, ".motn"))
            continue;
        MOTN_Bank* banks = realloc(g_view.banks, (g_view.bankCount + 1) * sizeof(MOTN_Bank));
        if (banks == NULL)
            return;
        g_view.banks = banks;
        bool loaded = file->packed != NULL ?
            MOTN_LoadFromMemory(file->packed->data, file->packed->size, file->name, &g_view.banks[g_view.bankCount]) :
            MOTN_Load(file->path, &g_view.banks[g_view.bankCount]);
        if (loaded)
            g_view.bankCount++;
    }
}

// The MOTN files of the folder of an AUTH file; if it has none, those of the other PKS of its family (see
// AddFamilyMotionBanks); the m_*.bin files of the map of the scene with the name of that folder, the name of its PKS
// (e.g. scene/01/yq14/m_01114.bin for scene/01/d000/yq14/SEQDATA2.auth); then the common motions of the game
// (motion/motion.bin, next to the scene folder), with the small motion IDs (e.g. 0x2DF, the walk of Ryo). For a
// cutscene of a mapinfo.bin: the m_*.bin files of its map (e.g. scene/03/nbik/m_01bike.bin), the MOTN files of the
// PKF/PKS files of its map (AddMapMotionBanks), then the common motions
static void LoadMotionBanks(const char* filepath) {
    char folder[AUTHVIEW_PATH_SIZE];
    snprintf(folder, sizeof(folder), "%s", GetDirectoryPath(filepath));
    if (g_view.cutscene < 0) {
        AddFolderMotionBanks(folder);
        if (g_view.bankCount == 0)
            AddFamilyMotionBanks(folder);
    }
    if (g_view.sceneFolder[0] != '\0') {
        const char* map = TextFormat("%s/%s", g_view.sceneFolder, GetFileName(folder));
        if (DirectoryExists(map)) {
            FilePathList mapFiles = LoadDirectoryFilesEx(map, ".bin", false);
            for (unsigned int i = 0; i < mapFiles.count; i++) {
                const char* name = GetFileName(mapFiles.paths[i]);
                if ((name[0] == 'm' || name[0] == 'M') && name[1] == '_')
                    AddMotionBank(mapFiles.paths[i]);
            }
            UnloadDirectoryFiles(mapFiles);
        }
    }
    if (g_view.cutscene >= 0)
        AddMapMotionBanks();
    char common[AUTHVIEW_PATH_SIZE];
    snprintf(common, sizeof(common), "%s", g_view.sceneFolder);
    if (common[0] != '\0' && ToParent(common) && ToParent(common)) {
        strncat(common, AUTHVIEW_COMMON_MOTIONS, sizeof(common) - strlen(common) - 1);
        if (FileExists(common))
            AddMotionBank(common);
    }
}

// Path of a model file of the CHRT scripts in a folder: <name>.chrm or <name>.mt5, without the extension of the script
// (always .MT5) and the $ and @ prefixes of some names. Returns false if there is none
static bool ModelPath(const char* folder, const char* file, char* path, size_t size) {
    while (*file == '$' || *file == '@')
        file++;
    const char* dot = strrchr(file, '.');
    int length = (int)(dot != NULL ? dot - file : strlen(file));
    const char* extensions[2] = { "chrm", "mt5" };
    for (int i = 0; i < 2; i++) {
        snprintf(path, size, "%s/%.*s.%s", folder, length, file, extensions[i]);
        if (FileExists(path))
            return true;
    }
    return false;
}

// Model file of a model name of the CHRT scripts in the files of the map of a cutscene of a mapinfo.bin, preferably of a
// package (the PKF/PKS files can have models with the same name). path is the file, or <package>/<name> for a packed
// file. Returns NULL if there is none
static const MapFile* MapModel(const char* file, const char* package, char* path, size_t size) {
    while (*file == '$' || *file == '@')
        file++;
    char base[64];
    snprintf(base, sizeof(base), "%s", file);
    char* dot = strchr(base, '.');
    if (dot != NULL)
        *dot = '\0';
    const MapFile* found = NULL;
    for (uint32_t i = 0; i < g_view.mapFiles.count; i++) {
        const MapFile* candidate = &g_view.mapFiles.files[i];
        if (!IsFileExtension(candidate->name, ".chrm;.mt5") || !EqualsIgnoreCase(GetFileNameWithoutExt(candidate->name), base))
            continue;
        if (found == NULL || (package != NULL && EqualsIgnoreCase(candidate->package, package) && !EqualsIgnoreCase(found->package, package)))
            found = candidate;
    }
    if (found != NULL && found->packed != NULL)
        snprintf(path, size, "%s/%s", found->package, found->name);
    else if (found != NULL)
        snprintf(path, size, "%s", found->path);
    return found;
}

// Model of a character of a CHRT script: the file of its first Image (of the DefImage with that name, or the name itself),
// in the folder of the script (none for a packed script), in the files of the map of a cutscene of a mapinfo.bin
// (preferably of the package of the script), or in the folder of the models of the scene (e.g.
// scene/01/model/chara/ykb_m.mt5, Ryo). packed is the file if it is packed, NULL otherwise. Returns false if the script
// has no such character with a model file
static bool ScriptModel(const CHRT_Script* script, const char* id, const char* folder, const char* package, char* path, size_t size,
    const IPAC_File** packed, bool* human) {
    for (uint32_t i = 0; i < script->commandCount; i++) {
        const CHRT_Command* command = &script->commands[i];
        if (!CHRT_IsCommand(command, "Character") || command->argCount == 0 || command->args[0].type != CHRT_ARG_LITERAL ||
            strcmp(command->args[0].literal, id) != 0)
            continue;
        CHRT_Character character;
        CHRT_ReadCharacter(script, i, &character);
        if (character.image == NULL || character.arm)
            continue;
        const char* file = CHRT_ImageFile(script, character.image);
        if (file == NULL)
            file = character.image;
        char models[AUTHVIEW_PATH_SIZE];
        snprintf(models, sizeof(models), "%s/%s", g_view.sceneFolder, AUTHVIEW_SCENE_MODELS);
        const MapFile* mapFile = NULL;
        if ((folder != NULL && ModelPath(folder, file, path, size)) || (mapFile = MapModel(file, package, path, size)) != NULL ||
            (g_view.sceneFolder[0] != '\0' && ModelPath(models, TextToLower(file), path, size))) {
            *packed = mapFile != NULL ? mapFile->packed : NULL;
            *human = character.human;
            return true;
        }
    }
    return false;
}

// Model of a character in a CHRT file
static bool ScriptFileModel(const char* scriptPath, const char* id, char* path, size_t size, const IPAC_File** packed, bool* human) {
    CHRT_Script script;
    if (!FileExists(scriptPath) || !CHRT_Load(scriptPath, &script))
        return false;
    char folder[AUTHVIEW_PATH_SIZE];
    snprintf(folder, sizeof(folder), "%s", GetDirectoryPath(scriptPath));
    bool found = ScriptModel(&script, id, folder, NULL, path, size, packed, human);
    CHRT_Free(&script);
    return found;
}

// Model of a character in the CHRT files of the map of a cutscene of a mapinfo.bin (e.g. the CHARA.chrt of
// scene/01/jomo/common01.pks), extracted or not. Returns the script where it was found, NULL if none
static const char* MapScriptsModel(const char* id, char* path, size_t size, const IPAC_File** packed, bool* human) {
    for (uint32_t i = 0; i < g_view.mapFiles.count; i++) {
        const MapFile* file = &g_view.mapFiles.files[i];
        CHRT_Script script;
        if (!IsFileExtension(file->name, ".chrt") || !(file->packed != NULL ?
            CHRT_LoadFromMemory(file->packed->data, file->packed->size, file->name, &script) : CHRT_Load(file->path, &script)))
            continue;
        char folder[AUTHVIEW_PATH_SIZE];
        if (file->path != NULL)
            snprintf(folder, sizeof(folder), "%s", GetDirectoryPath(file->path));
        bool found = ScriptModel(&script, id, file->path != NULL ? folder : NULL, file->package, path, size, packed, human);
        CHRT_Free(&script);
        if (found)
            return TextFormat("%s%s%s", file->package, file->package[0] != '\0' ? "/" : "", file->name);
    }
    return NULL;
}

// Model file of a character: in the CHRT scripts of the folder of the cutscene (e.g. the bus driver BUSS of
// scene/01/d000/buss), or of the PKF/PKS files of the map for a cutscene of a mapinfo.bin; in the folder of the character
// in the scene (scene/01/stream/humans/YAMA); or in the characters of the mapinfo.bin of the map, the parent folder of an
// AUTH file (Ryo, AKIR, in scene/01/d000). packed is the file if it is packed. Returns the script where it was found,
// NULL if none
static const char* FindCharacterModel(const char* id, const CHRT_Script* mapScript, const char* mapFolder, char* path, size_t size,
    const IPAC_File** packed, bool* human) {
    const char* source = NULL;
    if (g_view.cutscene >= 0)
        source = MapScriptsModel(id, path, size, packed, human);
    else {
        char folder[AUTHVIEW_PATH_SIZE];
        snprintf(folder, sizeof(folder), "%s", GetDirectoryPath(g_view.path));
        FilePathList scripts = LoadDirectoryFilesEx(folder, ".chrt", false);
        for (unsigned int i = 0; i < scripts.count && source == NULL; i++) {
            if (ScriptFileModel(scripts.paths[i], id, path, size, packed, human))
                source = TextFormat("%s", GetFileName(scripts.paths[i]));
        }
        UnloadDirectoryFiles(scripts);
    }
    if (source != NULL)
        return source;
    char humans[AUTHVIEW_PATH_SIZE];
    snprintf(humans, sizeof(humans), "%s/%s/%s/CHARA.chrt", g_view.sceneFolder, AUTHVIEW_HUMANS, id);
    if (g_view.sceneFolder[0] != '\0' && ScriptFileModel(humans, id, path, size, packed, human))
        return TextFormat("%s/%s/CHARA.chrt", AUTHVIEW_HUMANS, id);
    if (mapScript != NULL && ScriptModel(mapScript, id, mapFolder, NULL, path, size, packed, human))
        return g_view.cutscene >= 0 ? "mapinfo.bin" : "mapinfo.bin of the map";
    return NULL;
}

// Index of the loaded model of a file (on disk, or packed: path is then its package and name), for one character.
// Returns -1 if it can't be loaded
static int LoadModelFile(const char* path, const IPAC_File* packed) {
    AUTHView_Model* model = &g_view.models[g_view.modelCount++];
    snprintf(model->path, sizeof(model->path), "%s", path);
    model->loaded = packed != NULL ? MT5Render_LoadPackedModel(packed, &g_view.mapFiles.packed, &model->model) :
        MT5Render_LoadModel(path, &model->model);
    if (model->loaded) {
        model->lift = -MT5Render_Bottom(&model->model);
        model->top = MT5Render_Top(&model->model);
        model->body = MT5Pose_FindBody(&model->model.mesh, &model->bodyNodes);
        if (model->body)
            model->pose = malloc((model->model.mesh.nodeCount + 1) * sizeof(Matrix));
    }
    return model->loaded ? (int)(model - g_view.models) : -1;
}

// Last frame that the commands play of a motion
static uint32_t LastPlayedFrame(uint32_t id) {
    uint32_t last = 0;
    for (uint32_t i = 0; i < g_view.sequence.commandCount; i++) {
        const AUTH_Command* command = &g_view.sequence.commands[i];
        if (command->opcode == AUTH_COMMAND_MOTION && Arg(command, 1) == id && Arg(command, 3) > last)
            last = Arg(command, 3);
    }
    return last;
}

// Decodes the motions of the motion commands. Several banks can have the same ID (e.g. 0x1001 in m_01114.bin and
// m_0114.bin of scene/01/yq14): the motion is the first one that lasts up to the last frame that the commands play, or
// else the first one found
static bool LoadMotions(void) {
    const AUTH_Sequence* sequence = &g_view.sequence;
    g_view.motions = calloc(sequence->commandCount + 1, sizeof(AUTHView_Motion));
    if (g_view.motions == NULL)
        return false;
    for (uint32_t i = 0; i < sequence->commandCount; i++) {
        const AUTH_Command* command = &sequence->commands[i];
        if (command->opcode != AUTH_COMMAND_MOTION)
            continue;
        uint32_t id = Arg(command, 1);
        bool known = false;
        for (uint32_t m = 0; m < g_view.motionCount && !known; m++)
            known = g_view.motions[m].id == id;
        if (known)
            continue;
        AUTHView_Motion* motion = &g_view.motions[g_view.motionCount++];
        motion->id = id;
        uint32_t last = LastPlayedFrame(id);
        for (uint32_t b = 0; b < g_view.bankCount && !(motion->valid && motion->motion.frames >= last); b++) {
            int32_t index = MOTN_FindMotion(&g_view.banks[b], id);
            MOTN_Motion candidate;
            if (index < 0 || !MOTN_ReadMotion(&g_view.banks[b], (uint32_t)index, &candidate))
                continue;
            if (motion->valid && candidate.frames < last) {
                MOTN_FreeMotion(&candidate);
                continue;
            }
            if (motion->valid)
                MOTN_FreeMotion(&motion->motion);
            motion->motion = candidate;
            motion->valid = true;
            motion->name = MOTN_FindName(&g_view.banks[b], id);
        }    }
    return true;
}

static const MOTN_Motion* FindMotion(uint32_t id) {
    for (uint32_t i = 0; i < g_view.motionCount; i++) {
        if (g_view.motions[i].id == id)
            return g_view.motions[i].valid ? &g_view.motions[i].motion : NULL;
    }
    return NULL;
}

static bool LoadCharacterModels(void) {
    const AUTH_Sequence* sequence = &g_view.sequence;
    g_view.models = calloc(sequence->characterCount + 1, sizeof(AUTHView_Model));
    g_view.characterModels = calloc(sequence->characterCount + 1, sizeof(AUTHView_CharacterModel));
    g_view.instances = calloc(sequence->characterCount + 1, sizeof(MT5Render_Instance));
    if (g_view.models == NULL || g_view.characterModels == NULL || g_view.instances == NULL)
        return false;

    // The characters of the map
    char mapFolder[AUTHVIEW_PATH_SIZE];
    snprintf(mapFolder, sizeof(mapFolder), "%s", GetDirectoryPath(g_view.mapinfoPath));
    MAPINFO_File mapinfo;
    CHRT_Script mapScript;
    bool hasMapinfo = FileExists(g_view.mapinfoPath) && MAPINFO_Load(g_view.mapinfoPath, &mapinfo);
    bool hasMapScript = hasMapinfo && mapinfo.characters != NULL &&
        CHRT_LoadFromMemory(mapinfo.characters, mapinfo.charactersSize, g_view.mapinfoPath, &mapScript);

    for (uint32_t i = 0; i < sequence->characterCount; i++) {
        const AUTH_Character* character = &sequence->characters[i];
        AUTHView_CharacterModel* characterModel = &g_view.characterModels[i];
        characterModel->model = -1;
        if (!HasMovement(character))
            continue;
        char path[AUTHVIEW_PATH_SIZE];
        const IPAC_File* packed = NULL;
        characterModel->source = FindCharacterModel(character->id, hasMapScript ? &mapScript : NULL, mapFolder, path, sizeof(path),
            &packed, &characterModel->human);
        if (characterModel->source != NULL) {
            // A copy of the source is kept: TextFormat reuses its buffers
            char* source = malloc(strlen(characterModel->source) + 1);
            if (source != NULL)
                strcpy(source, characterModel->source);
            characterModel->source = source;
            characterModel->model = LoadModelFile(path, packed);
        }
        if (characterModel->model < 0)
            printf("[AUTHView_Open]: No model for %s (%s)\n", character->id, characterModel->source != NULL ? "invalid model file" : "not found");
        else
            printf("  %s: %s (%s)\n", character->id, GetFileName(path), characterModel->source);
    }
    if (hasMapScript)
        CHRT_Free(&mapScript);
    if (hasMapinfo)
        MAPINFO_Free(&mapinfo);
    return true;
}

static void ResetCamera(void) {
    FreeCamera_Reset(&g_view.camera, g_view.center, (Vector3) { sinf(0.6f), 0.0f, cosf(0.6f) }, g_view.radius, AUTHVIEW_FOVY);
    g_view.camera.pitch = 0.6f;
}

void AUTHView_Close(void) {
    if (g_view.open)
        g_options.fly = g_view.camera.fly;
    AUTHSounds_Free();
    for (uint32_t i = 0; i < g_view.modelCount; i++) {
        if (g_view.models[i].loaded)
            MT5Render_FreeModel(&g_view.models[i].model);
        free(g_view.models[i].pose);
    }
    for (uint32_t i = 0; i < g_view.motionCount; i++) {
        if (g_view.motions[i].valid)
            MOTN_FreeMotion(&g_view.motions[i].motion);
    }
    free(g_view.motions);
    for (uint32_t i = 0; g_view.characterModels != NULL && i < g_view.sequence.characterCount; i++)
        free((char*)g_view.characterModels[i].source);
    if (g_view.open)
        MT5Render_Free(&g_view.render);
    free(g_view.models);
    free(g_view.characterModels);
    free(g_view.instances);
    AUTH_Free(&g_view.sequence);
    for (uint32_t i = 0; i < g_view.bankCount; i++)
        MOTN_Free(&g_view.banks[i]);
    free(g_view.banks);
    free(g_view.labels);
    free(g_view.characterLabels);
    free(g_view.shownCharacters);
    MapFiles_Free(&g_view.mapFiles);
    memset(&g_view, 0, sizeof(g_view));
}

// Shows a loaded cutscene, of the AUTH file filepath or of the TRCK chunk cutscene of the SCEX chunk of the mapinfo.bin
// filepath (cutscene -1 for an AUTH file), replacing the current one
static bool Start(const AUTH_Sequence* sequence, const char* filepath, int cutscene) {
    AUTHView_Close();
    snprintf(g_view.path, sizeof(g_view.path), "%s", filepath);
    g_view.sequence = *sequence;
    g_view.cutscene = cutscene;
    g_view.current = -1;
    g_view.selectedCharacter = -1;
    for (char* c = g_view.path; *c != '\0'; c++) {
        if (*c == '\\')
            *c = '/';
    }
    // The map of an AUTH file is the parent folder of its folder (of its PKS)
    char mapFolder[AUTHVIEW_PATH_SIZE];
    snprintf(mapFolder, sizeof(mapFolder), "%s", GetDirectoryPath(g_view.path));
    if (cutscene < 0)
        ToParent(mapFolder);
    snprintf(g_view.mapinfoPath, sizeof(g_view.mapinfoPath), "%s", cutscene < 0 ? TextFormat("%s/mapinfo.bin", mapFolder) : g_view.path);
    FindSceneFolder(g_view.path, g_view.sceneFolder, sizeof(g_view.sceneFolder));
    if (cutscene >= 0 && !MapFiles_Load(mapFolder, ".mt5;.chrm;.chrt;.motn", &g_view.mapFiles)) {
        fputs("[AUTHView_Open]: Memory error\n", stderr);
        AUTHView_Close();
        return false;
    }
    LoadMotionBanks(g_view.path);
    printf("%s%s: cutscene, %u frames, %u characters, %u commands, %u sounds, %u MOTN files\n", GetFileName(g_view.path),
        cutscene >= 0 ? TextFormat(" #%d", cutscene) : "", sequence->duration, sequence->characterCount, sequence->commandCount,
        sequence->soundCount, g_view.bankCount);
    if (!LoadMotions() || !CreateLabels() || !LoadCharacterModels()) {
        fputs("[AUTHView_Open]: Memory error\n", stderr);
        AUTHView_Close();
        return false;
    }
    MT5Render_Init(&g_view.render);
    g_view.open = true;
    // The program of the map loads the bank of the sounds with the name of the PKS of an AUTH file, or of the map
    AUTHSounds_Load(g_view.sceneFolder[0] != '\0' ? g_view.sceneFolder : NULL, g_view.mapinfoPath,
        GetFileName(GetDirectoryPath(g_view.path)), &g_view.sequence);
    ComputeBounds();
    ResetCamera();
    FreeCamera_SetFly(&g_view.camera, g_options.fly);
    g_view.playing = true;
    g_view.jumped = true;
    return true;
}

bool AUTHView_Open(const char* filepath) {
    AUTH_Sequence sequence;
    return AUTH_Load(filepath, &sequence) && Start(&sequence, filepath, -1);
}

bool AUTHView_OpenMapCutscene(const char* filepath, uint32_t cutscene) {
    MAPINFO_File file;
    if (!MAPINFO_Load(filepath, &file))
        return false;
    AUTH_Sequence sequence;
    bool loaded = cutscene < file.cutsceneCount && AUTH_LoadFromMemory(file.data + file.cutscenes[cutscene].offset,
        file.cutscenes[cutscene].size, TextFormat("%s #%u", filepath, cutscene), &sequence);
    MAPINFO_Free(&file);
    return loaded && Start(&sequence, filepath, (int)cutscene);
}

bool AUTHView_IsMapCutscene(void) {
    return g_view.open && g_view.cutscene >= 0;
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
    g_view.jumped = true;
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
        Seek(0.0f);
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
    // The view of the map is open behind its cutscenes
    if (AUTHView_IsMapCutscene() && MAPINFOView_IsOpen()) {
        if (Toolbar_Button(ICON_ARROW_LEFT, "Map", "Close the cutscene and go back to its map", KEY_BACKSPACE)) {
            AUTHView_Close();
            return;
        }
        Toolbar_Separator();
    }
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
    Toolbar_Toggle(ICON_TEXT_A, "Labels", "Show the name, sound and motion over the characters", KEY_L, &g_options.labels);
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
    AUTHSounds_Update(g_view.time, g_view.playing && !g_view.seeking, g_view.jumped);
    g_view.jumped = false;
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

// The last motion that the character started at or before the current frame: it keeps the pose of its last frame after
// its end. NULL if none
static const AUTH_Command* PoseCommand(uint32_t idValue) {
    const AUTH_Command* motion = NULL;
    uint32_t frame = CurrentFrame();
    for (uint32_t i = 0; i < g_view.sequence.commandCount && g_view.sequence.commands[i].frame <= frame; i++) {
        const AUTH_Command* command = &g_view.sequence.commands[i];
        if (command->opcode == AUTH_COMMAND_MOTION && Arg(command, 0) == idValue)
            motion = command;
    }
    return motion;
}

// Puts the model of a character in the pose of its motion at the current time (from the first frame of the command to
// its last one), or in the pose of the file if it has none
static void PoseCharacter(uint32_t index) {
    const AUTH_Character* character = &g_view.sequence.characters[index];
    AUTHView_Model* model = &g_view.models[g_view.characterModels[index].model];
    const AUTH_Command* command = model->body ? PoseCommand(character->idValue) : NULL;
    const MOTN_Motion* motion = command != NULL ? FindMotion(Arg(command, 1)) : NULL;
    float frame = 0.0f;
    if (motion != NULL) {
        float first = (float)Arg(command, 2), last = fminf((float)Arg(command, 3), (float)motion->frames);
        frame = Clamp(first + g_view.time * AUTH_FRAME_RATE - command->frame, first, fmaxf(first, last));
    }
    model->posed = motion != NULL && model->pose != NULL && MT5Pose_Compute(&model->model.mesh, &model->bodyNodes, motion, frame, model->pose);
    MT5Render_Pose(&model->model, model->posed ? model->pose : NULL);
}

// As the nodes of the models: rotation around X, Y and Z, and translation, at the current time. The models of the persons
// are raised to stand at their position, unless a motion poses them, with the feet on the floor
static Matrix CharacterTransform(uint32_t index) {
    const AUTH_Character* character = &g_view.sequence.characters[index];
    const AUTHView_CharacterModel* characterModel = &g_view.characterModels[index];
    const AUTHView_Model* model = &g_view.models[characterModel->model];
    Vector3 position = CurvePoint(&character->curves[AUTH_CHARACTER_X], g_view.time);
    Vector3 rotation = CurvePoint(&character->curves[AUTH_CHARACTER_ROTATION_X], g_view.time);
    Matrix matrix = MatrixTranslate(0.0f, characterModel->human && !model->posed ? model->lift : 0.0f, 0.0f);
    matrix = MatrixMultiply(matrix, MatrixRotateX(rotation.x * DEG2RAD));
    matrix = MatrixMultiply(matrix, MatrixRotateY(rotation.y * DEG2RAD));
    matrix = MatrixMultiply(matrix, MatrixRotateZ(rotation.z * DEG2RAD));
    return MatrixMultiply(matrix, MatrixTranslate(position.x, position.y, position.z));
}

// Height of the top of a character above its position: of its model, or of the cylinder
static float CharacterHeight(uint32_t index) {
    const AUTHView_CharacterModel* characterModel = &g_view.characterModels[index];
    if (characterModel->model < 0)
        return AUTHVIEW_HEIGHT;
    const AUTHView_Model* model = &g_view.models[characterModel->model];
    return model->top + (characterModel->human ? model->lift : 0.0f);
}

// With their model, or as cylinders with a line to the direction they face if it wasn't found
static void DrawCharacters(Camera3D camera) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < g_view.sequence.characterCount; i++) {
        int model = g_view.characterModels[i].model;
        if (!IsShown(i) || model < 0)
            continue;
        PoseCharacter(i);
        Matrix transform = CharacterTransform(i);
        MT5Render_Model* instanced = &g_view.models[model].model;
        g_view.instances[count++] = (MT5Render_Instance) { instanced, transform, Vector3Transform(instanced->mesh.center, transform), 0 };
    }
    MT5Render_DrawInstances(&g_view.render, g_view.instances, count, camera);

    for (uint32_t i = 0; i < g_view.sequence.characterCount; i++) {
        const AUTH_Character* character = &g_view.sequence.characters[i];
        if (!IsShown(i) || g_view.characterModels[i].model >= 0)
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
        Vector3 head = Vector3Add(CurvePoint(&character->curves[AUTH_CHARACTER_X], g_view.time), (Vector3) { 0.0f, CharacterHeight(i) + 0.15f, 0.0f });
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

// The characters, whose eye shows or hides them in the scene; a click on one selects it and looks at it with the free
// camera, or unselects it. Then the commands by lane of the timeline, with the one of the current frame selected while
// no character is; a click on one goes to its frame. The current command is followed while playing or seeking, without
// expanding its group
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
        int flags = ((int)i == g_view.selectedCharacter ? TREE_SELECTED : 0) | (moves ? 0 : TREE_DIMMED);
        if (Tree_Item(tree, g_view.characterLabels[i], flags, moves ? &g_view.shownCharacters[i] : NULL))
            clickedCharacter = (int)i;
    }
    Tree_EndGroup(tree);
    for (int g = GROUP_MOTIONS; g < GROUP_COUNT; g++) {
        Tree_BeginGroup(tree, g_groupNames[g], &g_options.expanded[g], NULL);
        for (uint32_t i = 0; i < sequence->commandCount; i++) {
            if ((int)CommandGroup(&sequence->commands[i]) == g &&
                Tree_Item(tree, g_view.labels[i], (int)i == current && g_view.selectedCharacter < 0 ? TREE_SELECTED : 0, NULL))
                clickedCommand = (int)i;
        }
        Tree_EndGroup(tree);
    }
    Tree_End(tree);

    if (clickedCommand >= 0) {
        SeekFrame(sequence->commands[clickedCommand].frame);
        g_view.selectedCharacter = -1;
    }
    if (clickedCharacter >= 0 && clickedCharacter == g_view.selectedCharacter)
        g_view.selectedCharacter = -1;
    else if (clickedCharacter >= 0) {
        g_view.selectedCharacter = clickedCharacter;
        const AUTH_Character* character = &sequence->characters[clickedCharacter];
        if (!HasMovement(character))
            return;
        g_options.cutsceneCamera = false;
        g_view.camera.target = Vector3Add(CurvePoint(&character->curves[AUTH_CHARACTER_X], g_view.time),
            (Vector3) { 0.0f, CharacterHeight((uint32_t)clickedCharacter) * 0.5f, 0.0f });
        g_view.camera.distance = 6.0f;
    }
}

static const char* FormatTime(float seconds) {
    int centiseconds = (int)(seconds * 100.0f);
    return TextFormat("%d:%02d.%02d", centiseconds / 6000, centiseconds / 100 % 60, centiseconds % 100);
}

static const char* g_characterCurves[AUTH_CHARACTER_CURVES] = { "X", "Y", "Z", "Rotation X", "Rotation Y", "Rotation Z", "Unknown 6",
    "Unknown 7", "Unknown 8" };

// Its values at the current time, what it does, and the keys of its curves
static void CharacterProperties(Properties* properties, uint32_t index) {
    const AUTH_Character* character = &g_view.sequence.characters[index];
    const AUTHView_CharacterModel* characterModel = &g_view.characterModels[index];
    Properties_Text(properties, "ID", character->id);
    Properties_Hex(properties, "ID value", character->idValue);
    if (HasMovement(character)) {
        Properties_Text(properties, "Model", characterModel->model >= 0 ? GetFileName(g_view.models[characterModel->model].path) :
            characterModel->source != NULL ? "invalid model file" : "not found");
        if (characterModel->source != NULL)
            Properties_Text(properties, "Found in", characterModel->source);
        Properties_Bool(properties, "Human", characterModel->human);
    }
    if (HasMovement(character)) {
        Properties_Section(properties, "Now");
        Vector3 position = CurvePoint(&character->curves[AUTH_CHARACTER_X], g_view.time);
        Vector3 rotation = CurvePoint(&character->curves[AUTH_CHARACTER_ROTATION_X], g_view.time);
        Properties_Vector(properties, "Position", (const float[]) { position.x, position.y, position.z }, 3);
        Properties_Vector(properties, "Rotation", (const float[]) { rotation.x, rotation.y, rotation.z }, 3);
        const AUTH_Command* motion = CharacterMotion(character->idValue);
        const char* name = motion != NULL ? MotionName(Arg(motion, 1)) : NULL;
        Properties_Text(properties, "Motion", motion == NULL ? "none" : name != NULL ? name : TextFormat("0x%X", Arg(motion, 1)));
        if (characterModel->model >= 0) {
            const AUTHView_Model* model = &g_view.models[characterModel->model];
            Properties_Text(properties, "Pose", model->posed ? "of the motion" : !model->body ? "of the file: not a body" :
                "of the file: no motion found");
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
    const AUTH_Command* command = &g_view.sequence.commands[index];
    Properties_Int(properties, "Frame", command->frame);
    Properties_Text(properties, "Time", FormatTime(command->frame / AUTH_FRAME_RATE));
    Properties_Int(properties, "Opcode", command->opcode);
    Properties_Text(properties, "Command", CommandText(command));
    if (command->opcode == AUTH_COMMAND_VOICE || command->opcode == AUTH_COMMAND_SOUND)
        Properties_Text(properties, "Audio", AUTHSounds_HasSound(index) ? "found" : "not found");
    Properties_Section(properties, TextFormat("%u arguments", command->argCount));
    for (uint32_t i = 0; i < command->argCount; i++)
        Properties_Text(properties, TextFormat("%u", i), TextFormat("0x%08X  %d  %g", command->args[i], (int32_t)command->args[i],
            AUTH_ArgFloat(command, i)));
}

// Of the selected character, or of the command of the current frame
static void DrawProperties(Rectangle bounds) {
    Properties* properties = &g_view.properties;
    const AUTH_Sequence* sequence = &g_view.sequence;
    int current = CurrentCommand();
    if (g_view.selectedCharacter >= 0) {
        const AUTH_Character* character = &sequence->characters[g_view.selectedCharacter];
        Properties_Begin(properties, bounds, TextFormat("Character %s", character->id));
        CharacterProperties(properties, (uint32_t)g_view.selectedCharacter);
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
    if (!g_view.open || area.width <= 0.0f || area.height <= 0.0f)
        return;

    Font font = GuiGetFont();
    float fontSize = (float)GuiGetStyle(DEFAULT, TEXT_SIZE);
    const AUTH_Sequence* sequence = &g_view.sequence;
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
    const char* status = TextFormat("Frame %u/%u   %s / %s", CurrentFrame(), sequence->duration, FormatTime(g_view.time),
        FormatTime(Duration()));
    DrawTextEx(font, status, (Vector2) { x, controls.y + (controlsHeight - fontSize) * 0.5f }, fontSize, 1.0f, LIGHTGRAY);
}

void AUTHView_Status(ViewStatus* status) {
    const AUTH_Sequence* sequence = &g_view.sequence;
    snprintf(status->file, sizeof(status->file), "%s", g_view.path);
    AUTHSounds_Info sounds;
    AUTHSounds_GetInfo(&sounds);
    snprintf(status->info, sizeof(status->info), "%s%s, %u characters, %u commands, voices %u/%u, effects %u/%u%s%s",
        g_view.cutscene >= 0 ? TextFormat("Cutscene %d, ", g_view.cutscene) : "", FormatTime(Duration()), sequence->characterCount, sequence->commandCount, sounds.voices, sounds.voiceCommands, sounds.effects, sounds.effectCommands,
        sounds.bank[0] != '\0' ? " in " : "", sounds.bank);
    if (g_options.cutsceneCamera)
        snprintf(status->hint, sizeof(status->hint), "Click the timeline: seek");
    else
        snprintf(status->hint, sizeof(status->hint), "%s", FreeCamera_Help(&g_view.camera));
}
