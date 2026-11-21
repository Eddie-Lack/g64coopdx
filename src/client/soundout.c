/* The online game's audible sound (soundout.h). */
#include "soundout.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

#include "../../weemitt/src/lib/glover.h"

void plog_printf(const char *fmt, ...);

#ifdef _WIN32
#define SOUND_LIB "g64coopdx-sound.dll"
#else
#define SOUND_LIB "g64coopdx-sound.so"
#endif

/* The samples the player keeps ready ahead of the device: about 150 ms. */
#define LEAD_SAMPLES 3300

static void *lib;
static Glover *game;
static SDL_AudioDeviceID device;
static int rate;
static double advanced;         /* retraces given to the player */
static SDL_atomic_t consumed;   /* samples the device has taken */
static SDL_atomic_t volume;     /* the master volume, percent */
static g64_sound_backend backend;

static int32_t (*f_default)(GloverConfig *);
static int32_t (*f_create)(const GloverConfig *, Glover **, char *, uint32_t);
static int32_t (*f_destroy)(Glover *);
static int32_t (*f_sound)(Glover *, const GloverSoundCmd *, uint32_t, int32_t *);
static int32_t (*f_advance)(Glover *, float);
static int32_t (*f_read)(Glover *, int16_t *, int32_t);
static int32_t (*f_rate)(Glover *);


static void SDLCALL callback(void *user, Uint8 *stream, int len) {
    int16_t *out = (int16_t *)stream;
    int frames = len / 4, got = 0, v = SDL_AtomicGet(&volume), i;
    (void)user;
    while (got < frames) {
        int n = f_read(game, out + got * 2, frames - got);
        if (n <= 0)
            break;
        got += n;
    }
    /* The player has fallen behind: silence, not old samples. */
    memset(out + got * 2, 0, (size_t)(frames - got) * 4);
    if (v < 100)
        for (i = 0; i < frames * 2; i++)
            out[i] = (int16_t)(out[i] * v / 100);
    SDL_AtomicAdd(&consumed, frames);
}

static void run(GloverSoundCmd *c, int32_t *handle) {
    int32_t h = 0;
    if (game && f_sound(game, c, 1, &h) == 0 && handle)
        *handle = h;
}

static int b_start(void *user, int id, int vol, int pan, int pitch, int reverb) {
    GloverSoundCmd c;
    int32_t h = 0;
    (void)user;
    memset(&c, 0, sizeof c);
    c.op = GLOVER_SOUND_EFFECT;
    c.a = id;
    c.b = vol;
    c.c = pan;
    c.e = pitch;
    c.f = reverb;
    c.freq = (float)(pitch - 128) / 10.0f;
    run(&c, &h);
    return h;
}

static void b_set(void *user, int handle, int vol, int pan, int pitch, int reverb) {
    GloverSoundCmd c;
    (void)user;
    memset(&c, 0, sizeof c);
    c.op = GLOVER_SOUND_SET;
    c.a = handle;
    c.b = vol;
    c.c = pan;
    c.e = pitch;
    c.f = reverb;
    c.freq = (float)(pitch - 128) / 10.0f;
    run(&c, NULL);
}

static void b_stop(void *user, int handle) {
    GloverSoundCmd c;
    (void)user;
    memset(&c, 0, sizeof c);
    c.op = GLOVER_SOUND_STOP;
    c.a = handle;
    run(&c, NULL);
}

static void b_song(void *user, int song) {
    GloverSoundCmd c;
    (void)user;
    memset(&c, 0, sizeof c);
    c.op = GLOVER_SOUND_SONG;
    c.a = song;
    run(&c, NULL);
}

static void b_master(void *user, int music, int effects) {
    GloverSoundCmd c;
    (void)user;
    memset(&c, 0, sizeof c);
    c.op = GLOVER_SOUND_MASTER;
    if (music >= 0) {
        c.a = 2;
        c.b = music;
        run(&c, NULL);
    }
    if (effects >= 0) {
        c.a = 1;
        c.b = effects;
        run(&c, NULL);
    }
}

static void *load(const char *path) { return SDL_LoadObject(path); }

