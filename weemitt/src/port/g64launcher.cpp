/* g64coopdx: the start screen of the online program. It opens when the program starts without options (from a double
   click): host an online game, join one by its room code, or play alone. It looks like the game's title menu: the
   game's own title screen runs behind it (platform.c) and draws its pages (weemitt's g64title.c). It connects
   through g64coopdx's netplay.c, which keeps the window drawing while it waits. */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../game/g64title.h"
#include "g64launcher.h"
#include "prefpath.h"
#include "../../../src/client/netplay.h"
#include "../../../src/session/session.h"

/* A room code's character in capitals, or 0 when a room code cannot hold it. */
static char code_char(char c) {
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ? c : 0;
}

/* The player's colors: a palette, or custom. Each color is chosen (G64_COLOR_SET plus 0xRRGGBB) or automatic (0, the
   color of the player's number). The colors multiply the glove's and the ball's own: white keeps them. They are kept in
   colors.txt, in the program's own folder: the palette's number and the two custom colors. */
typedef struct {
    const char *name;
    unsigned glove, ball;
} palette;
static const palette palettes[] = {
    {"Automatic", 0, 0},
    {"Classic", G64_COLOR_SET | 0xFFFFFF, G64_COLOR_SET | 0xFFFFFF},
    {"Cross-Stitch", G64_COLOR_SET | 0x3A3A44, G64_COLOR_SET | 0x8C1A1A},
    {"Blue", G64_COLOR_SET | 0x60A0FF, G64_COLOR_SET | 0x60A0FF},
    {"Green", G64_COLOR_SET | 0x70FF70, G64_COLOR_SET | 0x70FF70},
    {"Yellow", G64_COLOR_SET | 0xFFE040, G64_COLOR_SET | 0xFFE040},
    {"Red", G64_COLOR_SET | 0xFF6060, G64_COLOR_SET | 0xFF6060},
    {"Purple", G64_COLOR_SET | 0xC070FF, G64_COLOR_SET | 0xC070FF},
    {"Orange", G64_COLOR_SET | 0xFFA040, G64_COLOR_SET | 0xFFA040},
    {"Cyan", G64_COLOR_SET | 0x60FFFF, G64_COLOR_SET | 0x60FFFF},
};
#define PALETTES ((int)(sizeof palettes / sizeof palettes[0]))
#define CUSTOM PALETTES
static int choice;
static unsigned custom[2] = {G64_COLOR_SET | 0xFFFFFF, G64_COLOR_SET | 0xFFFFFF};
static int colorsLoaded;

static void colors_path(char *out, size_t size) {
    char *pref = SDL_GetPrefPath(PORT_PREF_ORG, PORT_PREF_APP);
    snprintf(out, size, "%scolors.txt", pref ? pref : "");
    if (pref)
        SDL_free(pref);
}

static void apply_colors(void) {
    if (choice == CUSTOM)
        netplay_set_colors(custom[0], custom[1]);
    else
        netplay_set_colors(palettes[choice].glove, palettes[choice].ball);
}

static void load_colors(void) {
    char path[1024];
    FILE *f;
    int c;
    unsigned g, b;
    colorsLoaded = 1;
    colors_path(path, sizeof path);
    f = fopen(path, "r");
    if (f) {
        if (fscanf(f, "%d %x %x", &c, &g, &b) == 3 && c >= 0 && c <= CUSTOM) {
            choice = c;
            custom[0] = G64_COLOR_SET | (g & 0xFFFFFF);
            custom[1] = G64_COLOR_SET | (b & 0xFFFFFF);
        }
        fclose(f);
    }
    apply_colors();
}

static void save_colors(void) {
    char path[1024];
    FILE *f;
    colors_path(path, sizeof path);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, "%d %06x %06x\n", choice, custom[0] & 0xFFFFFF, custom[1] & 0xFFFFFF);
        fclose(f);
    }
}

/* The player's name, kept in name.txt in the program's own folder. */
static char nameText[17];
static int nameLoaded;

static void name_path(char *out, size_t size) {
    char *pref = SDL_GetPrefPath(PORT_PREF_ORG, PORT_PREF_APP);
    snprintf(out, size, "%sname.txt", pref ? pref : "");
    if (pref)
        SDL_free(pref);
}

