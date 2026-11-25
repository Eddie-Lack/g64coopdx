PORT_C_BEGIN
#include "common.h"
#include "ident.h"
#include "tip.h"
#include "jalloc.h"
#include "game.h"
#include "hand.h"
#include "controller.h"
#include "maths.h"
#include "debris.h"
#include "overlay.h"
#include "sound.h"
#include "delta.h"
#include "g64players.h"

typedef struct TipLine {
    /* 0x00 */ PTR32(char) str;
    /* 0x04 */ s32 type;
} TipLine;

extern PTR32(TipLine) tipText;
extern Overlay tipOverlay;
extern Overlay tipOverlay2;
extern Overlay tipOverlay3;
extern s32 tipSentence;
extern f32 tipFontScale;
extern u8 tipFontAlpha;
extern PTR32(char) mrTipString[];
void SetFont(s32 type);
struct Text *NEW_TEXT(s16 x, s16 y, u8 r, u8 g, u8 b, u8 a, f32 sx, f32 sy, u8 oneShot, char *str, u8 centre);
extern s8 readingTip;
extern u8 gamePaused;
extern s32 frameCount;
extern u8 ActiveController;
extern PTR32(struct Texture) tipTexture[2];
extern s16 lastTipTag;
extern TipLine tipTextEnglish[];
extern TipLine tipTextFrench[];
extern TipLine tipTextGerman[];

void InitTipList(void) {
    tipList.head.next = tipList.head.prev = &tipList.head;
    tipList.count = 0;
    switch (gameInfo.language) {
        case 0:
            tipText = tipTextEnglish;
            break;
        case 1:
            tipText = tipTextFrench;
            break;
        case 2:
            tipText = tipTextGerman;
            break;
    }
}

void AddTip(Tip *tip) {
    Tip *ptr = tipList.head.next;

    tip->prev = ptr;
    tip->next = ptr->next;
    ptr->next->prev = tip;
    ptr->next = tip;
    tipList.count++;
}

/* g64coopdx: the text of a tip, for a tip that a player reads on their own (g64players.c, "Tips"). A tip's text is
   a run of lines. A page ends with a line of type 1, and the tip's last page with a line of type 2.
   TipFirstLine gives the number of a tip's first line, TipPageEnd the last line of the page that starts at a line. */
s32 TipFirstLine(s32 tag) {
    s32 line = 0;
    s32 x;

    for (x = 0; x < tag; x++) {
        while (tipText[line].type != 2) {
            line++;
        }
        line++;
    }
    return line;
}

s32 TipPageEnd(s32 line) {
    while (tipText[line].type == 0) {
        line++;
    }
    return line;
}

s32 TipLineType(s32 line) {
    return tipText[line].type;
}

const char *TipLineText(s32 line) {
    return tipText[line].str;
}

/* g64coopdx: puts the game's own tip box and text into the frame that is drawn next, for the player whose view
   it is (G64TipView). The page starts at `line`; `time` counts the frames since the tip opened, and the box grows
   and fades in over them as in UpdateTip. The client loads the state again after the view, so nothing here stays. */
void TipDrawPage(s32 line, s32 time) {
    s32 grown = time < 6 ? time + 1 : 6;
    s32 alpha = time < 6 ? 0 : (time - 5) * 42;
    s32 row = 1;

    if (alpha > 252) {
        alpha = 252;
    }
    tipOverlay.x = 80.0f;
    tipOverlay.y = 60.0f;
    tipOverlay.scaleX = 0x400;
    tipOverlay.scaleY = 0x400;
    tipOverlay.alpha = alpha;
    if (time < 6) {
        tipOverlay2.x = grown * 26.0f;
        tipOverlay2.y = grown * 20.0f;
        tipOverlay2.scaleX = grown * 0x42A;
        tipOverlay2.scaleY = grown * 0x341;
    } else {
        tipOverlay2.x = 160.0f;
        tipOverlay2.y = 120.0f;
        tipOverlay2.scaleX = 0x1900;
        tipOverlay2.scaleY = 0x1388;
    }
    tipOverlay2.alpha = 200;
    tipOverlay2.r = 255;
    tipOverlay2.g = 200;
    tipOverlay2.b = 255;
    AddOverlay(&tipOverlay2);
    AddOverlay(&tipOverlay);
    SetFont(1);
    NEW_TEXT(160, 65, 170, 170, 255, alpha, 1.0f, 1.0f, 1, mrTipString[gameInfo.language], 1);
    while (tipText[line].type == 0) {
        NEW_TEXT(160, row++ * 15 + 70, 255, 255, 255, alpha, 1.0f, 1.0f, 1, tipText[line].str, 1);
        line++;
    }
    NEW_TEXT(160, row * 15 + 70, 255, 255, 255, alpha, 1.0f, 1.0f, 1, tipText[line].str, 1);
    if (frameCount & 8) {
        NEW_TEXT(245, 190, 255, 255, 255, alpha, 1.0f, 1.0f, 1, "\xA3", 1);
    }
    SetFont(0);
}

