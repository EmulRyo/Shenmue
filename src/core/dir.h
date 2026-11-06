#ifndef __DIR_H__
#define __DIR_H__

#include <stddef.h>
#include "core/hashTable.h"

int MakeDirs(const char* path);
// A set of the folders already created, to skip them in MakeDirsCached. Not thread-safe: one per thread. Free it with
// HT_Destroy
HashTable DirCache_Create(void);
// MakeDirs, unless path is in cache (which may be NULL)
int MakeDirsCached(const char* path, HashTable cache);
// Opens the folder at path (UTF-8) in the file explorer. Returns 0 on success
int OpenDir(const char* path);

#endif