static void load_name(void) {
    char path[1024];
    FILE *f;
    nameLoaded = 1;
    /* A name given on the command line stays. */
    if (!netplay_name_chosen()) {
        name_path(path, sizeof path);
        f = fopen(path, "r");
        if (f) {
            if (fgets(nameText, sizeof nameText, f))
                nameText[strcspn(nameText, "\r\n")] = 0;
            fclose(f);
        }
        if (nameText[0])
            netplay_set_name(nameText);
    }
    snprintf(nameText, sizeof nameText, "%s", netplay_name());
}

void g64_load_name(void) {
    if (!nameLoaded)
        load_name();
}

static void save_name(void) {
    char path[1024];
    FILE *f;
    netplay_set_name(nameText);
    name_path(path, sizeof path);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, "%s\n", netplay_name());
        fclose(f);
    }
}

/* The colors a custom choice steps through, for the glove and for the ball. */
typedef struct {
    const char *name;
    unsigned rgb;
} named_color;
static const named_color shades[] = {
    {"White", 0xFFFFFF},  {"Gray", 0x909090},     {"Slate", 0x3A3A44}, {"Black", 0x000000},  {"Red", 0xFF4040},
    {"Dark red", 0x8C1A1A}, {"Orange", 0xFFA040}, {"Yellow", 0xFFE040}, {"Lime", 0xA0FF40},  {"Green", 0x40C040},
    {"Teal", 0x40C0A0},   {"Cyan", 0x60FFFF},     {"Sky", 0x60A0FF},   {"Blue", 0x4060FF},   {"Purple", 0xC070FF},
    {"Pink", 0xFF80C0},   {"Brown", 0x8C5A2A},
};
#define SHADES ((int)(sizeof shades / sizeof shades[0]))

/* The shade of a custom color, or -1 when the saved color is none of them. */
static int shade_of(unsigned c) {
    int i;
    for (i = 0; i < SHADES; i++)
        if (shades[i].rgb == (c & 0xFFFFFF))
            return i;
    return -1;
}

static void step_shade(unsigned *c, int d) {
    int i = shade_of(*c);
    i = i < 0 ? 0 : (i + d + SHADES) % SHADES;
    *c = G64_COLOR_SET | shades[i].rgb;
}

/* The page that the start screen shows. */
enum { PAGE_MAIN, PAGE_HOST, PAGE_JOIN, PAGE_OPTIONS, PAGE_WAIT };
/* What a row does. */
enum {
    R_HOST_PAGE, R_JOIN_PAGE, R_OPTIONS_PAGE, R_ALONE,
    R_PLAYERS, R_BALL_CONTROLS, R_ANY_BALL, R_ANY_WORLD, R_LISTED, R_TITLE, R_HOST,
    R_CODE, R_JOIN, R_ROOM,
    R_NAME, R_COLORS, R_GLOVE, R_BALL, R_DELAY,
    R_COPY, R_START_NOW, R_CANCEL, R_PACK_YES, R_PACK_NO,
};

typedef struct {
    int what;
    int room; /* R_ROOM: the open game's number in the list */
    int flags;
} row_info;

static row_info rows[G64_TITLE_ROWS];
static int rowCount;

static void add_row(int what, const char *label, const char *value, int flags, int room = 0) {
    if (rowCount >= G64_TITLE_ROWS)
        return;
    rows[rowCount].what = what;
    rows[rowCount].room = room;
    rows[rowCount].flags = flags;
    rowCount++;
    G64TitleRow(label, value, flags);
}

/* Small text below the rows, broken into lines of at most 40 characters: the game draws no more of a text. */
static void note(const char *text, unsigned rgb) {
    char line[64];
    const char *s = text;
    while (*s) {
        int n = (int)strlen(s), cut = n;
        if (n > 40) {
            cut = 40;
            while (cut > 20 && s[cut] != ' ')
                cut--;
            if (s[cut] != ' ')
                cut = 40;
        }
        snprintf(line, sizeof line, "%.*s", cut, s);
        G64TitleNote(line, rgb);
        s += cut;
        while (*s == ' ')
            s++;
    }
}

#define GRAY 0xA0A0A0u
#define RED 0xFF7878u
#define YELLOW 0xFFD278u
#define TIP 0xAAAAFFu

/* The open games that the join page shows at once, below the code and Join. */
#define ROOMS_SHOWN (G64_TITLE_ROWS - 2)

