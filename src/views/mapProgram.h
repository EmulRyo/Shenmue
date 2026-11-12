#ifndef __MAPPROGRAM_H__
#define __MAPPROGRAM_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "formats/map/mapinfo.h"
#include "formats/script/scnData.h"

// What the program of a map does with it (scnData.h), for the views of the map and of the cutscenes: the SH-4 program
// of the SCN3 chunk of its mapinfo.bin, with the arguments of the bytecode of its SCN file, or its bytecode

// The SCN file of a map: scenario/actNN_<map>.scn for scene/NN/<map>/mapinfo.bin (name: "actNN_<map>.scn"). Returns
// false if the path isn't the one of a map
bool MapProgram_ScnPath(const char* mapinfoPath, char* name, size_t nameSize, char* path, size_t pathSize);
// Reads the data of the program of the SCN3 chunk of a loaded mapinfo.bin. Returns false if it has no valid program or
// on memory error
bool MapProgram_Read(const char* mapinfoPath, const MAPINFO_File* file, SD_Data* data);
// The AUTH file of a cutscene that the program loads in the table of cutscenes (SD_SEQUENCE): in the PKS file of its
// package in the folder of the map, or in the folder of the package extracted, or a file of a scene folder (e.g.
// "SCENE/01/DNOZ", seqdata2.bin). Returns NULL if it isn't found. The data is freed with free
uint8_t* MapProgram_LoadSequence(const char* mapinfoPath, const SD_Reference* sequence, size_t* size);

#endif
