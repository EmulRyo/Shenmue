// What the program of the map does while a cutscene plays: its run, its items in the tree and their properties
#include "core/log.h"
#include "views/auth/authViewInternal.h"

static const char* g_programFields[] = { "rotation", "position", "scale" };

// The items of the program in the tree: the calls for the same node, field and mode, or that place the same character
// with the same function and flags, are one item while each one comes at most a frame after the previous one; then the
// sound commands, all in the order of their first frame
static int CompareProgramItems(const void* a, const void* b) {
    const AUTHView_ProgramItem* x = a;
    const AUTHView_ProgramItem* y = b;
    if (x->firstFrame != y->firstFrame)
        return x->firstFrame < y->firstFrame ? -1 : 1;
    if (x->kind != y->kind)
        return x->kind < y->kind ? -1 : 1;
    return x->first < y->first ? -1 : x->first > y->first;
}

static bool CreateProgramItems(void) {
    const SCNCutscene* program = &g_auth.program;
    uint32_t capacity = program->nodeCount + program->placeCount + program->displayCount + program->attachCount +
        program->soundCount;
    if (capacity == 0)
        return true;
    g_auth.programItems = malloc(capacity * sizeof(AUTHView_ProgramItem));
    g_auth.programLabels = malloc(capacity * sizeof(*g_auth.programLabels));
    if (g_auth.programItems == NULL || g_auth.programLabels == NULL)
        return false;
    uint32_t count = 0;
    for (uint32_t i = 0; i < program->nodeCount; i++) {
        const SCNCutscene_Node* node = &program->nodes[i];
        AUTHView_ProgramItem* item = NULL;
        for (uint32_t j = count; j-- > 0 && item == NULL;) {
            AUTHView_ProgramItem* other = &g_auth.programItems[j];
            const SCNCutscene_Node* last = &program->nodes[other->last];
            if (other->kind == PROGRAM_NODE && last->character == node->character && last->node == node->node && last->field == node->field &&
                last->add == node->add && node->frame <= other->lastFrame + 1)
                item = other;
        }
        if (item == NULL) {
            item = &g_auth.programItems[count++];
            *item = (AUTHView_ProgramItem) { PROGRAM_NODE, i, i, 0, node->frame, node->frame };
        }
        item->last = i;
        item->lastFrame = node->frame;
        item->count++;
    }
    for (uint32_t i = 0; i < program->placeCount; i++) {
        const SCNCutscene_Place* place = &program->places[i];
        AUTHView_ProgramItem* item = NULL;
        for (uint32_t j = count; j-- > 0 && item == NULL;) {
            AUTHView_ProgramItem* other = &g_auth.programItems[j];
            const SCNCutscene_Place* last = &program->places[other->last];
            if (other->kind == PROGRAM_PLACE && last->character == place->character && last->angle == place->angle &&
                last->flags == place->flags && place->frame <= other->lastFrame + 1)
                item = other;
        }
        if (item == NULL) {
            item = &g_auth.programItems[count++];
            *item = (AUTHView_ProgramItem) { PROGRAM_PLACE, i, i, 0, place->frame, place->frame };
        }
        item->last = i;
        item->lastFrame = place->frame;
        item->count++;
    }
    for (uint32_t i = 0; i < program->displayCount; i++)
        g_auth.programItems[count++] = (AUTHView_ProgramItem) { PROGRAM_DISPLAY, i, i, 1, program->displays[i].frame,
            program->displays[i].frame };
    for (uint32_t i = 0; i < program->attachCount; i++)
        g_auth.programItems[count++] = (AUTHView_ProgramItem) { PROGRAM_ATTACH, i, i, 1, program->attaches[i].frame,
            program->attaches[i].frame };
    for (uint32_t i = 0; i < program->soundCount; i++)
        g_auth.programItems[count++] = (AUTHView_ProgramItem) { PROGRAM_SOUND, i, i, 1, program->sounds[i].frame,
            program->sounds[i].frame };
    qsort(g_auth.programItems, count, sizeof(AUTHView_ProgramItem), CompareProgramItems);
    g_auth.programItemCount = count;
    for (uint32_t i = 0; i < count; i++) {
        const AUTHView_ProgramItem* item = &g_auth.programItems[i];
        char* label = g_auth.programLabels[i];
        const char* frames = item->lastFrame != item->firstFrame ? TextFormat("%u-%u", item->firstFrame, item->lastFrame) :
            TextFormat("%u", item->firstFrame);
        if (item->kind == PROGRAM_SOUND)
            snprintf(label, AUTHVIEW_LABEL_SIZE, "%5s  Sound 0x%08X", frames, program->sounds[item->first].command);
        else if (item->kind == PROGRAM_DISPLAY) {
            const SCNCutscene_Display* display = &program->displays[item->first];
            snprintf(label, AUTHVIEW_LABEL_SIZE, "%5s  %s %s", frames, display->active ? (display->on ? "Wake" : "Sleep") :
                (display->on ? "Show" : "Hide"), IDText(display->character));
        } else if (item->kind == PROGRAM_ATTACH) {
            const SCNCutscene_Attach* attach = &program->attaches[item->first];
            char character[8];
            snprintf(character, sizeof(character), "%s", IDText(attach->character));
            if (attach->parent != 0)
                snprintf(label, AUTHVIEW_LABEL_SIZE, "%5s  Attach %s to %s %u", frames, character, IDText(attach->parent), attach->node);
            else
                snprintf(label, AUTHVIEW_LABEL_SIZE, "%5s  Detach %s", frames, character);
        } else if (item->kind == PROGRAM_PLACE) {
            const SCNCutscene_Place* place = &program->places[item->first];
            snprintf(label, AUTHVIEW_LABEL_SIZE, "%5s  Place %s %s%s", frames, IDText(place->character),
                (place->flags & SCNCUTSCENE_PLACE_ADD) ? "+" : "", place->angle ? "angle" : "position");
        } else {
            const SCNCutscene_Node* node = &program->nodes[item->first];
            snprintf(label, AUTHVIEW_LABEL_SIZE, "%5s  Node %s %u %s%s", frames, IDText(node->character), node->node,
                node->add ? "+" : "", g_programFields[node->field]);
        }
    }
    return true;
}

