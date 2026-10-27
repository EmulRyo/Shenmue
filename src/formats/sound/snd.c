#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "core/bytes.h"
#include "core/file.h"
#include "formats/sound/snd.h"

#define SND_HEADER_SIZE     0x60
#define SND_SECTION_COUNT   8
#define SND_SECTION_PARTS       0
#define SND_SECTION_PROGRAMS    1
#define SND_SECTION_CURVES      2
#define SND_SECTION_SEQUENCES   3
#define SND_SECTION_EFFECTS     4
#define SND_SECTION_DSP         6
#define SND_SECTION_SAMPLES     7
#define SND_SAMPLE_ENTRY_SIZE   0x10
#define SND_PART_SIZE           0x20
#define SND_LAYER_SIZE          0x40
#define SND_SPLIT_SIZE          0x40
#define SND_DSP_SIZE            0xC28
#define SND_MAX_PLAYLIST        64

// True if the range [offset, offset + length) is inside the file
static bool InFile(const SND_Bank* bank, size_t offset, size_t length) {
    return offset <= bank->size && length <= bank->size - offset;
}

uint32_t SND_ReadU32(const SND_Bank* bank, uint32_t pos) {
    return InFile(bank, pos, 4) ? Bytes_ReadU32(bank->data + pos) : 0;
}

// Decodes the 4-bit ADPCM of the AICA: the low nibble first, the step adapts with the 3 low bits of each nibble
static void DecodeADPCM(const uint8_t* data, uint32_t size, int16_t* output) {
    static const int scale[8] = { 0x0E6, 0x0E6, 0x0E6, 0x0E6, 0x133, 0x199, 0x200, 0x266 };
    int signal = 0;
    int step = 0x7F;
    for (uint32_t i = 0; i < size * 2; i++) {
        int nibble = (data[i / 2] >> ((i & 1) * 4)) & 0x0F;
        int diff = (((nibble & 7) * 2 + 1) * step) >> 3;
        signal += nibble & 8 ? -diff : diff;
        if (signal > 32767) signal = 32767;
        if (signal < -32768) signal = -32768;
        step = (step * scale[nibble & 7]) >> 8;
        if (step < 0x7F) step = 0x7F;
        if (step > 0x6000) step = 0x6000;
        output[i] = (int16_t)signal;
    }
}

// Reads an entry of the sample table and decodes its data. The data is 16-bit PCM, whatever the format given by the
// offset field, except in the samples whose loop end is about twice the size: those are ADPCM
static bool ReadSample(const uint8_t* buffer, size_t size, const uint8_t* entry, SND_Sample* sample) {
    uint32_t offsetField = Bytes_ReadU32(entry);
    uint32_t loopStart = Bytes_ReadU16(entry + 4);
    uint32_t loopEnd = Bytes_ReadU16(entry + 6);
    memset(sample, 0, sizeof(*sample));
    sample->offset = offsetField & 0x7FFFFF;
    sample->size = Bytes_ReadU32(entry + 12);
    sample->loop = (offsetField >> 25) & 1;
    // Half sample rate: the data has been doubled, but the loop points count the samples at half rate
    sample->halfRate = Bytes_ReadU32(entry + 8) == 0x80;
    if (sample->offset > size || sample->size > size - sample->offset)
        return false;

    if (!sample->halfRate && loopEnd >= sample->size + sample->size / 2) {
        sample->encoding = SND_ENCODING_ADPCM;
        sample->frameCount = sample->size * 2;
    }
    else {
        sample->encoding = SND_ENCODING_PCM16;
        sample->frameCount = sample->size / 2;
    }
    sample->pcm = malloc(((size_t)sample->frameCount + 1) * sizeof(int16_t));
    if (sample->pcm == NULL)
        return false;
    const uint8_t* data = buffer + sample->offset;
    if (sample->encoding == SND_ENCODING_ADPCM)
        DecodeADPCM(data, sample->size, sample->pcm);
    else {
        for (uint32_t i = 0; i < sample->frameCount; i++)
            sample->pcm[i] = (int16_t)Bytes_ReadU16(data + (size_t)i * 2);
    }

    if (sample->halfRate) {
        loopStart *= 2;
        loopEnd *= 2;
    }
    if (loopEnd > sample->frameCount)
        loopEnd = sample->frameCount;
    if (loopStart >= loopEnd)
        sample->loop = false;
    sample->loopStart = loopStart;
    sample->loopEnd = loopEnd;
    if (sample->loop) {
        int16_t min = INT16_MAX, max = INT16_MIN;
        for (uint32_t i = loopStart; i < loopEnd; i++) {
            if (sample->pcm[i] < min) min = sample->pcm[i];
            if (sample->pcm[i] > max) max = sample->pcm[i];
        }
        sample->silentLoop = max - min < 64;
        sample->tailLoop = loopEnd - loopStart <= 64 && sample->frameCount - loopEnd < 16;
    }
    return true;
}

