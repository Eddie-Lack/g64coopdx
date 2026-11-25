/* The index of the ROM's banks (rombanks.h). It uses no game code: the bank ranges come from a callback. */
#include "rombanks.h"

#include <stdlib.h>
#include <string.h>

#include "../../third_party/monocypher/monocypher.h"
#include "fla2.h"

/* A pointer in a bank keeps its offset in the low 24 bits (the game's Rom2Ram), so no bank is larger. */
#define BANK_MAX (1u << 24)

typedef struct {
    uint8_t *p;
    size_t n, cap, elem;
} Vec;

struct G64RomBanks {
    Vec objectIds, textureIds, texels, verts, vtx;
    /* Each object bank's ids, as pairs of the bank's number and an id (BankId), sorted. */
    Vec bankIds;
    /* The texture ids named by the sprites of the objects in the bank being walked, and then those ids of each
       object bank (BankId), sorted. */
    Vec spriteIds, bankSprites;
    /* Each texture bank's texture ids (BankId), sorted. */
    Vec bankTextures;
    G64RomBanksStats st;
    int oom;
};

static int vec_push(G64RomBanks *b, Vec *v, const void *x) {
    if (v->n == v->cap) {
        size_t cap = v->cap ? v->cap * 2 : 256;
        uint8_t *p = realloc(v->p, cap * v->elem);
        if (p == NULL) {
            b->oom = 1;
            return 0;
        }
        v->p = p;
        v->cap = cap;
    }
    memcpy(v->p + v->n * v->elem, x, v->elem);
    v->n++;
    return 1;
}

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

typedef struct {
    uint32_t num, id;
} BankId;

static int cmp_bank_id(const void *a, const void *b) {
    const BankId *x = a, *y = b;
    if (x->num != y->num)
        return x->num < y->num ? -1 : 1;
    return x->id < y->id ? -1 : x->id > y->id;
}

static int cmp_h16(const void *a, const void *b) {
    return memcmp(a, b, 16);
}

/* Sorts a vector and drops repeats. */
static void vec_unique(Vec *v, int (*cmp)(const void *, const void *)) {
    size_t i, k = 0;

    if (v->n == 0)
        return;
    qsort(v->p, v->n, v->elem, cmp);
    for (i = 1; i < v->n; i++)
        if (cmp(v->p + k * v->elem, v->p + i * v->elem) != 0) {
            k++;
            memmove(v->p + k * v->elem, v->p + i * v->elem, v->elem);
        }
    v->n = k + 1;
}

static int vec_has(const Vec *v, const void *x, int (*cmp)(const void *, const void *)) {
    return v->n != 0 && bsearch(x, v->p, v->n, v->elem, cmp) != NULL;
}

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint32_t be16(const uint8_t *p) {
    return (uint32_t)p[0] << 8 | p[1];
}

/* Whether n bytes at off lie inside a bank of size bytes. */
static int inside(size_t off, size_t n, size_t size) {
    return off <= size && n <= size - off;
}

void g64_rombanks_hash(uint8_t hash[16], const uint8_t *p, size_t n) {
    crypto_blake2b(hash, 16, p, n);
}

static int push_hash(G64RomBanks *b, Vec *v, const uint8_t *p, size_t n) {
    uint8_t h[16];

    g64_rombanks_hash(h, p, n);
    return vec_push(b, v, h);
}

/* ---- Texture banks: a count, then each record: a 36-byte header, the texels, and maybe a palette. The walk steps
   by each record's size, as LoadTextureBankAt does. Returns 0 on a fault. */
static int walk_textures(G64RomBanks *b, const uint8_t *d, size_t n) {
    uint32_t count, i;
    size_t p = 4;

    if (n < 4)
        return 0;
    count = be32(d);
    if (count > (n - 4) / 36)
        return 0;
    for (i = 0; i < count; i++) {
        uint32_t crc, w, h, siz, pal, size;
        size_t end, bytes;

        if (!inside(p, 36, n))
            return 0;
        crc = be32(d + p);
        w = be16(d + p + 0x0C);
        h = be16(d + p + 0x0E);
        size = be32(d + p + 0x14);
        siz = be16(d + p + 0x1A);
        pal = be32(d + p + 0x20);
        if (size < 36 || size > 0x7FFFFFFF || !inside(p, size, n))
            return 0;
        if (!vec_push(b, &b->textureIds, &crc))
            return 0;
        /* The texels end at the palette when it lies in the record, as the port's converter reads it. */
        end = p + size;
        if (pal != 0 && pal >= 36 && pal <= size)
            end = p + pal;
        bytes = siz <= 3 ? (((size_t)w * h) << siz) / 2 : 0;
        if (bytes == 0 || bytes > end - (p + 36))
            b->st.unhashedTextures++;
        else if (!push_hash(b, &b->texels, d + p + 36, bytes))
            return 0;
        p += size;
    }
    return 1;
}

