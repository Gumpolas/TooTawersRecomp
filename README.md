# The Two Towers Recompiled v1.6

A native PC port of *The Lord of the Rings: The Two Towers* (Xbox, USA, 2002), made by static recompilation. It needs your own copy of the game, either the disc image (ISO) or an extracted folder. No game data is included.

> ⚠ **Work in progress.** The game is playable, but not everything works perfectly yet. Play at **30 fps**, the frame rate the game was made for. If the game crashes or freezes, restart it: the same spot usually works on the next try.

## Downloads

| File | For |
|---|---|
| `TwoTowers-Recompiled-v1.6-win64.zip` | Windows 7 SP1, 8.1, 10, 11 (64-bit) |
| `TwoTowers-Recompiled-v1.6-linux-x86_64.tar.gz` | 64-bit Linux, glibc 2.27 or newer (Ubuntu 18.04+, Debian 10+, Fedora 28+, Steam Deck desktop mode, …) |
| `TwoTowers-Recompiled-v1.6-macos-x86_64.tar.gz` | macOS 10.15 or newer, Intel or Apple silicon (through Rosetta 2). **New and not yet tested on a Mac**: see below |
| `TwoTowers-Recompiled-v1.6-source.zip` | Source code (LGPL-2.1) |

Updating from 1.5.x or earlier: unpack over your old folder, or into a new one and copy your `game_files` folder (or point the launcher at your extracted folder again). Your saves and settings carry over. The Extra heroes mod's `heroes` folder is rebuilt automatically; the heroes' experience, upgrades and unlocks are kept.

## What's new in 1.6

### Everyone
- **Keyboard and mouse button prompts.** Every button icon can show the key or mouse button its action is bound to (a keycap such as K, SPACE or NUM 8, or a mouse with the button lit), and it changes as soon as you rebind a key. **Automatic** (the default) follows what you play with. Choose it in the F10 menu, the launcher or `[Input] prompts=auto|xbox|playstation|keyboard`. The prologue's hints in play change too.
- **Keyboard and mouse on the loading screens.** The loading screens that explain the controls on a picture of the Xbox pad show a keyboard and a mouse instead: the keys and buttons you use are lit, and each of the game's labels has your key beside it. The hint under the picture names your keys ("Hold F to switch to your Ranged Weapon. Press K or the left mouse button to fire. Use W A S D to aim.").
- **Vulkan (Windows):** Renderer → **Vulkan (DXVK)** in the launcher runs the game's Direct3D 11 renderer on Vulkan through DXVK. Download a DXVK release and copy its `x64\d3d11.dll` and `x64\dxgi.dll` into a folder named `dxvk` next to `twotowers_recomp.exe`. Without them the game uses Direct3D 11 and says so in the log. Experimental: see the known issues.
- **Widescreen HUD at the screen's edges:** in a picture wider than 4:3 the health sword and counters sit against the left edge and the prompts against the right; "HUD in widescreen" can put it back in the middle.
- **Fewer stutters.** Compiled shaders are kept in a `shadercache` folder beside the game, so a new effect stutters only the first time you ever see it. A small cushion of sound is kept queued at the output, so the sound no longer breaks up when a busy moment holds the game back.
- **Sharper on scaled displays.** The game window is DPI aware: on a 4K screen at 150–200 % scaling it is drawn in real pixels instead of small and stretched by Windows.
- **macOS download** (x86-64; Apple silicon through Rosetta 2). It is cross-built and has not been tested on a Mac yet. The first time, right-click `TwoTowers.command` → Open (the program is not signed by Apple); it asks for your disc image and installs the game files. SDL2 is included. Testing for the Mac version is welcome. 

