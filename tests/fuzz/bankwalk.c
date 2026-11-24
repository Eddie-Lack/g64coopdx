/* The shadow walk (bankwalk.h). Each function names the game's function it copies. The copies keep the game's
 * order of reads and writes, and its integer types where they matter (an s16 loop counter, an s16 or s32 count).
 * Pointers are words, as on the port's 64-bit builds: a fixed pointer is an offset in the bank plus BASE, and a
 * texture's texels are an offset in the texture bank plus TEXBASE. Words stay in the host's byte order after the
 * converters, as in the port. */
#include "bankwalk.h"

#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#define BASE 0x40000000u
#define TEXBASE 0x20000000u
#define G_VTX 0x04
#define G_ENDDL 0xB8

typedef struct {
    uint8_t *b;
    int64_t n;
    int kind;
} Mem;

static jmp_buf stop;
static const char *why;
static int64_t whyAt;
static int whyBank;

static Mem T, O;
static uint8_t *converted; /* the port's bitmap: one bit per 2 bytes of the object bank */
static uint8_t *changed;   /* one bit per word of the object bank: the loaders changed it */
static int numCycling;

static void fault(const Mem *m, int64_t at, const char *what) {
    why = what;
    whyAt = at;
    whyBank = m->kind;
    longjmp(stop, 1);
}

static uint8_t *acc(const Mem *m, int64_t at, int64_t len) {
    if (at < 0 || len < 0 || at > m->n || len > m->n - at)
        fault(m, at, "an access outside the bank");
    return m->b + at;
}

static uint32_t rd32(const Mem *m, int64_t at) {
    uint32_t v;
    memcpy(&v, acc(m, at, 4), 4);
    return v;
}

static uint16_t rd16(const Mem *m, int64_t at) {
    uint16_t v;
    memcpy(&v, acc(m, at, 2), 2);
    return v;
}

static void wr32(const Mem *m, int64_t at, uint32_t v) {
    memcpy(acc(m, at, 4), &v, 4);
}

static void swap16(uint8_t *p) {
    uint8_t t = p[0];
    p[0] = p[1];
    p[1] = t;
}

static void swap32(uint8_t *p) {
    uint8_t t = p[0];
    p[0] = p[3];
    p[3] = t;
    t = p[1];
    p[1] = p[2];
    p[2] = t;
}

/* ---- The structures' scalar fields: offset and size pairs, as weemitt's gen/layouts.c lists them. */

typedef struct {
    uint16_t size, count;
    const uint16_t *units;
} Layout;

static const uint16_t uTexture[] = {0, 4, 6, 2, 8, 2, 10, 2, 12, 2, 14, 2, 16, 2,
                                    18, 2, 20, 4, 24, 2, 26, 2, 28, 4, 32, 4};
static const uint16_t uEntry[] = {0, 4, 4, 4};
static const uint16_t uInstance[] = {12, 4, 24, 4};
static const uint16_t uAnimState[] = {0, 2, 2, 2, 4, 4, 8, 4, 12, 4, 16, 2, 18, 2, 20, 2, 22, 2, 24, 2, 28, 4, 32, 4,
                                      36, 4, 40, 4, 44, 4, 48, 4, 52, 4, 56, 4, 60, 4, 64, 4, 68, 2, 72, 4, 76, 4};
static const uint16_t uAnimInfo[] = {0, 2, 2, 2, 4, 4, 8, 4};
static const uint16_t uObject[] = {0, 4, 14, 2, 16, 2, 18, 2, 20, 4, 24, 4, 28, 4, 32, 4,
                                   36, 4, 40, 4, 44, 4, 50, 2, 52, 4, 56, 4, 60, 4};
static const uint16_t uMesh[] = {0, 2, 2, 2, 4, 4, 8, 4, 12, 4, 16, 4, 20, 4, 24, 4, 28, 4, 32, 4};
static const uint16_t uSprite[] = {0, 4, 4, 4, 8, 2, 10, 2, 12, 2, 14, 2, 16, 2, 22, 2};
static const uint16_t uKey[] = {0, 4, 4, 4, 8, 4, 12, 4, 16, 4};
static const uint16_t uVtx[] = {0, 2, 2, 2, 4, 2, 6, 2, 8, 2, 10, 2};

