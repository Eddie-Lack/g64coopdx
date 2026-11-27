# The build. The game comes from the copy of weemitt in weemitt/ and is built by its own Makefile, at 64 bits only.
# The game's output goes to weemitt/build/; the rest goes to build/.

VERSION ?= dev
JOBS ?= $(shell nproc 2>/dev/null || echo 4)
WEEMITT := $(MAKE) -C weemitt -j$(JOBS)

PHONY_SOUND := sound sound-build
.PHONY: $(PHONY_SOUND)
.PHONY: test-relay client game windows headless savestates savestates-test rollback-game-build rollback-game twoplayers-build twoplayers levelstart-build levelstart meshguards-build meshguards bankwalk-build bankwalk packbanks-build packbanks scenery-build scenery models-build models levcheck-build levcheck rombanks-build rombanks bots-build bots swaps-build swaps instances-build instances crossbuild-build crossbuild solosave startscreen regress server test test-c test-windows fuzz $(FUZZ_TARGETS) image packtool packtool-windows

# The online program for Linux: weemitt/build/studio64/weemitt-studio (needs SDL2). It's weemitt's studio at 64 bits
# with g64coopdx's online mode: --host PORT --players N, or --join HOST[:PORT] (docs/playing-online.md). Its sound comes
# from weemitt's library with the real sound player, next to it as g64coopdx-sound.so (src/client/soundout.h).
client:
	$(WEEMITT) studio64
	$(WEEMITT) lib64-sound
	cp weemitt/build/lib64-sound/libglover.so weemitt/build/studio64/g64coopdx-sound.so

# The game's window program for Linux: weemitt/build/linux64/weemitt (needs SDL2).
game:
	$(WEEMITT) linux64

# The game's window program for Windows: weemitt/build/windows64/weemitt.exe (needs MinGW-w64 and SDL2 for MinGW,
# weemitt/docs/building.md).
windows:
	$(WEEMITT) windows64

# The test build without a window: weemitt/build/headless64/weemitt.
headless:
	$(WEEMITT) headless64

# The brute forcer's engine with its savestates, at 64 bits: weemitt/bruteforce/build/linux64/glover and
# weemitt/bruteforce/build/windows64/glover.exe.
savestates:
	$(MAKE) -C weemitt/bruteforce -j$(JOBS) BITS=64
	$(MAKE) -C weemitt/bruteforce -j$(JOBS) windows64

# The engine's tests on both 64-bit programs, the Windows one under Wine (needs the ROM, Wine and a 64-bit prefix).
# They compare the game with the 32-bit Linux build, which needs gcc-multilib.
savestates-test: savestates
	$(MAKE) -C weemitt/bruteforce -j$(JOBS)
	cd weemitt/bruteforce && WINE=$(WINE64) WINEPREFIX=$(WINEPREFIX64) python3 tests/run.py --build build/linux64 --wine \
	    --wine-build build/windows64

# The rollback core with the real game (tests/rom/rollback_game.c), for Linux and for Windows. They link the engine
# that make savestates builds.
ENGINE_LINUX := weemitt/bruteforce/build/linux64/libglover.a
ENGINE_WINDOWS := weemitt/bruteforce/build/windows64/libglover.a
# The check of a pack's models.bin and what it needs, for every program that reads packs (src/client/pack.c). The
# table of the game's bank switch (src/client/bankranges.h) is for programs that don't link the game;
# tools/gen_bankranges.py writes it.
BANKRANGES_SRC := src/client/bankranges.c src/client/bankranges_table.c
MODELS_SRC := src/client/bankcheck.c src/client/rombanks.c src/client/fla2.c
MODELS_DEPS := src/client/bankcheck.h src/client/rombanks.h src/client/fla2.h src/client/bankranges.h
ROLLBACK_GAME_SRC := tests/rom/rollback_game.c src/rollback/engine_game.c src/rollback/rollback.c \
                     weemitt/src/port/statehash.c

rollback-game-build: savestates
	@mkdir -p build/tests
	$(CC) -m64 -std=gnu99 -O2 -g -Wall -Wextra -Werror -no-pie -o build/tests/rollback_game $(ROLLBACK_GAME_SRC) \
	    $(ENGINE_LINUX) -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/rollback_game.exe $(ROLLBACK_GAME_SRC) $(ENGINE_WINDOWS) -lm

# The online game's sound with rollbacks (tests/rom/sound.c), on Linux and under Wine; the outputs must be equal (needs
# the ROM in GLOVER_ROM).
SOUND_SRC := tests/rom/sound.c src/client/sound.c src/rollback/engine_game.c src/rollback/rollback.c \
             weemitt/src/port/statehash.c
sound-build: savestates
	@mkdir -p build/tests
	$(CC) -m64 -std=gnu99 -O2 -g -Wall -Wextra -Werror -no-pie -o build/tests/sound $(SOUND_SRC) $(ENGINE_LINUX) \
	    -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/sound.exe $(SOUND_SRC) $(ENGINE_WINDOWS) -lm

sound: sound-build
	build/tests/sound "$$GLOVER_ROM" > build/tests/sound.out
	tail -1 build/tests/sound.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/sound.exe "$$GLOVER_ROM" | tr -d '\r' \
	    > build/tests/sound-windows.out
	cmp build/tests/sound.out build/tests/sound-windows.out

# One host and three clients play level 13 over a lossy simulated network, on Linux and under Wine (needs the ROM in
# GLOVER_ROM).
rollback-game: rollback-game-build
	build/tests/rollback_game "$$GLOVER_ROM" 600 > build/tests/rollback_game.out || \
	    { cat build/tests/rollback_game.out; exit 1; }
	cat build/tests/rollback_game.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/rollback_game.exe "$$GLOVER_ROM" 600 | \
	    tr -d '\r' > build/tests/rollback_game-windows.out
	cat build/tests/rollback_game-windows.out
	grep -q 'offline run' build/tests/rollback_game.out
	grep -c '^FAIL' build/tests/rollback_game-windows.out | grep -qx 0
	grep 'offline run' build/tests/rollback_game.out > build/tests/game-hash-linux.txt
	grep 'offline run' build/tests/rollback_game-windows.out > build/tests/game-hash-windows.txt
	cmp build/tests/game-hash-linux.txt build/tests/game-hash-windows.txt

