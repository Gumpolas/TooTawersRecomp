# The Two Towers Recompiled v2.0.0

A native PC port of *The Lord of the Rings: The Two Towers* (Xbox, USA, 2002), made by static recompilation. It needs your own copy of the game, either the disc image (ISO) or an extracted folder. No game data is included.

> ⚠ **Work in progress.** The game is fully playable, but not everything works perfectly yet. It is recommended to play the game at no more than 1000 fps. Co op and Versus are still in alpha (see co-op.md for how these modes work). See known issues at the bottom.

## Downloads

| File | For |
|---|---|
| `TwoTowers-Recompiled-v2.0.0-win64.zip` | Windows 7 SP1, 8.1, 10, 11 (64-bit) |
| `TwoTowers-Recompiled-v2.0.0-linux-x86_64.tar.gz` | 64-bit Linux, glibc 2.27 or newer |
| `TwoTowers-Recompiled-v2.0.0-x86_64.AppImage` | 64-bit Linux, glibc 2.27 or newer, one file |
| `TwoTowers-Recompiled-v2.0.0-macos-x86_64.tar.gz` | macOS 10.15 or newer, Intel or Apple silicon (Rosetta 2); not yet tested on a Mac |
| `TwoTowers-Recompiled-v2.0.0-source.zip` | Source code |

**Ultra textures, GameCube pack:** download the GameCube high-resolution texture pack (`GLOS69`) separately from its author. It is not part of this release.
https://www.moddb.com/mods/the-two-towers-textures-4k-ai-remaster/addons/the-two-towers-textures-4k-ai-remaster-575gb-version-native

**2.0.0 Credits music:** for copyright reasons, the new credits songs are not included in this release. There is a new `music` folder where two extra .wav files can be added for the credits, so the credits don't just loop one song ("Concerning Hobbits"). The two files must be named `credits2.wav` and `credits3.wav`, in lowercase (Linux and macOS go by case), as 16-bit PCM .wav. The author designed them to work best with two songs composed by Howard Shore:

- `credits2.wav`: "The Road Goes Ever On..., Pt. 2 / In Dreams" (feat. Edward Ross), the last 3 minutes 11 seconds (skipping the first 30 seconds)
- `credits3.wav`: "Rivendell", the first 1 minute 45 seconds

For copyright reasons, these are not included with 2.0.0 or any earlier or later release. For the complete experience, It is recommended to purchase copies of these two songs and edit them into`credits2.wav` and `credits3.wav`. Without them, the original credits song plays and loops as it always did.

Updating from 1.9.9 or earlier: unpack over your old folder, or into a new one and copy your `game_files` and `saves` folders. Your saves, settings and achievements carry over.


## What the port adds

