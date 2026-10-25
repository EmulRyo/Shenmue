#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "file.h"
#include "snd.h"

#define SND_HEADER_SIZE     0x60
#define SND_SECTION_COUNT   8
#define SND_SECTION_PROGRAMS    1
#define SND_SECTION_SEQUENCES   3
#define SND_SECTION_DSP         6
#define SND_SECTION_SAMPLES     7
#define SND_SAMPLE_ENTRY_SIZE   0x10

static uint16_t ReadU16(const uint8_t* p) {
	return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t ReadU32(const uint8_t* p) {
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
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
	uint32_t offsetField = ReadU32(entry);
	uint32_t loopStart = ReadU16(entry + 4);
	uint32_t loopEnd = ReadU16(entry + 6);
	uint32_t pairs = ReadU32(entry + 8) == 0x80; // The loop points count pairs of samples
	memset(sample, 0, sizeof(*sample));
	sample->offset = offsetField & 0x7FFFFF;
	sample->size = ReadU32(entry + 12);
	sample->loop = (offsetField >> 25) & 1;
	if (sample->offset > size || sample->size > size - sample->offset)
		return false;

	if (!pairs && loopEnd >= sample->size + sample->size / 2) {
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
			sample->pcm[i] = (int16_t)ReadU16(data + (size_t)i * 2);
	}

	if (pairs) {
		loopStart *= 2;
		loopEnd *= 2;
	}
	if (loopEnd > sample->frameCount)
		loopEnd = sample->frameCount;
	if (loopStart >= loopEnd)
		sample->loop = false;
	sample->loopStart = loopStart;
	sample->loopEnd = loopEnd;
	return true;
}

// Number of entries of the program bank: a list of banks, each one a list of programs (uint16 last index + offsets)
static uint32_t CountPrograms(const uint8_t* buffer, size_t size, uint32_t section) {
	if (section == 0 || (size_t)section + 4 > size)
		return 0;
	uint32_t bank = section + ReadU16(buffer + section + 2);
	if ((size_t)bank + 2 > size)
		return 0;
	return ReadU16(buffer + bank) + 1u;
}

// Number of sequences of all the groups of the sequence bank
static uint32_t CountSequences(const uint8_t* buffer, size_t size, uint32_t section) {
	if (section == 0 || (size_t)section + 4 > size)
		return 0;
	uint32_t groupCount = ReadU32(buffer + section) + 1;
	uint32_t count = 0;
	for (uint32_t i = 0; i < groupCount && (size_t)section + 8 + i * 4 <= size; i++) {
		uint32_t group = section + ReadU16(buffer + section + 4 + i * 4);
		if ((size_t)group + 4 > size)
			break;
		count += ReadU32(buffer + group) + 1;
	}
	return count;
}

void SND_Free(SND_Bank* bank) {
	if (bank->samples != NULL) {
		for (uint32_t i = 0; i < bank->sampleCount; i++)
			free(bank->samples[i].pcm);
		free(bank->samples);
	}
	memset(bank, 0, sizeof(*bank));
}

bool SND_Load(const char* filepath, SND_Bank* bank) {
	memset(bank, 0, sizeof(*bank));
	size_t size = 0;
	uint8_t* buffer = File_LoadToMemoryDecompressed(filepath, &size);
	if (buffer == NULL)
		return false;
	if (size < SND_HEADER_SIZE || memcmp(buffer, "DTPK", 4) != 0) {
		free(buffer);
		return false;
	}

	uint32_t sections[SND_SECTION_COUNT];
	for (int i = 0; i < SND_SECTION_COUNT; i++)
		sections[i] = ReadU32(buffer + 0x20 + i * 4);
	uint32_t table = sections[SND_SECTION_SAMPLES];
	if (table == 0 || (size_t)table + 4 > size) {
		free(buffer);
		return false;
	}
	uint32_t count = ReadU32(buffer + table);
	if (count > (size - table - 4) / SND_SAMPLE_ENTRY_SIZE) {
		free(buffer);
		return false;
	}

	bank->size = ReadU32(buffer + 8);
	bank->programCount = CountPrograms(buffer, size, sections[SND_SECTION_PROGRAMS]);
	bank->sequenceCount = CountSequences(buffer, size, sections[SND_SECTION_SEQUENCES]);
	bank->hasDSP = sections[SND_SECTION_DSP] != 0;
	bank->samples = calloc(count > 0 ? count : 1, sizeof(SND_Sample));
	if (bank->samples == NULL) {
		free(buffer);
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
	free(buffer);
	return true;
}
