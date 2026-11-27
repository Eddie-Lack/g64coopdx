/*
 * g64coopdx: the start screen's remix of the menu theme (the game's song 60, TITLE2_TRACK), in the style of late-90s
 * game jungle: a chopped breakbeat under the song. The sound library plays it (src/lib/sound.c, GLOVER_SOUND_REMIX);
 * the online program's start screen asks for it (src/client/soundout.c). Nothing here runs in the game itself: the
 * player's hooks (musRemixFrame, musRemixNote) stay NULL there.
 *
 * The notes, the instruments, the drums and the effects all come from the player's own ROM as the song plays. The
 * remix only changes how they're played, by these rules:
 *   - The tempo is 107/128 of the song's own, about 100 beats a minute (MusHandleSetTempo).
 *   - The two lead instruments trade parts: each of their notes plays with the other one's wave, moved by the
 *     difference between the transposes the song uses with the two waves (both waves then sound the written pitch).
 *   - Every pitched part goes down a fourth (5 semitones). A note that the player would push past an octave above its
 *     sample (where libmus falls silent) goes down an octave instead, and a note that would sink below LOWEST, where
 *     a sample slowed that far only rumbles, goes up an octave. The bass plays louder.
 *   - The song's own drums are silent. A breakbeat takes their place, on a 16-bar cycle (break_hits): the kick skips
 *     beats, the snare has ghost notes between its beats, the hats play in sixteenths with a swing in their
 *     velocity, a snare roll ends every eighth bar, and one bar is in half time and one in double time. The first
 *     bars of the cycle are plainer. Each hit plays on a free channel, as a copy of the song's own channel that last
 *     played that drum, with a one-note stream.
 *   - Cross-Stitch laughs (the game's effect 176, which he plays when he casts a spell, nmeprocess2.c) at the end of
 *     two phrases of the cycle: once as he does, once a fifth lower and slower, with an echo.
 *   - Seagulls (wave 51 of the game's sound bank, which the Pirates 3 song plays) call now and then, panned left and
 *     right, at their own pitch and higher, each with a quieter echo on the other side.
 *   - The song plays with more reverb than the game gives it.
 */
PORT_C_BEGIN
#include "mus_internal.h"
#include "jalloc.h"
#include "sound.h"

/* The song's waves by role (the menu theme's pbank numbers). */
#define BASS 1
#define LEAD_A 59
#define LEAD_B 94
#define PAD 84
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
#define TEMPO 107 /* MusHandleSetTempo: 128 is the song's own */
#define REVERB 40
#define BASS_LOUDER 160 /* in 128ths of the bass's velocity */

/* The seagull: wave 51, which the Pirates 3 song plays at note + transpose 37 (its written pitch). */
#define GULL_WAVE 51
#define GULL_PITCH 37
/* Cross-Stitch's laugh, a sound effect. */
#define LAUGH_EFFECT 176

/* The added sounds' channels: the last ones, which the song leaves free. */
enum { HIT_KICK, HIT_SNARE, HIT_HAT, HIT_GULL, HIT_ECHO, HITS };
/* The song's channel that last played each drum (and the pad, for the gulls), as it was then: the hits copy it. */
static const int templateWave[HITS] = {KICK, SNARE, HAT, PAD, PAD};
static MusChannel hitTemplate[HITS];
static int hitHave[HITS];
/* The one-note streams of the hits, in the game's memory (a stream is a word in port_ram): 4 bytes each. */
static u8 *hitStream;
static int remixOn;
static u32 lastStep;
/* A sound that waits for a later sixteenth: the laugh's echo, or a gull's. */
typedef struct {
    u32 step;
    s32 what; /* 1 the laugh's echo, 2 a gull's echo */
    s32 pan, note, velocity;
} Later;
#define LATER 4
static Later later[LATER];

static int is_drum(u16 wave) {
    return wave == 14 || wave == 15 || wave == 17 || wave == 18 || wave == 19 || wave == 26 || wave == 77 || wave == 93;
}

static int is_hit_channel(s32 index) {
    return index >= musNumChannels - HITS;
}

