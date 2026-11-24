/* The check of a pack's models.bin (src/client/bankcheck.h), without the game.
 *
 *   - Glovebox's real output, tests/fixtures/cube.models.bin, passes. Its path is the first argument.
 *   - Files made here pass: a tree of objects, two textures, several G_VTX batches, retail numbers.
 *   - One change to the cube for each rule fails with that rule's message, at the offset of the fault.
 *   - The rules that need the ROM, with an index built from a made-up ROM that holds the cube's own banks.
 *   - The shadow walk (tests/fuzz/bankwalk.h) passes every accepted file, and stops on banks that the check refuses
 *     for a reason the game's loaders would trip on.
 *
 * Nothing here comes from the ROM. The output must be equal on Linux and under Wine. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/bankcheck.h"
#include "../../src/client/rombanks.h"
#include "../fuzz/bankwalk.h"
#include "bankbuild.h"

static uint8_t fixture[4096];
static size_t fixtureN;
static uint8_t buf[1 << 18];
static size_t bufN;
static int failures;

/* The bank numbers of the game's switch: the cases of TextureBankRange and ObjectBankRange in weemitt's
   src/game/banks.c. The program that links the game asks G64BankRom instead. */
static int game_banks(void *user, int kind, unsigned num, uint32_t *start, uint32_t *end) {
    static const uint8_t objects[] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 17, 18, 19,
                                      20, 21, 23, 25, 26, 27, 29, 30, 31, 33, 35, 38, 39, 40, 43, 44, 45, 46, 47,
                                      49, 52, 53, 54, 55, 56, 58, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71};
    size_t i;
    (void)user;
    *start = *end = 0;
    if (kind == G64_BANK_TEXTURE)
        return num <= 18;
    for (i = 0; i < sizeof objects; i++)
        if (objects[i] == num)
            return 1;
    return 0;
}

/* Checks a file and the shadow walk on it. want is NULL when the file must pass. */
static void expect(const char *what, const uint8_t *f, size_t n, const char *want, size_t wantAt) {
    G64Models m;
    size_t at = 0;
    const char *why = g64_models_check(f, n, game_banks, NULL, &m, &at);
    if (why)
        printf("%s: %s at %u\n", what, why, (unsigned)at);
    else
        printf("%s: accepted\n", what);
    if (want == NULL ? why != NULL : (why == NULL || strcmp(why, want) != 0 || at != wantAt)) {
        printf("FAIL: %s: wanted %s at %u\n", what, want ? want : "accepted", (unsigned)wantAt);
        failures++;
    }
    if (why == NULL && m.texSize) {
        int bank = 0;
        const char *walk = g64_bank_shadow(f + m.texAt, m.texSize, f + m.objAt, m.objSize, &at, &bank);
        if (walk) {
            printf("FAIL: %s: the shadow walk stopped: %s in bank %d at %u\n", what, walk, bank, (unsigned)at);
            failures++;
        }
    }
}

static void put32(uint8_t *p, uint32_t v) {
    bb_put32(p, v);
}

static void putf(uint8_t *p, float f) {
    bb_putf(p, f);
}

/* A fresh copy of the cube to change. */
static uint8_t *cube(void) {
    memcpy(buf, fixture, fixtureN);
    bufN = fixtureN;
    return buf;
}

/* The cube's layout (tests/fixtures/README.md): the header is 16 bytes, the texture bank 176 bytes from 16, and the
   object bank 1056 bytes from 192. Offsets below are in the file. */
enum { TEX = 16, REC = 20, TEXELS = 56, OBJ = 192, DL = 208, FD = 240, GVTX = 296, ENDDL = 464, VTXBLOCK = 472,
       VERTS = 600, FACES = 840, FACECOLOURS = 912, MESH = 960, POSKEY = 1000, ROTKEY = 1024, SCALEKEY = 1048,
       OBJECT = 1072, ANIM = 1136, INSTANCE = 1216 };

static void header_only(const char *what, uint8_t flags, const uint8_t *tex, unsigned a, const uint8_t *obj,
                        unsigned b, const char *want, size_t wantAt) {
    static BB bb;
    size_t n;
    bb_begin(&bb, 1);
    n = bb_end(&bb, buf, sizeof buf, flags, tex, a, obj, b);
    expect(what, buf, n, want, wantAt);
}

