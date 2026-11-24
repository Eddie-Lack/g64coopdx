/* A level pack with models.bin from a host to a joiner, through the real session over loopback UDP. The host checks
 * its pack as the online program does (g64_pack_may_send), sends its bytes (g64_pack_blob), and the joiner reads
 * the download with the sizes, id and name of the offer it got (g64_pack_from_download). The pack is a made-up level
 * file, a script of 20,000 bytes, and Glovebox's cube (tests/fixtures/cube.models.bin). The two ROMs are made up
 * too: one holds a bank with other ids (tests/net/bankbuild.h), the other holds the cube's own banks. The host also
 * runs the rule for the level file's object ids after g64_pack_may_send, as host_offer_pack does: a pack whose level
 * names an object in no bank is not offered, and the online host stops its session. No ROM needed. It prints ids and
 * messages only; Linux and Windows must print the same.
 *
 * Usage: packsend_test CUBE. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/bankranges.h"
#include "../../src/client/pack.h"
#include "../../src/client/rombanks.h"
#include "../../src/net/udp.h"
#include "../../src/session/session.h"

#define BB_BANK 4096
#include "bankbuild.h"

#define ROM 0x1234abcdu
#define BUILD 0x0b01d001u
#define SCRIPT 20000

static int failures;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        failures++;
}

/* A made-up ROM: the bytes of one models.bin, whose two banks are bank 0 of each kind. */
typedef struct {
    G64Models info;
} fake_rom;

static int fake_banks(void *user, int kind, unsigned num, uint32_t *start, uint32_t *end) {
    const fake_rom *r = (const fake_rom *)user;
    if (num != 0)
        return 0;
    *start = kind == G64_BANK_TEXTURE ? r->info.texAt : r->info.objAt;
    *end = *start + (kind == G64_BANK_TEXTURE ? r->info.texSize : r->info.objSize);
    return 1;
}

/* An index of a made-up ROM, from the bytes of a models.bin. */
static G64RomBanks *fake_index(const uint8_t *models, size_t n, fake_rom *r) {
    static const g64_pack_banks table = {g64_bank_exists, NULL, NULL};
    size_t at = 0;
    if (g64_pack_check_models(models, n, &table, &r->info, &at) != NULL)
        return NULL;
    return g64_rombanks_build(models, n, fake_banks, r);
}

/* Sends n bytes as a pack from a host to a joiner. The host offers id with the sizes in info. Returns a copy of the
 * joiner's download, and the offer the joiner got in got, or NULL when the download did not finish. */
static uint8_t *send_pack(const uint8_t *bytes, const g64_pack_offer *info, const uint8_t *id, g64_pack_offer *got,
                          size_t *have) {
    char err[128];
    net_socket *hs = net_open(0, err, sizeof err), *s1 = net_open(0, err, sizeof err);
    g64_session *host, *a;
    net_addr hostAddr;
    uint8_t *copy = NULL;
    uint32_t clock = 1000;
    int i;
    *have = 0;
    if (!hs || !s1)
        return NULL;
    host = g64_session_host(hs, 2, ROM, 0, 3);
    net_resolve("127.0.0.1", net_port(hs), &hostAddr, err, sizeof err);
    g64_session_set_build(host, BUILD);
    g64_session_set_pack(host, id);
    g64_session_send_pack(host, info, bytes);
    a = g64_session_join(s1, &hostAddr, ROM, "joiner");
    g64_session_set_build(a, BUILD);
    for (i = 0; i < 100 && g64_session_pack_state(a) != G64_PACK_OFFERED; i++) {
        clock += 10;
        g64_session_poll(a, clock, NULL, NULL);
        g64_session_poll(host, clock, NULL, NULL);
    }
    if (g64_session_pack_state(a) == G64_PACK_OFFERED && g64_session_pack_accept(a) == 0) {
        *got = *g64_session_pack_offer(a);
        for (i = 0; i < 20000 && g64_session_pack_state(a) == G64_PACK_LOADING; i++) {
            clock += 10;
            g64_session_poll(a, clock, NULL, NULL);
            g64_session_poll(host, clock, NULL, NULL);
        }
        if (g64_session_pack_state(a) == G64_PACK_LOADED) {
            *have = g64_session_pack_have(a);
            copy = (uint8_t *)malloc(*have);
            if (copy)
                memcpy(copy, g64_session_pack_bytes(a), *have);
        }
    }
    g64_session_free(a);
    g64_session_free(host);
    net_close(hs);
    net_close(s1);
    return copy;
}

