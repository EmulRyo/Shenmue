#include <stdint.h>
#include <string.h>
#include "audio/sndSynthInternal.h"

#define MAX_EVENTS_PER_SAMPLE 4096  // Stops a sequence that loops without advancing in time

// ---------------------------------------------------------------------------------------------------------------------
// Channels

void Channels_Reset(SND_Synth* synth) {
    const SND_Bank* bank = synth->bank;
    for (int c = 0; c < CHANNEL_COUNT; c++) {
        Channel* channel = &synth->channels[c];
        memset(channel, 0, sizeof(*channel));
        channel->volume = c < SND_MAX_CHANNELS ? 100 : 127;
        channel->expression = 127;
        channel->pan = 64;
        channel->bend = 64;
        channel->program = -1;
    }
    // The volume of the part is the initial volume of its channel
    for (int i = (int)bank->partCount - 1; i >= 0; i--)
        synth->channels[bank->parts[i].channel].volume = bank->parts[i].volume;
}

static void Controller(SND_Synth* synth, int c, uint8_t controller, uint8_t value) {
    Channel* channel = &synth->channels[c];
    switch (controller) {
    case 1:
        channel->modulation = value;
        break;
    case 7:
        channel->volume = value;
        break;
    case 10:
        channel->pan = value;
        channel->panSet = true;
        break;
    case 11:
        channel->expression = value;
        break;
    case 64:
        channel->sustain = value >= 64;
        if (!channel->sustain) {
            for (int i = 0; i < SND_SYNTH_VOICES; i++) {
                Voice* voice = &synth->voices[i];
                if (voice->active && voice->channel == c && voice->sustained)
                    Synth_KeyOff(voice);
            }
        }
        break;
    case 121: // Reset all controllers
        channel->expression = 127;
        channel->modulation = 0;
        channel->bend = 64;
        channel->sustain = false;
        break;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Sequencer

void Sequencer_Start(SND_Synth* synth, const SND_Sequence* sequence) {
    Sequencer* sequencer = &synth->sequencer;
    memset(sequencer, 0, sizeof(*sequencer));
    sequencer->active = true;
    sequencer->effect = sequence->type == SND_SEQUENCE_EFFECT;
    sequencer->start = sequence->offset;
    sequencer->word = 4; // The playlist starts after the first word
    if (sequencer->effect)
        sequencer->pos = sequence->offset + 1; // After the header byte
}

void Sequencer_Stop(SND_Synth* synth) {
    synth->sequencer.active = false;
}

static void MusicEvent(SND_Synth* synth, const SND_Event* event) {
    int channel = event->status & 0x0F;
    switch (event->status >> 4) {
    case 0x8:
        Synth_NoteOff(synth, channel, event->data[0]);
        break;
    case 0x9:
        if (event->data[1] == 0)
            Synth_NoteOff(synth, channel, event->data[0]);
        else
            Synth_NoteOn(synth, channel, event->data[0], event->data[1]);
        break;
    case 0xB:
        Controller(synth, channel, event->data[0], event->data[1]);
        break;
    case 0xC:
        synth->channels[channel].program = event->data[0];
        break;
    case 0xE:
        synth->channels[channel].bend = event->data[0];
        break;
    }
}

// Effects: Dx starts the effect data[0] of the effect program with the volume data[1] (0 stops it), Ax is a controller
static void EffectEvent(SND_Synth* synth, const SND_Event* event) {
    int channel = SND_MAX_CHANNELS + (event->status & 0x0F);
    const SND_Bank* bank = synth->bank;
    switch (event->status >> 4) {
    case 0xD:
        if (event->data[1] == 0) {
            for (int i = 0; i < SND_SYNTH_VOICES; i++) {
                Voice* voice = &synth->voices[i];
                if (voice->active && voice->channel == channel && voice->key == event->data[0])
                    Synth_KeyOff(voice);
            }
        }
        else if (bank->hasEffectProgram)
            Synth_StartNote(synth, &bank->effectProgram, channel, event->data[0], event->data[1], NULL);
        break;
    case 0xA:
        if (event->data[0] == 7 || event->data[0] == 11)
            Controller(synth, channel, event->data[0], event->data[1]);
        break;
    }
}

// Moves to the next track of the playlist. Returns false at the end of the playlist
static bool NextTrack(SND_Synth* synth) {
    Sequencer* sequencer = &synth->sequencer;
    if (sequencer->last)
        return false;
    for (int i = 0; i < 64; i++) {
        uint32_t word = SND_ReadU32(synth->bank, sequencer->start + sequencer->word);
        if (SND_PLAYLIST_COMMAND(word))
            sequencer->word += 4;
        else if (SND_PLAYLIST_JUMP(word))
            sequencer->word = word & 0xFFFFFF;
        else if (SND_PLAYLIST_TRACK(word) || SND_PLAYLIST_LAST(word)) {
            sequencer->pos = sequencer->start + (word & 0xFFFFFF);
            sequencer->status = 0;
            sequencer->last = SND_PLAYLIST_LAST(word);
            sequencer->word += 4;
            return true;
        }
        else
            return false;
    }
    return false;
}

void Sequencer_Update(SND_Synth* synth) {
    Sequencer* sequencer = &synth->sequencer;
    for (int count = 0; sequencer->active && sequencer->wait <= 0.0; count++) {
        if (count >= MAX_EVENTS_PER_SAMPLE) {
            Sequencer_Stop(synth);
            return;
        }
        if (sequencer->pos == 0 && (sequencer->effect || !NextTrack(synth))) {
            Sequencer_Stop(synth);
            return;
        }
        SND_Event event;
        uint32_t next = SND_ReadEvent(synth->bank, sequencer->pos, sequencer->effect, &sequencer->status, &event);
        if (next == 0) {
            Sequencer_Stop(synth);
            return;
        }
        sequencer->pos = next;
        if (event.status == SND_EVENT_END) {
            sequencer->pos = 0;
            continue;
        }
        if (sequencer->effect)
            EffectEvent(synth, &event);
        else
            MusicEvent(synth, &event);
        sequencer->wait += event.delta * SAMPLES_PER_MS;
    }
    sequencer->wait -= 1.0;
}
