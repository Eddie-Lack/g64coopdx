# The build. The game comes from the copy of weemitt in weemitt/ and is built by its own Makefile, at 64 bits only.
# The game's output goes to weemitt/build/; the rest goes to build/.

VERSION ?= dev
JOBS ?= $(shell nproc 2>/dev/null || echo 4)
WEEMITT := $(MAKE) -C weemitt -j$(JOBS)

.PHONY: game windows headless savestates savestates-test rollback-game-build rollback-game crossbuild-build crossbuild regress server test test-c test-windows fuzz image

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

build/tests/loopback.exe: tests/net/loopback.c $(ROLLBACK_DEPS)
	@mkdir -p build/tests
	$(MINGW64) -std=c99 -O2 -Wall -Wextra -Werror -static -o $@ tests/net/loopback.c $(ROLLBACK_SRC)

build/tests/rollback_fuzz: tests/fuzz/rollback_fuzz.c $(ROLLBACK_DEPS)
	@mkdir -p build/tests
	clang -std=c99 -O1 -g -fsanitize=fuzzer,address,undefined -o $@ tests/fuzz/rollback_fuzz.c $(ROLLBACK_SRC)

# The loopback tests: 2 to 16 clients over simulated networks.
test-c: build/tests/loopback
	build/tests/loopback > build/tests/loopback.out || { cat build/tests/loopback.out; exit 1; }
	tail -2 build/tests/loopback.out

# The same tests as a Windows program under Wine. Its offline hash must equal the Linux program's.
test-windows: build/tests/loopback.exe test-c
	WINEDEBUG=-all WINEPREFIX=$(WINEPREFIX64) $(WINE64) build/tests/loopback.exe | tr -d '\r' > build/tests/loopback-windows.out
	tail -2 build/tests/loopback-windows.out
	grep -q '^ok: 0 failures' build/tests/loopback-windows.out
	grep 'offline run' build/tests/loopback.out > build/tests/hash-linux.txt
	grep 'offline run' build/tests/loopback-windows.out > build/tests/hash-windows.txt
	cmp build/tests/hash-linux.txt build/tests/hash-windows.txt

# The message parsers under libFuzzer for FUZZ_TIME seconds (needs clang).
fuzz: build/tests/rollback_fuzz
	@mkdir -p build/fuzz-corpus
	build/tests/rollback_fuzz -max_total_time=$(FUZZ_TIME) -max_len=600 build/fuzz-corpus

# Every test that needs no ROM.
test: test-c
	cd server && test -z "$$(gofmt -l .)" && go vet ./... && go test -race ./...

# The lobby server's container image.
image:
	docker build --build-arg VERSION=$(VERSION) -t g64coopdx-lobby server
