/*
 * g64coopdx: the start screen's remix of the menu theme (the game's song 60, TITLE2_TRACK). The sound library plays
 * it (src/lib/sound.c, GLOVER_SOUND_REMIX); the online program's start screen asks for it (src/client/soundout.c).
 * Nothing here runs in the game itself: the player's hooks (musRemixFrame, musRemixNote) stay NULL there.
 *
 * The notes, the instruments and the drums all come from the player's own ROM as the song plays. The remix only
 * changes how they're played, by these rules:
 *   - The tempo is 9/16 of the song's own, about 68 beats a minute (MusHandleSetTempo). The added drums keep the
 *     song's beat, so they slow with it.
 *   - The two lead instruments trade parts: each of their notes plays with the other one's wave, moved by the
 *     difference between the transposes the song uses with the two waves (both waves then sound the written pitch).
 *   - Every pitched part goes down a fourth (5 semitones), the bass too: an octave lower, the bass rumbled. A note
 *     that the player would push past an octave above its sample (where libmus falls silent) goes down an octave
 *     instead, and a note that would sink below LOWEST, where a sample slowed that far only rumbles, goes up an
 *     octave.
 *   - The song's hats play at a little over half their velocity: the remix has less of the highest sounds.
 *   - A rhythm layer joins the song's own drums: the kick on every beat, the snare on beats 2 and 4, and a hat on the
 *     offbeat eighths. Each hit plays on a free channel, as a copy of the song's own channel that last played that
 *     drum, with a one-note stream.
 *   - The song plays with more reverb than the game gives it.
 */
PORT_C_BEGIN
#include "mus_internal.h"
#include "jalloc.h"
#include "sound.h"

/* The song's waves by role (the menu theme's pbank numbers). */
#define LEAD_A 59
#define LEAD_B 94
#define KICK 19
#define SNARE 18
#define HAT 15
/* The transposes the song uses with the two lead waves: they tune each sample to the written pitch. */
#define LEAD_A_TRANSPOSE 0
#define LEAD_B_TRANSPOSE 12
/* The highest note + transpose that a sample takes: above it the pitch ratio passes 2 (musVoiceUpdatePitch). */
#define HIGHEST 60
#define LOWEST 26
#define KEY_SHIFT (-5)
#define TEMPO 72 /* MusHandleSetTempo: 128 is the song's own */
/* The song's own hats, and the share of their velocity they keep, in 128ths. */
#define HAT_B 77
#define HAT_SHARE 72
#define REVERB 40
#define HIT_LENGTH 24 /* ticks */

static int remixOn;
/* The song's channel that last played each drum, as it was then: the added hits copy it. */
#define DRUMS 3
static const int drumWave[DRUMS] = {KICK, SNARE, HAT};
static const int drumVelocity[DRUMS] = {90, 70, 30};
static MusChannel drumTemplate[DRUMS];
static int drumHave[DRUMS];
/* The one-note streams of the added hits, in the game's memory (a stream is a word in port_ram). */
static u8 *hitStream;
static u32 lastEighth;

static int is_drum(u16 wave) {
    return wave == 14 || wave == 15 || wave == 17 || wave == 18 || wave == 19 || wave == 26 || wave == 77 || wave == 93;
}

static void remix_note(MusChannel *ch, s32 index, s32 *note) {
    s32 i, n = *note, transpose = (s8)ch->transpose;
    (void)index;
    for (i = 0; i < DRUMS; i++) {
        if (ch->wave == drumWave[i]) {
            drumTemplate[i] = *ch;
            drumHave[i] = 1;
        }
    }
    if (is_drum(ch->wave)) {
        if (ch->wave == HAT || ch->wave == HAT_B) {
            ch->velocity = (u8)(ch->velocity * HAT_SHARE / 128);
        }
        return;
    }
    if (ch->wave == LEAD_A) {
        ch->wave = LEAD_B;
        n += LEAD_B_TRANSPOSE - LEAD_A_TRANSPOSE;
    } else if (ch->wave == LEAD_B) {
        ch->wave = LEAD_A;
        n += LEAD_A_TRANSPOSE - LEAD_B_TRANSPOSE;
    }
    n += KEY_SHIFT;
    while (n + transpose > HIGHEST && n >= 12) {
        n -= 12;
    }
    while (n + transpose < LOWEST) {
        n += 12;
    }
    *note = n;
}

