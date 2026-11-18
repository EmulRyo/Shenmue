// The motions of a cutscene: the MOTN banks of its folder, of its family, of its map and of the game, and the
// motions that its commands play, decoded from them
#include "views/auth/authViewInternal.h"

#define AUTHVIEW_COMMON_MOTIONS "/motion/motion.bin" // Motions shared by the whole game, from the parent of the scene folder

// Loads a MOTN file at the end of the banks. Returns false if there is no memory
static bool AddMotionBank(const char* path) {
    MOTN_Bank* banks = realloc(g_auth.banks, (g_auth.bankCount + 1) * sizeof(MOTN_Bank));
    if (banks == NULL)
        return false;
    g_auth.banks = banks;
    if (MOTN_Load(path, &g_auth.banks[g_auth.bankCount]))
        g_auth.bankCount++;
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
                if (Text_EqualsIgnoreCase(GetFileName(family[i]) + familyLength, "mot") == (mot == 1))
                    AddFolderMotionBanks(family[i]);
            }
        }
    }
    free(family);
    UnloadDirectoryFiles(folders);
}

// The MOTN files of the PKF/PKS files of the map of a cutscene of a mapinfo.bin, extracted or not (e.g. M_01JA of
// scene/01/jomo/common01.pks), of a package (NULL: of all of them), but those already loaded (loaded: per file of the
// map). Returns the number of files loaded
static uint32_t AddMapMotionBanks(const char* package, bool* loaded) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < g_auth.mapFiles.count; i++) {
        const MapFile* file = &g_auth.mapFiles.files[i];
        if (loaded[i] || !IsFileExtension(file->name, ".motn") || (package != NULL && !Text_EqualsIgnoreCase(file->package, package)))
            continue;
        loaded[i] = true;
        MOTN_Bank* banks = realloc(g_auth.banks, (g_auth.bankCount + 1) * sizeof(MOTN_Bank));
        if (banks == NULL)
            return count;
        g_auth.banks = banks;
        bool read = file->packed != NULL ?
            MOTN_LoadFromMemory(file->packed->data, file->packed->size, file->name, &g_auth.banks[g_auth.bankCount]) :
            MOTN_Load(file->path, &g_auth.banks[g_auth.bankCount]);
        if (read) {
            g_auth.bankCount++;
            count++;
        }
    }
    return count;
}

// The MOTN files of the other packages of the map of the family of a package named <family>_<suffix> (e.g. DJHN_05):
// first <family>_mot, then the others in order, as AddFamilyMotionBanks
static void AddMapFamilyMotionBanks(const char* package, bool* loaded) {
    const char* underscore = strrchr(package, '_');
    if (underscore == NULL || underscore == package)
        return;
    int familyLength = (int)(underscore - package + 1); // With the underscore
    char name[IPAC_FILENAME_SIZE], prefix[IPAC_FILENAME_SIZE];
    snprintf(name, sizeof(name), "%.*s", familyLength, package);
    const char** family = calloc(g_auth.mapFiles.count + 1, sizeof(const char*));
    if (family == NULL)
        return;
    uint32_t count = 0;
    for (uint32_t i = 0; i < g_auth.mapFiles.count; i++) {
        const char* other = g_auth.mapFiles.files[i].package;
        bool known = false;
        for (uint32_t f = 0; f < count && !known; f++)
            known = Text_EqualsIgnoreCase(family[f], other);
        snprintf(prefix, sizeof(prefix), "%.*s", familyLength, other);
        if (!known && (int)strlen(other) > familyLength && Text_EqualsIgnoreCase(prefix, name) && !Text_EqualsIgnoreCase(other, package))
            family[count++] = other;
    }
    qsort(family, count, sizeof(const char*), CompareFolders);
    for (int mot = 1; mot >= 0; mot--) {
        for (uint32_t i = 0; i < count; i++) {
            if (Text_EqualsIgnoreCase(family[i] + familyLength, "mot") == (mot == 1))
                AddMapMotionBanks(family[i], loaded);
        }
    }
    free(family);
}

// The m_*.bin files of a map folder of the scene (e.g. scene/01/yq14)
static void AddMapFolderMotionBanks(const char* name) {
    if (g_auth.sceneFolder[0] == '\0' || name == NULL || name[0] == '\0')
        return;
    const char* map = TextFormat("%s/%s", g_auth.sceneFolder, name);
    if (!DirectoryExists(map))
        return;
    // The motion file that the program loads for the cutscene is already loaded
    char bankFolder[AUTHVIEW_PATH_SIZE];
    snprintf(bankFolder, sizeof(bankFolder), "%s", g_auth.motionBank[0] != '\0' ? GetFileName(GetDirectoryPath(g_auth.motionBank)) : "");
    FilePathList mapFiles = LoadDirectoryFilesEx(map, ".bin", false);
    for (unsigned int i = 0; i < mapFiles.count; i++) {
        const char* file = GetFileName(mapFiles.paths[i]);
        bool loaded = Text_EqualsIgnoreCase(bankFolder, name) && Text_EqualsIgnoreCase(file, GetFileName(g_auth.motionBank));
        if ((file[0] == 'm' || file[0] == 'M') && file[1] == '_' && !loaded)
            AddMotionBank(mapFiles.paths[i]);
    }
    UnloadDirectoryFiles(mapFiles);
}

