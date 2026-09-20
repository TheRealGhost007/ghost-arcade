#!/usr/bin/env bash
# Ghost Arcade installer: builds the eleven games and Ghost Launcher from this
# checkout and installs them for the current user (~/.local). No root needed,
# and nothing outside your home folder is touched.
#
#   ./install.sh              build, test and install everything
#   ./install.sh --update     git pull first, then the same
#   ./install.sh --uninstall  remove the programs (your saves and scores stay)
#   ./install.sh --yes        do not ask before installing missing packages
#   ./install.sh --skip-tests install without running the test suites
set -euo pipefail

cd "$(dirname "$(readlink -f "$0")")"

MODE=install
ASSUME_YES=0
RUN_TESTS=1
for arg in "$@"; do
    case "$arg" in
        --update) MODE=update ;;
        --uninstall) MODE=uninstall ;;
        --yes|-y) ASSUME_YES=1 ;;
        --skip-tests) RUN_TESTS=0 ;;
        --help|-h) sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "install.sh: unknown option '$arg' (try --help)" >&2; exit 2 ;;
    esac
done

say()  { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m!!\033[0m  %s\n' "$*" >&2; }
die()  { printf '\033[1;31mxx\033[0m  %s\n' "$*" >&2; exit 1; }

[ "$(id -u)" -ne 0 ] || die "Run this as your normal user, not root: it installs into your own ~/.local."
[ -f ghost-launcher/Makefile ] && [ -d ghost-common ] || die "Run this from inside the ghost-arcade folder."

if [ "$MODE" = uninstall ]; then
    say "Removing Ghost Arcade programs (saves, scores and settings are kept)"
    make -s uninstall
    say "Done. To remove your data too: rm -rf ~/.local/share/{ghost-launcher,blockfall,coilrush,brickburst,skyraid,ghostmaze,rockdrift,lanehop,crawlshot,moondrop,gemdive,girderclimb} ~/.config/{ghost-launcher,ghost-arcade}"
    exit 0
fi

# ---------------------------------------------------------------- dependencies
missing=()
for tool in gcc make pkg-config; do command -v "$tool" >/dev/null 2>&1 || missing+=("$tool"); done
if command -v pkg-config >/dev/null 2>&1; then
    pkg-config --exists raylib  || missing+=("raylib")
    pkg-config --exists libcurl || missing+=("libcurl")
else
    missing+=("raylib" "libcurl")
fi

if [ "${#missing[@]}" -gt 0 ]; then
    warn "Missing: ${missing[*]}"
    if command -v pacman >/dev/null 2>&1; then
        cmd="sudo pacman -S --needed base-devel raylib curl git"
        echo "    On Arch / Omarchy / Manjaro / EndeavourOS this installs them:"
        echo "        $cmd"
        if [ "$ASSUME_YES" -eq 1 ]; then answer=y
        elif [ -t 0 ]; then read -r -p "    Run that now? [y/N] " answer
        else answer=n; fi
        case "$answer" in
            y|Y|yes) $cmd ;;
            *) die "Install those packages, then run ./install.sh again." ;;
        esac
    else
        cat >&2 <<'EOF'
    Install a C compiler, make, pkg-config, libcurl's development files and raylib 6.
      Debian/Ubuntu:  sudo apt install build-essential pkg-config libcurl4-openssl-dev git cmake \
                          libasound2-dev libx11-dev libxrandr-dev libxi-dev libgl1-mesa-dev \
                          libxcursor-dev libxinerama-dev libwayland-dev libxkbcommon-dev
      Fedora:         sudo dnf install gcc make pkgconf-pkg-config libcurl-devel git cmake \
                          alsa-lib-devel libX11-devel libXrandr-devel libXi-devel mesa-libGL-devel \
                          libXcursor-devel libXinerama-devel wayland-devel libxkbcommon-devel
    Those distributions do not package raylib 6 yet, so build it once from source:
      git clone --depth 1 --branch 6.0 https://github.com/raysan5/raylib.git
      cmake -S raylib -B raylib/build -DBUILD_SHARED_LIBS=ON -DBUILD_EXAMPLES=OFF
      cmake --build raylib/build -j && sudo cmake --install raylib/build && sudo ldconfig
EOF
        die "Then run ./install.sh again."
    fi
fi

# ----------------------------------------------------------------------- build
if [ "$MODE" = update ]; then
    command -v git >/dev/null 2>&1 || die "--update needs git."
    [ -d .git ] || die "--update needs a git checkout (git clone, not a downloaded zip)."
    say "Fetching the newest version"
    git pull --ff-only
fi

jobs=$(nproc 2>/dev/null || echo 2)
say "Building eleven games and the launcher ($jobs jobs)"
make -s -j"$jobs"

if [ "$RUN_TESTS" -eq 1 ]; then
    say "Running the test suites"
    make -s test
fi

say "Installing into ~/.local"
make -s install >/dev/null

# ----------------------------------------------------------------------- after
case ":$PATH:" in
    *":$HOME/.local/bin:"*) ;;
    *) warn "$HOME/.local/bin is not on your PATH. Add this to your shell profile:"
       echo '        export PATH="$HOME/.local/bin:$PATH"' ;;
esac
[ -d .git ] || warn "This is not a git checkout, so the launcher cannot tell you about updates. 'git clone' gives you that."

say "Installed. Start the arcade with:  ghost-launcher   (or find Ghost Launcher in your app menu)"
echo "    Update later with:  ./install.sh --update      Remove with:  ./install.sh --uninstall"
