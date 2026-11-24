/* The check of a pack's models.bin (bankcheck.h). The rules come from a read of weemitt's src/game/banks.c
 * (LoadTextureBankAt, FindTexture, LoadObjectBankAt, RestoreObjectPointers), src/port/loaders.c (the converters and
 * RestoreDrawList), src/game/object.c (DrawActor and TransformAndDrawObject) and the structures in include/actor.h,
 * include/texture.h and include/banks.h. A rule marked (G) allows only what Glovebox writes today
 * (crates/core/src/custom/pack.rs). tests/fixtures/cube.models.bin is its real output. */
#include "bankcheck.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* weemitt's include/enums.h. */
enum { FONT_TEX_BANK = 10, GENERIC_TEX_BANK = 2, SYSTEM_OBJ_BANK = 0, GENERIC_OBJ_BANK = 1 };

#define FLA2 0x464C4132u

#define MAX_TEXTURES 1024
#define MAX_ENTRIES 1024
#define MAX_OBJECTS 4096
/* RestoreObjectPointers and TransformAndDrawObject call themselves for each child and each sibling. */
#define MAX_DEPTH 32
/* Each level of children keeps one matrix on the game's matrix stack of 20 (TransformAndDrawObject), after the up to
   4 that DrawActor pushes, and one entry of parentScaleStack, which has room for 10 (gen/ram.S: 120 bytes up to
   currentDrawActor). A root object is level 1. */
#define MAX_NESTING 10
#define MAX_FACES 1500 /* in one mesh: Glovebox's MAX_TRIANGLES */
/* In one mesh: the three corners of each of 1500 faces. Glovebox counts each pair of a position and texture
   coordinates as a vertex. */
#define MAX_VERTS 4500

/* Bounds of the floats. guMtxF2L turns each matrix entry times 65536 into an s32, which overflows past 32768.
   TransformAndDrawObject translates a child by its position times its parent's scale, and keeps the parent's
   rotation and translation on the matrix stack while the child draws. So the translations add up along the tree.
   MAX_REACH bounds that sum for an actor at the origin with scale 1. The actor's own position and scale come from
   the level, and add to it. The rotations are unit quaternions after QuaternionToMatrix divides by the length, so
   they do not change a length. */
#define MAX_REACH 16384.0
#define MAX_POSITION 16384.0f
#define MAX_SCALE 64.0f
#define MAX_VERTEX 100000.0f

/* Texture bank record fields (include/texture.h). */
enum { T_CRC = 0, T_CYCLE = 4, T_FLAGS = 6, T_SPEED = 8, T_COUNT = 10, T_W = 12, T_H = 14, T_MASKS = 16,
       T_MASKT = 18, T_SIZE = 20, T_FMT = 24, T_SIZ = 26, T_DATA = 28, T_PALETTE = 32, T_HEADER = 36 };

/* Object fields (include/actor.h). */
enum { O_CRC = 0, O_NAME = 4, O_INIT = 0x0C, O_ALPHA = 0x0D, O_NSCALE = 0x0E, O_NPOS = 0x10, O_NROT = 0x12,
       O_MESH = 0x14, O_DL = 0x18, O_SCALE = 0x1C, O_POS = 0x20, O_ROT = 0x24, O_NSPRITES = 0x28, O_SPRITES = 0x2C,
       O_UNK30 = 0x30, O_FLAGS = 0x32, O_CHILD = 0x34, O_SIBLING = 0x38, O_SPHERE = 0x3C, O_SIZE = 0x40 };

/* Mesh fields. */
enum { M_NFACES = 0, M_NVERTS = 2, M_VERTS = 4, M_FACES = 8, M_FACECOLOURS = 0x0C, M_UV = 0x10, M_ORIGUV = 0x14,
       M_COLOURS = 0x18, M_FACEFLAGS = 0x1C, M_TEXTURES = 0x20, M_SIZE = 0x24 };

/* ObjectInstance and AnimState. */
enum { I_OBJ = 0x0C, I_ANIM = 0x18, I_SIZE = 0x1C, ANIM_SIZE = 0x50, KEY_SIZE = 20 };

/* The bits of an object's flags that Glovebox writes (G): 2 blended, 4 cut out. */
#define OBJECT_FLAGS 6

typedef struct {
    uint32_t id;
    uint32_t at; /* in the texture bank */
    uint16_t w, h;
    uint8_t used;
} Tex;

typedef struct {
    uint32_t at, len; /* in the object bank */
} Span;

typedef struct {
    Span *v;
    size_t n, cap;
} Spans;