// The MOTN files of the folder of an AUTH file; if it has none, those of the other PKS of its family (see
// AddFamilyMotionBanks); the m_*.bin files of the map of the scene with the name of that folder, the name of its PKS
// (e.g. scene/01/yq14/m_01114.bin for scene/01/d000/yq14/SEQDATA2.auth); then the common motions of the game
// (motion/motion.bin, next to the scene folder), with the small motion IDs (e.g. 0x2DF, the walk of Ryo). For a
// cutscene of a mapinfo.bin loaded by the program of the map from a package (package, e.g. "YQ14"), the same with the
// files of the map: the MOTN files of the package, or of its family, and the m_*.bin files of the map with its name
// (the motions 0x1001 to 0x1003 of SEQDATA2 of yq14.pks of scene/01/d000 are those of scene/01/yq14/m_01114.bin, not
// those of the m_*.bin of d000, which have the same IDs). Then, for every cutscene of a mapinfo.bin, the m_*.bin files
// of its map (e.g. scene/03/nbik/m_01bike.bin), the other MOTN files of the PKF/PKS files of its map, and the common
// motions. Before all of them, the motion file that the program of the map loads for the cutscene (g_auth.motionBank):
// the m_*.bin files of a map can have the same IDs (scene/01/dnoz/m_01ful.bin and m_01ski.bin, 0x1000 to 0x1006)
void AUTHMotions_LoadBanks(const char* filepath, const char* package) {
    char folder[AUTHVIEW_PATH_SIZE];
    snprintf(folder, sizeof(folder), "%s", GetDirectoryPath(filepath));
    if (g_auth.motionBank[0] != '\0')
        AddMotionBank(g_auth.motionBank);
    if (g_auth.cutscene < 0) {
        AddFolderMotionBanks(folder);
        if (g_auth.bankCount == 0)
            AddFamilyMotionBanks(folder);
        AddMapFolderMotionBanks(GetFileName(folder));
    } else {
        bool* loaded = calloc(g_auth.mapFiles.count + 1, sizeof(bool));
        if (loaded == NULL)
            return;
        if (package != NULL && package[0] != '\0') {
            if (AddMapMotionBanks(package, loaded) == 0)
                AddMapFamilyMotionBanks(package, loaded);
            char lower[IPAC_FILENAME_SIZE];
            snprintf(lower, sizeof(lower), "%s", package);
            for (char* c = lower; *c != '\0'; c++)
                *c = (char)tolower((unsigned char)*c);
            if (!Text_EqualsIgnoreCase(lower, GetFileName(folder)))
                AddMapFolderMotionBanks(lower);
        }
        AddMapFolderMotionBanks(GetFileName(folder));
        AddMapMotionBanks(NULL, loaded);
        free(loaded);
    }
    char common[AUTHVIEW_PATH_SIZE];
    snprintf(common, sizeof(common), "%s", g_auth.sceneFolder);
    if (common[0] != '\0' && Path_ToParent(common) && Path_ToParent(common)) {
        strncat(common, AUTHVIEW_COMMON_MOTIONS, sizeof(common) - strlen(common) - 1);
        if (FileExists(common))
            AddMotionBank(common);
    }
}

// Last frame that the commands play of a motion
static uint32_t LastPlayedFrame(uint32_t id) {
    uint32_t last = 0;
    for (uint32_t i = 0; i < g_auth.sequence.commandCount; i++) {
        const AUTH_Command* command = &g_auth.sequence.commands[i];
        if (command->opcode == AUTH_COMMAND_MOTION && Arg(command, 1) == id && Arg(command, 3) > last)
            last = Arg(command, 3);
    }
    return last;
}

// Decodes the motions of the motion commands. Several banks can have the same ID (e.g. 0x1001 in m_01114.bin and
// m_0114.bin of scene/01/yq14): the motion is the first one that lasts up to the last frame that the commands play, or
// else the first one found
bool AUTHMotions_Load(void) {
    const AUTH_Sequence* sequence = &g_auth.sequence;
    g_auth.motions = calloc(sequence->commandCount + 1, sizeof(AUTHView_Motion));
    if (g_auth.motions == NULL)
        return false;
    for (uint32_t i = 0; i < sequence->commandCount; i++) {
        const AUTH_Command* command = &sequence->commands[i];
        if (command->opcode != AUTH_COMMAND_MOTION)
            continue;
        uint32_t id = Arg(command, 1);
        bool known = false;
        for (uint32_t m = 0; m < g_auth.motionCount && !known; m++)
            known = g_auth.motions[m].id == id;
        if (known)
            continue;
        AUTHView_Motion* motion = &g_auth.motions[g_auth.motionCount++];
        motion->id = id;
        uint32_t last = LastPlayedFrame(id);
        for (uint32_t b = 0; b < g_auth.bankCount && !(motion->valid && motion->motion.frames >= last); b++) {
            int32_t index = MOTN_FindMotion(&g_auth.banks[b], id);
            MOTN_Motion candidate;
            if (index < 0 || !MOTN_ReadMotion(&g_auth.banks[b], (uint32_t)index, &candidate))
                continue;
            if (motion->valid && candidate.frames < last) {
                MOTN_FreeMotion(&candidate);
                continue;
            }
            if (motion->valid)
                MOTN_FreeMotion(&motion->motion);
            motion->motion = candidate;
            motion->valid = true;
            motion->name = MOTN_FindName(&g_auth.banks[b], id);
        }    }
    return true;
}

const MOTN_Motion* AUTHMotions_Find(uint32_t id) {
    for (uint32_t i = 0; i < g_auth.motionCount; i++) {
        if (g_auth.motions[i].id == id)
            return g_auth.motions[i].valid ? &g_auth.motions[i].motion : NULL;
    }
    return NULL;
}
