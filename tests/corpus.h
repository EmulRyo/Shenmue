#ifndef __CORPUS_H__
#define __CORPUS_H__

#include <stdbool.h>
#include <stdint.h>

// The files of the game used by the tests, found in the content folder

#define CORPUS_PATH_SIZE 512

typedef struct {
    char    path[CORPUS_PATH_SIZE];     // Full path
    char    name[CORPUS_PATH_SIZE];     // Relative to the content folder, with '/' (e.g. "scene/01/d000/mapinfo.bin")
    int     disc;                       // Number of the scene folder (1 for scene/01) or of the act (1 for act01_d000.scn)
    char    map[8];                     // Map folder, e.g. "d000"
} CorpusFile;

typedef struct {
    uint32_t    count;
    CorpusFile* files;
} CorpusList;

bool Corpus_Exists(const char* content);
// scene/NN/<map>/mapinfo.bin, sorted by name
CorpusList Corpus_MapinfoFiles(const char* content);
// scenario/actNN_<map>.scn, sorted by name
CorpusList Corpus_ScnFiles(const char* content);
void Corpus_Free(CorpusList* list);
// Whole file in memory, NULL on error. size can be NULL
uint8_t* Corpus_Load(const CorpusFile* file, uint32_t* size);

#endif
