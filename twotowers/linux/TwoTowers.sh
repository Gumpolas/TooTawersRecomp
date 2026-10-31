#!/bin/sh
# The Two Towers Recompiled -- start the game on Linux.
#
# First run: if the game files are not installed yet, asks for your disc
# image (The Two Towers, USA, Xbox) and extracts it into game_files/ --
# with a file dialog when zenity is installed, otherwise in a terminal.
# Then starts the game. Arguments are passed on to it.
#
#   TwoTowers.sh --launcher   the launcher first: mods (extra heroes, ECTS 2002
#                             HUD, orange health), language, full screen, frame
#                             rate, button prompts -- then Play. F5 on the
#                             game's main menu comes back here too.

cd "$(dirname "$(readlink -f "$0")")" || exit 1
TITLE="The Two Towers Recompiled"
LAUNCHER=0
[ "$1" = "--launcher" ] && { LAUNCHER=1; shift; }
export TT_LAUNCHED=1   # (the game: started from here, so it does not start this script again when its files are missing)

have_gui() { [ -n "$DISPLAY$WAYLAND_DISPLAY" ] && command -v zenity >/dev/null 2>&1; }
# a message in a window without zenity: KDE's kdialog, else the game's own (SDL's message box)
win_msg() {
    [ -n "$DISPLAY$WAYLAND_DISPLAY" ] || return 1
    if command -v kdialog >/dev/null 2>&1 && kdialog --title "$TITLE" --error "$1" 2>/dev/null; then return 0; fi
    # the game's own box (straight on X11, else SDL's); xmessage (x11-utils) when that could not open one
    if [ -x ./twotowers_recomp ]; then
        out=$(TT_MSGBOX="$1" ./twotowers_recomp 2>&1 >/dev/null | grep '^(')
        [ -n "$out" ] && printf '%s\n' "$out" >&2
        case "$out" in *"shown in a window"*) return 0;; esac
    fi
    if command -v xmessage >/dev/null 2>&1; then printf '%s\n' "$1" | xmessage -center -title "$TITLE" -buttons OK:0 -file - 2>/dev/null; return 0; fi
    echo "(no window could be opened for this message)" >&2
    return 0
}
say() {
    if have_gui; then zenity --info --title="$TITLE" --width=420 --text="$1" 2>/dev/null
    else printf '%s\n' "$1" >&2; fi
}
fail() {
    if have_gui; then zenity --error --title="$TITLE" --width=460 --text="$1" 2>/dev/null
    else printf '%s\n' "$1" >&2; [ -t 2 ] || win_msg "$1"; fi
    exit 1
}