- **Runs natively** on Windows, Linux and macOS (experimental), from your own disc.
- **Languages** *(new)*: 38 languages besides English for every text in the game, subtitles included, plus the launcher and the F10 menu.
- **Resolution and widescreen**: internal resolution 1x–12x, 4:3 to 32:9 with the HUD at the screen's edges, widescreen cutscenes, menus that fill the screen.
- **Picture**: Direct3D 11, Vulkan (DXVK) or OpenGL; MSAA and FXAA, anisotropic filtering, brightness, sharpening and color.
- **Ultra textures**: sharper menus, text and HUD, and every texture with the GameCube pack.
- **Frame rate**: 30 fps as on the Xbox, or 60, 75, 90, 100, 120, 144, 165, 200, 240, 300 or unlocked (experimental).
- **Keyboard and mouse**: every action rebindable, a Walk key, a mouse pointer in every menu, mouse aiming and mouse steering.
- **Controllers**: Xbox, PlayStation, Switch Pro and others with rumble; prompts that match what you play with.
- **Menus**: Display and Controls options pages in game, the F10 settings panel, an Exit item.
- **Couch co-op** *(alpha)*: two players on one PC, each with their own hero, controller, weapons, health, ammo, experience and upgrades. With thanks in part to [Volverine-Dude](https://github.com/Volverine-Dude); see [COOP.md](COOP.md).
- **Versus** *(alpha)*: player 1 against player 2, one on one, on every level, with any hero, enemies in some arenas, and Troll Battle; see [COOP.md](COOP.md). More modes, characters and maps to come in future updates.
- **Achievements**: earned as you play, listed on the F10 panel's Achievements page.
- **Mods**: the ECTS 2002 demo's HUD, orange health pickups, and seven new playable heroes (more in Versus).
- **Difficulties**: New difficulty (impossible difficulty) added to the game.

## What's new in 2.0.0

### Co-op fixes, by [Volverine-Dude](https://github.com/Volverine-Dude) (PRs #16 and #17)

- **Player 2's own character:** his own stats, damage and turning, his own combo meter and Perfect; his health upgrades, sword segments and upgraded arrows count. His heavy blows break shields.
- **Player 2's aim:** his lock holds and no longer blinks, he turns to it while aiming, and he can lock the **cave troll**.
- **Enemies** share their attention between both players. The extra enemy copies on some levels stay asleep until their originals come, and their blows spare the villagers (Plains of Rohan).
- **Expert** in co-op.
- **P2 Bot: Off or On** (When Idle is gone; it cut in on a controller). A gentle stick push counts as player 2 playing.
- **Show Upgrades Player 2** on the pause menu. Player 2's controller works on both upgrade pages, his shop and his stats screen after a level, and his Start pauses the game.
- **Button prompts** follow whichever controller was used last, on any port.
- Gimli as player 2 holds his throwing axe in his left hand while aiming; player 2 is hidden at a cutscene's very start.
- Fixes: the Weathertop freeze, the cutscenes' casts, the max health's scaling.

### Languages
- Adds **Welsh** (Cymraeg), **Old English** (Ænglisc), **Old English in the Anglo-Saxon runes**, **Old Norse** (Norrœnt mál) and **Latin** (Latina):
- Every language: the Versus and co-op menus, the notes, the ECTS HUD's labels and the achievements are translated.
- **Ultra textures:** every language's text is upscaled, all session long. It used to go blurry later in a session, or stay blurry in a language first played before the pack was made ready.

### Launcher (Linux and macOS)
- **F5 on the main menu** opens the launcher (mods, language, settings): on Linux the AppImage and `TwoTowers.sh` go straight to the game, so the mods were out of reach. (Windows has its own launcher, which starts the game.)
- **Linux launcher window** (zenity or kdialog, else the terminal menu): `TwoTowers.sh --launcher`, the AppImage with `--launcher`, or the menu entry's right-click **Launcher** action. It also opens once after the first install.

### Versus
- **Westfold is a Versus arena:** the whole map to fight over, with random starts, some of its orcs and Uruk-hai about, their own sounds and the level's own music, and the health and ammo pickups.
- **Exploding bomb stacks and carts on Westfold:** a few of them (not every one the level has), set off by either player with a blow or a shot (aim at one and the bow locks onto it). The blast hurts whoever stands near, the enemies too, and sets off the ones beside it. A blast you set off that catches the other player counts as your hit.
- **Plains of Rohan:** the players are drawn at their starts (their markers showed, but not them).
- **The Ent** now hurts the enemies on the Versus maps, light and heavy blows too, and breaks their shields; the enemies he fells fall and die as by any other blow (his blows hit nothing before).
- **Enemy sounds in Versus:** their grunts, cries, blows and deaths, and the sound of being hit, play as in the campaign (they were silent).
- **Orthanc Summit:** players no longer get stuck running on the spot at their starts (it happened after playing another arena first; only restarting the game cleared it). A player pushing the stick who still cannot move for 2.5 s is released, and put back on his last safe ground if need be.

### Other fixes/Linux fixes
- **Load Game with no saves:** Enter on "No saved games" takes you back to the main menu (it kept showing the message again).
- The **level 5 and level 10 achievements** require a real level-up (finishing the prologue as Isildur earned both).
- **Linux, laptops with an NVIDIA card** beside the built-in graphics: the game runs on the NVIDIA card (it ran slowly on the built-in chip). `TT_NO_PRIME=1` leaves the choice to the system.
- **Linux with an NVIDIA card: no more flickering.** Surfaces flickered grey and broke into flat patches, in the menus and in game and most when paused: the OpenGL renderer kept reading shader settings from graphics memory the NVIDIA driver had already handed back. Also: the characters' shadows repaired as on Windows, and graphics driver errors, if any, written to the log.
- **Linux: no more stutter every second** with the keyboard's button prompts (a search for the hints' words ran on the game's own thread).
- **Linux: the More page's Colour grading, Grading amount, Movie sharpness and Distant texture detail** now work properly (they did only on Windows).

## Tested (2.0.0)
- Linux: Campaign coop and Versus modes. Versus bot runs on Westfold (the arena, the bomb stacks by blows and shots from both players), the Plains of Rohan and the Orthanc Summit (Restart Match); co-op bot runs on Balin's Tomb and the Plains of Rohan; the F5 launcher and the Linux launcher window; every language's upscaled fonts at the main menu.
- Windows: the co-op changes were tested by their author with bots.
- Windows, Linux and macOS builds compile.
- Not tested: co-op with two real controllers; the macOS build on a Mac; both co-op players as Gimli.

## Known issues (2.0.0)
- In Versus, in the Plains of Rohan map, a player can still show without his body for about a second as a round starts.

## What 1.9.9 added

### The128ReBits' improvements
Made possible by [The128ReBits](https://github.com/The128ReBits).
- **More versus exclusive heroes:** the **Elven Warrior**, the **Uruk Captain** the **Cave Troll**, the **Ent** and **Saruman**, each with his own moves and weapons.
- **Animation improvements** for all the extra heroes. 

### Versus: new heroes and a new mode
- **New Versus heroes:** **Saruman** (his own staff casts), the **Ent** (Treebeard from Fangorn Forest: his walk, his blows, thrown rocks and sounds), the **Elven Warrior**, the **Uruk Captain**.
- **Troll Battle** (Mode: on the Versus menu): cave troll against cave troll in Balin's Tomb or Amon Hen, with twice the health, thrown logs, its own end picture, its own sounds for FIGHT and the match's end, its own results table and kept records.
- **Both players may pick the same hero** in Deathmatch.
- **Enemies in the arenas:** a few of the level's own orcs and Uruk-hai on **Amon Hen**, **Fangorn Forest**, the **Plains of Rohan**, the **Breached Wall** and the **Hornburg Courtyard**, spread over the map. They attack both players with their own sounds and hit as hard as a player does. 
- **Random starts:** each match begins somewhere else on the map, and later rounds go on where the players stand. Starts are always on the map's ground (Balin's Tomb: its hall floor, no ledges).
- **Explore the whole map:** the levels' parts are joined (Amon Hen from the start to Lurtz's ruins), every part is drawn, and the map's edges hold both players. The props keep their collision and still break.
- **The camera** keeps both players in view by the map's walls, and stands further back when the Ent or a troll is fighting.
- **Hits from any side:** blows land on a hero's side or back, and on the trolls' and the Ent's bulk.
- A horn at the end of each round and **a random level's music** each match (arenas with enemies keep their own).
- **Mount Doom** in Versus: the whole arena is playable for both players, over the lava and on its far side, with its music.
- **Restart Match** and **Restart Round** in the pause menu (Back / Y, or a click on the note at the top): either starts again from a new random spot; Restart Round keeps the score.
- The enemies' blows don't count before a round begins (the countdown and between rounds).
- The round's ammo and health pickups lie on the ground, where both players can take them.
- **Plains of Rohan:** starts on the open field, and both players are always shown.
- **Hornburg Courtyard:** the camera no longer zooms in by the west wall and stays zoomed (the level's own close view took over), and the players can't walk out through the yard's wall.
- The Versus bot rests after landing a hit (it could keep player 1 down in Troll Battle).


### Achievements
- **25 new achievements for Versus:** Troll Battle (win 3 or 10 matches, a flawless match, a 400 smash, 25,000 damage, a round under 20 seconds, 25 logs or rocks landed), the arenas' enemies (slay 25 and 150, win on all five enemy arenas), win 3 matches as each of the newer heroes (Saruman, the Ent, the Uruk Captain, the Elven Warrior, Lurtz, Sauron, Gandalf, Elrond, Boromir, Frodo, the Berserker), the Ent's rocks and a mirror match.
- **The note says why:** an achievement's note now shows what it was for, so there is no need to open F10 each time.

### Co-op Fixes
- **Player 2's bow** (or his own ranged weapon) shows for every hero.
- **Player 2's Perfect glow** shows on his gauge, as player 1's does.
- Player 2's weapons are drawn again after he respawns.
- Player 2 can kick the ladders down again beside a different player 1 hero.
- Player 2's special move names on his side, his own rumble on the second controller.
- The heroes take more hits in co-op (125% health).
- Player 2's arrows and blows hit the **Watcher in the Water**.
- Player 2 picks up the level's own arrows and health (the arrows by Balin's Tomb's cave troll), not only the enemies' drops.
- **No leash:** player 2 can roam the level on his own; when the two are far apart the camera goes over to player 1.
- **P2 Teleport on/off from the pause menu:** a note at the top while paused; Y (the I key) or a click on it switches it, without going to the settings.
- A stuck player 2 is moved beside player 1 only with P2 Teleport on.
- **Player 2 breaks what player 1 can:** boxes, Fangorn Forest's log, the Breached Wall's catapult, and the enemies' shields with heavy attacks.
- The Breached Wall's catapult bar is drawn whole under player 2's health bar (half of it was on top of his bar).

### Other Fixes
- **Linux, PlayStation 3 controller:** a DualShock 3 is read through the system's own driver. It was found and named but sent no input on some systems (issue #14).
- Versus starts no longer leave the screen black (Fangorn Forest, the Gates of Moria).
- A player stuck by a wall after a round can step back out.
- New Game: Easy, Normal, Expert and Impossible can be clicked with the mouse.
- The Walk key works for both players in co-op and Versus.
- Lurtz and the Uruk Captain fall onto the ground (their falls no longer float), and Lurtz's hit no longer lifts him.
- The Uruk extra hero is renamed to "Berserker"

### Linux AppImage
- `TwoTowers-Recompiled-v1.9.9-x86_64.AppImage`: one file with SDL2 inside (used when your system has none). Make it executable (`chmod +x`, or Properties > Permissions) and run it; the first time it asks for your disc image.
- The game's folder (settings, saves, log, game files) is `TwoTowers-Recompiled` beside the AppImage, or `~/.local/share/TwoTowers-Recompiled` when that can't be written. A newer AppImage updates the program there and keeps your saves and settings.

## Tested (1.9.9)
- Linux (OpenGL): Versus bot runs on Amon Hen, Fangorn Forest, the Plains of Rohan, the Breached Wall, the Hornburg Courtyard, Balin's Tomb and the Gates of Moria (random starts, enemies fighting both players, the Ent felling orcs); co-op runs in Fangorn Forest (player 2's respawn, his Perfect glow).
- Windows: played by hand on the developer's PC through the 1.9.9 test builds.
- Windows, Linux and macOS builds compile.
- Not tested: the macOS build on a Mac; the PlayStation 3 controller fix on the reporter's Linux system; the trolls breaking props.