static void valid_files(void) {
    static BB b;
    BBObject s;
    uint32_t leaf, mid, root, o[12];
    size_t n;
    unsigned i;
    static const uint8_t rt[] = {3, 4}, ro[] = {12, 13};

    expect("the cube", fixture, fixtureN, NULL, 0);

    cube();
    buf[9] = 1;
    buf[10] = 1;
    buf[12] = 3;
    buf[13] = 12;
    expect("the cube with a retail texture bank and a retail object bank", buf, bufN, NULL, 0);

    header_only("a file with only retail numbers", 1, rt, 2, ro, 2, NULL, 0);
    header_only("a file with retail texture bank 0, the title's", 0, (const uint8_t *)"\0", 1, NULL, 0, NULL, 0);

    bb_begin(&b, 1);
    bb_texture(&b, 0x0A000001, 8, 8, 0);
    memset(&s, 0, sizeof s);
    s.verts = 8;
    s.faces = 12;
    s.ntex = 1;
    s.tex[0] = 0x0A000001;
    bb_entry(&b, 0x0B000001, bb_object(&b, &s), 1);
    n = bb_end(&b, buf, sizeof buf, 0, NULL, 0, NULL, 0);
    expect("a made cube", buf, n, NULL, 0);

    bb_begin(&b, 1);
    bb_texture(&b, 0x0A000001, 16, 8, 0);
    memset(&s, 0, sizeof s);
    s.name = "leaf";
    s.verts = 5;
    s.faces = 3;
    s.ntex = 1;
    s.tex[0] = 0x0A000001;
    s.flags = 2;
    leaf = bb_object(&b, &s);
    s.name = "mid";
    s.flags = 4;
    s.child = leaf;
    mid = bb_object(&b, &s);
    bb_entry(&b, 0x0B000002, mid, 0);
    n = bb_end(&b, buf, sizeof buf, 0, NULL, 0, NULL, 0);
    expect("two objects in a tree, with no animation state", buf, n, NULL, 0);

    bb_begin(&b, 1);
    bb_texture(&b, 0x0A000001, 2, 2, 0);
    memset(&s, 0, sizeof s);
    s.verts = 6;
    s.faces = 4;
    s.ntex = 1;
    s.tex[0] = 0x0A000001;
    s.name = "b";
    leaf = bb_object(&b, &s);
    s.name = "a";
    s.sibling = leaf;
    mid = bb_object(&b, &s);
    s.name = "root";
    s.sibling = 0;
    s.child = mid;
    root = bb_object(&b, &s);
    bb_entry(&b, 0x0B000003, root, 1);
    n = bb_end(&b, buf, sizeof buf, 0, NULL, 0, NULL, 0);
    expect("a tree of three objects: a child with a sibling", buf, n, NULL, 0);

    bb_begin(&b, 2);
    bb_texture(&b, 0x0A000001, 64, 32, 0);
    bb_texture(&b, 0x0A000002, 4, 4, 1);
    memset(&s, 0, sizeof s);
    s.verts = 40;
    s.faces = 60;
    s.ntex = 2;
    s.tex[0] = 0x0A000002;
    s.tex[1] = 0x0A000001;
    s.batches = 3;
    s.pos[0] = 16384.0f;
    s.scale[1] = 64.0f;
    bb_entry(&b, 0x0B000004, bb_object(&b, &s), 1);
    s.ntex = 1;
    s.batches = 1;
    s.pos[0] = -12.5f;
    s.scale[1] = 0.5f;
    bb_entry(&b, 0x0B000005, bb_object(&b, &s), 0);
    n = bb_end(&b, buf, sizeof buf, 0, rt, 1, NULL, 0);
    expect("two textures, three G_VTX batches each, two entries", buf, n, NULL, 0);

    /* The largest mesh Glovebox writes: 1500 faces, and three vertices for each. */
    bb_begin(&b, 1);
    bb_texture(&b, 0x0A000001, 8, 8, 0);
    memset(&s, 0, sizeof s);
    s.verts = 4500;
    s.faces = 1500;
    s.ntex = 1;
    s.tex[0] = 0x0A000001;
    bb_entry(&b, 0x0B000001, bb_object(&b, &s), 1);
    n = bb_end(&b, buf, sizeof buf, 0, NULL, 0, NULL, 0);
    expect("a mesh of 1500 faces and 4500 vertices", buf, n, NULL, 0);

    /* Ten levels of children is the most. */
    bb_begin(&b, 1);
    bb_texture(&b, 0x0A000001, 8, 8, 0);
    memset(&s, 0, sizeof s);
    s.verts = 3;
    s.faces = 1;
    s.ntex = 1;
    s.tex[0] = 0x0A000001;
    for (i = 0; i < 10; i++) {
        s.child = i ? o[i - 1] : 0;
        o[i] = bb_object(&b, &s);
    }
    bb_entry(&b, 0x0B000006, o[9], 0);
    n = bb_end(&b, buf, sizeof buf, 0, NULL, 0, NULL, 0);
    expect("ten levels of children", buf, n, NULL, 0);

    /* One more is refused at the pointer to the eleventh. */
    bb_begin(&b, 1);
    bb_texture(&b, 0x0A000001, 8, 8, 0);
    for (i = 0; i < 11; i++) {
        s.child = i ? o[i - 1] : 0;
        o[i] = bb_object(&b, &s);
    }
    bb_entry(&b, 0x0B000006, o[10], 0);
    n = bb_end(&b, buf, sizeof buf, 0, NULL, 0, NULL, 0);
    expect("eleven levels of children", buf, n, "the object tree has more than 10 levels of children",
           16 + 176 + o[1] + 0x34);
}

/* The offset in models.bin of an object's position key, for a builder file with no retail numbers. */
static uint32_t pos_at(const BB *b, uint32_t obj) {
    uint32_t p = obj + 0x20;
    return 16 + ((b->tn + 4 + 7) & ~7u) +
           ((uint32_t)b->obj[p] << 24 | (uint32_t)b->obj[p + 1] << 16 | (uint32_t)b->obj[p + 2] << 8 | b->obj[p + 3]);
}

/* A root, and a child of it. Returns the child. */
static uint32_t two_levels(BB *b, float parentX, float parentScale, float childX) {
    BBObject s;
    uint32_t child;
    bb_begin(b, 1);
    bb_texture(b, 0x0A000001, 8, 8, 0);
    memset(&s, 0, sizeof s);
    s.verts = 3;
    s.faces = 1;
    s.ntex = 1;
    s.tex[0] = 0x0A000001;
    s.pos[0] = childX;
    child = bb_object(b, &s);
    s.pos[0] = parentX;
    s.scale[0] = s.scale[1] = s.scale[2] = parentScale;
    s.child = child;
    bb_entry(b, 0x0B000001, bb_object(b, &s), 0);
    return child;
}

