#ifndef __TEST_H__
#define __TEST_H__

#include <stdbool.h>
#include <stdint.h>

// Minimal test framework. A test is a function that runs checks over the game files of the content folder: a failed
// check prints its location and message, and the test fails if any check failed. Tests print statistics with
// Test_Info, so the output can be compared with the wiki

typedef struct {
    const char* content;    // Folder with the extracted files of the game (e.g. content/Shenmue1)
} TestContext;

typedef void (*TestFunction)(const TestContext* context);

typedef struct {
    const char*     name;
    TestFunction    function;
    bool            content;    // Needs the files of the game
} Test;

void Test_Check(bool condition, const char* file, int line, const char* format, ...);
void Test_Info(const char* format, ...);

#define CHECK(condition, ...) Test_Check((condition), __FILE__, __LINE__, __VA_ARGS__)
// Checks an unsigned value (counts, offsets) against the expected one
#define CHECK_EQ(actual, expected, what) \
    CHECK((uint64_t)(actual) == (uint64_t)(expected), "%s: %llu (0x%llX), expected %llu (0x%llX)", what, \
        (unsigned long long)(actual), (unsigned long long)(actual), (unsigned long long)(expected), \
        (unsigned long long)(expected))

#endif
