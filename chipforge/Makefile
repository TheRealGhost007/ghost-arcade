CC := gcc
SRC_DIR := src
BUILD_DIR := build
TEST_DIR := tests
# Code shared by every Ghost Arcade game (rng, the launcher data contract, ...).
COMMON_DIR ?= ../ghost-common
COMMON_MODULES := ghostlink keynames keynames_check sfx ui musicsynth song prefs winscale safefile howto achievements tuning daily

TARGET := $(BUILD_DIR)/chipforge
TEST_TARGET := $(BUILD_DIR)/chipforge-tests
IMPORT_TARGET := $(BUILD_DIR)/chipforge-import
IMPORT_SOURCES := tools/import_cli.c $(SRC_DIR)/transcribe.c $(SRC_DIR)/browser.c $(COMMON_DIR)/song.c $(COMMON_DIR)/musicsynth.c $(COMMON_DIR)/safefile.c
# The importer is built in one gcc call without -MMD, so the headers its
# sources include are listed by hand; otherwise a header edit (NOTE_MAX, the
# TranscribeOptions layout) leaves a stale chipforge-import to be installed.
IMPORT_HEADERS := $(SRC_DIR)/transcribe.h $(SRC_DIR)/browser.h $(COMMON_DIR)/song.h $(COMMON_DIR)/musicsynth.h $(COMMON_DIR)/safefile.h

RAYLIB_CFLAGS := $(shell pkg-config --cflags raylib)
RAYLIB_LIBS := $(shell pkg-config --libs raylib)

WARN_FLAGS := -Wall -Wextra -Wno-unused-parameter
INCLUDES := -I$(SRC_DIR) -I$(COMMON_DIR)
CFLAGS := -std=c17 -O2 -flto $(WARN_FLAGS) -MMD -MP
LDFLAGS := -flto

SOURCES := $(wildcard $(SRC_DIR)/*.c)
OBJECTS := $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o) $(COMMON_MODULES:%=$(BUILD_DIR)/%.o)
DEPS := $(OBJECTS:.o=.d)

CORE_SOURCES := $(COMMON_DIR)/song.c $(COMMON_DIR)/musicsynth.c $(COMMON_DIR)/safefile.c $(SRC_DIR)/editor.c $(SRC_DIR)/transcribe.c $(SRC_DIR)/browser.c $(SRC_DIR)/compose.c $(SRC_DIR)/recipe.c $(SRC_DIR)/keymode.c $(SRC_DIR)/pick.c $(SRC_DIR)/arrange.c $(SRC_DIR)/take.c $(SRC_DIR)/jam.c $(SRC_DIR)/appini.c
TEST_SOURCES := $(TEST_DIR)/test_main.c $(CORE_SOURCES)

PREFIX ?= $(HOME)/.local
BIN_DIR := $(PREFIX)/bin
DATA_DIR := $(PREFIX)/share/chipforge
ICON_DIR := $(HOME)/.local/share/icons/hicolor/256x256/apps
APPS_DIR := $(HOME)/.local/share/applications

.PHONY: all run test clean install uninstall debug import-cli

all: $(TARGET) $(IMPORT_TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) $(RAYLIB_CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(COMMON_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) $(RAYLIB_CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $(TARGET) $(RAYLIB_LIBS) -lm

-include $(DEPS)

# The command-line importer: the app's decode + transcribe path without a window.
import-cli: $(IMPORT_TARGET)

$(IMPORT_TARGET): $(IMPORT_SOURCES) $(IMPORT_HEADERS) | $(BUILD_DIR)
	$(CC) -std=c17 -O2 $(WARN_FLAGS) $(INCLUDES) $(RAYLIB_CFLAGS) $(IMPORT_SOURCES) -o $(IMPORT_TARGET) $(RAYLIB_LIBS) -lm

debug: CFLAGS := -std=c17 -O0 -g -fsanitize=address,undefined $(WARN_FLAGS) -MMD -MP
debug: LDFLAGS := -fsanitize=address,undefined
debug: clean $(TARGET)

run: $(TARGET)
	./$(TARGET)

# Core game-logic tests build without raylib so they run headless/CI-friendly.
test: | $(BUILD_DIR)
	$(CC) -std=c17 -O0 -g $(WARN_FLAGS) $(INCLUDES) $(TEST_SOURCES) -o $(TEST_TARGET) -lm
	./$(TEST_TARGET)

clean:
	rm -rf $(BUILD_DIR)

install: all
	install -Dm755 $(TARGET) $(BIN_DIR)/chipforge
	install -Dm755 $(IMPORT_TARGET) $(BIN_DIR)/chipforge-import
	mkdir -p $(DATA_DIR)
	cp -r assets $(DATA_DIR)/
	install -Dm644 assets/icons/chipforge.png $(ICON_DIR)/chipforge.png
	install -Dm644 packaging/chipforge.desktop $(APPS_DIR)/chipforge.desktop
	-update-desktop-database $(APPS_DIR) 2>/dev/null
	-gtk-update-icon-cache $(HOME)/.local/share/icons/hicolor 2>/dev/null
	@echo "Installed. Launch with: chipforge  (ensure $(BIN_DIR) is in PATH)"

uninstall:
	rm -f $(BIN_DIR)/chipforge $(BIN_DIR)/chipforge-import
	# Only the installed assets: $(DATA_DIR) also holds the player's save data
	# and scores, which an uninstall must never delete.
	rm -rf $(DATA_DIR)/assets
	-rmdir $(DATA_DIR) 2>/dev/null
	rm -f $(ICON_DIR)/chipforge.png
	rm -f $(APPS_DIR)/chipforge.desktop
