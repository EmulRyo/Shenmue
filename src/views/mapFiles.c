#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <raylib.h>
#include "core/file.h"
#include "formats/archive/afs.h"
#include "formats/archive/pkf.h"
#include "formats/archive/pks.h"
#include "formats/script/chrt.h"
#include "views/mapFiles.h"

// Names the files added to the list from first: their package
static bool NamePackage(IPAC_FileList* packed, char (**packages)[IPAC_FILENAME_SIZE], uint32_t first, const char* name) {
    char (*grown)[IPAC_FILENAME_SIZE] = realloc(*packages, (packed->count + 1) * sizeof(**packages));
    if (grown == NULL)
        return false;
    *packages = grown;
    for (uint32_t f = first; f < packed->count; f++)
        snprintf(grown[f], sizeof(grown[f]), "%s", name);
    return true;
}

// The files of the PKF and PKS entries of the AFS files of the folder without an IDX file (unnamed entries, e.g.
// scene/01/op00/op99.afs) that aren't extracted. A pair of a PKF and the PKS after it (its textures and its models) is
// a package named <afs>#<index of the PKF>, e.g. op99#000 for the entries 0 and 1. Returns false on memory error
static bool LoadArchivePacks(const char* folder, IPAC_FileList* packed, char (**packages)[IPAC_FILENAME_SIZE]) {
    FilePathList files = LoadDirectoryFilesEx(folder, ".afs", false);
    bool ok = true;
    for (unsigned int i = 0; i < files.count && ok; i++) {
        char extracted[1024];
        File_GetOutputFolder(files.paths[i], "", extracted, sizeof(extracted));
        char idx[1024];
        snprintf(idx, sizeof(idx), "%s/%s.idx", GetDirectoryPath(files.paths[i]), GetFileNameWithoutExt(files.paths[i]));
        if (DirectoryExists(extracted) || FileExists(idx))
            continue;
        uint32_t count = 1, pair = 0;
        for (uint32_t e = 0; e < count && ok; e++) {
            size_t size = 0;
            uint8_t* data = AFS_LoadEntry(files.paths[i], e, &size, &count);
            if (data == NULL)
                continue;
            if (File_IsGzip(data, size)) {
                size_t decompressedSize = 0;
                uint8_t* decompressed = File_GzipDecompress(data, size, &decompressedSize);
                free(data);
                if (decompressed == NULL)
                    continue;
                data = decompressed;
                size = decompressedSize;
            }
            bool pkf = size >= 4 && memcmp(data, "PAKF", 4) == 0, pks = size >= 4 && memcmp(data, "PAKS", 4) == 0;
            if (pkf)
                pair = e;
            char name[IPAC_FILENAME_SIZE];
            snprintf(name, sizeof(name), "%s#%03u", GetFileNameWithoutExt(files.paths[i]), pks ? pair : e);
            uint32_t first = packed->count;
            if (pkf)
                PKF_LoadFilesFromMemory(data, size, name, packed);
            else if (pks)
                PKS_LoadFilesFromMemory(data, size, name, packed);
            free(data);
            ok = NamePackage(packed, packages, first, name);
        }
    }
    UnloadDirectoryFiles(files);
    return ok;
}

// The files of the PKF and PKS files of the folder that aren't extracted, and the name of the PKF/PKS file of each one
// (packages, packed->count of them); then those of the AFS files of the folder (LoadArchivePacks). Returns false on
// memory error
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
    return ok && LoadArchivePacks(folder, packed, packages);
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