# Two to four players in one game (tests/rom/twoplayers.c), players who join and leave it (tests/rom/members.c), the pause menu's choices
# (tests/rom/pause.c), each player's crystal scene in the cave (tests/rom/cave.c) and
# exits in the online game (tests/rom/exits.c), enemies, puzzles, respawns, world hazards, garibs, pick-ups, bosses and bonus levels with more than one player
# (tests/rom/enemies.c, tests/rom/puzzles.c, tests/rom/respawn.c, tests/rom/hazards.c, tests/rom/collect.c, tests/rom/bosses.c, tests/rom/bonus.c), the look-around mode (tests/rom/lookaround.c), the mouse's camera and clicks (tests/rom/mouselook.c), the frame's limits with 16 players (tests/rom/limits.c), tips (tests/rom/tips.c), a cameo that a player sets off (tests/rom/cameos.c), each player's ball texture (tests/rom/textures.c), each player's glove and ball models (tests/rom/playermodels.c) and the host's "any ball" rule (tests/rom/anyball.c), on Linux and under Wine; the outputs must be equal (needs the ROM).
TWOPLAYERS_SRC := tests/rom/twoplayers.c weemitt/src/port/statehash.c
MEMBERS_SRC := tests/rom/members.c weemitt/src/port/statehash.c
PAUSE_SRC := tests/rom/pause.c weemitt/src/port/statehash.c
CAVE_SRC := tests/rom/cave.c weemitt/src/port/statehash.c
EXITS_SRC := tests/rom/exits.c weemitt/src/port/statehash.c
ENEMIES_SRC := tests/rom/enemies.c weemitt/src/port/statehash.c
PUZZLES_SRC := tests/rom/puzzles.c weemitt/src/port/statehash.c
RESPAWN_SRC := tests/rom/respawn.c weemitt/src/port/statehash.c
HAZARDS_SRC := tests/rom/hazards.c weemitt/src/port/statehash.c
COLLECT_SRC := tests/rom/collect.c weemitt/src/port/statehash.c
BOSSES_SRC := tests/rom/bosses.c weemitt/src/port/statehash.c
BONUS_SRC := tests/rom/bonus.c weemitt/src/port/statehash.c
LOOKAROUND_SRC := tests/rom/lookaround.c weemitt/src/port/statehash.c
LIMITS_SRC := tests/rom/limits.c weemitt/src/port/statehash.c
TIPS_SRC := tests/rom/tips.c weemitt/src/port/statehash.c
CAMEOS_SRC := tests/rom/cameos.c weemitt/src/port/statehash.c
TEXTURES_SRC := tests/rom/textures.c weemitt/src/port/statehash.c
PLAYERMODELS_SRC := tests/rom/playermodels.c weemitt/src/port/statehash.c
ANYBALL_SRC := tests/rom/anyball.c weemitt/src/port/statehash.c
MOUSELOOK_ROM_SRC := tests/rom/mouselook.c weemitt/src/port/mouselook.c weemitt/src/port/statehash.c
twoplayers-build: savestates
	@mkdir -p build/tests
	$(CC) -m64 -std=gnu99 -O2 -Wall -Wextra -Werror -no-pie -o build/tests/twoplayers $(TWOPLAYERS_SRC) $(ENGINE_LINUX) \
	    -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/twoplayers.exe $(TWOPLAYERS_SRC) $(ENGINE_WINDOWS) -lm
	$(CC) -m64 -std=gnu99 -O2 -Wall -Wextra -Werror -no-pie -o build/tests/members $(MEMBERS_SRC) $(ENGINE_LINUX) \
	    -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/members.exe $(MEMBERS_SRC) $(ENGINE_WINDOWS) -lm
	$(CC) -m64 -std=gnu99 -O2 -Wall -Wextra -Werror -no-pie -o build/tests/exits $(EXITS_SRC) $(ENGINE_LINUX) \
	    -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/exits.exe $(EXITS_SRC) $(ENGINE_WINDOWS) -lm
	$(CC) -m64 -std=gnu99 -O2 -Wall -Wextra -Werror -no-pie -o build/tests/pause $(PAUSE_SRC) $(ENGINE_LINUX) \
	    -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/pause.exe $(PAUSE_SRC) $(ENGINE_WINDOWS) -lm
	$(CC) -m64 -std=gnu99 -O2 -Wall -Wextra -Werror -no-pie -o build/tests/cave $(CAVE_SRC) $(ENGINE_LINUX) \
	    -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/cave.exe $(CAVE_SRC) $(ENGINE_WINDOWS) -lm
	$(CC) -m64 -std=gnu99 -O2 -Wall -Wextra -Werror -no-pie -o build/tests/enemies $(ENEMIES_SRC) $(ENGINE_LINUX) \
	    -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/enemies.exe $(ENEMIES_SRC) $(ENGINE_WINDOWS) -lm
	$(CC) -m64 -std=gnu99 -O2 -Wall -Wextra -Werror -no-pie -o build/tests/puzzles $(PUZZLES_SRC) $(ENGINE_LINUX) \
	    -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/puzzles.exe $(PUZZLES_SRC) $(ENGINE_WINDOWS) -lm
	$(CC) -m64 -std=gnu99 -O2 -Wall -Wextra -Werror -no-pie -o build/tests/respawn $(RESPAWN_SRC) $(ENGINE_LINUX) \
	    -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/respawn.exe $(RESPAWN_SRC) $(ENGINE_WINDOWS) -lm
	$(CC) -m64 -std=gnu99 -O2 -Wall -Wextra -Werror -no-pie -o build/tests/hazards $(HAZARDS_SRC) $(ENGINE_LINUX) \
	    -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/hazards.exe $(HAZARDS_SRC) $(ENGINE_WINDOWS) -lm
	$(CC) -m64 -std=gnu99 -O2 -Wall -Wextra -Werror -no-pie -o build/tests/collect $(COLLECT_SRC) $(ENGINE_LINUX) \
	    -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/collect.exe $(COLLECT_SRC) $(ENGINE_WINDOWS) -lm
	$(CC) -m64 -std=gnu99 -O2 -Wall -Wextra -Werror -no-pie -o build/tests/bosses $(BOSSES_SRC) $(ENGINE_LINUX) \
	    -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/bosses.exe $(BOSSES_SRC) $(ENGINE_WINDOWS) -lm
	$(CC) -m64 -std=gnu99 -O2 -Wall -Wextra -Werror -no-pie -o build/tests/bonus $(BONUS_SRC) $(ENGINE_LINUX) \
	    -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/bonus.exe $(BONUS_SRC) $(ENGINE_WINDOWS) -lm
	$(CC) -m64 -std=gnu99 -O2 -Wall -Wextra -Werror -no-pie -o build/tests/lookaround $(LOOKAROUND_SRC) $(ENGINE_LINUX) \
	    -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/lookaround.exe $(LOOKAROUND_SRC) $(ENGINE_WINDOWS) -lm
	$(CC) -m64 -std=gnu99 -O2 -Wall -Wextra -Werror -no-pie -o build/tests/limits $(LIMITS_SRC) $(ENGINE_LINUX) \
	    -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/limits.exe $(LIMITS_SRC) $(ENGINE_WINDOWS) -lm
	$(CC) -m64 -std=gnu99 -O2 -Wall -Wextra -Werror -no-pie -o build/tests/tips $(TIPS_SRC) $(ENGINE_LINUX) \
	    -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/tips.exe $(TIPS_SRC) $(ENGINE_WINDOWS) -lm
	$(CC) -m64 -std=gnu99 -O2 -Wall -Wextra -Werror -no-pie -o build/tests/cameos $(CAMEOS_SRC) $(ENGINE_LINUX) \
	    -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/cameos.exe $(CAMEOS_SRC) $(ENGINE_WINDOWS) -lm
	$(CC) -m64 -std=gnu99 -O2 -Wall -Wextra -Werror -no-pie -o build/tests/textures $(TEXTURES_SRC) $(ENGINE_LINUX) \
	    -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/textures.exe $(TEXTURES_SRC) $(ENGINE_WINDOWS) -lm
	$(CC) -m64 -std=gnu99 -O2 -Wall -Wextra -Werror -no-pie -o build/tests/playermodels $(PLAYERMODELS_SRC) \
	    $(ENGINE_LINUX) -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/playermodels.exe $(PLAYERMODELS_SRC) $(ENGINE_WINDOWS) -lm
	$(CC) -m64 -std=gnu99 -O2 -Wall -Wextra -Werror -no-pie -o build/tests/anyball $(ANYBALL_SRC) $(ENGINE_LINUX) \
	    -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/anyball.exe $(ANYBALL_SRC) $(ENGINE_WINDOWS) -lm
	$(CC) -m64 -std=gnu99 -O2 -Wall -Wextra -Werror -no-pie -o build/tests/mouselook $(MOUSELOOK_ROM_SRC) \
	    $(ENGINE_LINUX) -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/mouselook.exe $(MOUSELOOK_ROM_SRC) $(ENGINE_WINDOWS) -lm

