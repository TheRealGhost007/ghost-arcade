# Ghost Arcade: build, test or install everything from one place.
#   make            build all eleven games and the launcher
#   make test       run every headless test suite (no raylib or display needed)
#   make install    install everything into ~/.local
#   make clean
GAMES := blockfall coilrush brickburst skyraid ghostmaze rockdrift lanehop crawlshot moondrop gemdive girderclimb
PROJECTS := $(GAMES) ghost-launcher

.PHONY: all test install uninstall clean $(PROJECTS)

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

install uninstall clean:
	@for p in $(PROJECTS); do $(MAKE) -s -C $$p $@ || exit 1; done
