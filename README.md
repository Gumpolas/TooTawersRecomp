# The Two Towers Recompiled v1.9

A native PC port of *The Lord of the Rings: The Two Towers* (Xbox, USA, 2002), made by static recompilation. It needs your own copy of the game, either the disc image (ISO) or an extracted folder. No game data is included.

> ⚠ **Work in progress.**  The game is fully playable, but not everything works perfectly yet. It is recommended to play at no higher than 60 fps, as unlocked frame rates are experimental. See known issues.
## Downloads

| File | For |
|---|---|
| `TwoTowers-Recompiled-v1.9-win64.zip` | Windows 7 SP1, 8.1, 10, 11 (64-bit) |
| `TwoTowers-Recompiled-v1.9-linux-x86_64.tar.gz` | 64-bit Linux, glibc 2.27 or newer |
| `TwoTowers-Recompiled-v1.9-macos-x86_64.tar.gz` | macOS 10.15 or newer, Intel or Apple silicon (through Rosetta 2); not yet tested on a Mac |
| `TwoTowers-Recompiled-v1.9-source.zip` | Source code (LGPL-2.1) |

**Ultra textures Gamecube pack:** Requires the 4k ai remastered GameCube high-resolution texture pack (`GLOS69`) downloaded separately from its author. It is not part of this release.
 https://www.moddb.com/mods/the-two-towers-textures-4k-ai-remaster/addons/the-two-towers-textures-4k-ai-remaster-575gb-version-native

Updating from 1.8.x or earlier: unpack over your old folder, or into a new one and copy your `game_files` folder. Your saves and settings carry over. If you used the Ultra textures test build, keep your `GLOS69` folder; the launcher prepares it again once.

## What's new in 1.9

### Ultra textures
- **Default Ultra Textures:** the menus, the level select and its pictures of each level, the text and the HUD (including the ECTS HUD mod's icons), upscaled 4x from the game's own textures, for more readable, less blurry text. Turn on **Ultra textures** in Graphics settings or Display Options.
- **The GameCube high-resolution Ultra texture pack (`GLOS69`)** replaces every texture. It is its author's work and is **downloaded separately**: put its `GLOS69` folder next to `twotowers_recomp.exe`. The first time you press **Play** with the option on, the launcher prepares the pack (once; a few minutes, with a progress bar; about 1 GB). If you turn the option on in the game's own menus, the launcher opens and prepares it while you play, and the new pictures appear when it finishes. On Linux and macOS, `TwoTowers.sh` / `TwoTowers.command` do this (or run `./twotowers-setup ultra`).

### Graphics
- **Internal resolution up to 12x** (7680 × 5760 at 4:3). A picture larger than the window is now averaged down to it properly. Before, high settings sharpened textures but left edges as jagged as at 1x; now they smooth edges too. At very wide aspect ratios the picture is held to the largest one a graphics card can draw (16384 wide).
- **MSAA anti-aliasing:** MSAA 2x, 4x and 8x, and MSAA 4x / 8x with FXAA, in Display Options, the launcher and the ini (`antialiasing=msaa4`, ...). It works on Direct3D 11, Vulkan and OpenGL (Linux, macOS). At very high internal resolutions it uses fewer samples rather than run out of video memory.

### Gameplay and effects
- **Up to 512 particle effects at once** (the game had 64). Beams, the Uruk-hai's fire rock and the archers' fire pots now all show together. Before, your own effects put out the archers' fire, though it still caused player damage.

### From 1.8.2 (test builds)
- In widescreen, the in-engine cutscenes stay 4:3 unless **Widescreen cutscenes** is on (Display Options, launcher). Their skies and mountain cards were made for 4:3.
- Menu buttons no longer press themselves (the title screen, hovering over Credits). Right click backs out of Options opened from the mission select. Right click on the character select unselects the hero.
- The Uruk-hai's fire rock burns. The upgrade screen's level leaves follow the pointer. **Mouse steering** turns the hero where he stands, and the move keys go by his facing. Frodo's stones hurt the Watcher.

## Tested
- Windows, Linux and macOS builds compile.
- Ultra textures on the real GLOS69 pack: the prepared set matches the hand-made one checked earlier (the same 2869 pictures, the same pairings). The launcher's `--build-ultra`, Linux's `twotowers-setup ultra`, and the game picking the pack up while running were also tested.
- Under Wine (Direct3D 11) and on Linux (OpenGL): MSAA 4x / 8x in Orthanc and its scenes (edges smoothed, effects as before); 4x and 12x internal resolution (12x at 32:9 with 8x MSAA held to 16384 wide); 512 particle effects at once, and a level replayed.
- Not tested: real graphics cards at 9x–12x or 8x MSAA (video memory and speed); MSAA through Vulkan (DXVK); the macOS build on a Mac.



## Known issues
-  Visual bugs: Character shadow bug. Sometimes the character shadows can appear stretched out or stringy, especially for larger NPC's. Some visual bugs with lighting as well. With the extra heros mod enabled, replaying the second level can sometimes show an odd texture covering the top of the upgrades screen after completing the level. This can be fixed temporarily by turning the mod off. 
- * Vulkan: If you get a black screen when using the Vulkan rendering option, switch back to DX11. Also make sure that the dxvk folder is in the same folder as the games executable. 
- Extra Heroes: Kicking animations and their range during the Deepening level can be off. Kicking works fine against enemies, but some of the new characters have to be angled to the side to kick down the ladders. Again kicking works fine against enemies but not the ladders during this level. NPC's like Sauron, Frodo, and Lurtz can kick them just fine, but Elrond, Boromir, the Uruk and Gandalf may have some trouble and need to be angled to the side.
- Extra heroes: Sauron and Lurtz are too tall for the end-of-level upgrade screen and are cut off.
- Extra heroes: your hero takes Aragorn's place in the cutscenes; where his own character also appears (Boromir, Gandalf and Frodo in Balin's Tomb), such a scene shows him twice. Also when playing as characters that have new weapons and ranged attacks,
some of those weapons may show up on other NPC's. This doesn't happen with the mod turned off.
- Extra heroes: Currently the new models used for the upgrade menu in the in game pause menu show up mangled. The models in the actual upgrade screen after a level is completed appear normal and play their animations. 
- Unlocked fps is experimental: One Known issue with frame rates above 60 fps is that some of the NPC animations during the prologue level fail to complete during the end of the in game cutscene for that level. Example is both Sauron and an elf NPC not turning their heads properly. Another known issue is Sauron falling through the Gates of Moria level after the intro cutscene during gameplay. At 60 fps these issues don't occur, so it is advised to set the frame rate to 60 fps before beginning the prologue or when playing that mission with Sauron. As of 1.8 to fix this issue, in game cutscenes are now locked at 60 fps and for 5 seconds after they end. Crackling audio can occur at unlocked frame rates as well.
- Widescreen is experimental: a few full-screen effects may not line up at the edges.
- -Sauron's Sprint option in the settings does not currently work in 1.8. Leaving it on does nothing. Due to his default walking speed, he has trouble completing some of the timed Rohan levels as he can walks slower than the other characters. 
- Some grass, clouds, and certain environmental textures included with the high resolution pack (Glos69) can appear warped or out of place. Examples are level 2,6, and 10. Most of these textures were upscaled with AI and converted from the gamecube version, so that could explain why that occurs as applying alpha/transparency fixes didn't seem to work.
- Some of the included high resolution menu textures in the level select menu can appear or some odd to some as well. The textures can be disabled at will if needed.
- The Uruk currently can only shoot arrows instead of rocks on level 3 due to the way the watcher boss battle works. His ranged attack still fires rocks in all other levels. 
- Some fire pots thrown by archers may disappear for a few seconds when multiple ranged beam shots are fired from either Gandalf or Sauron. This is due to the way the particle limit works. Normal ranged attacks or the Uruks fire rocks/Frodo's rocks do not cause this issue. 