static const Layout lTexture = {36, 13, uTexture}, lEntry = {8, 2, uEntry}, lInstance = {28, 2, uInstance},
                    lAnimState = {80, 23, uAnimState}, lAnimInfo = {12, 4, uAnimInfo}, lObject = {64, 15, uObject},
                    lMesh = {36, 10, uMesh}, lSprite = {24, 8, uSprite}, lKey = {20, 5, uKey}, lVtx = {16, 6, uVtx};

/* ---- port_convert_texture_bank (src/port/loaders.c). */

static void convert_texture_bank(void) {
    uint32_t count, i;
    int64_t p;

    swap32(acc(&T, 0, 4));
    count = rd32(&T, 0);
    p = 4;
    for (i = 0; i < count && p + 36 <= T.n; i++) {
        int32_t size;
        uint32_t palette, k;
        uint16_t siz;
        int64_t data, end, pal;
        for (k = 0; k < lTexture.count; k++) {
            uint8_t *u = acc(&T, p + lTexture.units[2 * k], lTexture.units[2 * k + 1]);
            if (lTexture.units[2 * k + 1] == 2)
                swap16(u);
            else
                swap32(u);
        }
        size = (int32_t)rd32(&T, p + 20);
        if (size <= 0 || p + size > T.n)
            break;
        data = p + 36;
        end = p + size;
        palette = rd32(&T, p + 32);
        pal = palette ? p + palette : -1;
        if (pal != -1 && (pal < data || pal > end))
            pal = -1;
        siz = rd16(&T, p + 26);
        if (siz == 2) {
            int64_t n = ((pal != -1 ? pal : end) - data) / 2;
            for (k = 0; k < n; k++)
                swap16(acc(&T, data + 2 * k, 2));
        } else if (siz == 3) {
            int64_t n = ((pal != -1 ? pal : end) - data) / 4;
            for (k = 0; k < n; k++)
                swap32(acc(&T, data + 4 * k, 4));
        }
        if (pal != -1) {
            int64_t n = (end - pal) / 2;
            for (k = 0; k < n; k++)
                swap16(acc(&T, pal + 2 * k, 2));
        }
        p = end;
    }
}

/* ---- LoadTextureBankAt and FindTexture (src/game/banks.c), with the pack's bank in the only slot. */

static void load_texture_bank(void) {
    int32_t count = (int32_t)rd32(&T, 0);
    int64_t cur = 4;
    int32_t i;

    /* The game's counter is an s16. */
    if (count > 32767)
        fault(&T, 0, "a texture count past the loader's s16 counter");
    for (i = 0; i < count; i++) {
        int64_t tex = cur;
        uint32_t palette;
        wr32(&T, tex + 28, TEXBASE + (uint32_t)(tex + 36));
        palette = rd32(&T, tex + 32);
        if (palette)
            wr32(&T, tex + 32, TEXBASE + (uint32_t)(tex + palette));
        cur += (int32_t)rd32(&T, tex + 20);
        if (rd16(&T, tex + 6) & 4) {
            if (numCycling >= 20)
                fault(&T, tex + 6, "more than 20 cycling textures");
            numCycling++;
        }
    }
}

/* The record of a texture id, or -1. */
static int64_t find_texture(uint32_t id) {
    int32_t count = (int32_t)rd32(&T, 0), j;
    int64_t p = 4;

    for (j = 0; j < count; j++) {
        if (j > (1 << 20))
            fault(&T, p, "a texture search that does not end");
        if (rd32(&T, p) == id)
            return p;
        p += (int32_t)rd32(&T, p + 20);
    }
    return -1;
}

/* ---- port_convert_object_bank (src/port/loaders.c). Its bitmap is a static array of 1 MiB. */