/* ---- Object banks. */

/* The most bytes the hashes of one object bank may cover: four times the bank, and 1 MiB more. The vanilla banks
   hash at most 0.6 times their size. */
#define HASH_BUDGET(n) ((size_t)(n) * 4 + ((size_t)1 << 20))

typedef struct {
    G64RomBanks *b;
    const uint8_t *d;
    size_t n;
    uint8_t *seen;     /* one bit per byte offset: the objects already put on the stack */
    uint8_t *listSeen; /* one bit per byte offset: the display list commands already walked */
    uint8_t *meshSeen; /* one bit per byte offset: the meshes already walked */
    uint32_t *stack;
    size_t top, cap;
    size_t hashed, budget; /* the bytes hashed so far in this bank, and the most it may hash */
} Walk;

/* Hashes len bytes at off into v. A bank whose hashes would cover more than its budget is a fault. Many meshes or
   commands can point at one large array, and the budget keeps the work linear in the bank's size. */
static int walk_hash(Walk *w, Vec *v, size_t off, size_t len) {
    if (!inside(off, len, w->n) || len > w->budget - w->hashed)
        return 0;
    w->hashed += len;
    return push_hash(w->b, v, w->d + off, len);
}

/* A display list, as RestoreDrawList reads it: commands of 8 bytes up to G_ENDDL (0xB8). It steps over the rest of
   a texture load after G_SETTIMG (0xFD), by the opcode two words on. The walk from a command depends only on where
   the command is. So a walk that reaches a command walked before stops there: the rest was walked and hashed. */
static int walk_list(Walk *w, size_t pos) {
    for (;;) {
        uint32_t w0, w1;

        /* pos only grows, so the walk ends at the end of the bank. */
        if (!inside(pos, 8, w->n))
            return 0;
        if (w->listSeen[pos >> 3] & (1 << (pos & 7)))
            return 1;
        w->listSeen[pos >> 3] |= (uint8_t)(1 << (pos & 7));
        w0 = be32(w->d + pos);
        w1 = be32(w->d + pos + 4);
        switch (w0 >> 24) {
            case 0xB8:
                return 1;
            case 0x04: {
                size_t at = w1 & 0xFFFFFF, len = (size_t)((w0 >> 10) & 0x3F) * 16;
                if (len != 0 && !walk_hash(w, &w->b->vtx, at, len))
                    return 0;
                pos += 8;
                break;
            }
            case 0xFD: {
                uint32_t next;
                if (!inside(pos + 8, 4, w->n))
                    return 0;
                next = w->d[pos + 8];
                pos += next == 0xF5 ? 0x38 : next == 0xE8 ? 0x30 : 0x10;
                break;
            }
            default:
                pos += 8;
                break;
        }
    }
}

/* An object is marked when it goes on the stack, so each offset goes on it once at most. An offset outside the bank
   goes on it unmarked, and the walk fails when it comes off. */
static int push_object(Walk *w, uint32_t off) {
    if (inside(off, 64, w->n)) {
        if (w->seen[off >> 3] & (1 << (off & 7)))
            return 1;
        w->seen[off >> 3] |= (uint8_t)(1 << (off & 7));
    }
    if (w->top == w->cap) {
        size_t cap = w->cap ? w->cap * 2 : 64;
        uint32_t *s = realloc(w->stack, cap * sizeof *s);
        if (s == NULL) {
            w->b->oom = 1;
            return 0;
        }
        w->stack = s;
        w->cap = cap;
    }
    w->stack[w->top++] = off;
    return 1;
}

/* An object's sprites, as RestoreObjectPointers reads them: numSprites records of 0x18 bytes, each with a texture id
   in its first word. The game looks each id up with FindTexture. Many objects can share one array, so the bytes read
   count against the bank's budget, as the hashes do. */
