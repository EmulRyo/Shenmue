#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "core/bytes.h"
#include "core/file.h"
#include "formats/animation/motn.h"

// Header: table offset (always 0x20), names offset, data offset, last ID, size, first ID, last ID again, 0.
// There are lastID - firstID - 1 motions; the names offset points to one name offset per motion
#define MOTN_HEADER_SIZE 0x20

void MOTN_Free(MOTN_Bank* bank) {
    free(bank->names);
    free(bank->data);
    memset(bank, 0, sizeof(MOTN_Bank));
}

static bool ReadNames(MOTN_Bank* bank) {
    const uint8_t* data = bank->data;
    if (bank->size < MOTN_HEADER_SIZE || Bytes_ReadU32(data) != MOTN_HEADER_SIZE)
        return false;
    uint32_t namesOffset = Bytes_ReadU32(data + 0x04);
    uint32_t lastID = Bytes_ReadU32(data + 0x0C);
    bank->firstID = Bytes_ReadU32(data + 0x14);
    if (lastID <= bank->firstID || Bytes_ReadU32(data + 0x18) != lastID)
        return false;
    bank->count = lastID - bank->firstID - 1;
    if (namesOffset > bank->size || bank->count > (bank->size - namesOffset) / 4)
        return false;

    bank->names = calloc(bank->count + 1, sizeof(const char*));
    if (bank->names == NULL)
        return false;
    for (uint32_t i = 0; i < bank->count; i++) {
        uint32_t offset = Bytes_ReadU32(data + namesOffset + i * 4);
        if (offset >= bank->size || memchr(data + offset, '\0', bank->size - offset) == NULL)
            return false;
        bank->names[i] = (const char*)(data + offset);
    }
    return true;
}

bool MOTN_LoadNames(const char* filepath, MOTN_Bank* bank) {
    memset(bank, 0, sizeof(MOTN_Bank));
    bank->data = File_LoadToMemoryDecompressed(filepath, &bank->size);
    if (bank->data == NULL)
        return false;
    if (!ReadNames(bank)) {
        fprintf(stderr, "[MOTN_LoadNames]: Invalid MOTN file %s\n", filepath);
        MOTN_Free(bank);
        return false;
    }
    return true;
}

const char* MOTN_FindName(const MOTN_Bank* bank, uint32_t id) {
    if (id <= bank->firstID || id - bank->firstID - 1 >= bank->count)
        return NULL;
    return bank->names[id - bank->firstID - 1];
}
