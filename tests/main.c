#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "corpus.h"
#include "test.h"

// ShenmueTests <content folder> [test...]: runs the given tests, or all of them. Returns 0 if they pass, 1 if one
// failed, and TEST_SKIPPED (the SKIP_RETURN_CODE of CTest) if the content folder isn't there

#define TEST_SKIPPED 77

void Test_MapinfoChunks(const TestContext* context);
void Test_Scn3Chunks(const TestContext* context);
void Test_ScnFiles(const TestContext* context);
void Test_ScnPairs(const TestContext* context);

static const Test s_tests[] = {
    { "mapinfo_chunks", Test_MapinfoChunks },
    { "scn3_chunks",    Test_Scn3Chunks },
    { "scn_files",      Test_ScnFiles },
    { "scn_pairs",      Test_ScnPairs },
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
    if (!Corpus_Exists(context.content)) {
        printf("Skipped: no game files in %s\n", context.content);
        return TEST_SKIPPED;
    }
    bool passed = true;
    if (argc == 2) {
        for (size_t i = 0; i < sizeof(s_tests) / sizeof(s_tests[0]); i++)
            passed &= RunTest(&s_tests[i], &context);
    }
    for (int arg = 2; arg < argc; arg++) {
        const Test* test = NULL;
        for (size_t i = 0; i < sizeof(s_tests) / sizeof(s_tests[0]); i++) {
            if (strcmp(s_tests[i].name, argv[arg]) == 0)
                test = &s_tests[i];
        }
        if (test == NULL) {
            fprintf(stderr, "Unknown test %s\n", argv[arg]);
            return 1;
        }
        passed &= RunTest(test, &context);
    }
    return passed ? 0 : 1;
}