static int walk_sprites(Walk *w, const uint8_t *o) {
    int32_t count = (int32_t)be32(o + 0x28);
    size_t at = be32(o + 0x2C) & 0xFFFFFF, len, i;

    if (be32(o + 0x2C) == 0 || count <= 0)
        return 1;
    len = (size_t)count * 0x18;
    if ((size_t)count > w->n / 0x18 || !inside(at, len, w->n) || len > w->budget - w->hashed)
        return 0;
    w->hashed += len;
    for (i = 0; i < (size_t)count; i++) {
        uint32_t id = be32(w->d + at + i * 0x18);
        if (!vec_push(w->b, &w->b->spriteIds, &id))
            return 0;
    }
    return 1;
}

/* An object tree, as RestoreObjectPointers walks it: the mesh, the sprites, the display list, then the child and the
   sibling. An object reached twice is walked once, and so is a mesh. */
static int walk_tree(Walk *w, uint32_t root) {
    w->top = 0;
    if (!push_object(w, root))
        return 0;
    while (w->top) {
        uint32_t off = w->stack[--w->top];
        const uint8_t *o;
        uint32_t mesh, list, child, sibling;

        if (!inside(off, 64, w->n))
            return 0;
        o = w->d + off;
        mesh = be32(o + 0x14);
        list = be32(o + 0x18);
        child = be32(o + 0x34);
        sibling = be32(o + 0x38);
        if (mesh) {
            size_t m = mesh & 0xFFFFFF;
            int32_t numVerts;
            if (!inside(m, 36, w->n))
                return 0;
            numVerts = (int16_t)be16(w->d + m + 2);
            if (!(w->meshSeen[m >> 3] & (1 << (m & 7))) && numVerts > 0) {
                size_t v = be32(w->d + m + 4) & 0xFFFFFF, len = (size_t)numVerts * 12;
                if (!walk_hash(w, &w->b->verts, v, len))
                    return 0;
            }
            w->meshSeen[m >> 3] |= (uint8_t)(1 << (m & 7));
        }
        if (!walk_sprites(w, o))
            return 0;
        if (list && !walk_list(w, list & 0xFFFFFF))
            return 0;
        /* The sibling goes on the stack first, so the child is walked first, as in the game. */
        if (sibling && !push_object(w, sibling & 0xFFFFFF))
            return 0;
        if (child && !push_object(w, child & 0xFFFFFF))
            return 0;
    }
    return 1;
}

/* The directory: entries of an id and an instance, up to an entry whose instance is 0, as LoadObjectBankAt reads
   it. Each instance names its object tree at byte 12. Returns 0 on a fault. */
static int walk_objects(G64RomBanks *b, const uint8_t *d, size_t n) {
    Walk w;
    size_t e;
    int ok = 0;

    memset(&w, 0, sizeof w);
    w.b = b;
    w.d = d;
    w.n = n;
    w.budget = HASH_BUDGET(n);
    w.seen = calloc(n / 8 + 1, 1);
    w.listSeen = calloc(n / 8 + 1, 1);
    w.meshSeen = calloc(n / 8 + 1, 1);
    if (w.seen == NULL || w.listSeen == NULL || w.meshSeen == NULL) {
        free(w.seen);
        free(w.listSeen);
        free(w.meshSeen);
        b->oom = 1;
        return 0;
    }
    for (e = 0;; e += 8) {
        uint32_t crc, inst;
        size_t io;

        if (!inside(e, 8, n))
            goto done;
        crc = be32(d + e);
        inst = be32(d + e + 4);
        if (inst == 0)
            break;
        if (!vec_push(b, &b->objectIds, &crc))
            goto done;
        io = inst & 0xFFFFFF;
        if (!inside(io, 28, n) || !walk_tree(&w, be32(d + io + 12) & 0xFFFFFF))
            goto done;
    }
    ok = 1;
done:
    free(w.seen);
    free(w.listSeen);
    free(w.meshSeen);
    free(w.stack);
    return ok;
}

/* ---- The build. */