typedef struct {
    const uint8_t *t, *o; /* the banks */
    uint32_t tn, on;      /* their sizes */
    uint32_t tAt, oAt;    /* their offsets in the file */
    uint8_t *own;         /* one bit per byte of the object bank: a structure reaches it */
    Tex tex[MAX_TEXTURES];
    unsigned ntex;
    uint32_t entries, dirEnd;
    unsigned objects;
    int collect; /* keep the meshes' vertex arrays and the G_VTX blocks for the ROM rules */
    Spans verts, blocks;
    const char *why;
    size_t at;
} Check;

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint32_t be16(const uint8_t *p) {
    return (uint32_t)p[0] << 8 | p[1];
}

static int fail(Check *c, size_t at, const char *why) {
    if (c->why == NULL) {
        c->why = why;
        c->at = at;
    }
    return 0;
}

/* Offsets in the file of a texture bank byte and an object bank byte. */
static size_t ft(const Check *c, uint32_t x) {
    return (size_t)c->tAt + x;
}

static size_t fo(const Check *c, uint32_t x) {
    return (size_t)c->oAt + x;
}

static int all_zero(const uint8_t *p, size_t n) {
    size_t i;
    for (i = 0; i < n; i++)
        if (p[i])
            return 0;
    return 1;
}

static int push(Spans *s, uint32_t at, uint32_t len) {
    if (s->n == s->cap) {
        size_t cap = s->cap ? s->cap * 2 : 64;
        Span *v = realloc(s->v, cap * sizeof *v);
        if (v == NULL)
            return 0;
        s->v = v;
        s->cap = cap;
    }
    s->v[s->n].at = at;
    s->v[s->n].len = len;
    s->n++;
    return 1;
}

/* ---- Floats, read from their bits. A float whose exponent bits are all ones is infinite or not a number. */

static int finite_at(const uint8_t *p) {
    return (be32(p) & 0x7F800000u) != 0x7F800000u;
}

