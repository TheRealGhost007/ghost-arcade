#!/bin/bash
# fake-update.sh: stands in for ../install.sh --update wherever Update now
# must not really pull or install (the launcher's tests, trying the screen
# by hand). It prints install.sh's plain-text "==>" lines and touches
# nothing. Point the launcher at it with
#   GHOST_LAUNCHER_UPDATER=/path/to/tools/fake-update.sh ghost-launcher
# FAKE_UPDATE_FAIL=packages|local|network|build|test|install fails there;
# FAKE_UPDATE_SLEEP=SECONDS waits that long in each step (so Stop can be
# tried); FAKE_UPDATE_VERSION=SHA is the "installed" commit.
set -u
fail=${FAKE_UPDATE_FAIL-}
nap=${FAKE_UPDATE_SLEEP-0}
say() { printf '==> %s\n' "$*"; }
warn() { printf '!!  %s\n' "$*" >&2; }
die() { printf 'xx  %s\n' "$*" >&2; exit 1; }
step() { [ "$nap" = 0 ] || sleep "$nap"; }

if [ "$fail" = packages ]; then
    warn "Missing: raylib libcurl"
    die "Install those packages, then run ./install.sh again."
fi
say "Fetching the newest version"
step
[ "$fail" = local ] && die "Your copy has changes of its own (local edits or commits), so it was not updated."
[ "$fail" = network ] && die "Could not download the newest version (no network, or GitHub did not answer)."
say "Building the six games, Chipforge and Ghost Launcher (4 jobs)"
step
if [ "$fail" = build ]; then
    echo "src/main.c:12:5: error: expected ';' before 'return'" >&2
    echo "make[1]: *** [Makefile:70: build/main.o] Error 1" >&2
    exit 2
fi
say "Running the test suites"
step
if [ "$fail" = test ]; then
    echo "FAIL: update: the fake test that fails (tests/test_update.c:1)"
    echo "1400 checks, 1 failures"
    exit 2
fi
say "Installing into ~/.local"
step
if [ "$fail" = install ]; then
    echo "install: cannot create regular file '/nowhere/bin/ghost-launcher': Permission denied" >&2
    exit 2
fi
say "Version: ${FAKE_UPDATE_VERSION:-89abcdef0123456789abcdef0123456789abcdef}"
say "Installed. Start the arcade with:  ghost-launcher   (or find Ghost Launcher in your app menu)"
