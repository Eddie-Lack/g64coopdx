/* g64pack: makes and checks level packs (docs/discovery/level-pack.md). A pack is a folder with pack.txt, level.lev
 * and, if wanted, script.lua. The level file comes from Glovebox, the level editor: either the .lev file that its
 * ".lev" button saves, or a ROM that it built, from which this tool takes one level's file.
 *
 *   g64pack make DIR --name NAME --level N (--lev FILE | --rom ROM) [--script FILE]
 *   g64pack check DIR
 *   g64pack levels
 *
 * Every file the tool reads is untrusted: sizes and offsets are checked before use. The pack it writes is read
 * back with the game's own pack reader (src/client/pack.c), so a pack this tool accepts is one the game accepts. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define make_dir(p) mkdir(p, 0777)
#endif

#include "../src/client/pack.h"

/* The game's levels by number (weemitt's enum Level). */
static const char *const levelName[G64_PACK_LEVELS] = {
    "Hub 1",          "Hub 2",          "Hub 3",         "Hub 4",           "Hub 5",          "Hub 6",
    "Hub 7",          "Hub 8",          "Crystal cave",  "Training course", "Atlantis 1",     "Atlantis 2",
    "Atlantis 3",     "Atlantis boss",  "Atlantis bonus", "Carnival 1",     "Carnival 2",     "Carnival 3",
    "Carnival boss",  "Carnival bonus", "Pirates 1",     "Pirates 2",       "Pirates 3",      "Pirates boss",
    "Pirates bonus",  "Prehistoric 1",  "Prehistoric 2", "Prehistoric 3",   "Prehistoric boss", "Prehistoric bonus",
    "Fear 1",         "Fear 2",         "Fear 3",        "Fear boss",       "Fear bonus",     "Space 1",
    "Space 2",        "Space 3",        "Space boss 1",  "Space boss, between", "Space boss 3", "Space bonus",
    "Wayroom",        "Presents",       "Flythrough 1",  "Flythrough 2",    "Intro",          "Outro"};

/* The largest ROM the tool reads. The game's is 8 MB, and one that Glovebox built can be larger. */
#define ROM_MAX (64u << 20)

static int fail(const char *what, const char *detail) {
    fprintf(stderr, "g64pack: %s%s%s\n", what, detail ? ": " : "", detail ? detail : "");
    return 1;
}

/* Reads a whole file of 1 to max bytes. The caller frees it. NULL when it cannot be read or has another size. */
static unsigned char *read_file(const char *path, size_t max, size_t *size) {
    FILE *f = fopen(path, "rb");
    unsigned char *d;
    size_t n;
    if (f == NULL) {
        return NULL;
    }
    d = malloc(max + 1);
    if (d == NULL) {
        fclose(f);
        return NULL;
    }
    n = fread(d, 1, max + 1, f);
    fclose(f);
    if (n == 0 || n > max) {
        free(d);
        return NULL;
    }
    *size = n;
    return d;
}

static int write_file(const char *dir, const char *name, const unsigned char *d, size_t n) {
    char path[1024];
    FILE *f;
    int ok;
    if (snprintf(path, sizeof path, "%s/%s", dir, name) >= (int)sizeof path) {
        return -1;
    }
    f = fopen(path, "wb");
    if (f == NULL) {
        return -1;
    }
    ok = fwrite(d, 1, n, f) == n;
    return fclose(f) == 0 && ok ? 0 : -1;
}

static int file_exists(const char *dir, const char *name) {
    char path[1024];
    FILE *f;
    if (snprintf(path, sizeof path, "%s/%s", dir, name) >= (int)sizeof path) {
        return 1;
    }
    f = fopen(path, "rb");
    if (f != NULL) {
        fclose(f);
    }
    return f != NULL;
}

static void print_pack(const char *dir, const g64_pack *p) {
    int i;
    printf("pack: %s\n", dir);
    printf("name: %s\n", p->name);
    printf("level: %d (%s)\n", p->level, levelName[p->level]);
    printf("level file: %lu bytes\n", (unsigned long)p->levSize);
    if (p->script != NULL) {
        printf("script: %lu bytes\n", (unsigned long)p->scriptSize);
    } else {
        printf("script: none\n");
    }
    printf("id: ");
    for (i = 0; i < G64_PACK_ID; i++) {
        printf("%02x", p->id[i]);
    }
    printf("\n");
}

static int check(const char *dir) {
    g64_pack p;
    char err[200];
    if (g64_pack_load(dir, &p, err, sizeof err) != 0) {
        return fail("the pack does not load", err);
    }
    print_pack(dir, &p);
    g64_pack_free(&p);
    return 0;
}

