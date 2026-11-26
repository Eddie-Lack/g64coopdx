/* g64coopdx: the start screen of the online program in the style of the game's title menu. The program
   (g64launcher.cpp) runs the game's own title screen behind it, and sets the game's state back to before the title
   when the player has chosen (platform.c, g64_title_end). While the start screen is on, the title menu's own words
   and its controller input are off: the title's bars carry the start screen's rows, and its glove shows the player's
   colors. The program describes the page before each frame with the G64Title functions below. */
PORT_C_BEGIN
#include "common.h"
#include "libc.h"
#include "maths.h"
#include "overlay.h"
#include "actor.h"
#include "camera.h"
#include "delta.h"
#include "enums.h"
#include "text.h"
#include "jalloc.h"
#include "g64title.h"
#include "game.h"

struct Text *NEW_TEXT(s16 x, s16 y, u8 r, u8 g, u8 b, u8 a, f32 sx, f32 sy, u8 oneShot, char *str, u8 centre);
void SetFont(u8 type);
s16 GetStringPixelLength(char *str, f32 sx, f32 sy);
void TitleScreenCameraSpin2(void);
void LoadObjectBank(s32 num);
void LoadTextureBank(s32 num);
void GetQuaternionFromXYZRot(Quat *q, Vec3 *rot);
void InitActorStructures(Actor *actor, s32 n);
void AnimateActor(Actor *actor, s32 animNum, s32 loop, s32 queue, f32 speed);
void initFGObject(char *NameString, f32 XPos, f32 YPos, f32 ZPos, f32 XRot, f32 YRot, f32 ZRot, f32 XSize, f32 YSize,
                  f32 ZSize, s32 TL);

extern PTR32(Actor) foregroundObjects[];
extern s32 numForegroundObjects;
extern Overlay ballPicOL;
extern Overlay gloverOL[6];
extern f32 animFloat;
extern s32 demoTimer;
extern s32 cameFromTitle;
extern s32 frameCount2;
extern s32 aimBallX;
extern s32 aimBallY;
extern PTR32(char) startMenuStrings[][3];

/* The page: what the program asks for. */
typedef struct {
    char label[40];
    char value[40];
    s32 flags;
} TitleRow;

#define NOTES 10

static struct {
    s32 on;
    char heading[40];
    TitleRow rows[G64_TITLE_ROWS];
    s32 count;
    s32 selected;
    char notes[NOTES][64];
    u32 noteColor[NOTES];
    s32 noteCount;
    char big[16];
    s32 back; /* the page goes back with B: the game's own "B BACK" mark shows, as on its file select */
    u32 glove;
    u32 ball;
} page;

/* The game draws text and pictures through one buffer of about 180 letters a frame (overlay_dl, font.c). The start
   screen has more text than that: while it's on, the game draws them through two larger buffers on the game's
   heap, one for each picture buffer. */
#define TEXT_GFX 12000
static Gfx *textGfx[2];

Gfx *G64TitleTextStart(s32 buffer) {
    return page.on && textGfx[0] != NULL ? textGfx[buffer ? 0 : 1] : NULL;
}

/* 1 when the buffer has no room left for n more letters or pictures. */
s32 G64TitleTextFull(Gfx *next, s32 n) {
    Gfx *start = textGfx[0] <= next && next < textGfx[0] + TEXT_GFX ? textGfx[0] : textGfx[1];
    return next + NUM_DL(n) > start + TEXT_GFX;
}

/* What the start screen keeps from frame to frame: the bars it added to the title's, each row's spin, and the row
   that was chosen before. */
static s32 barsMade;
static f32 jog[G64_TITLE_ROWS]; /* from 6.28 down to 0 after the row is chosen */
static s32 lastSelected = -1;
static s32 lastCount = -1;
static f32 gloveTurn;

/* The glove stands, then shows one of these moves, then stands again, a few seconds each, as a character does on a
   fighting game's character screen. Each move plays at its own speed. A move that loops plays for SHOW_TIME; a move
   that doesn't plays once. The game's own queue starts the next move when the one before has ended (AnimateActor),
   as the title's glove does. None of them needs the ball. */