/* The joiner's side of one download: the bytes and the offer, read with banks. Prints the message on a refusal. */
static int joiner_reads(const uint8_t *d, size_t n, const g64_pack_offer *o, const g64_pack_banks *banks,
                        g64_pack *out) {
    char err[240];
    int r = g64_pack_from_download(d, n, o->manifestSize, o->levSize, o->scriptSize, o->modelsSize, o->id, o->name,
                                   banks, out, err, sizeof err);
    if (r != 0)
        printf("joiner: %s\n", err);
    return r;
}

/* A level file with one PLATFORM for each id, after the name "t.ln". */
static size_t platform_level(uint8_t *lev, size_t max, const uint32_t *ids, int n) {
    size_t at = 0;
    int i, k;
    if (max < 11 + (size_t)n * 14)
        return 0;
    at = 4;
    memcpy(lev + at, "t.ln", 5);
    at += 5;
    for (i = 0; i < n; i++) {
        lev[at++] = 0;
        lev[at++] = 98; /* PLATFORM: the object's id is the first word of its 12 bytes of fields */
        for (k = 0; k < 4; k++)
            lev[at++] = (uint8_t)(ids[i] >> (24 - 8 * k));
        memset(lev + at, 0, 8);
        at += 8;
    }
    lev[at++] = 0x7D;
    lev[at++] = 0x00;
    for (k = 0; k < 4; k++)
        lev[k] = (uint8_t)(at >> (24 - 8 * k));
    return at;
}

/* The host's gate as host_offer_pack runs it: the rule for the level file's object ids, with the level's banks
 * levelBanks of the made-up ROM index, on a level file that passes the strict check; then g64_pack_may_send. There is
 * no ROM here, so no level is the ROM's. Returns 0 when the pack is offered, 1 when it is not offered, and -1 when the
 * id rule refuses it (the online host then stops its session). Prints the message. */
static int host_gate(const g64_pack *p, const g64_pack_banks *banks, const g64_level_banks *levelBanks,
                     G64RomBanks *index) {
    char err[240];
    size_t at = 0;
    const char *why;
    if (g64_pack_check_strict(p, NULL) == NULL) {
        why = g64_pack_check_level_ids(p, levelBanks, g64_rombanks_bank_has, index, &at);
        if (why) {
            printf("host: the level file did not pass the check of its objects: %s (at byte %u)\n", why,
                   (unsigned)at);
            return -1;
        }
    }
    if (g64_pack_may_send(p, NULL, 0, banks, err, sizeof err) != 0) {
        printf("host: %s\n", err);
        return 1;
    }
    return 0;
}

static void print_id(const char *what, const uint8_t *id) {
    int i;
    printf("id %s ", what);
    for (i = 0; i < G64_PACK_ID; i++)
        printf("%02x", id[i]);
    printf("\n");
}

