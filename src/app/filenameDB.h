#ifndef __FILENAMEDB_H__
#define __FILENAMEDB_H__

#include "core/hashTable.h"

// Loads the database of the filenames of the TAD entries (resources/FilenameDatabase.txt, embedded in the executable)
// into a table from the hash (uint32_t) to the path (char[128])
HashTable FilenameDB_Load(void);

#endif
