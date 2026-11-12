#include <string.h>
#include "core/bytes.h"
#include "formats/script/scn.h"

bool SCN_Read(const uint8_t* data, size_t size, SCN_Program* program) {
    memset(program, 0, sizeof(*program));
    if (size < SCN_HEADER_SIZE || memcmp(data, "SCN3", 4) != 0)
        return false;
    SCN_Header* header = &program->header;
    header->size = Bytes_ReadU32(data + 0x04);
    header->version = Bytes_ReadU32(data + 0x08);
    header->entry = Bytes_ReadU32(data + 0x0C);
    header->dataStart = Bytes_ReadU32(data + 0x10);
    header->end = Bytes_ReadU32(data + 0x14);
    header->variableSize = Bytes_ReadU32(data + 0x18);
    header->unknown1C = Bytes_ReadU32(data + 0x1C);
    header->codeStart = Bytes_ReadU32(data + 0x20);
    header->end2 = Bytes_ReadU32(data + 0x24);
    header->unknown28 = Bytes_ReadU32(data + 0x28);
    header->unknown2C = Bytes_ReadU32(data + 0x2C);
    if (header->size < SCN_HEADER_SIZE || header->size > size)
        return false;
    if (header->version != SCN_VERSION_SH4 && header->version != SCN_VERSION_BYTECODE)
        return false;
    if (header->codeStart < SCN_HEADER_SIZE || header->codeStart > header->dataStart ||
        header->dataStart > header->end || header->end > header->size)
        return false;
    if (header->entry < header->codeStart || header->entry >= header->dataStart)
        return false;
    program->data = data;
    program->relocationsEnd = header->end;
    if (header->version == SCN_VERSION_SH4)
        return true;

    // Relocations up to an entry of 8 null bytes
    uint32_t count = 0;
    for (uint32_t pos = header->end;; pos += SCN_RELOCATION_SIZE, count++) {
        if (pos + SCN_RELOCATION_SIZE > header->size) {
            memset(program, 0, sizeof(*program));
            return false;
        }
        if (Bytes_ReadU32(data + pos) == 0 && Bytes_ReadU32(data + pos + 4) == 0) {
            program->relocationsEnd = pos + SCN_RELOCATION_SIZE;
            break;
        }
    }
    program->relocationCount = count;
    program->relocations = count > 0 ? data + header->end : NULL;
    return true;
}

SCN_Relocation SCN_GetRelocation(const SCN_Program* program, uint32_t index) {
    SCN_Relocation relocation = { 0 };
    if (index < program->relocationCount) {
        const uint8_t* entry = program->relocations + index * SCN_RELOCATION_SIZE;
        relocation.type = Bytes_ReadU32(entry);
        relocation.offset = Bytes_ReadU32(entry + 4);
    }
    return relocation;
}

bool SCN_IsSH4(const SCN_Program* program) {
    return program->header.version == SCN_VERSION_SH4;
}
