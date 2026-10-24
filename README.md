# The Two Towers Recompiled v1.9.7

A native PC port of *The Lord of the Rings: The Two Towers* (Xbox, USA, 2002), made by static recompilation. It needs your own copy of the game, either the disc image (ISO) or an extracted folder. No game data is included.

> ⚠ **Work in progress.** The game is fully playable, but not everything works perfectly yet. Unlocked frame rates work but are experimental. It is recommended to play the game at no more than 1000 fps. See known issues. 

## Downloads

| File | For |
|---|---|
| `TwoTowers-Recompiled-v1.9.7-win64.zip` | Windows 7 SP1, 8.1, 10, 11 (64-bit) |
| `TwoTowers-Recompiled-v1.9.7-linux-x86_64.tar.gz` | 64-bit Linux, glibc 2.27 or newer |
| `TwoTowers-Recompiled-v1.9.7-macos-x86_64.tar.gz` | macOS 10.15 or newer, Intel or Apple silicon (through Rosetta 2) |
| `TwoTowers-Recompiled-v1.9.7-source.zip` | Source code |


**Ultra textures, GameCube compatibility pack:** download the GameCube high-resolution texture pack (`GLOS69`) separately from its author. It is not part of this release.
https://www.moddb.com/mods/the-two-towers-textures-4k-ai-remaster/addons/the-two-towers-textures-4k-ai-remaster-575gb-version-native

Updating from 1.9.6 or earlier: unpack over your old folder, or into a new one and copy your `game_files` folder. Your saves and settings carry over. Copy the new `textures_ultra\pairs.txt` too (it comes with the download); a prepared `GLOS69` pack is not prepared again.

## What the port adds

