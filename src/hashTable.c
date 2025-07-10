#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "hashTable.h"

typedef enum {
    ENTRY_EMPTY,
    ENTRY_OCCUPIED,
    ENTRY_DELETED
} EntryState;

typedef struct {
    size_t psl; // Probe Sequence Length
    EntryState state;
    char data[]; // Flexible array member for key and value
} Entry;

typedef struct HashTableInstance {
    size_t count;
    size_t capacity;
    size_t keySize;
    size_t valueSize;
    char* data; // Stores data as Entry + key + value
    HT_HashFunCB hashFunCB;
} HashTableInstance;

// djb2 hash (simple y eficaz para strings)
size_t Hash_djb2(const void* data) {
    const char* str = (const char*)data;
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

static size_t IndexOfKey(HashTable ht, const void* key, size_t keySize) {
    size_t entrySize = sizeof(Entry) + ht->keySize + ht->valueSize;
    size_t index = ht->hashFunCB(key) % ht->capacity;
    keySize = (keySize > ht->keySize) ? ht->keySize : keySize; // Ensure it doesnt exceed the size of the key 
    size_t psl = 0;

    while (true) {
        Entry* slot = (Entry*)(ht->data + entrySize * index);

        if (slot->state == ENTRY_EMPTY)
            return SIZE_MAX; // Doesnt exist

        if (slot->state == ENTRY_OCCUPIED && memcmp(slot->data, key, keySize) == 0)
            return index;

        if (slot->psl < psl)
            return SIZE_MAX; // It can't be farther than this

        index = (index + 1) % ht->capacity;
        psl++;
    }
}

static void ResizeIfNeeded(HashTable ht) {
    if (ht->count >= ht->capacity * 0.6) {
        size_t entrySize = sizeof(Entry) + ht->keySize + ht->valueSize;
        size_t newCapacity = ht->capacity * 2;
        HashTable newHT = HT_Create(newCapacity, ht->keySize, ht->valueSize, ht->hashFunCB);
        for (size_t i = 0; i < ht->capacity; i++) {
            Entry* oldSlot = (Entry*)(ht->data + entrySize * i);
            if (oldSlot->state == ENTRY_OCCUPIED) {
                HT_Set(newHT, oldSlot->data, oldSlot->data + ht->keySize);
            }
        }
        free(ht->data);
        *ht = *newHT; // Copy the new hash table
        free(newHT);
    }
}

HashTable HT_Create(size_t initialCapacity, size_t keySize, size_t valueSize, HT_HashFunCB hashFunCB) {
    initialCapacity = (initialCapacity > 0) ? initialCapacity : 32;
    HashTable ht = malloc(sizeof(HashTableInstance));
    assert(ht != NULL);
    ht->capacity = initialCapacity;
    ht->count = 0;
    ht->keySize = keySize;
    ht->valueSize = valueSize;
    ht->data = calloc(initialCapacity+2, sizeof(Entry)+keySize+valueSize); // +2 to use it for temporary storage
    ht->hashFunCB = hashFunCB;
    return ht;
}

size_t HT_Capacity(HashTable ht) {
    return ht->capacity;
}

size_t HT_Count(HashTable ht) {
    return ht->count;
}

void HT_Clear(HashTable ht) {
    size_t entrySize = sizeof(Entry) + ht->keySize + ht->valueSize;
    for (size_t i = 0; i < ht->capacity; i++) {
        Entry* slot = (Entry*)(ht->data + entrySize * i);
        slot->state = ENTRY_EMPTY;
        slot->psl = 0;
    }
    ht->count = 0;
}

// Insertion with Robin Hood hashing
void HT_SetEx(HashTable ht, const void* key, size_t keySize, const void* value, size_t valueSize) {
    ResizeIfNeeded(ht);
    size_t entrySize = sizeof(Entry) + ht->keySize + ht->valueSize;
    size_t index = ht->hashFunCB(key) % ht->capacity;
    keySize = (keySize > ht->keySize) ? ht->keySize : keySize; // Ensure it doesnt exceed the size of the key
	valueSize = (valueSize > ht->valueSize) ? ht->valueSize : valueSize; // Ensure it doesnt exceed the size of the value
    
    Entry* newEntry = (Entry*)(ht->data + entrySize * ht->capacity); // we use a temporary slot at the end of the array
    newEntry->psl = 0;
    newEntry->state = ENTRY_OCCUPIED;
    memcpy(newEntry->data, key, keySize);
    memcpy(newEntry->data + ht->keySize, value, valueSize);

    while (true) {
        Entry* slot = (Entry*)(ht->data + entrySize*index);

        if (slot->state == ENTRY_EMPTY || slot->state == ENTRY_DELETED) {
            // Empty space or slot set as deleted
            memcpy(slot, newEntry, entrySize);
            ht->count++;
            return;
        }

        if (memcmp(slot->data, key, keySize) == 0) {
            // key already exists -> replace value
            memcpy(slot->data + ht->keySize, value, valueSize);
            return;
        }

        if (slot->psl < newEntry->psl) {
            // Robin Hood: swap
            Entry* temp = (Entry*)(ht->data + entrySize * (ht->capacity+1)); // we use a temporary slot at the end of the array
            memcpy(temp, slot, entrySize);
            memcpy(slot, newEntry, entrySize);
            memcpy(newEntry, temp, entrySize);
        }

        // Avanzamos
        index = (index + 1) % ht->capacity;
        newEntry->psl++;
    }
}

void HT_Set(HashTable ht, const void* key, const void* value) {
    HT_SetEx(ht, key, ht->keySize, value, ht->valueSize);
}

bool HT_GetEx(HashTable ht, const void* key, size_t keySize, const void* defaultValue, void* out_value) {
	size_t index = IndexOfKey(ht, key, keySize);
    if (index == SIZE_MAX) {
        if (defaultValue != NULL) {
            memcpy(out_value, defaultValue, ht->valueSize);
        }
        return false; // Key not found
    }
    size_t entrySize = sizeof(Entry) + ht->keySize + ht->valueSize;
    Entry* slot = (Entry*)(ht->data + entrySize * index);
    memcpy(out_value, slot->data + ht->keySize, ht->valueSize);
	return true;
}

bool HT_GetDefault(HashTable ht, const void* key, const void* defaultValue, void* out_value) {
    return HT_GetEx(ht, key, ht->keySize, defaultValue, out_value);
}

bool HT_Get(HashTable ht, const void* key, void* out_value) {
    return HT_GetEx(ht, key, ht->keySize, NULL, out_value);
}

bool HT_ContainsKey(HashTable ht, const void* key) {
	return IndexOfKey(ht, key, ht->keySize) != SIZE_MAX;
}

bool HT_ContainsKeyEx(HashTable ht, const void* key, size_t keySize) {
    return IndexOfKey(ht, key, keySize) != SIZE_MAX;
}

bool HT_ContainsValueEx(HashTable ht, const void* value, size_t sizeValue) {
    size_t entrySize = sizeof(Entry) + ht->keySize + ht->valueSize;

    for (size_t i = 0; i < ht->capacity; i++) {
        Entry* slot = (Entry*)(ht->data + entrySize * i);
        if (slot->state == ENTRY_OCCUPIED && memcmp(slot->data + ht->keySize, value, sizeValue) == 0) {
            return true;
        }
	}

	return false;
}

bool HT_ContainsValue(HashTable ht, const void* value) {
	return HT_ContainsValueEx(ht, value, ht->valueSize);
}

bool HT_RemoveEx(HashTable ht, const void* key, size_t keySize) {
    size_t entrySize = sizeof(Entry) + ht->keySize + ht->valueSize;
    size_t index = ht->hashFunCB(key) % ht->capacity;
    keySize = (keySize > ht->keySize) ? ht->keySize : keySize; // Ensure it doesnt exceed the size of the key 
    size_t psl = 0;

    while (true) {
        Entry* slot = (Entry*)(ht->data + entrySize * index);

        if (slot->state == ENTRY_EMPTY)
            return false;

        if (slot->state == ENTRY_OCCUPIED && memcmp(slot->data, key, keySize) == 0) {
            slot->state = ENTRY_DELETED;
            ht->count--;
            return true;
        }

        if (slot->psl < psl)
            return false;

        index = (index + 1) % ht->capacity;
        psl++;
    }
}

bool HT_Remove(HashTable ht, const void* key) {
    return HT_RemoveEx(ht, key, ht->keySize);
}

void HT_Destroy(HashTable ht) {
    free(ht->data);
    free(ht);
}

void HT_Debug(HashTable ht, void (*printKeyCB)(const void *), void (*printValueCB)(const void*)) {
	size_t maxPsl = 0;
    size_t entrySize = sizeof(Entry) + ht->keySize + ht->valueSize;
    printf("------------------ HashTable Debug Info: ------------------\n");
    printf("| capacity: %zu, count: %zu, key size: %zu, value size: %zu     |\n", ht->capacity, ht->count, ht->keySize, ht->valueSize);
    printf("-----------------------------------------------------------\n");
    printf("|       hash | bucket | psl | index |        key | value  |\n");
    printf("-----------------------------------------------------------\n");
    for (int i = 0; i < ht->capacity; i++) {
        Entry* slot = (Entry*)(ht->data + entrySize * i);
        if (slot->state == ENTRY_OCCUPIED) {
            size_t hash = ht->hashFunCB(slot->data);
            printf("| 0x%08llX | %6zu | %3zu | %5d | ", hash, hash % ht->capacity, slot->psl, i);
            printKeyCB(slot->data);
            printf(" | ");
            printValueCB(slot->data + ht->keySize);
            printf("\n");
            if (slot->psl > maxPsl) {
                maxPsl = slot->psl;
			}
        }
    }
    printf("-----------------------------------------------------------\n");
	printf("| Max PSL: %zu |\n", maxPsl);
}
