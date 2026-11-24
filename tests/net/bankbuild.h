/* A builder of models.bin files for the bank check's test and fuzz target (src/client/bankcheck.h). It writes the
 * structures in the forms Glovebox writes (crates/core/src/custom/pack.rs): textures of 16-bit RGBA, objects with one
 * key per track, a mesh with face normals, and a display list of texture loads, G_VTX batches and triangles. The
 * texels, vertices and ids are made up here. Nothing in them comes from the ROM.
 *
 * Use: bb_begin, then bb_texture for each texture, bb_object for each object (children before their parents), and
 * bb_entry for each directory entry. bb_end writes the file. */
#ifndef G64_BANKBUILD_H
#define G64_BANKBUILD_H

#include <stdint.h>
#include <string.h>

#define BB_BANK (1 << 21)
#define BB_ENTRIES 16
#define BB_TEXTURES 8

typedef struct {
    uint8_t tex[BB_BANK], obj[BB_BANK];
    uint32_t tn, on;
    uint32_t texId[BB_TEXTURES];
    uint16_t texW[BB_TEXTURES], texH[BB_TEXTURES];
    unsigned ntex, nentries, maxEntries;
    int full; /* a structure did not fit */
} BB;

/* One object: its name, flags, mesh size and display list. The display list loads each texture of tex in turn, with
 * batches G_VTX batches after each load. child and sibling are offsets from earlier bb_object calls, or 0. */
typedef struct {
    const char *name;
    uint16_t flags;
    unsigned verts, faces;
    unsigned ntex;
    uint32_t tex[4];
    unsigned batches;
    unsigned reloads; /* after that, this many more loads of the first texture, each with one triangle */
    uint32_t child, sibling;
    float pos[3], scale[3];
} BBObject;

