# The build. The game comes from the copy of weemitt in weemitt/ and is built by its own Makefile, at 64 bits only.
# The game's output goes to weemitt/build/; the rest goes to build/.

VERSION ?= dev
JOBS ?= $(shell nproc 2>/dev/null || echo 4)
WEEMITT := $(MAKE) -C weemitt -j$(JOBS)

PHONY_SOUND := sound sound-build
.PHONY: $(PHONY_SOUND)
.PHONY: test-relay client game windows headless savestates savestates-test rollback-game-build rollback-game twoplayers-build twoplayers levelstart-build levelstart instances-build instances crossbuild-build crossbuild regress server test test-c test-windows fuzz image

# The online program for Linux: weemitt/build/studio64/weemitt-studio (needs SDL2). It is weemitt's studio at 64 bits
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
# exits in the online game (tests/rom/exits.c), on Linux and under Wine; the outputs must be equal (needs the ROM).
TWOPLAYERS_SRC := tests/rom/twoplayers.c weemitt/src/port/statehash.c
MEMBERS_SRC := tests/rom/members.c weemitt/src/port/statehash.c
PAUSE_SRC := tests/rom/pause.c weemitt/src/port/statehash.c
CAVE_SRC := tests/rom/cave.c weemitt/src/port/statehash.c
EXITS_SRC := tests/rom/exits.c weemitt/src/port/statehash.c
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

# Players moving between instances of the real game (tests/rom/instances.c), on Linux and under Wine; the outputs must
# be equal (needs the ROM in GLOVER_ROM).
INSTANCES_SRC := tests/rom/instances.c src/client/instances.c src/client/imsg.c src/rollback/engine_game.c \
                 src/rollback/rollback.c \
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

NET_SRC := src/net/udp.c
SESSION_SRC := src/session/session.c
build/tests/session_test: tests/net/session_test.c $(SESSION_SRC) src/session/session.h $(NET_SRC) $(ROLLBACK_DEPS)
	@mkdir -p build/tests
	$(CC) $(CFLAGS_TEST) -std=gnu99 $(SANITIZE) -o $@ tests/net/session_test.c $(SESSION_SRC) $(NET_SRC) $(ROLLBACK_SRC)

build/tests/session_test.exe: tests/net/session_test.c $(SESSION_SRC) src/session/session.h $(NET_SRC) $(ROLLBACK_DEPS)
	@mkdir -p build/tests
	$(MINGW64) -std=gnu99 -O2 -Wall -Wextra -Werror -static -o $@ tests/net/session_test.c $(SESSION_SRC) $(NET_SRC) \
	    $(ROLLBACK_SRC) -lws2_32

build/tests/relay_test: tests/net/relay_test.c $(SESSION_SRC) src/session/session.h $(NET_SRC) src/net/udp.h $(ROLLBACK_DEPS)
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

build/tests/session_fuzz: tests/fuzz/session_fuzz.c $(SESSION_SRC) src/session/session.h $(NET_SRC)
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

# The loopback tests (2 to 16 clients over simulated networks) and the UDP tests (real sockets on this machine).
test-c: build/tests/loopback build/tests/udp_test build/tests/session_test
	build/tests/loopback > build/tests/loopback.out || { cat build/tests/loopback.out; exit 1; }
	tail -2 build/tests/loopback.out
	build/tests/udp_test > build/tests/udp_test.out || { cat build/tests/udp_test.out; exit 1; }
	tail -1 build/tests/udp_test.out
	build/tests/session_test > build/tests/session_test.out || { cat build/tests/session_test.out; exit 1; }
	tail -1 build/tests/session_test.out

# The same tests as a Windows program under Wine. Its offline hash must equal the Linux program's.
test-windows: build/tests/loopback.exe build/tests/udp_test.exe build/tests/session_test.exe test-c
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

# The message parsers under libFuzzer for FUZZ_TIME seconds (needs clang).
fuzz: build/tests/rollback_fuzz build/tests/session_fuzz build/tests/relay_fuzz build/tests/instances_fuzz
	@mkdir -p build/fuzz-corpus build/fuzz-corpus-session build/fuzz-corpus-relay build/fuzz-corpus-instances
	build/tests/instances_fuzz -max_total_time=$(FUZZ_TIME) -max_len=1200 build/fuzz-corpus-instances
	build/tests/relay_fuzz -max_total_time=$(FUZZ_TIME) -max_len=1220 build/fuzz-corpus-relay
	cd server && go test -run=^$$ -fuzz=FuzzRelayHandle -fuzztime=$(FUZZ_TIME)s .
	build/tests/rollback_fuzz -max_total_time=$(FUZZ_TIME) -max_len=600 build/fuzz-corpus
	build/tests/session_fuzz -max_total_time=$(FUZZ_TIME) -max_len=1201 build/fuzz-corpus-session

# Every test that needs no ROM.
test: test-c test-relay
	cd server && test -z "$$(gofmt -l .)" && go vet ./... && go test -race ./...

# The lobby server's container image.
image:
	docker build --build-arg VERSION=$(VERSION) -t g64coopdx-lobby server