typedef struct {
    s32 anim;
    s32 loop;
} ShowMove;
static const ShowMove showMoves[] = {
    {HANDANIM_WAVE, 0},  {HANDANIM_TIPTOE, 1},    {HANDANIM_THUMBSUP, 0}, {HANDANIM_CARTWHEEL, 0},
    {HANDANIM_OKAY, 0},  {HANDANIM_DENIS, 1},     {HANDANIM_POINTCYCLE, 1}, {HANDANIM_RUN, 1},
};
#define SHOW_MOVES ((s32)(sizeof showMoves / sizeof showMoves[0]))
/* Original frames that the glove stands between two moves, and that a looping move lasts. */
#define SHOW_TIME 90
static s32 showMove;
static s32 showTime;
static s32 showing; /* 1 while a move plays, 0 while the glove stands */

/* The name at the left, where the title has its Glover logo: one letter above the other. */
static const char logoName[] = "G64COOPDX";

/* The title's four bars are foreground objects 0 to 3, and 4 is its logo. The start screen adds three more bars
   (5 to 7), the player's glove (8) and ball (9). The game's list holds 10 objects. */
#define PLAYER_GLOVE 8
#define PLAYER_BALL 9
/* Where the bars stand, in the title screen's world: their middle is at x 185 (MID) of the picture. The player's
   glove and ball turn at the right of them. */
#define BAR_X -31.0f
#define MID 185
/* The bars' width: the red tile is TILE_WIDTH points wide at a scale of 1. A row's text keeps TILE_MARGIN from the
   tile's ends, and a row with a minus and a plus keeps ARROW_ROOM more on each side for them. */
#define BAR_SCALE 1.1f
#define TILE_WIDTH 126.0f
#define TILE_MARGIN 8.0f
#define ARROW_ROOM 14.0f
/* The tint of a row's value. The large font's own letters are yellow with a red edge: the tint makes them green. */
#define VALUE_TINT 0x60FF60
static Actor *bar(s32 i) {
    return foregroundObjects[i < 4 ? i : i + 1];
}

/* The characters of the game's two fonts (text.c): the large font (0) of the rows and the small font (1) of the
   notes. The game reads them from the ROM at power-on. */
extern char letters_index[];
extern char help_letters_index[];
char *my_strchr(char *s, char c);

/* 1 when the font draws c, a printable ASCII character that isn't the percent sign. The game draws a character
   that its font lacks with whatever picture was in that place before (text_sprite), so such a character must not
   reach the game. */
static s32 drawable(char c, s32 font) {
    char *index = font ? help_letters_index : letters_index;
    if (c < 32 || c >= 127 || c == '%')
        return 0;
    if (index[0] == 0)
        return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ';
    return my_strchr(index, c) != NULL;
}

int G64FontDraws(char c, s32 font) {
    return drawable(c, font);
}

int G64TitleDrawable(char c) {
    if (c >= 'a' && c <= 'z')
        c = c - 'a' + 'A';
    return drawable(c, 0) && drawable(c, 1);
}

/* Copies the program's text into the page, for the font that draws it. The game's fonts have capitals, digits and a
   few marks only. A character the font lacks becomes a space; the percent sign would be read as a format. The small
   font draws the controller's buttons for the bytes from 0xA3 up, which a name or a title that another player typed
   must not have. */
static void copy_text_in(char *out, s32 size, const char *text, s32 font) {
    s32 n = 0;
    for (; text != NULL && *text != 0 && n < size - 1; text++) {
        char c = *text;
        if (c >= 'a' && c <= 'z')
            c = c - 'a' + 'A';
        out[n++] = drawable(c, font) ? c : ' ';
    }
    out[n] = 0;
}

static void copy_text(char *out, s32 size, const char *text) {
    copy_text_in(out, size, text, 0);
}

void G64TitleOn(int on) {
    page.on = on;
}

