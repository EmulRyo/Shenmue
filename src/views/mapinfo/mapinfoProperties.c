// The properties of the selected item of the view of the maps
#include "views/mapinfo/mapinfoViewInternal.h"

// The calls that read it and those that set it, with the values set
static void FlagProperties(Properties* properties, uint32_t index) {
    int32_t key = g_map.flags[index];
    bool byte = key >= SD_FLAGS;
    Properties_Text(properties, "Kind", byte ? "byte value of the game (0x0C225B68 in 1ST_READ.BIN)" :
        "flag of the game (bits at 0x0C225AE8 in 1ST_READ.BIN)");
    Properties_Int(properties, "Number", byte ? key - SD_FLAGS : key);
    Properties_Text(properties, "Read by the program", MAPINFOItems_ProgramCalls(byte ? SD_BYTE_READ : SD_FLAG_READ, byte ? key - SD_FLAGS : key, NULL));
    Properties_Text(properties, "Set by the program", MAPINFOItems_ProgramCalls(byte ? SD_BYTE_SET : SD_FLAG_SET, byte ? key - SD_FLAGS : key, NULL));
    // The different values set, each one once (SD_UNKNOWN for those that aren't constant)
    int32_t values[8];
    uint32_t valueCount = 0;
    for (uint32_t i = 0; i < g_map.program.count; i++) {
        const SD_Reference* reference = &g_map.program.references[i];
        if ((reference->kind != SD_FLAG_SET && reference->kind != SD_BYTE_SET) || MAPINFOItems_FlagKey(reference) != key)
            continue;
        bool found = false;
        for (uint32_t v = 0; v < valueCount && !found; v++)
            found = values[v] == reference->value;
        if (!found && valueCount < 8)
            values[valueCount++] = reference->value;
    }
    char text[MAPINFOVIEW_LABEL_SIZE] = "";
    int length = 0;
    for (uint32_t v = 0; v < valueCount && length < (int)sizeof(text); v++)
        length += snprintf(text + length, sizeof(text) - length, v > 0 ? ", %s" : "%s",
            values[v] == SD_UNKNOWN ? "computed" : TextFormat("%d", values[v]));
    if (valueCount > 0)
        Properties_Text(properties, "Values set", text);
}

static const char* g_cameraCurves[AUTH_CAMERA_CURVES] = { "X", "Y", "Z", "Target X", "Target Y", "Target Z", "Roll", "Field of view" };

// A model of the map: its slot and package, and what the program does with it at the date
static void MapModelProperties(Properties* properties, uint32_t index) {
    int slot = g_map.mapModelSlots != NULL ? g_map.mapModelSlots[index] : -1;
    const char* package = g_map.mapModelPackages != NULL ? g_map.mapModelPackages[index] : "";
    if (slot >= 0)
        Properties_Int(properties, "Slot", slot);
    Properties_Text(properties, "Package", package[0] != '\0' ? package : "none");
    if (g_map.hasDate && g_mapOptions.date)
        Properties_Text(properties, "At the date", MAPINFODate_MapModelText(index));
    Properties_Section(properties, "Model");
}

// The chunks of the file, when nothing is selected
static void FileProperties(Properties* properties) {
    const MAPINFO_File* file = &g_map.file;
    const GameDB_Map* map = GameDB_MapOfFile(g_map.filename);
    Properties_Note(properties, "Select an item of the tree");
    if (map != NULL) {
        Properties_Text(properties, "Map", map->name);
        if (map->area[0] != '\0')
            Properties_Text(properties, "Area", map->area);
    }
    MAPINFODate_Properties(properties);
    Properties_Section(properties, TextFormat("%u chunks", file->chunkCount));
    for (uint32_t i = 0; i < file->chunkCount; i++)
        Properties_Text(properties, file->chunks[i].id, TextFormat("0x%X bytes at 0x%X", file->chunks[i].size, file->chunks[i].offset));
}

