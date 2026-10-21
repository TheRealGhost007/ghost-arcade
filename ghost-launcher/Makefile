CC := gcc
SRC_DIR := src
BUILD_DIR := build

TARGET := $(BUILD_DIR)/ghost-launcher
# ghost-sync: the online-leaderboard helper (the only libcurl user). Games
# and the launcher spawn it; they never touch the network themselves.
SYNC_TARGET := $(BUILD_DIR)/ghost-sync
SYNC_SOURCES := sync/main.c sync/syncdata.c sync/updatecheck.c ../ghost-common/safefile.c
TEST_TARGET := $(BUILD_DIR)/ghost-launcher-tests

RAYLIB_CFLAGS := $(shell pkg-config --cflags raylib)
RAYLIB_LIBS := $(shell pkg-config --libs raylib)

WARN_FLAGS := -Wall -Wextra -Wno-unused-parameter
CFLAGS := -std=c17 -O2 -flto $(WARN_FLAGS) -MMD -MP
LDFLAGS := -flto=auto # parallel link-time optimisation; plain -flto warns once the code splits in two

# The arcade-wide preferences (reduced flashing, ...) come from ghost-common.
COMMON_DIR ?= ../ghost-common
CFLAGS += -I$(COMMON_DIR)

SOURCES := $(wildcard $(SRC_DIR)/*.c)
# Hall music: ghost-common's music, synth and song code, linked as they are.
MUSIC_OBJECTS := $(BUILD_DIR)/music.o $(BUILD_DIR)/musicsynth.o $(BUILD_DIR)/song.o
OBJECTS := $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o) $(BUILD_DIR)/prefs.o $(BUILD_DIR)/achievements.o $(BUILD_DIR)/winscale.o $(BUILD_DIR)/syncdata.o $(BUILD_DIR)/updatecheck.o $(BUILD_DIR)/safefile.o $(BUILD_DIR)/howto.o $(BUILD_DIR)/daily.o $(MUSIC_OBJECTS)
DEPS := $(OBJECTS:.o=.d)

PREFIX ?= $(HOME)/.local
BIN_DIR := $(PREFIX)/bin
DATA_DIR := $(PREFIX)/share/ghost-launcher
ICON_DIR := $(HOME)/.local/share/icons/hicolor/256x256/apps
APPS_DIR := $(HOME)/.local/share/applications

# Rules below come before `all`, so say what a bare `make` means.
.DEFAULT_GOAL := all

# Admin mode: admin.local.mk (git-ignored, made by `make admin-lock`) holds a
# one-way fingerprint of the owner's machine. No file = no admin mode.
-include admin.local.mk
ADMIN_LOCK_HASH ?=
$(BUILD_DIR)/adminlock.o: CFLAGS += -DADMIN_LOCK_HASH='"$(ADMIN_LOCK_HASH)"'
$(BUILD_DIR)/adminlock.o: $(wildcard admin.local.mk)

.PHONY: all run test shots shots-asan monkey clean install uninstall admin-lock admin-unlock

admin-lock:
	@test -r /etc/machine-id || { echo "no /etc/machine-id on this system"; exit 1; }
	@printf 'ADMIN_LOCK_HASH := %s\n' "$$(printf 'ghost-launcher-admin-v1:%s' "$$(head -n1 /etc/machine-id | tr -d ' \r\n')" | sha256sum | cut -d' ' -f1)" > admin.local.mk
	@echo "admin mode is now locked to THIS machine (admin.local.mk written; it is git-ignored). Run: make && make install"

admin-unlock:
	@rm -f admin.local.mk
	@echo "admin.local.mk removed: the next build has no admin mode."

all: $(TARGET) $(SYNC_TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -c $< -o $@

$(BUILD_DIR)/prefs.o: $(COMMON_DIR)/prefs.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/winscale.o: $(COMMON_DIR)/winscale.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -c $< -o $@

$(BUILD_DIR)/safefile.o: $(COMMON_DIR)/safefile.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/updatecheck.o: sync/updatecheck.c sync/updatecheck.h sync/syncdata.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Isync -c $< -o $@

$(BUILD_DIR)/syncdata.o: sync/syncdata.c sync/syncdata.h sync/online_config.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Isync -c $< -o $@

$(BUILD_DIR)/achievements.o: $(COMMON_DIR)/achievements.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# The daily desk features a game picked with the games' own daily seed (raylib-free).
$(BUILD_DIR)/daily.o: $(COMMON_DIR)/daily.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# A machine's page shows the game's own how-to card (raylib-free).
$(BUILD_DIR)/howto.o: $(COMMON_DIR)/howto.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/music.o: $(COMMON_DIR)/music.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -c $< -o $@

$(BUILD_DIR)/musicsynth.o: $(COMMON_DIR)/musicsynth.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/song.o: $(COMMON_DIR)/song.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $(TARGET) $(RAYLIB_LIBS) -lm

# The commit this build was made from, so ghost-sync can tell when GitHub has
# something newer. Rewritten only when it changes; empty outside a git checkout.
BUILD_COMMIT := $(shell git -C $(CURDIR) rev-parse HEAD 2>/dev/null)
BUILD_SOURCE_DIR := $(shell git -C $(CURDIR) rev-parse --show-toplevel 2>/dev/null)
$(BUILD_DIR)/build_commit.h: FORCE | $(BUILD_DIR)
	@printf '#define BUILD_COMMIT "%s"\n#define BUILD_SOURCE_DIR "%s"\n#define BUILD_BIN_DIR "%s"\n' "$(BUILD_COMMIT)" "$(BUILD_SOURCE_DIR)" "$(BIN_DIR)" > $@.new
	@if cmp -s $@.new $@; then rm -f $@.new; else mv $@.new $@; fi
FORCE:

$(BUILD_DIR)/main.o: $(BUILD_DIR)/build_commit.h
$(BUILD_DIR)/main.o: CFLAGS += -I$(BUILD_DIR)

$(SYNC_TARGET): $(SYNC_SOURCES) sync/syncdata.h sync/updatecheck.h sync/online_config.h $(BUILD_DIR)/build_commit.h | $(BUILD_DIR)
	$(CC) -std=c17 -O2 $(WARN_FLAGS) -I$(COMMON_DIR) -Isync -I$(BUILD_DIR) $(filter %.c,$(SYNC_SOURCES)) -o $(SYNC_TARGET) $(shell pkg-config --cflags --libs libcurl)

-include $(DEPS)

# Headless tests: no raylib, no libcurl, no network. Update now runs
# tools/fake-update.sh (GHOST_LAUNCHER_UPDATER), never git or install.sh.
test: | $(BUILD_DIR)
	$(CC) -std=c17 -O0 -g $(WARN_FLAGS) -I$(COMMON_DIR) -Isync tests/test_main.c tests/test_kit.c tests/test_settings.c tests/test_hall.c tests/test_row.c tests/test_page.c tests/test_extras.c tests/test_update.c $(COMMON_DIR)/achievements.c $(COMMON_DIR)/daily.c $(COMMON_DIR)/tuning.c $(COMMON_DIR)/safefile.c $(COMMON_DIR)/howto.c $(COMMON_DIR)/prefs.c $(COMMON_DIR)/song.c $(COMMON_DIR)/musicsynth.c sync/syncdata.c sync/updatecheck.c src/scores.c src/profile.c src/manifest.c src/launch.c src/stats.c src/runstats.c src/adminlock.c src/sha256.c src/input.c src/launcherini.c src/prefswatch.c src/kit.c src/nav.c src/screens.c src/keymap.c src/textfield.c src/settings.c src/hall.c src/nameboard.c src/lineup.c src/hallmusic.c src/migrate.c src/boards.c src/machines.c src/gameform.c src/gamepage.c src/desk.c src/attract.c src/jukebox.c src/selfupdate.c -o $(TEST_TARGET) -lm
	./$(TEST_TARGET)

# Screenshots without a visible window (tools/shots/driver.c): the real
# launcher in a hidden window, scripted input, no audio, no game launches,
# a temp HOME. `make shots` runs every script in tools/shots/scripts and
# writes the PNGs to SHOTS_OUT; SHOTS_SCRIPTS picks other scripts.
# `make shots-asan` runs the same under ASan/UBSan (its own binary, so no
# rebuild dance), and `make monkey` throws MONKEY_RUNS random scripts of
# MONKEY_FRAMES frames each (keys, pad, mouse, focus, game exits) at that
# build. The wrap list is every __wrap_ function the driver defines.
SHOTS_BIN := $(BUILD_DIR)/shots/ghost-launcher-shots
SHOTS_ASAN_BIN := $(BUILD_DIR)/shots-asan/ghost-launcher-shots
SHOTS_OUT ?= $(BUILD_DIR)/shots/png
SHOTS_SCRIPTS ?= $(wildcard tools/shots/scripts/*.txt)
MONKEY_OUT ?= $(BUILD_DIR)/shots/monkey
MONKEY_RUNS ?= 10
MONKEY_FRAMES ?= 2000
MONKEY_SEED ?= 1
SHOTS_SOURCES := $(filter-out $(SRC_DIR)/main.c $(SRC_DIR)/launch.c,$(SOURCES)) \
	tools/shots/main_wrap.c tools/shots/driver.c tools/shots/fake_launch.c tools/shots/gallery.c \
	$(COMMON_DIR)/prefs.c $(COMMON_DIR)/achievements.c $(COMMON_DIR)/winscale.c $(COMMON_DIR)/safefile.c $(COMMON_DIR)/howto.c \
	$(COMMON_DIR)/music.c $(COMMON_DIR)/musicsynth.c $(COMMON_DIR)/song.c $(COMMON_DIR)/daily.c \
	sync/syncdata.c sync/updatecheck.c
SHOTS_WRAPS = $(shell grep -o '__wrap_[A-Za-z0-9_]*' tools/shots/driver.c | sed 's/__wrap_//' | sort -u)
comma := ,

SHOTS_SAN :=
$(SHOTS_ASAN_BIN): SHOTS_SAN := -fsanitize=address,undefined -fno-sanitize-recover=undefined
$(SHOTS_BIN) $(SHOTS_ASAN_BIN): $(SHOTS_SOURCES) $(SRC_DIR)/main.c $(BUILD_DIR)/build_commit.h \
		$(wildcard $(SRC_DIR)/*.h tools/shots/*.h sync/*.h $(COMMON_DIR)/*.h)
	@mkdir -p $(dir $@)
	$(CC) -std=c17 -O1 -g $(SHOTS_SAN) $(WARN_FLAGS) -I$(COMMON_DIR) -I$(SRC_DIR) -Isync -Itools/shots -I$(BUILD_DIR) \
		-DADMIN_LOCK_HASH='"$(ADMIN_LOCK_HASH)"' $(RAYLIB_CFLAGS) $(SHOTS_SOURCES) -o $@ \
		$(addprefix -Wl$(comma)--wrap=,$(SHOTS_WRAPS)) $(RAYLIB_LIBS) -lm

shots: $(SHOTS_BIN)
	tools/shots/run.sh $(SHOTS_BIN) $(SHOTS_OUT) $(SHOTS_SCRIPTS)

shots-asan: $(SHOTS_ASAN_BIN)
	tools/shots/run.sh $(SHOTS_ASAN_BIN) $(SHOTS_OUT) $(SHOTS_SCRIPTS)

monkey: $(SHOTS_ASAN_BIN)
	tools/shots/monkey.sh $(SHOTS_ASAN_BIN) $(MONKEY_OUT) $(MONKEY_RUNS) $(MONKEY_FRAMES) $(MONKEY_SEED)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR)

install: all
	install -Dm755 $(TARGET) $(BIN_DIR)/ghost-launcher
	install -Dm755 $(SYNC_TARGET) $(BIN_DIR)/ghost-sync
	mkdir -p $(DATA_DIR)
	cp -r assets $(DATA_DIR)/
	install -Dm644 assets/icons/ghost-launcher.png $(ICON_DIR)/ghost-launcher.png
	install -Dm644 packaging/ghost-launcher.desktop $(APPS_DIR)/ghost-launcher.desktop
	-update-desktop-database $(APPS_DIR) 2>/dev/null
	-gtk-update-icon-cache $(HOME)/.local/share/icons/hicolor 2>/dev/null
	@echo "Installed. Launch with: ghost-launcher  (ensure $(BIN_DIR) is in PATH)"

uninstall:
	rm -f $(BIN_DIR)/ghost-launcher
	rm -f $(BIN_DIR)/ghost-sync
	# Only the installed assets: $(DATA_DIR) also holds the player's save data
	# and scores, which an uninstall must never delete.
	rm -rf $(DATA_DIR)/assets
	-rmdir $(DATA_DIR) 2>/dev/null
	rm -f $(ICON_DIR)/ghost-launcher.png
	rm -f $(APPS_DIR)/ghost-launcher.desktop