- **Runs natively** on Windows, Linux and macOS (experimental), from your own disc.
- **Languages** *(new)*: 33 languages besides English for every text in the game, subtitles included, plus the launcher and the F10 menu.
- **Resolution and widescreen**: internal resolution 1x–12x, 4:3 to 32:9 with the HUD at the screen's edges, widescreen cutscenes, menus that fill the screen.
- **Picture**: Direct3D 11, Vulkan (DXVK) or OpenGL; MSAA and FXAA, anisotropic filtering, brightness, sharpening and color.
- **Ultra textures**: sharper menus, text and HUD, and every texture with the GameCube pack.
- **Frame rate**: 30 fps as on the Xbox, or 60, 75, 90, 100, 120, 144, 165, 200, 240, 300 or unlocked (experimental).
- **Keyboard and mouse**: every action rebindable, a Walk key, a mouse pointer in every menu, mouse aiming and mouse steering.
- **Controllers**: Xbox, PlayStation, Switch Pro and others with rumble; prompts that match what you play with.
- **Menus**: Display and Controls options pages in game, the F10 settings panel, an Exit item.
- **Couch co-op** *(alpha)*: two players on one PC, each with their own hero, controller, weapons, health, ammo, experience and upgrades. With thanks in part to [Volverine-Dude](https://github.com/Volverine-Dude); see [COOP.md](COOP.md).
- **Versus** *(new/alpha)*: player 1 against player 2, one on one, on every level but Westfold, with any hero; see [COOP.md](COOP.md). More modes characters and maps to come in future updates. 
- **Achievements**: earned as you play, listed on the F10 panel's Achievements page.
- **Mods**: the ECTS 2002 demo's HUD, orange health pickups, and seven new playable heroes.
- **Difficulties**: New difficulty (impossible difficulty) added to the game.

## What's new in 1.9.7

### Versus
Player 1 against player 2, one on one, on one PC. Main menu > **Versus**.

- **Arenas:** every level but Westfold: Weathertop, Balin's Tomb, Amon Hen, Fangorn Forest, the Gates of Moria, the Plains of Rohan, the Road to Helm's Deep, the Deeping Wall, the Breached Wall, Hornburg Courtyard, Orthanc, Orthanc's summit and the prologue. 
- **Heroes:** Aragorn, Legolas, Gimli and Isildur; with the Extra heroes mod also the seven extra heroes and three Versus-only exclusive hero's added: the **Nazgul**, a **Shield Orc** (his shield halves every blow until it breaks) and **Eomer**. Each cries out in his own voice.
- **Rounds:** choose 3, 5 or 7 rounds to win. A countdown starts each round; the first to the rounds chosen wins, then a results screen (hits, damage, biggest hit, arrows on target, fastest round, awards).
- **Fair fights:** both heroes have the same health and every move. A guard breaks after four blocks in a row, kicks knock back but do almost no damage, ranged attacks lock on to the other (a step aside dodges them), and health and ammo pickups appear each round.
- **Your saves stay yours:** both players are at the top level for the match only. Your heroes' levels, experience and upgrades, and the extra heroes' records, are put back as they were when you leave Versus.
- **Devices:** as for co-op (Options > Co-op Options > P1 Device / P2 Device). With P2 Bot on, the game fights player 1.
- **Its own menu background, results screen and sounds** in the `versus` folder (`menu_bg.bmp`, `results_bg.bmp`, `fight.wav`, `match_win.wav`); add `round_win.wav` for a round won, or replace any of them with your own.

See `COOP.md` for the details.

### Also new
- **Achievements:** 47, for the story and for Versus, noted in the corner as you earn them and listed on the F10 panel's **Achievements** page. They're kept in `achievements.txt`, only on your PC.
- **Impossible difficulty:** past Expert, any enemy's second blow fells you. It's right of Expert on the New Game's difficulty screen, or in Options > General Options, and works in co-op too.
- **F10 > More:** colour grading (Cinema, Rich, Shadow) and its amount, movie sharpness, pause or mute while the window is in the background, and achievements on or off.
- **F9** hides the HUD; **F8** (or F12, Print Screen) saves a screenshot in the `Screenshots` folder beside the game.

### Co-op
- **Camera:** playing the levels with Camera Zoom (Options > Co-op Options) on **Normal** or **Wide** is recommended, so both heroes stay in view.

### Fixes
- **The Breached Wall at high frame rates:** fire and blasts did their damage every frame, so above 30 fps the catapults broke the gate far too fast (at 500 fps within a minute). Their damage now adds up the same at any frame rate.
- **Credits** scroll at the same speed at any frame rate (above 30 fps they ran too fast).
  
## Tested (1.9.7)
- Linux (OpenGL): Versus matches on the arenas with the P2 Bot and scripted input; the main menu's Co-op and Versus with the keyboard, the mouse and a lost press; the campaign's progress kept when the Versus menu opens and put back when it's left (not yet checked after a full match).
- Windows: Versus played by hand on the author's PC.
- Windows, Linux and macOS builds compile.
- Not tested: the macOS build on a Mac.

## What 1.9.6 added

### Couch co-op (alpha)
Co-op was in development for the main port when [Volverine-Dude](https://github.com/Volverine-Dude) published their own implementation in the [TooTawersRecomp-coop](https://github.com/Volverine-Dude/TooTawersRecomp-coop) fork. 1.9.6 brings the two efforts together on top of 1.9.5, and this release is thanks in large part to their work. Thank you!

- **Two players on one PC** play the story levels together on one shared camera. Each has their own hero, controller, weapons, health, ammo, experience and upgrades.
- **Starting:** Main menu > **Co-op** > New Game or Load Game; then each player picks a hero (player 2 can't take player 1's).
- **A hero who goes down** comes back beside the other after a wait. Both down is a game over.
- **After a level:** each player's stats and shop in turn, then the save. Player 2's experience and purchases are saved with his hero.
- **Co-op Options** (Options, and the pause menu in a co-op game): Difficulty (Easy, Normal, Expert), Player Icons ("P1" / "P2" over the heroes), Camera Zoom (the camera zooms out as the heroes part), P2 Teleport, and P2 Bot (the game plays player 2).
- **New in this release, on top of the fork:**
  - **Each player's device** (Options > Co-op Options > P1 Device / P2 Device): Automatic, Keyboard & Mouse, or any controller connected. With one controller, player 1 takes the keyboard and mouse and player 2 the controller; with two, one each. The mouse steers and aims only for the keyboard's player.
  - **Pickups: Double** (default): each hero can take a health or ammo pickup once.
  - Co-op support for Linux and macOS (experimental) (settings for those platforms were not read there).

See `COOP.md` for the details.

### Fixes
- **Upgrade screen at high frame rates:** the points count down at the same speed at any frame rate. Above 60 fps the count, and the purchase sound playing with it, were cut short.

## Tested (1.9.6)
 Windows: couch co-op played/tested through the first seven levels and Tower of Orthanc.
- Linux (OpenGL): co-op in Weathertop and Balin's Tomb with the P2 Bot and with scripted input for player 2 -- both heroes, the P1/P2 markers and the two-player HUD; player 2 on the keyboard (port 2) while player 1 stays still; Double pickups (player 2 refilled twice from one pickup); the Co-op Options page with the new rows.
- Windows, Linux and macOS builds compile.
- Not tested by hand: a second physical controller, Windows play, the macOS build on a Mac.

## What's new in 1.9.5

### Performance: more of the game runs as native code
- **The game's own code runs faster.** The translated game code kept the Xbox processor's registers in memory and wrote every change back. Its floating-point and integer registers now stay in the PC processor's registers inside each function. Every changed function was run against its old version from the same starting state, and gave the same results.
- **The busiest Direct3D library functions are written in C.** The game's draw calls, the vertex setup before each draw and its shader constants are now hand-written code instead of translated Xbox code. They send the graphics chip exactly the same commands, checked call by call over millions of draws, at about half the cost.
- **The controller is read directly.** The game no longer goes through an imitation of the Xbox's USB chip to read the controller; it asks the PC's own input (controller, keyboard, mouse and your bindings).
- **The render thread does less per draw.** Draws with nothing changed between them are sent to the graphics card as one. Vertex data and commands are copied with faster copy routines, and graphics state lookups are quicker.
- **Lighter frames.** Busy gameplay ran at about 450 fps on average on a fast PC in testing (Windows, Direct3D 11), with the game's code down to about 1.7 ms a frame. Earlier test logs showed over 3.5 ms in busy scenes.
- **Fewer draw calls.** Draws that differ only in their vertex constants are sent to the graphics card as one draw; the constants travel with the vertices.
- **Linux gets the render thread** too (OpenGL), and the game no longer spins a core while it waits for the graphics card.

### High frame rates
- **Animations keep their speed.** Each frame an animated character moved on by whole milliseconds of (frame time x animation speed), the rest dropped. At 500 fps animations playing below full speed kept about half their time or less: slow attacks, cutscene animations that stopped short, and heads that did not turn toward what a character looks at. The fractions are now kept and added up.
- **Falling keeps its speed.** A falling character's speed grew once per frame, so at high frame rates anything that jumped or stepped off a ledge fell many times too fast and landed deep in the ground. It now grows with time, exactly as at 30 fps.
- **Small moves are kept.** Movement under 0.01 units a frame was dropped, which above about 300 fps was most of it (characters stood still or caught on the ground). The threshold now shrinks with the frame time.
- **Unlocked: frames at least a millisecond apart.** After a late frame the next few could come through back to back, under a millisecond apart, which the game's clock could not keep straight (the Westfold intro's run played slowly). Unlocked now means up to 1000 frames a second, each given its proper time.

### Sound
- **No more cut-outs or crackle.** Whenever the game updated its sounds, the emulated sound chip skipped that slice of audio: 3-10% of all sound came out as silence in short slices, and sounds seemed to stop part way, more the higher the frame rate. The sound now plays on, and the game's sound commands are applied between slices.
- Volume changes glide across each slice instead of stepping.

## Tested (1.9.5)
- On Windows by the author: gameplay in busy scenes at unlocked frame rates (Westfold and others), with profiles of each change; the F10 menu fix; animation speed, the prologue and sound at up to 1000 fps.
- On Linux at fixed frame times (33, 8, 2, 1 and 0.7 ms): animation time, falls and landings, and the world clock measured against 30 fps; sound lengths and gaps measured at 30 fps and unlocked.
- Under Wine (Direct3D 11): levels load and play, fights render correctly.
- On Linux: gameplay renders correctly; the native Direct3D functions checked against the translated ones call by call (14.7 million calls); every function changed by the register work compared old against new (8,348 identical, none different).
- Windows, Linux and macOS builds compile.
- Not tested by hand: the macOS build on a Mac.

## What 1.9.4 added

### Performance
- **Higher frame rates/performance improvements.** The game's graphics commands now run on a thread of their own, beside the game, with far less processor work per draw (Direct3D 11 and Vulkan). On a fast PC this gives several times the frame rate of 1.9.3 and keeps the graphics card busier and usage scales better. 
- **Game time improvements for higher frame rate.** Above the monitor's refresh rate (usually above 240 hz/fps) some animations in the game world would move slowly and feel delayed. They now run at the right speed at high frame rates.
- **A drop to 20 fps** with Gandalf or Sauron at a certain spot in the Gates of Moria is fixed. 

### Frame-limiter and display
- **Frame rate limiter options** - 30, 60, 75, 90, 100, 120, 144, 165, 200, 240, 300 and Unlocked (Options → Display, F10, the launcher, or `[Display] fps=165`). The limit holds in the menus too.
- **Vulkan** is no longer held to the monitor's refresh rate with VSync off.
- The exclusive **Full-screen resolution** list goes from small to large, so the right arrow goes to a higher resolution, as Window size does.

## What 1.9.3 added

### Character shadow improvements
The stripes and thin lines that crossed character shadows in every level, at any resolution, are not fixed completely, but some of the shadows are now filled in more (Direct3D 11 and Vulkan).

### Exclusive full screen
- **Display mode** now has **Windowed**, **Borderless** and **Exclusive** full screen (Options → Display, F10, the launcher, or `[Display] fullscreen=2`).
- **Full-screen resolution** chooses the display's resolution for exclusive full screen: the desktop's, or another the display offers, at its highest refresh rate (`[Display] fs_resolution=1920x1080`). Changing it keeps you on the Display page.
- The window and mouse follow every resolution change, so the pointer no longer drifts and W A S D keep working in the menus.

### Controller fixes
- In the main menus, a part push of the **left stick** moves the choice as the d-pad does. The main menu used to need a full push, and a push slightly to the side often did nothing. A held stick steps at the d-pad's pace.
- A held stick or d-pad no longer runs down the **Options pages** at high frame rates.
- **Mouse steering** and **mouse aiming** now take over the left stick only for the keys and mouse. Before, pushing a controller's stick up with steering on, or holding its LT with mouse aiming on, gave the stick to the mouse.

### macOS updates (Apple silicon)
The 1.9.2 Mac build stopped at start-up on Apple silicon. Three fixes, from a player's report and tested patch (M2 Pro, macOS 27.2, Rosetta 2):
- **The game window opens.** macOS only lets the main thread make windows; the game drew no window ("NSWindow should only be instantiated on the main thread"). The window now runs on the main thread, the game on its own.
- **No crash once the renderer is up.** The OpenGL picture could be drawn from a second thread that had no OpenGL context (a crash in `glBindFramebuffer`).
- **No crash at the first sound.** Rosetta 2 reports the step the audio-register handling takes as a breakpoint (`unhandled exception 0x80000003`); it is now recognised as the step it is.
- A crash log line now also names the function it stopped in.

### Other fixes
- **Vulkan without a separate download.** DXVK 3.0.2 now comes in the Windows download's `dxvk` folder: choose **Vulkan** as the Renderer in the launcher. Direct3D 11 stays the default.
- The **Secret Codes** page shows your controls: its L trigger follows the button prompts you chose (as R did), and its text names your keys with keyboard prompts.
  
## Tested
- Under Wine (Direct3D 11): levels load and play with the shadow repair on, at 1x and 4x internal resolution; exclusive full screen starts; the main menu with scripted left-stick pushes at 30 %, 40 %, 60 % and full (both ways), and a held stick against a held d-pad; the orange pickups and the ECTS HUD.
- Linux (OpenGL): a level from boot. Windows, Linux and macOS builds compile.
- macOS: the reporter's version of these fixes, in their own build, reached the renderer, sound and the intro screens at 30 frames a second on an M2 Pro (Rosetta 2). This release's Mac build (with the window fix reworked) has not been run on a Mac; full play, controllers and Intel Macs are untested.



## What 1.9.2 added. 

### Languages
- Choose one in the launcher's **Language** list (on Linux and macOS: `./twotowers-setup`, then **l**). English, French, German, Spanish (Spain and Latin America), Italian, Portuguese (Brazil and Portugal), Dutch, Swedish, Danish, Norwegian, Finnish, Polish, Czech, Slovak, Hungarian, Romanian, Croatian, Turkish, Greek, Bulgarian, Russian, Ukrainian, Vietnamese, Indonesian, Japanese, Korean, Chinese (Simplified and Traditional), Thai, Hindi, Hebrew and Arabic.
- Everything the game writes is translated: menus, hints, upgrades, the results and pause screens, credits, and the subtitles of the scenes and the bonus interviews. So are the port's own words: Options pages, key names, button hints, the extra heroes' names and upgrades, the launcher and the F10 panel.
- Letters the game's fonts lack are added to them at each font's size and weight, and to the Ultra textures' upscaled fonts too, so text stays sharp in every language. Hebrew and Arabic read right to left; Chinese, Japanese and Thai lines break between words.
- The `lang` folder holds only the translations. The game's files are copied with the language's text into `lang_cache` on first use (a few seconds); your game files are not changed. Spoken dialogue stays English.

### Fixes
- **Legolas's mission results picture** showed Aragorn with Ultra textures and the GameCube pack (the pack has no picture of him, and its second picture of Aragorn had been paired with it). It is his own now.
- **Mouse steering**: hold Walk with W and the hero walks while steering. 

## Tested
- Under Wine (Direct3D 11 and software): the title screen and menus in French, Japanese and Arabic; the largest level loads in Japanese and Chinese; the upscaled fonts of Japanese, Polish and Russian (Ultra textures); the results screen as Legolas with Ultra textures; the launcher in Japanese (Wine has no Japanese font, so its words showed as boxes there; Windows has the fonts).
- Linux (OpenGL): the title in Russian with the upscaled font. Windows, Linux and macOS builds compile.




Known issues

    - Visual bugs: Character shadow bug. Sometimes the character shadows can appear stretched out or stringy, especially for larger NPC's. Some visual bugs with lighting as well. With the extra heros mod enabled, replaying the second level can sometimes show an odd texture covering the top of the upgrades screen after completing the level. This can be fixed temporarily by turning the mod off.
        - Vulkan: If you get any black screens when using the Vulkan rendering option, switch back to DX11. Also make sure that the dxvk folder is in the same folder as the games executable.
    - Extra Heroes: Kicking animations and their range during the Deepening level can be off. Kicking works fine against enemies, but some of the new characters have to be angled to the side to kick down the ladders. Again kicking works fine against enemies but not the ladders during this level. NPC's like Sauron, Frodo, and Lurtz can kick them just fine, but Elrond, Boromir, the Uruk and Gandalf may have some trouble and need to be angled to the side.
    - Extra heroes: Sauron and Lurtz are too tall for the end-of-level upgrade screen and are cut off.
    - Extra heroes: your hero takes Aragorn's place in the cutscenes; where his own character also appears (Boromir, Gandalf and Frodo in Balin's Tomb), such a scene shows him twice. Also when playing as characters that have new weapons and ranged attacks, some of those weapons may show up on other NPC's. This doesn't happen with the mod turned off.
    - Extra heroes: Currently the models used for the upgrade menu in the in game pause menu show up mangled. The models in the actual upgrade screen after a level is completed appear normal and play their animations.
    - Versus (1.9.7) is new: the arenas are the story levels without their enemies, so parts of the bigger maps can't be reached yet, and Amon Hen's arena ends 50 units from the start.
    - Co-op is an alpha: play the levels with Camera Zoom on Normal or Wide. Head markers are dimmed, not hidden, behind walls (Direct3D 11 only); the P2 Bot can get stuck (the lake before the Gates of Moria's boss) and has trouble with the Watcher's tentacles.
    - Widescreen is experimental: a few full-screen effects may not line up at the edges.
     - Chinese, Japanese and Korean use about 5–6 MB more of the game's memory for their letters; the largest levels load, but report anything that fails to load in those languages.
     - The "Loading..." picture and other text drawn into pictures stay English.
     - Versus: Most of the bigger maps in Versus mode can end up being cut off or not render properly after a certain point. It's recommended to keep fights in the playable area until the rest of the map can be used for future game modes.
     - FMV's can sometimes appear slow after alt-tabbing out of the game then back in. A temporary fix for this is to open the f10 menu or press start + back on controller and changing from borderless to fullscreen and back, or vice versa.
     - Versus/co op back screen: If you ever experience a black screen when clicking on co op from the main menu or starting a game in versus mode, wait a few seconds and then press select on keyboard/A on controller, then wait another second and press select or A again, and it will load you into the versus map. Press it too fast/soon and it will load the first level and its cutscene and you won't be able to skip it, so be careful. Once loaded into a versus game, backing out to the versus menu will not cause this problem to happen again when pressing "start".


Bug reports are welcome, especially for Mac and Linux users. Please include twotowers_log.txt, which is next to the game.

Screenshots and Gameplay:

<img width="3840" height="2160" alt="New folder (14) Screenshot 2026 10 24 - 02 00 33 07" src="https://github.com/user-attachments/assets/35a356e3-651a-4a39-8d08-a72952829d13" />


<img width="3840" height="2160" alt="Screenshot 2026-10-16 11-55-43" src="https://github.com/user-attachments/assets/5c4c73fe-d546-4551-bac5-64c1f05abbdd" />



<img width="3840" height="2160" alt="Screenshot 2026-10-16 11-56-39" src="https://github.com/user-attachments/assets/d4c031fa-852d-4448-95fd-0143d080b8d5" />


<img width="3840" height="2160" alt="Screenshot 2026-10-16 11-56-43" src="https://github.com/user-attachments/assets/882c535a-8f86-4078-a121-a02b1fd3760e" />

<img width="3840" height="2160" alt="Screenshot 2026-10-16 11-56-56" src="https://github.com/user-attachments/assets/b96fb9ba-cd0e-4372-a379-3bebd560968e" />



https://github.com/user-attachments/assets/5f94968f-4495-4255-bf60-1e4cf17cf93e




https://github.com/user-attachments/assets/449a0282-e7a1-411b-b408-3d6efe263bc8









https://github.com/user-attachments/assets/ac6a6be2-0a31-4150-a950-2fe51930cd3e




https://github.com/user-attachments/assets/b3eb1f43-a06b-4e18-8734-1e3cc1443df6




https://github.com/user-attachments/assets/00546d04-6453-4923-9260-b53e81e554df


https://github.com/user-attachments/assets/5fd31b9a-4d86-4f92-b8f9-b0e9238b8d4b




<img width="3840" height="2160" alt="image" src="https://github.com/user-attachments/assets/1b11ae21-d9d8-495d-a56b-872ac8aaf93b" />


<img width="1920" height="1080" alt="New folder (8) Screenshot 2026 10 16 - 11 50 36 24" src="https://github.com/user-attachments/assets/cd27127e-3580-4620-928b-2710676de681" />

<img width="1920" height="1080" alt="New folder (8) Screenshot 2026 10 16 - 12 16 28 12" src="https://github.com/user-attachments/assets/5e621dc1-031b-4191-b3af-47542d20ac47" />

<img width="4608" height="1296" alt="Untitled3" src="https://github.com/user-attachments/assets/c9254d2a-92aa-44ca-8b0d-40faea7f567a" />

<img width="4608" height="1408" alt="Untitle33d" src="https://github.com/user-attachments/assets/e4df0b16-506f-488c-ad76-4aafbfaf8b5f" />


<img width="3840" height="2160" alt="Screenshot 2026-10-16 12-00-37" src="https://github.com/user-attachments/assets/11b96e05-570a-4d35-ad20-39f3a484b7af" />

## Building from source

The game's code is not included. You translate it from **your own copy** of the game, then compile it. Nothing leaves your machine.

**You need:** Python 3.10+, CMake 3.20+ and Ninja, plus a compiler for your platform (below).

**1. Translate the game** (every platform). Point it at your extracted disc folder or its `default.xbe`. It checks that the file is the USA retail game and takes a few minutes:

    python generate.py "path/to/extracted/game"

**2. Build for your platform:**

- **Windows**: install [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) and put its `bin` folder on your PATH, then run:

      cmake -S twotowers -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
      cmake --build build

  This gives you `build/twotowers_recomp.exe` and `build/The Two Towers Recompiled.exe`. Copy both into a folder, together with `SDL2.dll` and the folders from a release download (`dxvk`, `lang`, `textures_ultra`, …).
  *Cross-compiling from Linux or WSL* (how the releases are made): with llvm-mingw on your PATH, configure with
  `-DCMAKE_TOOLCHAIN_FILE=$PWD/xboxrecomp-0.12.0/cmake/mingw-w64-x86_64.cmake` instead of the two compiler options.

- **Linux**: install a compiler and SDL2's development files, e.g. `sudo apt install build-essential cmake ninja-build libsdl2-dev` (on Fedora: `sudo dnf install gcc-c++ cmake ninja-build SDL2-devel`), then run:

      cmake -S twotowers -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
      cmake --build build

  This gives you `build/twotowers_recomp` and `build/twotowers-setup`. Put them in a folder with `twotowers/linux/TwoTowers.sh` and run that.

- **macOS** (Intel, or Apple silicon through Rosetta 2): install the Xcode command line tools (`xcode-select --install`), then `brew install cmake ninja sdl2 python`. On Apple silicon, SDL2 must come from an x86-64 Homebrew: `arch -x86_64 /usr/local/bin/brew install sdl2`. Then run:

      sh twotowers/macos/build-macos.sh

  It builds into `build-macos/TwoTowers-Recompiled`, with SDL2 copied into `lib`. Double-click `TwoTowers.command` there.

**3. Play:** on first start the game asks for your disc image, or copy your extracted game into `game_files` beside the program.
