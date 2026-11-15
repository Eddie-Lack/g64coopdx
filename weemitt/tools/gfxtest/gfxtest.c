/* Draws one game frame from a memory dump with the native renderer (src/gfx) and writes it as a PNG.

   Usage: gfxtest DUMP OUT.png [--size WxH] [--window] [--repeat N]
     DUMP       the dump's path without its extension: dump.mjs wrote DUMP.mem and DUMP.txt
     --size     the window's drawable size (default 1280x960)
     --window   write the whole window, black bars included, instead of the 4:3 frame
     --repeat   draw the frame N times and print the time per frame

   The renderer reads the display lists from the dump: gfx_set_memory_base makes the dump's offsets (WebAssembly
   addresses) point into it. The environment variables of src/gfx/gfx.h work here too.

   The steps, with a browser port of Glover built (it is not public):
     node tools/gfxtest/dump.mjs ROM 10 DIR --frames 30 90     DIR/L10-30.mem, .txt and so on; keep DIR outside
                                                                the repository: dumps hold game data
     tools/gfxtest/build.sh                                     build/gfxtest/gfxtest
     DISPLAY=:99 build/gfxtest/gfxtest DIR/L10-30 out.png       a virtual X display (Xvfb) works
   The browser port's screenshot tool takes the browser renderer's pictures of the same frames. */
#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"

#define MAX_TASKS 16

#if defined(_WIN32)
#define GL_CALL __stdcall
#else
#define GL_CALL
#endif

typedef void(GL_CALL *ReadPixelsFn)(int, int, int, int, unsigned int, unsigned int, void *);

