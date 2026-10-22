CC := gcc
SRC_DIR := src
BUILD_DIR := build
TEST_DIR := tests
# Shared Ghost Arcade code: only the engine parts (sound, music, window
# scaling, preferences, saves). Pond Kings draws its own look, not ui.c's.
COMMON_DIR ?= ../ghost-common
COMMON_MODULES := rng sfx musicsynth song music prefs winscale safefile ghostlink runlog achievements daily keynames keynames_check

TARGET := $(BUILD_DIR)/pondkings
TEST_TARGET := $(BUILD_DIR)/pondkings-tests
SHOTS_TARGET := $(BUILD_DIR)/pondkings-shots

RAYLIB_CFLAGS := $(shell pkg-config --cflags raylib)
RAYLIB_LIBS := $(shell pkg-config --libs raylib)
# Online play (lobby.c, relay.c) talks to the arcade's server with libcurl on
# worker threads. The tests never link either.
CURL_CFLAGS := $(shell pkg-config --cflags libcurl)
CURL_LIBS := $(shell pkg-config --libs libcurl) -lpthread

WARN_FLAGS := -Wall -Wextra -Wno-unused-parameter
# online_config.h: the arcade server's address and publishable key, shared with ghost-sync
ONLINE_DIR ?= ../ghost-launcher/sync
INCLUDES := -I$(SRC_DIR) -I$(COMMON_DIR) -I$(ONLINE_DIR)
CFLAGS := -std=c17 -O2 -flto $(WARN_FLAGS) -MMD -MP
LDFLAGS := -flto

SOURCES := $(wildcard $(SRC_DIR)/*.c)
OBJECTS := $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o) $(COMMON_MODULES:%=$(BUILD_DIR)/%.o)
DEPS := $(OBJECTS:.o=.d)

# Everything that needs neither raylib nor libcurl: the rules, the Journey,
# and online play's packets, rollback, crews, sockets and server messages.
# The tests (and CI, which has only a compiler) build just these.
CORE_SOURCES := $(COMMON_DIR)/rng.c $(COMMON_DIR)/safefile.c $(SRC_DIR)/game.c $(SRC_DIR)/ai.c $(SRC_DIR)/journey.c $(SRC_DIR)/teams.c $(SRC_DIR)/wire.c $(SRC_DIR)/session.c $(SRC_DIR)/crews.c $(SRC_DIR)/net.c $(SRC_DIR)/netmsg.c
TEST_SOURCES := $(TEST_DIR)/test_main.c $(CORE_SOURCES)

PREFIX ?= $(HOME)/.local
BIN_DIR := $(PREFIX)/bin
DATA_DIR := $(PREFIX)/share/pondkings
ICON_DIR := $(HOME)/.local/share/icons/hicolor/256x256/apps
APPS_DIR := $(HOME)/.local/share/applications

.PHONY: all run test shots clean install uninstall debug

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) $(RAYLIB_CFLAGS) $(CURL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(COMMON_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) $(RAYLIB_CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $(TARGET) $(RAYLIB_LIBS) $(CURL_LIBS) -lm

-include $(DEPS)

debug: CFLAGS := -std=c17 -O0 -g -fsanitize=address,undefined $(WARN_FLAGS) -MMD -MP
debug: LDFLAGS := -fsanitize=address,undefined
debug: clean $(TARGET)

run: $(TARGET)
	./$(TARGET)

# Core tests build without raylib so they run headless and in CI.
test: | $(BUILD_DIR)
	$(CC) -std=c17 -O0 -g $(WARN_FLAGS) $(INCLUDES) $(TEST_SOURCES) -o $(TEST_TARGET)
	./$(TEST_TARGET)

# Pictures of the real screens from a hidden window, written to build/shots:
# the menus, the Journey, results and the online screens (tools/shots.c),
# and the venues, goalies, slapshots, breaks, replays, the shootout and the
# twists (tools/shots_venues.c). Nothing is shown on the desktop.
SHOTS_LIB := $(filter-out $(SRC_DIR)/main.c,$(SOURCES)) $(COMMON_MODULES:%=$(COMMON_DIR)/%.c)
SHOTS_VENUES_TARGET := $(BUILD_DIR)/pondkings-shots-venues
shots: | $(BUILD_DIR)
	$(CC) -std=c17 -O1 -g $(WARN_FLAGS) $(INCLUDES) $(RAYLIB_CFLAGS) $(CURL_CFLAGS) tools/shots.c $(SHOTS_LIB) -o $(SHOTS_TARGET) $(RAYLIB_LIBS) $(CURL_LIBS) -lm
	$(CC) -std=c17 -O1 -g $(WARN_FLAGS) $(INCLUDES) $(RAYLIB_CFLAGS) $(CURL_CFLAGS) tools/shots_venues.c $(SHOTS_LIB) -o $(SHOTS_VENUES_TARGET) $(RAYLIB_LIBS) $(CURL_LIBS) -lm
	mkdir -p $(BUILD_DIR)/shots
	./$(SHOTS_TARGET) $(BUILD_DIR)/shots
	./$(SHOTS_VENUES_TARGET) $(BUILD_DIR)/shots

clean:
	rm -rf $(BUILD_DIR)

install: all
	install -Dm755 $(TARGET) $(BIN_DIR)/pondkings
	mkdir -p $(DATA_DIR)
	cp -r assets $(DATA_DIR)/
	install -Dm644 assets/icons/pondkings.png $(ICON_DIR)/pondkings.png
	install -Dm644 packaging/pondkings.desktop $(APPS_DIR)/pondkings.desktop
	-update-desktop-database $(APPS_DIR) 2>/dev/null
	-gtk-update-icon-cache $(HOME)/.local/share/icons/hicolor 2>/dev/null
	@echo "Installed. Launch with: pondkings  (ensure $(BIN_DIR) is in PATH)"

uninstall:
	rm -f $(BIN_DIR)/pondkings
	# Only the installed assets: $(DATA_DIR) also holds the player's save data,
	# which an uninstall must never delete.
	rm -rf $(DATA_DIR)/assets
	-rmdir $(DATA_DIR) 2>/dev/null
	rm -f $(ICON_DIR)/pondkings.png
	rm -f $(APPS_DIR)/pondkings.desktop
