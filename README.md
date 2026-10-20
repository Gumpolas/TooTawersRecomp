# The Two Towers Recompiled

An unofficial native Windows (x64) port of **The Lord of the Rings: The Two Towers** for the original Xbox (USA retail release). It was made by *static recompilation*: the game's own executable is translated into C and runs natively, with no emulator in between. It uses the [xboxrecomp](#credits) toolkit.

> [!IMPORTANT]
> **No game data is included anywhere: not in this repository and not in the release build.** To play, you need your own legally obtained copy of the original game: the USA Xbox disc, as an ISO image or as extracted files.

---

## Disclaimer

- **For educational and game-preservation purposes only.** This is a non-commercial, fan-made research project. Its purpose is to study how original Xbox software works and to keep a legally owned game playable on modern hardware.
- **Not affiliated with, endorsed by, or connected to** Middle-earth Enterprises, the Tolkien Estate, New Line Cinema, Warner Bros. Entertainment, Electronic Arts, Stormfront Studios, Microsoft, or any of their affiliates or licensors.
- *The Lord of the Rings*, *The Two Towers* and every related name, character, place, logo and image are trademarks and/or copyrighted material of their respective owners. Xbox is a trademark of Microsoft. **The authors of this project own no rights to any of them.** All rights remain with their owners.
- **What is and isn't included:**
  - **The source repository** contains no game data, no code generated from the game, and no cover art. Building from source translates the game's code on your own computer, from your own copy, with `generate.py`.
  - **The release build** (`TwoTowers-Recompiled-…-win64.zip`) contains no game data either: no movies, sounds, music, textures, models, levels, text or cover art. Its game program does include code that was mechanically translated from the original game's executable, as every static recompilation does. That translated code cannot run on its own: when the program starts, it loads the game's original executable and all of its data from your own copy.
  - **You must own the game.** The release is only for use with a legally obtained USA copy of *The Two Towers* that belongs to you. If you don't own the game, don't download or use the release.
- **Do not use this project for piracy.** Do not ask for, share or link to copies of the game or its files. The launcher only accepts the USA retail release, checked by its SHA-256 hash. Buy the game and make your own backup of your own disc.
- The software is provided "as is", without warranty of any kind (see [LICENSE](LICENSE)). Use it at your own risk.
- If you are a rights holder and have a concern, please open an issue and it will be addressed promptly.

---

## What the port adds

Everything below is on top of the original game, which plays as it did on the Xbox:

- **Runs natively** on Windows (64-bit), Linux and macOS, from your own disc: the launcher installs it from an ISO or an extracted folder and checks it is the USA release.
- **Languages** *(1.9.2)*: 33 languages besides English for every text in the game — menus, hints, upgrades, credits and all subtitles — in the game's own fonts with the letters each language needs, plus the launcher and the F10 menu. Chosen in the launcher.
- **Resolution and widescreen**: internal resolution from 1x to 12x (4.5x for 4K), 4:3, 16:9, 16:10, 21:9 and 32:9 with the HUD at the screen's edges, optional widescreen cutscenes, menus that fill the screen, windowed, borderless full screen or exclusive full screen at a resolution of your choice.
- **Picture**: Direct3D 11, Vulkan (DXVK) or OpenGL; MSAA and FXAA anti-aliasing, anisotropic filtering, mipmaps, VSync, brightness, sharpening and colour; a shader cache so effects stutter only once.
- **Ultra textures**: sharper menus, text and HUD out of the box, and every texture replaced with the GameCube high-resolution pack (downloaded separately).
- **Frame rate**: 30 fps as on the Xbox, or 60, 75, 90, 100, 120, 144, 165, 200, 240, 300 or unlocked (experimental), with game time kept to real time at any rate.
- **Keyboard and mouse**: a full default layout, every action rebindable (launcher, F10 or the in-game Controls page), a Walk key, a mouse pointer in every menu, mouse aiming for ranged attacks and mouse steering for movement.
- **Controllers**: Xbox / XInput pads, plus PlayStation, Switch Pro and others through SDL, with rumble; button prompts and loading-screen pictures that match what you play with (Xbox, Xbox One, PlayStation or your bound keys) and follow your bindings.
- **Menus**: in-game Display and Controls options pages, the F10 settings panel (also Back + Start on a controller), an Exit item, and intro films skippable with a click.
- **Mods** (off by default): the ECTS 2002 demo's HUD, the pre-release orange health pickups, and seven extra playable heroes — Boromir, Gandalf, Frodo, Lurtz, Elrond, an Uruk-hai and Sauron — each with his own model, moves, weapons, voice, pictures and upgrades.

## Contents

