#include <stdlib.h>
#include <string.h>
#include "scenario/scnGoal.h"

// The parameters of each function of the program that are the slot of a call to Cutscene: bit k, the parameter k is
// the argument 0 of a call to Cutscene, or an argument of a call to a function whose parameter is. NULL on memory error
uint32_t* SCNGoal_SlotParameters(const BC_Program* program) {
    uint32_t* parameters = calloc(program->functionCount + 1, sizeof(uint32_t));
    if (parameters == NULL)
        return NULL;
    for (bool changed = true; changed;) {
        changed = false;
        for (uint32_t c = 0; c < program->callCount; c++) {
            const SH4_Call* call = &program->calls[c];
            int function = BC_FunctionAt(program, call->address);
            if (function < 0)
                continue;
            uint32_t bits = 0;
            if (call->kind == SH4_CALL_GAME && call->id == SCNCUTSCENE_PLAY && call->argumentCount >= 1 &&
                call->arguments[0].kind == SH4_VALUE_PARAMETER && call->arguments[0].value < 32)
                bits = 1u << call->arguments[0].value;
            int callee = call->kind == SH4_CALL_LOCAL ? BC_FunctionAt(program, call->target) : -1;
            for (uint32_t a = 0; callee >= 0 && a < call->argumentCount && a < 32; a++) {
                const SH4_Value* argument = &call->arguments[a];
                if ((parameters[callee] >> a & 1) && argument->kind == SH4_VALUE_PARAMETER && argument->value < 32)
                    bits |= 1u << argument->value;
            }
            if ((parameters[function] | bits) != parameters[function]) {
                parameters[function] |= bits;
                changed = true;
            }
        }
    }
    return parameters;
}

// The call plays the slot: a call to Cutscene with it, or to a function whose parameter that has it is a slot
bool SCNGoal_PlaysSlot(const BC_Program* program, const SH4_Call* call, const uint32_t* parameters, uint32_t slot) {
    if (call->kind == SH4_CALL_GAME)
        return call->id == SCNCUTSCENE_PLAY && call->argumentCount >= 1 && call->arguments[0].kind == SH4_VALUE_CONSTANT &&
            call->arguments[0].value == slot;
    int callee = call->kind == SH4_CALL_LOCAL ? BC_FunctionAt(program, call->target) : -1;
    for (uint32_t a = 0; callee >= 0 && a < call->argumentCount && a < 32; a++) {
        if ((parameters[callee] >> a & 1) && call->arguments[a].kind == SH4_VALUE_CONSTANT && call->arguments[a].value == slot)
            return true;
    }
    return false;
}

// The call gives the slot as a constant to a function of the program
static bool GivesSlot(const SH4_Call* call, uint32_t slot) {
    for (uint32_t a = 0; call->kind == SH4_CALL_LOCAL && a < call->argumentCount; a++) {
        if (call->arguments[a].kind == SH4_VALUE_CONSTANT && call->arguments[a].value == slot)
            return true;
    }
    return false;
}

// The function of the program called or started as a thread by a call, -1 if none
static int CalledFunction(const BC_Program* program, const SH4_Call* call) {
    if (call->kind == SH4_CALL_LOCAL)
        return BC_FunctionAt(program, call->target);
    if (call->kind == SH4_CALL_GAME && call->id == VM_START_THREAD && call->argumentCount >= 1 &&
        call->arguments[0].kind == SH4_VALUE_CONSTANT && call->arguments[0].value < program->codeEnd)
        return BC_FunctionAt(program, call->arguments[0].value);
    return -1;
}

// The call gives constants to a function of the program (the function of a thread excepted)
static bool GivesConstants(const SH4_Call* call) {
    for (uint32_t a = 0; call->kind == SH4_CALL_LOCAL && a < call->argumentCount; a++) {
        if (call->arguments[a].kind == SH4_VALUE_CONSTANT)
            return true;
    }
    return false;
}

// The goals of a call: 2 if it plays the slot (a constant, SCNGoal_PlaysSlot), gives it as a constant to a function of the
// program that gets to a call that plays it, or calls (or starts) without constants a function with a call of goal 2
// (strong), 1 if it calls another function that gets to a call that plays it (e.g. with other constants, which choose
// other cutscenes: func(23) of the other vending machine of scene/01/d000 to the function that plays its cutscenes, the
// cutscene 22 with func(22)), else 0
static int CallGoal(const BC_Program* program, const SH4_Call* call, const uint32_t* parameters, const bool* reaches,
    const bool* strong, uint32_t slot) {
    if (SCNGoal_PlaysSlot(program, call, parameters, slot))
        return 2;
    int callee = CalledFunction(program, call);
    if (callee < 0 || !reaches[callee])
        return 0;
    return GivesSlot(call, slot) || (strong != NULL && strong[callee] && !GivesConstants(call)) ? 2 : 1;
}

