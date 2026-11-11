#include <stdlib.h>
#include <string.h>
#include "formats/script/scnPair.h"

// The calls that both versions make: to their own functions, to the game and to the services of the context (the yields
// and the other pointers of the context are SH-4 only)
static bool IsPaired(const SH4_Call* call) {
    return call->kind == SH4_CALL_LOCAL || call->kind == SH4_CALL_GAME || call->kind == SH4_CALL_SERVICE2C ||
        call->kind == SH4_CALL_SERVICE30;
}

// The calls of a function, as indices in calls (sorted by function, as the calls of both programs are)
static uint32_t FunctionCalls(const SH4_Call* calls, uint32_t count, uint32_t function, uint32_t* first, uint32_t* indices,
    uint32_t max) {
    uint32_t found = 0;
    for (uint32_t c = *first; c < count && calls[c].function <= function; c++) {
        if (calls[c].function < function) {
            *first = c + 1;
            continue;
        }
        if (IsPaired(&calls[c]) && found < max)
            indices[found++] = c;
    }
    return found;
}

// The same call: kind, ID, and the paired function for the local calls
static bool SameCall(const SCN_Pair* pair, const SH4_Program* sh4, const BC_Program* bytecode, const SH4_Call* a,
    const SH4_Call* b) {
    if (a->kind != b->kind)
        return false;
    if (a->kind != SH4_CALL_LOCAL)
        return a->id == b->id;
    int target = SH4_FunctionAt(sh4, a->target), other = BC_FunctionAt(bytecode, b->target);
    return target >= 0 && pair->functions[target] == other;
}

bool SCN_PairPrograms(SCN_Pair* pair, const SH4_Program* sh4, const BC_Program* bytecode) {
    memset(pair, 0, sizeof(*pair));
    if (sh4->functionCount != bytecode->functionCount + 1)
        return false;
    pair->functionCount = sh4->functionCount;
    pair->callCount = sh4->callCount;
    pair->functions = malloc((sh4->functionCount + 1) * sizeof(int32_t));
    pair->sameCalls = calloc(sh4->functionCount + 1, sizeof(bool));
    pair->calls = malloc((sh4->callCount + 1) * sizeof(int32_t));
    uint32_t max = sh4->callCount > bytecode->callCount ? sh4->callCount : bytecode->callCount;
    uint32_t* a = malloc((max + 1) * sizeof(uint32_t));
    uint32_t* b = malloc((max + 1) * sizeof(uint32_t));
    if (pair->functions == NULL || pair->sameCalls == NULL || pair->calls == NULL || a == NULL || b == NULL) {
        free(a);
        free(b);
        SCN_FreePair(pair);
        return false;
    }
    pair->functions[0] = -1;
    for (uint32_t f = 1; f < sh4->functionCount; f++)
        pair->functions[f] = (int32_t)f - 1;
    pair->pairedFunctions = sh4->functionCount - 1;
    for (uint32_t c = 0; c < sh4->callCount; c++)
        pair->calls[c] = -1;
    uint32_t firstA = 0, firstB = 0;
    for (uint32_t f = 1; f < sh4->functionCount; f++) {
        uint32_t countA = FunctionCalls(sh4->calls, sh4->callCount, f, &firstA, a, max);
        uint32_t countB = FunctionCalls(bytecode->calls, bytecode->callCount, f - 1, &firstB, b, max);
        bool same = countA == countB;
        for (uint32_t i = 0; i < countA && same; i++)
            same = SameCall(pair, sh4, bytecode, &sh4->calls[a[i]], &bytecode->calls[b[i]]);
        pair->sameCalls[f] = same;
        pair->sameFunctions += same;
        for (uint32_t i = 0; i < countA && same; i++)
            pair->calls[a[i]] = (int32_t)b[i];
    }
    free(a);
    free(b);
    return true;
}

void SCN_FreePair(SCN_Pair* pair) {
    free(pair->functions);
    free(pair->sameCalls);
    free(pair->calls);
    memset(pair, 0, sizeof(*pair));
}

const SH4_Call* SCN_PairedCall(const SCN_Pair* pair, const BC_Program* bytecode, uint32_t sh4Call) {
    if (pair->calls == NULL || sh4Call >= pair->callCount || pair->calls[sh4Call] < 0)
        return NULL;
    return &bytecode->calls[pair->calls[sh4Call]];
}
