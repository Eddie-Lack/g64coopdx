/* g64coopdx: the start screen of the online program (g64launcher.cpp). */
#ifndef G64LAUNCHER_H
#define G64LAUNCHER_H

#include <SDL2/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Shows the start screen until the player chooses. Returns 1 when an online game starts (level and players say how),
   0 to play alone, -1 when the window is closed. relay is the relay's address. *players on the way in is the number of
   players that HOST GAME starts with (--players), or 0 for the one chosen last time. The menu must be set up
   (menu_init): Escape on the first page opens it, with the player's settings. */
int g64_launcher(SDL_Window *window, const unsigned char *rom, unsigned long romSize, const char *relay, int *level,
                 int *players);
/* Draws the player's own pause menu in the online game, when it's open (netplay_pause_open). Call it after the game's
   frame is drawn and before the window's buffers are swapped. */
void g64_pause_draw(SDL_Window *window);
/* The game's own title screen behind the start screen (platform.c): runs its frames for the time that has passed
   and draws the newest one into the window. */
void g64_title_frame(SDL_Window *window);
/* The controller's d-pad (or stick), A (or Start) and B as the game reads them: the G64_PAD bits that are held. */
int g64_title_pad(void);
#define G64_PAD_UP 1
#define G64_PAD_DOWN 2
#define G64_PAD_LEFT 4
#define G64_PAD_RIGHT 8
#define G64_PAD_A 16
#define G64_PAD_B 32
#define G64_PAD_Z 64
/* After the start screen, when the player chose to play alone: the player's glove and ball models, the file select
   at once, and the save's second file as a complete game (G64SoloStart). Call it after the state from before the
   start screen is back (g64_title_end). */
void g64_launcher_solo(void);
/* The mouse's camera sensitivity (mouselook.h), kept in settings.ini with the other settings (platform.c). The new
   value is kept in range and saved at once. */
int port_mouse_sensitivity(void);
void port_set_mouse_sensitivity(int s);
/* The free camera (src/client/freecam.h) and its Y the other way round, 0 or 1, kept in settings.ini too. */
int port_free_camera(void);
void port_set_free_camera(int on);
int port_invert_y(void);
void port_set_invert_y(int on);
/* Reads the player's saved name (name.txt in the program's folder) unless a name was already given. */
void g64_load_name(void);
/* Saves a name that was given on the command line (--name) as the player's name for the next time. */
void g64_save_name(void);
/* Reads the player's saved choices (options.txt in the program's folder): the input delay, and with host 1 also the
   host's choices on HOST GAME. Options on the command line come after it and win. */
void g64_load_options(int host);
/* The start screen's OPTIONS for the Esc menu's Player window, during a game too. Each change is saved at once, as on
   the start screen (colors.txt, name.txt, options.txt), and an online game sends it to the others (netplay.h). kind is
   G64_MODEL_GLOVE or G64_MODEL_BALL. A palette past the last one is CUSTOM. */
int g64_palette_count(void);
const char *g64_palette_name(int i);
int g64_palette(void);
void g64_set_palette(int i);
int g64_model(int kind);
void g64_set_model(int kind, int i);
unsigned g64_custom_color(int kind);
void g64_set_custom_color(int kind, unsigned rgb);
/* The custom colors a player steps through (GLOVE COLOR and BALL COLOR), by name. g64_shade_of gives -1 for a color
   that is none of them, whose name is SAVED. */
int g64_shade_count(void);
const char *g64_shade_name(int i);
unsigned g64_shade_rgb(int i);
int g64_shade_of(unsigned rgb);
void g64_set_name_tags(int on);
void g64_set_player_name(const char *name);

#ifdef __cplusplus
}
#endif

#endif