static int64_t at24(uint32_t ptr) {
    uint32_t off = ptr & 0xFFFFFF;
    if (ptr == 0 || off >= (O.n < 0x1000000 ? O.n : 0x1000000))
        return -1;
    return off;
}

static int is_converted(int64_t p) {
    int64_t i = p >> 1;
    if ((i >> 3) >= (1 << 20))
        fault(&O, p, "the converter reads past its bitmap");
    return (converted[i >> 3] >> (i & 7)) & 1;
}

static void swap_unit(int64_t p, uint32_t size) {
    int64_t i = p >> 1, k, n = size >> 1;
    if (p + size > O.n || (p & 1))
        return;
    for (k = 0; k < n; k++)
        if ((converted[(i + k) >> 3] >> ((i + k) & 7)) & 1)
            return;
    for (k = 0; k < n; k++)
        converted[(i + k) >> 3] |= (uint8_t)(1 << ((i + k) & 7));
    if (size == 2)
        swap16(O.b + p);
    else
        swap32(O.b + p);
}

static void swap_struct(int64_t p, const Layout *l) {
    uint32_t i;
    if (p < 0)
        return;
    for (i = 0; i < l->count; i++)
        swap_unit(p + l->units[2 * i], l->units[2 * i + 1]);
}

/* The port loops count times. Past the end of the bank its swaps do nothing, so this stops there. */
static void swap_structs(int64_t p, int32_t count, const Layout *l) {
    int32_t i;
    for (i = 0; p >= 0 && i < count && p + (int64_t)i * l->size < O.n; i++)
        swap_struct(p + (int64_t)i * l->size, l);
}

static void swap_units(int64_t p, int32_t count, uint32_t size) {
    int32_t i;
    for (i = 0; p >= 0 && i < count && p + (int64_t)i * size < O.n; i++)
        swap_unit(p + (int64_t)i * size, size);
}

static void convert_display_list(int64_t dl) {
    while (dl >= 0 && dl + 8 <= O.n && !is_converted(dl)) {
        uint32_t w0, w1;
        swap_unit(dl, 4);
        swap_unit(dl + 4, 4);
        w0 = rd32(&O, dl);
        w1 = rd32(&O, dl + 4);
        if ((w0 >> 24) == G_VTX)
            swap_structs(at24(w1), (w0 >> 10) & 0x3F, &lVtx);
        if ((w0 >> 24) == G_ENDDL)
            break;
        dl += 8;
    }
}

static void convert_object(int64_t obj, int depth) {
    if (depth > 100000)
        fault(&O, obj, "the converter recurses without end");
    while (obj >= 0 && !is_converted(obj)) {
        int64_t mesh;
        swap_struct(obj, &lObject);
        mesh = at24(rd32(&O, obj + 0x14));
        if (mesh >= 0 && !is_converted(mesh)) {
            int16_t nf;
            swap_struct(mesh, &lMesh);
            swap_units(at24(rd32(&O, mesh + 4)), (int16_t)rd16(&O, mesh + 2) * 3, 4);
            nf = (int16_t)rd16(&O, mesh);
            if (nf > 0) {
                swap_units(at24(rd32(&O, mesh + 8)), nf * 3, 2);
                swap_units(at24(rd32(&O, mesh + 16)), nf * 6, 2);
                swap_units(at24(rd32(&O, mesh + 32)), nf, 4);
            }
        }
        convert_display_list(at24(rd32(&O, obj + 0x18)));
        swap_structs(at24(rd32(&O, obj + 0x1C)), (int16_t)rd16(&O, obj + 0x0E), &lKey);
        swap_structs(at24(rd32(&O, obj + 0x20)), (int16_t)rd16(&O, obj + 0x10), &lKey);
        swap_structs(at24(rd32(&O, obj + 0x24)), (int16_t)rd16(&O, obj + 0x12), &lKey);
        swap_structs(at24(rd32(&O, obj + 0x2C)), (int32_t)rd32(&O, obj + 0x28), &lSprite);
        convert_object(at24(rd32(&O, obj + 0x34)), depth + 1);
        obj = at24(rd32(&O, obj + 0x38));
    }
}

