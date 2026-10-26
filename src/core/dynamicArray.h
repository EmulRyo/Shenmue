#ifndef __DYNAMIC_ARRAY_H__
#define __DYNAMIC_ARRAY_H__

#include <stdbool.h>

typedef struct DynamicArrayInstance* DynamicArray;
typedef int (*DA_CompareFunCB) (const void*, const void*);

void         DA_Add(DynamicArray da, const void* element);                  // Adds an element to the end of the array
void         DA_AddRange(DynamicArray da, 
                const void* elements, size_t count);                        // Adds a range of elements to the end of the array
size_t       DA_Capacity(DynamicArray da);                                  // Gets the total number of elements the internal data structure can hold without resizing
void         DA_Clear(DynamicArray da);                                     // Removes all elements from the array
bool         DA_Contains(DynamicArray da, const void* element);             // Determines whether an element is in the array
size_t       DA_Count(DynamicArray da);                                     // Gets the number of elements contained in the array
DynamicArray DA_Create(size_t initialCapacity, size_t elementSize);         // Initializes a new instance of the array that can hold elements of size "elementSize", that is empty and has the specified "initialCapacity"
void*        DA_Data(DynamicArray da);                                      // Gets a pointer to the internal data structure
void         DA_Destroy(DynamicArray da);                                   // Releases the resources allocated by the array
void*        DA_Get(DynamicArray da, size_t index);                         // Gets the element at the specified index
size_t       DA_IndexOf(DynamicArray da, const void* element);              // Searches for the specified element and returns the zero-based index of the first occurrence within the entire array. Returns SIZE_MAX if the element is not found
void         DA_Insert(DynamicArray da, size_t index, const void* element); // Inserts an element into the array at the specified index
void         DA_InsertRange(DynamicArray da,
                size_t index, size_t count, const void* elements);          // Inserts the elements into the array at the specified index
size_t       DA_LastIndexOf(DynamicArray da, const void* element);          // Searches for the specified element and returns the zero-based index of the last occurrence within the entire array. Returns SIZE_MAX if the element is not found
bool         DA_Remove(DynamicArray da, const void* element);               // Removes the first occurrence of a specific element from the array
void         DA_RemoveAt(DynamicArray da, size_t index);                    // Removes the element at the specified index of the array
void         DA_RemoveRange(DynamicArray da, size_t index, size_t count);   // Removes a range of elements from the array
void         DA_Resize(DynamicArray da, size_t newSize);                    // Resizes the array to the specified size, if the new size is larger than the current capacity, the capacity is increased to accommodate the new size
void         DA_ResizeFillValue(DynamicArray da,
                size_t newSize, const void* fillValue);                     // Resizes the array to the specified size and fills the new elements with the specified value
void         DA_Reverse(DynamicArray da);                                   // Reverses the order of the elements in the entire array
void         DA_Set(DynamicArray da, size_t index, const void* element);    // Sets the element at the specified index
void         DA_Shuffle(DynamicArray da);                                   // Randomizes the order of the elements in the entire array
void         DA_Sort(DynamicArray da, DA_CompareFunCB compareFunCB);        // Sorts the elements in the entire array using the specified comparison function
void         DA_TrimExcess(DynamicArray da);                                // Sets the capacity to the actual number of elements in the array, if that number is less than 90% of current capacity

#endif