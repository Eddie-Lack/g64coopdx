/* g64probe: a throwaway test of the "separate program" option. It loads weemitt's library (libglover.so) at run
   time, starts a level and steps the game. With two library files it loads both copies into one process and runs
   two games side by side with the same input, then compares their memory hashes.

   Build: gcc -O2 -I WEEMITT/src/lib g64probe.c -o g64probe -ldl
          x86_64-w64-mingw32-gcc -O2 -I WEEMITT/src/lib g64probe.c -o g64probe.exe
   Run:   ./g64probe ROM LEVEL FRAMES LIB [LIB2] */
#ifdef _WIN32
#include <windows.h>
#define dlopen(p, f) ((void *)LoadLibraryA(p))
#define dlsym(h, n) ((void *)GetProcAddress((HMODULE)(h), n))
#define dlerror() "LoadLibrary failed"
#else
#include <dlfcn.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "glover.h"

typedef struct Api {
    void *h;
    void (*default_config)(GloverConfig *);
    int32_t (*create)(const GloverConfig *, Glover **, char *, uint32_t);
    int32_t (*update)(Glover *, float, const GloverInput *, GloverFrame *);
    int32_t (*bodies)(Glover *, GloverBodies *);
    uint32_t (*hash)(Glover *);
    int32_t (*memory)(Glover *, GloverMemory *);
    const char *(*build_id)(void);
} Api;

static int load(Api *a, const char *path) {
    a->h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!a->h) { fprintf(stderr, "dlopen %s: %s\n", path, dlerror()); return 0; }
#define SYM(field, name) *(void **)&a->field = dlsym(a->h, name); if (!a->field) return 0
    SYM(default_config, "glover_default_config");
    SYM(create, "glover_create");
    SYM(update, "glover_update");
    SYM(bodies, "glover_bodies");
    SYM(hash, "glover_hash_memory");
    SYM(memory, "glover_memory");
    SYM(build_id, "glover_build_id");
    return 1;
}

static unsigned char *read_file(const char *p, long *n) {
    FILE *f = fopen(p, "rb");
    unsigned char *b;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc(*n);
    if (fread(b, 1, *n, f) != (size_t)*n) { fclose(f); free(b); return NULL; }
    fclose(f);
    return b;
}

/* A fixed, varied input: the stick turns slowly, A and B are tapped now and then. */
static void input_for(int frame, GloverInput *in) {
    int phase = (frame / 40) % 8;
    static const int sx[8] = { 0, 60, 80, 40, 0, -40, -80, -60 };
    static const int sy[8] = { 80, 60, 0, -40, -80, -60, 0, 60 };
    memset(in, 0, sizeof(*in));
    in->stickX = sx[phase];
    in->stickY = sy[phase];
    if (frame % 37 < 3) in->buttons |= GLOVER_BTN_A;
    if (frame % 53 < 2) in->buttons |= GLOVER_BTN_B;
}

int main(int argc, char **argv) {
    Api api[2];
    Glover *g[2] = { 0 };
    int n = argc > 5 ? 2 : 1, i, f, frames, level, diverged = -1;
    long romSize;
    unsigned char *rom;
    char err[256];
    struct timespec t0, t1;
    double ms;

    if (argc < 5) { fprintf(stderr, "usage: g64probe ROM LEVEL FRAMES LIB [LIB2]\n"); return 2; }
    rom = read_file(argv[1], &romSize);
    level = atoi(argv[2]);
    frames = atoi(argv[3]);
    if (!rom) { fprintf(stderr, "cannot read the ROM\n"); return 2; }
    for (i = 0; i < n; i++) {
        GloverConfig c;
        if (!load(&api[i], argv[4 + i])) return 1;
        api[i].default_config(&c);
        c.rom = rom;
        c.romSize = (uint32_t)romSize;
        c.eeprom = NULL;
        c.startLevel = level;
        if (api[i].create(&c, &g[i], err, sizeof err) != GLOVER_OK) {
            fprintf(stderr, "game %d: glover_create failed: %s\n", i, err);
            return 1;
        }
        printf("game %d: %s, library %s\n", i, api[i].build_id(), argv[4 + i]);
    }
    {   /* A second game from the same library file must be refused (one game per library copy). */
        GloverConfig c;
        Glover *x = NULL;
        int32_t r;
        api[0].default_config(&c);
        c.rom = rom; c.romSize = (uint32_t)romSize; c.startLevel = level;
        r = api[0].create(&c, &x, err, sizeof err);
        printf("second glover_create on the same copy: %d (GLOVER_ERR_BUSY is %d)\n", r, GLOVER_ERR_BUSY);
    }
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (f = 0; f < frames; f++) {
        GloverInput in;
        input_for(f, &in);
        for (i = 0; i < n; i++) {
            GloverFrame fr;
            memset(&fr, 0, sizeof fr);
            fr.size = sizeof fr;
            if (api[i].update(g[i], 1.0f, &in, &fr) != GLOVER_OK) { fprintf(stderr, "update failed\n"); return 1; }
        }
        if (n == 2 && diverged < 0 && f % 10 == 0 && api[0].hash(g[0]) != api[1].hash(g[1])) diverged = f;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    ms = (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
    for (i = 0; i < n; i++) {
        GloverBodies b;
        GloverMemory m;
        memset(&b, 0, sizeof b);
        b.size = sizeof b;
        api[i].bodies(g[i], &b);
        memset(&m, 0, sizeof m);
        m.size = sizeof m;
        api[i].memory(g[i], &m);
        printf("game %d after %d frames: glove %.2f %.2f %.2f, ball %.2f %.2f %.2f, action %d, held %d, hash %08x, "
               "memory at %p\n", i, frames, b.hand[0], b.hand[1], b.hand[2], b.ball[0], b.ball[1], b.ball[2],
               b.handAction, b.ballHeld, api[i].hash(g[i]), (void *)m.ram);
    }
    printf("%d frames x %d games in %.1f ms (%.3f ms a game frame, with hashes every 10 frames)\n", frames, n, ms,
           ms / frames / n);
    if (n == 2) printf("the two games %s\n", diverged < 0 ? "kept equal hashes at every 10th frame" : "diverged");
    return 0;
}