static void ReadSplit(const uint8_t* p, SND_Split* split) {
    uint16_t envelope1 = Bytes_ReadU16(p + 0x0C);
    uint16_t envelope2 = Bytes_ReadU16(p + 0x0E);
    split->highKey = p[0x00];
    split->sample = p[0x02];
    split->pan = p[0x06] & 0x80 ? SND_UNSET : p[0x06] & 0x1F;
    split->level = p[0x07] & 0x80 ? SND_UNSET : p[0x07] & 0x0F;
    split->velocityLevel = p[0x08] >> 1;
    split->transpose = (int8_t)p[0x0A];
    split->fine = (int8_t)p[0x0B];
    split->detune = p[0x1C];
    split->lowVelocity = p[0x20];
    split->highVelocity = p[0x21];
    split->attackRate = envelope1 & 0x1F;
    split->decay1Rate = (envelope1 >> 6) & 0x1F;
    split->decay2Rate = (envelope1 >> 11) & 0x1F;
    split->releaseRate = envelope2 & 0x1F;
    split->decayLevel = (envelope2 >> 5) & 0x1F;
    split->keyRateScaling = (envelope2 >> 10) & 0x0F;
    // Filter: 5 words with a level (FLV, bits 0-10, 2 bits left out) and a rate (bits 11-15): FAR, FD1R, FD2R, FRR and Q.
    // The filter is off when the first level is 0
    for (int i = 0; i < 5; i++) {
        uint16_t word = Bytes_ReadU16(p + 0x10 + i * 2);
        split->filterLevels[i] = (uint16_t)((word & 0x7FF) << 2);
        if (i < 4)
            split->filterRates[i] = (uint8_t)(word >> 11);
        else
            split->resonance = (uint8_t)(word >> 11);
    }
    split->filter = split->filterLevels[0] != 0;
    split->sendSet = (p[0x1E] & 0x80) == 0;
    split->send = p[0x1F];
    split->lfo = Bytes_ReadU16(p + 0x30);
}

