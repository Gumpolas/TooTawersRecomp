# The Two Towers Recompiled v1.7

A native PC port of *The Lord of the Rings: The Two Towers* (Xbox, USA, 2002), made by static recompilation. It needs your own copy of the game, either the disc image (ISO) or an extracted folder. No game data is included.

> ⚠ **Work in progress.** The game is playable, but not everything works perfectly yet. Play at **30 fps**, the frame rate the game was made for. If the game crashes or freezes, restart it: the same spot usually works on the next try.

## Downloads

| File | For |
|---|---|
| `TwoTowers-Recompiled-v1.7-win64.zip` | Windows 7 SP1, 8.1, 10, 11 (64-bit) |
| `TwoTowers-Recompiled-v1.7-linux-x86_64.tar.gz` | 64-bit Linux, glibc 2.27 or newer (Ubuntu 18.04+, Debian 10+, Fedora 28+, Steam Deck desktop mode, …) |
| `TwoTowers-Recompiled-v1.7-macos-x86_64.tar.gz` | macOS 10.15 or newer, Intel or Apple silicon (through Rosetta 2). **New and not yet tested on a Mac**: see below |
| `TwoTowers-Recompiled-v1.7-source.zip` | Source code (LGPL-2.1) |

Updating from 1.6 or earlier: unpack over your old folder, or into a new one and copy your `game_files` folder (or point the launcher at your extracted folder again). Your saves and settings carry over. The Extra heroes mod's `heroes` folder is rebuilt automatically; the heroes' experience, upgrades and unlocks are kept.

## What's new in 1.7

This release brings together everything from 1.7

### Rendering and button prompts. 
- **Menus fill the screen.** The game drew its menus, loading screens and upgrade and results screens inside the old TV "safe area", a black margin all round. They're now shown large enough for the art to reach the screen's edges (with "Stretch to fill", edge to edge in widescreen). Play and movies are unchanged. `RECOMP_MENU_FILL=0` puts the margin back.
- **Walk key for keyboard and mouse: B.** Hold it with the move keys and your hero walks instead of running, as a light push on a controller's stick does. Rebindable in the launcher's **Controls…** and the F10 **Controls** page (`kb_walk` in the settings file). On a controller, push the stick part way.
- **Controller Start:** the log (`twotowers_log.txt`) records the controller's first Start presses and whether each one reached the game. This is to help track down reports of Start not working on some controllers.

### Extra heroes (still highly experimental, off by default)
- **In game cutscene improvements.** The new heroes play the heroes' own moves in the in-game cutscenes, fitted to their own bodies, and stay whole: Fangorn's opening scene (where they were torn into stretched slabs), Balin's Tomb's opening scene and the Gates of Moria's scenes (where they twisted and turned).
- **Lurtz's and the Uruk-hai's blades reach a little further.
- **Tougher Gandalf and Lurtz.** Both take less damage. Lurtz has more health (1800), is much harder to knock back or down, and gets over his flinches sooner.
- **The last ranged upgrade changes the shots:**
  - **Gandalf — White Fire:** his beam turns white.
  - **Sauron — Doom Fire:** his bolts are red and blue in turn; before this upgrade they stay red.
  - **The Uruk-hai — Lava Rocks:** plain stones until this upgrade, then burning rocks. Every third one bursts as the Orthanc archers' fire pots do and leaves fire on the ground that hurts. His first two ranged upgrades are renamed **Crag Rocks** and **War Rocks**.
- **No more drifting in water.** Several movement animations a new hero's own set lacked had been filled with a sideways step. In water (Gates of Moria, Gap of Rohan) and elsewhere he went sideways or backwards. Those are now the heroes' moves, fitted to his body. Sauron keeps all his own moves.
- **Pause-screen figures stand upright,** weapon in hand at their side, instead of twisted or mid-stride (Lurtz looked one-legged).

Everything from 1.6 is included: see its notes, and the README for the full list.

## Tested
- Windows, Linux and macOS builds compile.
- Windows build under Wine:
  - the cutscenes as the new heroes (Fangorn, Balin's Tomb, the Gates of Moria);
  - the shots before and after the last ranged upgrade in Hornburg Courtyard;
  - the pause-screen figures, and the menus at 16:9 with stretch;
  - Sauron at the Gates of Moria: walking and fighting with every upgrade, and the fall-through catch (pushed under the ground on purpose, he was stood back on the path);
  - the stick pushed part way (what Walk does) moving at about a quarter of running speed;
  - Start pressed on a (simulated) controller pausing the game and confirming in the menus.
- Not tested: a high frame rate (the test machine reaches 5–16 fps); the launcher's and F10's new Walk row on Windows; the macOS build on a Mac.

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
