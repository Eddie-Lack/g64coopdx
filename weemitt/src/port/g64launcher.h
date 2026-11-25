/* g64coopdx: the start screen of the online program (g64launcher.cpp). */
#ifndef G64LAUNCHER_H
#define G64LAUNCHER_H

#include <SDL2/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Shows the start screen until the player chooses. Returns 1 when an online game starts (level and players say how),
   0 to play alone, -1 when the window is closed. relay is the relay's address. The menu must be set up (menu_init). */
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
/* After the start screen, when the player chose to play alone: the player's glove and ball models, the file select
   at once, and the save's second file as a complete game (G64SoloStart). Call it after the state from before the
   start screen is back (g64_title_end). */
void g64_launcher_solo(void);
/* The mouse's camera sensitivity (mouselook.h), kept in settings.ini with the other settings (platform.c). The new
   value is kept in range and saved at once. */
int port_mouse_sensitivity(void);
void port_set_mouse_sensitivity(int s);
/* Reads the player's saved name (name.txt in the program's folder) unless a name was already given. */
void g64_load_name(void);

#ifdef __cplusplus
}
#endif

#endif