static float float_at(const uint8_t *p) {
    uint32_t u = be32(p);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

/* 1 when the float at p is finite and at most limit in size. */
static int bounded(const uint8_t *p, float limit) {
    float f;
    if (!finite_at(p))
        return 0;
    f = float_at(p);
    return f <= limit && f >= -limit;
}

/* ---- The header (model-banks.md, "The file"). */

static int retail_ok(Check *c, const uint8_t *f, int kind, unsigned first, unsigned count, g64_bank_range_fn exists,
                     void *user) {
    unsigned i, k;
    for (i = 0; i < count; i++) {
        unsigned num = f[first + i];
        uint32_t s, e;
        if (kind == G64_BANK_TEXTURE && (num == FONT_TEX_BANK || num == GENERIC_TEX_BANK))
            return fail(c, first + i, "the font and generic texture banks cannot be named");
        if (kind == G64_BANK_OBJECT && (num == SYSTEM_OBJ_BANK || num == GENERIC_OBJ_BANK))
            return fail(c, first + i, "object banks 0 and 1 cannot be named");
        if (exists == NULL || !exists(user, kind, num, &s, &e))
            return fail(c, first + i, "a retail bank number that the game does not have");
        for (k = 0; k < i; k++)
            if (f[first + k] == num)
                return fail(c, first + i, "a retail bank number named twice");
    }
    return 1;
}

static int header(Check *c, const uint8_t *f, size_t n, g64_bank_range_fn exists, void *user, G64Models *m) {
    uint32_t t, o, h, a, b;
    uint64_t total;

    if (n < 12)
        return fail(c, n, "the file is shorter than its header");
    t = be32(f);
    o = be32(f + 4);
    if (t % 8)
        return fail(c, 0, "the texture bank's size is not a multiple of 8");
    if (o % 8)
        return fail(c, 4, "the object bank's size is not a multiple of 8");
    if ((t == 0) != (o == 0))
        return fail(c, t == 0 ? 0 : 4, "the file has one bank of the pack's own without the other");
    if ((uint64_t)t + o > G64_MODELS_BANKS_MAX)
        return fail(c, 0, "the banks are larger than 4 MiB");
    if (f[8] & ~1u)
        return fail(c, 8, "a flag that the format does not have");
    a = f[9];
    b = f[10];
    if (a > G64_MODELS_RETAIL_MAX)
        return fail(c, 9, "more than 4 retail texture banks");
    if (b > G64_MODELS_RETAIL_MAX)
        return fail(c, 10, "more than 4 retail object banks");
    if (f[11])
        return fail(c, 11, "a header byte that must be 0 is not");
    if (t == 0 && a == 0 && b == 0)
        return fail(c, 0, "the file holds no models and names no bank");
    /* The flag orders the retail texture banks before the level's own. With none it would mean nothing. */
    if ((f[8] & 1) && a == 0)
        return fail(c, 8, "the flag for retail textures, with no retail texture bank");
    h = (12 + a + b + 7) & ~7u;
    if (n < h)
        return fail(c, n, "the file is shorter than its header");
    if (!retail_ok(c, f, G64_BANK_TEXTURE, 12, a, exists, user) ||
        !retail_ok(c, f, G64_BANK_OBJECT, 12 + a, b, exists, user))
        return 0;
    if (!all_zero(f + 12 + a + b, h - 12 - a - b)) {
        size_t i = 12 + a + b;
        while (!f[i])
            i++;
        return fail(c, i, "a padding byte of the header is not 0");
    }
    total = (uint64_t)h + t + o;
    if (n < total)
        return fail(c, n, "the file is shorter than its header says");
    if (n > total)
        return fail(c, (size_t)total, "the file is longer than its header says");
    memset(m, 0, sizeof *m);
    m->texAt = h;
    m->texSize = t;
    m->objAt = h + t;
    m->objSize = o;
    m->flags = f[8];
    m->numTex = (uint8_t)a;
    m->numObj = (uint8_t)b;
    memcpy(m->tex, f + 12, a);
    memcpy(m->obj, f + 12 + a, b);
    return 1;
}

/* ---- The texture bank. It mirrors LoadTextureBankAt's loop, which steps by each record's size with no bound. */

static int log2_pow(uint32_t v) {
    int k = 0;
    if (v == 0 || (v & (v - 1)))
        return -1;
    while (v > 1) {
        v >>= 1;
        k++;
    }
    return k;
}

static int textures(Check *c) {
    const uint8_t *t = c->t;
    uint32_t count, i, p = 4, end = c->tn - 4;

    if (be32(t) == FLA2)
        return fail(c, ft(c, 0), "the texture bank starts with FLA2");
    count = be32(t);
    if (count < 1 || count > MAX_TEXTURES)
        return fail(c, ft(c, 0), "the texture bank's count is not 1 to 1024");
    for (i = 0; i < count; i++) {
        const uint8_t *r;
        uint32_t w, h, size, k, texels;
        int sw, sh;
        if (p > end || end - p < T_HEADER)
            return fail(c, ft(c, p), "a texture record runs past the end of the bank");
        r = t + p;
        if (be32(r + T_CRC) == 0)
            return fail(c, ft(c, p + T_CRC), "a texture id is 0");
        for (k = 0; k < c->ntex; k++)
            if (c->tex[k].id == be32(r + T_CRC))
                return fail(c, ft(c, p + T_CRC), "two textures have one id");
        /* Bit 2 of the flags makes a cycling texture. The game keeps 20 of them, with no bound (G). */
        if (r[T_CYCLE] || r[T_CYCLE + 1])
            return fail(c, ft(c, p + T_CYCLE), "a texture's cycle range is not 0");
        if (be16(r + T_FLAGS))
            return fail(c, ft(c, p + T_FLAGS), "a texture's flags are not 0");
        if (be16(r + T_SPEED))
            return fail(c, ft(c, p + T_SPEED), "a texture's cycle speed or count is not 0");
        if (be16(r + T_COUNT))
            return fail(c, ft(c, p + T_COUNT), "a texture's cycle speed or count is not 0");
        w = be16(r + T_W);
        h = be16(r + T_H);
        sw = log2_pow(w);
        sh = log2_pow(h);
        if (sw < 1 || sw > 8 || sh < 1 || sh > 8 || w * h > 2048)
            return fail(c, ft(c, p + T_W), "a texture's size is not one Glovebox writes");
        if (be16(r + T_MASKS) != (uint32_t)sw || be16(r + T_MASKT) != (uint32_t)sh)
            return fail(c, ft(c, p + T_MASKS), "a texture's masks do not match its size");
        if (be16(r + T_FMT) != 0 || be16(r + T_SIZ) != 2)
            return fail(c, ft(c, p + T_FMT), "a texture is not 16-bit RGBA");
        if (be32(r + T_DATA) != T_HEADER)
            return fail(c, ft(c, p + T_DATA), "a texture's data field is not 36");
        /* The loader adds a palette offset to the record with no check. */
        if (be32(r + T_PALETTE) != 0)
            return fail(c, ft(c, p + T_PALETTE), "a texture has a palette");
        texels = T_HEADER + 2 * w * h;
        size = be32(r + T_SIZE);
        if (size != ((texels + 7) & ~7u))
            return fail(c, ft(c, p + T_SIZE), "a texture's size field does not match its texels");
        if (size > end - p)
            return fail(c, ft(c, p), "a texture record runs past the end of the bank");
        for (k = texels; k < size; k++)
            if (r[k])
                return fail(c, ft(c, p + k), "a padding byte of a texture is not 0");
        c->tex[c->ntex].id = be32(r + T_CRC);
        c->tex[c->ntex].at = p;
        c->tex[c->ntex].w = (uint16_t)w;
        c->tex[c->ntex].h = (uint16_t)h;
        c->tex[c->ntex].used = 0;
        c->ntex++;
        p += size;
    }
    if (p != end)
        return fail(c, ft(c, p), "the texture bank holds bytes after its records");
    for (i = end; i < c->tn; i++)
        if (t[i])
            return fail(c, ft(c, i), "the texture bank's last 4 bytes are not 0");
    return 1;
}

/* ---- The object bank. */

/* Checks the pointer word at bank offset at, which names len bytes, and marks those bytes as reached. The loaders
   relocate a word each time they reach it, so no bytes may be reached twice. */
static int claim(Check *c, uint32_t at, uint32_t w, uint32_t align, uint32_t len) {
    uint32_t i;
    if (w >> 24)
        return fail(c, fo(c, at), "a pointer has high bits set");
    if (w % align)
        return fail(c, fo(c, at), "a pointer is not aligned");
    if (w < c->dirEnd)
        return fail(c, fo(c, at), "a pointer names the directory");
    if (w > c->on || len > c->on - w)
        return fail(c, fo(c, at), "a pointer names a range outside the bank");
    for (i = w; i < w + len; i++)
        if (c->own[i >> 3] & (1 << (i & 7)))
            return fail(c, fo(c, at), "two structures share bytes");
    for (i = w; i < w + len; i++)
        c->own[i >> 3] |= (uint8_t)(1 << (i & 7));
    return 1;
}

static int find_texture(const Check *c, uint32_t id) {
    unsigned k;
    for (k = 0; k < c->ntex; k++)
        if (c->tex[k].id == id)
            return (int)k;
    return -1;
}

/* RestoreDrawList's FD case: when the next command is F5, it points the FD at the texels and skips 0x38 bytes,
   which is FD and the 6 commands of Glovebox's texture_load after it. Every FD must start that block, with the
   words Glovebox writes for the texture's size (G). Then RestoreDrawList and a walk one command at a time see the
   same commands. The E8 case would point the FD at a palette, and pack textures have none. */
static int texture_load(Check *c, uint32_t at) {
    const uint8_t *d = c->o + at;
    uint32_t w, h, masks, row, dxt, want[14];
    unsigned k;
    int t;

    if (be32(d) != 0xFD100000u)
        return fail(c, fo(c, at), "a texture load is not in Glovebox's form");
    t = find_texture(c, be32(d + 4));
    if (t < 0)
        return fail(c, fo(c, at + 4), "a texture load names a texture that is not in the pack's texture bank");
    c->tex[t].used = 1;
    w = c->tex[t].w;
    h = c->tex[t].h;
    masks = (uint32_t)log2_pow(h) << 14 | (uint32_t)log2_pow(w) << 4;
    row = w * 2 / 8 ? w * 2 / 8 : 1;
    dxt = (2048 + row - 1) / row;
    want[0] = 0xF5100000u;
    want[1] = 7u << 24 | masks;
    want[2] = 0xE6000000u;
    want[3] = 0;
    want[4] = 0xF3000000u;
    want[5] = 7u << 24 | ((w * h - 1) & 0xFFF) << 12 | (dxt & 0xFFF);
    want[6] = 0xE7000000u;
    want[7] = 0;
    want[8] = 0xF5100000u | (((w * 2 + 7) >> 3) & 0x1FF) << 9;
    want[9] = masks;
    want[10] = 0xF2000000u;
    want[11] = (((w - 1) << 2) & 0xFFF) << 12 | (((h - 1) << 2) & 0xFFF);
    for (k = 0; k < 12; k++)
        if (be32(d + 8 + 4 * k) != want[k])
            return fail(c, fo(c, at + 8 + 8 * (k / 2)), "a texture load is not followed by Glovebox's commands");
    return 1;
}

/* The opcodes Glovebox writes: G_VTX, TRI2, MODIFYVTX, CLEARGEOMETRYMODE, SETGEOMETRYMODE, ENDDL, SETOTHERMODE_H,
   TRI1, the three syncs, SETTILESIZE, LOADBLOCK, SETTILE and SETTIMG (G). 0x06 (G_DL) is not among them. */
static int allowed(unsigned op) {
    switch (op) {
        case 0x04: case 0xB1: case 0xB2: case 0xB6: case 0xB7: case 0xB8: case 0xBA: case 0xBF:
        case 0xE6: case 0xE7: case 0xE8: case 0xF2: case 0xF3: case 0xF5: case 0xFD:
            return 1;
    }
    return 0;
}

static int display_list(Check *c, uint32_t ptrAt, uint32_t d) {
    uint32_t i, count = 0;

    /* The walk is bounded by the bank's end. The renderer stops a list after 100000 commands (gfx_dl.c). That is
       only a picture, and Glovebox writes far fewer. */
    if (d >> 24 || d % 8 || d < c->dirEnd || d >= c->on)
        return claim(c, ptrAt, d, 8, 8);
    /* The list ends at the first G_ENDDL, as RestoreDrawList and the renderer read it. */
    for (i = 0;; i++) {
        uint32_t at = d + 8 * i;
        unsigned op;
        if (at > c->on - 8)
            return fail(c, fo(c, ptrAt), "a display list runs past the end of the bank");
        op = c->o[at];
        if (!allowed(op))
            return fail(c, fo(c, at), "a display list command that Glovebox does not write");
        if (op == 0xB8) {
            count = i + 1;
            break;
        }
    }
    if (!claim(c, ptrAt, d, 8, count * 8))
        return 0;
    for (i = 0; i < count; i++) {
        uint32_t at = d + 8 * i, w0 = be32(c->o + at), w1 = be32(c->o + at + 4);
        switch (w0 >> 24) {
            case 0x04: {
                /* Glovebox loads n vertices at slot 0: n in bits 10 to 15, and n * 16 - 1 in bits 0 to 9 (G). */
                uint32_t n = (w0 >> 10) & 0x3F;
                if (n < 1 || n > 32 || w0 != (0x04000000u | n << 10 | (n * 16 - 1)))
                    return fail(c, fo(c, at), "a G_VTX command is not in Glovebox's form");
                /* Glovebox's linker copies each G_VTX command's block, so no two commands share one (G). */
                if (!claim(c, at + 4, w1, 8, n * 16))
                    return 0;
                if (c->collect && !push(&c->blocks, w1, n * 16))
                    return fail(c, fo(c, at), "out of memory");
                break;
            }
            case 0xFD:
                if (i + 7 > count)
                    return fail(c, fo(c, at + 8), "a texture load is not followed by Glovebox's commands");
                if (!texture_load(c, at))
                    return 0;
                i += 6;
                break;
            case 0xB8:
                if (w0 != 0xB8000000u || w1 != 0)
                    return fail(c, fo(c, at), "a G_ENDDL command is not in Glovebox's form");
                break;
        }
    }
    return 1;
}

static int keys(Check *c, uint32_t ptrAt, uint32_t k, int kind) {
    const uint8_t *p;
    unsigned i;
    if (!claim(c, ptrAt, k, 4, KEY_SIZE))
        return 0;
    p = c->o + k;
    if (kind == 2) {
        /* A rotation key is a quaternion. Near unit length it gives a rotation; all zeros give NaN values. */
        double q[4], len = 0;
        for (i = 0; i < 4; i++) {
            if (!finite_at(p + 4 * i))
                return fail(c, fo(c, k + 4 * i), "a key holds a float that is not finite");
            q[i] = float_at(p + 4 * i);
            len += q[i] * q[i];
        }
        if (!(len >= 0.5 && len <= 2.0))
            return fail(c, fo(c, k), "a rotation key is not near unit length");
    } else {
        float limit = kind == 0 ? MAX_POSITION : MAX_SCALE;
        for (i = 0; i < 3; i++) {
            if (!finite_at(p + 4 * i))
                return fail(c, fo(c, k + 4 * i), "a key holds a float that is not finite");
            if (!bounded(p + 4 * i, limit))
                return fail(c, fo(c, k + 4 * i), kind == 0 ? "a position is past 16384" : "a scale is past 64");
        }
        if (be32(p + 12))
            return fail(c, fo(c, k + 12), "a key's fourth value is not 0");
    }
    if (be32(p + 16))
        return fail(c, fo(c, k + 16), "a key's time is not 0");
    return 1;
}

static int mesh(Check *c, uint32_t ptrAt, uint32_t m) {
    const uint8_t *p;
    uint32_t nf, nv, verts, faces, fc, i;

    if (!claim(c, ptrAt, m, 4, M_SIZE))
        return 0;
    p = c->o + m;
    nf = be16(p + M_NFACES);
    nv = be16(p + M_NVERTS);
    if (nf < 1 || nf > MAX_FACES)
        return fail(c, fo(c, m + M_NFACES), "a mesh's face count is not 1 to 1500");
    if (nv < 3 || nv > MAX_VERTS)
        return fail(c, fo(c, m + M_NVERTS), "a mesh's vertex count is not 3 to 4500");
    /* RestoreObjectPointers allocates a block for texture coordinates (uv), and looks up a texture for each face
       (textures). Glovebox writes neither (G). */
    for (i = M_UV; i <= M_ORIGUV; i += 4)
        if (be32(p + i))
            return fail(c, fo(c, m + i), "a mesh has texture coordinates");
    for (i = M_COLOURS; i <= M_TEXTURES; i += 4)
        if (be32(p + i))
            return fail(c, fo(c, m + i), "a mesh has colors, face flags or face textures");
    verts = be32(p + M_VERTS);
    if (!claim(c, m + M_VERTS, verts, 4, nv * 12))
        return 0;
    for (i = 0; i < nv * 3; i++)
        if (!bounded(c->o + verts + 4 * i, MAX_VERTEX))
            return fail(c, fo(c, verts + 4 * i), "a vertex is not finite or is past 100000");
    if (c->collect && !push(&c->verts, verts, nv * 12))
        return fail(c, fo(c, m), "out of memory");
    faces = be32(p + M_FACES);
    if (!claim(c, m + M_FACES, faces, 4, nf * 6))
        return 0;
    /* Collision and platforms read the vertices of each face with no check. */
    for (i = 0; i < nf * 3; i++)
        if (be16(c->o + faces + 2 * i) >= nv)
            return fail(c, fo(c, faces + 2 * i), "a face names a vertex past the mesh's vertices");
    fc = be32(p + M_FACECOLOURS);
    if (fc && !claim(c, m + M_FACECOLOURS, fc, 4, nf * 4))
        return 0;
    return 1;
}

static const char *const reserved[] = {"gball", "cball", "GShinL", "GShinR", "gbl_bt"};

/* parentScale is the largest size of the parent's scale (1 for a root). reach bounds the length of the parent's
   translation from the actor (0 for a root). A sibling has the same parent as the object. */
static int object(Check *c, uint32_t ptrAt, uint32_t o, unsigned depth, unsigned nesting, double parentScale,
                  double reach) {
    const uint8_t *p;
    uint32_t i, len, child, sibling, at;
    double length = 0, scale = 0, mine;

    if (depth > MAX_DEPTH)
        return fail(c, fo(c, ptrAt), "the object tree is deeper than 32");
    if (nesting > MAX_NESTING)
        return fail(c, fo(c, ptrAt), "the object tree has more than 10 levels of children");
    if (++c->objects > MAX_OBJECTS)
        return fail(c, fo(c, ptrAt), "the bank has more than 4096 objects");
    if (!claim(c, ptrAt, o, 4, O_SIZE))
        return 0;
    p = c->o + o;
    for (len = 0; len < 8 && p[O_NAME + len]; len++) {
    }
    /* RestoreDrawList points the game's ball and hand texture words at the list of an object with these names. */
    for (i = 0; i < sizeof reserved / sizeof reserved[0]; i++)
        if (strlen(reserved[i]) == len && memcmp(p + O_NAME, reserved[i], len) == 0)
            return fail(c, fo(c, o + O_NAME), "an object has the name of the ball or the hand");
    if (p[O_INIT])
        return fail(c, fo(c, o + O_INIT), "an object's initialized byte is not 0");
    /* TransformAndDrawObject reads key 0 of a track with fewer than 2 keys. Glovebox writes 1 key (G). */
    for (i = O_NSCALE; i <= O_NROT; i += 2)
        if (be16(p + i) != 1)
            return fail(c, fo(c, o + i), "an object's key count is not 1");
    /* The sprite loop looks up a texture for each sprite. */
    if (be32(p + O_NSPRITES) || be32(p + O_SPRITES))
        return fail(c, fo(c, o + O_NSPRITES), "an object has sprites");
    if (be16(p + O_UNK30))
        return fail(c, fo(c, o + O_UNK30), "an object's bytes at 0x30 are not 0");
    if (be16(p + O_FLAGS) & ~(uint32_t)OBJECT_FLAGS)
        return fail(c, fo(c, o + O_FLAGS), "an object's flags have a bit that Glovebox does not write");
    /* TransformAndDrawObject writes through the sphere pointer, which the loaders do not relocate. */
    if (be32(p + O_SPHERE))
        return fail(c, fo(c, o + O_SPHERE), "an object has a collision sphere");
    /* With no display list, the game writes the mesh into a vertex buffer with no bound. Glovebox writes both (G). */
    for (i = O_MESH; i <= O_DL; i += 4)
        if (be32(p + i) == 0)
            return fail(c, fo(c, o + i), "an object lacks a mesh or a display list");
    for (i = O_SCALE; i <= O_ROT; i += 4)
        if (be32(p + i) == 0)
            return fail(c, fo(c, o + i), "an object lacks a key");
    if (!mesh(c, o + O_MESH, be32(p + O_MESH)) || !display_list(c, o + O_DL, be32(p + O_DL)) ||
        !keys(c, o + O_SCALE, be32(p + O_SCALE), 1) || !keys(c, o + O_POS, be32(p + O_POS), 0) ||
        !keys(c, o + O_ROT, be32(p + O_ROT), 2))
        return 0;
    at = be32(p + O_POS);
    for (i = 0; i < 3; i++) {
        double v = float_at(c->o + at + 4 * i), s = float_at(c->o + be32(p + O_SCALE) + 4 * i);
        length += v * v;
        if (s < 0)
            s = -s;
        if (s > scale)
            scale = s;
    }
    mine = reach + sqrt(length) * parentScale;
    if (mine > MAX_REACH)
        return fail(c, fo(c, at), "an object's position, added up along its parents, is past 16384");
    child = be32(p + O_CHILD);
    sibling = be32(p + O_SIBLING);
    if (child && !object(c, o + O_CHILD, child, depth + 1, nesting + 1, scale, mine))
        return 0;
    if (sibling && !object(c, o + O_SIBLING, sibling, depth + 1, nesting, parentScale, reach))
        return 0;
    return 1;
}

/* The ids of objects that the game finds by a name of its own and that the retail ROM's banks do not have: the
   names of nine enemy kinds (kinds 0 to 6 share one name, and kinds 46 and 57 have one each) and of the pick-ups 7,
   8 and 9. The
   game asks FindObject for them when a level has such an enemy or pick-up. A pack's object with one of these ids
   would be that enemy or pick-up, which reads an animation. The rule for the ROM's ids covers every other name.
   tests/rom/levcheck.c compares this list with the game's names and the ROM. */
const uint32_t g64_models_game_ids[G64_MODELS_GAME_IDS] = {0x5e9f46bf, 0xf90dc11e, 0x61af1e01, 0x0ee2f4e2,
                                                           0x571f7f95, 0x8516ac66};

/* The directory: entries of {id, instance} up to an entry of 8 zero bytes. LoadObjectBankAt stops only at an
   instance of 0. */
static int objects(Check *c) {
    const uint8_t *o = c->o;
    uint32_t e, i, k;

    if (be32(o) == FLA2)
        return fail(c, fo(c, 0), "the object bank starts with FLA2");
    for (e = 0;; e++) {
        uint32_t at = e * 8;
        if (at > c->on - 8)
            return fail(c, fo(c, at), "the directory runs past the end of the bank");
        if (be32(o + at + 4) == 0) {
            if (be32(o + at))
                return fail(c, fo(c, at), "a directory entry has an id and no instance");
            break;
        }
        if (be32(o + at) == 0)
            return fail(c, fo(c, at), "a directory entry has the id 0");
        for (k = 0; k < G64_MODELS_GAME_IDS; k++)
            if (be32(o + at) == g64_models_game_ids[k])
                return fail(c, fo(c, at), "an object has the id of an enemy or a pick-up of the game");
        if (e == MAX_ENTRIES)
            return fail(c, fo(c, at), "the directory has more than 1024 entries");
        for (k = 0; k < e; k++)
            if (be32(o + k * 8) == be32(o + at))
                return fail(c, fo(c, at), "two objects have one id");
    }
    if (e == 0)
        return fail(c, fo(c, 0), "the directory is empty");
    c->entries = e;
    c->dirEnd = (e + 1) * 8;
    for (i = 0; i < c->dirEnd; i++)
        c->own[i >> 3] |= (uint8_t)(1 << (i & 7));
    for (i = 0; i < e; i++) {
        uint32_t inst = be32(o + i * 8 + 4), anim;
        const uint8_t *p;
        if (!claim(c, i * 8 + 4, inst, 4, I_SIZE))
            return 0;
        p = o + inst;
        /* Glovebox writes the object's id at 0, the two pointers, and zeros (G). */
        if (be32(p) != be32(o + i * 8))
            return fail(c, fo(c, inst), "an instance does not start with its object's id");
        if (!all_zero(p + 4, I_OBJ - 4) || !all_zero(p + I_OBJ + 4, I_ANIM - I_OBJ - 4))
            return fail(c, fo(c, inst + 4), "an instance's spare bytes are not 0");
        if (be32(p + I_OBJ) == 0)
            return fail(c, fo(c, inst + I_OBJ), "an instance has no object");
        anim = be32(p + I_ANIM);
        /* Glovebox points the instance at an animation state of zeros: no animations (G). */
        if (anim) {
            if (!claim(c, inst + I_ANIM, anim, 4, ANIM_SIZE))
                return 0;
            if (!all_zero(o + anim, ANIM_SIZE))
                return fail(c, fo(c, anim), "an animation state is not all zeros");
        }
        if (!object(c, inst + I_OBJ, be32(p + I_OBJ), 1, 1, 1.0, 0.0))
            return 0;
    }
    return 1;
}

/* Runs the whole check. The header is checked with exists. */
static int run(Check *c, const uint8_t *file, size_t n, g64_bank_range_fn exists, void *user, G64Models *m) {
    uint32_t i;

    if (!header(c, file, n, exists, user, m))
        return 0;
    if (m->texSize == 0)
        return 1;
    c->t = file + m->texAt;
    c->tn = m->texSize;
    c->tAt = m->texAt;
    c->o = file + m->objAt;
    c->on = m->objSize;
    c->oAt = m->objAt;
    c->own = calloc(c->on / 8 + 1, 1);
    if (c->own == NULL)
        return fail(c, 0, "out of memory");
    if (!textures(c) || !objects(c))
        return 0;
    for (i = 0; i < c->ntex; i++)
        if (!c->tex[i].used)
            return fail(c, ft(c, c->tex[i].at), "a texture that no object uses");
    for (i = 0; i < c->on; i++)
        if (c->o[i] && !(c->own[i >> 3] & (1 << (i & 7))))
            return fail(c, fo(c, i), "a byte that no structure reaches is not 0");
    m->numEntries = c->entries;
    m->numObjects = c->objects;
    m->numTextures = c->ntex;
    return 1;
}

static void done(Check *c) {
    free(c->own);
    free(c->verts.v);
    free(c->blocks.v);
    free(c);
}

const char *g64_models_check(const uint8_t *file, size_t n, g64_bank_range_fn exists, void *user, G64Models *out,
                             size_t *at) {
    Check *c = calloc(1, sizeof *c);
    G64Models m;
    const char *why;

    if (c == NULL) {
        if (at)
            *at = 0;
        return "out of memory";
    }
    if (run(c, file, n, exists, user, &m) && out)
        *out = m;
    why = c->why;
    if (at)
        *at = why ? c->at : 0;
    done(c);
    return why;
}

/* ---- The rules that need the ROM. */

static int any_bank(void *user, int kind, unsigned num, uint32_t *start, uint32_t *end) {
    (void)user;
    (void)kind;
    (void)num;
    *start = *end = 0;
    return 1;
}

/* 1 when every 16-bit texel of a texture is the same. Glovebox's white stand-in texture is one color. */
static int one_color(const uint8_t *p, uint32_t texels) {
    uint32_t i;
    for (i = 1; i < texels; i++)
        if (p[2 * i] != p[0] || p[2 * i + 1] != p[1])
            return 0;
    return 1;
}

static int rom_rules(Check *c, const G64RomBanks *rom) {
    uint8_t hash[16];
    uint32_t i;
    size_t k;

    for (i = 0; i < c->entries; i++)
        if (g64_rombanks_has_object_id(rom, be32(c->o + i * 8)))
            return fail(c, fo(c, i * 8), "an object id that the ROM has");
    for (i = 0; i < c->ntex; i++)
        if (g64_rombanks_has_texture_id(rom, c->tex[i].id))
            return fail(c, ft(c, c->tex[i].at), "a texture id that the ROM has");
    for (i = 0; i < c->ntex; i++) {
        const uint8_t *p = c->t + c->tex[i].at + T_HEADER;
        uint32_t texels = (uint32_t)c->tex[i].w * c->tex[i].h;
        if (one_color(p, texels))
            continue;
        g64_rombanks_hash(hash, p, texels * 2);
        if (g64_rombanks_has_texels_hash(rom, hash))
            return fail(c, ft(c, c->tex[i].at + T_HEADER), "a texture equal to one in the ROM");
    }
    for (k = 0; k < c->verts.n; k++) {
        const Span *s = &c->verts.v[k];
        if (s->len < 64)
            continue;
        g64_rombanks_hash(hash, c->o + s->at, s->len);
        if (g64_rombanks_has_verts_hash(rom, hash))
            return fail(c, fo(c, s->at), "a mesh's vertices equal to a mesh's in the ROM");
    }
    for (k = 0; k < c->blocks.n; k++) {
        const Span *s = &c->blocks.v[k];
        if (s->len < 64)
            continue;
        g64_rombanks_hash(hash, c->o + s->at, s->len);
        if (g64_rombanks_has_vtx_hash(rom, hash))
            return fail(c, fo(c, s->at), "a G_VTX block equal to one in the ROM");
    }
    return 1;
}

const char *g64_models_check_rom(const uint8_t *file, size_t n, const G64RomBanks *rom, size_t *at) {
    Check *c = calloc(1, sizeof *c);
    G64Models m;
    const char *why;

    if (c == NULL) {
        if (at)
            *at = 0;
        return "out of memory";
    }
    c->collect = 1;
    if (run(c, file, n, any_bank, NULL, &m) && m.texSize)
        rom_rules(c, rom);
    why = c->why;
    if (at)
        *at = why ? c->at : 0;
    done(c);
    return why;
}
