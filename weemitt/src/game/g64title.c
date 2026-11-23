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
#include "g64title.h"

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
    char hints[64];
    u32 glove;
    u32 ball;
} page;

/* What the start screen keeps from frame to frame: the bars it added to the title's, each row's spin, and the row
   that was chosen before. */
static s32 barsMade;
static f32 jog[G64_TITLE_ROWS];
static s32 lastSelected = -1;
static s32 lastCount = -1;
static f32 gloveTurn;

/* The title's four bars are foreground objects 0 to 3, and 4 is its logo. The start screen adds three more bars
   (5 to 7), the player's glove (8) and ball (9). The game's list holds 10 objects. */
#define PLAYER_GLOVE 8
#define PLAYER_BALL 9
/* Where the bars stand, in the title screen's world: their middle is at x 185 (MID) of the picture. The player's
   glove and ball turn at the right of them. */
#define BAR_X -31.0f
#define MID 185
static Actor *bar(s32 i) {
    return foregroundObjects[i < 4 ? i : i + 1];
}

/* Copies the program's text into the page. The game's fonts have capitals, digits and a few marks only. A
   character they lack is drawn as a space; the percent sign would be read as a format. */
static void copy_text(char *out, s32 size, const char *text) {
    s32 n = 0;
    for (; text != NULL && *text != 0 && n < size - 1; text++) {
        char c = *text;
        if (c >= 'a' && c <= 'z')
            c = c - 'a' + 'A';
        if ((c >= 32 && c < 127 && c != '%') || (u8)c >= 0xA3)
            out[n++] = c;
    }
    out[n] = 0;
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
    page.hints[0] = 0;
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
    copy_text(page.notes[page.noteCount], sizeof page.notes[0], text);
    page.noteColor[page.noteCount++] = rgb;
}

void G64TitleBig(const char *text) {
    copy_text(page.big, sizeof page.big, text);
}

void G64TitleHints(const char *text) {
    copy_text(page.hints, sizeof page.hints, text);
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

/* Draws one line of text. The game keeps 63 characters of a text. */
static void text(s32 x, s32 y, u32 rgb, s32 alpha, f32 scale, s32 font, const char *s, s32 centre) {
    char line[64];
    copy_text(line, sizeof line, s);
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
        /* The player's glove and ball: the game's own models, from its generic banks, which the title screen does
           not load. The textures come first: the objects find theirs when they load. */
        LoadTextureBank(GENERIC_TEX_BANK);
        LoadObjectBank(1);
        initFGObject("goodglov.ndo", 5000.0f, 0.0f, 400.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 255);
        initFGObject("gball.ndo", 5000.0f, 0.0f, 400.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 255);
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
    row_layout(page.count, &top, &step, &size);
    if (page.heading[0]) {
        text(MID, 24, 0xFFFFFF, 255, 0.8f, 0, page.heading, 1);
    }
    for (i = 0; i < G64_TITLE_ROWS; i++) {
        Actor *b = bar(i);
        if (i >= page.count) {
            b->pos.x = 5000.0f;
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
            b->pos.y = (y + 8.0f - 119.5f) / 0.73f;
            b->pos.z = 400.0f;
            b->scale.x = 0.85f;
            b->scale.y = size * (sel ? 1.0f : 0.9f);
            b->scale.z = 1.0f;
            b->alpha = 255;
            b->rot.x = jog[i];
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
            /* A row that is being typed in shows a blinking mark at its end. */
            if ((page.rows[i].flags & G64_TITLE_TYPING) && (frameCount2 & 8)) {
                s32 n = strlen(line);
                if (n < 62) {
                    line[n] = '-';
                    line[n + 1] = 0;
                }
            }
            /* A long row gets smaller letters: the bar is about 150 points wide. */
            SetFont(0);
            {
                s32 w = GetStringPixelLength(line, scale, scale);
                if (w > 150)
                    scale = scale * 150.0f / w;
            }
            text(MID, (s32)y, off ? 0xA0A0A0 : 0xFFFFFF, alpha, scale, 0, line, 1);
            /* A value that left and right change has a minus and a plus at its ends (the large font has no
               plus). */
            if (sel && (page.rows[i].flags & G64_TITLE_ARROWS)) {
                s32 half;
                SetFont(0);
                half = GetStringPixelLength(line, scale, scale) / 2;
                text(MID - half - 10, (s32)y + 2, 0xFFFF60, 255, 0.8f, 1, "-", 1);
                text(MID + half + 10, (s32)y + 2, 0xFFFF60, 255, 0.8f, 1, "+", 1);
            }
            if (sel) {
                SetFont(0);
                aimBallX = MID - GetStringPixelLength(line, scale, scale) / 2 - 22;
                aimBallY = (s32)y - 10;
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
        if (page.big[0]) {
            text(MID, (s32)y + 12, 0xFFFF60, 255, 1.3f, 0, page.big, 1);
            y += 30.0f;
        }
        if (y > 226.0f - page.noteCount * 9.0f) {
            y = 226.0f - page.noteCount * 9.0f;
        }
        for (i = 0; i < page.noteCount; i++) {
            text(160, (s32)y + i * 9, page.noteColor[i], 255, 0.45f, 1, page.notes[i], 1);
        }
    }
    if (page.hints[0]) {
        text(160, 236, 0xFFFFFF, 255, 0.6f, 1, page.hints, 1);
    }
    /* The player's glove and ball turn slowly at the right, in the player's colors. For these two, a larger y is
       higher in the picture: the glove's middle is at y 80 and the ball's at y 130. */
    {
        Actor *g = foregroundObjects[PLAYER_GLOVE];
        Actor *b = foregroundObjects[PLAYER_BALL];
        gloveTurn += 0.03f * deltaTime;
        g->pos.x = -150.0f;
        g->pos.y = 51.0f;
        g->pos.z = 400.0f;
        g->scale.x = g->scale.y = g->scale.z = 0.15f;
        g->rot.x = 0.0f;
        g->rot.y = gloveTurn;
        g->rot.z = 0.0f;
        g->alpha = 255;
        b->pos.x = -150.0f;
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