twoplayers: twoplayers-build
	build/tests/twoplayers "$$GLOVER_ROM" 3000 4 > build/tests/twoplayers.out
	tail -2 build/tests/twoplayers.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/twoplayers.exe "$$GLOVER_ROM" 3000 4 | tr -d '\r' \
	    > build/tests/twoplayers-windows.out
	cmp build/tests/twoplayers.out build/tests/twoplayers-windows.out
	build/tests/members "$$GLOVER_ROM" > build/tests/members.out
	tail -1 build/tests/members.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/members.exe "$$GLOVER_ROM" | tr -d '\r' \
	    > build/tests/members-windows.out
	cmp build/tests/members.out build/tests/members-windows.out
	build/tests/pause "$$GLOVER_ROM" > build/tests/pause.out
	tail -1 build/tests/pause.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/pause.exe "$$GLOVER_ROM" | tr -d '\r' \
	    > build/tests/pause-windows.out
	cmp build/tests/pause.out build/tests/pause-windows.out
	build/tests/cave "$$GLOVER_ROM" > build/tests/cave.out
	tail -1 build/tests/cave.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/cave.exe "$$GLOVER_ROM" | tr -d '\r' \
	    > build/tests/cave-windows.out
	cmp build/tests/cave.out build/tests/cave-windows.out
	build/tests/exits "$$GLOVER_ROM" > build/tests/exits.out
	tail -1 build/tests/exits.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/exits.exe "$$GLOVER_ROM" | tr -d '\r' \
	    > build/tests/exits-windows.out
	cmp build/tests/exits.out build/tests/exits-windows.out
	build/tests/enemies "$$GLOVER_ROM" > build/tests/enemies.out
	tail -1 build/tests/enemies.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/enemies.exe "$$GLOVER_ROM" | tr -d '\r' \
	    > build/tests/enemies-windows.out
	cmp build/tests/enemies.out build/tests/enemies-windows.out
	build/tests/puzzles "$$GLOVER_ROM" > build/tests/puzzles.out
	tail -1 build/tests/puzzles.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/puzzles.exe "$$GLOVER_ROM" | tr -d '\r' \
	    > build/tests/puzzles-windows.out
	cmp build/tests/puzzles.out build/tests/puzzles-windows.out
	build/tests/respawn "$$GLOVER_ROM" > build/tests/respawn.out
	tail -1 build/tests/respawn.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/respawn.exe "$$GLOVER_ROM" | tr -d '\r' \
	    > build/tests/respawn-windows.out
	cmp build/tests/respawn.out build/tests/respawn-windows.out
	build/tests/hazards "$$GLOVER_ROM" > build/tests/hazards.out
	tail -1 build/tests/hazards.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/hazards.exe "$$GLOVER_ROM" | tr -d '\r' \
	    > build/tests/hazards-windows.out
	cmp build/tests/hazards.out build/tests/hazards-windows.out
	build/tests/collect "$$GLOVER_ROM" > build/tests/collect.out
	tail -1 build/tests/collect.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/collect.exe "$$GLOVER_ROM" | tr -d '\r' \
	    > build/tests/collect-windows.out
	cmp build/tests/collect.out build/tests/collect-windows.out
	build/tests/bosses "$$GLOVER_ROM" > build/tests/bosses.out
	tail -1 build/tests/bosses.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/bosses.exe "$$GLOVER_ROM" | tr -d '\r' \
	    > build/tests/bosses-windows.out
	cmp build/tests/bosses.out build/tests/bosses-windows.out
	build/tests/bonus "$$GLOVER_ROM" > build/tests/bonus.out
	tail -1 build/tests/bonus.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/bonus.exe "$$GLOVER_ROM" | tr -d '\r' \
	    > build/tests/bonus-windows.out
	cmp build/tests/bonus.out build/tests/bonus-windows.out
	build/tests/lookaround "$$GLOVER_ROM" > build/tests/lookaround.out
	tail -1 build/tests/lookaround.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/lookaround.exe "$$GLOVER_ROM" | tr -d '\r' \
	    > build/tests/lookaround-windows.out
	cmp build/tests/lookaround.out build/tests/lookaround-windows.out
	build/tests/mouselook "$$GLOVER_ROM" > build/tests/mouselook.out
	tail -1 build/tests/mouselook.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/mouselook.exe "$$GLOVER_ROM" | tr -d '\r' \
	    > build/tests/mouselook-windows.out
	cmp build/tests/mouselook.out build/tests/mouselook-windows.out
	build/tests/limits "$$GLOVER_ROM" > build/tests/limits.out
	tail -1 build/tests/limits.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/limits.exe "$$GLOVER_ROM" | tr -d '\r' \
	    > build/tests/limits-windows.out
	cmp build/tests/limits.out build/tests/limits-windows.out
	build/tests/tips "$$GLOVER_ROM" > build/tests/tips.out
	tail -1 build/tests/tips.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/tips.exe "$$GLOVER_ROM" | tr -d '\r' \
	    > build/tests/tips-windows.out
	cmp build/tests/tips.out build/tests/tips-windows.out
	build/tests/cameos "$$GLOVER_ROM" > build/tests/cameos.out
	tail -1 build/tests/cameos.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/cameos.exe "$$GLOVER_ROM" | tr -d '\r' \
	    > build/tests/cameos-windows.out
	cmp build/tests/cameos.out build/tests/cameos-windows.out
	build/tests/textures "$$GLOVER_ROM" > build/tests/textures.out
	tail -1 build/tests/textures.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/textures.exe "$$GLOVER_ROM" | tr -d '\r' \
	    > build/tests/textures-windows.out
	cmp build/tests/textures.out build/tests/textures-windows.out
	build/tests/playermodels "$$GLOVER_ROM" > build/tests/playermodels.out
	tail -1 build/tests/playermodels.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/playermodels.exe "$$GLOVER_ROM" | tr -d '\r' \
	    > build/tests/playermodels-windows.out
	cmp build/tests/playermodels.out build/tests/playermodels-windows.out
	build/tests/anyball "$$GLOVER_ROM" > build/tests/anyball.out
	tail -1 build/tests/anyball.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/anyball.exe "$$GLOVER_ROM" | tr -d '\r' \
	    > build/tests/anyball-windows.out
	cmp build/tests/anyball.out build/tests/anyball-windows.out

# The level file check against the game's own parser, on every level of the ROM, on Linux and under Wine
# (tests/rom/levcheck.c); the two outputs must be equal (needs the ROM in GLOVER_ROM).
LEVCHECK_SRC := tests/rom/levcheck.c src/client/levcheck.c src/client/pack.c src/client/packrom.c $(MODELS_SRC) src/client/rombanks_game.c \
    $(BANKRANGES_SRC) third_party/monocypher/monocypher.c \
    weemitt/src/port/statehash.c
levcheck-build: savestates
	@mkdir -p build/tests
	$(CC) -m64 -std=gnu99 -O2 -Wall -Wextra -Werror -no-pie -o build/tests/levcheck $(LEVCHECK_SRC) $(ENGINE_LINUX) \
	    -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/levcheck.exe $(LEVCHECK_SRC) $(ENGINE_WINDOWS) -lm

levcheck: levcheck-build
	build/tests/levcheck "$$GLOVER_ROM" > build/tests/levcheck.out || { cat build/tests/levcheck.out; exit 1; }
	tail -1 build/tests/levcheck.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/levcheck.exe "$$GLOVER_ROM" | tr -d '\r' \
	    > build/tests/levcheck-windows.out
	cmp build/tests/levcheck.out build/tests/levcheck-windows.out

# The index of the ROM's banks and the bounded FLA2 reader against the game's loaders and decompressor, on every bank
# of the ROM, on Linux and under Wine (tests/rom/rombanks.c); the two outputs must be equal (needs the ROM in
# GLOVER_ROM). The time the index took goes to stderr.
ROMBANKS_SRC := tests/rom/rombanks.c src/client/rombanks.c src/client/rombanks_game.c src/client/fla2.c $(BANKRANGES_SRC) \
    third_party/monocypher/monocypher.c weemitt/src/port/statehash.c
rombanks-build: savestates
	@mkdir -p build/tests
	$(CC) -m64 -std=gnu99 -O2 -Wall -Wextra -Werror -no-pie -o build/tests/rombanks $(ROMBANKS_SRC) $(ENGINE_LINUX) \
	    -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/rombanks.exe $(ROMBANKS_SRC) $(ENGINE_WINDOWS) -lm

rombanks: rombanks-build
	build/tests/rombanks "$$GLOVER_ROM" > build/tests/rombanks.out || { cat build/tests/rombanks.out; exit 1; }
	tail -1 build/tests/rombanks.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/rombanks.exe "$$GLOVER_ROM" \
	    > build/tests/rombanks-windows.raw || { tr -d '\r' < build/tests/rombanks-windows.raw; exit 1; }
	tr -d '\r' < build/tests/rombanks-windows.raw > build/tests/rombanks-windows.out
	cmp build/tests/rombanks.out build/tests/rombanks-windows.out

# Every level built from the power-on state, twice, on Linux and under Wine (tests/rom/levelstart.c); the two outputs
# must be equal (needs the ROM in GLOVER_ROM).
LEVELSTART_SRC := tests/rom/levelstart.c weemitt/src/port/statehash.c
levelstart-build: savestates
	@mkdir -p build/tests
	$(CC) -m64 -std=gnu99 -O2 -Wall -Wextra -Werror -no-pie -o build/tests/levelstart $(LEVELSTART_SRC) $(ENGINE_LINUX) \
	    -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/levelstart.exe $(LEVELSTART_SRC) $(ENGINE_WINDOWS) -lm

levelstart: levelstart-build
	build/tests/levelstart "$$GLOVER_ROM" > build/tests/levelstart.out
	tail -1 build/tests/levelstart.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/levelstart.exe "$$GLOVER_ROM" | tr -d '\r' \
	    > build/tests/levelstart-windows.out
	cmp build/tests/levelstart.out build/tests/levelstart-windows.out