void G64TitlePage(const char *heading) {
    copy_text(page.heading, sizeof page.heading, heading);
    page.count = 0;
    page.selected = 0;
    page.noteCount = 0;
    page.big[0] = 0;
    page.back = 0;
}

void G64TitleBack(int on) {
    page.back = on;
}

void G64TitleRow(const char *label, const char *value, int flags) {
    TitleRow *r;
    if (page.count >= G64_TITLE_ROWS)
        return;
    r = &page.rows[page.count++];
    copy_text(r->label, sizeof r->label, label);
    copy_text(r->value, sizeof r->value, value);
    r->flags = flags;
}

void G64TitleSelect(int row) {
    page.selected = row;
}

void G64TitleNote(const char *text, unsigned rgb) {
    if (page.noteCount >= NOTES)
        return;
    copy_text_in(page.notes[page.noteCount], sizeof page.notes[0], text, 1);
    page.noteColor[page.noteCount++] = rgb;
}

void G64TitleBig(const char *text) {
    copy_text(page.big, sizeof page.big, text);
}

void G64TitleColors(unsigned glove, unsigned ball) {
    page.glove = glove;
    page.ball = ball;
}

s32 G64TitleActive(void) {
    return page.on;
}

/* 1 once the title menu runs with the start screen's page: the program runs the game fast up to there. */
static s32 ready;
int G64TitleReady(void) {
    return ready;
}

/* The tint of the player's glove and ball: the player's colors (0 keeps the game's own colors). */
u32 G64TitleTint(Actor *a) {
    u32 c;
    if (!page.on || a == NULL || !barsMade)
        return 0;
    if (a == foregroundObjects[PLAYER_GLOVE])
        c = page.glove;
    else if (a == foregroundObjects[PLAYER_BALL])
        c = page.ball;
    else
        return 0;
    if (!(c & G64_TITLE_COLOR_SET))
        return 0;
    c &= 0xFFFFFF;
    return c ? c : 0x010101;
}

/* The player's chosen model for the glove or the ball on the start screen: their color word, which holds the
   model's number (g64models.h), or 0 for another actor. *kind says which. */
u32 G64TitleModelWord(Actor *a, s32 *kind) {
    if (!page.on || a == NULL || !barsMade)
        return 0;
    if (a == foregroundObjects[PLAYER_GLOVE]) {
        *kind = 0;
        return page.glove;
    }
    if (a == foregroundObjects[PLAYER_BALL]) {
        *kind = 1;
        return page.ball;
    }
    return 0;
}

/* The layout of the rows: up to four as large as the title's, more as smaller bars. */
static void row_layout(s32 count, f32 *top, f32 *step, f32 *size) {
    if (count <= 4) {
        *top = 62.0f;
        *step = 36.0f;
        *size = 0.85f;
    } else {
        *step = 136.0f / count;
        *top = 46.0f + *step / 2.0f;
        *size = *step / 42.0f;
    }
}

/* The width that a text of the large font has on the screen at a scale. The font's space between letters is below
   0, and the sprite scales it with the letters (sprite.c), so the width is the width at scale 1 times the scale.
   GetStringPixelLength adds the space unscaled: it gives more than this at a scale below 1. */
static f32 drawn_width(char *s, f32 scale) {
    SetFont(0);
    return GetStringPixelLength(s, 1.0f, 1.0f) * scale;
}

/* Draws one line of the page's text, which copy_text has already made safe. The game keeps 63 characters of a
   text. */
static void text(s32 x, s32 y, u32 rgb, s32 alpha, f32 scale, s32 font, const char *s, s32 centre) {
    char line[64];
    s32 n;
    for (n = 0; s[n] != 0 && n < 63; n++)
        line[n] = s[n];
    line[n] = 0;
    SetFont(font);
    NEW_TEXT(x, y, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, alpha, scale, scale, 1, line, centre);
}

u32 G64TitleTint(Actor *a);
s32 G64TitleMenu(void);

