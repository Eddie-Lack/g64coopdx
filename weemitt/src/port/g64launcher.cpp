/* g64coopdx: the start screen of the online program. It opens when the program starts without options (from a double
   click): host an online game, join one by its room code, or play alone. It draws with the menu's Dear ImGui context
   (menu.cpp) and connects through g64coopdx's netplay.c, which keeps the window drawing while it waits. */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "g64launcher.h"
#include "prefpath.h"
#include "../../../src/client/netplay.h"
#include "../../../src/session/session.h"

/* A room code: letters and digits, without blanks, in capitals. */
static int code_char(ImGuiInputTextCallbackData *d) {
    ImWchar c = d->EventChar;
    if (c >= 'a' && c <= 'z') {
        d->EventChar = (ImWchar)(c - 'a' + 'A');
        return 0;
    }
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ? 0 : 1;
}

/* The player's colors: a palette, or custom. Each color is chosen (G64_COLOR_SET plus 0xRRGGBB) or automatic (0, the
   color of the player's number). The colors multiply the glove's and the ball's own: white keeps them. They are kept in
   colors.txt, in the program's own folder: the palette's number and the two custom colors. */
typedef struct {
    const char *name;
    unsigned glove, ball;
} palette;
static const palette palettes[] = {
    {"Automatic", 0, 0},
    {"Classic", G64_COLOR_SET | 0xFFFFFF, G64_COLOR_SET | 0xFFFFFF},
    {"Cross-Stitch", G64_COLOR_SET | 0x3A3A44, G64_COLOR_SET | 0x8C1A1A},
    {"Blue", G64_COLOR_SET | 0x60A0FF, G64_COLOR_SET | 0x60A0FF},
    {"Green", G64_COLOR_SET | 0x70FF70, G64_COLOR_SET | 0x70FF70},
    {"Yellow", G64_COLOR_SET | 0xFFE040, G64_COLOR_SET | 0xFFE040},
    {"Red", G64_COLOR_SET | 0xFF6060, G64_COLOR_SET | 0xFF6060},
    {"Purple", G64_COLOR_SET | 0xC070FF, G64_COLOR_SET | 0xC070FF},
    {"Orange", G64_COLOR_SET | 0xFFA040, G64_COLOR_SET | 0xFFA040},
    {"Cyan", G64_COLOR_SET | 0x60FFFF, G64_COLOR_SET | 0x60FFFF},
};
#define PALETTES ((int)(sizeof palettes / sizeof palettes[0]))
#define CUSTOM PALETTES
static int choice;
static unsigned custom[2] = {G64_COLOR_SET | 0xFFFFFF, G64_COLOR_SET | 0xFFFFFF};
static int colorsLoaded;

static void colors_path(char *out, size_t size) {
    char *pref = SDL_GetPrefPath(PORT_PREF_ORG, PORT_PREF_APP);
    snprintf(out, size, "%scolors.txt", pref ? pref : "");
    if (pref)
        SDL_free(pref);
}

static void apply_colors(void) {
    if (choice == CUSTOM)
        netplay_set_colors(custom[0], custom[1]);
    else
        netplay_set_colors(palettes[choice].glove, palettes[choice].ball);
}

static void load_colors(void) {
    char path[1024];
    FILE *f;
    int c;
    unsigned g, b;
    colorsLoaded = 1;
    colors_path(path, sizeof path);
    f = fopen(path, "r");
    if (f) {
        if (fscanf(f, "%d %x %x", &c, &g, &b) == 3 && c >= 0 && c <= CUSTOM) {
            choice = c;
            custom[0] = G64_COLOR_SET | (g & 0xFFFFFF);
            custom[1] = G64_COLOR_SET | (b & 0xFFFFFF);
        }
        fclose(f);
    }
    apply_colors();
}

static void save_colors(void) {
    char path[1024];
    FILE *f;
    colors_path(path, sizeof path);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, "%d %06x %06x\n", choice, custom[0] & 0xFFFFFF, custom[1] & 0xFFFFFF);
        fclose(f);
    }
}

/* One custom color. Returns 1 when it changed. */
static int color_picker(const char *label, unsigned *c) {
    float rgb[3] = {((*c >> 16) & 255) / 255.0f, ((*c >> 8) & 255) / 255.0f, (*c & 255) / 255.0f};
    if (!ImGui::ColorEdit3(label, rgb, ImGuiColorEditFlags_NoInputs))
        return 0;
    *c = G64_COLOR_SET | (unsigned)(rgb[0] * 255.0f + 0.5f) << 16 | (unsigned)(rgb[1] * 255.0f + 0.5f) << 8 |
         (unsigned)(rgb[2] * 255.0f + 0.5f);
    return 1;
}

/* The colors' part of the start screen. */
static void colors_ui(void) {
    int changed = 0, i;
    if (!colorsLoaded)
        load_colors();
    if (ImGui::BeginCombo("Colors", choice == CUSTOM ? "Custom" : palettes[choice].name)) {
        for (i = 0; i <= CUSTOM; i++)
            if (ImGui::Selectable(i == CUSTOM ? "Custom" : palettes[i].name, i == choice)) {
                choice = i;
                changed = 1;
            }
        ImGui::EndCombo();
    }
    if (choice == CUSTOM) {
        changed |= color_picker("Glove", &custom[0]);
        ImGui::SameLine();
        changed |= color_picker("Ball", &custom[1]);
    }
    if (changed) {
        apply_colors();
        save_colors();
    }
}