// The instructions of a function as a graph, to go back from some of them: the predecessors of each one, with the cost
// of running it (1, plus the cost of the function it calls)
typedef struct {
    uint32_t    start;
    uint32_t    end;
    uint32_t*   heads;      // Per instruction (offset - start): first link, UINT32_MAX if none
    uint32_t*   links;      // Pairs: predecessor, next link
    uint32_t    linkCount;
    uint32_t*   callees;    // Per instruction: the function it calls, UINT32_MAX if none
    uint8_t*    sources;    // Per instruction: an instruction to go back from (distance 0)
    bool*       valid;      // Per instruction: the start of one
} FunctionGraph;

#define GOAL_FAR    0x10000000u     // Cost of a call to a function that doesn't return

// Builds the graph of a function: its instructions from its start, the branches within it
static bool BuildGraph(const BC_Program* program, uint32_t function, FunctionGraph* graph) {
    uint32_t start = program->functions[function], end = BC_FunctionEnd(program, function);
    if (end > program->codeEnd)
        end = program->codeEnd;
    uint32_t count = end > start ? end - start : 0;
    graph->start = start;
    graph->end = start + count;
    graph->linkCount = 0;
    graph->heads = malloc((count + 1) * sizeof(uint32_t));
    graph->links = malloc((count * 4 + 2) * sizeof(uint32_t));
    graph->callees = malloc((count + 1) * sizeof(uint32_t));
    graph->sources = calloc(count + 1, 1);
    graph->valid = calloc(count + 1, sizeof(bool));
    if (graph->heads == NULL || graph->links == NULL || graph->callees == NULL || graph->sources == NULL || graph->valid == NULL)
        return false;
    for (uint32_t i = 0; i < count; i++) {
        graph->heads[i] = UINT32_MAX;
        graph->callees[i] = UINT32_MAX;
    }
    for (uint32_t pc = start; pc < graph->end;) {
        BC_Instruction ins;
        if (!BC_Decode(program->scn->data, program->codeEnd, pc, &ins) || ins.size == 0)
            break;
        uint32_t next = pc + ins.size;
        graph->valid[pc - start] = true;
        if (ins.kind == BC_CALL) {
            int callee = BC_FunctionAt(program, ins.target);
            if (callee >= 0)
                graph->callees[pc - start] = (uint32_t)callee;
        }
        uint32_t successors[2];
        uint32_t successorCount = 0;
        if (ins.kind == BC_BRANCH || ins.kind == BC_JUMP)
            successors[successorCount++] = ins.target;
        else if (ins.kind == BC_BRANCH_ZERO) {
            successors[successorCount++] = next;
            successors[successorCount++] = ins.target;
        } else if (ins.kind != BC_RET)
            successors[successorCount++] = next;
        for (uint32_t s = 0; s < successorCount; s++) {
            uint32_t to = successors[s];
            if (to < start || to >= graph->end)
                continue;
            graph->links[graph->linkCount * 2] = pc;
            graph->links[graph->linkCount * 2 + 1] = graph->heads[to - start];
            graph->heads[to - start] = graph->linkCount++;
        }
        pc = next;
    }
    return true;
}

static void FreeGraph(FunctionGraph* graph) {
    free(graph->heads);
    free(graph->links);
    free(graph->callees);
    free(graph->sources);
    free(graph->valid);
    memset(graph, 0, sizeof(*graph));
}

// A binary heap of (distance, offset)
typedef struct {
    uint64_t*   items;      // distance << 32 | offset
    uint32_t    count;
} Heap;

static void HeapPush(Heap* heap, uint32_t distance, uint32_t offset) {
    uint32_t i = heap->count++;
    heap->items[i] = (uint64_t)distance << 32 | offset;
    while (i > 0 && heap->items[(i - 1) / 2] > heap->items[i]) {
        uint64_t item = heap->items[i];
        heap->items[i] = heap->items[(i - 1) / 2];
        heap->items[(i - 1) / 2] = item;
        i = (i - 1) / 2;
    }
}