// Runs the program of the map for a cutscene of its table: the slot cutscene, loaded from file (NULL for a cutscene of
// the SCEX chunk)
bool AUTHProgram_Run(int cutscene, const char* file) {
    g_auth.selectedProgram = -1;
    if (cutscene < 0 || !g_auth.hasMapinfo)
        return true;
    MAPINFO_File mapinfo;
    if (!MAPINFO_Load(g_auth.mapinfoPath, &mapinfo))
        return true;
    MapBytecode bytecode;
    bool ok = true;
    if (MapProgram_LoadBytecode(g_auth.mapinfoPath, &mapinfo, &bytecode)) {
        // What the cutscene tells of the game: Ryo where it starts, and its characters, which it moves
        const AUTH_Sequence* sequence = &g_auth.sequence;
        uint32_t* characters = malloc((sequence->characterCount + 1) * sizeof(uint32_t));
        SCNCutscene_Scene scene = { false, { 0 }, { 0 }, 0, characters };
        for (uint32_t i = 0; characters != NULL && i < sequence->characterCount; i++) {
            const AUTH_Character* character = &sequence->characters[i];
            characters[scene.characterCount++] = character->idValue;
            if (character->idValue != SCNCUTSCENE_RYO || character->curves[AUTH_CHARACTER_X].count == 0)
                continue;
            scene.ryo = true;
            for (int v = 0; v < 3; v++) {
                scene.ryoPosition[v] = AUTH_CurveValue(&character->curves[AUTH_CHARACTER_X + v], 0.0f, 0.0f);
                scene.ryoAngle[v] = AUTH_CurveValue(&character->curves[AUTH_CHARACTER_ROTATION_X + v], 0.0f, 0.0f);
            }
        }
        double start = GetTime();
        ok = characters != NULL && SCNCutscene_Run(&bytecode.program, (uint32_t)cutscene, file, sequence->duration, &scene,
            &g_auth.program);
        free(characters);
        const SCNCutscene* program = &g_auth.program;
        if (ok && program->found)
            Log_Info("[AUTHView_Open]: program of the map: cutscene played at 0x%X (function 0x%X, emulated from 0x%X, %u levels "
                "up), %u threads, %u frames, %u node calls, %u place calls, %u display calls, %u sound commands in %.2f s\n",
                program->address, program->function, program->start, program->levels, program->run.threads,
                program->run.frames, program->nodeCount, program->placeCount, program->displayCount, program->soundCount,
                GetTime() - start);
        else if (ok)
            Log_Info("[AUTHView_Open]: the program of the map doesn't play the cutscene %d\n", cutscene);
        MapProgram_FreeBytecode(&bytecode);
    }
    MAPINFO_Free(&mapinfo);
    return ok && CreateProgramItems() && AUTHModels_LoadPlaced();
}

