#include <stdint.h>
#include <string.h>
#include "audio/sndDsp.h"

static uint16_t DSP_Pack(int32_t value) {
    int sign = (value >> 23) & 1;
    uint32_t temp = (uint32_t)(value ^ (value << 1)) & 0xFFFFFF;
    int exponent = 0;
    for (int k = 0; k < 12; k++) {
        if (temp & 0x800000)
            break;
        temp <<= 1;
        exponent++;
    }
    if (exponent < 12)
        value = (value << exponent) & 0x3FFFFF;
    else
        value <<= 11;
    value >>= 11;
    value &= 0x7FF;
    value |= sign << 15;
    value |= exponent << 11;
    return (uint16_t)value;
}

static int32_t DSP_Unpack(uint16_t value) {
    int sign = (value >> 15) & 1;
    int exponent = (value >> 11) & 0xF;
    int32_t result = (value & 0x7FF) << 11;
    if (exponent > 11) {
        exponent = 11;
        result |= sign << 22;
    }
    else
        result |= (sign ^ 1) << 22;
    result |= sign << 23;
    result = (int32_t)((uint32_t)result << 8) >> 8;
    return result >> exponent;
}

static int32_t SignExtend24(int32_t value) {
    return (int32_t)((uint32_t)value << 8) >> 8;
}

static int32_t Clamp24(int64_t value) {
    if (value > 0x7FFFFF) return 0x7FFFFF;
    if (value < -0x800000) return -0x800000;
    return (int32_t)value;
}

void DSP_Init(DSP* dsp, const SND_DSP* program) {
    for (int step = 0; step < 128; step++) {
        if (program->mpro[step][0] | program->mpro[step][1] | program->mpro[step][2] | program->mpro[step][3])
            dsp->stepCount = step + 1;
    }
}

void DSP_Step(DSP* dsp, const SND_DSP* program) {
    memset(dsp->efreg, 0, sizeof(dsp->efreg));
    for (int step = 0; step < dsp->stepCount; step++) {
        const uint16_t* ins = program->mpro[step];
        uint32_t tra = (ins[0] >> 9) & 0x7F;
        uint32_t twt = (ins[0] >> 8) & 0x01;
        uint32_t twa = (ins[0] >> 1) & 0x7F;
        uint32_t xsel = (ins[1] >> 15) & 0x01;
        uint32_t ysel = (ins[1] >> 13) & 0x03;
        uint32_t ira = (ins[1] >> 7) & 0x3F;
        uint32_t iwt = (ins[1] >> 6) & 0x01;
        uint32_t iwa = (ins[1] >> 1) & 0x1F;
        uint32_t table = (ins[2] >> 15) & 0x01;
        uint32_t mwt = (ins[2] >> 14) & 0x01;
        uint32_t mrd = (ins[2] >> 13) & 0x01;
        uint32_t ewt = (ins[2] >> 12) & 0x01;
        uint32_t ewa = (ins[2] >> 8) & 0x0F;
        uint32_t adrl = (ins[2] >> 7) & 0x01;
        uint32_t frcl = (ins[2] >> 6) & 0x01;
        uint32_t shift = (ins[2] >> 4) & 0x03;
        uint32_t yrl = (ins[2] >> 3) & 0x01;
        uint32_t negb = (ins[2] >> 2) & 0x01;
        uint32_t zero = (ins[2] >> 1) & 0x01;
        uint32_t bsel = (ins[2] >> 0) & 0x01;
        uint32_t nofl = (ins[3] >> 15) & 0x01;
        uint32_t masa = (ins[3] >> 9) & 0x1F;
        uint32_t adreb = (ins[3] >> 8) & 0x01;
        uint32_t nxadr = (ins[3] >> 7) & 0x01;

        // Input: the memory registers, the mixed voices (20 bits) or the external inputs (silent)
        int32_t inputs;
        if (ira <= 0x1F)
            inputs = dsp->mems[ira];
        else if (ira <= 0x2F)
            inputs = dsp->mixs[ira - 0x20] << 4;
        else
            inputs = 0;
        inputs = SignExtend24(inputs);
        if (iwt) {
            dsp->mems[iwa] = dsp->memVal;
            if (ira == iwa)
                inputs = dsp->memVal;
        }

        int32_t b = 0;
        if (!zero) {
            b = bsel ? dsp->acc : SignExtend24(dsp->temp[(tra + dsp->dec) & 0x7F]);
            if (negb)
                b = -b;
        }
        int32_t x = xsel ? inputs : SignExtend24(dsp->temp[(tra + dsp->dec) & 0x7F]);
        int32_t y;
        if (ysel == 0)
            y = dsp->frcReg;
        else if (ysel == 1)
            y = ((int16_t)program->coef[step]) >> 3; // The coefficient of each step
        else if (ysel == 2)
            y = (dsp->yReg >> 11) & 0x1FFF;
        else
            y = (dsp->yReg >> 4) & 0x0FFF;
        if (yrl)
            dsp->yReg = inputs;

        int32_t shifted;
        if (shift == 0)
            shifted = Clamp24(dsp->acc);
        else if (shift == 1)
            shifted = Clamp24((int64_t)dsp->acc * 2);
        else if (shift == 2)
            shifted = SignExtend24(dsp->acc * 2);
        else
            shifted = SignExtend24(dsp->acc);

        y = (int32_t)((uint32_t)y << 19) >> 19; // 13 bits
        dsp->acc = (int32_t)(((int64_t)x * y) >> 12) + b;

        if (twt)
            dsp->temp[(twa + dsp->dec) & 0x7F] = shifted;
        if (frcl)
            dsp->frcReg = shift == 3 ? shifted & 0x0FFF : (shifted >> 11) & 0x1FFF;

        if (mrd || mwt) {
            uint32_t address = program->madrs[masa];
            if (!table)
                address += dsp->dec;
            if (adreb)
                address += dsp->adrsReg & 0x0FFF;
            if (nxadr)
                address++;
            address &= DSP_RAM_MASK;
            // The memory is accessed on the odd steps only
            if (mrd && (step & 1))
                dsp->memVal = nofl ? dsp->ram[address] << 8 : DSP_Unpack(dsp->ram[address]);
            if (mwt && (step & 1))
                dsp->ram[address] = nofl ? (uint16_t)(shifted >> 8) : DSP_Pack(shifted);
        }

        if (adrl)
            dsp->adrsReg = shift == 3 ? (shifted >> 12) & 0x0FFF : inputs >> 16;
        if (ewt)
            dsp->efreg[ewa] += shifted >> 8;
    }
    dsp->dec--;
    memset(dsp->mixs, 0, sizeof(dsp->mixs));
}