/* The translations add up along a tree of objects (TransformAndDrawObject). Their sum must stay in guMtxF2L's
   range. */
static void reach(void) {
    static BB b;
    static uint8_t out[1 << 18];
    BBObject s;
    uint32_t child, a, sib, root;
    size_t n;
    const char *past = "an object's position, added up along its parents, is past 16384";

    child = two_levels(&b, 0, 64.0f, 16384.0f);
    n = bb_end(&b, out, sizeof out, 0, NULL, 0, NULL, 0);
    expect("a child at 16384 of a parent of scale 64", out, n, past, pos_at(&b, child));
    child = two_levels(&b, 0, 2.0f, 16384.0f);
    n = bb_end(&b, out, sizeof out, 0, NULL, 0, NULL, 0);
    expect("a child at 16384 of a parent of scale 2", out, n, past, pos_at(&b, child));
    child = two_levels(&b, 16384.0f, 1.0f, 16384.0f);
    n = bb_end(&b, out, sizeof out, 0, NULL, 0, NULL, 0);
    expect("a child at 16384 of a parent at 16384", out, n, past, pos_at(&b, child));
    child = two_levels(&b, 8192.0f, 1.0f, 8192.0f);
    n = bb_end(&b, out, sizeof out, 0, NULL, 0, NULL, 0);
    expect("a child at 8192 of a parent at 8192: 16384 in all", out, n, NULL, 0);
    child = two_levels(&b, 8192.0f, 2.0f, 4096.0f);
    n = bb_end(&b, out, sizeof out, 0, NULL, 0, NULL, 0);
    expect("a child at 4096 of a parent at 8192 of scale 2: 16384 in all", out, n, NULL, 0);
    child = two_levels(&b, 8192.0f, 2.0f, 4097.0f);
    n = bb_end(&b, out, sizeof out, 0, NULL, 0, NULL, 0);
    expect("a child at 4097 of a parent at 8192 of scale 2", out, n, past, pos_at(&b, child));
    child = two_levels(&b, 0, -64.0f, 256.0f);
    n = bb_end(&b, out, sizeof out, 0, NULL, 0, NULL, 0);
    expect("a child at 256 of a parent of scale -64", out, n, NULL, 0);
    child = two_levels(&b, 0, -64.0f, 257.0f);
    n = bb_end(&b, out, sizeof out, 0, NULL, 0, NULL, 0);
    expect("a child at 257 of a parent of scale -64", out, n, past, pos_at(&b, child));

    /* The length counts, not each axis: a rotation can turn the three axes into one. */
    bb_begin(&b, 1);
    bb_texture(&b, 0x0A000001, 8, 8, 0);
    memset(&s, 0, sizeof s);
    s.verts = 3;
    s.faces = 1;
    s.ntex = 1;
    s.tex[0] = 0x0A000001;
    s.pos[0] = s.pos[1] = s.pos[2] = 16384.0f;
    root = bb_object(&b, &s);
    bb_entry(&b, 0x0B000001, root, 0);
    n = bb_end(&b, out, sizeof out, 0, NULL, 0, NULL, 0);
    expect("a root at 16384 on all three axes", out, n, past, pos_at(&b, root));
    s.pos[0] = s.pos[1] = s.pos[2] = 9000.0f;
    bb_begin(&b, 1);
    bb_texture(&b, 0x0A000001, 8, 8, 0);
    bb_entry(&b, 0x0B000001, bb_object(&b, &s), 0);
    n = bb_end(&b, out, sizeof out, 0, NULL, 0, NULL, 0);
    expect("a root at 9000 on all three axes", out, n, NULL, 0);

    /* Siblings share their parent: one sibling's position does not add to the next one's. */
    bb_begin(&b, 1);
    bb_texture(&b, 0x0A000001, 8, 8, 0);
    memset(&s, 0, sizeof s);
    s.verts = 3;
    s.faces = 1;
    s.ntex = 1;
    s.tex[0] = 0x0A000001;
    s.pos[0] = 16000.0f;
    sib = bb_object(&b, &s);
    s.pos[0] = -16000.0f;
    s.sibling = sib;
    a = bb_object(&b, &s);
    s.pos[0] = 300.0f;
    s.sibling = 0;
    s.child = a;
    root = bb_object(&b, &s);
    bb_entry(&b, 0x0B000001, root, 0);
    n = bb_end(&b, out, sizeof out, 0, NULL, 0, NULL, 0);
    expect("two siblings at 16000 under a root at 300", out, n, NULL, 0);
    /* The sibling's parent is the root: 16000 and 400 are past 16384. */
    bb_begin(&b, 1);
    bb_texture(&b, 0x0A000001, 8, 8, 0);
    s.child = 0;
    s.pos[0] = 16000.0f;
    sib = bb_object(&b, &s);
    s.pos[0] = 0;
    s.sibling = sib;
    a = bb_object(&b, &s);
    s.pos[0] = 400.0f;
    s.sibling = 0;
    s.child = a;
    root = bb_object(&b, &s);
    bb_entry(&b, 0x0B000001, root, 0);
    n = bb_end(&b, out, sizeof out, 0, NULL, 0, NULL, 0);
    expect("a sibling at 16000 under a root at 400", out, n, past, pos_at(&b, sib));
}

