#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif

#include "core/dir.h"

#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
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
            // Directory does not exist, try to create it
            if (MKDIR(subpath) != 0) {
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