static int make(int argc, char **argv) {
    const char *dir = NULL, *name = NULL, *levPath = NULL, *romPath = NULL, *scriptPath = NULL, *why = NULL;
    unsigned char *lev = NULL, *script = NULL, *rom = NULL;
    size_t levSize = 0, scriptSize = 0, romSize = 0, at = 0;
    char manifest[G64_PACK_MANIFEST_MAX];
    g64_pack parsed;
    int level = -1, i, n, r = 1;
    for (i = 0; i < argc; i++) {
        const char *a = argv[i], **to = NULL;
        if (strcmp(a, "--name") == 0) {
            to = &name;
        } else if (strcmp(a, "--lev") == 0) {
            to = &levPath;
        } else if (strcmp(a, "--rom") == 0) {
            to = &romPath;
        } else if (strcmp(a, "--script") == 0) {
            to = &scriptPath;
        } else if (strcmp(a, "--level") == 0) {
            char *end = NULL;
            long v;
            if (i + 1 >= argc) {
                return fail("--level needs a number", NULL);
            }
            v = strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end != 0 || v < 0 || v >= G64_PACK_LEVELS) {
                return fail("--level takes a number from 0 to 47; 'g64pack levels' lists them", argv[i]);
            }
            level = (int)v;
            continue;
        } else if (a[0] == '-' || dir != NULL) {
            return fail("unknown argument", a);
        } else {
            dir = a;
            continue;
        }
        if (i + 1 >= argc) {
            return fail("this option needs a value", a);
        }
        *to = argv[++i];
    }
    if (dir == NULL || name == NULL || level < 0 || (levPath == NULL) == (romPath == NULL)) {
        return fail("usage: g64pack make DIR --name NAME --level N (--lev FILE | --rom ROM) [--script FILE]", NULL);
    }
    /* The manifest goes through the game's own reader, which holds the rules for a name. */
    n = snprintf(manifest, sizeof manifest, "format=1\nname=%s\nlevel=%d\n", name, level);
    if (n < 0 || n >= (int)sizeof manifest ||
        g64_pack_parse_manifest((const uint8_t *)manifest, (size_t)n, &parsed) != 0) {
        return fail("a pack's name has 1 to 32 characters of plain ASCII", name);
    }
    if (levPath != NULL) {
        lev = read_file(levPath, G64_PACK_LEVEL_MAX, &levSize);
        if (lev == NULL) {
            return fail("cannot read the level file, or it is empty or larger than 1 MB", levPath);
        }
    } else {
        rom = read_file(romPath, ROM_MAX, &romSize);
        if (rom == NULL) {
            return fail("cannot read the ROM", romPath);
        }
        why = g64_pack_level_from_rom(rom, romSize, level, &at, &levSize);
        if (why == NULL) {
            lev = malloc(levSize);
            if (lev != NULL) {
                memcpy(lev, rom + at, levSize);
            } else {
                why = "no memory";
            }
        }
        free(rom);
        if (why != NULL) {
            return fail(why, romPath);
        }
    }
    if (g64_pack_check_level(lev, levSize) != 0) {
        fail("this is not a level file: it does not start with its own size", levPath ? levPath : romPath);
        goto done;
    }
    if (scriptPath != NULL) {
        script = read_file(scriptPath, G64_PACK_SCRIPT_MAX, &scriptSize);
        if (script == NULL || g64_pack_check_script(script, scriptSize) != 0) {
            fail("the script must be Lua source text of at most 256 KB, with no zero byte", scriptPath);
            goto done;
        }
    }
    if (make_dir(dir) != 0 && errno != EEXIST) {
        fail("cannot make the folder", dir);
        goto done;
    }
    /* A script.lua that is already in the folder would become part of the pack without being asked for. */
    if (scriptPath == NULL && file_exists(dir, "script.lua")) {
        fail("the folder already holds a script.lua: remove it, or name a script with --script", dir);
        goto done;
    }
    if (write_file(dir, "pack.txt", (const unsigned char *)manifest, (size_t)n) != 0 ||
        write_file(dir, "level.lev", lev, levSize) != 0 ||
        (script != NULL && write_file(dir, "script.lua", script, scriptSize) != 0)) {
        fail("cannot write the pack's files", dir);
        goto done;
    }
    r = check(dir);
done:
    free(lev);
    free(script);
    return r;
}

int main(int argc, char **argv) {
    int i;
    if (argc >= 3 && strcmp(argv[1], "make") == 0) {
        return make(argc - 2, argv + 2);
    }
    if (argc == 3 && strcmp(argv[1], "check") == 0) {
        return check(argv[2]);
    }
    if (argc == 2 && strcmp(argv[1], "levels") == 0) {
        for (i = 0; i < G64_PACK_LEVELS; i++) {
            printf("%2d  %s\n", i, levelName[i]);
        }
        return 0;
    }
    fprintf(stderr, "g64pack makes and checks level packs for g64coopdx.\n\n"
                    "  g64pack make DIR --name NAME --level N (--lev FILE | --rom ROM) [--script FILE]\n"
                    "      writes a pack into the folder DIR. The level comes from a .lev file that Glovebox saved,\n"
                    "      or from level N of a ROM that Glovebox built.\n"
                    "  g64pack check DIR\n"
                    "      reads a pack as the game does, and prints its name, level and id.\n"
                    "  g64pack levels\n"
                    "      lists the game's levels and their numbers.\n");
    return 2;
}