# The game with meshes that have no texture coordinates and no vertex colors, as a pack's meshes have none, on Linux
# and under Wine (tests/rom/meshguards.c); the two outputs must be equal (needs the ROM in GLOVER_ROM).
MESHGUARDS_SRC := tests/rom/meshguards.c weemitt/src/port/statehash.c
meshguards-build: savestates
	@mkdir -p build/tests
	$(CC) -m64 -std=gnu99 -O2 -Wall -Wextra -Werror -no-pie -o build/tests/meshguards $(MESHGUARDS_SRC) $(ENGINE_LINUX) \
	    -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/meshguards.exe $(MESHGUARDS_SRC) $(ENGINE_WINDOWS) -lm

meshguards: meshguards-build
	build/tests/meshguards "$$GLOVER_ROM" > build/tests/meshguards.out
	cat build/tests/meshguards.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/meshguards.exe "$$GLOVER_ROM" | tr -d '\r' \
	    > build/tests/meshguards-windows.out
	cmp build/tests/meshguards.out build/tests/meshguards-windows.out

# A level pack's own banks loaded by the real game (tests/rom/bankwalk.c): the words the game's loaders change are the
# words the bank check's shadow walk changes, the banks play as scenery, the heap with 16 players, and the collision
# planes' limits, on Linux and under Wine; the two outputs must be equal (needs the ROM in GLOVER_ROM).
BANKWALK_SRC := tests/rom/bankwalk.c tests/fuzz/bankwalk.c src/client/bankcheck.c src/client/rombanks.c \
    src/client/fla2.c src/client/levcheck.c third_party/monocypher/monocypher.c weemitt/src/port/statehash.c
bankwalk-build: savestates
	@mkdir -p build/tests
	$(CC) -m64 -std=gnu99 -O2 -Wall -Wextra -Werror -no-pie -o build/tests/bankwalk $(BANKWALK_SRC) $(ENGINE_LINUX) \
	    -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/bankwalk.exe $(BANKWALK_SRC) $(ENGINE_WINDOWS) -lm

bankwalk: bankwalk-build
	build/tests/bankwalk "$$GLOVER_ROM" > build/tests/bankwalk.out || { cat build/tests/bankwalk.out; exit 1; }
	grep -E '^(FAIL|heap:|the largest)' build/tests/bankwalk.out || true
	tail -1 build/tests/bankwalk.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/bankwalk.exe "$$GLOVER_ROM" \
	    > build/tests/bankwalk-windows.raw || { tr -d '\r' < build/tests/bankwalk-windows.raw; exit 1; }
	tr -d '\r' < build/tests/bankwalk-windows.raw > build/tests/bankwalk-windows.out
	cmp build/tests/bankwalk.out build/tests/bankwalk-windows.out

# Retail banks that a level pack names, loaded into the game's table (tests/rom/packbanks.c): each bank's cost, every
# allowed object bank 4 at a time in every level with 16 players, the costliest banks, retail objects as scenery, the
# ball's and the hand's names, save and load, and the end of the level, on Linux and under Wine; the two outputs must
# be equal (needs the ROM in GLOVER_ROM).
PACKBANKS_SRC := tests/rom/packbanks.c src/client/levcheck.c src/client/pack.c src/client/packrom.c $(MODELS_SRC) \
    $(BANKRANGES_SRC) src/client/rombanks_game.c third_party/monocypher/monocypher.c weemitt/src/port/statehash.c
packbanks-build: savestates
	@mkdir -p build/tests
	$(CC) -m64 -std=gnu99 -O2 -Wall -Wextra -Werror -no-pie -o build/tests/packbanks $(PACKBANKS_SRC) $(ENGINE_LINUX) \
	    -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/packbanks.exe $(PACKBANKS_SRC) $(ENGINE_WINDOWS) -lm

packbanks: packbanks-build
	build/tests/packbanks "$$GLOVER_ROM" > build/tests/packbanks.out || { cat build/tests/packbanks.out; exit 1; }
	grep -E '^(FAIL|every level|the costliest)' build/tests/packbanks.out || true
	tail -1 build/tests/packbanks.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/packbanks.exe "$$GLOVER_ROM" \
	    > build/tests/packbanks-windows.raw || { tr -d '\r' < build/tests/packbanks-windows.raw; exit 1; }
	tr -d '\r' < build/tests/packbanks-windows.raw > build/tests/packbanks-windows.out
	cmp build/tests/packbanks.out build/tests/packbanks-windows.out

# Many scenery objects in one level file (tests/rom/scenery.c): the case from docs/discovery/model-banks.md (object
# bank 29 in level 35), also with 16 players; each of 9 levels with its objects in turn and with one object again and
# again up to the parser's limit; and every level with an unknown object. The level plays or stops as unplayable, on
# Linux and under Wine. The two outputs must be equal (needs the ROM in GLOVER_ROM).
SCENERY_SRC := tests/rom/scenery.c src/client/levcheck.c src/client/rombanks.c src/client/fla2.c \
    src/client/rombanks_game.c $(BANKRANGES_SRC) third_party/monocypher/monocypher.c weemitt/src/port/statehash.c
scenery-build: savestates
	@mkdir -p build/tests
	$(CC) -m64 -std=gnu99 -O2 -Wall -Wextra -Werror -no-pie -o build/tests/scenery $(SCENERY_SRC) $(ENGINE_LINUX) \
	    -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/scenery.exe $(SCENERY_SRC) $(ENGINE_WINDOWS) -lm

scenery: scenery-build
	build/tests/scenery "$$GLOVER_ROM" > build/tests/scenery.out || { cat build/tests/scenery.out; exit 1; }
	grep -E '^FAIL' build/tests/scenery.out || true
	tail -2 build/tests/scenery.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/scenery.exe "$$GLOVER_ROM" \
	    > build/tests/scenery-windows.raw || { tr -d '\r' < build/tests/scenery-windows.raw; exit 1; }
	tr -d '\r' < build/tests/scenery-windows.raw > build/tests/scenery-windows.out
	cmp build/tests/scenery.out build/tests/scenery-windows.out

# May a pack's own models be platforms (tests/rom/models.c)? Platforms, bridges, debris and water with the pack's
# models, on Linux and under Wine. Variant I has identity rotation keys: its two outputs must be equal. Variant Z has
# keys of all zeros, past the bank check. It's a measurement: its exit statuses and the comparison are printed, and
# don't fail the target (needs the ROM in GLOVER_ROM).
MODELTEST_SRC := tests/rom/models.c src/client/bankcheck.c src/client/rombanks.c src/client/fla2.c src/client/levcheck.c \
    third_party/monocypher/monocypher.c weemitt/src/port/statehash.c
models-build: savestates
	@mkdir -p build/tests
	$(CC) -m64 -std=gnu99 -O2 -Wall -Wextra -Werror -no-pie -o build/tests/models $(MODELTEST_SRC) $(ENGINE_LINUX) \
	    -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/models.exe $(MODELTEST_SRC) $(ENGINE_WINDOWS) -lm

models: models-build
	build/tests/models "$$GLOVER_ROM" I > build/tests/models-I.out || { cat build/tests/models-I.out; exit 1; }
	grep -E '^(FAIL|ok   variant)' build/tests/models-I.out || true
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/models.exe "$$GLOVER_ROM" I \
	    > build/tests/models-I-windows.raw || { tr -d '\r' < build/tests/models-I-windows.raw; exit 1; }
	tr -d '\r' < build/tests/models-I-windows.raw > build/tests/models-I-windows.out
	cmp build/tests/models-I.out build/tests/models-I-windows.out
	@echo "variant I: Linux and Windows give the same output"
	build/tests/models "$$GLOVER_ROM" Z > build/tests/models-Z.out; echo "variant Z: the Linux program exits with $$?"
	grep -E '^(FAIL|ok   variant)' build/tests/models-Z.out || true
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/models.exe "$$GLOVER_ROM" Z \
	    > build/tests/models-Z-windows.raw; echo "variant Z: the Windows program exits with $$?"
	tr -d '\r' < build/tests/models-Z-windows.raw > build/tests/models-Z-windows.out
	@if cmp -s build/tests/models-Z.out build/tests/models-Z-windows.out; then \
	    echo "variant Z (a measurement): Linux and Windows give the same output"; \
	else echo "variant Z (a measurement): Linux and Windows differ"; fi

