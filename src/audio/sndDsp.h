#ifndef __SNDDSP_H__
#define __SNDDSP_H__

#include <stdint.h>
#include "formats/sound/snd.h"

// DSP: the effect processor of the AICA, running the program of the bank once per output sample

#define DSP_RAM_MASK        0xFFFF

typedef struct {
    int32_t     temp[128];
    int32_t     mems[32];
    int32_t     mixs[16];
    int32_t     efreg[16];
    int32_t     acc;
    int32_t     frcReg;
    int32_t     yReg;
    int32_t     adrsReg;
    int32_t     memVal;
    uint32_t    dec;
    int         stepCount;
    uint16_t    ram[DSP_RAM_MASK + 1];
} DSP;

// Sets the number of steps of the program: it ends after its last step that isn't 0
void DSP_Init(DSP* dsp, const SND_DSP* program);
// Runs the program for one sample: reads the voices mixed in mixs, writes the effect outputs in efreg and clears mixs
void DSP_Step(DSP* dsp, const SND_DSP* program);

#endif
