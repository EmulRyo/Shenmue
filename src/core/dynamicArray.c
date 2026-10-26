#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "core/dynamicArray.h"

typedef struct DynamicArrayInstance {
    size_t count;
    size_t capacity;
    size_t elemSize;
    void*  data;
    void*  tmp;
	DA_CompareFunCB cmp;
} DynamicArrayInstance;

static unsigned int NextPowerOf2(unsigned int n) {
    if (n == 0) return 1;

    n--;                        // Resta 1 para manejar casos donde n ya es potencia de 2
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}

static void ResizeIfNeeded(DynamicArray da, size_t newSize) {
    if (newSize > da->capacity) {
		size_t newCapacity = da->capacity * 2;
		while (newCapacity < newSize) {
			newCapacity *= 2;
		}
        void* newPtr = realloc(da->data, newCapacity * da->elemSize);
        if (newPtr == NULL) {
            printf("Error: unable to allocate memory. Aborting\n");
            free(da->data);
            abort();
        }
        else {
            da->data = newPtr;
            da->capacity = newCapacity;
        }
    }
}

static inline void* Ptr(DynamicArray da, size_t index) {
    return (char*)da->data + da->elemSize * index;
}

static int Compare(DynamicArray da, size_t a, size_t b) {
	void* p1 = Ptr(da, a);
	void* p2 = Ptr(da, b);

	return da->cmp(p1, p2);
}

static void Swap(DynamicArray da, size_t a, size_t b) {
	void* p1 = Ptr(da, a);
    void* p2 = Ptr(da, b);

    memcpy(da->tmp,      p1, da->elemSize);
    memcpy(     p1,      p2, da->elemSize);
    memcpy(     p2, da->tmp, da->elemSize);
}

void DA_Add(DynamicArray da, const void* element) {
    ResizeIfNeeded(da, da->count+1);
    memcpy(Ptr(da, da->count), element, da->elemSize);
    da->count++;
}

void DA_AddRange(DynamicArray da, const void* elements, size_t count) {
    assert(elements && count > 0 && "DA_AddRange: invalid input parameters");
    ResizeIfNeeded(da, da->count + count);
    memcpy(Ptr(da, da->count), elements, count * da->elemSize);
    da->count += count;
}

size_t DA_Capacity(DynamicArray da) {
    return da->capacity;
}

void DA_Clear(DynamicArray da) {
    da->count = 0;
}

bool DA_Contains(DynamicArray da, const void* element) {
    return DA_IndexOf(da, element) != SIZE_MAX;
}

size_t DA_Count(DynamicArray da) {
    return da->count;
}

DynamicArray DA_Create(size_t initialCapacity, size_t elementSize) {
    initialCapacity = NextPowerOf2((unsigned int)(initialCapacity));
    DynamicArray inst = malloc(sizeof(DynamicArrayInstance));
	assert(inst && "DA_Create: memory allocation failed");
    inst->capacity = initialCapacity;
    inst->elemSize = elementSize;
    inst->data = malloc(initialCapacity * elementSize);
    inst->count = 0;
	inst->tmp = NULL;
	inst->cmp = NULL;

    return inst;
}

void DA_Destroy(DynamicArray da) {
    if (da->data) {
        free(da->data);
        da->data = NULL;
        da->count = 0;
        da->capacity = 0;
        da->elemSize = 0;
    }
	if (da->tmp) {
		free(da->tmp);
		da->tmp = NULL;
	}
}

void* DA_Get(DynamicArray da, size_t index) {
    assert(index >= 0 && index < da->count && "DA_Get: index out of bounds");
    return Ptr(da, index);
}

void* DA_Data(DynamicArray da) {
	return da->data;
}

size_t DA_IndexOf(DynamicArray da, const void* element) {
    for (int i = 0; i < da->count; i++) {
        if (memcmp(element, Ptr(da, i), da->elemSize) == 0)
            return i;
    }
    return SIZE_MAX;
}