/* Files larger than the cube that Glovebox can write. No rule counts faces across the bank, and none caps a list. */
static void large(void) {
    static BB b;
    static uint8_t out[1 << 22];
    BBObject s;
    size_t n;
    unsigned i;

    /* 45 meshes of 1500 faces: 67500 in all. */
    bb_begin(&b, 45);
    bb_texture(&b, 0x0A000001, 8, 8, 0);
    memset(&s, 0, sizeof s);
    s.verts = 3;
    s.faces = 1500;
    s.ntex = 1;
    s.tex[0] = 0x0A000001;
    for (i = 0; i < 45; i++)
        bb_entry(&b, 0x0B000001 + i, bb_object(&b, &s), 0);
    n = bb_end(&b, out, sizeof out, 0, NULL, 0, NULL, 0);
    printf("67500 faces: %u bytes\n", (unsigned)n);
    expect("45 meshes of 1500 faces", out, n, NULL, 0);

    /* One list of 17000 commands and more: 1700 texture loads of 10 commands each. */
    bb_begin(&b, 1);
    bb_texture(&b, 0x0A000001, 8, 8, 0);
    s.faces = 1;
    s.reloads = 1700;
    bb_entry(&b, 0x0B000001, bb_object(&b, &s), 0);
    n = bb_end(&b, out, sizeof out, 0, NULL, 0, NULL, 0);
    printf("a long list: %u bytes\n", (unsigned)n);
    expect("a display list of more than 17000 commands", out, n, NULL, 0);
}