## Known issues (1.9.9)
- Singleplayer/Co-op: Singleplayer is entirely unaffected by co op or versus mode except for the co op setting for double pickups. If the setting is enabled in the co op setting, it also applies in singleplayer. It is recommended to disable the setting while playing the base game, unless you prefer to have more health pickups. 
- macOS (OpenGL) is still experimental
- Versus spawns are made random for each map, but sometimes a player can still get stuck in a spawn. A "reset game/round" setting has been added to the pause menu for when this occurs. It resets the entire match or the specific round while also putting both players in different spawns/locations.
- Enemies/random NPC's around versus maps don't respawn entirely if a match is reset. If they were killed off during one of the rounds, they will stay that way until returning to the main menu and starting the match again. 


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
     - Versus/co op black screen: If you ever experience a black screen when clicking on co op from the main menu or starting a game in versus mode, wait a few seconds and then press select on keyboard/A on controller, then wait another second and press select or A again, and it will load you into the versus map. Press it too fast/soon and it will load the first level and its cutscene and you won't be able to skip it, so be careful. Once loaded into a versus game, backing out to the versus menu will not cause this problem to happen again when pressing "start".


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


## Linux: AppImage and PlayStation 3 controller install

**If clicking the AppImage does nothing**, start it from a terminal in the folder that it is located in:

```
chmod +x TwoTowers-Recompiled-v2.0.0-x86_64.AppImage
./TwoTowers-Recompiled-v2.0.0-x86_64.AppImage
```

- If it says something about **FUSE** (`libfuse.so.2` not found, "AppImages require FUSE to run"), install FUSE 2: `sudo apt install libfuse2` (Ubuntu 24.04 and newer, Linux Mint 22: `sudo apt install libfuse2t64`; Fedora: `sudo dnf install fuse-libs`; Arch: `sudo pacman -S fuse2`). Or run it without FUSE: `./TwoTowers-Recompiled-v2.0.0-x86_64.AppImage --appimage-extract-and-run`
- `./TwoTowers-Recompiled-v2.0.0-x86_64.AppImage --launcher` opens the launcher (mods, language, settings) first.
- Any error is also written to the terminal, and to `twotowers_log.txt` in the game's folder.

**PlayStation 3 controller (DualShock 3)**: the game requires and reads it through the system's joystick device (`/dev/input/js0`), for when it shows up but its buttons do nothing. To set that up and check it:

1. Install the joystick tools: `sudo apt install joystick` (Fedora: `sudo dnf install linuxconsoletools`; Arch: `sudo pacman -S joyutils`).
2. Start the game with the controller already connected and switched on.


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