void DA_Insert(DynamicArray da, size_t index, const void* element) {
    assert(index >= 0 && index <= da->count && "DA_Insert: index out of bounds");
    ResizeIfNeeded(da, da->count+1);
    if (index < da->count)
        memmove(Ptr(da, index+1), Ptr(da, index), (da->count - index)*da->elemSize);
    memcpy(Ptr(da, index), element, da->elemSize);
    da->count++;
}

void DA_InsertRange(DynamicArray da, size_t index, size_t count, const void* elements) {
    assert(index >= 0 && index <= da->count && "DA_InsertRange: index out of bounds");
    ResizeIfNeeded(da, da->count + count);
    if (index < da->count)
        memmove(Ptr(da, index + count), Ptr(da, index), (da->count - index)*da->elemSize);
    memcpy(Ptr(da, index), elements, count*da->elemSize);
    da->count += count;
}

size_t DA_LastIndexOf(DynamicArray da, const void* element) {
    for (size_t i = da->count-1; i != SIZE_MAX; i--) {
        if (memcmp(element, Ptr(da, i), da->elemSize) == 0)
            return i;
    }
    return SIZE_MAX;
}

bool DA_Remove(DynamicArray da, const void* element) {
    size_t index = DA_IndexOf(da, element);
    if (index != SIZE_MAX) {
        DA_RemoveAt(da, index);
        return true;
    }

    return false;
}

void DA_RemoveAt(DynamicArray da, size_t index) {
    assert(index >= 0 && index < da->count && "DA_RemoveAt: index out of bounds");
    if (index < da->count - 1)
        memmove(Ptr(da, index), Ptr(da, index+1), (da->count - index)*da->elemSize);

    da->count--;
}

void DA_RemoveRange(DynamicArray da, size_t index, size_t count) {
    assert(index >= 0 && index < da->count && "DA_RemoveRange: index out of bounds");
    assert(count > 0 && index + count <= da->count && "DA_RemoveRange: count out of bounds");
    if (index + count < da->count)
        memmove(Ptr(da, index), Ptr(da, index + count), (da->count - (index + count))*da->elemSize);
    da->count -= count;
}

void DA_Reverse(DynamicArray da) {
    da->tmp = malloc(da->elemSize);
    size_t head = 0;
    size_t tail = da->count - 1;

    for (int i = 0; i < (da->count / 2); i++) {
		Swap(da, head, tail);

        head += 1;
        tail -= 1;
    }

    free(da->tmp);
	da->tmp = NULL;
}

void DA_Set(DynamicArray da, size_t index, const void* element) {
    assert(index >= 0 && index < da->count && "DA_Set: index out of bounds");
    memcpy(Ptr(da, index), element, da->elemSize);
}

void DA_Shuffle(DynamicArray da) {
    static bool randInitialized = false;
	if (!randInitialized) {
        srand((unsigned int)time(NULL));
		randInitialized = true;
	}

    if (da->tmp == NULL)
	    da->tmp = malloc(da->elemSize);

    // Durstenfeld
    for (size_t i = da->count - 1; i > 0; i--) {
        size_t j = rand() % (i + 1);
        Swap(da, i, j);
    }

	free(da->tmp);
	da->tmp = NULL;
}

static void InsertionSort(DynamicArray da, size_t begin, size_t end) {
    for (size_t i = begin + 1; i < end; i++) {
        for (size_t j = i; j > begin && Compare(da, j-1, j) > 0; j--) {
            Swap(da, j, j-1);
        }
    }
}

// Heapify
static void Heapify(DynamicArray da, size_t begin, size_t count, size_t root) {
    size_t largest = root;
    size_t left = 2 * root + 1;
    size_t right = 2 * root + 2;

    if (left < count && Compare(da, begin+left, begin+largest) > 0)
        largest = left;

    if (right < count && Compare(da, begin+right, begin+largest) > 0)
        largest = right;

    if (largest != root) {
        Swap(da, begin+root, begin+largest);
        Heapify(da, begin, count, largest);
    }
}