/* The start screen's state. */
typedef struct {
    int page, back, selected;
    int connecting, hosting, copied;
    int wanted;
    int roomTop; /* the first open game that the join page shows */
    char code[NETPLAY_ROOM_CODE + 1];
    char title[33];
    char error[320];
    const char *relay;
} launcher;

/* A line that explains the chosen row, below the rows. */
static const char *row_help(int what) {
    switch (what) {
        case R_HOST_PAGE: return "Start a game that other players join with its room code.";
        case R_JOIN_PAGE: return "Join a game by its room code, or from the list of open games.";
        case R_OPTIONS_PAGE: return "Your name, your colors and your input delay.";
        case R_ALONE: return "Play the game on your own, without going online.";
        case R_PLAYERS: return "How many players the game waits for before it starts.";
        case R_BALL_CONTROLS: return "Easy: the ball follows the stick. Normal: the game's own ball controls.";
        case R_ANY_BALL: return "Own: each glove takes only its own ball. Shared: any glove takes any free ball.";
        case R_ANY_WORLD: return "Together: everyone stays in one world. Any: players go to different worlds.";
        case R_LISTED: return "Yes puts the game in the list of open games for everyone with this version.";
        case R_TITLE: return "The game's name in the list. Type it.";
        case R_CODE: return "Type the room code that the host sent you. Ctrl+V pastes it.";
        case R_NAME: return "The name the other players see. Type it.";
        case R_COLORS: return "The colors of your glove and your ball. Custom lets you choose each.";
        case R_DELAY: return "Frames of 50 ms. More: the other gloves jump less. Less: yours answers sooner.";
        default: return NULL;
    }
}

