#!/bin/sh
# The Two Towers Recompiled -- start the game on macOS (double-click in Finder).
#
# First run: if the game files are not installed yet, asks for your disc
# image (The Two Towers, USA, Xbox) and extracts it into game_files/.
# Then starts the game. Arguments are passed on to it.

cd "$(dirname "$0")" || exit 1
TITLE="The Two Towers Recompiled"

say()  { osascript -e "display dialog \"$1\" with title \"$TITLE\" buttons {\"OK\"} default button 1" >/dev/null 2>&1 || printf '%s\n' "$1" >&2; }
fail() { osascript -e "display alert \"$TITLE\" message \"$1\" as critical" >/dev/null 2>&1 || printf '%s\n' "$1" >&2; exit 1; }

# the build is x86-64: on Apple silicon it runs through Rosetta 2
if [ "$(uname -m)" = "arm64" ] && ! /usr/bin/pgrep -q oahd; then
    fail "This build needs Rosetta 2 on Apple silicon. Install it with:  softwareupdate --install-rosetta"
fi

# SDL2 (window, sound, controllers): lib/ beside the game, or Homebrew's
if [ ! -e lib/libSDL2-2.0.0.dylib ] && [ ! -e /usr/local/lib/libSDL2-2.0.0.dylib ] && [ ! -e /opt/homebrew/lib/libSDL2-2.0.0.dylib ]; then
    fail "SDL2 was not found. Put libSDL2-2.0.0.dylib in the lib folder beside the game, or install it with Homebrew:  brew install sdl2"
fi

# a file downloaded from the internet is quarantined: the game's own files are not
xattr -dr com.apple.quarantine . 2>/dev/null

if ! ./twotowers-setup status -q; then
    iso=$(osascript -e 'POSIX path of (choose file with prompt "Select your The Two Towers (USA) Xbox disc image (.iso)")' 2>/dev/null) || exit 0
    ./twotowers-setup install "$iso" || fail "The game files could not be installed."
fi

# Ultra textures on, and the GLOS69 pack put here but not made ready yet:
# made ready once (a few minutes)
if [ "$(./twotowers-setup get Display ultra_textures)" = "1" ] && ./twotowers-setup ultra-needed; then
    echo "Building the Ultra textures from the GLOS69 pack (once; a few minutes)..."
    ./twotowers-setup ultra | sed -n 's/^# //p'
fi

exec ./twotowers_recomp "$@"