// Reads the layers of the program at p (the effect program when effects is true). Bit 0 of the flags: the layers have a
// key map (low key, high key and the offset of the split of each key) between the layer header and the splits
static bool ReadProgram(SND_Bank* bank, uint32_t p, SND_Program* program, bool effects) {
    if (!InFile(bank, p, 0x10))
        return false;
    uint32_t flags = Bytes_ReadU32(bank->data + p);
    for (int i = 0; i < SND_MAX_LAYERS; i++) {
        uint16_t offset = Bytes_ReadU16(bank->data + p + 8 + i * 2);
        if (offset == 0)
            continue;
        uint32_t l = p + offset;
        if (!InFile(bank, l, SND_LAYER_SIZE))
            return false;
        const uint8_t* header = bank->data + l;
        SND_Layer* layer = &program->layers[program->layerCount];
        layer->splitCount = header[0] + 1u;
        layer->curve = header[0x06];
        layer->bendUp = header[0x21];
        layer->bendDown = header[0x22];
        layer->panPattern = header[0x28] & 0x1F;
        for (int key = 0; key < 128; key++)
            layer->keyMap[key] = -1;

        uint32_t splits = l + SND_LAYER_SIZE;
        uint8_t mapLow = 0, mapHigh = 0;
        if (flags & 1) {
            if (!InFile(bank, splits, 2))
                return false;
            mapLow = bank->data[splits];
            mapHigh = bank->data[splits + 1];
            if (mapHigh < mapLow || mapHigh > 127)
                return false;
            splits += (2 + 2 * (mapHigh - mapLow + 1) + 3) & ~3u;
        }
        if (!InFile(bank, splits, (size_t)layer->splitCount * SND_SPLIT_SIZE))
            return false;
        layer->splits = calloc(layer->splitCount, sizeof(SND_Split));
        if (layer->splits == NULL)
            return false;
        program->layerCount++;
        for (uint32_t s = 0; s < layer->splitCount; s++)
            ReadSplit(bank->data + splits + s * SND_SPLIT_SIZE, &layer->splits[s]);

        if (flags & 1) {
            for (int key = mapLow; key <= mapHigh; key++) {
                uint16_t splitOffset = Bytes_ReadU16(bank->data + l + SND_LAYER_SIZE + 2 + (key - mapLow) * 2);
                if (splitOffset == 0 || l + splitOffset < splits)
                    continue;
                uint32_t index = (l + splitOffset - splits) / SND_SPLIT_SIZE;
                if (index < layer->splitCount)
                    layer->keyMap[key] = (int16_t)index;
            }
        }
        else {
            // A key plays the first split whose high key is above or equal to it
            for (int key = 0; key < 128; key++) {
                for (uint32_t s = 0; s < layer->splitCount && layer->keyMap[key] < 0; s++) {
                    if (layer->splits[s].highKey >= key)
                        layer->keyMap[key] = (int16_t)s;
                }
            }
        }
        // The splits of a key map (one key each) and of the effects play at the rate of their recording, and have their
        // own velocity range. The splits of the other programs take the velocity range of the layer
        for (uint32_t s = 0; s < layer->splitCount; s++) {
            SND_Split* split = &layer->splits[s];
            split->fixedPitch = (flags & 1) || effects;
            if (!split->fixedPitch) {
                split->lowVelocity = header[0x02];
                split->highVelocity = header[0x03];
            }
        }
    }
    return true;
}

// The program bank is a list of banks (uint16 last index + offsets), each one a list of programs. Only the first bank
// is used by the files
static void ReadPrograms(SND_Bank* bank, uint32_t section) {
    if (section == 0 || !InFile(bank, section, 4))
        return;
    uint32_t list = section + Bytes_ReadU16(bank->data + section + 2);
    if (!InFile(bank, list, 2))
        return;
    uint32_t count = Bytes_ReadU16(bank->data + list) + 1u;
    if (!InFile(bank, list + 2, (size_t)count * 2))
        return;
    bank->programs = calloc(count, sizeof(SND_Program));
    if (bank->programs == NULL)
        return;
    bank->programCount = count;
    for (uint32_t i = 0; i < count; i++)
        ReadProgram(bank, list + Bytes_ReadU16(bank->data + list + 2 + i * 2), &bank->programs[i], false);
}