int main(int argc, char **argv) {
    static const char good[] = "format=1\nname=A pack with models\nlevel=10\n";
    static const uint8_t whole[11] = {0, 0, 0, 11, 't', '.', 'l', 'n', 0, 0x7D, 0x00};
    static uint8_t cube[8192], other[8192], script[SCRIPT];
    static BB b;
    const char *cubePath = argc > 1 ? argv[1] : "tests/fixtures/cube.models.bin";
    const g64_pack_banks table = {g64_bank_exists, NULL, NULL};
    g64_pack_banks otherRom = table, cubeRom = table;
    fake_rom otherR, cubeR;
    G64RomBanks *otherIndex, *cubeIndex;
    g64_pack p, q;
    g64_pack_offer info, got;
    size_t cubeN = 0, otherN, bn = 0, have = 0, i;
    uint8_t *blob, *down;
    char err[240];
    FILE *f = fopen(cubePath, "rb");
    if (f) {
        cubeN = fread(cube, 1, sizeof cube, f);
        fclose(f);
    }
    check(cubeN > 0 && cubeN < sizeof cube, "the cube's models.bin is read");
    if (cubeN == 0 || cubeN >= sizeof cube)
        return 1;
    if (net_init() != 0)
        return 1;
    /* A script of plain text, over several bursts of 8 KB. */
    for (i = 0; i < SCRIPT; i++)
        script[i] = (uint8_t)(i % 64 == 63 ? '\n' : 'a' + i % 26);

    /* The ROMs: one with a made-up bank of other ids, one with the cube's own banks. */
    bb_begin(&b, 1);
    bb_texture(&b, 0x0A000077, 8, 8, 0);
    {
        BBObject s;
        memset(&s, 0, sizeof s);
        s.verts = 8;
        s.faces = 12;
        s.ntex = 1;
        s.tex[0] = 0x0A000077;
        bb_entry(&b, 0x0B000077, bb_object(&b, &s), 1);
    }
    otherN = bb_end(&b, other, sizeof other, 0, NULL, 0, NULL, 0);
    otherIndex = otherN ? fake_index(other, otherN, &otherR) : NULL;
    cubeIndex = fake_index(cube, cubeN, &cubeR);
    check(otherIndex && cubeIndex, "the two made-up ROMs have an index");
    otherRom.rom = otherIndex;
    cubeRom.rom = cubeIndex;

    /* The host's pack, and its checks before it is offered. */
    check(g64_pack_from_bytes((const uint8_t *)good, sizeof good - 1, whole, sizeof whole, script, SCRIPT, cube, cubeN,
                              &table, 1, &p, err, sizeof err) == 0,
          "the host's pack: a level, a script and the cube");
    print_id("of the pack", p.id);
    check(g64_pack_may_send(&p, NULL, 0, &otherRom, err, sizeof err) == 0, "a host whose ROM has other ids offers it");
    check(g64_pack_may_send(&p, NULL, 0, &cubeRom, err, sizeof err) != 0 &&
              strstr(err, "an object id that the ROM has"),
          "a host whose ROM has the cube's ids does not offer it");
    printf("host: %s\n", err);
    check(g64_pack_may_send(&p, NULL, 0, &table, err, sizeof err) != 0, "a host without an index does not offer it");
    printf("host: %s\n", err);
    blob = g64_pack_blob(&p, &bn);
    memset(&info, 0, sizeof info);
    snprintf(info.name, sizeof info.name, "%s", p.name);
    info.manifestSize = p.manifestSize;
    info.levSize = p.levSize;
    info.scriptSize = p.scriptSize;
    info.modelsSize = p.modelsSize;
    check(blob && bn > 2 * 8192, "the pack's bytes take more than two bursts");

    /* A good download. */
    down = blob ? send_pack(blob, &info, p.id, &got, &have) : NULL;
    check(down && have == bn && memcmp(down, blob, bn) == 0 && got.modelsSize == cubeN && !strcmp(got.name, p.name),
          "the joiner downloads every byte, with the offer's sizes and name");
    check(down && joiner_reads(down, have, &got, &otherRom, &q) == 0 && memcmp(q.id, p.id, G64_PACK_ID) == 0 &&
              q.modelsSize == cubeN && q.scriptSize == SCRIPT && q.models && memcmp(q.models, cube, cubeN) == 0,
          "the joiner reads the download as the host's pack, on a ROM with other ids");
    g64_pack_free(&q);
    check(down && joiner_reads(down, have, &got, &cubeRom, &q) != 0 && !q.lev && !q.models,
          "a joiner whose ROM has the cube's ids refuses the download");
    check(down && joiner_reads(down, have, &got, &table, &q) != 0 && !q.lev,
          "a joiner without an index refuses a download with models");
    {
        /* The offer names another pack: another name, or another id. */
        g64_pack_offer o = got;
        snprintf(o.name, sizeof o.name, "Another name");
        check(down && joiner_reads(down, have, &o, &otherRom, &q) != 0 && !q.lev,
              "a download whose name is not the offer's is refused");
        o = got;
        o.id[0] ^= 1;
        check(down && joiner_reads(down, have, &o, &otherRom, &q) != 0 && !q.lev,
              "a download whose id is not the offer's is refused");
    }
    free(down);

    if (blob) {
        /* A host that sends other bytes under the same offer. A changed letter in the script still reads as a pack,
           but its id is not the offer's. A changed flag in models.bin fails the bank check. */
        uint8_t *bad = (uint8_t *)malloc(bn);
        if (bad) {
            memcpy(bad, blob, bn);
            bad[p.manifestSize + p.levSize + 100] ^= 1;
            down = send_pack(bad, &info, p.id, &got, &have);
            check(down && have == bn && joiner_reads(down, have, &got, &otherRom, &q) != 0 && !q.lev,
                  "a download with a changed script is refused by its id");
            free(down);
            memcpy(bad, blob, bn);
            bad[p.manifestSize + p.levSize + p.scriptSize + 8] = 2;
            down = send_pack(bad, &info, p.id, &got, &have);
            check(down && have == bn && joiner_reads(down, have, &got, &otherRom, &q) != 0 && !q.lev,
                  "a download with a damaged models.bin is refused by the bank check");
            free(down);
            free(bad);
        }
    }
    free(blob);
    g64_pack_free(&p);

    /* A pack without models needs no index. */
    check(g64_pack_from_bytes((const uint8_t *)good, sizeof good - 1, whole, sizeof whole, script, SCRIPT, NULL, 0,
                              &table, 1, &p, err, sizeof err) == 0 &&
              g64_pack_may_send(&p, NULL, 0, &table, err, sizeof err) == 0,
          "a host offers a pack without models without an index");
    blob = g64_pack_blob(&p, &bn);
    info.modelsSize = 0;
    down = blob ? send_pack(blob, &info, p.id, &got, &have) : NULL;
    check(down && joiner_reads(down, have, &got, &table, &q) == 0 && memcmp(q.id, p.id, G64_PACK_ID) == 0 && !q.models,
          "a joiner reads a pack without models without an index");
    g64_pack_free(&q);
    free(down);
    free(blob);
    g64_pack_free(&p);

    /* The rule for the level file's object ids. The level loads object bank 0 of the ROM with other ids. A platform of
       that bank and one of the cube pass. A platform whose id is in no bank is refused, though the pack passes
       g64_pack_may_send. */
    {
        static uint8_t lev[64];
        const g64_level_banks levelBanks = {1, {0}};
        uint32_t cubeId, ids[2];
        size_t levN;
        char why[240];
        cubeId = ((uint32_t)cube[cubeR.info.objAt] << 24) | ((uint32_t)cube[cubeR.info.objAt + 1] << 16) |
                 ((uint32_t)cube[cubeR.info.objAt + 2] << 8) | cube[cubeR.info.objAt + 3];
        ids[0] = 0x0B000077;
        ids[1] = cubeId;
        levN = platform_level(lev, sizeof lev, ids, 2);
        check(levN && g64_pack_from_bytes((const uint8_t *)good, sizeof good - 1, lev, levN, NULL, 0, cube, cubeN,
                                          &table, 1, &p, why, sizeof why) == 0,
              "a pack whose level has a platform of the ROM's bank and one of the cube");
        check(host_gate(&p, &otherRom, &levelBanks, otherIndex) == 0, "the host offers it: every id is in a bank");
        g64_pack_free(&p);
        ids[1] = 0x0B000099;
        levN = platform_level(lev, sizeof lev, ids, 2);
        check(levN && g64_pack_from_bytes((const uint8_t *)good, sizeof good - 1, lev, levN, NULL, 0, cube, cubeN,
                                          &table, 1, &p, why, sizeof why) == 0,
              "a pack whose level has a platform with an id in no bank");
        check(g64_pack_may_send(&p, NULL, 0, &otherRom, why, sizeof why) == 0,
              "it passes g64_pack_may_send: the id rule is a check of its own");
        check(host_gate(&p, &otherRom, &levelBanks, otherIndex) == -1,
              "the host refuses to offer it, and stops its session");
        g64_pack_free(&p);
    }

    g64_rombanks_free(otherIndex);
    g64_rombanks_free(cubeIndex);
    printf("%s: %d failures\n", failures ? "FAILED" : "ok", failures);
    return failures ? 1 : 0;
}
