#ifndef __SCNGOAL_H__
#define __SCNGOAL_H__

#include <stdbool.h>
#include <stdint.h>
#include "scenario/scnBytecode.h"
#include "scenario/scnCutscene.h"
#include "scenario/scnVm.h"

// The way of the emulation to the call that plays a cutscene slot (SCNCUTSCENE_PLAY): which functions get to it, and how
// far each instruction is from it (VM_Environment.distances), for the capture of scnCutscene.c

// The parameters of each function of the program that are the slot of a call to Cutscene: bit k, the parameter k.
// Allocated (free), NULL on memory error
uint32_t*   SCNGoal_SlotParameters(const BC_Program* program);
// The call plays the slot: a call to Cutscene with it, or to a function whose parameter that has it is a slot
bool        SCNGoal_PlaysSlot(const BC_Program* program, const SH4_Call* call, const uint32_t* parameters, uint32_t slot);
// How far each instruction of the code is from the call that plays the slot, indexed by offset (VM_DISTANCE_NONE,
// VM_DISTANCE_RETURN or a distance). Allocated (free), NULL on memory error
uint32_t*   SCNGoal_Distances(const BC_Program* program, const uint32_t* parameters, uint32_t slot);

#endif