// The part table is a list of blocks: settings (0x28 bytes) and lists of parts. Only the first list is used: it is the
// only one in almost all the files
static void ReadParts(SND_Bank* bank, uint32_t section) {
    if (section == 0 || !InFile(bank, section, 2))
        return;
    uint32_t blockCount = Bytes_ReadU16(bank->data + section) + 1u;
    if (blockCount < 2 || !InFile(bank, section + 2, (size_t)blockCount * 2))
        return;
    uint32_t list = section + Bytes_ReadU16(bank->data + section + 4);
    if (!InFile(bank, list, 8))
        return;
    uint32_t count = bank->data[list] + 1u;
    if (!InFile(bank, list + 8, (size_t)count * SND_PART_SIZE))
        return;
    bank->parts = calloc(count, sizeof(SND_Part));
    if (bank->parts == NULL)
        return;
    bank->partCount = count;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t* p = bank->data + list + 8 + i * SND_PART_SIZE;
        SND_Part* part = &bank->parts[i];
        part->channel = p[0x01] & 0x0F;
        part->program = p[0x03];
        part->send = p[0x08];
        part->level = p[0x09] & 0x0F;
        part->volume = p[0x0A] & 0x7F;
        part->pan = p[0x0B] & 0x1F;
        part->transpose = (int8_t)(p[0x0C] << 1) >> 1; // 7 bits
        part->fine = (int8_t)(p[0x0D] << 1) >> 1;
    }
}

static void ReadDSP(SND_Bank* bank, uint32_t section) {
    if (section == 0 || !InFile(bank, section, SND_DSP_SIZE))
        return;
    const uint8_t* p = bank->data + section;
    SND_DSP* dsp = &bank->dsp;
    for (int i = 0; i < 18; i++)
        dsp->outputs[i] = Bytes_ReadU16(p + 0x04 + i * 2);
    for (int i = 0; i < 128; i++)
        dsp->coef[i] = (uint16_t)Bytes_ReadU32(p + 0x28 + i * 4);
    for (int i = 0; i < 64; i++)
        dsp->madrs[i] = (uint16_t)Bytes_ReadU32(p + 0x228 + i * 4);
    for (int i = 0; i < 128; i++) {
        for (int j = 0; j < 4; j++)
            dsp->mpro[i][j] = (uint16_t)Bytes_ReadU32(p + 0x328 + (i * 4 + j) * 4);
    }
    dsp->present = true;
}

uint32_t SND_ReadEvent(const SND_Bank* bank, uint32_t pos, bool effect, uint8_t* runningStatus, SND_Event* event) {
    memset(event, 0, sizeof(*event));
    if (!InFile(bank, pos, 1))
        return 0;
    const uint8_t* data = bank->data;
    if (data[pos] == 0xFF) {
        // Effects end with FF, music tracks with the end of track meta event (FF 2F 00)
        event->status = SND_EVENT_END;
        return pos + (effect ? 1 : 3);
    }
    if (data[pos] & 0x80)
        *runningStatus = data[pos++];
    uint8_t status = *runningStatus;
    int count;
    switch (status >> 4) {
    case 0x8: case 0xC: case 0xE:
        count = 1;
        break;
    case 0x9: case 0xA: case 0xB:
        count = 2;
        break;
    case 0xD:
        count = effect ? 3 : 1;
        break;
    default:
        return 0;
    }
    if (!InFile(bank, pos, count))
        return 0;
    event->status = status;
    for (int i = 0; i < count; i++)
        event->data[i] = data[pos + i] & 0x7F;
    pos += count;
    // Bit 7 of the last data byte: no time to wait after the event. Otherwise, a variable-length delta follows
    if (data[pos - 1] & 0x80)
        return pos;
    uint32_t delta = 0;
    for (int i = 0; i < 4; i++) {
        if (!InFile(bank, pos, 1))
            return 0;
        uint8_t byte = data[pos++];
        delta = (delta << 7) | (byte & 0x7F);
        if (!(byte & 0x80))
            break;
    }
    event->delta = delta;
    return pos;
}

// Duration of a music track or an effect, in milliseconds
static uint32_t EventsDuration(const SND_Bank* bank, uint32_t pos, bool effect) {
    uint8_t status = 0;
    uint32_t duration = 0;
    SND_Event event;
    while ((pos = SND_ReadEvent(bank, pos, effect, &status, &event)) != 0 && event.status != SND_EVENT_END)
        duration += event.delta;
    return duration;
}

