#ifndef __STRING_BUILDER_H__
#define __STRING_BUILDER_H__

#include <stdbool.h>

typedef struct StringBuilderInstance* StringBuilder;

void          SB_AppendChar(StringBuilder sb, char c);                        // Adds a char to the end of the array
void          SB_AppendStr(StringBuilder sb, const char* str);                // Adds a string to the end of the array
void          SB_AppendSubstring(StringBuilder sb, const char* str, size_t start, size_t count); // Adds a substring to the end of the array
void          SB_AppendSubstringFromSB(StringBuilder sb, size_t start, size_t count); // Adds a substring to the end of the array
void          SB_AppendFormat(StringBuilder sb, const char* format, ...);     // Adds a formatted string to the end of the array
size_t        SB_Capacity(StringBuilder sb);                                  // Gets the total number of chars the internal data structure can hold without resizing
void          SB_Clear(StringBuilder sb);                                     // Removes all chars from the array
size_t        SB_Count(StringBuilder sb);                                     // Gets the number of chars contained in the array
StringBuilder SB_Create(size_t initialCapacity);                              // Initializes a new instance of the array that can hold strings, that is empty and has the specified "initialCapacity"
const char*   SB_CStr(StringBuilder sb);                                      // Gets a pointer to the internal data structure
const char*   SB_CStrOffset(StringBuilder sb, size_t offset);				  // Gets a pointer to the internal data structure at the specified offset
void          SB_Destroy(StringBuilder sb);                                   // Releases the resources allocated by the array
void          SB_Insert(StringBuilder sb, size_t index, const char* str);     // Inserts a string into the array at the specified index
void          SB_Remove(StringBuilder sb, size_t start, size_t count);        // Removes the specified range of characters from the string builder
void          SB_Resize(StringBuilder sb, size_t newSize);                    // Resizes the array to the specified size, if the new size is larger than the current capacity, the capacity is increased to accommodate the new size
void          SB_Set(StringBuilder sb, size_t index, char c);                 // Sets the char at the specified index
void          SB_TrimExcess(StringBuilder sb);                                // Sets the capacity to the actual number of chars in the array, if that number is less than 90% of current capacity

#endif