/* Builds the page's rows and text for the game to draw. */
static void build_page(launcher *L) {
    const char *room = netplay_room_code();
    int i;

    rowCount = 0;
    if (L->connecting)
        L->page = PAGE_WAIT;
    switch (L->page) {
        case PAGE_MAIN:
            G64TitlePage("Online");
            add_row(R_HOST_PAGE, "Host a game", "", 0);
            add_row(R_JOIN_PAGE, "Join a game", "", 0);
            add_row(R_OPTIONS_PAGE, "Your options", "", 0);
            add_row(R_ALONE, "Play alone", "", 0);
            break;
        case PAGE_HOST: {
            char n[8];
            snprintf(n, sizeof n, "%d", L->wanted);
            G64TitlePage("Host a game");
            add_row(R_PLAYERS, "Players", n, G64_TITLE_ARROWS);
            add_row(R_BALL_CONTROLS, "Ball", netplay_host_normal_ball() ? "normal" : "easy", G64_TITLE_ARROWS);
            add_row(R_ANY_BALL, "Balls", netplay_host_any_ball() ? "shared" : "own", G64_TITLE_ARROWS);
            add_row(R_ANY_WORLD, "Worlds", netplay_host_any_world() ? "any" : "together", G64_TITLE_ARROWS);
            add_row(R_LISTED, "In the list", netplay_public() ? "yes" : "no", G64_TITLE_ARROWS);
            add_row(R_TITLE, "Title", L->title, netplay_public() ? G64_TITLE_TEXT : G64_TITLE_OFF);
            add_row(R_HOST, "Start hosting", "", 0);
            break;
        }
        case PAGE_JOIN: {
            /* The open games below the code: as many as fit, from roomTop on. Moving past the last one shows the
               next. */
            int n = netplay_rooms_count();
            if (L->roomTop > n - ROOMS_SHOWN)
                L->roomTop = n - ROOMS_SHOWN;
            if (L->roomTop < 0)
                L->roomTop = 0;
            G64TitlePage("Join a game");
            add_row(R_CODE, "Code", L->code, G64_TITLE_TEXT);
            add_row(R_JOIN, "Join", "", strlen(L->code) == NETPLAY_ROOM_CODE ? 0 : G64_TITLE_OFF);
            for (i = L->roomTop; i < n && rowCount < G64_TITLE_ROWS; i++) {
                const net_room *r = netplay_room(i);
                char who[16];
                snprintf(who, sizeof who, "%d/%d", r->joined, r->wanted);
                add_row(R_ROOM, who, r->title, 0, i);
            }
            break;
        }
        case PAGE_OPTIONS: {
            char d[8];
            int own = choice == CUSTOM;
            int g = shade_of(custom[0]), b = shade_of(custom[1]);
            snprintf(d, sizeof d, "%d", netplay_input_delay());
            G64TitlePage("Your options");
            add_row(R_NAME, "Name", nameText, G64_TITLE_TEXT);
            add_row(R_COLORS, "Colors", own ? "custom" : palettes[choice].name, G64_TITLE_ARROWS);
            add_row(R_GLOVE, "Glove", own ? (g >= 0 ? shades[g].name : "saved") : "-",
                    own ? G64_TITLE_ARROWS : G64_TITLE_OFF);
            add_row(R_BALL, "Ball", own ? (b >= 0 ? shades[b].name : "saved") : "-",
                    own ? G64_TITLE_ARROWS : G64_TITLE_OFF);
            add_row(R_DELAY, "Input delay", d, G64_TITLE_ARROWS);
            break;
        }
        case PAGE_WAIT:
            G64TitlePage(L->hosting ? "Hosting" : "Joining");
            if (netplay_pack_asking()) {
                add_row(R_PACK_YES, netplay_pack_kept() ? "Play with the pack" : "Download and play", "", 0);
                add_row(R_PACK_NO, "No, leave", "", 0);
            } else {
                if (L->hosting && room[0])
                    add_row(R_COPY, L->copied ? "Copied" : "Copy code", "", 0);
                if (L->hosting && (room[0] || !L->relay[0]))
                    add_row(R_START_NOW, "Start now", "", 0);
                add_row(R_CANCEL, "Cancel", "", 0);
            }
            break;
    }

    /* The chosen row is one that is not grayed out. */
    if (L->selected >= rowCount)
        L->selected = rowCount - 1;
    if (L->selected < 0)
        L->selected = 0;
    if (rowCount > 0 && (rows[L->selected].flags & G64_TITLE_OFF)) {
        for (i = 0; i < rowCount; i++)
            if (!(rows[i].flags & G64_TITLE_OFF)) {
                L->selected = i;
                break;
            }
    }
    G64TitleSelect(L->selected);

    /* The text below the rows. */
    if (L->page == PAGE_WAIT) {
        if (L->hosting && room[0])
            G64TitleBig(room);
        note(netplay_wait_status(), 0xFFFFFF);
        if (netplay_pack_asking()) {
            char line[200], models[48] = "";
            /* A pack's models are most of its size: up to 4 MB, about 16 seconds of download. */
            if (netplay_offer_models() > 0) {
                snprintf(models, sizeof models, ", models: %u KB", (unsigned)((netplay_offer_models() + 1023) / 1024));
            }
            snprintf(line, sizeof line, "Level pack: %s (%u KB%s%s)", netplay_offer_name(),
                     (unsigned)((netplay_offer_size() + 1023) / 1024), netplay_offer_script() ? ", with a script" : "",
                     models);
            note(line, 0xFFFFFF);
            note(netplay_pack_kept() ? "You downloaded this pack before. Play with it?"
                                     : "You do not have it. Download it from the host?",
                 0xFFFFFF);
            note("A pack changes the game on your computer. The program checks it before use, but the check does "
                 "not catch everything. Take a pack only from a host you trust.",
                 YELLOW);
        } else if (L->hosting && room[0]) {
            note("Send the code to the other players. The game starts when everyone is in, or when you press "
                 "Start now. Others can still join after the start.",
                 TIP);
        }
    } else {
        const char *help = rowCount > 0 ? row_help(rows[L->selected].what) : NULL;
        if (L->page == PAGE_JOIN) {
            int n = netplay_rooms_count();
            if (n < 0)
                note("Open games: asking the lobby...", GRAY);
            else if (n == 0)
                note("Open games: none right now", GRAY);
            else if (n > ROOMS_SHOWN) {
                char line[64];
                snprintf(line, sizeof line, "Open games %d to %d of %d", L->roomTop + 1, L->roomTop + ROOMS_SHOWN, n);
                note(line, GRAY);
            }
        }
        if (help)
            note(help, TIP);
        if (L->page == PAGE_MAIN) {
            char build[64];
            snprintf(build, sizeof build, "Version %s", netplay_build());
            note(build, GRAY);
        }
    }
    if (L->error[0])
        note(L->error, RED);
    G64TitleHints(L->page == PAGE_MAIN ? "\xa3 select" : "\xa3 select   \xa4 back");
    G64TitleColors(choice == CUSTOM ? custom[0] : palettes[choice].glove,
                   choice == CUSTOM ? custom[1] : palettes[choice].ball);
}