static void CameraProperties(Properties* properties, uint32_t index) {
    const MAPINFO_Camera* camera = &g_map.file.cameras[index];
    float values[AUTH_CAMERA_CURVES];
    MAPINFOView_MapCameraValues(camera, values);
    Properties_Text(properties, "ID", camera->id);
    Properties_Hex(properties, "Flags", camera->flags);
    Properties_Bool(properties, "From the current camera", (camera->flags & MAPINFO_CAMERA_FROM_CURRENT) != 0);
    Properties_Bool(properties, "Relative", (camera->flags & MAPINFO_CAMERA_RELATIVE) != 0);
    if (g_map.program.count > 0 && MAPINFOItems_CameraNumber(camera->id) >= 0) {
        Properties_Text(properties, "Shown by the program", MAPINFOItems_ProgramCalls(SD_CAMERA, MAPINFOItems_CameraNumber(camera->id), NULL));
        const char* characters = MAPINFOItems_CameraCharacters(MAPINFOItems_CameraNumber(camera->id));
        if (characters[0] != '\0')
            Properties_Text(properties, "With the characters", characters);
    }
    Properties_Int(properties, "Keys", MAPINFOView_CameraKeys(camera));
    Properties_Text(properties, "Duration", TextFormat("%.2f s", MAPINFOView_CameraDuration(camera)));
    Properties_Section(properties, "Now");
    Properties_Vector(properties, "Position", &values[AUTH_CAMERA_X], 3);
    Properties_Vector(properties, "Target", &values[AUTH_CAMERA_TARGET_X], 3);
    Properties_Float(properties, "Roll", values[AUTH_CAMERA_ROLL]);
    Properties_Float(properties, "Field of view", values[AUTH_CAMERA_FOV]);
    Properties_Section(properties, "Keys of the curves");
    for (int c = 0; c < AUTH_CAMERA_CURVES; c++)
        Properties_Int(properties, g_cameraCurves[c], camera->curves[c].count);
}

// Its color, intensity, position, direction and angle can be edited
static void LightProperties(Properties* properties, uint32_t index) {
    MAPINFO_Light* light = &g_map.file.lights[index];
    const MAPINFO_Light* original = &g_map.originalLights[index];
    Properties_Int(properties, "Group", light->group);
    Properties_Int(properties, "ID", light->id);
    Properties_Hex(properties, "Flags", light->flags);
    Properties_Int(properties, "Unknown", light->unknown);
    Properties_EditVector(properties, "Color", light->color, original->color, 3, 0.005f);
    Properties_EditFloat(properties, "Intensity", &light->intensity, &original->intensity, 0.01f);
    Properties_EditVector(properties, "Position", light->position, original->position, 3, 0.01f);
    Properties_EditVector(properties, "Direction", light->direction, original->direction, 3, 0.01f);
    Properties_EditFloat(properties, "Angle", &light->angle, &original->angle, 0.5f);
}

// The fields of the prefix that the group of the shape has, then its points
static void ShapeProperties(Properties* properties, uint32_t index) {
    const MAPINFO_Shape* shape = &g_map.file.shapes[index];
    Properties_Text(properties, "Group", TextFormat("%s (%s)", g_mapLayerNames[LAYER_GROUPS + shape->group],
        MAPINFO_GroupName(shape->group)));
    Properties_Text(properties, "Type", MAPINFOItems_ShapeType(shape));
    Properties_Int(properties, "Block", shape->block);
    if (shape->group == MAPINFO_GROUP_COLI)
        Properties_Int(properties, "Collision type", shape->value);
    else if (shape->group == MAPINFO_GROUP_EVNT || shape->group == MAPINFO_GROUP_SOND)
        Properties_Int(properties, "Value", shape->value);
    else if (shape->group == MAPINFO_GROUP_PROP) {
        Properties_Text(properties, "Property", shape->property);
        Properties_Hex(properties, "First word", shape->value);
    }
    if (shape->group == MAPINFO_GROUP_HGHT)
        Properties_Float(properties, "Height", shape->height);
    if (shape->group == MAPINFO_GROUP_UNDU)
        Properties_Vector(properties, "Heights", shape->heights, 4);
    Vector3 center = MAPINFOItems_ShapeCenter(shape);
    Properties_Vector(properties, "Center", (const float[]) { center.x, center.y, center.z }, 3);
    Properties_Section(properties, TextFormat("%u values", shape->valueCount));
    // Circles: radius, x, z; rectangles: x, z of the center and the half axes; the others: x, z of each point
    for (uint32_t v = 0; v < shape->valueCount; v++)
        Properties_Float(properties, TextFormat("%u", v), shape->values[v]);
}

