#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <wmcodecdsp.h>
#endif
#include "core/file.h"
#include "formats/sound/xwm.h"

typedef struct {
	const uint8_t*	format;   // WAVEFORMATEX
	uint32_t		formatSize;
	const uint8_t*	dpds;     // uint32 per packet
	uint32_t		dpdsCount;
	const uint8_t*	data;
	uint32_t		dataSize;
} XWM_Chunks;

static uint16_t ReadU16(const uint8_t* p) {
	return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t ReadU32(const uint8_t* p) {
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Finds the chunks of the file and reads the format. Returns false if the file is not a valid xWMA file
static bool ReadChunks(const uint8_t* buffer, size_t size, XWM_Chunks* chunks, XWM_Info* info) {
	if (size < 12 || memcmp(buffer, "RIFF", 4) != 0 || memcmp(buffer + 8, "XWMA", 4) != 0)
		return false;

	memset(chunks, 0, sizeof(*chunks));
	for (size_t pos = 12; pos + 8 <= size;) {
		uint32_t chunkSize = ReadU32(buffer + pos + 4);
		if (chunkSize > size - pos - 8)
			return false;
		const uint8_t* chunk = buffer + pos + 8;
		if (memcmp(buffer + pos, "fmt ", 4) == 0) {
			chunks->format = chunk;
			chunks->formatSize = chunkSize;
		}
		else if (memcmp(buffer + pos, "dpds", 4) == 0) {
			chunks->dpds = chunk;
			chunks->dpdsCount = chunkSize / 4;
		}
		else if (memcmp(buffer + pos, "data", 4) == 0) {
			chunks->data = chunk;
			chunks->dataSize = chunkSize;
		}
		pos += 8 + (size_t)chunkSize + (chunkSize & 1);
	}
	if (chunks->format == NULL || chunks->formatSize < 16 || chunks->dpds == NULL || chunks->dpdsCount == 0 || chunks->data == NULL)
		return false;

	info->formatTag = ReadU16(chunks->format);
	info->channels = ReadU16(chunks->format + 2);
	info->sampleRate = ReadU32(chunks->format + 4);
	info->avgBytesPerSec = ReadU32(chunks->format + 8);
	info->blockAlign = ReadU16(chunks->format + 12);
	if (info->channels == 0 || info->sampleRate == 0 || info->blockAlign == 0)
		return false;
	info->packetCount = chunks->dataSize / info->blockAlign;
	info->frameCount = ReadU32(chunks->dpds + (size_t)(chunks->dpdsCount - 1) * 4) / (2u * info->channels);
	return true;
}

#ifdef _WIN32

// The clips have no extra data, but the decoder needs the encode options of WMA v2. 0x1F decodes all the clips except
// the one at 32000 Hz (jap/e1030.afs), which needs 0x17. The right options are the ones that give the decoded size of
// the dpds table
static const uint16_t g_encodeOptions[] = { 0x1F, 0x17 };

// Creates the WMA decoder for the format with the given encode options, with 16-bit PCM output
static IMFTransform* CreateDecoder(const XWM_Info* info, uint16_t encodeOptions) {
	IMFTransform* transform = NULL;
	if (FAILED(CoCreateInstance(&CLSID_CWMADecMediaObject, NULL, CLSCTX_INPROC_SERVER, &IID_IMFTransform, (void**)&transform)))
		return NULL;

	// WAVEFORMATEX followed by the WMA v2 extra data: samples per block, encode options and super block align
	uint8_t format[sizeof(WAVEFORMATEX) + 10] = { 0 };
	WAVEFORMATEX* wfx = (WAVEFORMATEX*)format;
	wfx->wFormatTag = info->formatTag;
	wfx->nChannels = info->channels;
	wfx->nSamplesPerSec = info->sampleRate;
	wfx->nAvgBytesPerSec = info->avgBytesPerSec;
	wfx->nBlockAlign = info->blockAlign;
	wfx->wBitsPerSample = 16;
	wfx->cbSize = 10;
	memcpy(format + sizeof(WAVEFORMATEX) + 4, &encodeOptions, 2);

	bool success = false;
	IMFMediaType* inputType = NULL;
	if (SUCCEEDED(MFCreateMediaType(&inputType)) &&
		SUCCEEDED(MFInitMediaTypeFromWaveFormatEx(inputType, wfx, sizeof(format))) &&
		SUCCEEDED(IMFTransform_SetInputType(transform, 0, inputType, 0))) {
		IMFMediaType* outputType = NULL;
		for (DWORD i = 0; !success && SUCCEEDED(IMFTransform_GetOutputAvailableType(transform, 0, i, &outputType)); i++) {
			GUID subtype = { 0 };
			UINT32 bits = 0;
			IMFMediaType_GetGUID(outputType, &MF_MT_SUBTYPE, &subtype);
			IMFMediaType_GetUINT32(outputType, &MF_MT_AUDIO_BITS_PER_SAMPLE, &bits);
			if (IsEqualGUID(&subtype, &MFAudioFormat_PCM) && bits == 16)
				success = SUCCEEDED(IMFTransform_SetOutputType(transform, 0, outputType, 0));
			IMFMediaType_Release(outputType);
		}
	}
	if (inputType != NULL)
		IMFMediaType_Release(inputType);
	if (!success) {
		IMFTransform_Release(transform);
		return NULL;
	}
	return transform;
}

// Appends the decoded output to pcm until the decoder needs more input. Returns false on error
static bool ReadOutput(IMFTransform* transform, IMFSample* sample, IMFMediaBuffer* buffer, uint8_t** pcm, size_t* pcmSize, size_t* pcmCapacity) {
	for (;;) {
		IMFMediaBuffer_SetCurrentLength(buffer, 0);
		MFT_OUTPUT_DATA_BUFFER output = { 0 };
		output.pSample = sample;
		DWORD status = 0;
		HRESULT result = IMFTransform_ProcessOutput(transform, 0, 1, &output, &status);
		if (output.pEvents != NULL)
			IMFCollection_Release(output.pEvents);
		if (result == MF_E_TRANSFORM_NEED_MORE_INPUT)
			return true;
		if (FAILED(result))
			return false;

		BYTE* data = NULL;
		DWORD length = 0;
		if (FAILED(IMFMediaBuffer_Lock(buffer, &data, NULL, &length)))
			return false;
		if (*pcmSize + length > *pcmCapacity) {
			size_t capacity = (*pcmSize + length) * 2;
			uint8_t* grown = realloc(*pcm, capacity);
			if (grown == NULL) {
				IMFMediaBuffer_Unlock(buffer);
				return false;
			}
			*pcm = grown;
			*pcmCapacity = capacity;
		}
		memcpy(*pcm + *pcmSize, data, length);
		*pcmSize += length;
		IMFMediaBuffer_Unlock(buffer);
	}
}

// Decodes all the packets, one input sample each. Returns the PCM data and its size, or NULL on error
static uint8_t* DecodePackets(IMFTransform* transform, const XWM_Chunks* chunks, const XWM_Info* info, size_t* outSize) {
	MFT_OUTPUT_STREAM_INFO streamInfo = { 0 };
	if (FAILED(IMFTransform_GetOutputStreamInfo(transform, 0, &streamInfo)))
		return NULL;

	size_t pcmSize = 0;
	size_t pcmCapacity = (size_t)info->frameCount * info->channels * 2 + 65536;
	uint8_t* pcm = malloc(pcmCapacity);
	IMFSample* outputSample = NULL;
	IMFMediaBuffer* outputBuffer = NULL;
	bool success = pcm != NULL &&
		SUCCEEDED(MFCreateSample(&outputSample)) &&
		SUCCEEDED(MFCreateMemoryBuffer(streamInfo.cbSize > 0 ? streamInfo.cbSize : 65536, &outputBuffer)) &&
		SUCCEEDED(IMFSample_AddBuffer(outputSample, outputBuffer));

	for (uint32_t i = 0; success && i < info->packetCount; i++) {
		IMFSample* inputSample = NULL;
		IMFMediaBuffer* inputBuffer = NULL;
		BYTE* data = NULL;
		success = SUCCEEDED(MFCreateSample(&inputSample)) &&
			SUCCEEDED(MFCreateMemoryBuffer(info->blockAlign, &inputBuffer)) &&
			SUCCEEDED(IMFMediaBuffer_Lock(inputBuffer, &data, NULL, NULL));
		if (success) {
			memcpy(data, chunks->data + (size_t)i * info->blockAlign, info->blockAlign);
			IMFMediaBuffer_Unlock(inputBuffer);
			success = SUCCEEDED(IMFMediaBuffer_SetCurrentLength(inputBuffer, info->blockAlign)) &&
				SUCCEEDED(IMFSample_AddBuffer(inputSample, inputBuffer)) &&
				SUCCEEDED(IMFTransform_ProcessInput(transform, 0, inputSample, 0)) &&
				ReadOutput(transform, outputSample, outputBuffer, &pcm, &pcmSize, &pcmCapacity);
		}
		if (inputBuffer != NULL)
			IMFMediaBuffer_Release(inputBuffer);
		if (inputSample != NULL)
			IMFSample_Release(inputSample);
	}
	// The decoder keeps the last frame until it is drained
	success = success &&
		SUCCEEDED(IMFTransform_ProcessMessage(transform, MFT_MESSAGE_COMMAND_DRAIN, 0)) &&
		ReadOutput(transform, outputSample, outputBuffer, &pcm, &pcmSize, &pcmCapacity);

	if (outputBuffer != NULL)
		IMFMediaBuffer_Release(outputBuffer);
	if (outputSample != NULL)
		IMFSample_Release(outputSample);
	if (!success) {
		free(pcm);
		return NULL;
	}
	*outSize = pcmSize;
	return pcm;
}

static int16_t* Decode(const XWM_Chunks* chunks, const XWM_Info* info) {
	// The thread may already use COM in another mode (the file dialogs), which is fine for the decoder
	HRESULT comResult = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
		if (SUCCEEDED(comResult))
			CoUninitialize();
		return NULL;
	}

	size_t expectedSize = (size_t)info->frameCount * info->channels * 2;
	uint8_t* pcm = NULL;
	for (size_t i = 0; pcm == NULL && i < sizeof(g_encodeOptions) / sizeof(g_encodeOptions[0]); i++) {
		IMFTransform* transform = CreateDecoder(info, g_encodeOptions[i]);
		if (transform == NULL)
			continue;
		size_t size = 0;
		pcm = DecodePackets(transform, chunks, info, &size);
		IMFTransform_Release(transform);
		if (pcm != NULL && size != expectedSize) {
			free(pcm);
			pcm = NULL;
		}
	}

	MFShutdown();
	if (SUCCEEDED(comResult))
		CoUninitialize();
	return (int16_t*)pcm;
}

#else

static int16_t* Decode(const XWM_Chunks* chunks, const XWM_Info* info) {
	fputs("[XWM_Decode]: WMA decoding is only available on Windows\n", stderr);
	return NULL;
}

#endif

int16_t* XWM_Decode(const char* filepath, XWM_Info* info) {
	size_t size = 0;
	uint8_t* buffer = File_LoadToMemoryDecompressed(filepath, &size);
	if (buffer == NULL)
		return NULL;

	XWM_Chunks chunks;
	int16_t* pcm = NULL;
	if (!ReadChunks(buffer, size, &chunks, info))
		fprintf(stderr, "[XWM_Decode]: Not a valid xWMA file: %s\n", filepath);
	else if (info->formatTag != 0x161)
		fprintf(stderr, "[XWM_Decode]: Unsupported format 0x%X: %s\n", info->formatTag, filepath);
	else if ((pcm = Decode(&chunks, info)) == NULL)
		fprintf(stderr, "[XWM_Decode]: Failed to decode %s\n", filepath);

	free(buffer);
	return pcm;
}