int g64_launcher(SDL_Window *window, const unsigned char *rom, unsigned long romSize, const char *relay, int *level,
                 int *players) {
    static int wanted = 2;
    static char code[NETPLAY_ROOM_CODE + 1];
    char error[320] = "";
    int connecting = 0, hosting = 0, copied = 0, printed = 0;
    /* A test: G64_LAUNCHER_AUTO=host or join:CODE presses Host or Join at once. */
    const char *autoPress = getenv("G64_LAUNCHER_AUTO");

    if (!colorsLoaded)
        load_colors();
    if (autoPress && !strncmp(autoPress, "join:", 5)) {
        snprintf(code, sizeof code, "%s", autoPress + 5);
    }

    for (;;) {
        SDL_Event e;
        int w, h;

        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) {
                netplay_cancel();
                return -1;
            }
        }
        if (connecting) {
            int r = netplay_poll();
            if (r > 0) {
                *level = netplay_level();
                *players = netplay_players();
                return 1;
            }
            if (r < 0) {
                snprintf(error, sizeof error, "%s", netplay_error());
                netplay_cancel();
                connecting = 0;
            }
        }
        SDL_GetWindowSize(window, &w, &h);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2((float)w, (float)h), IM_COL32(28, 30, 48, 255));
        ImGui::SetNextWindowPos(ImVec2(w * 0.5f, h * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Always);
        ImGui::Begin("g64coopdx", NULL,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
                         | ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetWindowFontScale(1.3f);
        if (!connecting) {
            char err[160];

            int delay = netplay_input_delay();
            bool normalBall = netplay_host_normal_ball() != 0;

            ImGui::TextUnformatted("Host an online game");
            ImGui::SliderInt("Players", &wanted, 2, 8);
            /* The host's choice, for every player: the game's own ball controls, or the easy ones (the default). */
            if (ImGui::Checkbox("Normal ball controls", &normalBall)) {
                netplay_set_normal_ball(normalBall ? 1 : 0);
            }
            if (ImGui::Button("Host", ImVec2(-1, 0)) || (autoPress && !strcmp(autoPress, "host"))) {
                autoPress = NULL;
                error[0] = 0;
                copied = 0;
                if (netplay_start(NULL, 0, relay, NULL, wanted, 0, rom, romSize, err, sizeof err) == 0) {
                    connecting = hosting = 1;
                } else {
                    snprintf(error, sizeof error, "%s", err);
                }
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Join an online game");
            ImGui::InputText("Room code", code, sizeof code, ImGuiInputTextFlags_CallbackCharFilter, code_char);
            ImGui::BeginDisabled(strlen(code) != NETPLAY_ROOM_CODE);
            if (ImGui::Button("Join", ImVec2(-1, 0)) || (autoPress && !strncmp(autoPress, "join:", 5))) {
                autoPress = NULL;
                error[0] = 0;
                if (netplay_start(NULL, 0, relay, code, 0, 0, rom, romSize, err, sizeof err) == 0) {
                    connecting = 1;
                    hosting = 0;
                } else {
                    snprintf(error, sizeof error, "%s", err);
                }
            }
            ImGui::EndDisabled();
            ImGui::Separator();
            /* Each player's own choice: how many frames after a press the input counts (netplay.h). */
            ImGui::TextUnformatted("Your options");
            if (ImGui::SliderInt("Input delay", &delay, 0, 8)) {
                netplay_set_input_delay(delay);
            }
            colors_ui();
            ImGui::TextWrapped("Frames of 50 ms. More: the other gloves jump less. Less: yours answers sooner.");
            ImGui::Separator();
            if (ImGui::Button("Play alone", ImVec2(-1, 0))) {
                ImGui::End();
                ImGui::Render();
                return 0;
            }
            if (error[0]) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 120, 120, 255));
                ImGui::TextWrapped("%s", error);
                ImGui::PopStyleColor();
            }
        } else {
            const char *room = netplay_room_code();

            ImGui::TextWrapped("%s", netplay_wait_status());
            if (hosting && room[0] && !printed) {
                printf("g64coopdx: Room code: %s\n", room);
                fflush(stdout);
                printed = 1;
            }
            if (hosting && room[0]) {
                ImGui::Spacing();
                ImGui::TextUnformatted("Room code:");
                ImGui::SetWindowFontScale(2.5f);
                ImGui::TextUnformatted(room);
                ImGui::SetWindowFontScale(1.3f);
                if (ImGui::Button(copied ? "Copied" : "Copy code", ImVec2(-1, 0))) {
                    SDL_SetClipboardText(room);
                    copied = 1;
                }
                ImGui::TextWrapped("Send the code to the other players. The game starts when everyone is in.");
            }
            ImGui::Spacing();
            if (ImGui::Button("Cancel", ImVec2(-1, 0))) {
                netplay_cancel();
                connecting = 0;
            }
        }
        ImGui::End();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
        SDL_Delay(10);
    }
}