### Extra heroes (still highly experimental, off by default)
- **Three more heroes, eleven in all:** **Elrond** (the elves' curved sword), **the black Uruk-hai** (his great sword and his own attacks; he throws **burning rocks**) and **Sauron** (his mace and his own swings; a **red** bolt of power, and he takes blows better). Unlocked by the **6th**, **7th** and **8th** finish of the Tower of Orthanc as Isildur.
- **Unlocks go with your game.** Loading a save brings that save's count of Isildur's Orthanc finishes; every save you write carries the count of the game you are playing — whichever slot it goes to, and whichever hero you played since. The save written after Isildur's finish counts one more. The 2nd finish unlocks Boromir, then one more hero with each finish. Before you load anything, the newest save's count shows.
- **Upgrades of their own.** Each hero's six named moves have names of their own instead of Isildur's — Sauron's Dance, Eye of Sauron, You Shall Not Pass, Shadowfax Charge, Sting's Bite, Ringbearer's Will, Horn of Gondor, Berserker Frenzy, … — and his ranged weapon is named and described for what it is: Frodo's **Shire Stones**, **Flintshot** and **Elf Stones**, the Uruk-hai's **Fire Rocks**, **Hot Rocks** and **Lava Rocks**, Gandalf's and Sauron's fire, Lurtz's black bows of Isengard. Plus "Strength of Barad-dur" and the like.
- **Their own voices:** attacks, blows taken and deaths have each hero's own voice; on the character select, choosing one plays his own sounds. **Their shots** have their own sounds (Sauron's bolt, Frodo's stones, the Uruk-hai's burning rocks; Gandalf's the bow's), and Gandalf's and Sauron's bolts show only the beam.
- **Character select:** every unlocked character shows his level in the distance, Isildur and the new heroes too. Isildur no longer loses his sword or stands in a T-pose after you played as a new hero. Sauron comes last.
- **Toughness:** Gandalf has more health and flinches less (a little less than Sauron); Lurtz a little less than Gandalf. Sauron takes blows better. Lurtz has more health as well. 
- **Moves:** everyone kicks and parries with the heroes' kick and parry, fitted to his body; they fall when they die; they stand still as they aim; the Uruk-hai's heavy attacks are his two forward swings and his rising attack is his own getting-up move. Frodo throws stones.
- **Icons:** their own ammo counters and upgrade-screen items; a posed figure on the pause screen; your own upgrade-screen pictures from an `upgpics` folder. With the ECTS HUD: Changed Gandalf's and Frodo's portraits (Frodo's from the E3 demo), and added the cave troll's health bar in Balin's Tomb as in the E3 demo.

## Tested
- **Windows build under Wine:** the new heroes in Hornburg Courtyard, Amon Hen, Fangorn and the Helm's Deep levels; Gandalf's and Sauron's beams; the character select with every hero unlocked; Frodo's upgrade names and descriptions; the keyboard loading screens of Weathertop, the Gates of Moria and Balin's Tomb; Vulkan through DXVK on a software Vulkan driver.
- The release was built from a fresh copy of the source, with the game code generated from scratch by `generate.py`. The macOS build is cross-compiled and has **not** been run on a Mac.

## Known issues
-  Visual bugs: Character shadow bug. Sometimes the character shadows can appear stretched out or stringy, especially for larger NPC's. Some visual bugs with lighting as well. 
- * Vulkan: If you get a black screen when using the Vulkan rendering option, switch back to DX11. Also make sure that the dxvk folder is in the same folder as the games executable. 
- Extra heroes: Most in game cut scenes do not currently show proper animations for the new characters yet, so any cut scene that requires a special animation will have the new character models appear mangled/unaligned. All animations seem to work okay in game/during gameplay.
- Extra Heroes: Kicking animations and their range during the Deepening level can be off. Kicking works fine against enemies, but some of the new characters have to be angled to the side to kick down the ladders. Again kicking works fine against enemies but not the ladders during this level. NPC's like Sauron, Frodo, and Lurtz can kick them just fine, but Elrond, Boromir, the Uruk and Gandalf may have some trouble and need to be angled to the side.
- Extra heroes: Sauron and Lurtz are too tall for the end-of-level upgrade screen and are cut off.
- Extra heroes: your hero takes Aragorn's place in the cutscenes; where his own character also appears (Boromir, Gandalf and Frodo in Balin's Tomb), such a scene shows him twice. Also when playing as characters that have new weapons and ranged attacks,
some of those weapons may show up on other NPC's. This doesn't happen with the mod turned off.
- Extra heroes: Currently the models used for the upgrade menu in the in game pause menu show up mangled. The models in the actual upgrade screen after a level is completed appear normal and play their animations. 
- With PlayStation prompts, on-screen text still names the Xbox buttons ("Press the A button…").
- 60 fps and unlocked are experimental: crackling or music cutting out can still happen.
- Widescreen is experimental: a few full-screen effects may not line up at the edges.


Bug reports are welcome, especially for Mac and Linux users. Please include `twotowers_log.txt`, which is next to the game (for the Extra heroes mod, its `[HEROES]` lines).

Screenshots and gameplay: 
<img width="2336" height="940" alt="71a47139-f2d2-4a8e-a85a-1cb04c062484" src="https://github.com/user-attachments/assets/106a8073-de66-4084-bbf6-cafeb3460dc9" />




https://github.com/user-attachments/assets/a91ea354-b100-43ea-9da8-bf31345b9c16



https://github.com/user-attachments/assets/796bfe14-f20b-465d-bde9-e2c76afb346f



https://github.com/user-attachments/assets/54b37a07-94b8-4e76-91b1-ead4931073d0




https://github.com/user-attachments/assets/ca862d24-921b-450e-8466-92478e4df73b



https://github.com/user-attachments/assets/9663397e-1631-4198-93cf-a58018c648fd



https://github.com/user-attachments/assets/6153922f-3cb8-4017-bf66-747b611f1825



https://github.com/user-attachments/assets/fc51cdac-edbf-4c00-b49c-f038150b6bce


<img width="4608" height="1296" alt="Untitled" src="https://github.com/user-attachments/assets/1c7279cc-2d39-4248-9096-a7143594f540" />



<img width="1345" height="1030" alt="image" src="https://github.com/user-attachments/assets/184186dd-bc56-4783-8ee3-610839aeea58" />
<img width="4608" height="1508" alt="Untitled6" src="https://github.com/user-attachments/assets/1d865abd-7658-421a-81db-88167dd4d178" />
<img width="1096" height="617" alt="TwoTowers-Recompiled-0 12 Screenshot 2026 10 10 - 00 06 48 45" src="https://github.com/user-attachments/assets/a04259e8-5b13-40c7-b51c-f124ac166873" />
<img width="4608" height="1296" alt="Untitled3" src="https://github.com/user-attachments/assets/84582aff-17c4-48a7-980d-e6517e253a2d" />
