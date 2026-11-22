/* Tests of the level pack reader (src/client/pack.h): the manifest's parser, the level file's outside check, the
 * id, and a pack loaded from a folder that the test writes. The level file here is made up: a size and a name. No
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
    printf("%s: %d failures\n", failures ? "FAILED" : "ok", failures);
    return failures ? 1 : 0;
}