SWAPS_SRC := tests/rom/swaps.c src/client/levcheck.c src/client/pack.c $(MODELS_SRC) third_party/monocypher/monocypher.c \
    weemitt/src/port/statehash.c
swaps-build: savestates
	@mkdir -p build/tests
	$(CC) -m64 -std=gnu99 -O2 -g -Wall -Wextra -Werror -no-pie -o build/tests/swaps $(SWAPS_SRC) $(ENGINE_LINUX) \
	    -lm -lrt -lpthread

# Every level with every other level's file in its place: the game plays it or stops it, and never crashes (Linux).
swaps: swaps-build
	build/tests/swaps "$$GLOVER_ROM" 1500 > build/tests/swaps.out || { grep FAIL build/tests/swaps.out; exit 1; }
	tail -1 build/tests/swaps.out

# Every level with 8 and with 16 players who press buttons and push sticks at random, and with 8 under the host's
# "any ball" rule (tests/rom/bots.c), on Linux and under Wine; the outputs must be equal (needs the ROM in GLOVER_ROM).
BOTS_SRC := tests/rom/bots.c weemitt/src/port/statehash.c
# Level 14 from frame 0 with seeds that once crashed the game: two players' frog tongues shared one bullet. Each run
# is PLAYERS,FRAMES and its words, split at the commas.
BOTS_TONGUES := 8,1000,level=14,seed=1,quiet=0 8,1000,level=14,seed=3,quiet=0 16,600,level=14,seed=777,quiet=0
bots-build: savestates
	@mkdir -p build/tests
	$(CC) -m64 -std=gnu99 -O2 -Wall -Wextra -Werror -no-pie -o build/tests/bots $(BOTS_SRC) $(ENGINE_LINUX) \
	    -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/bots.exe $(BOTS_SRC) $(ENGINE_WINDOWS) -lm

bots: bots-build
	build/tests/bots "$$GLOVER_ROM" 8 1500 > build/tests/bots.out
	build/tests/bots "$$GLOVER_ROM" 16 1500 >> build/tests/bots.out
	build/tests/bots "$$GLOVER_ROM" 8 1500 any >> build/tests/bots.out
	for run in $(BOTS_TONGUES); do build/tests/bots "$$GLOVER_ROM" $$(echo $$run | tr , ' ') >> build/tests/bots.out \
	    || exit 1; done
	grep "every level\|level 14 runs" build/tests/bots.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/bots.exe "$$GLOVER_ROM" 8 1500 | tr -d '\r' \
	    > build/tests/bots-windows.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/bots.exe "$$GLOVER_ROM" 16 1500 | tr -d '\r' \
	    >> build/tests/bots-windows.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/bots.exe "$$GLOVER_ROM" 8 1500 any | tr -d '\r' \
	    >> build/tests/bots-windows.out
	for run in $(BOTS_TONGUES); do WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/bots.exe \
	    "$$GLOVER_ROM" $$(echo $$run | tr , ' ') | tr -d '\r' >> build/tests/bots-windows.out; done
	cmp build/tests/bots.out build/tests/bots-windows.out

# Players moving between instances of the real game (tests/rom/instances.c), on Linux and under Wine; the outputs must
# be equal (needs the ROM in GLOVER_ROM).
INSTANCES_SRC := tests/rom/instances.c src/client/instances.c src/client/imsg.c src/client/freecam.c src/rollback/engine_game.c \
                 src/rollback/rollback.c src/client/levcheck.c src/client/pack.c src/client/packrom.c $(MODELS_SRC) \
                 $(BANKRANGES_SRC) src/client/rombanks_game.c third_party/monocypher/monocypher.c \
                 weemitt/src/port/statehash.c
instances-build: savestates
	@mkdir -p build/tests
	$(CC) -m64 -std=gnu99 -O2 -Wall -Wextra -Werror -no-pie -o build/tests/instances $(INSTANCES_SRC) $(ENGINE_LINUX) \
	    -lm -lrt -lpthread
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section \
	    -o build/tests/instances.exe $(INSTANCES_SRC) $(ENGINE_WINDOWS) -lm

instances: instances-build
	build/tests/instances "$$GLOVER_ROM" > build/tests/instances.out
	tail -1 build/tests/instances.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/instances.exe "$$GLOVER_ROM" | tr -d '\r' \
	    > build/tests/instances-windows.out
	cmp build/tests/instances.out build/tests/instances-windows.out

# What tests/rom/crossbuild.py runs: the Linux headless build, and the Windows library with its test host.
crossbuild-build: headless
	$(WEEMITT) lib-windows64-stub
	$(WEEMITT) libhost-windows64

# The Linux and Windows builds give the same state and memory hash every frame (needs the ROM and Wine).
crossbuild: crossbuild-build
	python3 tests/rom/crossbuild.py

# SOLO keeps its save: the online program plays alone, saves, quits and starts again, on Linux and under Wine. It
# runs in a window under Xvfb (needs the ROM and xvfb-run).
solosave: client
	$(WEEMITT) studio-windows64
	cp weemitt/deps/SDL2-mingw/x86_64-w64-mingw32/bin/SDL2.dll weemitt/build/studio-windows64/
	python3 tests/rom/solosave.py --exe weemitt/build/studio64/weemitt-studio
	WINEPREFIX=$(WINEPREFIX64) python3 tests/rom/solosave.py --exe weemitt/build/studio-windows64/weemitt-studio.exe \
	    --wine $(WINE64)

# The online program from its start screen, under Xvfb, through a lobby on this machine: the wait pages' lines, chat
# typed in a game that the start screen started, a removed player, and a host who leaves (needs the ROM, Xvfb and
# xdotool). Linux only.
startscreen: client server
	python3 tests/rom/startscreen.py --exe weemitt/build/studio64/weemitt-studio --lobby build/lobby

# The game's logic against g64coopdx's baseline (needs the ROM). The first run records the baseline from the commit
# that BASELINE_REF in weemitt/tools/regress.py names.
regress:
	cd weemitt && { test -d build/regress/baseline || python3 tools/regress.py baseline; } && python3 tools/regress.py check

# The lobby server: build/lobby
server:
	cd server && CGO_ENABLED=0 go build -trimpath -ldflags "-X main.version=$(VERSION)" -o ../build/lobby .

# g64coopdx's own C: the rollback core with the toy game. The Linux test build uses the sanitizers.
CC ?= cc
CFLAGS_TEST := -std=c99 -O1 -g -Wall -Wextra -Werror
SANITIZE := -fsanitize=address,undefined -fno-sanitize-recover=all
ROLLBACK_SRC := src/rollback/rollback.c tests/toy/toy.c
ROLLBACK_DEPS := $(ROLLBACK_SRC) src/rollback/rollback.h tests/toy/toy.h
MINGW64 ?= x86_64-w64-mingw32-gcc
WINE64 ?= $(shell command -v wine64 || command -v wine)
# A 64-bit Wine prefix, as weemitt/docs/building.md uses.
WINEPREFIX64 ?= $(HOME)/.wine64
FUZZ_TIME ?= 30

build/tests/loopback: tests/net/loopback.c $(ROLLBACK_DEPS)
	@mkdir -p build/tests
	$(CC) $(CFLAGS_TEST) $(SANITIZE) -o $@ tests/net/loopback.c $(ROLLBACK_SRC)

# The mouse and the direction keys as a controller, without the game (tests/net/mouselook_test.c).
MOUSELOOK_SRC := weemitt/src/port/mouselook.c
build/tests/mouselook_test: tests/net/mouselook_test.c $(MOUSELOOK_SRC) weemitt/src/port/mouselook.h
	@mkdir -p build/tests
	$(CC) $(CFLAGS_TEST) $(SANITIZE) -o $@ tests/net/mouselook_test.c $(MOUSELOOK_SRC)

build/tests/mouselook_test.exe: tests/net/mouselook_test.c $(MOUSELOOK_SRC) weemitt/src/port/mouselook.h
	@mkdir -p build/tests
	$(MINGW64) $(CFLAGS_TEST) -static -o $@ tests/net/mouselook_test.c $(MOUSELOOK_SRC)

# The free camera, without the game (tests/net/freecam_test.c).
build/tests/freecam_test: tests/net/freecam_test.c src/client/freecam.c src/client/freecam.h
	@mkdir -p build/tests
	$(CC) $(CFLAGS_TEST) $(SANITIZE) -o $@ tests/net/freecam_test.c src/client/freecam.c -lm

