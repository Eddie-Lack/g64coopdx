# The studio build (make studio, make studio-windows): the window program with the brute forcer (bruteforce/).
# The Makefile includes this file when STUDIO is 1.
#
# The game's objects are linked into one relocatable object, game.o, as the brute forcer's engine does
# (bruteforce/docs/engine.md). Its writable data goes into the sections gstate_data and gstate_bss, which a savestate
# copies, and the game's calls of the functions in bruteforce/engine/hooks.def go to the engine's hooks. The game's
# memory (gen/ram.S) gets a section of its own, gram_bss. The platform layer, the renderer and the menu stay outside
# the saved state. devview.c is game code that only the overlays read: it stays outside too.

BF := bruteforce

ifeq ($(TARGET),windows)
STUDIO_LD := $(MINGW)-ld
STUDIO_OBJCOPY := $(MINGW)-objcopy
# A state holds pointers to the program's code and data: the program loads at one fixed address, without ASLR and
# without a relocation table (bruteforce/Makefile). The 64-bit program keeps MinGW's usual base.
ifeq ($(BITS),64)
LDFLAGS += -Wl,--disable-dynamicbase -Wl,--disable-reloc-section -Wl,--image-base=0x140000000
# After the relocatable link, MinGW's inline functions would leave unwind entries that the final link cannot place
# (bruteforce/Makefile). The game's code needs no unwind tables.
GAME_CFLAGS += -fno-asynchronous-unwind-tables -fno-unwind-tables
GAME_CXXFLAGS += -fno-asynchronous-unwind-tables -fno-unwind-tables
else
LDFLAGS += -Wl,--disable-dynamicbase -Wl,--disable-reloc-section -Wl,--large-address-aware -Wl,--image-base=0x08000000
endif
else
STUDIO_LD := ld -m $(if $(filter 64,$(BITS)),elf_x86_64,elf_i386)
STUDIO_OBJCOPY := objcopy
ifeq ($(BITS),64)
# The program loads at one fixed address (bruteforce/Makefile); the game's objects stay position-independent.
LDFLAGS := -no-pie -Wl,-z,noexecstack
endif
endif
# The 64-bit Linux game is position-independent: its data with addresses is in .data.rel sections, which a state copies
# too (bruteforce/Makefile).
STUDIO_DATA_REL := $(if $(and $(filter 64,$(BITS)),$(filter-out windows,$(TARGET))),$(foreach s,.data.rel \
                   .data.rel.local .data.rel.ro .data.rel.ro.local,--rename-section $(s)=gstate_data))

HOST_CFLAGS += -DPORT_STUDIO -I$(BF)/engine -I$(BF)/src
# g64coopdx: the online program keeps its own folder for the player's files (src/port/prefpath.h).
HOST_CFLAGS += -DPORT_PREF_ORG='"g64coopdx"' -DPORT_PREF_APP='"Glover"'
HOST_CXXFLAGS += -DPORT_PREF_ORG='"g64coopdx"' -DPORT_PREF_APP='"Glover"'
HOST_CXXFLAGS += -DPORT_STUDIO -I$(BF)/engine
BF_CFLAGS := $(ARCH_FLAGS) -std=gnu99 -O2 -g -fno-pie -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
             -I$(BF)/engine -I$(BF)/src -I$(BUILD)/bf -DBUILD_OPT='"studio"' -DGLOVER_EMBEDDED

STUDIO_HOOKS := $(shell sed -n 's/^\(HOOK[01A]\|OFF0\|DRAW0\|SKIP[0P]\)(\([A-Za-z0-9_]*\)[),].*/\2/p' $(BF)/engine/hooks.def)
STUDIO_GAME_OBJS := $(filter-out $(BUILD)/gen/ram.o $(BUILD)/src/port/devview.o,$(GAME_OBJS))
STUDIO_RAM_OBJ := $(BUILD)/gen/ram-studio.o
BF_ENGINE_OBJS := $(patsubst %.c,$(BUILD)/bf/%.o,$(addprefix engine/,engine.c track.c hooks.c split.c)) \
                  $(BUILD)/bf/symbols.o