# (1.9.9) the launcher: the Windows launcher's settings, for the AppImage and the Linux package (a window with zenity or
# kdialog, else twotowers-setup's menu in a terminal). Returns when Play is chosen; Quit ends the script.
get() { v=$(./twotowers-setup get "$1" "$2"); echo "${v:-$3}"; }
onoff() { [ "$1" = "1" ] && echo "On" || echo "Off"; }
lang_name() {
    [ "$1" = "en" ] && { echo "English"; return; }
    n=$(sed -n 's/^name=//p' "lang/$1/info.txt" 2>/dev/null | head -n 1); echo "${n:-$1}"
}
pick() {   # pick <text> <tag> <label> ... : the tag chosen
    t=$1; shift
    if have_gui; then
        zenity --list --title="$TITLE" --text="$t" --column=id --column=" " --hide-column=1 --print-column=1 --hide-header \
               --width=560 --height=440 --ok-label=Select --cancel-label=Quit "$@" 2>/dev/null
    else
        kdialog --title "$TITLE" --menu "$t" "$@" 2>/dev/null
    fi
}
launcher() {
    if ! have_gui && ! { [ -n "$DISPLAY$WAYLAND_DISPLAY" ] && command -v kdialog >/dev/null 2>&1; }; then
        if [ -t 0 ]; then ./twotowers-setup; exit $?; fi   # (its menu: 9 plays)
        command -v x-terminal-emulator >/dev/null 2>&1 && exec x-terminal-emulator -e ./twotowers-setup
        fail "The launcher needs zenity (or kdialog) to open in a window:
  sudo apt install zenity
Or open a terminal in this folder:
  $(pwd)
and run:  ./twotowers-setup"
    fi
    while :; do
        eh=$(get Mods extra_heroes 0)
        case "$eh" in 1) ehn="On (unlock them by playing)";; 2) ehn="On (all unlocked, for testing)";; *) ehn="Off";; esac
        fps=$(get Display fps 30); lg=$(get Language language en)
        c=$(pick "Choose a setting to change it, then Play." \
            play "▶  Play" \
            eh "Extra heroes mod (Boromir, Gandalf, Frodo, Lurtz, Elrond, Uruk-hai, Sauron):  $ehn" \
            hud "ECTS 2002 HUD mod:  $(onoff "$(get Mods e3_hud 0)")" \
            pk "Orange health pickups (ECTS 2002):  $(onoff "$(get Mods e3_pickups 0)")" \
            lang "Language:  $(lang_name "$lg")" \
            fs "Full screen at start:  $(onoff "$(get Display fullscreen 0)")" \
            fps "Frame rate:  $fps  (above 30 is experimental)" \
            pr "Button prompts:  $(get Input prompts auto)") || exit 0
        case "$c" in
            play|"") return 0;;
            eh) case "$eh" in 0) n=1;; 1) n=2;; *) n=0;; esac; ./twotowers-setup set Mods extra_heroes $n;;
            hud) [ "$(get Mods e3_hud 0)" = "1" ] && n=0 || n=1; ./twotowers-setup set Mods e3_hud $n;;
            pk) [ "$(get Mods e3_pickups 0)" = "1" ] && n=0 || n=1; ./twotowers-setup set Mods e3_pickups $n;;
            fs) [ "$(get Display fullscreen 0)" = "1" ] && n=0 || n=1; ./twotowers-setup set Display fullscreen $n;;
            fps) n=30; set -- 30 60 75 90 100 120 144 165 200 240 300 unlocked 30
                 while [ $# -gt 1 ]; do [ "$1" = "$fps" ] && { n=$2; break; }; shift; done
                 ./twotowers-setup set Display fps $n;;
            pr) case "$(get Input prompts auto)" in auto) n=xbox;; xbox) n=xboxone;; xboxone) n=playstation;; playstation) n=keyboard;; *) n=auto;; esac
                ./twotowers-setup set Input prompts $n;;
            lang) set -- en "English"
                  for d in lang/*/info.txt; do [ -f "$d" ] || continue; k=${d#lang/}; k=${k%/info.txt}; set -- "$@" "$k" "$(lang_name "$k")"; done
                  n=$(pick "Language" "$@") && [ -n "$n" ] && ./twotowers-setup set Language language "$n";;
        esac
    done
}

# SDL2 (window, sound, controllers): from the system, or lib/ beside the game
if ldd ./twotowers_recomp 2>/dev/null | grep -q "libSDL2.*not found"; then
    fail "SDL2 is not installed. Install it and start the game again:

  Ubuntu / Debian / Mint:  sudo apt install libsdl2-2.0-0
  Fedora:                  sudo dnf install SDL2
  Arch / Manjaro:          sudo pacman -S sdl2
  openSUSE:                sudo zypper install libSDL2-2_0-0"
fi

if ! ./twotowers-setup status -q; then
    if have_gui; then
        iso=$(zenity --file-selection --title="Select your The Two Towers (USA) Xbox disc image" \
              --file-filter="Xbox disc image (*.iso *.xiso) | *.iso *.ISO *.xiso" --file-filter="All files | *" 2>/dev/null) || exit 0
        log=$(mktemp)
        ./twotowers-setup install "$iso" 2>"$log" \
            | sed -u -n 's/^PROGRESS \(.*\)/\1/p; s/^\(# .*\)/\1/p' \
            | zenity --progress --title="$TITLE" --text="Checking the disc..." --percentage=0 --auto-close --width=460 2>/dev/null
        if ! ./twotowers-setup status -q; then
            msg=$(cat "$log"); rm -f "$log"
            fail "The game files could not be installed.

${msg:-Extraction was cancelled.}"
        fi
        rm -f "$log"
        LAUNCHER=1   # (the first time: the launcher, where the mods are)
        # (an AppImage: the menu entry is the AppImage's own, through AppImageLauncher or the like)
        if [ -z "$TT_APPIMAGE_HOME" ] && zenity --question --title="$TITLE" --width=420 \
               --text="The game files are installed.\n\nAdd The Two Towers to your applications menu?" 2>/dev/null; then
            ./twotowers-setup desktop >/dev/null
        fi
    elif [ -t 0 ]; then
        ./twotowers-setup || exit 1
        ./twotowers-setup status -q || exit 1
    elif command -v x-terminal-emulator >/dev/null 2>&1; then
        exec x-terminal-emulator -e "$0" "$@"
    else
        # (1.9.9: worded as the welcome it is -- the AppImage catalog's test reads the screen and takes "not installed" for an error)
        fail "Welcome to The Two Towers Recompiled!

Before the first game, the game's files are copied from your own disc image (The Two Towers, USA, Xbox ISO). Open a terminal in this folder:
  $(pwd)
and run:
  ./twotowers-setup install /path/to/TheTwoTowers.iso

Tip: with zenity on your system (sudo apt install zenity), the game asks for your disc image in a window of its own."
    fi
fi

[ "$LAUNCHER" = 1 ] && launcher

# Ultra textures on, and the GLOS69 pack put here but not made ready yet:
# made ready once (a few minutes)
if [ "$(./twotowers-setup get Display ultra_textures)" = "1" ] && ./twotowers-setup ultra-needed; then
    if have_gui; then
        ./twotowers-setup ultra 2>/dev/null | sed -u -n 's/^PROGRESS \(.*\)/\1/p; s/^\(# .*\)/\1/p' \
            | zenity --progress --title="$TITLE" --text="Building the Ultra textures (once)..." --percentage=0 --auto-close --width=460 2>/dev/null
    else
        ./twotowers-setup ultra | sed -u -n 's/^# //p'
    fi
fi

exec ./twotowers_recomp "$@"