static uint64_t HeapPop(Heap* heap) {
    uint64_t top = heap->items[0];
    heap->items[0] = heap->items[--heap->count];
    for (uint32_t i = 0;;) {
        uint32_t smallest = i, left = i * 2 + 1, right = i * 2 + 2;
        if (left < heap->count && heap->items[left] < heap->items[smallest])
            smallest = left;
        if (right < heap->count && heap->items[right] < heap->items[smallest])
            smallest = right;
        if (smallest == i)
            break;
        uint64_t item = heap->items[i];
        heap->items[i] = heap->items[smallest];
        heap->items[smallest] = item;
        i = smallest;
    }
    return top;
}

// The distances of the instructions of a function back from its sources (written to distances, indexed by offset),
// each instruction costing 1 plus the cost of the function it calls. heap: room for the links of the function
static void GoBack(const FunctionGraph* graph, const uint32_t* costs, Heap* heap, uint32_t* distances) {
    heap->count = 0;
    for (uint32_t pc = graph->start; pc < graph->end; pc++) {
        distances[pc] = VM_DISTANCE_NONE;
        if (graph->valid[pc - graph->start] && graph->sources[pc - graph->start]) {
            distances[pc] = 0;
            HeapPush(heap, 0, pc);
        }
    }
    while (heap->count > 0) {
        uint64_t item = HeapPop(heap);
        uint32_t distance = (uint32_t)(item >> 32), pc = (uint32_t)item;
        if (distance != distances[pc])
            continue;
        for (uint32_t l = graph->heads[pc - graph->start]; l != UINT32_MAX; l = graph->links[l * 2 + 1]) {
            uint32_t from = graph->links[l * 2];
            uint32_t callee = graph->callees[from - graph->start];
            uint32_t cost = 1 + (callee != UINT32_MAX ? costs[callee] : 0);
            uint32_t total = distance + cost < distance || distance + cost >= VM_DISTANCE_RETURN ? VM_DISTANCE_RETURN - 1 :
                distance + cost;
            if (total < distances[from]) {
                distances[from] = total;
                HeapPush(heap, total, from);
            }
        }
    }
}