void UpdateTips(void) {
    Tip *tip;
    Vec3 tempVect2;
    Vec3 tempVect;
    f32 sine;

    readingTip = FALSE;
    if (deltaTick) {
        G64TipsStep();
    }
    for (tip = tipList.head.next; tip != &tipList.head; tip = tip->next) {
        /* The timer counts original frames. The bob and the pulse read it as continuous time. */
        if (deltaTick) {
            tip->timer++;
        }
        if (!gamePaused && tip->state == 0) {
            if (tip->read == 0) {
                sine = SineWave(5.0f, FrameTime(), 0) * 10.0f;
                tip->sprite.scaleX = sine + 48.0f;
                tip->sprite.scaleY = 48.0f - sine;
                sine = SineWave(2.0f, ((s16)tip->timer + deltaPhase) / 3.0f, 0);
                tempVect2.z = 0;
                tempVect2.x = 1.0f;
                tempVect2.y = -0.5f;
                RotateVector2D(&tempVect2, &tempVect2, frameCount);
                tempVect = tip->pos;
                tip->sprite.pos.y = tip->pos.y + sine * 4.0f + 10.0f;
                tempVect.y += sine;
                if (deltaTick && (frameCount & 1)) {
                    tempVect = tip->sprite.pos;
                    tempVect.x += Random(7) - 3;
                    tempVect.y -= 5.0f;
                    tempVect.z += Random(7) - 3;
                    AddDebris(WISP, &tempVect, &tempVect2, NULL, NULL);
                }
                if (deltaTick) {
                    if (Random(20) == 1) {
                        tip->sprite.tex = tipTexture[1];
                    } else {
                        tip->sprite.tex = tipTexture[0];
                    }
                }
                tip->sprite.alpha = 240;
            } else {
                sine = SineWave(10.0f, FrameTime(), 0) * 10.0f;
                tip->sprite.scaleX = sine + 48.0f;
                tip->sprite.scaleY = 48.0f - sine;
                tip->sprite.alpha = (SineWave(5.0f, ((s16)tip->timer + deltaPhase) / 3.0f, 0) + 1.0f) * 30.0f + 60.0f;
                tip->sprite.tex = tipTexture[1];
                tip->shadowAlpha = 140;
                tip->shadowTarget = 140;
            }
            if (G64TipsOwn()) {
                /* g64coopdx: online a tip opens for the players who ask for it, and the two tips that the game
                   opens by itself open for everyone. The level doesn't stop: tip->state stays 0. */
                u32 readers = 0;
                if ((gameInfo.level == HUB1 && tip->tag == 0 && frameCount == 330 && gameInfo.cameoActive)
                    || (gameInfo.level == ACOURSE && tip->tag == 0 && frameCount == 67)) {
                    readers = G64TipEveryone();
                } else if (deltaTick) {
                    readers = G64TipReaders(&tip->pos);
                }
                if (readers && G64TipOpen(readers, tip->tag, &tip->pos)) {
                    tip->read = 1;
                    lastTipTag = tip->tag;
                }
            } else if (deltaTick
                && ((DistanceBetweenPoints(&hand.pos, &tip->pos) < 20.0f && DeltaTickPressed(ActiveController, BUTTON_B))
                    || (gameInfo.level == HUB1 && tip->tag == 0 && frameCount == 330 && gameInfo.cameoActive)
                    || (gameInfo.level == ACOURSE && tip->tag == 0 && frameCount == 67))) {
                PlaySample(59, &tip->pos, 100, 190);
                tip->state = 1;
                tip->read = 1;
                tip->animTime = 0;
                lastTipTag = tip->tag;
            }
        }
        if (tip->state == 1) {
            readingTip = TRUE;
            UpdateTip(tip);
        }
        if (tip->state == 2) {
            tip->state = 0;
        }
    }
}