static void mutations(void) {
    static BB b;
    BBObject s;
    uint32_t first, second, o[1], chain[33];
    size_t n;
    unsigned i;
    static const char *const names[] = {"gball", "cball", "GShinL", "GShinR", "gbl_bt"};
    static const uint8_t five[] = {3, 4, 5, 6, 7}, two3[] = {3, 3}, font[] = {10}, generic[] = {2}, zero[] = {0},
                         one[] = {1}, none[] = {16};

    /* The header. */
    header_only("5 retail texture banks", 0, five, 5, NULL, 0, "more than 4 retail texture banks", 9);
    header_only("5 retail object banks", 0, NULL, 0, five, 5, "more than 4 retail object banks", 10);
    header_only("the font texture bank", 0, font, 1, NULL, 0, "the font and generic texture banks cannot be named", 12);
    header_only("the generic texture bank", 0, generic, 1, NULL, 0,
                "the font and generic texture banks cannot be named", 12);
    header_only("retail object bank 0", 0, NULL, 0, zero, 1, "object banks 0 and 1 cannot be named", 12);
    header_only("retail object bank 1", 0, NULL, 0, one, 1, "object banks 0 and 1 cannot be named", 12);
    header_only("an object bank the game does not have", 0, NULL, 0, none, 1,
                "a retail bank number that the game does not have", 12);
    header_only("a number named twice", 0, two3, 2, NULL, 0, "a retail bank number named twice", 13);
    header_only("a file with nothing", 0, NULL, 0, NULL, 0, "the file holds no models and names no bank", 0);
    header_only("the flag with no retail texture bank", 1, NULL, 0, two3, 1,
                "the flag for retail textures, with no retail texture bank", 8);
    header_only("a flag that does not exist", 2, two3, 1, NULL, 0, "a flag that the format does not have", 8);
    expect("a file of 11 bytes", fixture, 11, "the file is shorter than its header", 11);

    cube()[0x0B] = 1;
    expect("header byte 11", buf, bufN, "a header byte that must be 0 is not", 11);
    cube()[13] = 1;
    expect("a header padding byte", buf, bufN, "a padding byte of the header is not 0", 13);
    put32(cube(), 0x400000);
    expect("T + O over 4 MiB", buf, bufN, "the banks are larger than 4 MiB", 0);
    put32(cube(), 172);
    expect("T not a multiple of 8", buf, bufN, "the texture bank's size is not a multiple of 8", 0);
    put32(cube() + 4, 0);
    expect("a texture bank with no object bank", buf, bufN,
           "the file has one bank of the pack's own without the other", 4);
    cube();
    bufN += 8;
    memset(buf + fixtureN, 0, 8);
    expect("trailing bytes", buf, bufN, "the file is longer than its header says", 1248);
    cube();
    expect("a file cut short", buf, bufN - 8, "the file is shorter than its header says", 1240);

    /* The texture bank. */
    memcpy(cube() + TEX, "FLA2", 4);
    expect("the texture bank starts with FLA2", buf, bufN, "the texture bank starts with FLA2", TEX);
    put32(cube() + TEX, 2);
    expect("a count past the end", buf, bufN, "a texture record runs past the end of the bank", 188);
    put32(cube() + TEX, 0);
    expect("a count of 0", buf, bufN, "the texture bank's count is not 1 to 1024", TEX);
    put32(cube() + REC, 0);
    expect("a texture id of 0", buf, bufN, "a texture id is 0", REC);
    cube()[REC + 4] = 1;
    expect("a cycle range", buf, bufN, "a texture's cycle range is not 0", REC + 4);
    cube()[REC + 7] = 4;
    expect("flags value 4: a cycling texture", buf, bufN, "a texture's flags are not 0", REC + 6);
    cube()[REC + 9] = 1;
    expect("a cycle speed", buf, bufN, "a texture's cycle speed or count is not 0", REC + 8);
    cube()[REC + 13] = 12;
    expect("a width that is not a power of two", buf, bufN, "a texture's size is not one Glovebox writes", REC + 12);
    cube()[REC + 17] = 2;
    expect("a mask that does not match", buf, bufN, "a texture's masks do not match its size", REC + 16);
    cube()[REC + 27] = 1;
    expect("8-bit texels", buf, bufN, "a texture is not 16-bit RGBA", REC + 24);
    cube()[REC + 31] = 40;
    expect("a data field of 40", buf, bufN, "a texture's data field is not 36", REC + 28);
    cube()[REC + 35] = 0x40;
    expect("palette not 0", buf, bufN, "a texture has a palette", REC + 32);
    cube()[REC + 23] = 176;
    expect("a size field of 176", buf, bufN, "a texture's size field does not match its texels", REC + 20);
    cube()[REC + 164] = 1;
    expect("a texture's padding", buf, bufN, "a padding byte of a texture is not 0", REC + 164);
    cube()[189] = 1;
    expect("the texture bank's last bytes", buf, bufN, "the texture bank's last 4 bytes are not 0", 189);
    put32(cube() + FD + 4, 0x12345678);
    expect("a texture load of a texture not in the bank", buf, bufN,
           "a texture load names a texture that is not in the pack's texture bank", FD + 4);

    bb_begin(&b, 1);
    bb_texture(&b, 0x0A000001, 8, 8, 0);
    bb_texture(&b, 0x0A000002, 8, 8, 0);
    memset(&s, 0, sizeof s);
    s.verts = 3;
    s.faces = 1;
    s.ntex = 1;
    s.tex[0] = 0x0A000001;
    bb_entry(&b, 0x0B000001, bb_object(&b, &s), 1);
    n = bb_end(&b, buf, sizeof buf, 0, NULL, 0, NULL, 0);
    expect("a texture that no object uses", buf, n, "a texture that no object uses", 16 + 4 + 168);

    /* The directory and the instance. */
    memcpy(cube() + OBJ, "FLA2", 4);
    expect("the object bank starts with FLA2", buf, bufN, "the object bank starts with FLA2", OBJ);
    put32(cube() + OBJ, 0);
    expect("an object id of 0", buf, bufN, "a directory entry has the id 0", OBJ);
    put32(cube() + OBJ + 8, 1);
    expect("an entry with an id and no instance", buf, bufN, "a directory entry has an id and no instance", OBJ + 8);
    put32(cube() + OBJ + 4, 8);
    expect("an instance in the directory", buf, bufN, "a pointer names the directory", OBJ + 4);
    put32(cube() + INSTANCE, 0x94D4B65C);
    expect("an instance with another id", buf, bufN, "an instance does not start with its object's id", INSTANCE);
    cube()[INSTANCE + 0x10] = 1;
    expect("an instance's spare byte", buf, bufN, "an instance's spare bytes are not 0", INSTANCE + 4);
    put32(cube() + INSTANCE + 0x0C, 0);
    expect("an instance with no object", buf, bufN, "an instance has no object", INSTANCE + 0x0C);
    cube()[ANIM + 1] = 1;
    expect("an animation state that is not zeros", buf, bufN, "an animation state is not all zeros", ANIM);
    put32(cube() + INSTANCE + 0x18, 0);
    expect("an instance with no animation state", buf, bufN, NULL, 0);

    /* The object. */
    put32(cube() + OBJECT + 0x34, OBJECT - OBJ);
    expect("a cycle: an object that is its own child", buf, bufN, "two structures share bytes", OBJECT + 0x34);
    put32(cube() + OBJECT + 0x14, (MESH - OBJ) | 0x01000000);
    expect("a pointer with high bits", buf, bufN, "a pointer has high bits set", OBJECT + 0x14);
    put32(cube() + OBJECT + 0x18, DL - OBJ + 4);
    expect("a display list not on a multiple of 8", buf, bufN, "a pointer is not aligned", OBJECT + 0x18);
    put32(cube() + OBJECT + 0x18, 0x2000);
    expect("a display list outside the bank", buf, bufN, "a pointer names a range outside the bank", OBJECT + 0x18);
    for (i = 0; i < 5; i++) {
        char what[64];
        memset(cube() + OBJECT + 4, 0, 8);
        memcpy(buf + OBJECT + 4, names[i], strlen(names[i]));
        snprintf(what, sizeof what, "the reserved name %s", names[i]);
        expect(what, buf, bufN, "an object has the name of the ball or the hand", OBJECT + 4);
    }
    cube()[OBJECT + 10] = 'x';
    expect("a name with a byte after its end", buf, bufN, NULL, 0);
    cube()[OBJECT + 0x0C] = 1;
    expect("an object marked initialized", buf, bufN, "an object's initialized byte is not 0", OBJECT + 0x0C);
    cube()[OBJECT + 0x11] = 0;
    expect("a position key count of 0", buf, bufN, "an object's key count is not 1", OBJECT + 0x10);
    cube()[OBJECT + 0x13] = 2;
    expect("two rotation keys", buf, bufN, "an object's key count is not 1", OBJECT + 0x12);
    cube()[OBJECT + 0x2B] = 1;
    expect("a sprite", buf, bufN, "an object has sprites", OBJECT + 0x28);
    cube()[OBJECT + 0x31] = 1;
    expect("a byte at 0x31", buf, bufN, "an object's bytes at 0x30 are not 0", OBJECT + 0x30);
    cube()[OBJECT + 0x33] = 8;
    expect("object flags 8", buf, bufN, "an object's flags have a bit that Glovebox does not write", OBJECT + 0x32);
    put32(cube() + OBJECT + 0x3C, 0x10);
    expect("a collision sphere", buf, bufN, "an object has a collision sphere", OBJECT + 0x3C);
    put32(cube() + OBJECT + 0x18, 0);
    expect("an object with no display list", buf, bufN, "an object lacks a mesh or a display list", OBJECT + 0x18);
    put32(cube() + OBJECT + 0x24, 0);
    expect("an object with no rotation key", buf, bufN, "an object lacks a key", OBJECT + 0x24);

    /* The keys. */
    put32(cube() + ROTKEY + 12, 0);
    expect("a rotation key of zeros", buf, bufN, "a rotation key is not near unit length", ROTKEY);
    put32(cube() + ROTKEY, 0x7F800000);
    expect("an infinite rotation", buf, bufN, "a key holds a float that is not finite", ROTKEY);
    putf(cube() + POSKEY, 20000.0f);
    expect("a position of 20000", buf, bufN, "a position is past 16384", POSKEY);
    putf(cube() + SCALEKEY + 4, -100.0f);
    expect("a scale of -100", buf, bufN, "a scale is past 64", SCALEKEY + 4);
    put32(cube() + POSKEY + 8, 0x7FC00000);
    expect("a position that is not a number", buf, bufN, "a key holds a float that is not finite", POSKEY + 8);
    putf(cube() + SCALEKEY + 12, 1.0f);
    expect("a scale key's fourth value", buf, bufN, "a key's fourth value is not 0", SCALEKEY + 12);
    put32(cube() + POSKEY + 16, 1);
    expect("a key time of 1", buf, bufN, "a key's time is not 0", POSKEY + 16);

    /* The mesh. */
    cube()[MESH + 1] = 0;
    expect("a mesh of no faces", buf, bufN, "a mesh's face count is not 1 to 1500", MESH);
    cube()[MESH + 3] = 2;
    expect("a mesh of 2 vertices", buf, bufN, "a mesh's vertex count is not 3 to 4500", MESH + 2);
    put32(cube() + MESH + 0x10, 0x100);
    expect("uv not 0", buf, bufN, "a mesh has texture coordinates", MESH + 0x10);
    put32(cube() + MESH + 0x20, 0x100);
    expect("face textures", buf, bufN, "a mesh has colors, face flags or face textures", MESH + 0x20);
    put32(cube() + VERTS + 4, 0x7FC00000);
    expect("a vertex that is not a number", buf, bufN, "a vertex is not finite or is past 100000", VERTS + 4);
    putf(cube() + VERTS, 200000.0f);
    expect("a vertex at 200000", buf, bufN, "a vertex is not finite or is past 100000", VERTS);
    cube()[FACES + 1] = 20;
    expect("a face index equal to numVerts", buf, bufN, "a face names a vertex past the mesh's vertices", FACES);
    cube()[FACES + 0] = 0xFF;
    expect("a negative face index", buf, bufN, "a face names a vertex past the mesh's vertices", FACES);
    put32(cube() + MESH + 0x0C, FACES - OBJ + 8);
    expect("overlapping ranges: face colors over the faces", buf, bufN, "two structures share bytes", MESH + 0x0C);
    cube()[1020] = 1;
    expect("a nonzero byte that nothing reaches", buf, bufN, "a byte that no structure reaches is not 0", 1020);

    /* The display list. */
    cube()[DL] = 0x06;
    expect("a 0x06 command", buf, bufN, "a display list command that Glovebox does not write", DL);
    cube()[ENDDL] = 0xE7;
    expect("a missing B8", buf, bufN, "a display list command that Glovebox does not write", VTXBLOCK);
    cube()[ENDDL + 7] = 1;
    expect("a B8 with a nonzero word", buf, bufN, "a G_ENDDL command is not in Glovebox's form", ENDDL);
    cube()[FD + 8] = 0xE8;
    expect("FD followed by E8", buf, bufN, "a texture load is not followed by Glovebox's commands", FD + 8);
    cube()[FD + 8] = 0xB8;
    expect("FD followed by B8", buf, bufN, "a texture load is not followed by Glovebox's commands", FD + 8);
    cube()[FD + 3] = 1;
    expect("an FD in another form", buf, bufN, "a texture load is not in Glovebox's form", FD);
    cube()[FD + 8 * 4 + 7] ^= 1;
    expect("a texture load with a changed word", buf, bufN, "a texture load is not followed by Glovebox's commands",
           FD + 8 * 4);
    cube()[GVTX + 1] = 2;
    expect("a G_VTX at slot 1", buf, bufN, "a G_VTX command is not in Glovebox's form", GVTX);
    put32(cube() + GVTX + 4, VERTS - OBJ);
    expect("a G_VTX block over the vertices", buf, bufN, "two structures share bytes", GVTX + 4);
    put32(cube() + GVTX + 4, 0x420);
    expect("a G_VTX block past the end", buf, bufN, "a pointer names a range outside the bank", GVTX + 4);

    /* Two textures with one id, and two objects with one id. */
    bb_begin(&b, 2);
    bb_texture(&b, 0x0A000001, 8, 8, 0);
    bb_texture(&b, 0x0A000001, 8, 8, 0);
    memset(&s, 0, sizeof s);
    s.verts = 3;
    s.faces = 1;
    s.ntex = 1;
    s.tex[0] = 0x0A000001;
    first = bb_object(&b, &s);
    second = bb_object(&b, &s);
    bb_entry(&b, 0x0B000001, first, 0);
    bb_entry(&b, 0x0B000001, second, 0);
    n = bb_end(&b, buf, sizeof buf, 0, NULL, 0, NULL, 0);
    expect("two textures with one id", buf, n, "two textures have one id", 16 + 4 + 168);
    bb_begin(&b, 2);
    bb_texture(&b, 0x0A000001, 8, 8, 0);
    first = bb_object(&b, &s);
    second = bb_object(&b, &s);
    bb_entry(&b, 0x0B000001, first, 0);
    bb_entry(&b, 0x0B000001, second, 0);
    n = bb_end(&b, buf, sizeof buf, 0, NULL, 0, NULL, 0);
    expect("two objects with one id", buf, n, "two objects have one id", 16 + 176 + 8);

    /* A chain of 33 siblings is deeper than 32, with no children at all. */
    bb_begin(&b, 1);
    bb_texture(&b, 0x0A000001, 8, 8, 0);
    for (i = 0; i < 33; i++) {
        s.sibling = i ? chain[i - 1] : 0;
        chain[i] = bb_object(&b, &s);
    }
    bb_entry(&b, 0x0B000001, chain[32], 0);
    n = bb_end(&b, buf, sizeof buf, 0, NULL, 0, NULL, 0);
    expect("a chain of 33 siblings", buf, n, "the object tree is deeper than 32", 16 + 176 + chain[1] + 0x38);
    s.sibling = 0;

    /* A display list that reaches the end of the bank with no G_ENDDL: the cube with 8 more bytes of the object bank,
       which hold one E7, and its list pointer moved there. */
    cube();
    put32(buf + 4, 1064);
    memset(buf + 1248, 0, 8);
    buf[1248] = 0xE7;
    bufN = 1256;
    put32(buf + OBJECT + 0x18, 1056);
    expect("a display list with no end", buf, bufN, "a display list runs past the end of the bank", OBJECT + 0x18);

    cube()[REC + 13] = 64;
    buf[REC + 15] = 64;
    expect("a texture of 64 by 64", buf, bufN, "a texture's size is not one Glovebox writes", REC + 12);

    /* Two objects that share a mesh. */
    bb_begin(&b, 2);
    bb_texture(&b, 0x0A000001, 8, 8, 0);
    memset(&s, 0, sizeof s);
    s.verts = 3;
    s.faces = 1;
    s.ntex = 1;
    s.tex[0] = 0x0A000001;
    first = bb_object(&b, &s);
    second = bb_object(&b, &s);
    bb_entry(&b, 0x0B000001, first, 0);
    bb_entry(&b, 0x0B000002, second, 0);
    n = bb_end(&b, buf, sizeof buf, 0, NULL, 0, NULL, 0);
    memcpy(buf + 16 + 176 + second + 0x14, buf + 16 + 176 + first + 0x14, 4);
    expect("a shared mesh", buf, n, "two structures share bytes", 16 + 176 + second + 0x14);
    (void)o;
}

