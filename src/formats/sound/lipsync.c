#include <stdlib.h>
#include <string.h>
#include "core/bytes.h"
#include "formats/sound/lipsync.h"

// A record is three blocks, each one starting with its size: the speaker (8 bytes), the subtitle, and the keys (uint16
// mouth, uint16 always 2, uint32 duration) ending with 0xFFFFFFFF
bool LIPSYNC_Read(const uint8_t* table, size_t size, uint32_t offset, LIPSYNC_Record* record) {
    memset(record, 0, sizeof(*record));
    size_t pos = offset;
    if (pos + 8 > size || Bytes_ReadU32(table + pos) != 8)
        return false;
    memcpy(record->speaker, table + pos + 4, 4);
    pos += 8;
    if (pos + 4 > size)
        return false;
    uint32_t textSize = Bytes_ReadU32(table + pos);
    if (textSize < 4 || pos + textSize + 4 > size)
        return false;
    pos += textSize;
    uint32_t keysSize = Bytes_ReadU32(table + pos);
    if (keysSize < 4 || pos + keysSize > size)
        return false;
    uint32_t count = keysSize >= 8 ? (keysSize - 8) / 8 : 0;
    if (count == 0)
        return true;
    record->keys = malloc(count * sizeof(LIPSYNC_Key));
    if (record->keys == NULL)
        return false;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t* key = table + pos + 4 + i * 8;
        record->keys[i] = (LIPSYNC_Key) { Bytes_ReadU16(key), Bytes_ReadU32(key + 4) };
        record->duration += record->keys[i].duration;
    }
    record->keyCount = count;
    return true;
}

void LIPSYNC_Free(LIPSYNC_Record* record) {
    free(record->keys);
    memset(record, 0, sizeof(*record));
}

uint16_t LIPSYNC_MouthAt(const LIPSYNC_Record* record, float time, float* start, uint16_t* previous) {
    float end = 0.0f;
    uint16_t before = 0;
    for (uint32_t i = 0; i < record->keyCount; i++) {
        float keyStart = end;
        end += (float)record->keys[i].duration;
        if (time >= keyStart && time < end) {
            if (start != NULL)
                *start = keyStart;
            if (previous != NULL)
                *previous = before;
            return record->keys[i].mouth;
        }
        before = record->keys[i].mouth;
    }
    if (start != NULL)
        *start = end;
    if (previous != NULL)
        *previous = before;
    return 0;
}
