/* The in-game menu (Dear ImGui), opened with Escape. The platform layer calls these from its window loop.
   The interface is plain C. Headless builds link menu_null.c, which does nothing. */
#ifndef PORT_MENU_H
#define PORT_MENU_H

#include <SDL2/SDL.h>
#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The input device in use, which the platform layer provides. The name is copied into name (empty for none). */
enum { PORT_DEVICE_NONE, PORT_DEVICE_CONTROLLER, PORT_DEVICE_JOYSTICK };
int port_input_device(char *name, int size);
/* The same for the second device, which the debug pad uses when it is set to the second controller. */
int port_debug_device(char *name, int size);

/* The menu edits st, which the platform layer reads, and saves it to settings.ini. */
void menu_init(SDL_Window *window, SDL_GLContext gl, Settings *st);
void menu_shutdown(void);
/* Gives the menu an event. Returns 1 when the menu used it and the platform layer should ignore it. */
int menu_handle_event(const SDL_Event *e);
/* True while the menu is open. The game goes on, and its controls keep working. */
int menu_is_open(void);
/* True while the menu takes the keyboard: a text field has it, or a key is being given to an input. The game then
   gets no keys. Escape still closes the menu. */
int menu_takes_keys(void);
/* Opens the menu, as Escape does. */
void menu_open(void);
/* Puts the window in full screen (1) or back (0), and keeps the choice in the settings (platform.c). */
void port_set_fullscreen(int on);
int port_fullscreen(void);
/* g64coopdx: 1 when the developer tools (except the log) and the studio's tools are off. The menu doesn't show
   them, and their keys and environment variables do nothing. The rollback engine under the online game doesn't
   depend on them. Weemitt's own builds return 0. */
int menu_tools_hidden(void);
/* Draws the menu over the finished game frame. Call it before the buffer swap. */
void menu_draw(SDL_Window *window);

#ifdef __cplusplus
}
#endif

#endif