static void remix_note(MusChannel *ch, s32 index, s32 *note) {
    s32 i, n = *note, transpose = (s8)ch->transpose;
    if (is_hit_channel(index)) {
        return;
    }
    for (i = 0; i < HITS; i++) {
        if (ch->wave == templateWave[i]) {
            hitTemplate[i] = *ch;
            hitHave[i] = 1;
        }
    }
    if (is_drum(ch->wave)) {
        /* The breakbeat plays in their place. */
        ch->velocity = 0;
        return;
    }
    if (ch->wave == BASS) {
        s32 v = ch->velocity * BASS_LOUDER / 128;
        ch->velocity = (u8)(v > 127 ? 127 : v);
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

/* Plays one note on hit channel i, as a copy of its template: wave (or the template's own when -1), the note and its
   length in ticks, the velocity and the pan (-1: the template's). */
static void play_hit(s32 i, s32 handle, s32 wave, s32 note, s32 length, s32 velocity, s32 pan) {
    MusChannel *ch;
    s32 k = musNumChannels - 1 - i;
    u8 voiceOn;
    u8 *s;
    if (!hitHave[i] || hitStream == NULL || k < 0 || musChannels[k].fxNumber != 0) {
        return;
    }
    ch = &musChannels[k];
    voiceOn = ch->voiceOn;
    *ch = hitTemplate[i];
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
    if (wave >= 0) {
        ch->wave = (u16)wave;
    }
    if (pan >= 0) {
        ch->pan = (u8)pan;
    }
    /* The stream: one note, its length, and the end. */
    s = hitStream + i * 4;
    s[0] = (u8)note;
    s[1] = (u8)length;
    s[2] = 0x80;
    ch->velocityOn = 0;
    ch->fixedVelocity = (u8)(velocity > 127 ? 127 : velocity);
    ch->fixedLength = 0;
    ch->readNextLength = 0;
    ch->pdata = s;
    ch->dataStart = s;
    ch->time = 0;
    ch->noteEnd = 0;
    ch->length = 0;
}

/* The drums of one sixteenth: step 0 to 15 of bar 0 to 15 of the cycle. Each is a velocity, 0 for none. */
static void break_hits(u32 bar, u32 step, s32 *kick, s32 *snare, s32 *hat) {
    /* The busy bars' break: the kick skips beats, the snare is on 2 and 4 with ghost notes between. */
    static const u8 busyKick[16] = {100, 0, 0, 80, 0, 0, 0, 0, 0, 0, 95, 70, 0, 0, 0, 0};
    static const u8 busySnare[16] = {0, 0, 0, 0, 95, 0, 0, 30, 0, 26, 0, 0, 95, 0, 0, 32};
    /* The plain bars: the kick on 1 and on the "and" of 3, the snare on 2 and 4, the hats on the eighths. */
    static const u8 plainKick[16] = {100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 90, 0, 0, 0, 0, 0};
    static const u8 plainSnare[16] = {0, 0, 0, 0, 90, 0, 0, 0, 0, 0, 0, 0, 90, 0, 0, 0};
    int plain = bar < 4 || bar == 8 || bar == 9;
    *kick = plain ? plainKick[step] : busyKick[step];
    *snare = plain ? plainSnare[step] : busySnare[step];
    /* Sixteenths, with a swing in their velocity: the eighths are louder than the sixteenths between them. */
    *hat = plain ? (step % 2 == 0 ? 26 : 0) : (step % 2 == 0 ? 30 : step % 4 == 1 ? 16 : 21);
    if (bar == 11) {
        /* Half time: the snare on 3 only, the kick on 1 and the "and" of 3. */
        *kick = step == 0 ? 100 : step == 10 ? 80 : 0;
        *snare = step == 8 ? 100 : 0;
        *hat = step % 4 == 0 ? 28 : 0;
    } else if (bar == 13) {
        /* Double time: a snare on every "and", the kick on the beats. */
        *kick = step % 4 == 0 ? 90 : 0;
        *snare = step % 4 == 2 ? (step == 6 || step == 14 ? 95 : 60) : 0;
    }
    if ((bar == 7 || bar == 15) && step >= 12) {
        /* A roll that ends the phrase: a snare on each sixteenth, louder each time. */
        *kick = 0;
        *snare = 50 + (s32)(step - 12) * 15;
        *hat = 0;
    }
}

static void laugh(s32 pitch, s32 volume, s32 pan) {
    s32 handle = MusStartEffect2(LAUGH_EFFECT, volume, pan, 0, 1);
    if (handle != 0) {
        MusHandleSetFreqOffset(handle, (f32)pitch);
        /* A lower laugh is a slower one too, as from a slowed tape: 128 is its own speed. */
        MusHandleSetTempo(handle, pitch < 0 ? 96 : 128);
        MusHandleSetReverb(handle, 60);
    }
}

static void wait_for(u32 step, s32 what, s32 pan, s32 note, s32 velocity) {
    s32 i;
    for (i = 0; i < LATER; i++) {
        if (later[i].what == 0) {
            later[i].step = step;
            later[i].what = what;
            later[i].pan = pan;
            later[i].note = note;
            later[i].velocity = velocity;
            return;
        }
    }
}

/* A gull's call: the wave at its own pitch plus up, on side pan, with an echo 3 sixteenths later on the other side. */
static void gull(s32 handle, u32 step, s32 up, s32 pan) {
    s32 note = GULL_PITCH - (s8)hitTemplate[HIT_GULL].transpose + up;
    play_hit(HIT_GULL, handle, GULL_WAVE, note, 48, 90, pan);
    wait_for(step + 3, 2, 127 - pan, note, 45);
}

static void remix_frame(void) {
    MusChannel *ch = NULL;
    s32 i, kick, snare, hat;
    u32 step, bar, pos;
    /* The song's clock: its first channel that plays. */
    for (i = 0; i < musNumChannels - HITS; i++) {
        if (musChannels[i].pdata != NULL && musChannels[i].song != NULL && musChannels[i].handle == audioCtrl.handle &&
            !(musChannels[i].flags & MUS_CHANNEL_PAUSED)) {
            ch = &musChannels[i];
            break;
        }
    }
    if (ch == NULL || ch->stopCount != -1) {
        return;
    }
    /* A tick is 1/48 beat; the clock counts 1/256 ticks. A sixteenth is 12 ticks. */
    step = (ch->time + ch->tempoStep) / (12 * 256);
    if (step == lastStep) {
        return;
    }
    lastStep = step;
    bar = (step / 16) % 16;
    pos = step % 16;
    break_hits(bar, pos, &kick, &snare, &hat);
    if (kick) {
        play_hit(HIT_KICK, ch->handle, -1, 36, 24, kick, -1);
    }
    if (snare) {
        play_hit(HIT_SNARE, ch->handle, -1, 36, 12, snare, -1);
    }
    if (hat) {
        play_hit(HIT_HAT, ch->handle, -1, 36, 6, hat, -1);
    }
    /* Cross-Stitch laughs on the last beat of bar 7, as in the game, and of bar 15, lower and slower with an echo. */
    if (bar == 7 && pos == 12) {
        laugh(0, 200, 64);
    } else if (bar == 15 && pos == 12) {
        laugh(-7, 210, 40);
        wait_for(step + 3, 1, 88, 0, 120);
    }
    /* Gulls: in bar 3 on the left at their own pitch, in bar 10 on the right a fourth higher, in bar 13 on both
       sides. */
    if (bar == 3 && pos == 8) {
        gull(ch->handle, step, 0, 20);
    } else if (bar == 10 && pos == 0) {
        gull(ch->handle, step, 5, 108);
    } else if (bar == 13 && pos == 4) {
        gull(ch->handle, step, 2, 30);
    } else if (bar == 13 && pos == 12) {
        gull(ch->handle, step, 7, 98);
    }
    for (i = 0; i < LATER; i++) {
        if (later[i].what != 0 && later[i].step <= step) {
            if (later[i].what == 1) {
                laugh(-7, later[i].velocity, later[i].pan);
            } else {
                play_hit(HIT_ECHO, ch->handle, GULL_WAVE, later[i].note, 48, later[i].velocity, later[i].pan);
            }
            later[i].what = 0;
        }
    }
}

static void remix_reset(void) {
    memset(hitHave, 0, sizeof hitHave);
    memset(later, 0, sizeof later);
    lastStep = 0xFFFFFFFFu;
}

void g64_remix_mode(int on) {
    remixOn = on != 0;
    musRemixNote = remixOn ? remix_note : NULL;
    musRemixFrame = remixOn ? remix_frame : NULL;
    if (remixOn && hitStream == NULL) {
        hitStream = (u8 *)JallocAlloc(4 * HITS, 1, "Remix");
    }
    remix_reset();
}

/* After the song has started: the remix's tempo and reverb, or the song's own. */
void g64_remix_song_started(void) {
    if (audioCtrl.handle != 0) {
        MusHandleSetTempo(audioCtrl.handle, remixOn ? TEMPO : MUS_TEMPO_SCALE_ONE);
        if (remixOn) {
            MusHandleSetReverb(audioCtrl.handle, REVERB);
        }
    }
    remix_reset();
}
PORT_C_END