// Its placement can be edited
static void DoorProperties(Properties* properties, uint32_t index) {
    MAPINFO_Door* door = &g_map.file.doors[index];
    const MAPINFO_Door* original = &g_map.originalDoors[index];
    const char* error = g_map.doorErrors != NULL ? g_map.doorErrors[index] : NULL;
    Properties_Text(properties, "Model", door->model);
    Properties_Text(properties, "Type", door->type == MAPINFO_DOOR_DOOR ? "door" : door->type == MAPINFO_DOOR_MODEL ? "model" :
        TextFormat("%u", door->type));
    bool changed = Properties_EditVector(properties, "Position", door->position, original->position, 3, 0.01f);
    changed |= Properties_EditFloat(properties, "Angle", &door->angle, &original->angle, 0.5f);
    changed |= Properties_EditVector(properties, "Scale", door->scale, original->scale, 3, 0.01f);
    Properties_Text(properties, "Model file", error != NULL ? error : "loaded");
    if (changed)
        MAPINFOView_UpdateDoor(index);
}

static void ModelProperties(Properties* properties, const MT5Render_Model* model) {
    Properties_Int(properties, "Nodes", model->nodeCount);
    Properties_Int(properties, "Triangles", model->mesh.triangleCount);
    if (model->mesh.skippedTriangles > 0)
        Properties_Int(properties, "Skipped triangles", model->mesh.skippedTriangles);
    Properties_Text(properties, "Textures", TextFormat("%u/%u", model->loadedTextures, model->textureCount));
    Properties_Int(properties, "Animation frames", model->mesh.frameCount);
    Properties_Vector(properties, "Center", (const float[]) { model->mesh.center.x, model->mesh.center.y, model->mesh.center.z }, 3);
    Properties_Float(properties, "Radius", model->mesh.radius);
}

// Its position, if it is in the scene, and its angle can be edited
static void CharacterProperties(Properties* properties, uint32_t index) {
    MapCharacter* character = &g_map.characters[index];
    CHRT_Character* info = &character->info;
    const char* package = MAPINFOModels_PackageName(character->package);
    Properties_Text(properties, "ID", info->id);
    GameDB_CharacterProperties(properties, info->id);
    Properties_Text(properties, "Package", package != NULL ? package : "CHRD chunk");
    Properties_Text(properties, "Image", info->image != NULL ? info->image : "none");
    bool changed = false;
    if (info->positioned)
        changed = Properties_EditVector(properties, "Position", info->position, character->original.position, 3, 0.01f);
    else
        Properties_Text(properties, "Position", info->hasPosition ? "off the scene" : "none");
    changed |= Properties_EditVector(properties, "Angle", info->angle, character->original.angle, 3, 0.5f);
    if (changed)
        MAPINFOModels_ShowCharacters();
    Properties_Bool(properties, "Sleep", info->sleep);
    Properties_Bool(properties, "Human", info->human);
    Properties_Bool(properties, "Arm of Ryo", info->arm);
    if (g_map.program.count > 0) {
        Properties_Text(properties, "Woken by the program", MAPINFOItems_ProgramCalls(SD_WAKE, -1, info->id));
        Properties_Text(properties, "Put to sleep by it", MAPINFOItems_ProgramCalls(SD_SLEEP, -1, info->id));
        Properties_Text(properties, "Used by it", MAPINFOItems_ProgramCalls(SD_CHARACTER, -1, info->id));
    }
    // A shown package can have it hidden by a character with its ID of a previous package
    Properties_Text(properties, "Shown", character->shown ? "yes" : character->package >= 0 && !g_map.packages[character->package].shown ?
        "no, its package is hidden" : "no, another package has its ID");
    if (character->model >= 0) {
        Properties_Section(properties, "Model");
        ModelProperties(properties, &g_map.models[character->model]);
    }
    else
        Properties_Text(properties, "Model", character->error != NULL ? character->error : "none");
}

static void CutsceneProperties(Properties* properties, uint32_t index) {
    const MapCutscene* cutscene = &g_map.cutscenes[index];
    const MAPINFO_Cutscene* chunk = &g_map.file.cutscenes[index];
    Properties_Text(properties, "Chunk", TextFormat("0x%X bytes at 0x%X", chunk->size, chunk->offset));
    Properties_Text(properties, "Played by the program", MAPINFOItems_ProgramCalls(SD_CUTSCENE, (int32_t)index, NULL));
    if (!cutscene->valid) {
        Properties_Note(properties, chunk->size <= 8 ? "Empty TRCK chunk" : "Invalid TRCK chunk");
        return;
    }
    Properties_Text(properties, "Duration", TextFormat("%s (%u frames)", Text_FormatTime(cutscene->duration / AUTH_FRAME_RATE), cutscene->duration));
    Properties_Int(properties, "Characters", cutscene->characterCount);
    Properties_Text(properties, "IDs", cutscene->ids);
    Properties_Text(properties, "Names", MAPINFOItems_CutsceneNames(cutscene, INT32_MAX));
    Properties_Int(properties, "Commands", cutscene->commandCount);
    Properties_Int(properties, "Motions", cutscene->motions);
    Properties_Int(properties, "Voices", cutscene->voices);
    Properties_Int(properties, "Sounds", cutscene->sounds);
    Properties_Note(properties, "Play it with the Play button of the toolbar (Enter)");
}

