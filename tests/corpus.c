#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "corpus.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#endif

typedef void (*ListCallback)(const char* name, void* user);

// Calls callback with the name of every entry of a folder, but . and ..
static void ListDir(const char* path, ListCallback callback, void* user) {
#ifdef _WIN32
    char pattern[CORPUS_PATH_SIZE];
    snprintf(pattern, sizeof(pattern), "%s/*", path);
    WIN32_FIND_DATAA data;
    HANDLE handle = FindFirstFileA(pattern, &data);
    if (handle == INVALID_HANDLE_VALUE)
        return;
    do {
        if (strcmp(data.cFileName, ".") != 0 && strcmp(data.cFileName, "..") != 0)
            callback(data.cFileName, user);
    } while (FindNextFileA(handle, &data));
    FindClose(handle);
#else
    DIR* dir = opendir(path);
    if (dir == NULL)
        return;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
            callback(entry->d_name, user);
    }
    closedir(dir);
#endif
}

static bool IsFile(const char* path) {
    struct stat info;
    return stat(path, &info) == 0 && (info.st_mode & S_IFMT) == S_IFREG;
}

static bool IsDir(const char* path) {
    struct stat info;
    return stat(path, &info) == 0 && (info.st_mode & S_IFMT) == S_IFDIR;
}

static void Add(CorpusList* list, const char* content, const char* name, int disc, const char* map) {
    CorpusFile* files = realloc(list->files, (list->count + 1) * sizeof(CorpusFile));
    if (files == NULL)
        return;
    list->files = files;
    CorpusFile* file = &files[list->count++];
    snprintf(file->path, sizeof(file->path), "%s/%s", content, name);
    snprintf(file->name, sizeof(file->name), "%s", name);
    snprintf(file->map, sizeof(file->map), "%s", map);
    file->disc = disc;
}

static int CompareFiles(const void* a, const void* b) {
    return strcmp(((const CorpusFile*)a)->name, ((const CorpusFile*)b)->name);
}

static bool IsDigits(const char* text, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (text[i] < '0' || text[i] > '9')
            return false;
    }
    return text[count] == '\0';
}

typedef struct {
    const char* content;
    CorpusList* list;
    int         disc;   // While listing the maps of a scene folder
} ListState;

static void AddMapFolder(const char* map, void* user) {
    ListState* state = user;
    char name[CORPUS_PATH_SIZE], path[CORPUS_PATH_SIZE];
    snprintf(name, sizeof(name), "scene/%02d/%s/mapinfo.bin", state->disc, map);
    snprintf(path, sizeof(path), "%s/%s", state->content, name);
    if (strlen(map) < 8 && IsFile(path))
        Add(state->list, state->content, name, state->disc, map);
}

static void AddSceneFolder(const char* folder, void* user) {
    ListState* state = user;
    if (!IsDigits(folder, 2))
        return;
    char path[CORPUS_PATH_SIZE];
    snprintf(path, sizeof(path), "%s/scene/%s", state->content, folder);
    state->disc = atoi(folder);
    if (IsDir(path))
        ListDir(path, AddMapFolder, state);
}

static void AddScn(const char* filename, void* user) {
    ListState* state = user;
    // actNN_<map>.scn
    size_t length = strlen(filename);
    if (length < 10 || length > 16 || strncmp(filename, "act", 3) != 0 || filename[5] != '_' ||
        strcmp(filename + length - 4, ".scn") != 0)
        return;
    char disc[3] = { filename[3], filename[4], '\0' }, map[8] = { 0 }, name[CORPUS_PATH_SIZE];
    if (!IsDigits(disc, 2))
        return;
    memcpy(map, filename + 6, length - 10);
    snprintf(name, sizeof(name), "scenario/%s", filename);
    Add(state->list, state->content, name, atoi(disc), map);
}

bool Corpus_Exists(const char* content) {
    char path[CORPUS_PATH_SIZE];
    snprintf(path, sizeof(path), "%s/scene", content);
    return IsDir(path);
}

CorpusList Corpus_MapinfoFiles(const char* content) {
    CorpusList list = { 0 };
    ListState state = { content, &list, 0 };
    char path[CORPUS_PATH_SIZE];
    snprintf(path, sizeof(path), "%s/scene", content);
    ListDir(path, AddSceneFolder, &state);
    if (list.count > 0)
        qsort(list.files, list.count, sizeof(CorpusFile), CompareFiles);
    return list;
}

CorpusList Corpus_ScnFiles(const char* content) {
    CorpusList list = { 0 };
    ListState state = { content, &list, 0 };
    char path[CORPUS_PATH_SIZE];
    snprintf(path, sizeof(path), "%s/scenario", content);
    ListDir(path, AddScn, &state);
    if (list.count > 0)
        qsort(list.files, list.count, sizeof(CorpusFile), CompareFiles);
    return list;
}

void Corpus_Free(CorpusList* list) {
    free(list->files);
    list->files = NULL;
    list->count = 0;
}

uint8_t* Corpus_Load(const CorpusFile* file, uint32_t* size) {
    FILE* f = fopen(file->path, "rb");
    if (f == NULL)
        return NULL;
    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = length >= 0 ? malloc((size_t)length + 1) : NULL;
    if (data != NULL && fread(data, 1, (size_t)length, f) != (size_t)length) {
        free(data);
        data = NULL;
    }
    fclose(f);
    if (data != NULL && size != NULL)
        *size = (uint32_t)length;
    return data;
}