// For the capture, how far each instruction of the code is from the call that plays the slot (VM_Environment.distances):
// in the functions that get to it (that play it, or call or start a function that does), the instructions run to their
// nearest call on the way (the calls of goal 2 if the function has some: e.g. in the loop of the map of scene/01/d000, the
// call to the event of the vending machine that gives 22 to the function that plays its cutscenes, rather than the
// event of the other machine, which gives it other slots); in the other ones, to their return, which goes back to the
// way. A call to a function of the program counts the instructions from its start to its
// nearest return. NULL on memory error
uint32_t* SCNGoal_Distances(const BC_Program* program, const uint32_t* parameters, uint32_t slot) {
    uint32_t functionCount = program->functionCount, size = program->codeEnd;
    bool* reaches = calloc(functionCount + 1, sizeof(bool));
    bool* strong = calloc(functionCount + 1, sizeof(bool));  // Have a call of goal 2
    uint8_t* goals = calloc(size + 1, 1);          // Of the instructions of the calls
    uint32_t* distances = malloc((size + 1) * sizeof(uint32_t));
    uint32_t* costs = malloc((functionCount + 1) * sizeof(uint32_t));
    FunctionGraph* graphs = calloc(functionCount + 1, sizeof(FunctionGraph));
    Heap heap = { malloc((size * 4 + 4) * sizeof(uint64_t)), 0 };
    bool ok = reaches != NULL && strong != NULL && goals != NULL && distances != NULL && costs != NULL && graphs != NULL &&
        heap.items != NULL;
    // The functions that get to the call: those that play the slot, then those that call or start one of them
    for (uint32_t c = 0; ok && c < program->callCount; c++) {
        int function = BC_FunctionAt(program, program->calls[c].address);
        if (function >= 0 && CallGoal(program, &program->calls[c], parameters, reaches, NULL, slot) == 2)
            reaches[function] = true;
    }
    for (bool changed = ok; changed;) {
        changed = false;
        for (uint32_t c = 0; c < program->callCount; c++) {
            int function = BC_FunctionAt(program, program->calls[c].address);
            int callee = CalledFunction(program, &program->calls[c]);
            if (function >= 0 && callee >= 0 && reaches[callee] && !reaches[function]) {
                reaches[function] = true;
                changed = true;
            }
        }
    }
    for (bool changed = ok; changed;) {
        changed = false;
        for (uint32_t c = 0; c < program->callCount; c++) {
            int function = BC_FunctionAt(program, program->calls[c].address);
            if (function >= 0 && !strong[function] && CallGoal(program, &program->calls[c], parameters, reaches, strong, slot) == 2) {
                strong[function] = true;
                changed = true;
            }
        }
    }
    for (uint32_t c = 0; ok && c < program->callCount; c++) {
        const SH4_Call* call = &program->calls[c];
        if (call->address < size)
            goals[call->address] = (uint8_t)CallGoal(program, call, parameters, reaches, strong, slot);
    }
    // In the functions whose parameter is the slot (e.g. func(slot) of scene/01/d000, that plays the cutscenes 16 and
    // 17 of the bus), the calls that pass it on: to Cutscene, or to another such function
    for (uint32_t c = 0; ok && c < program->callCount; c++) {
        const SH4_Call* call = &program->calls[c];
        int function = BC_FunctionAt(program, call->address);
        if (function < 0 || parameters[function] == 0 || call->address >= size || goals[call->address] != 0)
            continue;
        int callee = call->kind == SH4_CALL_LOCAL ? BC_FunctionAt(program, call->target) : -1;
        for (uint32_t a = 0; a < call->argumentCount && a < 32; a++) {
            const SH4_Value* argument = &call->arguments[a];
            bool slotArgument = argument->kind == SH4_VALUE_PARAMETER && argument->value < 32 &&
                (parameters[function] >> argument->value & 1);
            if (slotArgument && ((call->kind == SH4_CALL_GAME && call->id == SCNCUTSCENE_PLAY && a == 0) ||
                (callee >= 0 && (parameters[callee] >> a & 1))))
                goals[call->address] = 3;
        }
    }
    for (uint32_t f = 0; ok && f < functionCount; f++) {
        ok = BuildGraph(program, f, &graphs[f]);
        costs[f] = GOAL_FAR;
    }
    // The cost of each function, from its start to its nearest return, with the costs of the functions it calls
    for (uint32_t f = 0; ok && f < functionCount; f++) {
        FunctionGraph* graph = &graphs[f];
        for (uint32_t pc = graph->start; pc < graph->end; pc++) {
            BC_Instruction ins;
            graph->sources[pc - graph->start] = graph->valid[pc - graph->start] &&
                BC_Decode(program->scn->data, program->codeEnd, pc, &ins) && ins.kind == BC_RET;
        }
    }
    for (uint32_t pass = 0, changed = 1; ok && changed && pass < 16; pass++) {
        changed = 0;
        for (uint32_t f = 0; f < functionCount; f++) {
            GoBack(&graphs[f], costs, &heap, distances);
            uint32_t cost = graphs[f].end > graphs[f].start && distances[graphs[f].start] < GOAL_FAR ?
                distances[graphs[f].start] : GOAL_FAR;
            if (cost != costs[f]) {
                costs[f] = cost;
                changed = 1;
            }
        }
    }
    // Then the distances to the calls on the way in the functions that get to the call. The others go back to the way
    // when they return, whatever the way: their branches are followed in the usual order (the value they return can be
    // the one that leads to the call, e.g. 17 for the vending machine of scene/01/d000 from the function that looks
    // where Ryo is), and they aren't left
    for (uint32_t f = 0; ok && f < functionCount; f++) {
        FunctionGraph* graph = &graphs[f];
        if (!reaches[f] && parameters[f] == 0) {
            for (uint32_t pc = graph->start; pc < graph->end; pc++)
                distances[pc] = VM_DISTANCE_RETURN;
            continue;
        }
        uint8_t level = 1;  // The calls with the slot first, if the function has some
        for (uint32_t pc = graph->start; pc < graph->end; pc++)
            level = goals[pc] >= 2 ? 2 : level;
        for (uint32_t pc = graph->start; pc < graph->end; pc++)
            graph->sources[pc - graph->start] = goals[pc] >= level;
        GoBack(graph, costs, &heap, distances);
    }
    for (uint32_t f = 0; graphs != NULL && f < functionCount; f++)
        FreeGraph(&graphs[f]);
    free(graphs);
    free(heap.items);
    free(reaches);
    free(strong);
    free(goals);
    free(costs);
    if (!ok) {
        free(distances);
        return NULL;
    }
    return distances;
}