static void convert_object_bank(void) {
    int64_t e;
    uint32_t bankSize = O.n < 0x1000000 ? (uint32_t)O.n : 0x1000000;

    for (e = 0; e + 8 <= bankSize; e += 8) {
        int64_t inst, anim;
        swap_struct(e, &lEntry);
        if (rd32(&O, e + 4) == 0)
            break;
        inst = at24(rd32(&O, e + 4));
        if (inst < 0 || is_converted(inst + 12))
            continue;
        swap_struct(inst, &lInstance);
        convert_object(at24(rd32(&O, inst + 12)), 0);
        anim = at24(rd32(&O, inst + 24));
        if (anim >= 0 && !is_converted(anim)) {
            swap_struct(anim, &lAnimState);
            swap_structs(at24(rd32(&O, anim + 72)), (int16_t)rd16(&O, anim), &lAnimInfo);
        }
    }
}

/* ---- LoadObjectBankAt, RestoreObjectPointers (src/game/banks.c) and RestoreDrawList (src/port/loaders.c). */

/* Rom2Ram on the word at at. A word changed twice no longer names its target. */
static uint32_t relocate(int64_t at) {
    uint32_t v = rd32(&O, at);
    if (at & 3)
        fault(&O, at, "the loaders change a word that is not aligned");
    if ((changed[at >> 5] >> ((at >> 2) & 7)) & 1)
        fault(&O, at, "the loaders change one word twice");
    changed[at >> 5] |= (uint8_t)(1 << ((at >> 2) & 7));
    v = (v & 0xFFFFFF) + BASE;
    wr32(&O, at, v);
    return v;
}

/* The bank offset of a fixed pointer to len bytes. */
static int64_t deref(uint32_t w, int64_t len, int64_t from) {
    int64_t off = (int64_t)w - BASE;
    if (w < BASE || off + len > O.n || len < 0)
        fault(&O, from, "a pointer that the game follows leads outside the bank");
    return off;
}

static const char *const reserved[] = {"gball", "cball", "GShinL", "GShinR", "gbl_bt"};

static int name_is(int64_t obj, const char *name) {
    int i;
    for (i = 0; name[i]; i++)
        if (*acc(&O, obj + 4 + i, 1) != (uint8_t)name[i])
            return 0;
    return *acc(&O, obj + 4 + i, 1) == 0;
}

static void reserved_names(int64_t obj, int64_t at) {
    unsigned i;
    for (i = 0; i < sizeof reserved / sizeof reserved[0]; i++)
        if (name_is(obj, reserved[i]))
            fault(&O, at, "the loader would point the ball's or the hand's texture word into the bank");
}

static void restore_draw_list(int64_t dl, int64_t obj) {
    int64_t p = dl;
    uint32_t steps = 0;

    while ((rd32(&O, p) >> 24) != G_ENDDL) {
        if (++steps > (1u << 22))
            fault(&O, p, "a display list walk that does not end");
        switch (rd32(&O, p) >> 24) {
            case 0x04:
                relocate(p + 4);
                p += 8;
                break;
            case 0xFD: {
                uint32_t next = rd32(&O, p + 8) >> 24;
                if (next == 0xF5 || next == 0xE8) {
                    int64_t tex = find_texture(rd32(&O, p + 4));
                    if (tex >= 0) {
                        /* The word now holds a pointer into the texture bank. */
                        if ((changed[(p + 4) >> 5] >> (((p + 4) >> 2) & 7)) & 1)
                            fault(&O, p + 4, "the loaders change one word twice");
                        changed[(p + 4) >> 5] |= (uint8_t)(1 << (((p + 4) >> 2) & 7));
                        wr32(&O, p + 4, next == 0xF5 ? rd32(&T, tex + 28) : rd32(&T, tex + 32));
                    }
                    reserved_names(obj, p);
                    p += next == 0xF5 ? 0x38 : 0x30;
                } else {
                    p += 0x10;
                }
                break;
            }
            default:
                p += 8;
                break;
        }
    }
}

