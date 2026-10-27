#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/dynamicArray.h"
#include "core/stringBuilder.h"

typedef struct DynamicArrayInstance StringBuilderInstance;

void SB_AppendChar(StringBuilder sb, char c) {
    DA_Add((DynamicArray)sb, &c);
}

void SB_AppendStr(StringBuilder sb, const char* str) {
    assert(str != NULL);
    size_t len = strlen(str);
    if (len > 0) {
        DA_AddRange((DynamicArray)sb, str, len);
    }
}

void SB_AppendSubstring(StringBuilder sb, const char* str, size_t start, size_t count) {
    assert(str != NULL);
    size_t len = strlen(str);
    assert(start < len);
    assert(count > 0);
    if (start + count > len) {
        count = len - start; // Adjust count if it exceeds the string length
    }
    if (count > 0) {
        DA_AddRange((DynamicArray)sb, str + start, count);
    }
}

void SB_AppendSubstringFromSB(StringBuilder sb, size_t start, size_t count) {
    size_t len = SB_Count(sb);
    assert(start < len);
    assert(count > 0);
    if (start + count > len) {
        count = len - start; // Adjust count if it exceeds the string length
    }
    if (count > 0) {
        DA_AddRange((DynamicArray)sb, SB_CStrOffset(sb, start), count);
    }
}

void SB_AppendFormat(StringBuilder sb, const char* format, ...) {
    va_list args;
    va_start(args, format);

    // Calculate the size needed for the formatted string
    size_t size = vsnprintf(NULL, 0, format, args);
    if (size < 0) {
        va_end(args);
        return; // Handle error in vsnprintf
    }

    size_t startBuffer = DA_Count((DynamicArray)sb);
    DA_Resize((DynamicArray)sb, DA_Count((DynamicArray)sb) + size + 1);

    char* buffer = (char*)DA_Data((DynamicArray)sb) + startBuffer;
    // Write the formatted string to the buffer
    vsnprintf(buffer, size + 1, format, args);
    va_end(args);
}

size_t SB_Capacity(StringBuilder sb) {
    return DA_Capacity((DynamicArray)sb);
}

void SB_Clear(StringBuilder sb) {
    DA_Clear((DynamicArray)sb);
}

size_t SB_Count(StringBuilder sb) {
    return DA_Count((DynamicArray)sb);
}

StringBuilder SB_Create(size_t initialCapacity) {
    return (StringBuilder)DA_Create(initialCapacity, sizeof(char));
}

const char* SB_CStr(StringBuilder sb) {
    return (const char*)DA_Data((DynamicArray)sb);
}

const char* SB_CStrOffset(StringBuilder sb, size_t offset) {
    return (const char*)DA_Data((DynamicArray)sb)+offset;
}

void SB_Destroy(StringBuilder sb) {
    DA_Destroy((DynamicArray)sb);
}

void SB_InsertChar(StringBuilder sb, size_t index, char c) {
    DA_Insert((DynamicArray)sb, index, &c);
}

void SB_Insert(StringBuilder sb, size_t index, const char* str) {
    DA_InsertRange((DynamicArray)sb, index, strlen(str), str);
}

void SB_Remove(StringBuilder sb, size_t start, size_t count) {
    DA_RemoveRange((DynamicArray)sb, start, count);
}

void SB_Resize(StringBuilder sb, size_t newSize) {
    DA_Resize((DynamicArray)sb, newSize);
}

void SB_Set(StringBuilder sb, size_t index, char c) {
    DA_Set((DynamicArray)sb, index, &c);
}

void SB_TrimExcess(StringBuilder sb) {
    DA_TrimExcess((DynamicArray)sb);
}