#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <raylib.h>
#include <rlgl.h>
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
#include "dynamicArray.h"
#include "cJSON.h"
#include "hashTable.h"
#include "tad.h"

static size_t Hash_Hash(const void* data) {
    size_t value = *(uint32_t*)data;
    return value;
}

static HashTable ReadJson(const char* filepath) {
    FILE* file = fopen(filepath, "rb");
    fseek(file, 0, SEEK_END);
    size_t length = ftell(file);
    rewind(file);

    char* data = malloc(length + 1);
    if (data == NULL) {
        fputs("[ReadJson]: Memory error", stderr);
        exit(2);
    }
    fread(data, 1, length, file);
    data[length] = '\0';
    fclose(file);

	HashTable ht = HT_Create(128000, sizeof(uint32_t), sizeof(char) * 128, Hash_Hash);

	cJSON* root = cJSON_Parse(data);
	cJSON* item = NULL;
	cJSON_ArrayForEach(item, root) {
		cJSON* hash = cJSON_GetObjectItemCaseSensitive(item, "Hash");
		cJSON* path = cJSON_GetObjectItemCaseSensitive(item, "Path");
		uint32_t key = (uint32_t)hash->valuedouble;
		HT_SetEx(ht, &key, sizeof(uint32_t), path->valuestring, strlen(path->valuestring)+1);
	}

	return ht;
}

static void Init() {
    HashTable filenameDB = ReadJson("resources/FilenameDatabase.json");

    FilePathList fileList = LoadDirectoryFilesEx("C:\\Program Files (x86)\\Steam\\steamapps\\common\\SMLaunch\\sm1\\archives\\dx11\\data", ".tad", false);
    for (unsigned int i = 0; i < fileList.count; i++) {
        const char* filename = fileList.paths[i];
        printf("Loading TAD file: %s\n", filename);
		TAD_ExtractAllFiles(filename, filenameDB, "content", true);
	}
    UnloadDirectoryFiles(fileList);
}

static void Update() {
	
}

static void Draw() {
	
}

void main() {
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(1280, 720, "Shenmue");

    Init();

    while (!WindowShouldClose()) {
        Update();

        BeginDrawing();

        ClearBackground((Color) {28, 28, 28, 255});
        Draw();

        EndDrawing();
    }

    CloseWindow();
}