static void restore_object_pointers(int64_t obj, int depth) {
    uint32_t w;
    int32_t i;

    if (depth > 100000)
        fault(&O, obj, "the loader recurses without end");
    if (rd32(&O, obj + 0x14)) {
        int64_t mesh = deref(relocate(obj + 0x14), 36, obj + 0x14);
        int16_t nf;
        relocate(mesh + 4);
        nf = (int16_t)rd16(&O, mesh);
        if (nf) {
            relocate(mesh + 8);
            if (rd32(&O, mesh + 12))
                relocate(mesh + 12);
            if (rd32(&O, mesh + 16)) {
                /* JallocAlloc(numFaces * 12) and a copy from the texture coordinates. */
                int64_t uv = deref(relocate(mesh + 16), (int64_t)nf * 12, mesh + 16);
                (void)uv;
                wr32(&O, mesh + 20, 0x10000000u);
            }
            if (rd32(&O, mesh + 28))
                relocate(mesh + 28);
            if (rd32(&O, mesh + 32)) {
                int64_t t = deref(relocate(mesh + 32), 0, mesh + 32);
                for (i = 0; i < nf; i++) {
                    int64_t tex = find_texture(rd32(&O, t + 4 * i));
                    wr32(&O, t + 4 * i, tex >= 0 ? TEXBASE + (uint32_t)tex : 0);
                }
            }
        }
        if (rd32(&O, mesh + 24))
            relocate(mesh + 24);
    }
    if (rd32(&O, obj + 0x2C)) {
        int64_t s = deref(relocate(obj + 0x2C), 0, obj + 0x2C);
        int32_t n = (int32_t)rd32(&O, obj + 0x28);
        for (i = 0; i < n; i++) {
            int64_t tex = find_texture(rd32(&O, s + 24 * i));
            wr32(&O, s + 24 * i, tex >= 0 ? TEXBASE + (uint32_t)tex : 0);
        }
    }
    if (rd32(&O, obj + 0x18))
        relocate(obj + 0x18);
    if (rd32(&O, obj + 0x1C))
        relocate(obj + 0x1C);
    if (rd32(&O, obj + 0x20))
        relocate(obj + 0x20);
    if (rd32(&O, obj + 0x24))
        relocate(obj + 0x24);
    if (rd32(&O, obj + 0x34))
        relocate(obj + 0x34);
    if (rd32(&O, obj + 0x38))
        relocate(obj + 0x38);
    if ((w = rd32(&O, obj + 0x18)) != 0)
        restore_draw_list(deref(w, 8, obj + 0x18), obj);
    if ((w = rd32(&O, obj + 0x34)) != 0)
        restore_object_pointers(deref(w, 64, obj + 0x34), depth + 1);
    if ((w = rd32(&O, obj + 0x38)) != 0)
        restore_object_pointers(deref(w, 64, obj + 0x38), depth + 1);
}

static void load_object_bank(void) {
    int32_t i;

    for (i = 0; rd32(&O, (int64_t)i * 8 + 4); i++) {
        int64_t e = (int64_t)i * 8, inst;
        /* The game's counter is an s16. */
        if (i == 32767)
            fault(&O, e, "a directory past the loader's s16 counter");
        inst = deref(relocate(e + 4), 28, e + 4);
        relocate(inst + 12);
        if (rd32(&O, inst + 24)) {
            int64_t anim = deref(relocate(inst + 24), 80, inst + 24);
            if (rd32(&O, anim + 72))
                relocate(anim + 72);
        }
        restore_object_pointers(deref(rd32(&O, inst + 12), 64, inst + 12), 0);
    }
}

/* ---- What TransformAndDrawObject and the renderer read. */