BF_PROGRAM_OBJS := $(patsubst $(BF)/%.c,$(BUILD)/bf/%.o,$(wildcard $(BF)/src/*.c))
STUDIO_OBJS := $(BUILD)/src/port/studio.o
# g64coopdx: the online mode (src/client/netplay.c of the repository around this copy) and what it needs.
G64_ROOT := ..
G64_SRCS := $(addprefix $(G64_ROOT)/src/,client/netplay.c client/instances.c client/imsg.c client/pack.c client/levcheck.c session/session.c session/seal.c \
            net/udp.c rollback/rollback.c rollback/engine_game.c client/sound.c client/soundout.c \
            client/fla2.c client/rombanks.c client/rombanks_game.c client/bankcheck.c client/bankranges.c \
            client/bankranges_table.c client/packrom.c)
G64_OBJS := $(patsubst $(G64_ROOT)/src/%.c,$(BUILD)/g64/%.o,$(G64_SRCS))
STUDIO_OBJS += $(G64_OBJS) $(BUILD)/src/port/g64launcher.o $(BUILD)/g64/monocypher.o

# The cryptography library that seals the session's messages (third_party/monocypher of the repository around).
$(BUILD)/g64/monocypher.o: $(G64_ROOT)/third_party/monocypher/monocypher.c
	@mkdir -p $(dir $@)
	$(CC) $(BF_CFLAGS) -c $< -o $@

$(BUILD)/src/port/g64launcher.o: src/port/g64launcher.cpp src/port/g64launcher.h src/game/g64title.h $(G64_ROOT)/src/client/netplay.h
	@mkdir -p $(dir $@)
	$(CXX) $(HOST_CXXFLAGS) -c $< -o $@

$(BUILD)/g64/%.o: $(G64_ROOT)/src/%.c $(wildcard $(G64_ROOT)/src/*/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(BF_CFLAGS) -c $< -o $@

# The build id: the commit the program is built from. The session's hello carries it, and a host refuses a joiner
# of another build (src/session/session.h). The header is written again only when the commit changes, and the online
# mode's code is compiled again then. G64_BUILD overrides it (a release can pass its own).
G64_BUILD ?= $(shell git -C $(G64_ROOT) rev-parse --short=12 HEAD 2>/dev/null || echo dev)
G64_BUILD_H := $(BUILD)/g64/g64build.h
G64_BUILD_LINE := \#define G64_BUILD_ID "$(G64_BUILD)"
$(shell mkdir -p $(BUILD)/g64 && { echo '$(G64_BUILD_LINE)' | cmp -s - $(G64_BUILD_H) || echo '$(G64_BUILD_LINE)' > $(G64_BUILD_H); })

$(BUILD)/g64/client/netplay.o: $(G64_ROOT)/src/client/netplay.c $(wildcard $(G64_ROOT)/src/*/*.h) $(G64_BUILD_H)
	@mkdir -p $(dir $@)
	$(CC) $(BF_CFLAGS) -DG64_BUILD_HEADER -I$(BUILD)/g64 -c $< -o $@

# The sound output talks to SDL.
$(BUILD)/g64/client/soundout.o: $(G64_ROOT)/src/client/soundout.c $(wildcard $(G64_ROOT)/src/*/*.h) src/lib/glover.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) -c $< -o $@

$(BUILD)/game.o: $(STUDIO_GAME_OBJS) $(BF)/engine/hooks.def
	$(STUDIO_LD) -r -d $(addprefix --wrap=,$(STUDIO_HOOKS)) -o $@.tmp $(STUDIO_GAME_OBJS)
	$(STUDIO_OBJCOPY) --rename-section .data=gstate_data --rename-section .bss=gstate_bss $(STUDIO_DATA_REL) $@.tmp $@
	rm -f $@.tmp
	@python3 $(BF)/tools/checkgame.py $@ $(TARGET) $(BITS)

$(STUDIO_RAM_OBJ): $(BUILD)/gen/ram.o
	$(STUDIO_OBJCOPY) --rename-section .bss=gram_bss $< $@

$(BUILD)/bf/symbols.c: gen/ram.S $(BF)/tools/gensyms.py
	@mkdir -p $(dir $@)
	python3 $(BF)/tools/gensyms.py $< > $@

$(BUILD)/bf/symbols.o: $(BUILD)/bf/symbols.c $(BF)/engine/engine.h
	$(CC) $(BF_CFLAGS) -c $< -o $@

$(BUILD)/bf/%.o: $(BF)/%.c $(wildcard $(BF)/engine/*.h $(BF)/src/*.h) $(BF)/engine/hooks.def
	@mkdir -p $(dir $@)
	$(CC) $(BF_CFLAGS) -c $< -o $@

$(BUILD)/src/port/studio.o: src/port/studio.c src/port/studio.h $(BF)/engine/engine.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) -c $< -o $@

$(BUILD)/src/port/platform.o: src/port/studio.h

$(EXE): $(BUILD)/game.o $(STUDIO_RAM_OBJ) $(BUILD)/src/port/devview.o $(HOST_OBJS) $(BF_ENGINE_OBJS) \
        $(BF_PROGRAM_OBJS) $(STUDIO_OBJS)
	$(CXX) $(ARCH_FLAGS) $(LDFLAGS) -o $@ $^ $(SDL_LIBS) -lm $(if $(filter windows,$(TARGET)),-lws2_32 -lbcrypt,-lrt -lpthread)

$(BUILD)/src/port/menu.o: src/port/studio.h
