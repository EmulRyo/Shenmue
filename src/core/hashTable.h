#ifndef __HASH_TABLE_H__
#define __HASH_TABLE_H__

#include <stdbool.h>

typedef struct HashTableInstance* HashTable;
typedef size_t (*HT_HashFunCB) (const void*);

size_t    HT_Capacity(HashTable ht);
void      HT_Clear(HashTable ht);
bool      HT_ContainsKey(HashTable ht, const void* key);
bool      HT_ContainsKeyEx(HashTable ht, const void* key, size_t sizeKey);
bool      HT_ContainsValue(HashTable ht, const void* value);
bool      HT_ContainsValueEx(HashTable ht, const void* value, size_t sizeValue);
size_t    HT_Count(HashTable ht);
/* Creates */
HashTable HT_Create(size_t initialCapacity, size_t keySize, size_t valueSize, HT_HashFunCB hashFunCB);
void      HT_Destroy(HashTable ht);
bool      HT_Get(HashTable ht, const void* key, void* out_value);
bool      HT_GetDefault(HashTable ht, const void* key, const void* defaultValue, void* out_value);
bool      HT_GetEx(HashTable ht, const void* key, size_t keySize, const void* defaultValue, void* out_value);
bool      HT_Remove(HashTable ht, const void* key);
bool      HT_RemoveEx(HashTable ht, const void* key, size_t keySize);
void      HT_Set(HashTable ht, const void* key, const void* value);
void      HT_SetEx(HashTable ht, const void* key, size_t keySize, const void* value, size_t valueSize);

// Hash functions
size_t Hash_djb2(const void* data);

void HT_Debug(HashTable ht, void (*printKeyCB)(const void*), void (*printValueCB)(const void*));

#endif