static void draw_list(int64_t dl) {
    int64_t p = dl;
    for (;;) {
        uint32_t w0 = rd32(&O, p), op = w0 >> 24;
        if (op == G_ENDDL)
            return;
        if (op == 0x06 || op == 0x01 || op == 0x03)
            fault(&O, p, "a display list command that names memory the loaders do not fix");
        if (op == G_VTX)
            deref(rd32(&O, p + 4), ((w0 >> 10) & 0x3F) * 16, p + 4);
        p += 8;
    }
}

static void draw_object(int64_t obj, int nesting, int depth) {
    static const int counts[3] = {0x0E, 0x10, 0x12}, keys[3] = {0x1C, 0x20, 0x24};
    int k;
    uint32_t w;

    if (depth > 100000)
        fault(&O, obj, "the draw recurses without end");
    /* DrawActor pushes up to 4 matrices, and each level of children keeps one more of the 20 (maths.h). */
    if (4 + nesting + 1 > 19)
        fault(&O, obj, "the draw would overrun the matrix stack");
    for (k = 0; k < 3; k++) {
        int16_t n = (int16_t)rd16(&O, obj + counts[k]);
        deref(rd32(&O, obj + keys[k]), (int64_t)(n >= 2 ? n : 1) * 20, obj + keys[k]);
    }
    if ((w = rd32(&O, obj + 0x18)) != 0) {
        draw_list(deref(w, 8, obj + 0x18));
    } else {
        int64_t mesh = deref(rd32(&O, obj + 0x14), 36, obj + 0x14);
        if (rd16(&O, mesh))
            fault(&O, obj + 0x18, "the game would write this mesh into its vertex buffer with no bound");
    }
    if ((int32_t)rd32(&O, obj + 0x28) > 0)
        fault(&O, obj + 0x28, "the game would write through the sprites' pointers");
    if (rd32(&O, obj + 0x3C))
        fault(&O, obj + 0x3C, "the game would write through the sphere pointer");
    if ((w = rd32(&O, obj + 0x34)) != 0) {
        /* parentScaleStack has room for 10 entries (gen/ram.S). */
        if (nesting > 9)
            fault(&O, obj + 0x34, "the draw would overrun parentScaleStack");
        draw_object(deref(w, 64, obj + 0x34), nesting + 1, depth + 1);
    }
    if ((w = rd32(&O, obj + 0x38)) != 0)
        draw_object(deref(w, 64, obj + 0x38), nesting, depth + 1);
}

static void draw_all(void) {
    int32_t i;
    for (i = 0; rd32(&O, (int64_t)i * 8 + 4); i++) {
        int64_t inst = deref(rd32(&O, (int64_t)i * 8 + 4), 28, (int64_t)i * 8 + 4);
        draw_object(deref(rd32(&O, inst + 12), 64, inst + 12), 1, 0);
    }
}

const char *g64_bank_shadow(const uint8_t *tex, size_t tn, const uint8_t *obj, size_t on, size_t *at, int *bank) {
    const char *result = NULL;

    T.b = malloc(tn ? tn : 1);
    O.b = malloc(on ? on : 1);
    converted = calloc(1, 1 << 20);
    changed = calloc(on / 32 + 1, 1);
    if (T.b == NULL || O.b == NULL || converted == NULL || changed == NULL) {
        result = "out of memory";
        whyAt = 0;
        whyBank = 0;
    } else {
        memcpy(T.b, tex, tn);
        memcpy(O.b, obj, on);
        T.n = (int64_t)tn;
        T.kind = 0;
        O.n = (int64_t)on;
        O.kind = 1;
        numCycling = 0;
        why = NULL;
        if (setjmp(stop) == 0) {
            convert_texture_bank();
            load_texture_bank();
            convert_object_bank();
            load_object_bank();
            draw_all();
        } else {
            result = why;
        }
    }
    if (at)
        *at = result ? (size_t)whyAt : 0;
    if (bank)
        *bank = result ? whyBank : 0;
    free(T.b);
    free(O.b);
    free(converted);
    free(changed);
    return result;
}