build/tests/freecam_test.exe: tests/net/freecam_test.c src/client/freecam.c src/client/freecam.h
	@mkdir -p build/tests
	$(MINGW64) $(CFLAGS_TEST) -static -o $@ tests/net/freecam_test.c src/client/freecam.c -lm

# The sound reader against the sound table, without the game (tests/net/sound_test.c).
build/tests/sound_test: tests/net/sound_test.c src/client/sound.c src/client/sound.h weemitt/src/port/soundtable.c \
                        weemitt/src/port/soundtable.h
	@mkdir -p build/tests
	$(CC) $(CFLAGS_TEST) -std=gnu99 $(SANITIZE) -o $@ tests/net/sound_test.c src/client/sound.c \
	    weemitt/src/port/soundtable.c -lm

NET_SRC := src/net/udp.c
# The session, its sealed messages, and the cryptography library they use (third_party/monocypher).
SEAL_SRC := src/session/seal.c third_party/monocypher/monocypher.c
SESSION_SRC := src/session/session.c $(SEAL_SRC)
SEAL_DEPS := src/session/seal.h third_party/monocypher/monocypher.h
build/tests/seal_test: tests/net/seal_test.c $(SEAL_SRC) $(SEAL_DEPS)
	@mkdir -p build/tests
	$(CC) $(CFLAGS_TEST) -std=gnu99 $(SANITIZE) -o $@ tests/net/seal_test.c $(SEAL_SRC)

build/tests/seal_test.exe: tests/net/seal_test.c $(SEAL_SRC) $(SEAL_DEPS)
	@mkdir -p build/tests
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -o $@ tests/net/seal_test.c $(SEAL_SRC) -lbcrypt

# The level pack reader (src/client/pack.h), with the hash it takes from the cryptography library.
PACK_SRC := src/client/pack.c src/client/packrom.c src/client/levcheck.c $(MODELS_SRC) $(BANKRANGES_SRC) third_party/monocypher/monocypher.c
PACK_DEPS := src/client/pack.h src/client/packrom.h src/client/levcheck.h $(MODELS_DEPS) third_party/monocypher/monocypher.h
build/tests/pack_test: tests/net/pack_test.c $(PACK_SRC) $(PACK_DEPS)
	@mkdir -p build/tests
	$(CC) $(CFLAGS_TEST) -std=gnu99 $(SANITIZE) -o $@ tests/net/pack_test.c $(PACK_SRC) -lm

build/tests/pack_test.exe: tests/net/pack_test.c $(PACK_SRC) $(PACK_DEPS)
	@mkdir -p build/tests
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -o $@ tests/net/pack_test.c $(PACK_SRC) -lm

# A pack with models from a host to a joiner through the session (tests/net/packsend_test.c). The pack reader's
# list holds monocypher already, so the session's part is listed without it.
PACKSEND_SRC := tests/net/packsend_test.c $(PACK_SRC) src/session/session.c src/session/seal.c $(NET_SRC)
PACKSEND_DEPS := $(PACK_DEPS) src/session/session.h $(SEAL_DEPS) src/net/udp.h tests/net/bankbuild.h
build/tests/packsend_test: $(PACKSEND_SRC) $(PACKSEND_DEPS)
	@mkdir -p build/tests
	$(CC) $(CFLAGS_TEST) -std=gnu99 $(SANITIZE) -o $@ $(PACKSEND_SRC) -lm

build/tests/packsend_test.exe: $(PACKSEND_SRC) $(PACKSEND_DEPS)
	@mkdir -p build/tests
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -o $@ $(PACKSEND_SRC) -lm -lws2_32 -lbcrypt