static void ReadSequence(SND_Bank* bank, SND_Sequence* sequence) {
    uint32_t start = sequence->offset;
    if (!InFile(bank, start, 1))
        return;
    if (bank->data[start] & 0x80) {
        // Effect: a header byte, then the events. The empty sequences only have a controller event
        uint8_t status = 0;
        SND_Event event;
        uint32_t pos = start + 1;
        while ((pos = SND_ReadEvent(bank, pos, true, &status, &event)) != 0 && event.status != SND_EVENT_END) {
            if (event.status >> 4 == 0xD)
                sequence->type = SND_SEQUENCE_EFFECT;
        }
        sequence->duration = EventsDuration(bank, start + 1, true);
        return;
    }

    sequence->type = SND_SEQUENCE_MUSIC;
    for (uint32_t i = 1; i < SND_MAX_PLAYLIST; i++) {
        uint32_t word = SND_ReadU32(bank, start + i * 4);
        if (SND_PLAYLIST_TRACK(word) || SND_PLAYLIST_LAST(word))
            sequence->duration += EventsDuration(bank, start + (word & 0xFFFFFF), false);
        if (SND_PLAYLIST_JUMP(word))
            sequence->loop = true;
        if (!SND_PLAYLIST_TRACK(word) && !SND_PLAYLIST_COMMAND(word))
            break;
    }
}

// The sequences are grouped: a list of groups (offset of 16 bits and 2 unknown bytes), each one a list of offsets
static void ReadSequences(SND_Bank* bank, uint32_t section) {
    if (section == 0 || !InFile(bank, section, 4))
        return;
    uint32_t groupCount = Bytes_ReadU32(bank->data + section) + 1;
    if (!InFile(bank, section + 4, (size_t)groupCount * 4))
        return;
    uint32_t total = 0;
    for (uint32_t g = 0; g < groupCount; g++) {
        uint32_t group = section + Bytes_ReadU16(bank->data + section + 4 + g * 4);
        if (!InFile(bank, group, 4))
            return;
        total += Bytes_ReadU32(bank->data + group) + 1;
    }
    bank->sequences = calloc(total, sizeof(SND_Sequence));
    if (bank->sequences == NULL)
        return;
    for (uint32_t g = 0; g < groupCount; g++) {
        uint32_t group = section + Bytes_ReadU16(bank->data + section + 4 + g * 4);
        uint32_t count = Bytes_ReadU32(bank->data + group) + 1;
        if (!InFile(bank, group + 4, (size_t)count * 4))
            return;
        for (uint32_t i = 0; i < count; i++) {
            SND_Sequence* sequence = &bank->sequences[bank->sequenceCount++];
            sequence->group = g;
            sequence->index = i;
            sequence->offset = section + Bytes_ReadU32(bank->data + group + 4 + i * 4);
            ReadSequence(bank, sequence);
        }
    }
}

// Rate of the sample at note 60, from the first split that plays it
static void SetSampleRate(SND_Bank* bank, const SND_Program* program) {
    for (uint32_t l = 0; l < program->layerCount; l++) {
        for (uint32_t s = 0; s < program->layers[l].splitCount; s++) {
            const SND_Split* split = &program->layers[l].splits[s];
            if (split->sample >= bank->sampleCount || bank->samples[split->sample].sampleRate != 0)
                continue;
            SND_Sample* sample = &bank->samples[split->sample];
            double rate = 44100.0 * pow(2.0, (split->transpose + split->fine / 64.0) / 12.0);
            sample->sampleRate = (uint32_t)(rate * (sample->halfRate ? 2 : 1) + 0.5);
        }
    }
}

static void FreeProgram(SND_Program* program) {
    for (uint32_t l = 0; l < program->layerCount; l++)
        free(program->layers[l].splits);
}

