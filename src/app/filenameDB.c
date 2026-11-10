#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "core/hashTable.h"
#include "formats/archive/tad.h"
#include "app/filenameDB.h"

#define PATH_SIZE 128

// resources/FilenameDatabase.txt, embedded by the build (see embed_resource in CMakeLists.txt)
extern const unsigned char g_filenameDatabase[];
extern const size_t g_filenameDatabaseSize;

static size_t Hash_Hash(const void* data) {
    size_t value = *(uint32_t*)data;
    return value;
}

// Adds the entry of a line: "path", or "hash<TAB>path" when the hash isn't the one of the path
static void AddLine(HashTable ht, const char* line, size_t length) {
    char path[PATH_SIZE];
    uint32_t hash = 0;
    bool hasHash = false;
    const char* tab = memchr(line, '\t', length);
    if (tab != NULL) {
        hash = (uint32_t)strtoul(line, NULL, 16);
        hasHash = true;
        length -= (size_t)(tab + 1 - line);
        line = tab + 1;
    }
    if (length == 0 || length >= PATH_SIZE) {
        if (length > 0)
            fprintf(stderr, "[FilenameDB_Load]: Path too long: %.*s\n", (int)length, line);
        return;
    }
    memcpy(path, line, length);
    path[length] = '\0';
    if (!hasHash)
        hash = TAD_HashPath(path);
    HT_SetEx(ht, &hash, sizeof(uint32_t), path, length + 1);
}

HashTable FilenameDB_Load(void) {
    HashTable ht = HT_Create(128000, sizeof(uint32_t), PATH_SIZE, Hash_Hash);
    const char* text = (const char*)g_filenameDatabase;
    const char* end = text + g_filenameDatabaseSize;
    while (text < end) {
        const char* lineEnd = memchr(text, '\n', (size_t)(end - text));
        if (lineEnd == NULL)
            lineEnd = end;
        size_t length = (size_t)(lineEnd - text);
        if (length > 0 && text[length - 1] == '\r') // The checkout can have CRLF
            length--;
        if (length > 0 && text[0] != '#')
            AddLine(ht, text, length);
        text = lineEnd + 1;
    }
    return ht;
}
