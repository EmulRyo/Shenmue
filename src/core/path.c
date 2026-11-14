#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "core/path.h"

void Path_ToSlashes(char* path) {
    for (char* c = path; *c != '\0'; c++) {
        if (*c == '\\')
            *c = '/';
    }
}

bool Path_ToParent(char* path) {
    char* slash = strrchr(path, '/');
    if (slash == NULL || slash == path)
        return false;
    *slash = '\0';
    return true;
}

const char* Path_LastComponent(const char* path) {
    const char* slash = strrchr(path, '/');
    return slash != NULL ? slash + 1 : path;
}

static char PathChar(char c) {
    return c == '\\' ? '/' : (char)tolower((unsigned char)c);
}

bool Path_Equals(const char* a, const char* b) {
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        if (PathChar(*a) != PathChar(*b))
            return false;
    }
    return *a == *b;
}

// The name of the parent of a folder with / separators is "scene": the folder has a parent, and the parent has a parent
static bool ParentIsScene(const char* folder) {
    const char* last = strrchr(folder, '/');
    if (last == NULL || last == folder)
        return false;
    const char* previous = last;
    while (previous > folder && previous[-1] != '/')
        previous--;
    if (previous == folder)
        return false;
    static const char scene[] = "scene";
    if (last - previous != (ptrdiff_t)(sizeof(scene) - 1))
        return false;
    for (size_t i = 0; i < sizeof(scene) - 1; i++) {
        if (tolower((unsigned char)previous[i]) != scene[i])
            return false;
    }
    return true;
}

void Path_FindSceneFolder(const char* filepath, char* folder, size_t size) {
    snprintf(folder, size, "%s", filepath);
    Path_ToSlashes(folder);
    while (Path_ToParent(folder)) {
        if (ParentIsScene(folder))
            return;
    }
    folder[0] = '\0';
}