// The values of a call for a node: degrees for the rotation
static const char* NodeValues(const SCNCutscene_Node* node) {
    char values[3][24];
    for (int v = 0; v < 3; v++) {
        if (!(node->known & (1 << v)))
            snprintf(values[v], sizeof(values[v]), "?");
        else if (node->field == SCNCUTSCENE_ROTATION)
            snprintf(values[v], sizeof(values[v]), "%.1f", (int32_t)node->values[v] * 360.0f / 65536.0f);
        else {
            float value;
            memcpy(&value, &node->values[v], sizeof(value));
            snprintf(values[v], sizeof(values[v]), "%g", value);
        }
    }
    return TextFormat("%s, %s, %s", values[0], values[1], values[2]);
}

// The values of a call that places a character: degrees for the angles, "-" for those not set
static const char* PlaceValues(const SCNCutscene_Place* place) {
    char values[3][24];
    for (int v = 0; v < 3; v++) {
        float value;
        memcpy(&value, &place->values[v], sizeof(value));
        if (!(place->known & (1 << v)))
            snprintf(values[v], sizeof(values[v]), "-");
        else if (place->angle)
            snprintf(values[v], sizeof(values[v]), "%.1f", (int32_t)place->values[v] * 360.0f / 65536.0f);
        else
            snprintf(values[v], sizeof(values[v]), "%g", value);
    }
    return TextFormat("%s, %s, %s", values[0], values[1], values[2]);
}

