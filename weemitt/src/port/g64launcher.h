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

#ifdef __cplusplus
}
#endif

#endif