static void *sym(const char *name) { return SDL_LoadFunction(lib, name); }

int soundout_open(const unsigned char *rom, unsigned long romSize) {
    char path[1024], err[256];
    const char *env = SDL_getenv("G64_SOUND_LIB");
    char *base = SDL_GetBasePath();
    GloverConfig c;
    SDL_AudioSpec want, have;
    GloverSoundCmd stop;
    snprintf(path, sizeof path, "%s%s", env ? "" : (base ? base : ""), env ? env : SOUND_LIB);
    SDL_free(base);
    lib = load(path);
    if (!lib) {
        plog_printf("g64coopdx: no sound: cannot load %s", path);
        return -1;
    }
    f_default = (int32_t (*)(GloverConfig *))sym("glover_default_config");
    f_create = (int32_t (*)(const GloverConfig *, Glover **, char *, uint32_t))sym("glover_create");
    f_destroy = (int32_t (*)(Glover *))sym("glover_destroy");
    f_sound = (int32_t (*)(Glover *, const GloverSoundCmd *, uint32_t, int32_t *))sym("glover_sound");
    f_advance = (int32_t (*)(Glover *, float))sym("glover_sound_advance");
    f_read = (int32_t (*)(Glover *, int16_t *, int32_t))sym("glover_audio_read");
    f_rate = (int32_t (*)(Glover *))sym("glover_audio_rate");
    if (!f_default || !f_create || !f_destroy || !f_sound || !f_advance || !f_read || !f_rate) {
        plog_printf("g64coopdx: no sound: %s has no sound functions", path);
        return -1;
    }
    f_default(&c);
    c.rom = rom;
    c.romSize = (uint32_t)romSize;
    c.startLevel = -1;
    if (f_create(&c, &game, err, sizeof err) != 0) {
        plog_printf("g64coopdx: no sound: %s", err);
        game = NULL;
        return -1;
    }
    /* The library's game started like a new game: nothing of it plays. */
    memset(&stop, 0, sizeof stop);
    stop.op = GLOVER_SOUND_STOP_ALL;
    stop.a = 3;
    run(&stop, NULL);
    rate = f_rate(game);
    if (rate <= 0 || SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        plog_printf("g64coopdx: no sound device: %s", SDL_GetError());
        soundout_close();
        return -1;
    }
    SDL_zero(want);
    want.freq = rate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = callback;
    SDL_AtomicSet(&volume, 100);
    SDL_AtomicSet(&consumed, 0);
    advanced = 0;
    device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (device == 0) {
        plog_printf("g64coopdx: no sound device: %s", SDL_GetError());
        soundout_close();
        return -1;
    }
    backend.start = b_start;
    backend.set = b_set;
    backend.stop = b_stop;
    backend.song = b_song;
    backend.master = b_master;
    soundout_tick();
    SDL_PauseAudioDevice(device, 0);
    plog_printf("g64coopdx: sound from %s at %d Hz", path, rate);
    return 0;
}

void soundout_close(void) {
    if (device) {
        SDL_CloseAudioDevice(device);
        device = 0;
    }
    if (game) {
        f_destroy(game);
        game = NULL;
    }
    memset(&backend, 0, sizeof backend);
}

const g64_sound_backend *soundout_backend(void) { return game && device ? &backend : NULL; }

void soundout_tick(void) {
    double made, want;
    if (!game)
        return;
    /* The player makes rate / 60 samples per retrace. It stays LEAD_SAMPLES ahead of the device. */
    made = advanced * rate / 60.0;
    want = (double)SDL_AtomicGet(&consumed) + LEAD_SAMPLES;
    if (made < want) {
        double r = (want - made) * 60.0 / rate;
        if (r > 60.0)
            r = 60.0;
        if (f_advance(game, (float)r) == 0)
            advanced += r;
    }
}

void soundout_volumes(int master, int music, int effects) {
    GloverSoundCmd c;
    SDL_AtomicSet(&volume, master < 0 ? 0 : master > 100 ? 100 : master);
    memset(&c, 0, sizeof c);
    c.op = GLOVER_SOUND_GAIN;
    c.a = music;
    c.b = effects;
    run(&c, NULL);
}