Bug reports are welcome, especially for Mac and Linux users. Please include `twotowers_log.txt`, which is next to the game (for the Extra heroes mod, its `[HEROES]` lines).

Screenshots and gameplay: 
<img width="1920" height="1080" alt="TwoTowers-Recompiled-0 12 Screenshot 2026 10 13 - 18 45 30 31" src="https://github.com/user-attachments/assets/40906651-76c2-4405-ab39-2425bb26baa7" />

<img width="1920" height="1080" alt="TwoTowers-Recompiled-0 12 Screenshot 2026 10 13 - 18 45 35 90" src="https://github.com/user-attachments/assets/680d7a28-bb32-499d-8461-72a77a3b0469" />

<img width="1920" height="1080" alt="TwoTowers-Recompiled-0 12 Screenshot 2026 10 13 - 18 45 38 41" src="https://github.com/user-attachments/assets/796889e4-60c0-4210-a187-94949b9f56ba" />

<img width="1920" height="1080" alt="TwoTowers-Recompiled-0 12 Screenshot 2026 10 13 - 18 45 43 21" src="https://github.com/user-attachments/assets/24e59e5d-3b48-4993-a86e-ee84d267557c" />


<img width="2336" height="940" alt="71a47139-f2d2-4a8e-a85a-1cb04c062484" src="https://github.com/user-attachments/assets/106a8073-de66-4084-bbf6-cafeb3460dc9" />

https://github.com/user-attachments/assets/fc51cdac-edbf-4c00-b49c-f038150b6bce


https://github.com/user-attachments/assets/a91ea354-b100-43ea-9da8-bf31345b9c16



https://github.com/user-attachments/assets/796bfe14-f20b-465d-bde9-e2c76afb346f



https://github.com/user-attachments/assets/54b37a07-94b8-4e76-91b1-ead4931073d0




https://github.com/user-attachments/assets/ca862d24-921b-450e-8466-92478e4df73b



https://github.com/user-attachments/assets/9663397e-1631-4198-93cf-a58018c648fd



https://github.com/user-attachments/assets/6153922f-3cb8-4017-bf66-747b611f1825





<img width="1920" height="1080" alt="Desktop Screenshot 2026 10 12 - 16 43 15 16" src="https://github.com/user-attachments/assets/ce849440-a354-4662-9651-11d7297fa8a8" />

  
<img width="1920" height="1080" alt="Desktop Screenshot 2026 10 12 - 20 57 50 13" src="https://github.com/user-attachments/assets/c0373910-f0d7-475f-9e90-7c2091114b44" />
<img width="1920" height="1080" alt="Desktop Screenshot 2026 10 12 - 20 29 51 74" src="https://github.com/user-attachments/assets/920917e7-0052-48ce-88ce-4a777b2c5394" />

<img width="1920" height="1080" alt="Desktop Screenshot 2026 10 12 - 20 47 32 98" src="https://github.com/user-attachments/assets/f88f7baa-d58a-4a95-8a15-2113c49df079" />

<img width="4608" height="1508" alt="Untitled6" src="https://github.com/user-attachments/assets/1d865abd-7658-421a-81db-88167dd4d178" />
<img width="1096" height="617" alt="TwoTowers-Recompiled-0 12 Screenshot 2026 10 10 - 00 06 48 45" src="https://github.com/user-attachments/assets/a04259e8-5b13-40c7-b51c-f124ac166873" />
