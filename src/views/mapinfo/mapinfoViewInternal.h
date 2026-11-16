#ifndef __MAPINFOVIEWINTERNAL_H__
#define __MAPINFOVIEWINTERNAL_H__

// The state of the view of the maps, shared by its files: mapinfoView.c (opening, selection, drawing and tree),
// mapinfoModels.c, mapinfoDate.c, mapinfoItems.c and mapinfoProperties.c

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include "app/gameDB.h"
#include "core/text.h"
#include "formats/animation/auth.h"
#include "formats/map/mapinfo.h"
#include "formats/map/weather.h"
#include "formats/script/chrt.h"
#include "game/mapDate.h"
#include "game/mapFiles.h"
#include "game/mapProgram.h"
#include "render/bounds.h"
#include "render/freeCamera.h"
#include "render/mt5Render.h"
#include "render/scene3D.h"
#include "scenario/scnData.h"
#include "scenario/scnDisplay.h"
#include "ui/properties.h"
#include "ui/tree.h"

#define MAPINFOVIEW_LABEL_SIZE   160

// Kinds of the items of the tree, in this order
typedef enum {
    ITEM_INFO,
    ITEM_MAP_MODEL,
    ITEM_DOOR,
    ITEM_CAMERA,
    ITEM_LIGHT,
    ITEM_SHAPE,
    ITEM_CHARACTER,
    ITEM_CUTSCENE,
    ITEM_SEQUENCE,      // A cutscene that the program loads in the table of cutscenes
    ITEM_FLAG,          // A flag or a byte value of the game that the program reads or sets
    ITEM_KIND_COUNT,
} ItemKind;

typedef struct {
    ItemKind    kind;
    uint32_t    index;  // In the models of the map, doors, cameras, lights, shapes or characters
} Item;

// Groups of the tree, one per kind of item, shown or hidden by their eye (not the information)
typedef enum {
    LAYER_INFO,
    LAYER_MAP,
    LAYER_DOORS,
    LAYER_CAMERAS,
    LAYER_LIGHTS,
    LAYER_SHAPES,
    LAYER_CHARACTERS,
    LAYER_CUTSCENES,        // Not drawn: a cutscene is played by the view of the cutscenes
    LAYER_FLAGS,            // Not drawn: the flags of the game used by the program
    LAYER_GROUPS,           // Subgroups of the shapes, one per COLS group, MAPINFO_GROUP_COUNT
    LAYER_COUNT = LAYER_GROUPS + MAPINFO_GROUP_COUNT,
} Layer;

// Names of the layers (mapinfoView.c)
extern const char* g_mapLayerNames[LAYER_COUNT];


// A CHRT script of a package of the map, with the characters of a situation of the map (e.g. the furniture of the Hazuki
// house in common01 of scene/01/jomo, the night in yoru of scene/01/d000). A package with a character ID of a previous
// one is a variant of it (e.g. dcb0 to dcb4 of scene/01/dcbn): the program loads one of them
typedef struct {
    uint32_t            script;         // In g_map.scripts
    char                name[IPAC_FILENAME_SIZE]; // Of its PKF/PKS files or of their extracted folder
    uint32_t            characterCount;
    int                 variantOf;      // Previous package with one of its character IDs, -1 if none
    bool                shown;          // Its characters are shown; at first, those of the packages that aren't variants
    bool                expanded;       // In the tree; at first, the shown ones
} MapPackage;

// A character of the CHRD chunk or of a CHRT script of a package of the map. The displayed ones (positioned in the scene,
// not an arm of Ryo, with an image) that are in the map are drawn with their model, sleeping or not, if they are shown
typedef struct {
    CHRT_Character      info;           // Its image points into the buffer of its script. Its position and angle can be
                                        // edited
    CHRT_Character      original;       // As read from the script, to restore the edits
    uint32_t            script;         // In g_map.scripts
    int                 package;        // In g_map.packages, -1 for the CHRD chunk
    int                 model;          // In g_map.models, -1 if it isn't drawn with a model
    bool                shown;          // The CHRD chunk or a shown package, and no character shown before has its ID. Drawn
                                        // if it is visible in the tree too
    const char*         error;          // Why the model of a displayed character couldn't be loaded, NULL otherwise
} MapCharacter;

#define MAPINFOVIEW_CUTSCENE_IDS 5 // Characters of a cutscene named in the tree