/* Starts hosting, or joining the room code. Returns 1 when it started. */
static int start(launcher *L, const unsigned char *rom, unsigned long romSize, const char *joinCode, int back) {
    char err[160];
    L->error[0] = 0;
    if (netplay_start(NULL, 0, L->relay, joinCode, joinCode ? 0 : L->wanted, 0, rom, romSize, err, sizeof err) != 0) {
        snprintf(L->error, sizeof L->error, "%s", err);
        return 0;
    }
    L->connecting = 1;
    L->hosting = joinCode == NULL;
    L->copied = 0;
    L->back = back;
    L->selected = 0;
    return 1;
}

int g64_launcher(SDL_Window *window, const unsigned char *rom, unsigned long romSize, const char *relay, int *level,
                 int *players) {
    static launcher L;
    int printed = 0;
    /* A test: G64_LAUNCHER_AUTO=host or join:CODE presses Host or Join at once, list joins the first open game and
       start presses Start now. G64_LAUNCHER_PACK=yes or no answers the host's level pack. */
    const char *autoPress = getenv("G64_LAUNCHER_AUTO");
    const char *accept = getenv("G64_LAUNCHER_PACK");
    /* A test: G64_LAUNCHER_KEYS lists keys that the start screen gets one after another, four a second: up,
       down, left, right, a, b, back (backspace) or a word to type (t:WORD). */
    const char *keys = getenv("G64_LAUNCHER_KEYS");
    int keyFrame = 0;
    Uint32 padAt = 0;
    int padHeld = 0;

    if (!colorsLoaded)
        load_colors();
    if (!nameLoaded)
        load_name();
    if (L.wanted == 0) {
        L.wanted = 2;
        snprintf(L.title, sizeof L.title, "%s", netplay_public_title());
    }
    L.relay = relay;
    L.page = PAGE_MAIN;
    L.connecting = 0;
    L.error[0] = 0;
    if (autoPress && !strncmp(autoPress, "join:", 5))
        snprintf(L.code, sizeof L.code, "%s", autoPress + 5);
    G64TitleOn(1);
    SDL_StartTextInput();

    for (;;) {
        SDL_Event e;
        /* The player's input in this frame: a move up or down, left or right, a press of A or B, typed text. */
        int move = 0, change = 0, pressA = 0, pressB = 0, erase = 0, what;
        char typed[64] = "";
        const char *room;

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                netplay_cancel();
                G64TitleOn(0);
                SDL_StopTextInput();
                return -1;
            }
            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_UP)
                    move = -1;
                else if (k == SDLK_DOWN || k == SDLK_TAB)
                    move = 1;
                else if (k == SDLK_LEFT)
                    change = -1;
                else if (k == SDLK_RIGHT)
                    change = 1;
                else if (k == SDLK_RETURN || k == SDLK_KP_ENTER)
                    pressA = 1;
                else if (k == SDLK_ESCAPE)
                    pressB = 1;
                else if (k == SDLK_BACKSPACE)
                    erase = 1;
                else if (k == SDLK_v && (e.key.keysym.mod & KMOD_CTRL)) {
                    char *clip = SDL_GetClipboardText();
                    if (clip) {
                        snprintf(typed, sizeof typed, "%s", clip);
                        SDL_free(clip);
                    }
                }
            } else if (e.type == SDL_TEXTINPUT) {
                size_t n = strlen(typed);
                snprintf(typed + n, sizeof typed - n, "%s", e.text.text);
            }
        }
        if (keys && *keys && ++keyFrame % 15 == 0) {
            const char *end = strchr(keys, ',');
            size_t n = end ? (size_t)(end - keys) : strlen(keys);
            if (n == 2 && !strncmp(keys, "up", 2))
                move = -1;
            else if (n == 4 && !strncmp(keys, "down", 4))
                move = 1;
            else if (n == 4 && !strncmp(keys, "left", 4))
                change = -1;
            else if (n == 5 && !strncmp(keys, "right", 5))
                change = 1;
            else if (n == 1 && keys[0] == 'a')
                pressA = 1;
            else if (n == 1 && keys[0] == 'b')
                pressB = 1;
            else if (n == 4 && !strncmp(keys, "back", 4))
                erase = 1;
            else if (n > 2 && !strncmp(keys, "t:", 2))
                snprintf(typed, sizeof typed, "%.*s", (int)(n - 2), keys + 2);
            keys = end ? end + 1 : keys + n;
        }
        /* The controller, as the game reads it. A move repeats every quarter second while it is held; A and B act
           once for each press. */
        {
            int held = g64_title_pad(), dir = held & (G64_PAD_UP | G64_PAD_DOWN | G64_PAD_LEFT | G64_PAD_RIGHT);
            int now = held & ~padHeld;
            if (dir != 0 && ((now & dir) || SDL_GetTicks() - padAt > 250)) {
                if (dir & G64_PAD_UP)
                    move = -1;
                else if (dir & G64_PAD_DOWN)
                    move = 1;
                else
                    change = (dir & G64_PAD_LEFT) ? -1 : 1;
                padAt = SDL_GetTicks();
            }
            if (now & G64_PAD_A)
                pressA = 1;
            if (now & G64_PAD_B)
                pressB = 1;
            padHeld = held;
        }

        if (L.connecting) {
            int r = netplay_poll();
            if (r > 0) {
                *level = netplay_level();
                *players = netplay_players();
                G64TitleOn(0);
                SDL_StopTextInput();
                return 1;
            }
            if (r < 0) {
                snprintf(L.error, sizeof L.error, "%s", netplay_error());
                netplay_cancel();
                L.connecting = 0;
                L.page = L.back;
                L.selected = 0;
            }
        }
        room = netplay_room_code();
        if (L.connecting && L.hosting && room[0] && !printed) {
            printf("g64coopdx: Room code: %s\n", room);
            fflush(stdout);
            printed = 1;
        }
        /* The open games that hosts have listed, for this build. The titles are other players' text. */
        if (!L.connecting && (L.page == PAGE_JOIN || (autoPress && !strcmp(autoPress, "list"))))
            netplay_rooms_refresh(relay);

        build_page(&L);

        /* The tests' presses. */
        if (autoPress && !L.connecting) {
            if (!strcmp(autoPress, "host")) {
                if (start(&L, rom, romSize, NULL, PAGE_HOST)) {
                    printed = 0;
                }
                autoPress = NULL;
            } else if (!strncmp(autoPress, "join:", 5)) {
                start(&L, rom, romSize, L.code, PAGE_JOIN);
                autoPress = NULL;
            } else if (!strcmp(autoPress, "list") && netplay_rooms_count() > 0) {
                const net_room *r = netplay_room(0);
                char c[NETPLAY_ROOM_CODE + 1];
                snprintf(c, sizeof c, "%s", r->code);
                start(&L, rom, romSize, c, PAGE_JOIN);
                autoPress = NULL;
            }
        } else if (autoPress && L.connecting && L.hosting && !strcmp(autoPress, "start")) {
            netplay_start_now();
            autoPress = NULL;
        }
        if (accept && netplay_pack_asking()) {
            netplay_pack_answer(!strcmp(accept, "yes"));
            accept = NULL;
        }

        /* On the list of open games, moving past its first or last shown game shows the one before or after. */
        if (move != 0 && L.page == PAGE_JOIN && rowCount > 2 && rows[L.selected].what == R_ROOM) {
            int n = netplay_rooms_count();
            if (move > 0 && L.selected == rowCount - 1 && L.roomTop + ROOMS_SHOWN < n) {
                L.roomTop++;
                move = 0;
            } else if (move < 0 && L.selected == 2 && L.roomTop > 0) {
                L.roomTop--;
                move = 0;
            }
        }
        /* Up and down skip rows that are grayed out. */
        if (move != 0 && rowCount > 0) {
            int i, s = L.selected;
            for (i = 0; i < rowCount; i++) {
                s = (s + move + rowCount) % rowCount;
                if (!(rows[s].flags & G64_TITLE_OFF))
                    break;
            }
            L.selected = s;
        }
        what = rowCount > 0 ? rows[L.selected].what : -1;

        /* Typing goes into the chosen row when it holds text: the name, the title or the room code. Backspace
           takes a character away there; elsewhere it goes back, as B does. */
        if (what == R_NAME || what == R_TITLE || what == R_CODE) {
            char *buf = what == R_NAME ? nameText : what == R_TITLE ? L.title : L.code;
            size_t size = what == R_NAME ? sizeof nameText : what == R_TITLE ? sizeof L.title : sizeof L.code;
            size_t n = strlen(buf);
            int changed = 0;
            const char *t;
            if (erase && n > 0) {
                buf[--n] = 0;
                changed = 1;
            }
            for (t = typed; *t; t++) {
                char c = what == R_CODE ? code_char(*t) : (*t >= 32 && *t < 127) ? *t : 0;
                if (c && n + 1 < size) {
                    buf[n++] = c;
                    buf[n] = 0;
                    changed = 1;
                }
            }
            if (changed && what == R_NAME)
                save_name();
            if (changed && what == R_TITLE)
                netplay_set_public(netplay_public(), L.title);
        } else if (erase) {
            pressB = 1;
        }

        /* Left and right change a value. */
        if (change != 0) {
            switch (what) {
                case R_PLAYERS:
                    L.wanted += change;
                    L.wanted = L.wanted < 2 ? 16 : L.wanted > 16 ? 2 : L.wanted;
                    break;
                case R_BALL_CONTROLS:
                    netplay_set_normal_ball(!netplay_host_normal_ball());
                    break;
                case R_ANY_BALL:
                    netplay_set_any_ball(!netplay_host_any_ball());
                    break;
                case R_ANY_WORLD:
                    netplay_set_any_world(!netplay_host_any_world());
                    break;
                case R_LISTED:
                    netplay_set_public(!netplay_public(), L.title);
                    break;
                case R_COLORS:
                    choice = (choice + change + CUSTOM + 1) % (CUSTOM + 1);
                    apply_colors();
                    save_colors();
                    break;
                case R_GLOVE:
                case R_BALL:
                    step_shade(&custom[what == R_GLOVE ? 0 : 1], change);
                    apply_colors();
                    save_colors();
                    break;
                case R_DELAY: {
                    int d = netplay_input_delay() + change;
                    netplay_set_input_delay(d < 0 ? 0 : d > 8 ? 8 : d);
                    break;
                }
                default:
                    break;
            }
        }

        /* A presses the chosen row. */
        if (pressA) {
            switch (what) {
                case R_HOST_PAGE:
                case R_JOIN_PAGE:
                case R_OPTIONS_PAGE:
                    L.page = what == R_HOST_PAGE ? PAGE_HOST : what == R_JOIN_PAGE ? PAGE_JOIN : PAGE_OPTIONS;
                    L.selected = 0;
                    L.error[0] = 0;
                    break;
                case R_ALONE:
                    netplay_rooms_close();
                    G64TitleOn(0);
                    SDL_StopTextInput();
                    return 0;
                case R_HOST:
                    if (start(&L, rom, romSize, NULL, PAGE_HOST))
                        printed = 0;
                    break;
                case R_CODE:
                case R_JOIN:
                    if (strlen(L.code) == NETPLAY_ROOM_CODE)
                        start(&L, rom, romSize, L.code, PAGE_JOIN);
                    break;
                case R_ROOM: {
                    const net_room *r = netplay_room(rows[L.selected].room);
                    char c[NETPLAY_ROOM_CODE + 1];
                    if (r != NULL) {
                        snprintf(c, sizeof c, "%s", r->code);
                        start(&L, rom, romSize, c, PAGE_JOIN);
                    }
                    break;
                }
                case R_COPY:
                    SDL_SetClipboardText(room);
                    L.copied = 1;
                    break;
                case R_START_NOW:
                    netplay_start_now();
                    break;
                case R_CANCEL:
                    pressB = 1;
                    break;
                case R_PACK_YES:
                case R_PACK_NO:
                    netplay_pack_answer(what == R_PACK_YES);
                    break;
                default:
                    break;
            }
        }
        /* B goes back to the first page. On a waiting page it cancels. */
        if (pressB) {
            if (L.connecting) {
                netplay_cancel();
                L.connecting = 0;
                L.page = L.back;
            } else if (L.page != PAGE_MAIN) {
                L.selected = L.page == PAGE_HOST ? 0 : L.page == PAGE_JOIN ? 1 : 2;
                L.page = PAGE_MAIN;
            }
            L.error[0] = 0;
        }

        /* The page with this frame's changes, then the game's frame. */
        build_page(&L);
        g64_title_frame(window);
        SDL_GL_SwapWindow(window);
        SDL_Delay(10);
    }
}


/* The online game's pause menu, its chat lines over the picture and a script's line are drawn by the game itself,
   in the game's own style (netplay.c calls G64MenuView, G64ChatView and G64LineView before each picture). Nothing
   of them is drawn here. */
void g64_pause_draw(SDL_Window *window) {
    (void)window;
}
