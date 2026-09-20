CC := gcc
SRC_DIR := src
BUILD_DIR := build
TEST_DIR := tests
# Code shared by every Ghost Arcade game (rng, the launcher data contract, ...).
COMMON_DIR ?= ../ghost-common
COMMON_MODULES := rng ghostlink keynames sfx ui musicsynth music prefs gamepad runlog achievements winscale tuning tuningui safefile howto

TARGET := $(BUILD_DIR)/coilrush
TEST_TARGET := $(BUILD_DIR)/coilrush-tests

RAYLIB_CFLAGS := $(shell pkg-config --cflags raylib)
RAYLIB_LIBS := $(shell pkg-config --libs raylib)

WARN_FLAGS := -Wall -Wextra -Wno-unused-parameter
INCLUDES := -I$(SRC_DIR) -I$(COMMON_DIR)
CFLAGS := -std=c17 -O2 -flto $(WARN_FLAGS) -MMD -MP
LDFLAGS := -flto

SOURCES := $(wildcard $(SRC_DIR)/*.c)
OBJECTS := $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o) $(COMMON_MODULES:%=$(BUILD_DIR)/%.o)
DEPS := $(OBJECTS:.o=.d)

CORE_SOURCES := $(COMMON_DIR)/rng.c $(SRC_DIR)/game.c $(COMMON_DIR)/ghostlink.c $(COMMON_DIR)/safefile.c
TEST_SOURCES := $(TEST_DIR)/test_main.c $(CORE_SOURCES)

PREFIX ?= $(HOME)/.local
BIN_DIR := $(PREFIX)/bin
DATA_DIR := $(PREFIX)/share/coilrush
ICON_DIR := $(HOME)/.local/share/icons/hicolor/256x256/apps
APPS_DIR := $(HOME)/.local/share/applications

.PHONY: all run test clean install uninstall debug

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
test: | $(BUILD_DIR)
	$(CC) -std=c17 -O0 -g $(WARN_FLAGS) $(INCLUDES) $(TEST_SOURCES) -o $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -rf $(BUILD_DIR)

install: all
	install -Dm755 $(TARGET) $(BIN_DIR)/coilrush
	mkdir -p $(DATA_DIR)
	cp -r assets $(DATA_DIR)/
	install -Dm644 assets/icons/coilrush.png $(ICON_DIR)/coilrush.png
	install -Dm644 packaging/coilrush.desktop $(APPS_DIR)/coilrush.desktop
	-update-desktop-database $(APPS_DIR) 2>/dev/null
	-gtk-update-icon-cache $(HOME)/.local/share/icons/hicolor 2>/dev/null
	@echo "Installed. Launch with: coilrush  (ensure $(BIN_DIR) is in PATH)"

uninstall:
	rm -f $(BIN_DIR)/coilrush
	# Only the installed assets: $(DATA_DIR) also holds the player's save data
	# and scores, which an uninstall must never delete.
	rm -rf $(DATA_DIR)/assets
	-rmdir $(DATA_DIR) 2>/dev/null
	rm -f $(ICON_DIR)/coilrush.png
	rm -f $(APPS_DIR)/coilrush.desktop
