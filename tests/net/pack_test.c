/* Tests of the level pack reader (src/client/pack.h): the manifest's parser, the level file's outside check, the
 * id, a pack loaded from a folder that the test writes, a pack from bytes, the cache, and the comparison with a
 * ROM's levels. The level file here is made up: a size and a name. No
 * ROM needed. It prints the id of a fixed pack; Linux and Windows must print the same. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/pack.h"

static int failures;

static void check(int ok, const char *what) {
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static int manifest(const char *text, g64_pack *p) {
    return g64_pack_parse_manifest((const uint8_t *)text, strlen(text), p);
}

static void write_file(const char *dir, const char *name, const void *d, size_t n) {
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "wb");
    if (f) {
        fwrite(d, 1, n, f);
        fclose(f);
    }
}

static void remove_file(const char *dir, const char *name) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    remove(path);
}

int main(int argc, char **argv) {
    static const uint8_t lev[12] = {0, 0, 0, 12, 't', 'e', 's', 't', '.', 'l', 'n', 0};
    static const char good[] = "format=1\nname=A test level\nlevel=10\n";
    const char *dir = argc > 1 ? argv[1] : ".";
    g64_pack p;
    uint8_t id[G64_PACK_ID], other[G64_PACK_ID];
    char err[200], big[G64_PACK_MANIFEST_MAX + 64];

    memset(&p, 0, sizeof p);
    check(manifest(good, &p) == 0 && !strcmp(p.name, "A test level") && p.level == 10, "a good manifest");
    check(manifest("level=10\nname=x\nformat=1\n", &p) == 0 && !strcmp(p.name, "x") && p.level == 10,
          "the keys in another order");
    check(manifest("format=1\r\nname=A\r\nlevel=1\r\n", &p) != 0, "Windows line ends are refused");
    check(manifest("format=1\nname=A\nlevel=47\n", &p) == 0 && p.level == 47, "the last level");
    check(manifest("format=2\nname=A\nlevel=1\n", &p) != 0, "another format is refused");
    check(manifest("format=1\nname=A\nlevel=48\n", &p) != 0, "a level past the last is refused");
    check(manifest("format=1\nname=A\nlevel=-1\n", &p) != 0, "a negative level is refused");
    check(manifest("format=1\nname=A\nlevel=07\n", &p) != 0, "a level with a leading zero is refused");
    check(manifest("format=1\nname=A\nlevel=1", &p) != 0, "a last line without its end is refused");
    check(manifest("format=1\nname=A\n", &p) != 0, "a missing key is refused");
    check(manifest("format=1\nname=A\nname=B\nlevel=1\n", &p) != 0, "a key twice is refused");
    check(manifest("format=1\nname=A\nlevel=1\nscript=x.lua\n", &p) != 0, "an unknown key is refused");
    check(manifest("format=1\nname=\nlevel=1\n", &p) != 0, "an empty name is refused");
    check(manifest("format=1\nname=a\tb\nlevel=1\n", &p) != 0, "a name with a control character is refused");
    check(manifest("format=1\nname=123456789012345678901234567890123\nlevel=1\n", &p) != 0,
          "a name of 33 characters is refused");
    check(manifest("format=1\n\nname=A\nlevel=1\n", &p) != 0, "an empty line is refused");
    memset(big, 'a', sizeof big);
    check(g64_pack_parse_manifest((const uint8_t *)big, sizeof big, &p) != 0, "a manifest that is too long is refused");
    check(g64_pack_parse_manifest(NULL, 0, &p) != 0, "no manifest is refused");

    check(g64_pack_check_level(lev, sizeof lev) == 0, "a level file whose first word is its size");
    check(g64_pack_check_level(lev, sizeof lev - 1) != 0, "a level file that is shorter than it says is refused");
    check(g64_pack_check_level(lev, 4) != 0, "a level file of four bytes is refused");

    g64_pack_make_id((const uint8_t *)good, strlen(good), lev, sizeof lev, NULL, 0, id);
    g64_pack_make_id((const uint8_t *)good, strlen(good), lev, sizeof lev, NULL, 0, other);
    check(memcmp(id, other, sizeof id) == 0, "the id is the same each time");
    g64_pack_make_id((const uint8_t *)good, strlen(good) - 1, lev, sizeof lev, NULL, 0, other);
    check(memcmp(id, other, sizeof id) != 0, "another manifest gives another id");
    g64_pack_make_id((const uint8_t *)good, strlen(good), lev, sizeof lev - 1, NULL, 0, other);
    check(memcmp(id, other, sizeof id) != 0, "another level file gives another id");
    printf("pack: id %02x%02x%02x%02x%02x%02x%02x%02x\n", id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7]);
    {
        /* A script is part of the pack's id. */
        static const char script[] = "function on_frame() end\n";
        uint8_t withScript[G64_PACK_ID];
        g64_pack_make_id((const uint8_t *)good, strlen(good), lev, sizeof lev, (const uint8_t *)script, sizeof script - 1,
                         withScript);
        check(memcmp(id, withScript, sizeof id) != 0, "a pack with a script has another id");
        printf("pack: id with a script %02x%02x%02x%02x%02x%02x%02x%02x\n", withScript[0], withScript[1], withScript[2],
               withScript[3], withScript[4], withScript[5], withScript[6], withScript[7]);
        check(g64_pack_check_script((const uint8_t *)script, sizeof script - 1) == 0, "a script of source text");
        check(g64_pack_check_script((const uint8_t *)script, sizeof script) != 0, "a script with a zero byte is refused");
        check(g64_pack_check_script((const uint8_t *)"\x1bLua", 4) != 0, "Lua's compiled form is refused");
        check(g64_pack_check_script((const uint8_t *)"", 0) != 0, "an empty script is refused");
        write_file(dir, "pack.txt", good, strlen(good));
        write_file(dir, "level.lev", lev, sizeof lev);
        write_file(dir, "script.lua", script, sizeof script - 1);
        check(g64_pack_load(dir, &p, err, sizeof err) == 0 && p.script && p.scriptSize == sizeof script - 1 &&
                  memcmp(p.id, withScript, sizeof withScript) == 0,
              "a pack with a script loads, with the script's id");
        g64_pack_free(&p);
        write_file(dir, "script.lua", "\x1bLua", 4);
        check(g64_pack_load(dir, &p, err, sizeof err) != 0, "a pack whose script is compiled Lua is refused");
        remove_file(dir, "script.lua");
    }

    /* A pack from a folder. */
    write_file(dir, "pack.txt", good, strlen(good));
    write_file(dir, "level.lev", lev, sizeof lev);
    check(g64_pack_load(dir, &p, err, sizeof err) == 0 && p.level == 10 && p.levSize == sizeof lev &&
              memcmp(p.id, id, sizeof id) == 0 && memcmp(p.lev, lev, sizeof lev) == 0,
          "a pack loads from its folder, with the same id");
    g64_pack_free(&p);
    write_file(dir, "level.lev", lev, sizeof lev - 2);
    check(g64_pack_load(dir, &p, err, sizeof err) != 0 && p.lev == NULL, "a pack with a cut level file is refused");
    write_file(dir, "level.lev", lev, sizeof lev);
    write_file(dir, "pack.txt", "format=9\n", 9);
    check(g64_pack_load(dir, &p, err, sizeof err) != 0, "a pack with a bad manifest is refused");
    check(g64_pack_load("no-such-folder-here", &p, err, sizeof err) != 0, "a folder that is not there is refused");
    write_file(dir, "pack.txt", good, strlen(good));

    {
        /* A pack from bytes, as one that came from a host. The strict form also holds the level file to the check
           of its structure: the made-up file above has no end token. */
        static const uint8_t whole[11] = {0, 0, 0, 11, 't', '.', 'l', 'n', 0, 0x7D, 0x00};
        static const char script[] = "function on_frame() end\n";
        g64_pack q;
        uint8_t wholeId[G64_PACK_ID];
        const uint8_t *m = (const uint8_t *)good;
        size_t mn = strlen(good);
        check(g64_pack_from_bytes(m, mn, lev, sizeof lev, NULL, 0, 0, &p, err, sizeof err) == 0 &&
                  memcmp(p.id, id, sizeof id) == 0 && p.manifestSize == mn && memcmp(p.manifest, m, mn) == 0,
              "a pack from bytes has the folder's id and keeps its manifest");
        check(g64_pack_check_strict(&p, NULL) != NULL, "the made-up level file does not pass the check");
        g64_pack_free(&p);
        check(g64_pack_from_bytes(m, mn, lev, sizeof lev, NULL, 0, 1, &p, err, sizeof err) != 0 && p.lev == NULL,
              "the strict form refuses a level file without an end");
        check(g64_pack_from_bytes(m, mn, whole, sizeof whole, NULL, 0, 1, &p, err, sizeof err) == 0,
              "the strict form takes a whole level file");
        memcpy(wholeId, p.id, sizeof wholeId);
        check(g64_pack_from_bytes(m, mn - 1, whole, sizeof whole, NULL, 0, 1, &q, err, sizeof err) != 0,
              "a cut manifest is refused");
        check(g64_pack_from_bytes(m, mn, whole, sizeof whole - 1, NULL, 0, 1, &q, err, sizeof err) != 0,
              "a cut level file is refused");
        check(g64_pack_from_bytes(m, mn, whole, sizeof whole, (const uint8_t *)"\x1bLua", 4, 1, &q, err, sizeof err) != 0,
              "a compiled script is refused");
        check(g64_pack_from_bytes(NULL, 0, NULL, 0, NULL, 0, 1, &q, err, sizeof err) != 0, "no bytes are refused");

        /* The cache: a folder named by the id alone. */
        {
            char path[600], want[600];
            snprintf(want, sizeof want, "%s/packs/", dir);
            check(g64_pack_cache_dir(dir, wholeId, path, sizeof path) == 0 && strlen(path) == strlen(want) + 32 &&
                      memcmp(path, want, strlen(want)) == 0 && strspn(path + strlen(want), "0123456789abcdef") == 32,
                  "the cache folder's name is the id in hexadecimal digits");
            check(g64_pack_cache_dir(dir, wholeId, path, 20) != 0, "a path that does not fit is refused");
            check(g64_pack_cache_dir("", wholeId, path, sizeof path) != 0, "no base folder is refused");
        }
        check(g64_pack_cache_load(dir, wholeId, &q, err, sizeof err) != 0, "a pack that was never kept is not found");
        check(g64_pack_cache_save(dir, &p, err, sizeof err) == 0, "a pack is kept");
        check(g64_pack_cache_load(dir, wholeId, &q, err, sizeof err) == 0 && memcmp(q.id, wholeId, sizeof wholeId) == 0 &&
                  q.levSize == sizeof whole && !q.script,
              "a kept pack is read back");
        g64_pack_free(&q);
        check(g64_pack_cache_save(dir, &p, err, sizeof err) == 0, "a pack is kept a second time");
        g64_pack_free(&p);
        {
            /* Files that changed on the disk: the id no longer fits, or the level file no longer passes. */
            char path[600];
            g64_pack_cache_dir(dir, wholeId, path, sizeof path);
            write_file(path, "script.lua", script, sizeof script - 1);
            check(g64_pack_cache_load(dir, wholeId, &q, err, sizeof err) != 0, "a kept pack with a new file is refused");
            remove_file(path, "script.lua");
            write_file(path, "level.lev", lev, sizeof lev);
            check(g64_pack_cache_load(dir, wholeId, &q, err, sizeof err) != 0, "a kept pack that changed is refused");
            check(g64_pack_cache_load(dir, id, &q, err, sizeof err) != 0, "a pack under another id's name is not found");
        }
        /* A kept pack with a script, and then the same folder used for a pack without one. */
        check(g64_pack_from_bytes(m, mn, whole, sizeof whole, (const uint8_t *)script, sizeof script - 1, 1, &p, err,
                                  sizeof err) == 0 && memcmp(p.id, wholeId, sizeof wholeId) != 0,
              "a pack with a script from bytes");
        check(g64_pack_cache_save(dir, &p, err, sizeof err) == 0 &&
                  g64_pack_cache_load(dir, p.id, &q, err, sizeof err) == 0 && q.scriptSize == sizeof script - 1,
              "a kept pack with a script is read back");
        g64_pack_free(&q);
        g64_pack_free(&p);
    }
    {
        /* A pack whose level file is a level of the ROM holds game data. The ROM here is made up: its first word,
           the two pointers of the loader's code, and two level files. */
        static uint8_t rom[0x54100];
        static const uint8_t start[8] = {0x3C, 0x02, 0xB0, 0x05, 0x34, 0x42, 0x40, 0x00};
        static const uint8_t end[8] = {0x3C, 0x03, 0xB0, 0x05, 0x34, 0x63, 0x40, 0x18};
        static const uint8_t second[12] = {0, 0, 0, 12, 't', 'e', 's', 't', '.', 'l', 'n', 0};
        static const uint8_t first[12] = {0, 0, 0, 12, 'o', 'n', 'e', '.', 'l', 'n', 0, 0};
        static uint8_t own[12] = {0, 0, 0, 12, 't', 'e', 's', 't', '.', 'l', 'n', 1};
        rom[0] = 0x80, rom[1] = 0x37, rom[2] = 0x12, rom[3] = 0x40;
        memcpy(rom + 341164, start, 8);
        memcpy(rom + 341348, end, 8);
        memcpy(rom + 0x54000, first, 12);
        memcpy(rom + 0x5400C, second, 12);
        check(g64_pack_from_bytes((const uint8_t *)good, strlen(good), second, 12, NULL, 0, 0, &p, err, sizeof err) == 0 &&
                  g64_pack_rom_level(&p, rom, sizeof rom) == 1,
              "a pack with a level of the ROM is found");
        g64_pack_free(&p);
        check(g64_pack_from_bytes((const uint8_t *)good, strlen(good), own, 12, NULL, 0, 0, &p, err, sizeof err) == 0 &&
                  g64_pack_rom_level(&p, rom, sizeof rom) == -1,
              "a pack with a level of its own is not one of the ROM's");
        check(g64_pack_rom_level(&p, rom, 64) == -1, "a ROM that is too short has no levels");
        g64_pack_free(&p);
    }
    printf("%s: %d failures\n", failures ? "FAILED" : "ok", failures);
    return failures ? 1 : 0;
}
