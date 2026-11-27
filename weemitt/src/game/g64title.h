/* g64coopdx: the start screen of the online program, drawn by the game's title screen (g64title.c). */
#ifndef G64TITLE_H
#define G64TITLE_H

#ifdef __cplusplus
extern "C" {
#endif

/* The most rows a page has. */
#define G64_TITLE_ROWS 7
/* A row's flags: grayed out, a value that left and right change, and a value that the player types (the chosen
   one shows a blinking mark). */
#define G64_TITLE_OFF 1
#define G64_TITLE_ARROWS 2
#define G64_TITLE_TEXT 4
/* A value that doesn't fit is cut at a word, with "..." after it, and the row keeps its letters' size. */
#define G64_TITLE_CUT 8
/* A color that the player chose: this bit plus 0xRRGGBB (the session's G64_COLOR_SET). */
#define G64_TITLE_COLOR_SET 0x1000000u

/* Turns the start screen on or off. While it's on, the title screen shows the page and ignores the controller. */
void G64TitleOn(int on);
/* 1 once the title screen shows the start screen's page. */
int G64TitleReady(void);
/* Starts a new page with a heading (empty for none). The rows, notes and big text that follow belong to it. */
void G64TitlePage(const char *heading);
void G64TitleRow(const char *label, const char *value, int flags);
void G64TitleSelect(int row);
/* A line of small text below the rows, in the color 0xRRGGBB. */
void G64TitleNote(const char *text, unsigned rgb);
/* Large text below the rows, such as a room code. */
void G64TitleBig(const char *text);
/* A test: on, the glove and the ball stand still, turned by angle (radians); 0 at the start. */
void G64TitleTurn(int on, float angle);
/* The player's glove and ball colors (0, or G64_TITLE_COLOR_SET plus 0xRRGGBB), each with the number of the model
   the player chose for it (g64models.h). */
void G64TitleColors(unsigned glove, unsigned ball);
/* 1 when both of the game's fonts draw the character (a lowercase letter counts as its capital). The start screen
   takes only these in a name or a title: other players see them in the large and the small font. */
int G64TitleDrawable(char c);

#ifdef __cplusplus
}
#endif

#endif