void SND_Free(SND_Bank* bank) {
    if (bank->samples != NULL) {
        for (uint32_t i = 0; i < bank->sampleCount; i++)
            free(bank->samples[i].pcm);
        free(bank->samples);
    }
    if (bank->programs != NULL) {
        for (uint32_t i = 0; i < bank->programCount; i++)
            FreeProgram(&bank->programs[i]);
        free(bank->programs);
    }
    FreeProgram(&bank->effectProgram);
    free(bank->parts);
    free(bank->curves);
    free(bank->sequences);
    free(bank->data);
    memset(bank, 0, sizeof(*bank));
}

bool SND_Load(const char* filepath, SND_Bank* bank) {
    memset(bank, 0, sizeof(*bank));
    size_t size = 0;
    uint8_t* buffer = File_LoadToMemoryDecompressed(filepath, &size);
    if (buffer == NULL)
        return false;
    if (size < SND_HEADER_SIZE || size > UINT32_MAX || memcmp(buffer, "DTPK", 4) != 0) {
        free(buffer);
        return false;
    }
    bank->data = buffer;
    bank->size = (uint32_t)size;

    uint32_t sections[SND_SECTION_COUNT];
    for (int i = 0; i < SND_SECTION_COUNT; i++)
        sections[i] = Bytes_ReadU32(buffer + 0x20 + i * 4);
    uint32_t table = sections[SND_SECTION_SAMPLES];
    if (table == 0 || !InFile(bank, table, 4)) {
        SND_Free(bank);
        return false;
    }
    // The table starts with the index of the last sample
    uint32_t last = Bytes_ReadU32(buffer + table);
    if (last >= (size - table - 4) / SND_SAMPLE_ENTRY_SIZE) {
        SND_Free(bank);
        return false;
    }
    uint32_t count = last + 1;
    bank->samples = calloc(count, sizeof(SND_Sample));
    if (bank->samples == NULL) {
        SND_Free(bank);
        return false;
    }
    bank->sampleCount = count;
    for (uint32_t i = 0; i < count; i++) {
        if (!ReadSample(buffer, size, buffer + table + 4 + (size_t)i * SND_SAMPLE_ENTRY_SIZE, &bank->samples[i])) {
            fprintf(stderr, "[SND_Load]: Invalid sample %u in %s\n", i, filepath);
            free(bank->samples[i].pcm);
            bank->samples[i].pcm = NULL;
            bank->samples[i].frameCount = 0;
            bank->samples[i].loop = false;
        }
    }

    ReadParts(bank, sections[SND_SECTION_PARTS]);
    ReadPrograms(bank, sections[SND_SECTION_PROGRAMS]);
    // The effect program: a program with one split per effect
    if (sections[SND_SECTION_EFFECTS] != 0)
        bank->hasEffectProgram = ReadProgram(bank, sections[SND_SECTION_EFFECTS], &bank->effectProgram, true);
    // The curves turn the velocity into a level. Without them, a linear curve
    uint32_t curves = sections[SND_SECTION_CURVES];
    uint32_t curveCount = curves != 0 && InFile(bank, curves, 4) ? Bytes_ReadU32(buffer + curves) + 1 : 0;
    if (curveCount == 0 || curveCount > 256 || !InFile(bank, curves + 4, (size_t)curveCount * 128))
        curveCount = 0;
    bank->curves = malloc((curveCount != 0 ? curveCount : 1) * sizeof(bank->curves[0]));
    if (bank->curves == NULL) {
        SND_Free(bank);
        return false;
    }
    if (curveCount != 0)
        memcpy(bank->curves, buffer + curves + 4, (size_t)curveCount * 128);
    else {
        curveCount = 1;
        for (int i = 0; i < 128; i++)
            bank->curves[0][i] = (uint8_t)(i * 2);
    }
    bank->curveCount = curveCount;
    ReadSequences(bank, sections[SND_SECTION_SEQUENCES]);
    ReadDSP(bank, sections[SND_SECTION_DSP]);

    for (uint32_t i = 0; i < bank->programCount; i++)
        SetSampleRate(bank, &bank->programs[i]);
    SetSampleRate(bank, &bank->effectProgram);
    return true;
}