void UpdateTip(Tip *tip) {
    s32 x;
    s32 line;

    SetFont(1);
    /* The box's animation, the page turns and their sounds step once per original frame. The text below is made in
       every update: a one-shot text lasts one update. */
    if (deltaTick) {
        x = 0;
        if (tip->animTime == 0) {
            gamePaused = TRUE;
            tipOverlay.x = 80.0f;
            tipOverlay.y = 60.0f;
            tipOverlay2.x = tipOverlay2.y = 0.0f;
            tipOverlay3.x = 180.0f;
            tipOverlay3.y = 160.0f;
            AddOverlay(&tipOverlay2);
            AddOverlay(&tipOverlay);
            tipOverlay.scaleX = 0x40;
            tipOverlay.scaleY = 0x40;
            tipOverlay.scaleX = 0x400;
            tipOverlay.scaleY = 0x400;
            tipOverlay.alpha = 0;
            tipOverlay2.scaleX = 0;
            tipOverlay2.scaleY = 0;
            tipOverlay3.scaleX = tipOverlay3.scaleY = 0;
            tipOverlay2.alpha = 200;
            tipOverlay2.r = 255;
            tipOverlay2.g = 200;
            tipOverlay2.b = 255;
            tipFontAlpha = 0;
            tipSentence = 0;
            tipFontScale = 1.0f;
            for (; x < tip->tag; x++) {
                while (tipText[tipSentence].type != 2) {
                    tipSentence++;
                }
                tipSentence++;
            }
        }
        if (tip->animTime < 6) {
            tipOverlay2.x += 26.0f;
            tipOverlay2.y += 20.0f;
            tipOverlay2.scaleX += 0x42A;
            tipOverlay2.scaleY += 0x341;
        } else if (tip->animTime < 12) {
            tipOverlay2.scaleX = 0x1900;
            tipOverlay2.scaleY = 0x1388;
            tipFontAlpha += 42;
            tipOverlay.alpha += 42;
            tipOverlay2.x = 160.0f;
            tipOverlay2.y = 120.0f;
        }
        if (DeltaTickPressed(ActiveController, BUTTON_A) && tip->animTime >= 13 && tip->animTime < 1000) {
            PlaySample(180, &tip->pos, 120, 120);
            x = tipSentence;
            while (tipText[x++].type == 0) {
            }
            x--;
            if (tipText[x].type == 2) {
                tip->animTime = 1000;
            } else {
                while (tipText[tipSentence++].type == 0) {
                }
            }
        }
        if (tip->animTime == 100) {
            tipOverlay3.scaleX = tipOverlay3.scaleY = 0x400;
            if (frameCount & 4) {
                tipOverlay3.r = tipOverlay3.g = tipOverlay3.b = 255;
            } else {
                tipOverlay3.r = tipOverlay3.g = tipOverlay3.b = 200;
            }
        } else {
            tipOverlay3.scaleX = tipOverlay3.scaleY = 0;
        }
        if (tip->animTime < 100 || tip->animTime >= 1000) {
            tip->animTime++;
        }
        if (tip->animTime >= 1000 && tip->animTime < 1007) {
            tipFontAlpha -= 42;
            tipOverlay.alpha -= 42;
        }
        if (tip->animTime >= 1007) {
            tipFontAlpha = 0;
            tipOverlay2.x += 26.0f;
            tipOverlay2.y += 20.0f;
            tipOverlay2.scaleX -= 0x42A;
            tipOverlay2.scaleY -= 0x341;
        }
        if (tip->animTime == 1012) {
            tip->state = 2;
            gamePaused = FALSE;
            pad[ActiveController].latch[BUTTON_A] = 1;
            SubOverlay(&tipOverlay);
            SubOverlay(&tipOverlay2);
        }
    }
    x = tipSentence;
    line = 1;
    NEW_TEXT(160, 65, 170, 170, 255, tipFontAlpha, tipFontScale, tipFontScale, 1, mrTipString[gameInfo.language], 1);
    while (tipText[x].type == 0) {
        NEW_TEXT(160, line++ * 15 + 70, 255, 255, 255, tipFontAlpha, tipFontScale, tipFontScale, 1, tipText[x].str, 1);
        x++;
    }
    NEW_TEXT(160, line * 15 + 70, 255, 255, 255, tipFontAlpha, tipFontScale, tipFontScale, 1, tipText[x].str, 1);
    if (frameCount & 8) {
        NEW_TEXT(245, 190, 255, 255, 255, tipFontAlpha, tipFontScale, tipFontScale, 1, "\xA3", 1);
    }
    SetFont(0);
}

PORT_C_END