/* A free channel for the added drum i: the last channels, which the song leaves free. */
static MusChannel *hit_channel(s32 i, s32 *index) {
    s32 k = musNumChannels - 1 - i;
    if (k < 0 || musChannels[k].pdata != NULL || musChannels[k].fxNumber != 0) {
        return NULL;
    }
    *index = k;
    return &musChannels[k];
}

static void play_hit(s32 i, s32 handle) {
    MusChannel *ch;
    s32 index;
    u8 voiceOn;
    u8 *s;
    if (!drumHave[i] || hitStream == NULL || (ch = hit_channel(i, &index)) == NULL) {
        return;
    }
    voiceOn = ch->voiceOn;
    *ch = drumTemplate[i];
    ch->voiceOn = voiceOn;
    ch->flags = 0;
    ch->handle = handle;
    ch->pendingWave = NULL;
    ch->stopCount = -1;
    ch->volumeStream = NULL;
    ch->bendStream = NULL;
    ch->loopDepth = 0;
    ch->portamentoTicks = 0;
    ch->vibratoPeriod = 0;
    ch->trillOnFrames = 0;
    ch->panSweepSpeed = 0;
    ch->tie = 0;
    ch->keepEnvelope = 0;
    ch->drumMap = NULL;
    ch->lastVolume = 0xFFFF;
    ch->lastPan = 0xFF;
    ch->lastReverb = 0xFF;
    ch->lastPitch = MUS_FIRST_PITCH;
    /* The stream: one note, its length, and the end. */
    s = hitStream + i * 4;
    s[0] = 36;
    s[1] = HIT_LENGTH;
    s[2] = 0x80;
    ch->velocityOn = 0;
    ch->fixedVelocity = (u8)drumVelocity[i];
    ch->fixedLength = 0;
    ch->readNextLength = 0;
    ch->pdata = s;
    ch->dataStart = s;
    ch->time = 0;
    ch->noteEnd = 0;
    ch->length = 0;
}

static void remix_frame(void) {
    MusChannel *ch = NULL;
    s32 i;
    u32 eighth;
    /* The song's clock: its first channel that plays. */
    for (i = 0; i < musNumChannels; i++) {
        if (musChannels[i].pdata != NULL && musChannels[i].song != NULL && musChannels[i].handle == audioCtrl.handle &&
            !(musChannels[i].flags & MUS_CHANNEL_PAUSED)) {
            ch = &musChannels[i];
            break;
        }
    }
    if (ch == NULL || ch->stopCount != -1) {
        return;
    }
    /* A tick is 1/48 beat; the clock counts 1/256 ticks. An eighth note is 24 ticks. */
    eighth = (ch->time + ch->tempoStep) / (24 * 256);
    if (eighth == lastEighth) {
        return;
    }
    lastEighth = eighth;
    if (eighth % 2 == 0) {
        play_hit(0, ch->handle);
    }
    if (eighth % 4 == 2) {
        play_hit(1, ch->handle);
    }
    if (eighth % 2 == 1) {
        play_hit(2, ch->handle);
    }
}

void g64_remix_mode(int on) {
    remixOn = on != 0;
    musRemixNote = remixOn ? remix_note : NULL;
    musRemixFrame = remixOn ? remix_frame : NULL;
    if (remixOn && hitStream == NULL) {
        hitStream = (u8 *)JallocAlloc(16, 1, "Remix");
    }
    memset(drumHave, 0, sizeof drumHave);
    lastEighth = 0;
}

/* After the song has started: the remix's tempo and reverb, or the song's own. */
void g64_remix_song_started(void) {
    if (audioCtrl.handle != 0) {
        MusHandleSetTempo(audioCtrl.handle, remixOn ? TEMPO : MUS_TEMPO_SCALE_ONE);
        if (remixOn) {
            MusHandleSetReverb(audioCtrl.handle, REVERB);
        }
    }
    memset(drumHave, 0, sizeof drumHave);
    lastEighth = 0;
}
PORT_C_END
