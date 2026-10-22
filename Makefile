# Ghost Arcade: build, test or install everything from one place.
#   make            build all six games, Pond Kings, Chipforge (the music maker) and the launcher
#   make test       run every headless test suite (no raylib or display needed)
#   make install    install everything into ~/.local
#   make clean
GAMES := blockfall brickburst skyraid rockdrift crawlshot moondrop pondkings
PROJECTS := $(GAMES) chipforge ghost-launcher
# Games that have left the arcade (2026-10-21). Every install and uninstall
# removes their programs, menu entries, icons and installed assets, as their
# own `make uninstall` did; their saves and scores stay. It lives here, not in
# install.sh, because an update runs the old install.sh but this new Makefile.
RETIRED := coilrush ghostmaze lanehop gemdive girderclimb
PREFIX ?= $(HOME)/.local
APPS_DIR := $(HOME)/.local/share/applications
ICON_DIR := $(HOME)/.local/share/icons/hicolor/256x256/apps

.PHONY: all test install uninstall clean retire $(PROJECTS)

all: $(PROJECTS)

$(PROJECTS):
	$(MAKE) -C $@

test:
	@fail=0; for p in $(PROJECTS); do \
		printf '%-16s' "$$p"; \
		out=$$($(MAKE) -s -C $$p test 2>&1); rc=$$?; \
		echo "$$out" | tail -n 1; \
		if [ $$rc -ne 0 ]; then fail=1; echo "$$out" | grep -E "FAIL|error" | head -n 20; fi; \
	done; exit $$fail

install uninstall:
	@for p in $(PROJECTS); do $(MAKE) -s -C $$p $@ || exit 1; done
	@$(MAKE) -s retire

clean:
	@for p in $(PROJECTS); do $(MAKE) -s -C $$p $@ || exit 1; done

retire:
	@gone=0; for g in $(RETIRED); do \
		if [ -e "$(PREFIX)/bin/$$g" ] || [ -e "$(APPS_DIR)/$$g.desktop" ] || [ -e "$(ICON_DIR)/$$g.png" ] || [ -d "$(PREFIX)/share/$$g/assets" ]; then \
			rm -f "$(PREFIX)/bin/$$g" "$(APPS_DIR)/$$g.desktop" "$(ICON_DIR)/$$g.png"; \
			rm -rf "$(PREFIX)/share/$$g/assets"; \
			rmdir "$(PREFIX)/share/$$g" 2>/dev/null || true; \
			echo "Removed $$g: it has left the arcade (its saves and scores stay)"; gone=1; \
		fi; \
	done; \
	if [ $$gone = 1 ]; then update-desktop-database "$(APPS_DIR)" 2>/dev/null || true; fi