/* The title screen's frame while the start screen is on. MainTitleWait calls this instead of its own menu. */
s32 G64TitleMenu(void) {
    s32 i;
    f32 top, step, size, spinDecay;
    f32 pulse = sinf(animFloat * 0.65) * 90.0f + 128.0f + 38.0f;

    ready = 1;
    cameFromTitle = FALSE;
    /* No attract demo while the player reads the menu. */
    demoTimer = 600;
    for (i = 0; i < 6; i++) {
        if (gloverOL[i].x < 44.0f) {
            gloverOL[i].x += 2.0f * deltaTime;
        }
    }
    if (numForegroundObjects < 5) {
        return FALSE;
    }
    if (!barsMade && numForegroundObjects == 5) {
        for (i = 0; i < 3; i++) {
            initFGObject("newwave.ndo", -15.0f, 0.0f, 400.0f, 0.0f, 0.0f, 3.14f, 1.0f, 1.0f, 1.0f, 255);
            InitActorStructures(foregroundObjects[numForegroundObjects - 1], 4);
            AnimateActor(foregroundObjects[numForegroundObjects - 1], 0, 1, 1, 0.9f + i * 0.1f);
        }
        /* The player's glove and ball: the game's own models, from its generic banks, which the title screen doesn't
           load. The textures come first: the objects find theirs when they load. */
        LoadTextureBank(GENERIC_TEX_BANK);
        LoadObjectBank(1);
        initFGObject("goodglov.ndo", 5000.0f, 0.0f, 400.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 255);
        InitActorStructures(foregroundObjects[PLAYER_GLOVE], 4);
        AnimateActor(foregroundObjects[PLAYER_GLOVE], HANDANIM_STAND, 1, 0, 0.0f);
        initFGObject("gball.ndo", 5000.0f, 0.0f, 400.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 255);
        textGfx[0] = (Gfx *)JallocAlloc(TEXT_GFX * sizeof(Gfx), 0, "G64Text0");
        textGfx[1] = (Gfx *)JallocAlloc(TEXT_GFX * sizeof(Gfx), 0, "G64Text1");
        barsMade = 1;
    }
    if (!barsMade) {
        return FALSE;
    }
    if (page.selected != lastSelected || page.count != lastCount) {
        if (page.selected >= 0 && page.selected < G64_TITLE_ROWS) {
            jog[page.selected] = 6.28f;
        }
        lastSelected = page.selected;
        lastCount = page.count;
    }
    /* The title's Glover logo gives way to the program's name, in the game's large letters. */
    foregroundObjects[4]->alpha = 0;
    for (i = 0; logoName[i] != 0; i++) {
        char letter[2] = {logoName[i], 0};
        text(40, 34 + i * 22, 0xFFFFFF, 255, 1.3f, 0, letter, 1);
    }
    row_layout(page.count, &top, &step, &size);
    if (page.back) {
        /* Where the game's file select draws it (frontend.c). */
        SetFont(1);
        NEW_TEXT(260, 227, 255, 255, 255, 255, 1.0f, 1.0f, 1, startMenuStrings[10][gameInfo.language], 1);
        SetFont(0);
    }
    if (page.heading[0]) {
        text(MID, 24, 0xFFFFFF, 255, 0.8f, 0, page.heading, 1);
    }
    for (i = 0; i < G64_TITLE_ROWS; i++) {
        Actor *b = bar(i);
        if (i >= page.count) {
            b->pos.y = -1000.0f; /* below the picture */
            continue;
        }
        {
            f32 y = top + i * step;
            s32 sel = i == page.selected;
            s32 off = page.rows[i].flags & G64_TITLE_OFF;
            char line[80];
            f32 scale = (sel ? 0.9f : 0.75f) * (size < 0.85f ? size / 0.85f : 1.0f);
            s32 alpha = off ? 110 : sel ? (s32)pulse : 255;
            b->pos.x = BAR_X;
            /* A larger y is higher in the picture. The bar's middle stands 11 points above the text's place. */
            b->pos.y = (121.0f - y) / 0.77f;
            b->pos.z = 400.0f;
            b->scale.x = BAR_SCALE;
            /* A newly chosen bar swells and settles. The title's bars spin instead, but these bars turn around a
               point far from their middle. */
            b->scale.y = size * (sel ? 1.0f : 0.9f) * (1.0f + jog[i] * 0.04f);
            b->scale.z = 1.0f;
            b->alpha = 255;
            b->rot.x = 0.0f;
            b->rot.y = sinf(animFloat * (0.1f + i * 0.03f)) * 0.1 + 3.14;
            b->rot.z = sinf(animFloat * 0.15) * 0.1;
            if (page.rows[i].value[0]) {
                s32 n = 0;
                const char *s;
                for (s = page.rows[i].label; *s && n < 70; s++)
                    line[n++] = *s;
                if (page.rows[i].label[0] && n < 70)
                    line[n++] = ' ';
                for (s = page.rows[i].value; *s && n < 78; s++)
                    line[n++] = *s;
                line[n] = 0;
            } else {
                copy_text(line, sizeof line, page.rows[i].label);
            }
            /* The chosen row of typed text shows a blinking mark at its end. A space holds its place while it's
               off: the row keeps its size. */
            if (sel && (page.rows[i].flags & G64_TITLE_TEXT)) {
                s32 n = strlen(line);
                if (n < 62) {
                    line[n] = (frameCount2 & 8) ? '-' : ' ';
                    line[n + 1] = 0;
                }
            }
            /* The text fits inside the tile: a long row gets smaller letters. */
            {
                f32 room = TILE_WIDTH * BAR_SCALE - 2.0f * TILE_MARGIN;
                f32 width;
                if (sel && (page.rows[i].flags & G64_TITLE_ARROWS))
                    room -= 2.0f * ARROW_ROOM;
                /* A row that cuts its value: whole words go from the end until the rest and "..." fit. */
                if ((page.rows[i].flags & G64_TITLE_CUT) && drawn_width(line, scale) > room) {
                    s32 n = (s32)strlen(line), head = (s32)strlen(page.rows[i].label) + 1;
                    while (n > head) {
                        s32 k = n - 1;
                        while (k > head && line[k] != ' ')
                            k--;
                        n = k > head ? k : n - 1;
                        line[n] = 0;
                        if (n + 3 < 78) {
                            line[n] = line[n + 1] = line[n + 2] = '.';
                            line[n + 3] = 0;
                        }
                        if (drawn_width(line, scale) <= room)
                            break;
                        line[n] = 0;
                    }
                }
                if (drawn_width(line, scale) > room)
                    scale = scale * room / drawn_width(line, scale);
                width = drawn_width(line, scale);
                /* The game centers a text by a width that is wrong for a scale below 1 (drawn_width), so the row is
                   placed by its left end. The game moves a centered text up by 12 points at its scale. A row's value
                   has another color than its label: the two stay apart. */
                {
                    s32 x0 = MID - (s32)(width / 2.0f), k = (s32)strlen(page.rows[i].label) + 1;
                    if (page.rows[i].value[0] && page.rows[i].label[0] && !off && k < (s32)strlen(line)) {
                        char head[80];
                        memcpy(head, line, k);
                        head[k] = 0;
                        text(x0, (s32)(y - scale * 12.0f), 0xFFFFFF, alpha, scale, 0, head, 0);
                        text(x0 + (s32)drawn_width(head, scale), (s32)(y - scale * 12.0f), VALUE_TINT, alpha, scale, 0,
                             line + k, 0);
                    } else {
                        text(x0, (s32)(y - scale * 12.0f), off ? 0xA0A0A0 : 0xFFFFFF, alpha, scale, 0, line, 0);
                    }
                }
                /* A value that left and right change has a minus and a plus at its ends (the large font has no
                   plus). */
                if (sel && (page.rows[i].flags & G64_TITLE_ARROWS)) {
                    s32 half = (s32)(width / 2.0f);
                    text(MID - half - (s32)ARROW_ROOM / 2 - 2, (s32)y + 2, 0xFFFF60, 255, 0.8f, 1, "-", 1);
                    text(MID + half + (s32)ARROW_ROOM / 2 + 2, (s32)y + 2, 0xFFFF60, 255, 0.8f, 1, "+", 1);
                }
                if (sel) {
                    aimBallX = MID - (s32)(width / 2.0f) - (page.rows[i].flags & G64_TITLE_ARROWS ? 34 : 22);
                    aimBallY = (s32)y - 10;
                }
            }
        }
    }
    /* The pointer glides to the chosen row, as in the game's menus. */
    ballPicOL.x -= (ballPicOL.x - aimBallX) / DeltaDiv(3.0f);
    ballPicOL.y -= (ballPicOL.y - aimBallY) / DeltaDiv(3.0f);
    spinDecay = DeltaPowD(0.9);
    for (i = 0; i < G64_TITLE_ROWS; i++) {
        jog[i] *= spinDecay;
    }
    /* The notes stand below the rows, across the picture: the game draws no text right of x 288. */
    {
        f32 y = page.count > 0 ? top + page.count * step - step / 2.0f + 8.0f : 60.0f;
        f32 gap = 9.0f;
        if (page.big[0]) {
            text(MID, (s32)y + 12, 0xFFFF60, 255, 1.3f, 0, page.big, 1);
            y += 30.0f;
        }
        /* The lines close up when they don't fit below the rows (the small font at this size is 7 points high).
           Only when they still don't fit do they move up. */
        if (page.noteCount > 0 && page.noteCount * gap > 226.0f - y) {
            gap = (226.0f - y) / page.noteCount;
            if (gap < 7.0f) {
                gap = 7.0f;
            }
        }
        if (y > 226.0f - page.noteCount * gap) {
            y = 226.0f - page.noteCount * gap;
        }
        for (i = 0; i < page.noteCount; i++) {
            text(160, (s32)(y + i * gap), page.noteColor[i], 255, 0.45f, 1, page.notes[i], 1);
        }
    }
    /* The player's glove and ball turn slowly at the right, in the player's colors: the glove's middle at y 80 and the
       ball's at y 130. */
    {
        Actor *g = foregroundObjects[PLAYER_GLOVE];
        Actor *b = foregroundObjects[PLAYER_BALL];
        gloveTurn += 0.02f * deltaTime;
        /* The next move, or standing again, starts at the end of the current one's cycle (the queue). The time
           counts from when the queue is empty: the move before has started. */
        if (g->anim != NULL && g->anim->queue[0] != -1) {
            showTime = 0;
        } else if (deltaTick && ++showTime >= SHOW_TIME && g->anim != NULL) {
            showTime = 0;
            g->anim->loop = 0;
            if (!showing) {
                const ShowMove *m = &showMoves[showMove];
                showMove = (showMove + 1) % SHOW_MOVES;
                AnimateActor(g, m->anim, m->loop, 1, 0.0f);
                if (!m->loop) {
                    AnimateActor(g, HANDANIM_STAND, 1, 1, 0.0f);
                } else {
                    showing = 1;
                }
            } else {
                AnimateActor(g, HANDANIM_STAND, 1, 1, 0.0f);
                showing = 0;
            }
        }
        g->pos.x = -165.0f;
        g->pos.y = 51.0f;
        g->pos.z = 400.0f;
        g->scale.x = g->scale.y = g->scale.z = 0.15f;
        g->rot.x = 0.0f;
        g->rot.y = gloveTurn;
        g->rot.z = 0.0f;
        g->alpha = 255;
        b->pos.x = -165.0f;
        b->pos.y = -14.0f;
        b->pos.z = 400.0f;
        b->scale.x = b->scale.y = b->scale.z = 0.1f;
        b->rot.y = gloveTurn;
        b->alpha = 255;
    }
    TitleScreenCameraSpin2();
    for (i = 0; i < numForegroundObjects; i++) {
        GetQuaternionFromXYZRot(&foregroundObjects[i]->rotQuat, &foregroundObjects[i]->rot);
    }
    return FALSE;
}

PORT_C_END