static void bb_put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void bb_put16(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void bb_putf(uint8_t *p, float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    bb_put32(p, u);
}

static void bb_cmd(uint8_t *p, uint32_t w0, uint32_t w1) {
    bb_put32(p, w0);
    bb_put32(p + 4, w1);
}

/* Starts a file whose directory has room for entries entries. */
static void bb_begin(BB *b, unsigned entries) {
    memset(b, 0, sizeof *b);
    b->maxEntries = entries;
    b->on = (entries + 1) * 8;
    b->tn = 4;
}

/* size bytes of the object bank, at a multiple of 8, as Glovebox's linker lays out its blocks. */
static uint32_t bb_alloc(BB *b, uint32_t size) {
    uint32_t at = b->on;
    if (at + size > BB_BANK) {
        b->full = 1;
        return 8;
    }
    b->on = (at + size + 7) & ~7u;
    return at;
}

static unsigned bb_log2(unsigned v) {
    unsigned k = 0;
    while (v > 1) {
        v >>= 1;
        k++;
    }
    return k;
}

/* A w by h texture with made-up texels: two colors in stripes, or one color when one is set. */
static void bb_texture(BB *b, uint32_t id, unsigned w, unsigned h, int one) {
    uint32_t size = (36 + 2 * w * h + 7) & ~7u, i;
    uint8_t *r = b->tex + b->tn;
    if (b->ntex == BB_TEXTURES || b->tn + size + 4 > BB_BANK) {
        b->full = 1;
        return;
    }
    bb_put32(r, id);
    bb_put16(r + 12, w);
    bb_put16(r + 14, h);
    bb_put16(r + 16, bb_log2(w));
    bb_put16(r + 18, bb_log2(h));
    bb_put32(r + 20, size);
    bb_put16(r + 26, 2);
    bb_put32(r + 28, 36);
    for (i = 0; i < w * h; i++)
        bb_put16(r + 36 + 2 * i, one || (i / 3) % 2 ? 0x22B1 : (0xEE09 ^ (id & 0xFF)));
    b->tn += size;
    b->texId[b->ntex] = id;
    b->texW[b->ntex] = (uint16_t)w;
    b->texH[b->ntex] = (uint16_t)h;
    b->ntex++;
}

/* Glovebox's set_tile. */
static void bb_set_tile(uint8_t *p, unsigned line, unsigned tile, unsigned w, unsigned h) {
    bb_cmd(p, 0xF5000000u | 2u << 19 | (line & 0x1FF) << 9,
           tile << 24 | (bb_log2(h) & 15) << 14 | (bb_log2(w) & 15) << 4);
}

/* Glovebox's texture_load: 9 commands. */
static uint32_t bb_texture_load(uint8_t *p, uint32_t id, unsigned w, unsigned h) {
    unsigned row = w * 2 / 8 ? w * 2 / 8 : 1, dxt = (2048 + row - 1) / row;
    bb_cmd(p, 0xE6000000u, 0);
    bb_cmd(p + 8, 0xBA000E02u, 0);
    bb_cmd(p + 16, 0xFD100000u, id);
    bb_set_tile(p + 24, 0, 7, w, h);
    bb_cmd(p + 32, 0xE6000000u, 0);
    bb_cmd(p + 40, 0xF3000000u, 7u << 24 | ((w * h - 1) & 0xFFF) << 12 | (dxt & 0xFFF));
    bb_cmd(p + 48, 0xE7000000u, 0);
    bb_set_tile(p + 56, (w * 2 + 7) >> 3, 0, w, h);
    bb_cmd(p + 64, 0xF2000000u, (((w - 1) << 2) & 0xFFF) << 12 | (((h - 1) << 2) & 0xFFF));
    return 72;
}

static int bb_find(const BB *b, uint32_t id) {
    unsigned k;
    for (k = 0; k < b->ntex; k++)
        if (b->texId[k] == id)
            return (int)k;
    return 0;
}

/* Writes an object and everything it points to. Returns the object's offset. */
static uint32_t bb_object(BB *b, const BBObject *s) {
    static uint8_t dl[1 << 18];
    uint32_t len = 0, vtxAt[32], nblocks = 0, blockN[32], i, k;
    uint32_t scale, pos, rot, verts, faces, colours, mesh, list, obj;
    const char *name = s->name ? s->name : "piece";

    scale = bb_alloc(b, 20);
    bb_putf(b->obj + scale, s->scale[0] ? s->scale[0] : 1.0f);
    bb_putf(b->obj + scale + 4, s->scale[1] ? s->scale[1] : 1.0f);
    bb_putf(b->obj + scale + 8, s->scale[2] ? s->scale[2] : 1.0f);
    pos = bb_alloc(b, 20);
    for (i = 0; i < 3; i++)
        bb_putf(b->obj + pos + 4 * i, s->pos[i]);
    rot = bb_alloc(b, 20);
    bb_putf(b->obj + rot + 12, 1.0f);
    verts = bb_alloc(b, s->verts * 12);
    for (i = 0; i < s->verts && !b->full; i++) {
        bb_putf(b->obj + verts + 12 * i, (float)(i % 5) * 2.5f - 5.0f);
        bb_putf(b->obj + verts + 12 * i + 4, (float)(i % 3) * 4.0f);
        bb_putf(b->obj + verts + 12 * i + 8, -(float)i * 1.5f);
    }
    faces = bb_alloc(b, s->faces * 6);
    for (i = 0; i < s->faces * 3 && !b->full; i++)
        bb_put16(b->obj + faces + 2 * i, (i + i / 3) % s->verts);
    colours = bb_alloc(b, s->faces * 4);
    for (i = 0; i < s->faces && !b->full; i++)
        b->obj[colours + 4 * i + (i % 3)] = 0x7F;

    /* The display list, then its vertex blocks right after it. */
    bb_cmd(dl + len, 0xB6000000u, 0x20000);
    len += 8;
    bb_cmd(dl + len, 0xB7000000u, 0x20000);
    len += 8;
    for (k = 0; k < s->ntex && k < 4; k++) {
        int t = bb_find(b, s->tex[k]);
        unsigned w = b->texW[t], h = b->texH[t], j;
        len += bb_texture_load(dl + len, s->tex[k], w, h);
        for (j = 0; j < (s->batches ? s->batches : 1) && nblocks < 32 && len < sizeof dl - 64; j++) {
            unsigned n = 3 + (nblocks * 7) % 30;
            bb_cmd(dl + len, 0x04000000u | n << 10 | (n * 16 - 1), 0);
            vtxAt[nblocks] = len + 4;
            blockN[nblocks++] = n;
            len += 8;
            if (j == 0) {
                bb_cmd(dl + len, 0xE8000000u, 0);
                bb_set_tile(dl + len + 8, (w * 2) >> 3, 0, w, h);
                len += 16;
            }
            bb_cmd(dl + len, 0xB2140000u | 2, 0x00200000);
            bb_cmd(dl + len + 8, 0xB1000204u, 0x00020400);
            bb_cmd(dl + len + 16, 0xBF000000u, 0x00000204);
            len += 24;
        }
    }
    for (k = 0; k < s->reloads && s->ntex && len < sizeof dl - 128; k++) {
        int t = bb_find(b, s->tex[0]);
        len += bb_texture_load(dl + len, s->tex[0], b->texW[t], b->texH[t]);
        bb_cmd(dl + len, 0xBF000000u, 0x00000204);
        len += 8;
    }
    bb_cmd(dl + len, 0xB8000000u, 0);
    len += 8;
    list = bb_alloc(b, len);
    if (!b->full)
        memcpy(b->obj + list, dl, len);
    for (i = 0; i < nblocks; i++) {
        uint32_t at = bb_alloc(b, blockN[i] * 16);
        for (k = 0; k < blockN[i] * 16 && !b->full; k++)
            b->obj[at + k] = (uint8_t)(k * 7 + i);
        if (!b->full)
            bb_put32(b->obj + list + vtxAt[i], at);
    }

    mesh = bb_alloc(b, 36);
    obj = bb_alloc(b, 64);
    if (b->full)
        return 8;
    bb_put16(b->obj + mesh, s->faces);
    bb_put16(b->obj + mesh + 2, s->verts);
    bb_put32(b->obj + mesh + 4, verts);
    bb_put32(b->obj + mesh + 8, faces);
    bb_put32(b->obj + mesh + 12, colours);

    bb_put32(b->obj + obj, 0x5EED0000u + obj);
    for (i = 0; i < 8 && name[i]; i++)
        b->obj[obj + 4 + i] = (uint8_t)name[i];
    b->obj[obj + 0x0D] = 0xFF;
    bb_put16(b->obj + obj + 0x0E, 1);
    bb_put16(b->obj + obj + 0x10, 1);
    bb_put16(b->obj + obj + 0x12, 1);
    bb_put32(b->obj + obj + 0x14, mesh);
    bb_put32(b->obj + obj + 0x18, list);
    bb_put32(b->obj + obj + 0x1C, scale);
    bb_put32(b->obj + obj + 0x20, pos);
    bb_put32(b->obj + obj + 0x24, rot);
    bb_put16(b->obj + obj + 0x32, s->flags);
    bb_put32(b->obj + obj + 0x34, s->child);
    bb_put32(b->obj + obj + 0x38, s->sibling);
    return obj;
}

/* A directory entry for id, with its instance. anim: give the instance an animation state of zeros. */
static void bb_entry(BB *b, uint32_t id, uint32_t obj, int anim) {
    uint32_t inst, state = 0;
    if (b->nentries == b->maxEntries) {
        b->full = 1;
        return;
    }
    if (anim)
        state = bb_alloc(b, 80);
    inst = bb_alloc(b, 28);
    if (b->full)
        return;
    bb_put32(b->obj + inst, id);
    bb_put32(b->obj + inst + 0x0C, obj);
    bb_put32(b->obj + inst + 0x18, state);
    bb_put32(b->obj + b->nentries * 8, id);
    bb_put32(b->obj + b->nentries * 8 + 4, inst);
    b->nentries++;
}

/* Writes models.bin into out, which has room for cap bytes, with the retail numbers given. Returns its size, or 0
 * when it does not fit. With no texture and no entry the file has no banks of its own. */
static size_t bb_end(BB *b, uint8_t *out, size_t cap, uint8_t flags, const uint8_t *rtex, unsigned a,
                     const uint8_t *robj, unsigned nobj) {
    uint32_t h = (12 + a + nobj + 7) & ~7u, t = 0, o = 0;
    size_t n;
    if (b->ntex || b->nentries) {
        bb_put32(b->tex, b->ntex);
        t = (b->tn + 4 + 7) & ~7u;
        o = (b->on + 7) & ~7u;
    }
    n = (size_t)h + t + o;
    if (b->full || n > cap)
        return 0;
    memset(out, 0, n);
    bb_put32(out, t);
    bb_put32(out + 4, o);
    out[8] = flags;
    out[9] = (uint8_t)a;
    out[10] = (uint8_t)nobj;
    if (a)
        memcpy(out + 12, rtex, a);
    if (nobj)
        memcpy(out + 12 + a, robj, nobj);
    if (t) {
        memcpy(out + h, b->tex, b->tn);
        memcpy(out + h + t, b->obj, b->on);
    }
    return n;
}

#endif
