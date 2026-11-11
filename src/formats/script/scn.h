#ifndef __SCN_H__
#define __SCN_H__

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Scene program: the SCN3 chunk of the mapinfo.bin files, and the .scn files of scenario/ (the same chunk alone). A
// header of 0x30 bytes, the code (native SH-4 code or a bytecode, depending on the version), the data used by the code
// and, in the bytecode, a list of relocations. See the SCN page and the SCN3 section of the MAPINFO page of the wiki

#define SCN_HEADER_SIZE         0x30
#define SCN_VERSION_BYTECODE    0x100
#define SCN_VERSION_SH4         0x20000
#define SCN_RELOCATION_SIZE     8
#define SCN_RELOCATION_CODE     0x2402  // The value is an offset in the code
#define SCN_RELOCATION_DATA     0x1402  // The value is an offset in the data

typedef struct {
    uint32_t    size;       // Header included
    uint32_t    version;    // SCN_VERSION_SH4 or SCN_VERSION_BYTECODE
    uint32_t    entry;      // Entry point, inside the code
    uint32_t    dataStart;  // End of the code, start of the data
    uint32_t    end;        // End of the data, start of the relocations
    uint32_t    unknown18;  // Unknown
    uint32_t    unknown1C;  // Always 0
    uint32_t    codeStart;  // Always 0x30
    uint32_t    end2;       // Bytecode: same as end. SH-4 code: 0
    uint32_t    unknown28;  // Always 0
    uint32_t    unknown2C;  // Always 0
} SCN_Header;

typedef struct {
    uint32_t    type;       // SCN_RELOCATION_CODE or SCN_RELOCATION_DATA
    uint32_t    offset;     // Of the 32-bit value to relocate, in the data
} SCN_Relocation;

typedef struct {
    const uint8_t*  data;       // From the "SCN3" ID. Not owned
    SCN_Header      header;
    uint32_t        relocationCount;
    const uint8_t*  relocations;    // Points into data, NULL if there are none
    uint32_t        relocationsEnd; // Offset after the null entry that ends the list (bytecode), header.end (SH-4 code)
} SCN_Program;

// Reads a scene program that starts at data (the "SCN3" chunk ID), of at most size bytes (the program can be shorter: its
// size is in its header). Returns false if it isn't a valid program: wrong ID, size or version, or code, data, entry
// point or relocation list out of bounds
bool SCN_Read(const uint8_t* data, size_t size, SCN_Program* program);
SCN_Relocation SCN_GetRelocation(const SCN_Program* program, uint32_t index);
bool SCN_IsSH4(const SCN_Program* program);

#endif
