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
LDFLAGS := -flto

# The arcade-wide preferences (reduced flashing, ...) come from ghost-common.
COMMON_DIR ?= ../ghost-common
CFLAGS += -I$(COMMON_DIR)

SOURCES := $(wildcard $(SRC_DIR)/*.c)
OBJECTS := $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o) $(BUILD_DIR)/prefs.o $(BUILD_DIR)/gamepad.o $(BUILD_DIR)/achievements.o $(BUILD_DIR)/winscale.o $(BUILD_DIR)/syncdata.o $(BUILD_DIR)/updatecheck.o $(BUILD_DIR)/safefile.o
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

.PHONY: all run test clean install uninstall admin-lock admin-unlock

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

$(BUILD_DIR)/gamepad.o: $(COMMON_DIR)/gamepad.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -c $< -o $@

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

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $(TARGET) $(RAYLIB_LIBS) -lm

# The commit this build was made from, so ghost-sync can tell when GitHub has
# something newer. Rewritten only when it changes; empty outside a git checkout.
BUILD_COMMIT := $(shell git -C $(CURDIR) rev-parse HEAD 2>/dev/null)
BUILD_SOURCE_DIR := $(shell git -C $(CURDIR) rev-parse --show-toplevel 2>/dev/null)
$(BUILD_DIR)/build_commit.h: FORCE | $(BUILD_DIR)
	@printf '#define BUILD_COMMIT "%s"\n#define BUILD_SOURCE_DIR "%s"\n' "$(BUILD_COMMIT)" "$(BUILD_SOURCE_DIR)" > $@.new
	@if cmp -s $@.new $@; then rm -f $@.new; else mv $@.new $@; fi
FORCE:

$(BUILD_DIR)/main.o: $(BUILD_DIR)/build_commit.h
$(BUILD_DIR)/main.o: CFLAGS += -I$(BUILD_DIR)

$(SYNC_TARGET): $(SYNC_SOURCES) sync/syncdata.h sync/updatecheck.h sync/online_config.h $(BUILD_DIR)/build_commit.h | $(BUILD_DIR)
	$(CC) -std=c17 -O2 $(WARN_FLAGS) -I$(COMMON_DIR) -Isync -I$(BUILD_DIR) $(filter %.c,$(SYNC_SOURCES)) -o $(SYNC_TARGET) $(shell pkg-config --cflags --libs libcurl)

-include $(DEPS)

# Headless tests: no raylib, no libcurl, no network.
test: | $(BUILD_DIR)
	$(CC) -std=c17 -O0 -g $(WARN_FLAGS) -I$(COMMON_DIR) -Isync tests/test_main.c $(COMMON_DIR)/achievements.c $(COMMON_DIR)/daily.c $(COMMON_DIR)/tuning.c $(COMMON_DIR)/safefile.c $(COMMON_DIR)/howto.c $(COMMON_DIR)/prefs.c sync/syncdata.c sync/updatecheck.c src/scores.c src/profile.c src/manifest.c src/launch.c src/stats.c src/runstats.c src/adminlock.c src/sha256.c -o $(TEST_TARGET)
	./$(TEST_TARGET)

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
