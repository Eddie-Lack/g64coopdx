/* g64coopdx: the start screen of the online program, drawn by the game's title screen (g64title.c). */
#ifndef G64TITLE_H
#define G64TITLE_H

#ifdef __cplusplus
extern "C" {
#endif

/* The most rows a page has. */
#define G64_TITLE_ROWS 7
/* A row's flags: grayed out, a value that left and right change, and a value that is being typed. */
#define G64_TITLE_OFF 1
#define G64_TITLE_ARROWS 2
#define G64_TITLE_TYPING 4
/* A color that the player chose: this bit plus 0xRRGGBB (the session's G64_COLOR_SET). */
#define G64_TITLE_COLOR_SET 0x1000000u

/* Turns the start screen on or off. While it is on, the title screen shows the page and ignores the controller. */
void G64TitleOn(int on);
/* 1 once the title screen shows the start screen's page. */
int G64TitleReady(void);
/* Starts a new page with a heading. The rows, notes, big text and hints that follow belong to it. */
void G64TitlePage(const char *heading);
void G64TitleRow(const char *label, const char *value, int flags);
void G64TitleSelect(int row);
/* A line of small text below the rows, in the color 0xRRGGBB. */
void G64TitleNote(const char *text, unsigned rgb);
/* Large text below the rows, such as a room code. */
void G64TitleBig(const char *text);
/* The line at the bottom that names the buttons. */
void G64TitleHints(const char *text);
/* The player's glove and ball colors (0, or G64_TITLE_COLOR_SET plus 0xRRGGBB). */
void G64TitleColors(unsigned glove, unsigned ball);

#ifdef __cplusplus
}
#endif

#endif
