#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <raylib.h>
#include "core/file.h"
#include "formats/archive/pkf.h"
#include "formats/archive/pks.h"
#include "formats/script/chrt.h"
#include "views/mapFiles.h"

// The files of the PKF and PKS files of the folder that aren't extracted, and the name of the PKF/PKS file of each one
// (packages, packed->count of them). Returns false on memory error
static bool LoadPacks(const char* folder, IPAC_FileList* packed, char (**packages)[IPAC_FILENAME_SIZE]) {
    FilePathList files = LoadDirectoryFilesEx(folder, ".pkf;.pks", false);
    bool ok = true;
    for (unsigned int i = 0; i < files.count && ok; i++) {
        char extracted[1024];
        File_GetOutputFolder(files.paths[i], "", extracted, sizeof(extracted));
        if (DirectoryExists(extracted))
            continue;
        uint32_t first = packed->count;
        if (IsFileExtension(files.paths[i], ".pkf"))
            PKF_LoadFiles(files.paths[i], packed);
        else
            PKS_LoadFiles(files.paths[i], packed);
        char (*grown)[IPAC_FILENAME_SIZE] = realloc(*packages, (packed->count + 1) * sizeof(**packages));
        ok = grown != NULL;
        if (ok) {
            *packages = grown;
            for (uint32_t f = first; f < packed->count; f++)
                snprintf(grown[f], sizeof(grown[f]), "%s", GetFileNameWithoutExt(files.paths[i]));
        }
    }
    UnloadDirectoryFiles(files);
    return ok;
}

bool MapFiles_Load(const char* folder, const char* extensions, MapFiles* files) {
    memset(files, 0, sizeof(*files));
    files->paths = LoadDirectoryFilesEx(folder, extensions, true);
    if (!LoadPacks(folder, &files->packed, &files->packages)) {
        MapFiles_Free(files);
        return false;
    }
    files->files = calloc(files->paths.count + files->packed.count + 1, sizeof(MapFile));
    if (files->files == NULL) {
        MapFiles_Free(files);
        return false;
    }
    for (unsigned int i = 0; i < files->paths.count; i++) {
        const char* path = files->paths.paths[i];
        const char* relative = path + strlen(folder);
        int separators = 0;
        for (const char* p = relative + 1; *p != '\0'; p++)
            separators += *p == '/' || *p == '\\';
        if (separators > 1)
            continue;
        MapFile* added = &files->files[files->count++];
        *added = (MapFile) { path, GetFileName(path), NULL, "" };
        if (separators == 1)
            snprintf(added->package, sizeof(added->package), "%s", GetFileName(GetDirectoryPath(path)));
    }
    uint32_t diskCount = files->count;
    for (uint32_t i = 0; i < files->packed.count; i++) {
        const IPAC_File* packed = &files->packed.files[i];
        char name[IPAC_FILENAME_SIZE];
        snprintf(name, sizeof(name), "%s", GetFileNameWithoutExt(packed->filename));
        bool onDisk = false;
        for (uint32_t d = 0; d < diskCount && !onDisk; d++)
            onDisk = files->files[d].package[0] == '\0' && CHRT_EqualsIgnoreCase(GetFileNameWithoutExt(files->files[d].name), name);
        if (onDisk || !IsFileExtension(packed->filename, extensions))
            continue;
        MapFile* added = &files->files[files->count++];
        *added = (MapFile) { NULL, packed->filename, packed, "" };
        snprintf(added->package, sizeof(added->package), "%s", files->packages[i]);
    }
    return true;
}

void MapFiles_Free(MapFiles* files) {
    free(files->files);
    free(files->packages);
    IPAC_FreeFiles(&files->packed);
    UnloadDirectoryFiles(files->paths);
    memset(files, 0, sizeof(*files));
}
