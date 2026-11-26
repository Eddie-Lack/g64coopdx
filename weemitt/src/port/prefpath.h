/* The folder of the player's own files (settings, save, logs), as SDL_GetPrefPath names it: weemitt's, unless the
   program that builds this code names its own (g64coopdx's online program does, in studio.mk). */
#ifndef PREFPATH_H
#define PREFPATH_H

#ifndef PORT_PREF_ORG
#define PORT_PREF_ORG "weemitt"
#endif
#ifndef PORT_PREF_APP
#define PORT_PREF_APP "Glover"
#endif
/* The program's name in its window's title and its error boxes: the program's own folder's name when it has one
   (g64coopdx), and "Glover" in weemitt's builds. */
#ifndef PORT_PRODUCT
#define PORT_PRODUCT (PORT_PREF_ORG[0] == 'w' ? "Glover" : PORT_PREF_ORG)
#endif

#endif