- [What the port adds](#what-the-port-adds)
- [Requirements](#requirements)
- [Installing and playing](#installing-and-playing)
- [Linux](#linux)
- [macOS](#macos)
- [Languages](#languages-new-in-192)
- [Graphics settings](#graphics-settings)
- [Ultra textures](#ultra-textures-new-in-19)
- [Controls](#controls)
- [Settings file reference](#settings-file-reference-twotowers_recompini)
- [Saves, logs and troubleshooting](#saves-logs-and-troubleshooting)
- [Building from source](#building-from-source)
- [Project layout](#project-layout)
- [Status and known issues](#status-and-known-issues)
- [Credits and licences](#credits-and-licences)

---

## Requirements

- Windows 7 SP1, 8.1, 10 or 11, 64-bit — or Linux, 64-bit (see [Linux](#linux)) — or macOS 10.15 or newer, Intel or Apple silicon (see [macOS](#macos)).
- A Direct3D 11 (feature level 11_0) graphics card: NVIDIA GeForce 400 series or later, AMD Radeon HD 5000 or later, Intel HD Graphics 4000 or later. Any card from the last ten years works at 1x internal resolution. Without one the game still runs, drawn by the CPU (slower).
- Your own copy of *The Lord of the Rings: The Two Towers*, **USA Xbox release**, in either form:
  - an Xbox disc image (`.iso` / `.xiso`, the "redump"/XDVDFS format), or
  - the disc's extracted files: a folder containing `default.xbe` and the `FMV`, `SOUND`, `UI`, `Worlds`, … folders.

  Other regions and versions are not supported. Both the launcher and `generate.py` check the SHA-256 of `default.xbe`.
- On Windows 10 and 11, no other software: every DLL the program uses is part of Windows.
- On Windows 7 and 8.1:
  - the HLSL shader compiler: `d3dcompiler_47.dll` (KB4019990 on Windows 7) or the DirectX End-User Runtime's `d3dcompiler_43.dll`. Without either, the game uses the software renderer;
  - sound uses XAudio2 where Windows has it (8 and later) and the older waveOut otherwise.

## Installing and playing

1. Download the release build `TwoTowers-Recompiled-…-win64.zip`, or [build it yourself](#building-from-source). It contains two programs:
   - `The Two Towers Recompiled.exe`: the launcher;
   - `twotowers_recomp.exe`: the game.
2. Put both in **a new, empty folder of their own**, for example `C:\Games\TwoTowersRecompiled`. Don't put them inside another game's install folder. The program writes its settings, saves and log next to itself.
3. Run **`The Two Towers Recompiled.exe`** and choose one of:
   - **Install from ISO…**: pick your disc image. The launcher checks that it is the USA release and extracts the game files into a `game_files` folder beside the launcher. This takes a minute or two, and a progress bar shows how far it has got.
   - **Use extracted folder…**: pick the folder that contains `default.xbe`. Nothing is copied; the launcher remembers the folder.
4. When the status line says **"Game files ready"**, press **Play**.

Later you can start `twotowers_recomp.exe` directly, or keep using the launcher.

Two launcher options:
- **Show the game window**: leave this on.
- **Write a log file**: writes `twotowers_log.txt`. It is useful for bug reports.

## Linux

The Linux build is a separate download, `TwoTowers-Recompiled-…-linux-x86_64.tar.gz`. It runs on 64-bit distributions from 2018 on (glibc 2.27 or newer: Ubuntu 18.04+, Debian 10+, Fedora 28+, Mint 19+, Arch, the Steam Deck's desktop mode, …).

1. Install SDL2 if you don't have it (most desktops do): `sudo apt install libsdl2-2.0-0` (Ubuntu / Debian / Mint), `sudo dnf install SDL2` (Fedora), `sudo pacman -S sdl2` (Arch).
2. Unpack the archive into a new folder of its own.
3. Run **`TwoTowers.sh`** (double-click it, or `./TwoTowers.sh` in a terminal). The first time it asks for your disc image and extracts it into `game_files` — with a file dialog if `zenity` is installed, otherwise in the terminal — and offers to add the game to your applications menu.

`./twotowers-setup` does the same from a terminal: `install <iso>`, `folder <dir>` (an extracted disc), `desktop` (menu entry), or no argument for a small menu with a few settings. Everything else is in the game's F10 menu and in `twotowers_recomp.ini`, as on Windows.

The picture is drawn with OpenGL 3.3 (any graphics driver from the last ten years — Mesa for AMD and Intel, or NVIDIA's), sound goes through SDL (PulseAudio, PipeWire or ALSA), and controllers through SDL too: Xbox, PlayStation 3/4/5, Switch Pro and most others, with rumble. Saves and the log are next to the program, as on Windows.

## macOS

*Experimental (new in 1.6).* The game runs on macOS through the same OpenGL renderer and SDL2 as on Linux. The build is x86-64: it runs natively on Intel Macs and through Rosetta 2 on Apple silicon (M1 and later). It is cross-built and has **not been tested on a Mac** by the author, so please report what you find.

**The download** (`TwoTowers-Recompiled-v1.9.4-macos-x86_64.tar.gz`, macOS 10.15 or newer):

1. Unpack it (double-click in Finder) and move the folder where you like.
2. The program is not signed by Apple. The first time, **right-click `TwoTowers.command` → Open → Open** (or run `xattr -dr com.apple.quarantine` on the folder in Terminal). On Apple silicon, install Rosetta 2 if macOS asks (`softwareupdate --install-rosetta`).
3. It asks for your disc image (The Two Towers, USA, Xbox) and extracts it into `game_files`, then starts the game. SDL2 is included in the `lib` folder.

**From source** instead:

1. Install the tools with [Homebrew](https://brew.sh): `brew install cmake ninja sdl2 python` (on Apple silicon, an x86-64 Homebrew for SDL2: `arch -x86_64 /usr/local/bin/brew install sdl2`).
2. Translate the game's code from your own copy (see [Building from source](#building-from-source), step 1: `python3 generate.py …`).
3. Run `sh twotowers/macos/build-macos.sh`. It builds into `build-macos/TwoTowers-Recompiled`, with SDL2 copied into its `lib` folder.
4. Double-click **`TwoTowers.command`** in that folder.

OpenGL on macOS is version 4.1 and is deprecated by Apple but still supported, including on Apple silicon. Controllers (Xbox, PlayStation, Switch Pro) work through SDL, as on Linux.

## Languages (new in 1.9.2)

Choose a language in the launcher's **Language** list (on Linux and macOS, `./twotowers-setup` and **l** in its menu; or `[Language] language=` in the settings file: `en`, or a folder name in `lang`). The game, the launcher and the F10 panel then use it from the next start.

**Languages:** English, French, German, Spanish (Spain), Spanish (Latin America), Italian, Portuguese (Brazil), Portuguese (Portugal), Dutch, Swedish, Danish, Norwegian, Finnish, Polish, Czech, Slovak, Hungarian, Romanian, Croatian, Turkish, Greek, Bulgarian, Russian, Ukrainian, Vietnamese, Indonesian, Japanese, Korean, Chinese (Simplified), Chinese (Traditional), Thai, Hindi, Hebrew and Arabic.

- **Everything the game writes** is translated: the menus, loading-screen hints, upgrade names and descriptions, the results and pause screens, the credits, and the subtitles of the levels' scenes and the bonus interviews. The port's own words are too: the Options pages, key names, button-prompt hints ("Press Enter to continue"), the Extra heroes' names and upgrades, the launcher and the F10 panel.
- **In the game's own lettering:** letters the game's fonts lack are added to them at the size and weight of each font, and to the Ultra textures' upscaled fonts too, so the text stays sharp in every language (accented letters are made from the game's own letters and accents; other scripts are drawn to match). Right-to-left languages (Hebrew, Arabic) read right to left, Arabic letters joined; Chinese, Japanese and Thai lines break between words.
- **Nothing of the game is shipped:** the `lang` folder holds only the translations, keyed by a hash of the English. The game's files are copied with the language's text into `lang_cache` on first use (a few seconds); your game files are not changed.
- The translations were made for this port, with each language's usual names for Tolkien's places and people. Corrections are welcome: each language is one text file (`twotowers/tools/lang/strings/<code>.tsv`; see `twotowers/tools/lang/README.md`).
- Spoken dialogue stays English (the game has only English voices).

## Graphics settings

You can change the graphics settings in two places. Both write to the same settings file.

### In the launcher

Press **Graphics settings…**. Each option has a drop-down list. **Defaults** restores the original settings, and **OK** saves.

### In game: the overlay menu

- Press **F10** on the keyboard, or **hold Back and press Start** on a controller.
- A panel appears over the game, and the game ignores your input while it is open.
- **Up/Down** chooses an option, and **Left/Right** (or **A** / **Enter**) changes it.
- **B**, **Esc** or **F10** closes the panel.
- Changes apply immediately and are saved automatically.
- The first line, **Page**, switches between the **Graphics** and **Controls** pages.

| Option | Choices | Notes |
|---|---|---|
| Renderer | Direct3D 11, Vulkan (DXVK), Software | Launcher only, Windows. **Vulkan** runs the same renderer on Vulkan through [DXVK](https://github.com/doitsujin/dxvk): download a DXVK release, and copy its `x64\d3d11.dll` and `x64\dxgi.dll` into a folder named **`dxvk`** next to `twotowers_recomp.exe`. If those files are missing, the game uses Direct3D 11 and says so in the log. Vulkan can be faster or smoother on some graphics cards and drivers (AMD and Intel especially); Direct3D 11 stays the default. *(new in 1.6)* |
| Frame rate | 30 (original), 60, 75, 90, 100, 120, 144, 165, 200, 240, 300, Unlocked | Above 30 is **experimental**: some scenes can run faster than intended. Use 30 if something seems off. The limit holds in the menus too; Unlocked stops at 1000. *(the list new in 1.9.4)* |
| Display mode | Windowed, Borderless full screen, Exclusive full screen | **Exclusive** switches the display to the **Full-screen resolution** (at its highest refresh rate) while the game is in front; leaving the game (Alt+Tab) puts the desktop back. **Alt+Enter** or **F11** switches between windowed and the full screen chosen. *(exclusive new in 1.9.3)* |
| Full-screen resolution | The desktop's, or one of the display's own | For exclusive full screen. *(new in 1.9.3)* |
| Window size | Automatic, 1280×720 … 5120×1440 | Launcher only. Sets the size of the window when not in full screen. |
| Aspect ratio | 4:3 (original), 16:9, 16:10, 21:9, 32:9 | **Widescreen is experimental.** Gameplay shows more to the left and right. The HUD keeps its shape, and menus and movies stay 4:3. In game, the 3D view widens fully after the next level load. |
| HUD in widescreen | At the screen's edges, Centred (4:3) | Where the HUD sits in a picture wider than 4:3. **At the screen's edges** (the default) puts the health sword and the counters against the left edge and the prompts against the right edge. **Centred** keeps everything in the 4:3 middle, as before. *(new in 1.6)* |
| Internal resolution | 1x (640×480) … 12x (7680×5760), and 4.5x (2160 lines: 3840×2160 at 16:9) | Sharper picture, at a higher graphics-card cost. Start with 2x. 4.5x matches a 4K screen pixel for pixel. 9x to 12x need a lot of video memory; at very wide aspect ratios they are held to the largest picture a graphics card can draw (16384 wide). A picture larger than the window is averaged down to it, so higher settings also smooth edges. *(9x-12x new in 1.9)* |
| Anisotropic filtering | Off, 2x, 4x, 8x, 16x | Sharper textures at an angle. |
| Mipmaps | On, Off | On means less shimmer on distant textures. |
| Anti-aliasing | Off, FXAA, MSAA 2x, MSAA 4x, MSAA 8x, MSAA 4x + FXAA, MSAA 8x + FXAA | **MSAA** (multisampling) smooths the edges of the 3D as it is drawn; **FXAA** smooths the finished picture. MSAA costs video memory and speed at high internal resolutions, and uses fewer samples where it would need more than about 1.5 GB per picture. Cut-out textures (grass, leaves, fences) are not smoothed by MSAA. *(MSAA new in 1.9)* |
| Ultra textures | Off, On | Sharper menus, level select, mission pictures, text and HUD (the game's own textures, upscaled; they come with the game). With the GameCube high-resolution texture pack (`GLOS69`, downloaded separately from its author) every texture is replaced: see [Ultra textures](#ultra-textures-new-in-19). |
| VSync | Off, On | On prevents tearing. |
| Picture | Keep shape, Stretch to fill | |
| Vertex programs | Graphics card, CPU | Leave this on Graphics card, which is much faster. CPU is the reference path, for troubleshooting. |
| Brightness | Darker … Brightest (5 steps) | Gamma on the final picture. Lifts the game's very dark areas. *(new in 1.1)* |
| Sharpening | Off, Low, Medium, High | Contrast-adaptive sharpening of the final picture. Best with a higher internal resolution. *(new in 1.1)* |
| Colour | Original, Vivid, Muted | Colour saturation. *(new in 1.1)* |
| Mod: ECTS 2002 HUD (experimental) | Off, On | Launcher only. See [Mods](#mods-new-in-11). |
| Mod: Orange health pickups (shown in pre-release & making-of videos) | Off, On | Launcher only. See [Mods](#mods-new-in-11). |
| Mod: Extra heroes | Off, On, On (unlocked for testing) | Launcher only. **Highly experimental.** See [Extra heroes](#extra-heroes-extra_heroes1-highly-experimental). |

## Ultra textures (new in 1.9)

**Ultra textures** (Display Options, the launcher's Graphics settings, `[Display] ultra_textures=1`) draws high-resolution pictures in place of the game's own textures.

- **Included with the game:** the menus, the level select and its pictures of each level, the text and the HUD (including the ECTS HUD mod's icons), upscaled 4x from the game's own textures, in `textures_ultra\ui`. These work as soon as the option is on.
- **The GameCube texture pack (`GLOS69`), downloaded separately.** The GameCube release's high-resolution texture pack is its author's work and is **not included**: download it from its author and put its **`GLOS69`** folder (the folder full of `tex1_...` files) in the game's folder, next to `twotowers_recomp.exe`. (A `GLOS69` folder one level further down, `GLOS69\GLOS69`, is found too.)
- **Made ready once.** The first time you press **Play** with Ultra textures on and the pack in place, the launcher prepares it (a few minutes, with a progress bar): it converts the pack's PNG pictures, gives cut-out pictures (grass, leaves, fences) soft edges, gives about a hundred pictures the game texture's transparency, and indexes every picture. The results go into `textures_ultra` (about 1 GB); the `GLOS69` folder is not changed. If you turn Ultra textures on in the game's own menus instead, the launcher opens and does the same while you play, and the pack's pictures appear when it is done. On Linux and macOS, `TwoTowers.sh` / `TwoTowers.command` do it (or run `./twotowers-setup ultra`). A new or updated pack is prepared again automatically.
- The green health pickup keeps its own texture (the Orange health pickups mod recolours it).
- Needs a graphics card with BC7 textures (any Direct3D 11 card).

## Mods (new in 1.1)

Two optional mods bring back how the game looked before it was finished, and a third (new in 1.2, much improved in 1.3, highly experimental) adds playable characters. All are off by default and leave the retail game untouched when off. Turn them on in the launcher (**Graphics settings…**) or in the `[Mods]` section of the settings file; they take effect when the game starts (they are deliberately not in the in-game F10 menu, so they cannot be switched half-way through a level).

### Why these mods exist

The Two Towers was shown before release — most visibly in the playable build at **ECTS 2002** (the European Computer Trade Show) and in pre-release trailers and making-of videos. That build differed from the retail game in a few visible ways: a different HUD, and orange health pickups instead of green ones. None of it shipped, and the footage is all that is left of it. The retail game still has a debug switch called *"DEBUG/Display EXP and Runes"*, but the code behind it was removed, so the old HUD cannot simply be switched back on.

These mods recreate that pre-release look from the footage, as a piece of preservation: a way to play the game as it was shown, not as an improvement on the retail design. They are reconstructions, not recovered code — measured frame by frame from the videos and drawn with the game's own textures and fonts where possible.

(Earlier builds of this project called them the "E3" HUD and "E3 power-ups". The footage is in fact from ECTS 2002, so they were renamed. The settings keys keep their old names — `e3_hud`, `e3_pickups` — so existing settings files still work.)

### ECTS 2002 HUD (`e3_hud=1`, experimental)

What changes, and why:

| Change | Retail game | ECTS 2002 HUD | Why |
|---|---|---|---|
| Portrait | none | the character's face in a round medallion at the health sword's grip | The demo showed who you were playing on the sword itself. |
| Sword cross-guard | straight bar | curved toward the blade, brighter steel | Matches the demo's sword. |
| Health bar | green | green (unchanged) | The demo's bar was the same colour, so it is left as retail. |
| Experience gauge | ornate tube with a silver medallion | plain rectangle, filling blue | The demo's simpler gauge. The retail fill is kept underneath, so it still shows your real experience. |
| Combo display | none | a **rune orb** under the gauge | The demo showed each combo as it was entered: the orb's quarters fill clockwise from the top right, one per attack — **A** (speed) blue, **Y** (fierce) green, **B** (knock back) red. |
| Combo name | none | **Speed**, **Fierce**, **Special** or **Mixed** beside the orb when a combo ends (Mixed turns the whole orb magenta) | As in the demo, in the game's own lettering, enlarged to match the footage. |
| Kill rating ("Good", "Excellent" …) | above the gauge | beside the gauge's top | Its place in the demo. |
| Experience readout | none | **EXP: current/needed** | The demo showed the numbers toward the next level. |
| Loading-screen tip about experience | the retail gauge | the new gauge and orb, with a second arrow and a short explanation of the combo runes | So the tip describes the HUD you actually see. |
| Loading-screen tip about the Skill Meter (Fangorn) | the retail gauge | the new gauge and orb, with the arrow to "Skill Meter" kept | The same tip for the same gauge. |
| Mission results screen | the retail gauge at the top right | the new gauge and orb in its place | The gauge looks the same everywhere it appears. |
| Helm's Deep wall: progress bar portrait | a hooded figure in a spiked medallion | the demo's framed picture of the figure, level with the bar | As in the demo footage. |
| Westfold: villager counter | a green bar with a portrait, shortening as villagers are lost | two rows of four villager couples; each pair lost is crossed out in red | The demo's counter. Same balance as retail: the bar had 8 steps, so 8 couples. |

It is marked **experimental** because it is drawn on top of a HUD the game was never designed to change: it has been tested through the levels played so far, but an odd frame (a menu, a cut-scene) could still show a piece of it out of place. Turn it off in the launcher if anything looks wrong.

The portraits are crops of the ECTS 2002 footage and are loaded from the `e3hud` folder beside the program (`aragorn.bmp`, `legolas.bmp`, `gimli.bmp`, 32-bit BMPs with transparency; for the Extra heroes mod also `boromir.bmp`, `frodo.bmp`, `lurtz.bmp`, `elrond.bmp`, `sauron.bmp` and `uruk.bmp`, made from the game's artwork), as are the Helm's Deep wall level's progress-bar picture (`level10.bmp`; without it that level keeps the retail portrait) and Westfold's villager couples (`villager.bmp`, `villager_dead.bmp`; without them Westfold keeps the retail bar). They are included in the release download but **not** in this source repository. Without them, Aragorn's, Legolas's and Gimli's portraits are made from the character models' own face textures. Isildur, who is not in the footage, always uses the picture from the game's upgrade screen.

### Orange health pickups (`e3_pickups=1`)

In pre-release and making-of videos the health pickups glow orange; in the retail game they are green vials. This mod recolours the vial's glass to orange as the game loads the texture (hash `71C950C7`, recognised out of the box). Only the glass panes change; the metal frame and everything else stay as they were, and nothing in your game files is modified.

If some other green pickup should turn orange too, its texture can be added:

1. Set `texture_dump=1` in the settings file (any section; `[Developer]` is where the template has it).
2. Play until the item is on screen, then quit. The log says where the dump went (`[TEXDUMP] saving textures to ...`).
3. The `texture_dump` folder beside the program now holds every texture drawn, named `HASH_WxH_fFMT.bmp`. Add the item's hash to `[Mods] e3_pickup_textures=` (several hashes can be given, separated by commas).
4. Set `texture_dump=0` again (dumping is slow).

### Extra heroes (`extra_heroes=1`, highly experimental)

> ⚠ **Highly experimental.** Much better in 1.3, but still a mod built on a game that was never made for it: expect the odd visual glitch, and possibly crashes. It is **off by default**; turn it off again (launcher, or `extra_heroes=0`) if anything goes wrong. Your saves are not changed by it.

Adds seven more playable characters to the character select, made from the game's own models: **Boromir, Gandalf, Frodo, Lurtz, Elrond, the black Uruk-hai and Sauron**, for eleven in all. On the select they come in that order, Sauron last.

- **Unlocking (new in 1.3):** one at a time, by finishing the Tower of Orthanc as Isildur on that save. The 2nd finish unlocks Boromir, the 3rd Gandalf, the 4th Frodo, the 5th Lurtz, the 6th Elrond, the 7th the Uruk-hai and the 8th Sauron (1.6). The count goes with your game as its progress does (1.6): loading a save brings that save's count, and every save you write carries the count of the game you are playing -- whichever slot it goes to and whichever hero you played since -- one more when it is the save after Isildur's finish. Before you load anything, the newest save's count shows. A hero still locked shows as a faint shadow on the select and is skipped. `extra_heroes=2` ("On, unlocked for testing" in the launcher) shows them all on any save. That only changes what the game sees while it runs and writes nothing to the save.
- **How they play:** each one takes the fourth hero's place (Isildur's) in the level, with his own experience, scores and upgrades. Those are kept in the `heroes` folder beside the program (`heroes/records.dat`, and the unlocks in `heroes/unlocks.txt`); the save keeps Isildur's.
- **Their own bodies and moves (new in 1.3):** they play on their own skeletons, with their own walk, run and hit reactions. Boromir, Frodo and Elrond fight with the heroes' light and heavy attacks, fitted to their bodies; Gandalf, Lurtz, Sauron and the Uruk-hai fight with their own. Heavy attacks hit as the heroes' do, and (1.5) Boromir's and Frodo's blows and kicks go where they face.
  - Everyone kicks and parries with the heroes' kick and parry, fitted to their bodies (1.6), so kicks knock enemies back and the Deeping Wall's ladders off the wall, and parries hold as the heroes' do.
  - Their own moves step where the heroes' do (1.6): they stand still as they aim, and stay put when standing.
  - They fall when they die (1.6): Gandalf, Frodo, Elrond and the Uruk-hai with their own fall, Boromir, Lurtz and Sauron with the heroes' death.
  - The Uruk-hai's heavy attacks are his two forward swings (1.6); before, the second was a guard that looked like a parry and struck nothing.
  - Lurtz's and the Uruk-hai's blades reach a little further (1.7.2), and Lurtz is tougher (1800 health, much heavier against knockdowns, over his flinches sooner). They keep their own attacks.
  - Moves that took them the wrong way are the heroes' (1.7.2): the game moves a character by his motion's own steps, and the moves a new hero's own set lacked were all one sideways step -- in water (Gates of Moria, the Gap of Rohan) and wherever the game played them, he went sideways or backwards. Their own walks and runs that go the right way are kept. Sauron keeps all his own (1.7.4). If a new hero falls through the ground, he is put back where he last stood (1.7.5).
  - In the cutscenes (1.7) they play the heroes' own moves, fitted to their bodies: in Fangorn's, Balin's Tomb's and the Gates of Moria's in-game scenes they are whole and move as the hero would, no longer twisted or torn into stretched shapes.
- **Their own weapons (new in 1.3):**
  - Boromir: his sword, and his shield on his other arm.
  - Gandalf: a sword in his right hand and his staff in his left (1.6). His ranged attack is a beam of light from the staff's head instead of arrows.
  - Frodo: Sting, its blade glowing a pale blue. No bow: his ranged attack throws stones (1.6).
  - Lurtz: his Uruk-hai sword and his own bow.
  - Elrond (1.6): the elves' curved sword from the prologue.
  - Sauron (1.6): his mace. His ranged attack is a bolt of power, like Gandalf's, in red.
  - With the last ranged upgrade (1.7.1): Gandalf's beam turns from blue to white (White Fire); Sauron's bolts are red and blue in turn, shot by shot (Doom Fire); the Uruk-hai's plain stones become burning rocks (Lava Rocks), and every third one bursts where it lands or strikes, as the Orthanc archers' fire pots do, leaving burning ground that hurts whoever stands in it. Before it, the Uruk-hai throws plain stones; his first two ranged upgrades are now called Crag Rocks and War Rocks.
  - The black Uruk-hai (1.6): his great sword. His ranged attack is a rock, thrown with his own throw: a plain stone, a burning one with his last ranged upgrade (1.7.1).
  - They carry them wherever they appear as the hero: in play, in the cutscenes, on the character select and in the pause screen's upgrade figure.
- **Their own pictures (new in 1.3):** each has his own idle pose on the character select, his own figure on the pause screen's upgrades (standing, weapon in hand at his side; 1.7.2: no longer twisted or mid-stride) and the end-of-level model, and his own HUD portrait (also in the ECTS HUD mod), upgrade and results-screen pictures. A picture of your own replaces a hero's upgrade-screen background: put it in an `upgpics` folder beside the program as `<name>.bmp` (`boromir.bmp`, `elrond.bmp`, `sauron.bmp`, ...; 24 or 32-bit BMP, up to 2048 pixels a side; it is cropped to fill the picture's left part).
- **Their own ammo counter and upgrade items (new in 1.6):** the HUD's ammo counter shows each one's own: Elrond's is Legolas's gold one, Gandalf's his staff, Frodo's a stone, the Uruk-hai's a burning stone, Sauron's his gauntlet glowing red (Boromir and Lurtz keep the bow). The pictures come from the `hudammo` folder beside the program (`<name>.bmp`, 64x128, 32-bit). On the upgrade screen the bow and sword items show each one's own weapons (the ranged pictures from the `upgitems` folder, `<name>_ranged.bmp`, 128x256; the melee ones drawn from his weapon: Sauron's mace, Gandalf's and Elrond's swords, Frodo's Sting, ...).
- **Their own voices (new in 1.6):** in play, their attacks, blows taken and deaths have their own voices, taken from the game's own sound banks: Lurtz and the Uruk-hai their own, Elrond and Frodo Legolas's, Sauron Lurtz's made far deeper, Boromir Aragorn's made deeper; Gandalf Gimli's, made a little deeper (1.7). On the character select each has his own sounds when you choose him (his weapon, then his line), as the heroes do. The copies are made from your game files each time a level loads (`heroes/voices`), never bigger than the game's own.
- **Their own upgrade names (new in 1.6):** the upgrades carry things of each hero's own: his six named moves have names of their own instead of Isildur's ("Sauron's Dance", "Eye of Sauron", "You Shall Not Pass", "Sting's Bite", ...), and his ranged weapon is named and described for what it is -- Frodo's stones ("Shire Stones", "Flintshot", "Elf Stones"), the Uruk-hai's burning rocks, Gandalf's and Sauron's fire -- rather than bows and arrows. Also "Strength of Barad-dur" and the like.
- **The `heroes` folder** also holds the mod's private copies of the game files it changes (the menu and the levels you play with a new hero). Deleting the folder resets the new heroes' progress and unlocks and rebuilds the copies.

Known problems:

- Gandalf's, Boromir's, Frodo's and Elrond's voices in play are other characters' (the game has no grunts of theirs).
- Sting's glow is a tint of the blade, not a light that shines on its surroundings.
- Breaking enemy shields with heavy attacks has not been measured for them as closely as plain hits; the Uruk-hai's heavy blows reach further in 1.6, but still land a little less often than Aragorn's. Reports are welcome.

Bug reports with `twotowers_log.txt` (its `[HEROES]` lines) are welcome.

## Controls

An Xbox / XInput controller (Xbox 360, One, Series, or anything that acts like one) works out of the box. The keyboard and mouse act as controller 1.

**PlayStation controllers** (DualShock 3, DualShock 4, DualSense / DualSense Edge), Switch Pro and other pads work too, with rumble, over USB or Bluetooth:
- on Windows, put **`SDL2.dll`** (64-bit, from the SDL2 release, or the one included with this download) beside `twotowers_recomp.exe`. It is only loaded if it is there; Xbox pads keep going through XInput either way. Steam's controller support can also present a PlayStation pad as an Xbox one, if you prefer;
- on Linux, nothing to add.

The DualShock 4 / DualSense touch pad click is Back, and their light bar turns green, like player 1's light on an Xbox pad.

**The mouse in the menus** *(new in 1.8)*. In every menu (the title screen, the main menu, options, load and save screens, the mission select, the character select, the upgrade screens, the pause screen and its dialogs) a pointer follows the mouse; it shows as soon as the game's first menu is up, never in play (only when the game is paused on a menu). Pointing at an item chooses it just as the controller would (it lights up, with the game's own sounds); a left click presses it; on a setting, a click on one of its values sets it and a click or drag on a slider moves the slider there; a right click goes back. On the mission select, pointing at any place on the map (a level or an interview, its icon or its highlight) chooses it at once, wherever it is; click to go on to the character select. On the character select, click the character to the left or right of the one in front to turn the ring to him (each click one step, clicks made while it turns are done in turn), the one in front to choose him. The leaves at the bottom corners (**Back** and **Continue**) light up under the pointer and are clicked on the leaf or its word only, so a click low on a character is not taken for them. On the upgrade screens (after a level, and Show Upgrades in the pause screen) the level leaves along the top can be pointed at and clicked too. The pointer hides when you use the controller or keys and comes back when the mouse moves. While it is up the mouse buttons belong to it; in play they act as bound, as before.

**Options pages** *(new in 1.8)*: Options (from the main menu, the mission select or the pause screen) now opens on a first page: **General Options** (the game's own page), **Display Options** (every display setting of the F10 menu: frame rate, display mode, aspect ratio, internal resolution, anisotropic filtering as a slider, mipmaps, anti-aliasing, VSync, picture, vertex programs, brightness and sharpening as sliders, colour, HUD in widescreen, menus, movies) and **Controls** (button prompts, mouse pointer, mouse sensitivity as a slider, pointer size, mouse aiming and its invert), in the options' own lettering, flowers, rings and sliders. B goes back to the first page, and from there out of Options. On the title screen, "Press START button" reads "Press Enter" (the key bound to Start) when you play with the keyboard. The pointer also shows over the films before the title screen; over the F10 menu the system's pointer shows instead. On the mission select, the picture galleries (production photos, concept art) go back and forward with a click on the left or right half; the Secret Codes place can be pointed at and clicked too.

**Steering with the mouse** *(1.9.1)*: with **Mouse steering** on (F10 / Controls page / launcher, `[Input] mouse_steer=1`), hold **W** and the mouse is the stick: the hero runs the way the mouse pushes, from the camera's view, and stops when you let go of W. Hold **Walk** with W and he walks *(1.9.2)*.

**Aiming with the mouse** *(new in 1.8)*: hold the ranged button (F, or whatever it is bound to) and move the mouse to aim, in place of W A S D (the mouse pushes a stick that stays where it was pushed until you let go of the button).

Settings (F10 menu, launcher's Graphics settings, `[Input]` in the settings file): **Mouse pointer in menus** (on / off, `menu_mouse`), **Mouse sensitivity** (low, medium, normal, high, very high: the pointer's speed and the aiming's, `mouse_sensitivity` 0.5 to 2), **Pointer size** (small, normal, large, extra large, `cursor_size`), **Mouse aiming (ranged)** (on / off, `mouse_aim`) and **Mouse aiming: invert** (off, up / down, left / right, both, `mouse_invert`).

**Exit** *(new in 1.8)*: the main menu has an **Exit** item under Credits that closes the game.

**Button prompts.** The game's button prompts (the upgrade screen's combo lists, "Help", and the prologue's hints in play) can show:
- **PlayStation** buttons: the cross, circle, square and triangle in place of A, B, X and Y, and R2 for the right trigger;
- **Keyboard / mouse** *(new in 1.6)*: the key or mouse button each action is bound to, as a keycap with its name (K, SPACE, NUM 8, F12, an arrow, …) or a mouse with that button lit. A key you rebind (in the launcher's **Controls…** or the F10 **Controls** page) shows at once.

**Automatic** (the default) follows what you play with: keyboard/mouse prompts after you press a key or click, PlayStation prompts with a PlayStation pad as player 1, and Xbox ones otherwise. Choose it in the F10 menu ("Button prompts": Automatic / Xbox / PlayStation / Keyboard / mouse), in the launcher's **Graphics settings…**, or in the settings file (`[Input] prompts=auto|xbox|playstation|keyboard`). The loading screens' controller pictures then show a PlayStation pad too — a DualShock 3, DualShock 4 or DualSense, whichever is plugged in as player 1 (a DualShock 4 if none is; `[Input] ps_pad=auto|ps3|ps4|ps5` picks one). Those pictures are the `prompts` folder beside the game; without it the Xbox pad stays.

### Default keyboard and mouse layout

| Controller | Keyboard / mouse |
|---|---|
| Left stick (move) | W A S D |
| Right stick | Numpad 8 / 4 / 2 / 6 |
| A | K, Left mouse button, Space |
| B | L |
| X | J, Right mouse button |
| Y | I |
| Black / White | C / Z |
| Left / Right trigger | F / R |
| Start | Enter, Esc |
| Back | Backspace, Tab |
| D-pad | Arrow keys |
| Left / right stick click | Shift / Middle mouse button |
| Walk (hold, with the move keys) | B *(new in 1.7.6)* |

**Walk** *(new in 1.7.6)*: hold it with the move keys and your hero walks instead of running (the keys push the stick part way, as a light push on a controller's stick does). It is an action of its own, rebindable in the launcher's **Controls…** and the F10 **Controls** page (`kb_walk` in the settings file). On a controller, push the stick part way.

There is also a "classic" layout: set `layout=classic` under `[Input]` in the settings file.

### Remapping

You can change every action, for both keyboard/mouse and controller.

**In the launcher:** press **Controls…**.
- Click an action's key box, then press any key or mouse button. Esc cancels.
- **×** clears an action's keys.
- The drop-down list on the right chooses which button on your own controller performs the action.
- **Defaults** restores the original layout. **OK** saves.

**In game:** press **F10**, then on the **Page** line press **Right** to reach **Controls**.
- Press **A** or **Enter** on an action, then press the new key or mouse button. **Esc** cancels and **Delete** clears it.
- **Left/Right** changes the controller button for that action.
- The last line resets every binding to its default.

How bindings behave:
- Each action can have up to three keys or mouse buttons.
- A key you assign is removed from any other action that used it.
- Choosing a controller button that another action already uses swaps the two, so nothing is lost.

## Settings file reference (`twotowers_recomp.ini`)

The program creates `twotowers_recomp.ini` next to itself on first run, and the comments inside it explain each option. The launcher and the in-game menu edit this file for you. The main keys:

```ini
[Paths]
game_dir=            ; extracted game folder; empty = game_files beside the exe
[Language]
language=en          ; en, or a folder in lang\ (fr, de, es, es-419, ja, ru, zh-Hans, ar, ...)
[Display]
window=1
fps=30               ; 30 | 60 | 75 | 90 | 100 | 120 | 144 | 165 | 200 | 240 | 300 | unlocked
fullscreen=0         ; 0 windowed | 1 borderless full screen | 2 exclusive full screen
fs_resolution=       ; exclusive full screen's resolution, e.g. 1920x1080; empty = the desktop's
window_size=         ; e.g. 1920x1080; empty = automatic
aspect=4:3           ; 4:3 | 16:9 | 16:10 | 21:9 | 32:9 | a number such as 2.37
hud=edges            ; edges | centre: the HUD in a picture wider than 4:3
render_scale=1       ; 1-12, or 4.5 (2160 lines: 3840x2160 at 16:9)
anisotropic=1        ; 1 (off), 2, 4, 8, 16
mipmaps=1
antialiasing=off     ; off | fxaa | msaa2 | msaa4 | msaa8 | msaa4+fxaa | msaa8+fxaa
ultra_textures=0     ; 1 = Ultra textures (sharper menus and HUD; every texture with the GLOS69 pack)
vsync=0
stretch=0
vertex_programs=gpu  ; gpu | cpu
renderer=d3d11       ; d3d11 | vulkan (DXVK, in a dxvk folder) | software
brightness=1         ; 0.85 | 1 | 1.15 | 1.3 | 1.45
sharpening=0         ; 0 | 0.4 | 0.7 | 1
colour=1             ; 1 | 1.25 (vivid) | 0.8 (muted)
[Mods]
e3_hud=0             ; 1 = the ECTS 2002 demo's HUD (experimental)
e3_pickups=0         ; 1 = orange health pickups
e3_pickup_textures=  ; vial texture hashes (hex, comma separated)
extra_heroes=0       ; 1 = Boromir, Gandalf, Frodo, Lurtz, Elrond, Uruk-hai, Sauron (highly experimental); 2 = the same, unlocked on any save
[Input]
keyboard=1
layout=wasd          ; wasd | classic
background_input=0   ; 1 = the controller also works while another window is in front
stick_deadzone=10    ; stick dead zone in percent (stops a drifting stick walking the hero); 0 = off
prompts=auto         ; auto | xbox | xboxone | playstation | keyboard
menu_mouse=1         ; 1 = a mouse pointer in the menus (point and click), 0 = off
mouse_sensitivity=1  ; 0.5 | 0.75 | 1 | 1.5 | 2: the pointer's and the mouse aiming's speed
cursor_size=normal   ; small | normal | large | xlarge
mouse_aim=1          ; 1 = hold the ranged button and move the mouse to aim
mouse_invert=0       ; 0 | y | x | xy: the mouse aiming inverted
mouse_steer=0        ; 1 = hold W and the mouse steers (Walk walks)
[Controls]
kb_a=K, Mouse Left, Space    ; written by the Controls pages
pad_a=A
[Audio]
output=1             ; 0 = no sound (for PCs without an audio device)
[Compat]
single_cpu=1         ; the game's own threads share one CPU, as on the Xbox
[Developer]
logging=1
texture_dump=0       ; 1 = save every texture drawn to texture_dump\ (slow)
```

If the settings file comes from an older build, the program rewrites it on start. It keeps your values and saves the previous file as `twotowers_recomp.ini.old`.

## Saves, logs and troubleshooting

- **Saves** go in `game_files\UDATA\` (or in your extracted folder's `UDATA`). Back that folder up to keep your progress.
- **Log**: `twotowers_log.txt`, next to the exe. Attach it to any bug report.
  - It lists the renderer, the frame rate (`[FPS]`) and, if the game ever stalls, where each part of the program was (`[HANG]`).
  - Please check that it contains nothing you'd rather not share before posting it.
- **Black window or "no Direct3D 11 device"**: update your graphics driver. `renderer=software` is a slow fallback.
- **Low frame rate**: lower **Internal resolution** first, then anisotropic filtering, and turn FXAA off. Make sure **Vertex programs** is set to **Graphics card**. On Windows, the **Vulkan (DXVK)** renderer can be faster with some drivers.
- **Crackling or broken-up sound in busy scenes**: since 1.6 the game keeps a small cushion of sound queued (about 27 ms) and refills it at once when it runs low. If sound still breaks up on a slow PC, set the environment variable `RECOMP_AUDIO_CUSHION` to a larger number of 5.3 ms periods (up to 14) before starting the game; the log notes `[APU] the sound output ran dry` when it happens.
- **Keyboard does nothing**: click the game window so it has focus, and check that `[Input] keyboard=1`.
- **Controller does nothing**: the game only reads it while its window is in front (so it never reacts while you use another program). Click the game window, or set `[Input] background_input=1`.
- **Game runs too fast**: set **Frame rate** back to **30**.

## Building from source

The C code for the game is not in this repository. You generate it from your own `default.xbe`, then compile it together with the runtime.

### Tools

- **Python 3.10+**, for the recompiler (no extra packages).
- **CMake 3.20+** and **Ninja**.
- A compiler:
  - **[llvm-mingw](https://github.com/mstorsjo/llvm-mingw)**: tested. Use it natively on Windows, or cross-compile from Linux or WSL.
  - **MSVC**: may work, but has not been tested.

### Steps

```sh
# 1. Translate YOUR copy of the game (default.xbe or the extracted disc folder).
#    It checks the SHA-256 and takes several minutes; nothing leaves your machine.
python generate.py "path/to/extracted/game"

# 2a. Configure on Windows, with llvm-mingw's bin folder on PATH:
cmake -S twotowers -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++

# 2b. ...or configure from Linux/WSL, cross-compiling with llvm-mingw on PATH:
cmake -S twotowers -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=$PWD/xboxrecomp-0.12.0/cmake/mingw-w64-x86_64.cmake

# 3. Build.
cmake --build build
```

Route 2b, cross-compiling, is how the release builds are made. Route 2a uses the same compiler natively.

The build produces `build/twotowers_recomp.exe` and `build/The Two Towers Recompiled.exe`. Copy both into an empty folder and follow [Installing and playing](#installing-and-playing).

### Optional

- **Your own icon and launcher art**: put `twotowers.ico` and/or `cover.bmp` (369×385) in `twotowers/resources/local/` before configuring. That folder is ignored by git. The launcher also shows a `cover.bmp` placed beside it.
- **Builds are reproducible and contain no paths from your machine**: source paths are remapped (`-ffile-prefix-map`) and the link timestamp is omitted.

### Developer switches

These are environment variables, used for testing:

| Variable | Effect |
|---|---|
| `RECOMP_HW_VP=0` | Run the game's vertex programs on the CPU. |
| `RECOMP_HW_PS=0` | Use the general pixel shader instead of per-effect shaders. |
| `RECOMP_PAD_SCRIPT=file` | Scripted controller input (`<ms> <BUTTON>` or `<file>+<ms> <BUTTON>` lines). |
| `TT_TWEAKS="SKIPMAINMENU=1;HERO=aragorn;LEVEL=02GATE2"` | The game's built-in developer switches: start straight in a level. |
| `TT_DEBUG_GOTO=<1-11>` | With the pad-script `FLAG1`, jump the front end to a screen (10 = end of level, 11 = upgrades). |

## Project layout

```
generate.py                 xbe -> C, using the pipeline below
twotowers/                  the game project
  CMakeLists.txt
  config/seed_functions.json  extra function entry points found while bringing the game up
  postproc_apu.py           post-pass on the generated code (direct sound-chip register access)
  src/main.c, twotowers_host.h      startup, settings file, paths, logging
  src/twotowers_manual.c    hand-written replacements and hooks for specific game functions
  src/setup_launcher.cpp    the launcher (ISO extraction, settings, controls)
  src/heroes.c              the Extra heroes mod (character select, re-rigged models, records)
  src/menus.c               the main menu's Exit and the mouse pointer in the menus
  src/e3hud.c               the ECTS 2002 HUD and orange-pickup mods
  src/lang.c                the languages: the game's files copied with a language's text and letters
  lang/                     the language packs (made by tools/lang/build_lang.py)
  tools/lang/               the translations and the tools that make the packs (README.md there)
  tools/rig/                how heroes.c's bone tables were made (chain.py)
  src/recomp/gen/           GENERATED by generate.py -- not in git
  resources/                neutral icon, manifest
xboxrecomp-0.12.0/          the recompiler and Xbox runtime, with the changes made for this game
  tools/                    xbe_parser, disasm, func_id, abi_analysis, recomp, ...
  src/                      kernel, NV2A GPU (Direct3D 11 back end), APU audio, input, video
CHANGES.md                  everything changed in xboxrecomp for this port
```

## Status and known issues

**Work in progress.** The game is playable, and more updates and bug fixes are coming. Play at **30 fps** (the frame rate it was made for), and if the game crashes or freezes, restart it: the same spot usually works on the next try.

- Tested in 1.1.1 by a scripted run of every level, rotating heroes (Aragorn, Legolas, Gimli, and Isildur in the prologue) and 30 / 60 / unlocked fps: the prologue, Weathertop, Caradhras, Balin's Tomb, Amon Hen, the three Rohan levels, the three Helm's Deep levels and both Orthanc levels all load, play their movies and reach gameplay with no crash and no freeze. Fangorn was not part of that run.
- Tested in 1.3: every level again on Windows without the mod (all 14 load, 30 / 60 / unlocked fps, no crash); the Extra heroes mod's four heroes on the Plains of Rohan and in Hornburg Courtyard (moves, weapons, Gandalf's beam, heavy-attack hits measured against Aragorn's); on Linux, a level with Boromir (built, opening scene).
- New in 1.3, **highly experimental**: the [Extra heroes](#extra-heroes-extra_heroes1-highly-experimental) mod's heroes have their own bodies, moves, weapons and pictures, and unlock one by one.
- New in 1.5: in the Extra heroes mod, Boromir's and Frodo's heavy attacks and kicks go where they face (before, about 40 degrees to one side). Tested on Windows in Hornburg Courtyard (their hits measured against Aragorn's), and on Linux with Frodo (level built, opening scene).
- New in 1.5.2: in the Extra heroes mod, Boromir's and Frodo's bow shot plants their sword in the ground as Aragorn's does; the level's own Boromir, Frodo, Gandalf and Lurtz are left alone when you play as one of them; the prologue is always Isildur's; the character select spreads the eight out and keeps everyone's idle moving. With the ECTS HUD mod, the prologue shows Isildur's portrait. Tested on Windows: the prologue's closing scene and Balin's Tomb's opening scene, with the mod on and off, show the same characters and moves; in Balin's Tomb as Boromir, the level's own Boromir keeps fighting.
- New in 1.6: **keyboard and mouse button prompts** (each prompt shows the key or mouse button the action is bound to, and changes when you rebind it; "Automatic" follows the keys or the pad you use), and the PlayStation and keyboard prompts now also replace the prologue's button hints in play. **Vulkan** on Windows through DXVK (Renderer: Vulkan, with DXVK's two DLLs in a `dxvk` folder). In widescreen the HUD sits at the screen's edges (or centred, as before: "HUD in widescreen"). A small cushion of sound queued at the output stops the sound breaking up when a busy moment holds the game back. macOS: the source builds for Intel Macs and Apple silicon (through Rosetta 2), not yet tested on a Mac. In the Extra heroes mod: the character select shows the level of every unlocked character in the distance (the new heroes' and Isildur's too); Gandalf is tougher (more health, flinches less; a little less than Sauron) and Lurtz a little less than Gandalf; the Uruk-hai's rising attack is his own getting-up move; an Orthanc finish as Isildur now counts on exactly the save the game writes after it; Isildur no longer loses his sword or stands in a T-pose on the character select after you played as a new hero; the new heroes' shots have their own sounds (Gandalf's and Sauron's bolts, Frodo's stones, the Uruk-hai's burning rocks); with the ECTS HUD, Frodo's portrait and the cave troll's health bar are the demo's.
- Also new in 1.6: with keyboard / mouse prompts the **loading screens' controls pictures** show a keyboard and a mouse with the keys you have bound (rebound keys show), and the hint under them names your keys. A **macOS download** (untested on a Mac). Compiled shaders are kept in a `shadercache` folder, so a new effect stutters only the first time it is seen. The game window is DPI aware: on a scaled display (a 4K screen at 150-200 %) it is drawn in real pixels -- sharper, and it fixes a black window with Vulkan. In the Extra heroes mod: the unlocks go with your game into every save you write (see [Extra heroes](#extra-heroes-extra_heroes1-highly-experimental)); Gandalf's shots have the bow's sounds again.
- Also new in 1.6: in the Extra heroes mod, three more heroes, eleven in all: Elrond, the black Uruk-hai and Sauron, each with his own model, weapon, moves, cutscene and upgrade-screen figure and pictures, unlocked by the 6th, 7th and 8th finish of the Tower of Orthanc as Isildur (a finish now counts on the save you play; before, it could go to the last save in the save list). Every new hero has his own voice in play and his own sounds when chosen on the character select, his own ammo counter and upgrade-screen items, his own upgrade names and a posed figure on the pause screen. Everyone kicks and parries with the heroes' kick and parry, falls when he dies, and stands still as he aims. Frodo throws stones (no bow), the Uruk-hai throws burning rocks and his heavy attacks both strike, Sauron's bolt is red and he is tougher; Gandalf has an ECTS HUD portrait; Sauron comes last on the character select. A crash loading Fangorn and the Helm's Deep levels as Sauron, Lurtz or the Uruk-hai is fixed. Also: the first light attack swings (Frodo fell and Boromir flinched on the first press); Gandalf carries a sword in his right hand and his staff in his left, his beam from the staff; Frodo's model is matte, and whole on the end-of-level screen. Character shadows no longer break into stripes (the renderer read the wrong one of two surfaces at one address). Levels rebuilt by an older version are no longer reused. Tested on Windows: the three new heroes in Caradhras, Hornburg Courtyard and the Plains of Rohan (attacks, hit rates against Aragorn's, the Uruk-hai's rocks); the character select with 7 finishes. Under Wine: all seven heroes in Hornburg Courtyard (voices, counters, aiming and shooting, Frodo's stones, Sauron's red beam on the Deeping Wall, the Uruk-hai's heavy hits), Sauron, Lurtz and the Uruk-hai in Fangorn and Helm's Deep, the character select's order and sounds. On Linux: the game built from the published source.
- New in 1.8.1: the **picture galleries** (Concept Art, Production Photos) step with a click on the left or right half; the end-of-level screens' **Replay**, **Continue** and **level leaves** answer the pointer at high frame rates; **Window size** in Display Options; **Mouse steering** (Controls page, launcher, `[Input] mouse_steer`): the mouse turns the way the move keys go; unlocked frame rate is unlocked throughout again (set 60 before the in-engine scenes if heads stop turning or Sauron falls at the Gates of Moria's start); Frodo's and the Uruk-hai's shots hurt the Watcher.
- New in 1.8.2: the end-of-level upgrade screen's **level leaves** follow the pointer (hover and click); **Mouse steering** turns the hero where he stands and the move keys then go by his facing (W ahead, S back, A / D to the sides); Frodo throws his stones at the Watcher again, and they hurt it. **Ultra textures** (Display Options, launcher, `[Display] ultra_textures`): the GameCube release's high-resolution texture pack, from the `GLOS69` folder beside the game, in place of the game's own textures (textures are paired by their look).
- New in 1.9: **Ultra textures** without bundling the GameCube pack: the upscaled menus, level select, text and HUD come with the game, and the `GLOS69` pack (downloaded separately from its author) is prepared by the launcher on first use (see [Ultra textures](#ultra-textures-new-in-19)). **Internal resolution** up to **12x**, averaged down to the window properly (higher settings now also smooth edges). **MSAA** anti-aliasing (2x, 4x, 8x, with or without FXAA) in Display Options, the launcher and the ini, on Direct3D 11, Vulkan and OpenGL (Linux, macOS). Up to 512 particle effects at once (the game's 64 made the archers' fire pots go out when a beam or a fire rock was thrown). From 1.8.2: the in-engine cutscenes stay 4:3 in widescreen unless **Widescreen cutscenes** is on; the Uruk-hai's fire rock burns; right click on the character select unselects; menu buttons no longer press themselves.
- New in 1.9.1: **Mouse steering** works as aiming with the ranged button does (as in the prologue, where Isildur has no bow): hold **W** (or push the stick up) and the mouse is the stick -- he runs the way the mouse has pushed it, from the camera's view, and stops when you let go of W. **Sauron's sprint is removed** (and its option): Shift and the left stick click are the left stick click again. The Uruk-hai throws his rocks (and their upgrades) at the Gates of Moria too, where he had thrown arrows, and they hurt the Watcher.
- New in 1.9.2: **languages** (see [Languages](#languages-new-in-192)). **Mouse steering**: **Walk** walks while steering (hold Walk with W). With Ultra textures and the GameCube pack, Legolas's mission results picture is his own (the pack has none of him, and its second picture of Aragorn had been paired with it).
- New in 1.9.4: **performance** -- the GPU commands run on a render thread of their own beside the game, with far less CPU work per draw: several times the frame rate on a fast PC, and the graphics card kept busier. **Game time keeps real time at any frame rate** (above the monitor's rate, the world ran slow and felt delayed). **Frame rate** choices 30, 60, 75, 90, 100, 120, 144, 165, 200, 240, 300 and unlocked, held in the menus too. **Vulkan** is no longer held to the monitor's refresh rate with VSync off. The exclusive **Full-screen resolution** list goes from small to large (right is higher). A slowdown to 20 fps with Gandalf or Sauron at the Gates of Moria (over 300 textures in use at once) is fixed. "Load game" no longer stops on a black screen.
- New in 1.9.3: **character shadows without the pale streaks** -- the stripes and lines that crossed shadows in every level, at any resolution, are filled in (Direct3D 11 and Vulkan). **Exclusive full screen** (Display mode) at a **Full-screen resolution** of your choice. Controllers: in the menus a part push of the left stick moves the choice as the d-pad does (the main menu wanted a full push); a held stick no longer runs down the Options pages at high frame rates; **Mouse steering** and **mouse aiming** take the left stick only for the keys and mouse, so a controller's stick and LT work with them on. The **Secret Codes** page shows your controls (its L trigger as the prompt style you chose). With the ECTS HUD, the **orange health pickups** work again when other settings are added.
- **1.8** brings together everything from 1.7.1 to 1.7.8 (below) and the following.
- Also new in 1.8: **Internal resolution** goes up to **8x**, with **4.5x** for 2160 lines (3840 x 2160 at 16:9). **Tab** (the Back key) backs out of the Options pages. The mouse wheel scrolls the Options pages. Messages such as "Game Saved. Press the A button to continue" name your keys with the keyboard (Enter, which also closes them). Clicking **Replay** on the results screen and the level leaves on the upgrade screen after a level works, as does the "Are you finished purchasing upgrades?" Yes / No.
- Also new in 1.8: a **left click skips the intro films** (the logos before the title screen).
- Also new in 1.8: the **Options** menu has three pages -- General (the game's own), Display and Controls -- with every F10 setting, and the Controls page lists every **key binding** (scroll down to them): choose one and press the key or mouse button to bind (Esc cancels, Delete clears), left / right changes the controller button. The **mouse wheel** scrolls the pages (and moves up / down in the game's menus). A new **Internal resolution 4.5x** draws 2160 lines (3840 x 2160 at 16:9).
- Also new in 1.8: in the Extra heroes mod, **Sauron's sprint** is back (left stick click / Shift, rebindable): a press turns it on and another off (or "Hold" in the options; it also ends when he stops), and it no longer changes his moves -- he simply runs faster, easing in and out, with no skip or jump at the press (F10 / launcher / Controls page: **Sauron's sprint**, `[Input] sprint=toggle|hold|off`). A new hero's weapon no longer turns up in other characters' hands in scenes (an archer with Sauron's mace on the Plains of Rohan). A crash on the upgrade screen after a level (pointing at a leaf) is fixed, the "Are you finished purchasing upgrades?" Yes / No follows the pointer, and the new Options pages are pointed at and clicked directly (they flipped between rows at high frame rates).
- Also new in 1.8: the mission select chooses the place under the pointer at once (no more walking there along the map's paths, which could get stuck); the leaves (Back, Continue) light up under the pointer and only the leaf and its word count as a click; the upgrade screens' level leaves and Back / Continue can be pointed at and clicked; the pointer shows from the first menu; **mouse aiming** for ranged attacks (hold the ranged button, move the mouse) with **invert**, a **mouse sensitivity** (pointer and aiming) and a **pointer size**.
- New in 1.8: an **Exit** item on the main menu, under Credits, in the menu's own lettering, that closes the game; a **mouse pointer in every menu** (point to choose, click to press, click values and sliders in the options, walk the mission select's map, turn the character select by clicking left or right; right click is back), with an F10 / launcher option to turn it off (**Mouse pointer in menus**, `[Input] menu_mouse`). Tested under Wine: the main menu (Exit chosen by the controller and by a click closes the game), the options page (rows, values, the music slider), the load screen, the mission select, the character select (left, right, three quick clicks), the pause screen and its Yes / No.
- New in 1.7.8: **controller button prompts follow your bindings**: each prompt shows the button of your own controller bound to that action (rebind A to RB and the A prompts show RB), as the keyboard prompts do, in play, on the menus and on the loading screens' hints. A new **Xbox One** prompt style (Button prompts: Xbox One): black buttons with coloured letters, LB / RB / LT / RT, View and Menu, and an Xbox One pad on the loading screens' controls pictures. 1.8: two new F10 / launcher options, **Menus** (fill the screen, the default, or the original with its margin) and **Movies in widescreen** (as made, the default, or fill the width with the films' black bands cut off); `[Display] menu_fill`, `movie_fill`. In the Extra heroes mod, the fall-through catch also works for fast falls and at a level's start.
- New in 1.7.7: the main menu, the mission select, the load and save screens and the character select also fill the screen (they draw some 3D, so 1.7.2's fill passed them by and left a black margin all round).
- New in 1.7.6: a **Walk** key for keyboard and mouse (B, held; rebindable in the launcher and F10), for every hero. The log now says when the controller's Start is pressed and whether it reached the game (looking into Start not working on some controllers: here it pauses the game and works in the menus).
- New in 1.7.5: in the Extra heroes mod, a new hero who falls through the ground (Sauron at the Gates of Moria, reported at an unlocked frame rate) is put back where he last stood, a few steps before. The cause was not found: it did not happen in testing here.
- New in 1.7.4: in the Extra heroes mod, Sauron's sprint is removed (Shift and the left stick click are the left stick click again, as before 1.7.2), and Sauron keeps all his own moves again: with some of the heroes' moves fitted to him he fell through the ground at the Gates of Moria.
- New in 1.7.3: every new hero fights with his own attacks again, as in 1.6: 1.7's finishing thrust and some of its scene motions had put Aragorn's swings into Gandalf's and Sauron's light attacks, and 1.7.2 gave Lurtz and the Uruk-hai the heroes' attacks. Their longer reach, Lurtz's toughness and the water fix stay.
- New in 1.7.2: menus, loading screens and the upgrade and results screens fill the screen (the game drew them inside the old TV safe area, a black margin all round; `RECOMP_MENU_FILL=0` puts it back). In the Extra heroes mod: Sauron's **Sprint** key (Shift / left stick click, rebindable; removed in 1.7.4); Lurtz's and the Uruk-hai's blows reach further and Lurtz is tougher; new heroes no longer go sideways in water or on other moves their own sets lacked; the pause screen's figures stand upright. Tested under Wine: Sauron's sprint motions and set switching, the pause screen's figure, the menus at 16:9 with stretch.
- New in 1.7.1: in the Extra heroes mod, the last ranged upgrade changes the shots: Gandalf's beam white, Sauron's bolts red and blue in turn, the Uruk-hai's stones burning rocks, every third bursting as the Orthanc archers' fire pots do (fire on the ground that hurts). Tested under Wine in Hornburg Courtyard with and without the upgrade.
- New in 1.7: in the Extra heroes mod, the new heroes are whole in the in-game cutscenes and play the heroes' own moves there, fitted to their bodies (Fangorn's opening scene, Balin's Tomb's, the Gates of Moria's); the finishing thrust on a foe on the ground is the heroes' for everyone, the blade turned point down; Gandalf and Lurtz take less damage (the health bar is the same size); Gandalf's voice is Gimli's, a little deeper. Tested under Wine: Fangorn's opening scene as Gandalf, Sauron and Lurtz, Balin's Tomb's as Gandalf, Frodo and Lurtz, the Gates of Moria's opening scene as Gandalf, and the Gates of Moria's later scenes' moves drawn on Gandalf's model.
- Tested in 1.2: every level again on Windows (all 14 load and reach gameplay, 30 / 60 / unlocked fps) and on Linux (all 14; a crash entering Balin's Tomb on Linux was found and fixed), and a start under Windows 7 compatibility.
- Known in 1.2: rarely (once in about 30 test runs, on a heavily loaded two-core machine) the game froze during the movie before Amon Hen; the next try was fine. On PlayStation prompts, the loading-screen and message text still names the Xbox buttons ("Press the A button"); the pictures and button icons change.
- Fixed in 1.2: controls freezing with the last input held (Windows builds); a crash in Helm's Deep at 60 fps / unlocked; with the ECTS HUD mod, Westfold's villager faces no longer show on the results and upgrade screens after the level.
- New in 1.2, **highly experimental**: the [Extra heroes](#extra-heroes-extra_heroes1-highly-experimental) mod (off by default).
- Fixed in 1.1.3: controller rumble now reaches the PC controller (it was received and dropped); sound now comes back by itself if the audio device stops (headphones, a default-device switch, a display going to sleep), which may be what silenced it after an upgrade screen; with the ECTS HUD mod, its gauge and portrait no longer appear over the mission results screen's "Replay" and title.
- Fixed in 1.1.2: after about eight minutes of play the game slowed right down, lost controller input or crashed (often noticed after a cutscene). A log counter overflowed and the game then wrote millions of log lines a second.
- Fixed in 1.1.1: a freeze during movies (Amon Hen, the prologue's opening), a freeze while loading on a busy PC, black boxes over the lava heat haze, and "disc is dirty" errors when another program reads a game file at the same moment.
- Character shadows: drawn as stripes until 1.1; they are now the game's soft shadows, and match the reference software renderer. Some scenes may still look off; reports with a screenshot are welcome.
- Audio: some crackling or music cutting out can still happen, mostly at 60 fps and unlocked.
- The 60 fps and unlocked modes are experimental. If a crash happens, the end of `twotowers_log.txt` says where (`[CRASH]`); please send it.
- Widescreen is experimental: a few full-screen effects may not line up at the edges.
- Bug reports are welcome. Please include `twotowers_log.txt`.

## Credits and licences

- **xboxrecomp** (the recompiler and Xbox runtime) is by sp00nz and is **MIT** licensed (`xboxrecomp-0.12.0/LICENSE`). This repository includes it with the changes described in `CHANGES.md`.
- Parts of the runtime are taken from **[xemu](https://github.com/xemu-project/xemu)**: the MCPX APU audio and the NV2A register definitions. They are **LGPL-2.1-or-later** and copyright of their authors (see `xboxrecomp-0.12.0/NOTICE` and `xboxrecomp-0.12.0/LICENSES/`). Their complete source is included here, so you can rebuild the program with modified versions of them, as the LGPL requires.
- The port-specific code (`twotowers/`, `generate.py`) is released under the **MIT** licence (see [LICENSE](LICENSE)).
- All game content belongs to its respective rights holders; see the [disclaimer](#disclaimer).