static unsigned char *read_file(const char *path, long *size) {
    FILE *f = fopen(path, "rb");
    unsigned char *data;

    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc(*size);
    if (data == NULL || fread(data, 1, *size, f) != (size_t)*size) {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    return data;
}

/* ---- A PNG writer with stored (uncompressed) deflate blocks. */

static uint32_t crcTable[256];

static uint32_t crc(uint32_t c, const unsigned char *p, size_t n) {
    size_t i;

    if (crcTable[1] == 0) {
        uint32_t k;
        for (k = 0; k < 256; k++) {
            uint32_t v = k;
            int j;
            for (j = 0; j < 8; j++) {
                v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            }
            crcTable[k] = v;
        }
    }
    c = ~c;
    for (i = 0; i < n; i++) {
        c = crcTable[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    }
    return ~c;
}

static void put32(unsigned char *p, uint32_t v) {
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

static void chunk(FILE *f, const char *type, const unsigned char *data, uint32_t n) {
    unsigned char b[4];
    uint32_t c = crc(0, (const unsigned char *)type, 4);

    put32(b, n);
    fwrite(b, 1, 4, f);
    fwrite(type, 1, 4, f);
    fwrite(data, 1, n, f);
    put32(b, crc(c, data, n));
    fwrite(b, 1, 4, f);
}

/* rgba: rows from the top. Writes an RGB image. */
static int write_png(const char *path, const unsigned char *rgba, int w, int h) {
    size_t raw = (size_t)(w * 3 + 1) * h;
    size_t blocks = (raw + 65534) / 65535;
    unsigned char *z = malloc(2 + raw + blocks * 5 + 4);
    unsigned char *rows = malloc(raw);
    unsigned char ihdr[13];
    uint32_t a = 1;
    uint32_t b = 0;
    size_t i;
    size_t o = 0;
    FILE *f;
    int x;
    int y;

    if (z == NULL || rows == NULL) {
        return 0;
    }
    for (y = 0; y < h; y++) {
        unsigned char *r = rows + (size_t)y * (w * 3 + 1);
        r[0] = 0;
        for (x = 0; x < w; x++) {
            memcpy(r + 1 + x * 3, rgba + ((size_t)y * w + x) * 4, 3);
        }
    }
    z[o++] = 0x78;
    z[o++] = 0x01;
    for (i = 0; i < raw; i += 65535) {
        size_t n = raw - i < 65535 ? raw - i : 65535;
        z[o++] = i + n == raw ? 1 : 0;
        z[o++] = n & 0xFF;
        z[o++] = n >> 8;
        z[o++] = ~n & 0xFF;
        z[o++] = (~n >> 8) & 0xFF;
        memcpy(z + o, rows + i, n);
        o += n;
    }
    for (i = 0; i < raw; i++) {
        a = (a + rows[i]) % 65521;
        b = (b + a) % 65521;
    }
    put32(z + o, (b << 16) | a);
    o += 4;

    f = fopen(path, "wb");
    if (f == NULL) {
        return 0;
    }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    put32(ihdr, w);
    put32(ihdr + 4, h);
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 2;  /* RGB */
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)o);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(z);
    free(rows);
    return 1;
}

int main(int argc, char **argv) {
    const char *dump = NULL;
    const char *out = NULL;
    int width = 1280;
    int height = 960;
    int window = 0;
    int repeat = 1;
    char path[1024];
    unsigned char *memory;
    long memSize;
    FILE *f;
    unsigned int cfb0;
    unsigned int cfb1;
    GfxTask tasks[MAX_TASKS];
    int count = 0;
    SDL_Window *win;
    SDL_GLContext gl;
    unsigned char *pixels;
    int w;
    int h;
    int i;
    Uint64 t0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            sscanf(argv[++i], "%dx%d", &width, &height);
        } else if (strcmp(argv[i], "--window") == 0) {
            window = 1;
        } else if (strcmp(argv[i], "--repeat") == 0 && i + 1 < argc) {
            repeat = atoi(argv[++i]);
        } else if (dump == NULL) {
            dump = argv[i];
        } else {
            out = argv[i];
        }
    }
    if (dump == NULL || out == NULL) {
        fprintf(stderr, "usage: gfxtest DUMP OUT.png [--size WxH] [--window] [--repeat N]\n");
        return 2;
    }

    snprintf(path, sizeof(path), "%s.mem", dump);
    memory = read_file(path, &memSize);
    if (memory == NULL) {
        fprintf(stderr, "cannot read %s\n", path);
        return 1;
    }
    snprintf(path, sizeof(path), "%s.txt", dump);
    f = fopen(path, "r");
    if (f == NULL || fscanf(f, "%u %u", &cfb0, &cfb1) != 2) {
        fprintf(stderr, "cannot read %s\n", path);
        return 1;
    }
    while (count < MAX_TASKS && fscanf(f, "%u %u %d", &tasks[count].addr, &tasks[count].size,
                                       &tasks[count].sprite2d) == 3) {
        count++;
    }
    fclose(f);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    win = SDL_CreateWindow("gfxtest", 0, 0, width, height, SDL_WINDOW_OPENGL);
    if (win == NULL) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    gl = SDL_GL_CreateContext(win);
    if (gl == NULL) {
        fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
        return 1;
    }
    gfx_init();
    gfx_set_memory_base((unsigned long)(uintptr_t)memory);
    gfx_set_memory_range(1, (unsigned int)memSize);
    gfx_set_framebuffers(cfb0, cfb1);

    t0 = SDL_GetPerformanceCounter();
    for (i = 0; i < repeat; i++) {
        gfx_draw_frame(tasks, count, width, height);
    }
    if (window) {
        ReadPixelsFn readPixels = (ReadPixelsFn)SDL_GL_GetProcAddress("glReadPixels");
        unsigned char *flipped = malloc((size_t)width * height * 4);
        w = width;
        h = height;
        pixels = malloc((size_t)width * height * 4);
        readPixels(0, 0, width, height, 0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */, flipped);
        for (i = 0; i < height; i++) {
            memcpy(pixels + (size_t)i * width * 4, flipped + (size_t)(height - 1 - i) * width * 4, width * 4);
        }
        free(flipped);
    } else {
        pixels = gfx_read_frame(&w, &h);
    }
    if (repeat > 1) {
        printf("%.3f ms per frame\n",
               (SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency() / repeat);
    }
    if (pixels == NULL || !write_png(out, pixels, w, h)) {
        fprintf(stderr, "no picture\n");
        return 1;
    }
    printf("%s: %dx%d\n", out, w, h);
    free(pixels);
    gfx_shutdown();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(win);
    SDL_Quit();
    free(memory);
    return 0;
}
