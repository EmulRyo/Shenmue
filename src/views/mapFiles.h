#ifndef __MAPFILES_H__
#define __MAPFILES_H__

#include <stdbool.h>
#include <stdint.h>
#include <raylib.h>
#include "formats/archive/ipac.h"

// The files of the folder of a map (scene/0N/<map>, with its mapinfo.bin): those of the folder and of one level of
// subfolders (the extracted PKF/PKS files of the map), and those of the PKF and PKS files of the folder that aren't
// extracted (no folder named as the file next to it), loaded in memory. A file of the folder with the name of a packed
// file is used instead of it (e.g. map.mt5 is the MAP.mt5 of mpk00.pks with its textures)

// A file of the map: on disk, or in a PKF/PKS file of the map that isn't extracted, loaded in memory
typedef struct {
    const char*         path;   // NULL if the file is in memory
    const char*         name;
    const IPAC_File*    packed;
    char                package[IPAC_FILENAME_SIZE]; // Name of its PKF/PKS files or of their extracted folder, empty for
                                                     // the files of the folder of the map
} MapFile;

typedef struct {
    uint32_t        count;
    MapFile*        files;      // On disk first
    IPAC_FileList   packed;     // All the files of the PKF/PKS files that aren't extracted: the external textures of the
                                // packed models are searched in them (e.g. the MAP.mt5 of mpk00.pks uses the textures of
                                // mpk00.pkf)
    char            (*packages)[IPAC_FILENAME_SIZE]; // Of each packed file
    FilePathList    paths;
} MapFiles;

// The files of a map folder with one of the extensions (as IsFileExtension takes them, e.g. ".mt5;.chrm;.chrt").
// Returns false on memory error
bool MapFiles_Load(const char* folder, const char* extensions, MapFiles* files);
void MapFiles_Free(MapFiles* files);

#endif