/* ---- The shadow walk on banks that the check refuses. It must stop on each. */

static void shadow_stops(const char *what) {
    size_t at = 0;
    int bank = 0;
    const char *why = g64_bank_shadow(buf + TEX, 176, buf + OBJ, 1056, &at, &bank);
    if (why)
        printf("the shadow walk on %s: %s in bank %d at %u\n", what, why, bank, (unsigned)at);
    else
        printf("the shadow walk on %s: no fault\n", what);
    if (why == NULL) {
        printf("FAIL: the shadow walk did not stop on %s\n", what);
        failures++;
    }
}

static void shadow(void) {
    size_t at = 0;
    int bank = 0;
    const char *why = g64_bank_shadow(fixture + TEX, 176, fixture + OBJ, 1056, &at, &bank);
    printf("the shadow walk on the cube: %s\n", why ? why : "no fault");
    failures += why != NULL;

    put32(cube() + OBJECT + 0x18, 0x2000);
    shadow_stops("a display list outside the bank");
    cube()[ENDDL] = 0xE7;
    shadow_stops("a missing B8");
    put32(cube() + OBJECT + 0x34, OBJECT - OBJ);
    shadow_stops("an object that is its own child");
    memcpy(cube() + OBJECT + 4, "gball", 5);
    shadow_stops("the name gball");
    put32(cube() + OBJECT + 0x3C, 0x10);
    shadow_stops("a collision sphere");
    put32(cube() + TEX + 4 + 20, 0x1000);
    put32(buf + TEX, 2);
    shadow_stops("a texture size past the end");
    cube()[MESH] = 0x7F;
    put32(buf + MESH + 0x20, 0x100);
    shadow_stops("face textures with a large face count");
    put32(cube() + OBJECT + 0x18, 0);
    shadow_stops("an object with no display list");
}