static void index_bank(G64RomBanks *b, int kind, const uint8_t *rom, size_t romSize, uint32_t start, uint32_t end) {
    size_t mark[6] = {b->objectIds.n, b->textureIds.n, b->texels.n, b->verts.n, b->vtx.n, b->spriteIds.n};
    unsigned unhashed = b->st.unhashedTextures;
    const uint8_t *d;
    uint8_t *buf = NULL;
    size_t n;
    int ok;

    if (start >= end || end > romSize || end - start > BANK_MAX) {
        b->st.skipped[kind]++;
        return;
    }
    d = rom + start;
    n = end - start;
    if (g64_fla2_is(d, n)) {
        size_t size;
        if (g64_fla2_size(d, n, BANK_MAX, &size) != NULL) {
            b->st.skipped[kind]++;
            return;
        }
        buf = malloc(size ? size : 1);
        if (buf == NULL) {
            b->oom = 1;
            return;
        }
        if (g64_fla2_decompress(d, n, buf, size) != NULL) {
            free(buf);
            b->st.skipped[kind]++;
            return;
        }
        d = buf;
        n = size;
    }
    ok = kind == G64_BANK_TEXTURE ? walk_textures(b, d, n) : walk_objects(b, d, n);
    if (ok) {
        b->st.banks[kind]++;
        if (buf)
            b->st.compressed++;
    } else {
        /* A bank counts whole or not at all. */
        b->objectIds.n = mark[0];
        b->textureIds.n = mark[1];
        b->texels.n = mark[2];
        b->verts.n = mark[3];
        b->vtx.n = mark[4];
        b->spriteIds.n = mark[5];
        b->st.unhashedTextures = unhashed;
        b->st.skipped[kind]++;
    }
    free(buf);
}

/* Pairs num with each id that the walk of bank num put in from, at index first and after. The pairs go in to. */
static void pair_ids(G64RomBanks *b, Vec *to, const Vec *from, size_t first, unsigned num) {
    for (; first < from->n; first++) {
        BankId e;
        e.num = num;
        memcpy(&e.id, from->p + first * 4, 4);
        vec_push(b, to, &e);
    }
}

/* Gives bank num a copy of each pair in v that belongs to bank was, an earlier number. */
static void copy_pairs(G64RomBanks *b, Vec *v, unsigned was, unsigned num) {
    size_t i, have = v->n;
    for (i = 0; i < have; i++) {
        BankId e = ((const BankId *)(const void *)v->p)[i];
        if (e.num == was) {
            e.num = num;
            vec_push(b, v, &e);
        }
    }
}

G64RomBanks *g64_rombanks_build(const uint8_t *rom, size_t romSize, g64_bank_range_fn range, void *user) {
    G64RomBanks *b = calloc(1, sizeof *b);
    uint32_t seenStart[2][256], seenEnd[2][256];
    unsigned seenNum[2][256], seenCount[2] = {0, 0};
    size_t first, firstTex;
    int kind;
    unsigned num, k;

    if (b == NULL)
        return NULL;
    b->objectIds.elem = b->textureIds.elem = b->spriteIds.elem = 4;
    b->bankIds.elem = b->bankSprites.elem = b->bankTextures.elem = sizeof(BankId);
    b->texels.elem = b->verts.elem = b->vtx.elem = 16;
    for (kind = 0; kind < 2; kind++) {
        for (num = 0; num < 256 && !b->oom; num++) {
            uint32_t start = 0, end = 0;
            if (!range(user, kind, num, &start, &end))
                continue;
            /* Two numbers can name one range. It's indexed once, and the second number gets the first one's ids. */
            for (k = 0; k < seenCount[kind]; k++)
                if (seenStart[kind][k] == start && seenEnd[kind][k] == end)
                    break;
            if (k < seenCount[kind]) {
                if (kind == G64_BANK_OBJECT) {
                    copy_pairs(b, &b->bankIds, seenNum[kind][k], num);
                    copy_pairs(b, &b->bankSprites, seenNum[kind][k], num);
                } else {
                    copy_pairs(b, &b->bankTextures, seenNum[kind][k], num);
                }
                continue;
            }
            seenStart[kind][seenCount[kind]] = start;
            seenEnd[kind][seenCount[kind]] = end;
            seenNum[kind][seenCount[kind]] = num;
            seenCount[kind]++;
            first = b->objectIds.n;
            firstTex = b->textureIds.n;
            b->spriteIds.n = 0;
            index_bank(b, kind, rom, romSize, start, end);
            /* A bank that was skipped took its ids back out. */
            if (kind == G64_BANK_OBJECT) {
                pair_ids(b, &b->bankIds, &b->objectIds, first, num);
                pair_ids(b, &b->bankSprites, &b->spriteIds, 0, num);
            } else {
                pair_ids(b, &b->bankTextures, &b->textureIds, firstTex, num);
            }
        }
    }
    if (b->oom) {
        g64_rombanks_free(b);
        return NULL;
    }
    vec_unique(&b->objectIds, cmp_u32);
    vec_unique(&b->bankIds, cmp_bank_id);
    vec_unique(&b->bankSprites, cmp_bank_id);
    vec_unique(&b->bankTextures, cmp_bank_id);
    vec_unique(&b->textureIds, cmp_u32);
    vec_unique(&b->texels, cmp_h16);
    vec_unique(&b->verts, cmp_h16);
    vec_unique(&b->vtx, cmp_h16);
    b->st.objectIds = (unsigned)b->objectIds.n;
    b->st.textureIds = (unsigned)b->textureIds.n;
    b->st.texels = (unsigned)b->texels.n;
    b->st.verts = (unsigned)b->verts.n;
    b->st.vtx = (unsigned)b->vtx.n;
    return b;
}