// A cutscene loaded by the program in a slot of the table of cutscenes: its AUTH file is loaded when it is played
static void SequenceProperties(Properties* properties, uint32_t index) {
    const SD_Reference* sequence = MAPINFOItems_Sequence(index);
    Properties_Int(properties, "Slot", sequence->number);
    Properties_Text(properties, "File", sequence->name);
    Properties_Text(properties, strchr(sequence->folder, '/') != NULL ? "Folder" : "Package", sequence->folder);
    uint32_t loads = 0;
    for (uint32_t i = 0; i < g_map.program.count; i++)
        loads += MAPINFOItems_SameReference(&g_map.program.references[i], sequence);
    Properties_Int(properties, "Loaded by calls", loads);
    Properties_Text(properties, "Played by the program", MAPINFOItems_ProgramCalls(SD_CUTSCENE, sequence->number, NULL));
    if ((uint32_t)sequence->number < g_map.file.cutsceneCount)
        Properties_Note(properties, "The slot of a TRCK chunk of the map: the program replaces its cutscene");
    if (g_map.sequenceFound[index])
        Properties_Note(properties, "Play it with the Play button of the toolbar (Enter)");
    else
        Properties_Note(properties, "Its file isn't in the game files: it can't be played");
}

// Of the selected item, or the chunks of the file
void MAPINFOProperties_Draw(Rectangle bounds) {
    Properties* properties = &g_map.properties;
    if (g_map.selected < 0) {
        Properties_Begin(properties, bounds, GetFileName(g_map.filename));
        FileProperties(properties);
        Properties_End(properties);
        return;
    }
    const Item* item = &g_map.items[g_map.selected];
    const MAPINFO_File* file = &g_map.file;
    switch (item->kind) {
    case ITEM_CAMERA:
        Properties_Begin(properties, bounds, TextFormat("Camera %s", file->cameras[item->index].id));
        CameraProperties(properties, item->index);
        break;
    case ITEM_LIGHT:
        Properties_Begin(properties, bounds, TextFormat("Light %u.%u", file->lights[item->index].group, file->lights[item->index].id));
        LightProperties(properties, item->index);
        break;
    case ITEM_SHAPE:
        Properties_Begin(properties, bounds, TextFormat("Shape %u", item->index));
        ShapeProperties(properties, item->index);
        break;
    case ITEM_DOOR:
        Properties_Begin(properties, bounds, TextFormat("Door %u", item->index));
        DoorProperties(properties, item->index);
        break;
    case ITEM_MAP_MODEL:
        Properties_Begin(properties, bounds, TextFormat("Model %s", g_map.mapModelNames[item->index]));
        MapModelProperties(properties, item->index);
        ModelProperties(properties, &g_map.models[item->index]);
        break;
    case ITEM_CHARACTER:
        Properties_Begin(properties, bounds, TextFormat("Character %s", GameDB_CharacterDisplay(g_map.characters[item->index].info.id)));
        CharacterProperties(properties, item->index);
        break;
    case ITEM_CUTSCENE:
        Properties_Begin(properties, bounds, TextFormat("Cutscene %u", item->index));
        CutsceneProperties(properties, item->index);
        break;
    case ITEM_SEQUENCE:
        Properties_Begin(properties, bounds, TextFormat("Cutscene %d, loaded by the program", MAPINFOItems_Sequence(item->index)->number));
        SequenceProperties(properties, item->index);
        break;
    case ITEM_FLAG:
        Properties_Begin(properties, bounds, MAPINFOItems_FlagName(item->index));
        FlagProperties(properties, item->index);
        break;
    default:
        Properties_Begin(properties, bounds, "Information");
        Properties_Note(properties, g_map.labels[g_map.selected]);
        break;
    }
    Properties_End(properties);
}
