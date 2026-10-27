#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <cJSON.h>
#include "core/hashTable.h"
#include "app/filenameDB.h"

static size_t Hash_Hash(const void* data) {
	size_t value = *(uint32_t*)data;
	return value;
}

HashTable FilenameDB_Load(const char* filepath) {
	FILE* file = fopen(filepath, "rb");
	fseek(file, 0, SEEK_END);
	size_t length = ftell(file);
	rewind(file);

	char* data = malloc(length + 1);
	if (data == NULL) {
		fputs("[FilenameDB_Load]: Memory error", stderr);
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