void g64_rombanks_free(G64RomBanks *b) {
    if (b == NULL)
        return;
    free(b->objectIds.p);
    free(b->bankIds.p);
    free(b->spriteIds.p);
    free(b->bankSprites.p);
    free(b->bankTextures.p);
    free(b->textureIds.p);
    free(b->texels.p);
    free(b->verts.p);
    free(b->vtx.p);
    free(b);
}

void g64_rombanks_stats(const G64RomBanks *b, G64RomBanksStats *s) {
    *s = b->st;
}

int g64_rombanks_complete(const G64RomBanksStats *s) {
    return s->banks[0] != 0 && s->banks[1] != 0 && s->skipped[0] == 0 && s->skipped[1] == 0;
}

int g64_rombanks_has_object_id(const G64RomBanks *b, uint32_t id) {
    return vec_has(&b->objectIds, &id, cmp_u32);
}

int g64_rombanks_bank_has_object(const G64RomBanks *b, unsigned num, uint32_t id) {
    BankId e;
    e.num = num;
    e.id = id;
    return vec_has(&b->bankIds, &e, cmp_bank_id);
}

size_t g64_rombanks_bank_object_count(const G64RomBanks *b, unsigned num) {
    size_t i, n = 0;
    for (i = 0; i < b->bankIds.n; i++)
        n += ((const BankId *)(const void *)b->bankIds.p)[i].num == num;
    return n;
}

int g64_rombanks_bank_has(void *user, unsigned num, uint32_t id) {
    return g64_rombanks_bank_has_object((const G64RomBanks *)user, num, id);
}

int g64_rombanks_has_texture_id(const G64RomBanks *b, uint32_t id) {
    return vec_has(&b->textureIds, &id, cmp_u32);
}

int g64_rombanks_texture_bank_has(const G64RomBanks *b, unsigned num, uint32_t id) {
    BankId e;
    e.num = num;
    e.id = id;
    return vec_has(&b->bankTextures, &e, cmp_bank_id);
}

size_t g64_rombanks_sprite_textures(const G64RomBanks *b, unsigned num, uint32_t *ids, size_t max) {
    size_t i, n = 0;
    for (i = 0; i < b->bankSprites.n; i++) {
        const BankId *e = (const BankId *)(const void *)b->bankSprites.p + i;
        if (e->num == num) {
            if (n < max)
                ids[n] = e->id;
            n++;
        }
    }
    return n;
}

int g64_rombanks_has_texels_hash(const G64RomBanks *b, const uint8_t *hash) {
    return vec_has(&b->texels, hash, cmp_h16);
}

int g64_rombanks_has_verts_hash(const G64RomBanks *b, const uint8_t *hash) {
    return vec_has(&b->verts, hash, cmp_h16);
}

int g64_rombanks_has_vtx_hash(const G64RomBanks *b, const uint8_t *hash) {
    return vec_has(&b->vtx, hash, cmp_h16);
}

const uint32_t *g64_rombanks_object_ids(const G64RomBanks *b, size_t *count) {
    *count = b->objectIds.n;
    return (const uint32_t *)(void *)b->objectIds.p;
}

const uint32_t *g64_rombanks_texture_ids(const G64RomBanks *b, size_t *count) {
    *count = b->textureIds.n;
    return (const uint32_t *)(void *)b->textureIds.p;
}
