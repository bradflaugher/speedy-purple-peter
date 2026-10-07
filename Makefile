# SPEEDY PURPLE PETER - build & test
GBDK_HOME ?= /opt/gbdk
LCC       := $(GBDK_HOME)/bin/lcc
CC        ?= gcc
PYTHON    ?= python3
BUILD     := build
OBJ       := $(BUILD)/obj
ROM       := $(BUILD)/speedy-purple-peter.gb

CORE_SRC  := src/core/sim.c src/core/ents.c src/core/level.c
CORE_HDR  := $(wildcard src/core/*.h)
GB_SRC    := src/gb/main.c src/gb/render.c src/gb/screens.c src/gb/sound.c src/gb/assets.c
GB_HDR    := $(wildcard src/gb/*.h)
HOST_CFLAGS := -std=c99 -O2 -Wall -Wextra -Werror

# MBC5 + RAM + battery (0x1B), one 8 KB SRAM bank, CGB-enhanced, banks packed automatically
CFLAGS_GB  := -Wf--max-allocs-per-node5000 -Wf--opt-code-speed -Isrc/core -Isrc/gb
LDFLAGS_GB := -Wm-yt0x1B -Wm-ya1 -Wm-yc -Wm-yn"PURPLEPETER" -Wm-yoA -autobank -Wl-j -Wm-yS

GB_OBJS := $(patsubst src/core/%.c,$(OBJ)/core_%.o,$(CORE_SRC)) $(patsubst src/gb/%.c,$(OBJ)/%.o,$(GB_SRC))

.PHONY: all rom assets test test-host test-assets test-rom test-bot screenshots clean

all: rom

rom: $(ROM)

assets: src/gb/assets.c

src/gb/assets.c src/gb/assets.h: $(wildcard assets/*.txt) tools/gen_assets.py src/core/tiles.h
	$(PYTHON) tools/gen_assets.py

$(OBJ)/core_%.o: src/core/%.c $(CORE_HDR) | $(OBJ)
	$(LCC) $(CFLAGS_GB) -c -o $@ $<

$(OBJ)/%.o: src/gb/%.c $(CORE_HDR) $(GB_HDR) | $(OBJ)
	$(LCC) $(CFLAGS_GB) -c -o $@ $<

$(ROM): $(GB_OBJS) | $(BUILD)
	$(LCC) $(LDFLAGS_GB) -o $@ $(GB_OBJS)
	@$(GBDK_HOME)/bin/romusage $(BUILD)/speedy-purple-peter.map -g 2>/dev/null | tail -n 14 || true

$(BUILD):
	mkdir -p $(BUILD)

$(OBJ):
	mkdir -p $(OBJ)

# ---- host builds of the core
$(BUILD)/test_core: tests/test_core.c tests/bot.c tests/bot.h $(CORE_SRC) $(CORE_HDR) | $(BUILD)
	$(CC) $(HOST_CFLAGS) -Isrc/core -Itests -o $@ tests/test_core.c tests/bot.c $(CORE_SRC)

$(BUILD)/sppgen: tools/sppgen.c tests/bot.c tests/bot.h $(CORE_SRC) $(CORE_HDR) | $(BUILD)
	$(CC) $(HOST_CFLAGS) -Isrc/core -Itests -o $@ tools/sppgen.c tests/bot.c $(CORE_SRC)

$(BUILD)/test_sound: tests/test_sound.c src/gb/sound.c $(GB_HDR) | $(BUILD)
	$(CC) $(HOST_CFLAGS) -DHOST_TEST -Isrc/gb -Isrc/core -o $@ tests/test_sound.c src/gb/sound.c

test-host: $(BUILD)/test_core $(BUILD)/test_sound $(BUILD)/sppgen
	$(BUILD)/test_core
	$(BUILD)/test_sound

test-assets:
	$(PYTHON) -m unittest discover -s tests -p 'test_assets.py'

test-rom: $(ROM)
	$(PYTHON) -m unittest discover -s tests -p 'test_rom.py' -v

# the balance report: the search bot plays every preset seed (slow-ish)
test-bot: $(BUILD)/sppgen
	$(BUILD)/sppgen bot 1 12

screenshots: $(ROM)
	$(PYTHON) tools/screenshots.py

test: test-host test-assets test-rom

clean:
	rm -rf $(BUILD)