// The frames, the calls and their values
void AUTHProgram_Properties(Properties* properties, uint32_t index) {
    const AUTHView_ProgramItem* item = &g_auth.programItems[index];
    const SCNCutscene* program = &g_auth.program;
    Properties_Text(properties, "Frames", item->lastFrame != item->firstFrame ? TextFormat("%u-%u", item->firstFrame, item->lastFrame) :
        TextFormat("%u", item->firstFrame));
    Properties_Text(properties, "Time", Text_FormatTime(item->firstFrame / AUTH_FRAME_RATE));
    Properties_Int(properties, "Calls", item->count);
    if (item->kind == PROGRAM_SOUND) {
        const SCNCutscene_Sound* sound = &program->sounds[item->first];
        Properties_Text(properties, "Function", TextFormat("SoundCommand (0x%03X)", SCNCUTSCENE_SOUND));
        Properties_Hex(properties, "Command", sound->command);
        Properties_Text(properties, "Sequence", TextFormat("group 0x%04X, sequence %u", sound->command & 0xFFFF, sound->command >> 16));
        Properties_Text(properties, "Audio", AUTHSounds_HasProgramSound(item->first) ? "found" : "not found");
        Properties_Hex(properties, "Address", sound->address);
    } else if (item->kind == PROGRAM_DISPLAY) {
        const SCNCutscene_Display* display = &program->displays[item->first];
        Properties_Text(properties, "Function", display->active ? TextFormat("CharacterActive (0x%03X)", SCNCUTSCENE_ACTIVE) :
            TextFormat("CharacterDisplay (0x%03X)", SCNCUTSCENE_DISPLAY));
        Properties_Text(properties, "Character", GameDB_CharacterDisplay(IDText(display->character)));
        Properties_Text(properties, "Value", display->active ? (display->on ? "woken" : "put to sleep") :
            (display->on ? "shown" : "hidden"));
        Properties_Hex(properties, "Address", display->address);
    } else if (item->kind == PROGRAM_ATTACH) {
        const SCNCutscene_Attach* attach = &program->attaches[item->first];
        Properties_Text(properties, "Function", attach->parent != 0 ? TextFormat("Attach (0x%03X)", SCNCUTSCENE_ATTACH) :
            TextFormat("Detach (0x%03X)", SCNCUTSCENE_DETACH));
        Properties_Text(properties, "Character", GameDB_CharacterDisplay(IDText(attach->character)));
        if (attach->parent != 0) {
            Properties_Text(properties, "To", GameDB_CharacterDisplay(IDText(attach->parent)));
            Properties_Int(properties, "Bone", attach->node);
            char values[2][3][24];
            for (int a = 0; a < 2; a++) {
                for (int v = 0; v < 3; v++) {
                    float value;
                    memcpy(&value, &attach->position[v], sizeof(value));
                    if (!(attach->known & (1 << (a * 3 + v))))
                        snprintf(values[a][v], sizeof(values[a][v]), "?");
                    else if (a == 1)
                        snprintf(values[a][v], sizeof(values[a][v]), "%.1f", attach->angles[v] * 360.0f / 65536.0f);
                    else
                        snprintf(values[a][v], sizeof(values[a][v]), "%g", value);
                }
                Properties_Text(properties, a == 0 ? "Position" : "Angles", TextFormat("%s, %s, %s", values[a][0], values[a][1],
                    values[a][2]));
            }
        }
        const char* model = "declared by the cutscene";
        const char* source = NULL;
        for (uint32_t i = 0; i < g_auth.placedCount; i++) {
            if (g_auth.placed[i].idValue == attach->character) {
                model = g_auth.placed[i].model >= 0 ? GetFileName(g_auth.models[g_auth.placed[i].model].path) : "not found";
                source = g_auth.placed[i].source;
            }
        }
        Properties_Text(properties, "Model", model);
        if (source != NULL)
            Properties_Text(properties, "Script", source);
        Properties_Hex(properties, "Address", attach->address);
    } else if (item->kind == PROGRAM_PLACE) {
        const SCNCutscene_Place* first = &program->places[item->first];
        const SCNCutscene_Place* last = &program->places[item->last];
        Properties_Text(properties, "Function", first->angle ? TextFormat("SetAngle (0x%03X)", SCNCUTSCENE_SET_ANGLE) :
            TextFormat("SetPosition (0x%03X)", SCNCUTSCENE_SET_POSITION));
        Properties_Text(properties, "Character", GameDB_CharacterDisplay(IDText(first->character)));
        Properties_Text(properties, "Value", TextFormat("%s, %s", first->angle ? "angles" : "position",
            (first->flags & SCNCUTSCENE_PLACE_ADD) ? "added" : "set"));
        Properties_Hex(properties, "Flags", first->flags);
        Properties_Text(properties, "First", PlaceValues(first));
        if (item->count > 1)
            Properties_Text(properties, "Last", PlaceValues(last));
        const char* model = "declared by the cutscene";
        for (uint32_t i = 0; i < g_auth.placedCount; i++) {
            if (g_auth.placed[i].idValue == first->character)
                model = g_auth.placed[i].model >= 0 ? GetFileName(g_auth.models[g_auth.placed[i].model].path) : "not found";
        }
        Properties_Text(properties, "Model", model);
        Properties_Hex(properties, "Address", first->address);
    } else {
        const SCNCutscene_Node* first = &program->nodes[item->first];
        const SCNCutscene_Node* last = &program->nodes[item->last];
        Properties_Text(properties, "Function", TextFormat("NodeTransform (0x%03X)", SCNCUTSCENE_NODE));
        Properties_Text(properties, "Character", GameDB_CharacterDisplay(IDText(first->character)));
        Properties_Int(properties, "Node", first->node);
        Properties_Text(properties, "Value", TextFormat("%s, %s", g_programFields[first->field], first->add ? "added" : "set"));
        Properties_Text(properties, "First", NodeValues(first));
        if (item->count > 1)
            Properties_Text(properties, "Last", NodeValues(last));
        Properties_Hex(properties, "Address", first->address);
    }
    Properties_Section(properties, "Program of the map");
    Properties_Hex(properties, "Cutscene played at", program->address);
    Properties_Hex(properties, "Function run", program->function);
    Properties_Hex(properties, "Emulated from", program->start);
    Properties_Int(properties, "Levels up", program->levels);
    Properties_Int(properties, "Threads", program->run.threads);
    Properties_Int(properties, "Frames run", program->run.frames);
}
