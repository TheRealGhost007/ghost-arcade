CC := gcc
SRC_DIR := src
BUILD_DIR := build
TEST_DIR := tests
# Code shared by every Ghost Arcade game (rng, the launcher data contract, ...).
COMMON_DIR ?= ../ghost-common
# The ui* modules are the opt-in input reader, back stack and button kit
# (only Blockfall builds them for now); gamepad.h's key macros are no longer
# used: the reader reads the pad as real buttons.
COMMON_MODULES := rng ghostlink keynames keynames_check sfx ui musicsynth song music prefs runlog achievements winscale tuning tuningui safefile howto \
	uiinput uiinput_rl uikeymap uinav uikit uikitdraw

TARGET := $(BUILD_DIR)/blockfall
TEST_TARGET := $(BUILD_DIR)/blockfall-tests

RAYLIB_CFLAGS := $(shell pkg-config --cflags raylib)
RAYLIB_LIBS := $(shell pkg-config --libs raylib)

WARN_FLAGS := -Wall -Wextra -Wno-unused-parameter
INCLUDES := -I$(SRC_DIR) -I$(COMMON_DIR)
CFLAGS := -std=c17 -O2 -flto=auto $(WARN_FLAGS) -MMD -MP
LDFLAGS := -flto=auto

SOURCES := $(wildcard $(SRC_DIR)/*.c)
OBJECTS := $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o) $(COMMON_MODULES:%=$(BUILD_DIR)/%.o)
DEPS := $(OBJECTS:.o=.d)

# Everything that can run without raylib: the rules, what runs record, the
# score tables, the settings file, auto-repeat, the play-screen layout and
# its lively parts (callouts, popups, the count-up), the
# input reader and button kit (with the text field the kit's tests cover,
# which the game itself does not use yet), the back stack, the play keys,
# what the menus decide, what happens around a run (the pause menu, the
# results card, the block wipe), Settings' tabs and their rules, block
# patterns and How to play's demos. keynames.c carries raylib's key
# numbers itself (no raylib needed), so the tests can check every name it stores.
# (ghost-common's ghostlink.c comes in through tests/shared_ghostlink.c.)
CORE_SOURCES := $(COMMON_DIR)/rng.c $(COMMON_DIR)/safefile.c $(COMMON_DIR)/prefs.c \
	$(COMMON_DIR)/uiinput.c $(COMMON_DIR)/uikeymap.c $(COMMON_DIR)/uinav.c $(COMMON_DIR)/uikit.c $(COMMON_DIR)/uitextfield.c \
	$(SRC_DIR)/tetromino.c $(SRC_DIR)/board.c $(SRC_DIR)/game.c $(SRC_DIR)/repeat.c $(SRC_DIR)/persist.c \
	$(SRC_DIR)/runrecord.c $(SRC_DIR)/localtable.c $(SRC_DIR)/layout.c $(SRC_DIR)/hudfx.c $(SRC_DIR)/feel.c $(SRC_DIR)/playstep.c $(SRC_DIR)/controls.c \
	$(SRC_DIR)/comfort.c $(SRC_DIR)/actions.c $(SRC_DIR)/menukeys.c $(SRC_DIR)/menumodel.c $(SRC_DIR)/backdrop.c $(SRC_DIR)/app.c \
	$(SRC_DIR)/wipe.c $(SRC_DIR)/pausemenu.c $(SRC_DIR)/results.c \
	$(SRC_DIR)/look.c $(SRC_DIR)/setpage.c $(SRC_DIR)/howtodemo.c $(COMMON_DIR)/keynames.c
TEST_SOURCES := $(wildcard $(TEST_DIR)/*.c) $(CORE_SOURCES)

PREFIX ?= $(HOME)/.local
BIN_DIR := $(PREFIX)/bin
DATA_DIR := $(PREFIX)/share/blockfall
ICON_DIR := $(HOME)/.local/share/icons/hicolor/256x256/apps
APPS_DIR := $(HOME)/.local/share/applications

.PHONY: all run test test-asan shots shots-asan clean install uninstall debug

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) $(RAYLIB_CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(COMMON_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) $(RAYLIB_CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $(TARGET) $(RAYLIB_LIBS) -lm

-include $(DEPS)

debug: CFLAGS := -std=c17 -O0 -g -fsanitize=address,undefined $(WARN_FLAGS) -MMD -MP
debug: LDFLAGS := -fsanitize=address,undefined
debug: clean $(TARGET)

run: $(TARGET)
	./$(TARGET)

# Core game-logic tests build without raylib so they run headless/CI-friendly.
# They make their own temp HOME (tests/test_main.c), so no real save is touched.
test: | $(BUILD_DIR)
	$(CC) -std=c17 -O0 -g $(WARN_FLAGS) $(INCLUDES) $(TEST_SOURCES) -o $(TEST_TARGET) -lm
	./$(TEST_TARGET)

# The same tests under ASan/UBSan (CI does this with CC="gcc -fsanitize=...").
test-asan: | $(BUILD_DIR)
	$(CC) -std=c17 -O0 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(WARN_FLAGS) $(INCLUDES) $(TEST_SOURCES) -o $(TEST_TARGET)-asan -lm
	ASAN_OPTIONS=detect_leaks=0 ./$(TEST_TARGET)-asan

# Screenshots without a visible window (tools/shots/driver.c): the real game
# in a hidden window, scripted keys and pad, no audio, no ghost-sync, a temp
# HOME per script, and a hyprctl check that the window never maps. `make shots`
# runs every script in tools/shots/scripts and writes PNGs and logs to
# SHOTS_OUT; SHOTS_SCRIPTS picks other scripts. `make shots-asan` runs the same
# under ASan/UBSan. The wrap list is every __wrap_ function the driver defines.
SHOTS_BIN := $(BUILD_DIR)/shots/blockfall-shots
SHOTS_ASAN_BIN := $(BUILD_DIR)/shots-asan/blockfall-shots
SHOTS_OUT ?= $(BUILD_DIR)/shots/png
SHOTS_SCRIPTS ?= $(sort $(wildcard tools/shots/scripts/*.txt))
SHOTS_SOURCES := $(filter-out $(SRC_DIR)/main.c,$(SOURCES)) $(COMMON_MODULES:%=$(COMMON_DIR)/%.c) \
	tools/shots/main_wrap.c tools/shots/driver.c
# -Wno-format-truncation: at -O1 GCC flags two ghost-common files (ghostlink.c,
# tuning.c) that are not Blockfall's to edit; the game's own build is clean.
SHOTS_WRAPS = $(shell grep -o '__wrap_[A-Za-z0-9_]*' tools/shots/driver.c | sed 's/__wrap_//' | sort -u)
comma := ,
SHOTS_SAN :=
$(SHOTS_ASAN_BIN): SHOTS_SAN := -fsanitize=address,undefined -fno-sanitize-recover=undefined
$(SHOTS_BIN) $(SHOTS_ASAN_BIN): $(SHOTS_SOURCES) $(SRC_DIR)/main.c $(wildcard $(SRC_DIR)/*.h $(COMMON_DIR)/*.h)
	@mkdir -p $(dir $@)
	$(CC) -std=c17 -O1 -g $(SHOTS_SAN) $(WARN_FLAGS) -Wno-format-truncation $(INCLUDES) $(RAYLIB_CFLAGS) $(SHOTS_SOURCES) -o $@ \
		$(addprefix -Wl$(comma)--wrap=,$(SHOTS_WRAPS)) $(RAYLIB_LIBS) -lm
	ln -sfn $(CURDIR)/assets $(dir $@)assets

shots: $(SHOTS_BIN)
	tools/shots/run.sh $(SHOTS_BIN) $(SHOTS_OUT) $(SHOTS_SCRIPTS)

shots-asan: $(SHOTS_ASAN_BIN)
	tools/shots/run.sh $(SHOTS_ASAN_BIN) $(SHOTS_OUT) $(SHOTS_SCRIPTS)

clean:
	rm -rf $(BUILD_DIR)

install: all
	install -Dm755 $(TARGET) $(BIN_DIR)/blockfall
	mkdir -p $(DATA_DIR)
	cp -r assets $(DATA_DIR)/
	install -Dm644 assets/icons/blockfall.png $(ICON_DIR)/blockfall.png
	install -Dm644 packaging/blockfall.desktop $(APPS_DIR)/blockfall.desktop
	-update-desktop-database $(APPS_DIR) 2>/dev/null
	-gtk-update-icon-cache $(HOME)/.local/share/icons/hicolor 2>/dev/null
	@echo "Installed. Launch with: blockfall  (ensure $(BIN_DIR) is in PATH)"

uninstall:
	rm -f $(BIN_DIR)/blockfall
	# Only the installed assets: $(DATA_DIR) also holds the player's save data
	# and scores, which an uninstall must never delete.
	rm -rf $(DATA_DIR)/assets
	-rmdir $(DATA_DIR) 2>/dev/null
	rm -f $(ICON_DIR)/blockfall.png
	rm -f $(APPS_DIR)/blockfall.desktop
