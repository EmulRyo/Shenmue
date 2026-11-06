#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif

#include "core/dir.h"
#include "core/hashTable.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#include <stdlib.h>
#include <unistd.h>
#define MKDIR(path) mkdir(path, 0755)
#endif

static void strncpy_safe(char* dest, const char* src, size_t size) {
    if (!dest || !src || size == 0) return;
#ifdef _MSC_VER
    strncpy_s(dest, size, src, _TRUNCATE);
#else
    strncpy(dest, src, size);
    dest[size - 1] = '\0';
#endif
}

static void strncat_safe(char* dest, const char* src, size_t size) {
    if (!dest || !src || size == 0) return;
#ifdef _MSC_VER
    strncat_s(dest, size, src, _TRUNCATE);
#else
    size_t dest_len = strlen(dest);
    if (dest_len >= size - 1) return;
    strncat(dest, src, size - dest_len - 1);
    dest[size - 1] = '\0';
#endif
}

static char* strtok_safe(char* str, const char* delim, char** context) {
#ifdef _MSC_VER
    return strtok_s(str, delim, context);  // MSVC
#elif defined(__unix__) || defined(__APPLE__) || defined(__MINGW64__)
    return strtok_r(str, delim, context);  // POSIX
#else
#warning "No secure strtok available. Using unsafe strtok as fallback."
    return strtok(str, delim);             // insecure, only as the last resort
#endif
}

int MakeDirs(const char *path) {
    char temp[1024];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(temp)) return -1;

    strncpy_safe(temp, path, sizeof(temp));
    temp[len] = '\0';

    // Normalize separators (replace '\' with '/')
    for (size_t i = 0; i < len; ++i) {
        if (temp[i] == '\\') temp[i] = '/';
    }

    char subpath[1024] = "";
    char* context = NULL;
    char *token = strtok_safe(temp, "/", &context);

    // Handle absolute paths on Unix (start with '/')
    if (path[0] == '/') strncat_safe(subpath, "/", sizeof(subpath));

#ifdef _WIN32
    // Handle Windows drive letters (e.g., "C:/...")
    if (len > 2 && path[1] == ':' && (path[2] == '/' || path[2] == '\\')) {
        strncpy_safe(subpath, path, 4); // Copy "C:/" (the size includes the null terminator)
        subpath[2] = '/';
        token = strtok_safe(temp + 3, "/", &context);
    }
#endif

    while (token) {
        strncat_safe(subpath, token, sizeof(subpath));

        struct stat st;
        if (stat(subpath, &st) != 0) {
            // Directory does not exist, try to create it. Another thread may have created it since the stat
            if (MKDIR(subpath) != 0 && errno != EEXIST) {
                perror("mkdir failed");
                return -1;
            }
        } else if (!S_ISDIR(st.st_mode)) {
            fprintf(stderr, "Error: %s exists but is not a directory\n", subpath);
            return -2;
        }

        strncat_safe(subpath, "/", sizeof(subpath));
        token = strtok_safe(NULL, "/", &context);
    }

    return 0;
}

int OpenDir(const char* path) {
#ifdef _WIN32
    // The path is UTF-8
    wchar_t widePath[MAX_PATH * 4];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, widePath, (int)(sizeof(widePath) / sizeof(widePath[0]))) == 0)
        return -1;
    HINSTANCE result = ShellExecuteW(NULL, L"open", widePath, NULL, NULL, SW_SHOWNORMAL);
    return (INT_PTR)result > 32 ? 0 : -1;
#else
    if (strchr(path, '\'') != NULL)
        return -1;
    char command[1100];
#ifdef __APPLE__
    snprintf(command, sizeof(command), "open '%s'", path);
#else
    snprintf(command, sizeof(command), "xdg-open '%s' &", path);
#endif
    return system(command) == 0 ? 0 : -1;
#endif
}

// The longest folder kept in a DirCache; longer ones are created every time
#define DIRCACHE_PATH 512

HashTable DirCache_Create(void) {
    return HT_Create(1024, DIRCACHE_PATH, 1, Hash_djb2);
}

int MakeDirsCached(const char* path, HashTable cache) {
    size_t length = strlen(path);
    if (cache == NULL || length >= DIRCACHE_PATH)
        return MakeDirs(path);
    char key[DIRCACHE_PATH] = { 0 }; // The whole key is compared: the rest must be zeros
    memcpy(key, path, length);
    if (HT_ContainsKey(cache, key))
        return 0;
    int result = MakeDirs(path);
    if (result == 0) {
        char created = 1;
        HT_Set(cache, key, &created);
    }
    return result;
}