static void HeapSort(DynamicArray da, size_t begin, size_t end) {
    size_t count = end - begin;

    for (size_t i = count / 2; i-- > 0; ) {
        Heapify(da, begin, count, i);
    }

    for (size_t i = count - 1; i > 0; i--) {
        Swap(da, begin, begin+i);
        Heapify(da, begin, i, 0);
    }
}

static size_t MedianOfThree(DynamicArray da, size_t a, size_t b, size_t c) {
    void* A = Ptr(da, a);
    void* B = Ptr(da, b);
    void* C = Ptr(da, c);

	DA_CompareFunCB cmp = da->cmp;

    if (cmp(A, B) < 0) {
        if (cmp(B, C) < 0) return b;
        else if (cmp(A, C) < 0) return c;
        else return a;
    }
    else {
        if (cmp(A, C) < 0) return a;
        else if (cmp(B, C) < 0) return c;
        else return b;
    }
}

// Hoare Partition with Median of Three Pivot
static size_t Partition(DynamicArray da, size_t begin, size_t end) {
    size_t mid = begin + (end - begin) / 2;
    size_t pivotIndex = MedianOfThree(da, begin, mid, end-1);

    // Move the pivot value to the begin in order to use it as a fixed reference
    Swap(da, pivotIndex, begin);
	void* pivotValue = (char*)da->tmp + da->elemSize;
    memcpy(pivotValue, Ptr(da, begin), da->elemSize); // Copy the value of the pivot

    size_t i = begin - 1;
    size_t j = end;

    while (1) {
        do { i++; } while (da->cmp(Ptr(da, i), pivotValue) < 0);
        do { j--; } while (da->cmp(Ptr(da, j), pivotValue) > 0);

        if (i >= j)
            return j;

        Swap(da, i, j);
    }
}

static void Introsort(DynamicArray da, size_t begin, size_t end, int depthLimit) {
    size_t size = end - begin;

    if (size < 16) {
        InsertionSort(da, begin, end);
        return;
    }

    if (depthLimit == 0) {
        HeapSort(da, begin, end);
        return;
    }

    size_t pivotIndex = Partition(da, begin, end);
    Introsort(da, begin, pivotIndex + 1, depthLimit - 1);
    Introsort(da, pivotIndex + 1, end, depthLimit - 1);
}

void DA_Resize(DynamicArray da, size_t newSize) {
	assert(newSize >= 0 && "DA_Resize: newSize must be non-negative");
	ResizeIfNeeded(da, newSize);
	da->count = newSize;
}

void DA_ResizeFillValue(DynamicArray da, size_t newSize, const void* fillValue) {
	assert(newSize >= 0 && "DA_ResizeFillValue: newSize must be non-negative");
	ResizeIfNeeded(da, newSize);
	if (newSize > da->count) {
		for (size_t i = da->count; i < newSize; i++) {
			memcpy(Ptr(da, i), fillValue, da->elemSize);
		}
	}
    da->count = newSize;
}

void DA_Sort(DynamicArray da, DA_CompareFunCB compareFunCB) {
    assert(da && compareFunCB && da->count && da->elemSize && "DA_Sort: Invalid input parameters");

    int depthLimit = 2 * (int)log2((double)da->count);

	da->tmp = malloc(da->elemSize*2);
	da->cmp = compareFunCB;

    // Perform a recursive Introsort
    Introsort(da, 0, da->count, depthLimit);

	free(da->tmp);
	da->tmp = NULL;
}

void DA_TrimExcess(DynamicArray da) {
	if (da->count < da->capacity * 0.9) {
		void* newPtr = realloc(da->data, da->count * da->elemSize);
		if (newPtr == NULL) {
			printf("Error: unable to allocate memory. Aborting\n");
			free(da->data);
			abort();
		}
		else {
			da->data = newPtr;
			da->capacity = da->count;
		}
	}
}