# The scripts' sandbox without the game: Lua, the game's script file, and a test that stands in for the game. Lua's
# files are built with its options header, as in the game, and without -Werror. -fwrapv is one of the game's flags:
# Lua's reader of hexadecimal numbers lets a very long exponent overflow, and every build must wrap it the same way.
LUA_DIR := weemitt/third_party/lua
LUA_TEST_SRC := weemitt/src/port/g64lua.c $(wildcard $(LUA_DIR)/*.c)
LUA_TEST_DEPS := weemitt/src/port/g64lua_conf.h $(wildcard $(LUA_DIR)/*.h)
LUA_TEST_FLAGS := -std=gnu99 -O2 -Wall -fwrapv -ffp-contract=off -include weemitt/src/port/g64lua_conf.h

build/tests/lua_test: tests/net/lua_test.c $(LUA_TEST_SRC) $(LUA_TEST_DEPS)
	@mkdir -p build/tests
	$(CC) $(LUA_TEST_FLAGS) -g $(SANITIZE) -o $@ tests/net/lua_test.c $(LUA_TEST_SRC) -lm

build/tests/lua_test.exe: tests/net/lua_test.c $(LUA_TEST_SRC) $(LUA_TEST_DEPS)
	@mkdir -p build/tests
	$(MINGW64) $(LUA_TEST_FLAGS) -static -o $@ tests/net/lua_test.c $(LUA_TEST_SRC)

# The level file's structure check (src/client/levcheck.c): its test without the game, and its fuzz target.
build/tests/levcheck_test: tests/net/levcheck_test.c src/client/levcheck.c src/client/levcheck.h
	@mkdir -p build/tests
	$(CC) $(CFLAGS_TEST) -std=gnu99 $(SANITIZE) -o $@ tests/net/levcheck_test.c src/client/levcheck.c

build/tests/levcheck_test.exe: tests/net/levcheck_test.c src/client/levcheck.c src/client/levcheck.h
	@mkdir -p build/tests
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -o $@ tests/net/levcheck_test.c src/client/levcheck.c

build/tests/levcheck_fuzz: tests/fuzz/levcheck_fuzz.c src/client/levcheck.c src/client/levcheck.h
	@mkdir -p build/tests
	clang -std=gnu99 -O1 -g -fsanitize=fuzzer,address,undefined -o $@ tests/fuzz/levcheck_fuzz.c src/client/levcheck.c

# The check of a pack's models.bin (src/client/bankcheck.c): its test without the game, and its fuzz target. Both
# run the shadow walk (tests/fuzz/bankwalk.c), a copy of the game's bank loaders.
BANKCHECK_SRC := src/client/bankcheck.c src/client/rombanks.c src/client/fla2.c third_party/monocypher/monocypher.c \
    tests/fuzz/bankwalk.c
BANKCHECK_DEPS := src/client/bankcheck.h src/client/rombanks.h src/client/fla2.h third_party/monocypher/monocypher.h \
    tests/fuzz/bankwalk.h tests/net/bankbuild.h
build/tests/bankcheck_test: tests/net/bankcheck_test.c $(BANKCHECK_SRC) $(BANKCHECK_DEPS)
	@mkdir -p build/tests
	$(CC) $(CFLAGS_TEST) -std=gnu99 $(SANITIZE) -o $@ tests/net/bankcheck_test.c $(BANKCHECK_SRC) -lm

build/tests/bankcheck_test.exe: tests/net/bankcheck_test.c $(BANKCHECK_SRC) $(BANKCHECK_DEPS)
	@mkdir -p build/tests
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -o $@ tests/net/bankcheck_test.c $(BANKCHECK_SRC) -lm

build/tests/bankcheck_fuzz: tests/fuzz/bankcheck_fuzz.c $(BANKCHECK_SRC) $(BANKCHECK_DEPS)
	@mkdir -p build/tests
	clang -std=gnu99 -O1 -g -fsanitize=fuzzer,address,undefined -o $@ tests/fuzz/bankcheck_fuzz.c $(BANKCHECK_SRC) -lm

# The tool that makes and checks level packs (tools/g64pack.c).
packtool: build/g64pack
packtool-windows: build/g64pack.exe

# The bounded FLA2 reader and the index of the ROM's banks: the fuzz target. It needs no game.
FLA2_SRC := src/client/fla2.c src/client/rombanks.c third_party/monocypher/monocypher.c
FLA2_DEPS := src/client/fla2.h src/client/rombanks.h third_party/monocypher/monocypher.h
build/tests/fla2_fuzz: tests/fuzz/fla2_fuzz.c $(FLA2_SRC) $(FLA2_DEPS)
	@mkdir -p build/tests
	clang -std=gnu99 -O1 -g -fsanitize=fuzzer,address,undefined -o $@ tests/fuzz/fla2_fuzz.c $(FLA2_SRC)

# g64pack compiles a pack's script with Lua's parser, built as the game builds it (tools/g64pack_lua.c).
PACK_LUA_DEPS := tools/g64pack_lua.c $(LUA_TEST_DEPS) $(wildcard $(LUA_DIR)/*.c)
build/g64pack: tools/g64pack.c $(PACK_SRC) $(PACK_DEPS) src/client/levcheck.c src/client/levcheck.h $(PACK_LUA_DEPS)
	@mkdir -p build
	$(CC) $(LUA_TEST_FLAGS) -c tools/g64pack_lua.c -o build/g64pack_lua.o
	$(CC) -std=gnu99 -O2 -Wall -Wextra -Werror -o $@ tools/g64pack.c $(PACK_SRC) build/g64pack_lua.o -lm

build/g64pack.exe: tools/g64pack.c $(PACK_SRC) $(PACK_DEPS) src/client/levcheck.c src/client/levcheck.h $(PACK_LUA_DEPS)
	@mkdir -p build
	$(MINGW64) $(LUA_TEST_FLAGS) -c tools/g64pack_lua.c -o build/g64pack_lua-windows.o
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -o $@ tools/g64pack.c $(PACK_SRC) build/g64pack_lua-windows.o \
	    -lm

build/tests/pack_fuzz: tests/fuzz/pack_fuzz.c $(PACK_SRC) $(PACK_DEPS)
	@mkdir -p build/tests
	clang -std=gnu99 -O1 -g -fsanitize=fuzzer,address,undefined -o $@ tests/fuzz/pack_fuzz.c $(PACK_SRC) -lm

build/tests/lua_fuzz: tests/fuzz/lua_fuzz.c $(LUA_TEST_SRC) $(LUA_TEST_DEPS)
	@mkdir -p build/tests
	clang $(LUA_TEST_FLAGS) -O1 -g -fsanitize=fuzzer,address,undefined -o $@ tests/fuzz/lua_fuzz.c $(LUA_TEST_SRC)

build/tests/session_test: tests/net/session_test.c $(SESSION_SRC) src/session/session.h $(SEAL_DEPS) $(NET_SRC) $(ROLLBACK_DEPS)
	@mkdir -p build/tests
	$(CC) $(CFLAGS_TEST) -std=gnu99 $(SANITIZE) -o $@ tests/net/session_test.c $(SESSION_SRC) $(NET_SRC) $(ROLLBACK_SRC)

build/tests/session_test.exe: tests/net/session_test.c $(SESSION_SRC) src/session/session.h $(SEAL_DEPS) $(NET_SRC) $(ROLLBACK_DEPS)
	@mkdir -p build/tests
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -o $@ tests/net/session_test.c $(SESSION_SRC) $(NET_SRC) \
	    $(ROLLBACK_SRC) -lws2_32 -lbcrypt

build/tests/relay_test: tests/net/relay_test.c $(SESSION_SRC) src/session/session.h $(SEAL_DEPS) $(NET_SRC) src/net/udp.h $(ROLLBACK_DEPS)
	@mkdir -p build/tests
	$(CC) $(CFLAGS_TEST) -std=gnu99 $(SANITIZE) -o $@ tests/net/relay_test.c $(SESSION_SRC) $(NET_SRC) $(ROLLBACK_SRC)

build/tests/instances_fuzz: tests/fuzz/instances_fuzz.c src/client/imsg.c src/client/imsg.h $(ROLLBACK_SRC)
	@mkdir -p build/tests
	clang -std=gnu99 -O1 -g -fsanitize=fuzzer,address,undefined -o $@ tests/fuzz/instances_fuzz.c src/client/imsg.c \
	    $(ROLLBACK_SRC)

build/tests/relay_fuzz: tests/fuzz/relay_fuzz.c $(NET_SRC) src/net/udp.h
	@mkdir -p build/tests
	clang -std=gnu99 -O1 -g -fsanitize=fuzzer,address,undefined -o $@ tests/fuzz/relay_fuzz.c $(NET_SRC)

# The session over the lobby's relay: starts build/lobby on spare ports, runs tests/net/relay_test against it.
test-relay: server build/tests/relay_test
	PORT=18099 LOBBY_RELAY_PORT=27165 build/lobby > build/tests/lobby.log 2>&1 & pid=$$!; sleep 1; \
	    build/tests/relay_test 127.0.0.1:27165 > build/tests/relay_test.out; status=$$?; kill $$pid; \
	    cat build/tests/relay_test.out | tail -1; exit $$status

build/tests/session_fuzz: tests/fuzz/session_fuzz.c $(SESSION_SRC) src/session/session.h $(SEAL_DEPS) $(NET_SRC)
	@mkdir -p build/tests
	clang -std=gnu99 -O1 -g -fsanitize=fuzzer,address,undefined -o $@ tests/fuzz/session_fuzz.c $(SESSION_SRC) $(NET_SRC)

build/tests/udp_test: tests/net/udp_test.c $(NET_SRC) src/net/udp.h $(ROLLBACK_DEPS)
	@mkdir -p build/tests
	$(CC) $(CFLAGS_TEST) -std=gnu99 $(SANITIZE) -o $@ tests/net/udp_test.c $(NET_SRC) $(ROLLBACK_SRC)

build/tests/udp_test.exe: tests/net/udp_test.c $(NET_SRC) src/net/udp.h $(ROLLBACK_DEPS)
	@mkdir -p build/tests
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -o $@ tests/net/udp_test.c $(NET_SRC) $(ROLLBACK_SRC) -lws2_32

build/tests/loopback.exe: tests/net/loopback.c $(ROLLBACK_DEPS)
	@mkdir -p build/tests
	$(MINGW64) -std=c99 -O2 -Wall -Wextra -Werror -static -o $@ tests/net/loopback.c $(ROLLBACK_SRC)

build/tests/rollback_fuzz: tests/fuzz/rollback_fuzz.c $(ROLLBACK_DEPS)
	@mkdir -p build/tests
	clang -std=c99 -O1 -g -fsanitize=fuzzer,address,undefined -o $@ tests/fuzz/rollback_fuzz.c $(ROLLBACK_SRC)

# The loopback tests (2 to 16 clients over simulated networks), the UDP and session tests (real sockets on this
# machine), and the tests of the sealed messages, the sound reader, the pack reader, the pack tool, a pack sent from a
# host to a joiner, the level file check, the bank check and the scripts' sandbox.
test-c: build/tests/loopback build/tests/udp_test build/tests/session_test build/tests/sound_test build/tests/seal_test \
        build/tests/pack_test build/tests/lua_test build/g64pack build/tests/levcheck_test build/tests/bankcheck_test \
        build/tests/packsend_test build/tests/mouselook_test build/tests/freecam_test
	@mkdir -p build/tests/pack-linux
	build/tests/pack_test build/tests/pack-linux tests/fixtures/cube.models.bin > build/tests/pack_test.out || { cat build/tests/pack_test.out; exit 1; }
	tail -1 build/tests/pack_test.out
	build/tests/seal_test > build/tests/seal_test.out || { cat build/tests/seal_test.out; exit 1; }
	tail -1 build/tests/seal_test.out
	build/tests/lua_test > build/tests/lua_test.out || { cat build/tests/lua_test.out; exit 1; }
	tail -1 build/tests/lua_test.out
	build/tests/levcheck_test > build/tests/levcheck_test.out || { cat build/tests/levcheck_test.out; exit 1; }
	tail -1 build/tests/levcheck_test.out
	build/tests/bankcheck_test tests/fixtures/cube.models.bin > build/tests/bankcheck_test.out \
	    || { cat build/tests/bankcheck_test.out; exit 1; }
	tail -1 build/tests/bankcheck_test.out
	sh tests/net/packtool_test.sh build/tests/packtool-linux build/g64pack > build/tests/packtool.out \
	    || { cat build/tests/packtool.out; exit 1; }
	tail -1 build/tests/packtool.out
	build/tests/sound_test > build/tests/sound_test.out || { cat build/tests/sound_test.out; exit 1; }
	tail -1 build/tests/sound_test.out
	build/tests/loopback > build/tests/loopback.out || { cat build/tests/loopback.out; exit 1; }
	tail -2 build/tests/loopback.out
	build/tests/udp_test > build/tests/udp_test.out || { cat build/tests/udp_test.out; exit 1; }
	tail -1 build/tests/udp_test.out
	build/tests/session_test > build/tests/session_test.out || { cat build/tests/session_test.out; exit 1; }
	tail -1 build/tests/session_test.out
	build/tests/packsend_test tests/fixtures/cube.models.bin > build/tests/packsend_test.out \
	    || { cat build/tests/packsend_test.out; exit 1; }
	tail -1 build/tests/packsend_test.out
	build/tests/mouselook_test > build/tests/mouselook_test.out || { cat build/tests/mouselook_test.out; exit 1; }
	tail -1 build/tests/mouselook_test.out
	build/tests/freecam_test > build/tests/freecam_test.out || { cat build/tests/freecam_test.out; exit 1; }
	tail -1 build/tests/freecam_test.out

# The same tests, except the sound reader's, as a Windows program under Wine. Its offline hash must equal the Linux
# program's.
test-windows: build/tests/loopback.exe build/tests/udp_test.exe build/tests/session_test.exe \
              build/tests/seal_test.exe build/tests/pack_test.exe build/tests/lua_test.exe build/g64pack.exe \
              build/tests/levcheck_test.exe build/tests/bankcheck_test.exe build/tests/packsend_test.exe \
              build/tests/mouselook_test.exe build/tests/freecam_test.exe test-c
	@mkdir -p build/tests/pack-windows
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/mouselook_test.exe | tr -d '\r' \
	    > build/tests/mouselook_test-windows.out
	tail -1 build/tests/mouselook_test-windows.out
	cmp build/tests/mouselook_test.out build/tests/mouselook_test-windows.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/freecam_test.exe | tr -d '\r' \
	    > build/tests/freecam_test-windows.out
	tail -1 build/tests/freecam_test-windows.out
	cmp build/tests/freecam_test.out build/tests/freecam_test-windows.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/packsend_test.exe tests/fixtures/cube.models.bin \
	    | tr -d '\r' > build/tests/packsend_test-windows.out
	tail -1 build/tests/packsend_test-windows.out
	cmp build/tests/packsend_test.out build/tests/packsend_test-windows.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/pack_test.exe build/tests/pack-windows tests/fixtures/cube.models.bin | tr -d '\r' \
	    > build/tests/pack_test-windows.out
	tail -1 build/tests/pack_test-windows.out
	cmp build/tests/pack_test.out build/tests/pack_test-windows.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/seal_test.exe | tr -d '\r' \
	    > build/tests/seal_test-windows.out
	tail -1 build/tests/seal_test-windows.out
	cmp build/tests/seal_test.out build/tests/seal_test-windows.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/lua_test.exe | tr -d '\r' \
	    > build/tests/lua_test-windows.out
	tail -1 build/tests/lua_test-windows.out
	cmp build/tests/lua_test.out build/tests/lua_test-windows.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/levcheck_test.exe | tr -d '\r' \
	    > build/tests/levcheck_test-windows.out
	tail -1 build/tests/levcheck_test-windows.out
	cmp build/tests/levcheck_test.out build/tests/levcheck_test-windows.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/bankcheck_test.exe tests/fixtures/cube.models.bin \
	    | tr -d '\r' > build/tests/bankcheck_test-windows.out
	tail -1 build/tests/bankcheck_test-windows.out
	cmp build/tests/bankcheck_test.out build/tests/bankcheck_test-windows.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) sh tests/net/packtool_test.sh build/tests/packtool-windows \
	    $(WINE64) build/g64pack.exe > build/tests/packtool-windows.out || { cat build/tests/packtool-windows.out; exit 1; }
	tail -1 build/tests/packtool-windows.out
	cmp build/tests/packtool.out build/tests/packtool-windows.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/session_test.exe | tr -d '\r' \
	    > build/tests/session_test-windows.out
	tail -1 build/tests/session_test-windows.out
	grep -q '^ok: 0 failures' build/tests/session_test-windows.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/udp_test.exe | tr -d '\r' > build/tests/udp_test-windows.out
	tail -1 build/tests/udp_test-windows.out
	grep -q '^ok: 0 failures' build/tests/udp_test-windows.out
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/loopback.exe | tr -d '\r' > build/tests/loopback-windows.out
	tail -2 build/tests/loopback-windows.out
	grep -q '^ok: 0 failures' build/tests/loopback-windows.out
	grep 'offline run' build/tests/loopback.out > build/tests/hash-linux.txt
	grep 'offline run' build/tests/loopback-windows.out > build/tests/hash-windows.txt
	cmp build/tests/hash-linux.txt build/tests/hash-windows.txt

# The message parsers under libFuzzer for FUZZ_TIME seconds each (needs clang). Each parser is its own target, and
# make -j runs several at once. CI uses -j4.
FUZZ_TARGETS := fuzz-bankcheck fuzz-fla2 fuzz-levcheck fuzz-lua fuzz-pack fuzz-instances fuzz-relay fuzz-relay-go fuzz-rollback \
    fuzz-session
fuzz: $(FUZZ_TARGETS)

# The corpus starts with the cube, Glovebox's real output (tests/fixtures/README.md). It holds no game data.
fuzz-bankcheck: build/tests/bankcheck_fuzz
	@mkdir -p build/fuzz-corpus-bankcheck
	cp tests/fixtures/cube.models.bin build/fuzz-corpus-bankcheck/cube
	build/tests/bankcheck_fuzz -max_total_time=$(FUZZ_TIME) -max_len=4000 build/fuzz-corpus-bankcheck
fuzz-fla2: build/tests/fla2_fuzz
	@mkdir -p build/fuzz-corpus-fla2
	build/tests/fla2_fuzz -max_total_time=$(FUZZ_TIME) -max_len=4000 build/fuzz-corpus-fla2
fuzz-levcheck: build/tests/levcheck_fuzz
	@mkdir -p build/fuzz-corpus-levcheck
	build/tests/levcheck_fuzz -max_total_time=$(FUZZ_TIME) -max_len=2000 build/fuzz-corpus-levcheck
fuzz-lua: build/tests/lua_fuzz
	@mkdir -p build/fuzz-corpus-lua
	build/tests/lua_fuzz -max_total_time=$(FUZZ_TIME) -max_len=2000 -timeout=20 -dict=tests/fuzz/lua.dict \
	    build/fuzz-corpus-lua
# The pack's corpus starts with a whole pack with the cube's models: the six bytes that cut the input (24, 11 and 0
# bytes), a manifest, a level file of 11 bytes, and the cube.
fuzz-pack: build/tests/pack_fuzz
	@mkdir -p build/fuzz-corpus-pack
	{ printf '\030\000\013\000\000\000format=1\nname=A\nlevel=1\n\000\000\000\013t.ln\000}\000'; \
	    cat tests/fixtures/cube.models.bin; } > build/fuzz-corpus-pack/cube
	build/tests/pack_fuzz -max_total_time=$(FUZZ_TIME) -max_len=4000 build/fuzz-corpus-pack
fuzz-instances: build/tests/instances_fuzz
	@mkdir -p build/fuzz-corpus-instances
	build/tests/instances_fuzz -max_total_time=$(FUZZ_TIME) -max_len=1200 build/fuzz-corpus-instances
fuzz-relay: build/tests/relay_fuzz
	@mkdir -p build/fuzz-corpus-relay
	build/tests/relay_fuzz -max_total_time=$(FUZZ_TIME) -max_len=1220 build/fuzz-corpus-relay
# Go's fuzzer uses one worker here, as each libFuzzer target does.
fuzz-relay-go:
	cd server && go test -run=^$$ -fuzz=FuzzRelayHandle -fuzztime=$(FUZZ_TIME)s -parallel=1 .
fuzz-rollback: build/tests/rollback_fuzz
	@mkdir -p build/fuzz-corpus
	build/tests/rollback_fuzz -max_total_time=$(FUZZ_TIME) -max_len=600 build/fuzz-corpus
fuzz-session: build/tests/session_fuzz
	@mkdir -p build/fuzz-corpus-session
	build/tests/session_fuzz -max_total_time=$(FUZZ_TIME) -max_len=1201 build/fuzz-corpus-session

# Every test that needs no ROM.
test: test-c test-relay
	cd server && test -z "$$(gofmt -l .)" && go vet ./... && go test -race ./...

# The lobby server's container image.
image:
	docker build --build-arg VERSION=$(VERSION) -t g64coopdx-lobby server
