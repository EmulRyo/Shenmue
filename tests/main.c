#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "corpus.h"
#include "test.h"

// ShenmueTests <content folder> [test...]: runs the given tests, or all of them. Returns 0 if they pass, 1 if one
// failed, and TEST_SKIPPED (the SKIP_RETURN_CODE of CTest) if the tests need the files of the game and the content
// folder isn't there

#define TEST_SKIPPED 77

void Test_MapinfoChunks(const TestContext* context);
void Test_Scn3Chunks(const TestContext* context);
void Test_ScnFiles(const TestContext* context);
void Test_ScnPairs(const TestContext* context);
void Test_Sh4Encodings(const TestContext* context);
void Test_Sh4Code(const TestContext* context);
void Test_Sh4Program(const TestContext* context);

static const Test s_tests[] = {
    { "mapinfo_chunks", Test_MapinfoChunks, true },
    { "scn3_chunks",    Test_Scn3Chunks,    true },
    { "scn_files",      Test_ScnFiles,      true },
    { "scn_pairs",      Test_ScnPairs,      true },
    { "sh4_encodings",  Test_Sh4Encodings,  false },
    { "sh4_code",       Test_Sh4Code,       true },
    { "sh4_program",    Test_Sh4Program,    true },
};

static int s_checks;
static int s_failures;

void Test_Check(bool condition, const char* file, int line, const char* format, ...) {
    s_checks++;
    if (condition)
        return;
    s_failures++;
    fprintf(stderr, "%s(%d): FAILED: ", file, line);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
}

void Test_Info(const char* format, ...) {
    fputs("  ", stdout);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    fputc('\n', stdout);
}

static bool RunTest(const Test* test, const TestContext* context) {
    int failures = s_failures, checks = s_checks;
    printf("[%s]\n", test->name);
    test->function(context);
    bool passed = s_failures == failures;
    printf("[%s] %s: %d checks, %d failed\n", test->name, passed ? "PASSED" : "FAILED", s_checks - checks,
        s_failures - failures);
    fflush(stdout);
    return passed;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <content folder> [test...]\nTests:", argv[0]);
        for (size_t i = 0; i < sizeof(s_tests) / sizeof(s_tests[0]); i++)
            fprintf(stderr, " %s", s_tests[i].name);
        fputc('\n', stderr);
        return 1;
    }
    TestContext context = { argv[1] };
    bool content = Corpus_Exists(context.content);
    const size_t testCount = sizeof(s_tests) / sizeof(s_tests[0]);
    bool selected[sizeof(s_tests) / sizeof(s_tests[0])] = { false };
    for (int arg = 2; arg < argc; arg++) {
        size_t i = 0;
        while (i < testCount && strcmp(s_tests[i].name, argv[arg]) != 0)
            i++;
        if (i == testCount) {
            fprintf(stderr, "Unknown test %s\n", argv[arg]);
            return 1;
        }
        selected[i] = true;
    }
    bool passed = true;
    int run = 0;
    for (size_t i = 0; i < testCount; i++) {
        if (argc > 2 && !selected[i])
            continue;
        if (s_tests[i].content && !content) {
            printf("[%s] skipped: no game files in %s\n", s_tests[i].name, context.content);
            continue;
        }
        passed &= RunTest(&s_tests[i], &context);
        run++;
    }
    if (run == 0)
        return TEST_SKIPPED;
    return passed ? 0 : 1;
}
