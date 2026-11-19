#ifndef __LIPSYNC_H__
#define __LIPSYNC_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Lip-sync tables: entries without name of the audio AFS files, one per group of voice clips, with for each clip the
// character who speaks, its subtitle and the shapes of the mouth (0 to 5) with the time each one is held, in 1/60 s. The
// IDX0 entry of a clip gives its table and the offset of its record (IDX_FindLipSync)

#define LIPSYNC_FRAME_RATE  60.0f

typedef struct {
    uint16_t    mouth;      // 0 to 5
    uint32_t    duration;   // 1/60 s
} LIPSYNC_Key;

typedef struct {
    char            speaker[5];  // Character ID, empty if none
    uint32_t        keyCount;
    LIPSYNC_Key*    keys;
    uint32_t        duration;    // Sum of the durations of the keys
} LIPSYNC_Record;

// Reads the record at offset of a lip-sync table. Returns false if it is out of the table or invalid
bool LIPSYNC_Read(const uint8_t* table, size_t size, uint32_t offset, LIPSYNC_Record* record);
void LIPSYNC_Free(LIPSYNC_Record* record);
// Shape of the mouth at a time (1/60 s) from the start of the clip, 0 after the end. *start receives the time at which
// that key starts (NULL to ignore), and *previous the shape of the key before it (0 for the first one)
uint16_t LIPSYNC_MouthAt(const LIPSYNC_Record* record, float time, float* start, uint16_t* previous);

#endif