// A cutscene of the SCEX chunk (a TRCK chunk), played by the view of the cutscenes
typedef struct {
    bool                valid;          // An AUTH cutscene, not an empty chunk
    uint32_t            duration;       // Frames
    uint32_t            characterCount;
    uint32_t            commandCount;
    uint32_t            motions;        // Commands of each kind
    uint32_t            voices;
    uint32_t            sounds;
    char                ids[MAPINFOVIEW_LABEL_SIZE]; // Of the characters, separated by spaces
} MapCutscene;

typedef struct {
    bool                open;
    char                filename[1024]; // Path of the open file
    MapCutscene*        cutscenes;      // file.cutsceneCount of them
    MAPINFO_File        file;
    SD_Data             program;        // What the program of the map does with it (mapProgram.h), empty if none
    uint32_t            sequenceCount;
    uint32_t*           sequences;      // The cutscenes that the program loads in the table of cutscenes: their first
                                        // reference (SD_SEQUENCE) in program, one per slot, file and package
    bool*               sequenceFound;  // Of each one: its AUTH file is in the game files (e.g. scene/01/JHD0/seqdataD.bin
                                        // isn't)
    uint32_t            flagCount;
    int32_t*            flags;          // The flags (0 to 1023) and the bytes (SD_FLAGS + 0 to 31) used by the program, sorted
    uint32_t            scriptCount;
    CHRT_Script*        scripts;        // The CHRD chunk, then the CHRT files of the map
    uint32_t            packageCount;
    MapPackage*         packages;       // The base package first, then those with more characters
    uint32_t            characterCount;
    MapCharacter*       characters;     // Of the CHRD chunk, then of each package
    uint32_t            modelledCharacters; // Shown with their model
    MT5Render           render;
    uint32_t            modelCount;
    MT5Render_Model*    models;         // The models of the map, then the models of the doors and of the characters
    uint32_t            mapModelCount;
    char                (*mapModelNames)[MAPINFOVIEW_LABEL_SIZE]; // File and package of each model of the map
    int*                mapModelSlots;  // Slot of each model of the map for the program (MapModelDisplay, 0x098: 0 for
                                        // MAP.MT5, n for MAPnn.MT5), -1 if none
    char                (*mapModelPackages)[IPAC_FILENAME_SIZE]; // Package of each model of the map, empty if none
    bool*               mapModelVariants; // The program loads the package of the model: a variant of the map
    bool*               mapModelUnloaded; // The game doesn't load the model: MAPnn.MT5 with nn above the MAPR chunk (0+)
                                        // (e.g. map01.mt5 of scene/02/toki, whose MAPR is 0), hidden in the tree
    bool                hasDate;        // The program can be emulated at a date (mapDate.h)
    uint32_t            instanceCount;
    MT5Render_Instance* instances;      // Each model of the map (mapModelCount), then each door with its model
                                        // (loadedDoors), then the shown characters with their model (from
                                        // characterInstances)
    uint32_t            loadedDoors;    // Doors with their model
    uint32_t            characterInstances;
    MT5Render_Instance* visibleInstances; // The instances drawn, filled before drawing them
    MT5Render_Light*    renderLights;   // The lights of the group 0 for the characters, filled before drawing them
    MT5Render_Lighting  lighting;
    int32_t*            doorInstances;  // Instance of each door, -1 if its model couldn't be loaded
    const char**        doorErrors;     // Why the model of each door couldn't be loaded, NULL if it was
    MAPINFO_Door*       originalDoors;  // The doors and the lights as read from the file, to restore the edits
    MAPINFO_Light*      originalLights;
    uint32_t            itemCount;
    Item*               items;          // Of each kind in ItemKind order, and in the order of their arrays
    uint32_t            firstItems[ITEM_KIND_COUNT]; // First item of each kind
    char                (*labels)[MAPINFOVIEW_LABEL_SIZE];
    bool*               failedItems;    // Doors and characters without their model
    bool*               visibleItems;   // Their eye in the tree
    Tree                tree;
    Properties          properties;     // Of the selected item
    bool                mapExpanded;    // The characters of the CHRD chunk in the tree
    int                 selected;       // Item, -1 if none
    bool                mapCamera;      // Show the map from the selected camera
    float               time;           // Seconds, for the animated cameras
    float               start[AUTH_CAMERA_CURVES]; // Active camera when the selected one started, for the transitions
                                                   // from the current camera (AUTH_CameraCurve order)
    FreeCamera         camera;
    bool                fit;            // The camera is fitted to the map in the next draw, when the size of the view is known
    Bounds              bounds;         // Box around the map
    Vector3             center;
    float               radius;
} MAPINFOView_State;