/* ---- The rules that need the ROM. The made-up ROM holds the banks of a file in place, as texture bank 0 and object
   bank 0. */

static uint32_t romTex, romTexEnd, romObjEnd;

static int rom_range(void *user, int kind, unsigned num, uint32_t *start, uint32_t *end) {
    (void)user;
    if (num != 0)
        return 0;
    *start = kind == G64_BANK_TEXTURE ? romTex : romTexEnd;
    *end = kind == G64_BANK_TEXTURE ? romTexEnd : romObjEnd;
    return 1;
}

static G64RomBanks *rom_of(const uint8_t *f, size_t n) {
    G64RomBanks *r;
    G64RomBanksStats st;
    romTex = 16;
    romTexEnd = 16 + (uint32_t)(f[3] | f[2] << 8);
    romObjEnd = (uint32_t)n;
    r = g64_rombanks_build(f, n, rom_range, NULL);
    g64_rombanks_stats(r, &st);
    printf("the made-up ROM: %u banks, %u skipped, %u object ids, %u texture ids, %u %u %u hashes\n",
           st.banks[0] + st.banks[1], st.skipped[0] + st.skipped[1], st.objectIds, st.textureIds, st.texels, st.verts,
           st.vtx);
    return r;
}

static void expect_rom(const char *what, const G64RomBanks *r, const char *want, size_t wantAt) {
    size_t at = 0;
    const char *why = g64_models_check(buf, bufN, game_banks, NULL, NULL, &at);
    if (why == NULL)
        why = g64_models_check_rom(buf, bufN, r, &at);
    printf("%s: %s", what, why ? why : "accepted");
    if (why)
        printf(" at %u", (unsigned)at);
    printf("\n");
    if (want == NULL ? why != NULL : (why == NULL || strcmp(why, want) != 0 || at != wantAt)) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* The cube with new ids: its object, and its texture when tex is set. */
static void new_ids(int tex) {
    cube();
    put32(buf + OBJ, 0x13579BDF);
    put32(buf + INSTANCE, 0x13579BDF);
    if (tex) {
        put32(buf + REC, 0x2468ACE0);
        put32(buf + FD + 4, 0x2468ACE0);
    }
}

static void rom_rules(void) {
    static uint8_t rom[4096], small[8192];
    static BB b;
    BBObject s;
    G64RomBanks *r = rom_of(fixture, fixtureN);
    size_t n;
    unsigned i;

    cube();
    expect_rom("the cube against a ROM that holds it", r, "an object id that the ROM has", OBJ);
    new_ids(0);
    expect_rom("a new object id", r, "a texture id that the ROM has", REC);
    new_ids(1);
    expect_rom("new ids", r, "a texture equal to one in the ROM", TEXELS);
    new_ids(1);
    buf[TEXELS] ^= 1;
    expect_rom("new ids and a changed texel", r, "a mesh's vertices equal to a mesh's in the ROM", VERTS);
    new_ids(1);
    buf[TEXELS] ^= 1;
    buf[VERTS + 1] ^= 1;
    expect_rom("new ids, a changed texel and a moved vertex", r, "a G_VTX block equal to one in the ROM", VTXBLOCK);
    new_ids(1);
    buf[TEXELS] ^= 1;
    buf[VERTS + 1] ^= 1;
    buf[VTXBLOCK + 9] ^= 1;
    expect_rom("new ids and changed data", r, NULL, 0);
    g64_rombanks_free(r);

    /* A texture of one color may equal the ROM's. */
    memcpy(rom, fixture, fixtureN);
    for (i = 0; i < 64; i++) {
        rom[TEXELS + 2 * i] = 0xEE;
        rom[TEXELS + 2 * i + 1] = 0x09;
    }
    r = rom_of(rom, fixtureN);
    new_ids(1);
    memcpy(buf + TEXELS, rom + TEXELS, 128);
    expect_rom("new ids and a texture of one color", r, "a mesh's vertices equal to a mesh's in the ROM", VERTS);
    g64_rombanks_free(r);

    /* A vertex array and a G_VTX block under 64 bytes may equal the ROM's. */
    bb_begin(&b, 1);
    bb_texture(&b, 0x0A000001, 8, 8, 0);
    memset(&s, 0, sizeof s);
    s.verts = 5;
    s.faces = 2;
    s.ntex = 1;
    s.tex[0] = 0x0A000001;
    bb_entry(&b, 0x0B000001, bb_object(&b, &s), 0);
    n = bb_end(&b, small, sizeof small, 0, NULL, 0, NULL, 0);
    r = rom_of(small, n);
    memcpy(buf, small, n);
    bufN = n;
    put32(buf + REC, 0x2468ACE0);
    buf[TEXELS] ^= 1;
    for (i = OBJ + 16; i + 8 <= n; i += 8)
        if (buf[i] == 0xFD && buf[i + 1] == 0x10 && buf[i + 2] == 0 && buf[i + 3] == 0)
            put32(buf + i + 4, 0x2468ACE0);
    put32(buf + 16 + 176, 0x13579BDF);
    for (i = OBJ; i + 4 <= n; i += 4)
        if (buf[i] == 0x0B && buf[i + 3] == 0x01 && buf[i + 1] == 0 && buf[i + 2] == 0)
            put32(buf + i, 0x13579BDF);
    expect_rom("a 60-byte vertex array and a 48-byte block equal to the ROM's", r, NULL, 0);
    g64_rombanks_free(r);
}

int main(int argc, char **argv) {
    FILE *f;
    if (argc < 2 || (f = fopen(argv[1], "rb")) == NULL) {
        printf("usage: bankcheck_test tests/fixtures/cube.models.bin\n");
        return 2;
    }
    fixtureN = fread(fixture, 1, sizeof fixture, f);
    fclose(f);
    if (fixtureN != 1248) {
        printf("FAIL: the fixture is %u bytes, not 1248\n", (unsigned)fixtureN);
        return 1;
    }
    valid_files();
    mutations();
    reach();
    large();
    shadow();
    rom_rules();
    printf("%s: %d failures\n", failures ? "FAILED" : "ok", failures);
    return failures != 0;
}