// Kept when another map is opened. The groups of shapes, the lights and the cameras are hidden at first
typedef struct {
    bool                layers[LAYER_COUNT];    // Visible
    bool                expanded[LAYER_COUNT];  // In the tree
    bool                labels;
    bool                lights;                 // The characters are lit by the lights of the group 0 of the LGHT chunk
    bool                fly;
    bool                date;                   // The models of the map are those that the program shows at the date
    bool                dateSet;                // The date is set (at the first map with weather)
    WEATHER_Date        gameDate;               // Its hour is one of g_hours
    int                 hour;                   // In g_hours
} MAPINFOView_Options;
// The hours of the date (g_hours of mapinfoDate.c), for the toolbar
#define MAPINFOVIEW_HOURS "10:00;15:00;19:30;23:00"

// The open map and the options of the view (mapinfoView.c)
extern MAPINFOView_State g_map;
extern MAPINFOView_Options g_mapOptions;

// Point of a shape in the scene: the shapes have x and z coordinates
static inline Vector3 ShapePoint(float x, float y, float z) {
    return (Vector3) { x, y, -z };
}

// Height of the point of a shape: the height of HGHT, the height of each corner of UNDU, 0 for the rest
static inline float ShapeHeight(const MAPINFO_Shape* shape, uint32_t point) {
    if (shape->group == MAPINFO_GROUP_HGHT)
        return shape->height;
    if (shape->group == MAPINFO_GROUP_UNDU)
        return shape->heights[point < 4 ? point : 0];
    return 0.0f;
}

// mapinfoView.c
bool MAPINFOView_IsItemVisible(ItemKind kind, uint32_t index);
float MAPINFOView_CameraDuration(const MAPINFO_Camera* camera);
uint32_t MAPINFOView_CameraKeys(const MAPINFO_Camera* camera);
void MAPINFOView_MapCameraValues(const MAPINFO_Camera* source, float values[AUTH_CAMERA_CURVES]);
void MAPINFOView_UpdateDoor(uint32_t door);

// mapinfoModels.c
const char* MAPINFOModels_PackageName(int package);
Vector3 MAPINFOModels_CharacterPosition(const CHRT_Character* character);
void MAPINFOModels_ShowCharacters(void);
Matrix MAPINFOModels_DoorTransform(const MAPINFO_Door* door);
void MAPINFOModels_Load(const char* folder);

// mapinfoDate.c
void MAPINFODate_FindVariants(void);
void MAPINFODate_Set(WEATHER_Date date);
void MAPINFODate_Init(void);
void MAPINFODate_Move(int days);
void MAPINFODate_NextEvents(void);
const char* MAPINFODate_Text(const WEATHER_Date* date);
bool MAPINFODate_IsHidden(uint32_t model);
const char* MAPINFODate_MapModelText(uint32_t model);
void MAPINFODate_Properties(Properties* properties);

// mapinfoItems.c
uint32_t MAPINFOItems_ProgramCount(SD_Kind kind, int32_t number, const char* name);
bool MAPINFOItems_SameReference(const SD_Reference* a, const SD_Reference* b);
const char* MAPINFOItems_ProgramCalls(SD_Kind kind, int32_t number, const char* name);
bool MAPINFOItems_FindSequences(void);
const SD_Reference* MAPINFOItems_Sequence(uint32_t index);
int32_t MAPINFOItems_CameraNumber(const char* id);
const char* MAPINFOItems_CameraCharacters(int32_t number);
int32_t MAPINFOItems_FlagKey(const SD_Reference* reference);
bool MAPINFOItems_FindFlags(void);
const char* MAPINFOItems_FlagName(uint32_t index);
const char* MAPINFOItems_PackageText(const MapPackage* package);
Vector3 MAPINFOItems_ShapeCenter(const MAPINFO_Shape* shape);
const char* MAPINFOItems_ShapeType(const MAPINFO_Shape* shape);
bool MAPINFOItems_ReadCutscenes(void);
const char* MAPINFOItems_CutsceneNames(const MapCutscene* cutscene, int max);
bool MAPINFOItems_Create(void);

// mapinfoProperties.c
void MAPINFOProperties_Draw(Rectangle bounds);

#endif
