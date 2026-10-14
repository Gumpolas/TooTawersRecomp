# The Two Towers Recompiled v1.8

A native PC port of *The Lord of the Rings: The Two Towers* (Xbox, USA, 2002), made by static recompilation. It needs your own copy of the game, either the disc image (ISO) or an extracted folder. No game data is included.

> ⚠ **Work in progress.** The game is fully playable, but not everything works perfectly yet. It is recommended to play at no higher than **60 fps**, as unlocked frame rates are experimental. See known issues.

## Downloads

| File | For |
|---|---|
| `TwoTowers-Recompiled-v1.8-win64.zip` | Windows 7 SP1, 8.1, 10, 11 (64-bit) |
| `TwoTowers-Recompiled-v1.8-linux-x86_64.tar.gz` | 64-bit Linux, glibc 2.27 or newer (Ubuntu 18.04+, Debian 10+, Fedora 28+, Steam Deck desktop mode, …) |
| `TwoTowers-Recompiled-v1.8-macos-x86_64.tar.gz` | macOS 10.15 or newer, Intel or Apple silicon (through Rosetta 2). **New and not yet tested on a Mac**: see below |
| `TwoTowers-Recompiled-v1.8-source.zip` | Source code (LGPL-2.1) |

Updating from 1.7 or an earlier 1.8: unpack over your old folder, or into a new one and copy your `game_files` folder (or point the launcher at your extracted folder again). Your saves and settings carry over. The Extra heroes mod's `heroes` folder is rebuilt automatically; the heroes' experience, upgrades and unlocks are kept.

## What's new in 1.8

Everything since 1.7, in one release.

### The mouse in the menus
- **A mouse pointer in every menu:** the title screen, main menu, options, load and save screens, mission select, character select, upgrade and results screens, and the pause screen. Point to choose, click to press, right click to go back. Leaves (Back, Continue, Replay) light up under the pointer, the mission select picks the place under the pointer at once, the character select turns with a click left or right, sliders can be clicked and dragged, and the mouse wheel scrolls. Picture galleries step left and right with a click.
- **A left click skips the intro films** (the logos before the title screen).
- **Aiming with the mouse:** hold the ranged button and move the mouse, with invert, sensitivity and pointer-size settings.
- On the mission and character select, a right mouse button bound to Help or Options does that action.
- Turn the pointer off with **Mouse pointer in menus** (F10, launcher, Controls page).

### Options and controls
- **Options opens on a first page:** General Options (the game's own page), **Display Options** (every graphics setting, in the game's own look) and **Controls** (the mouse, Sauron's sprint and **every key binding**: choose one and press the key or mouse button; Esc cancels, Delete clears; left / right changes the controller button). From the main menu, the mission select and the pause screen. **Tab** (the Back key) backs out.
- **Exit** on the main menu. The title screen says **Press Enter** when you play with the keyboard, and messages such as "Game Saved. Press the A button to continue" name your key (Enter, which also closes them).
- The "Are you finished purchasing upgrades?" Yes / No, Replay on the results screen and the upgrade screens' level leaves all work with the pointer.

### Display
- **Internal resolution up to 8x**, and **4.5x** for 2160 lines (3840 x 2160 at 16:9, a 4K screen pixel for pixel).
- With **unlocked** frame rate, the in-engine scenes and the first 5 seconds of play after one run at 60 fps: at very high frame rates the scenes' characters stopped turning their heads (the prologue's ending) and Sauron could fall through the ground as the Gates of Moria began.

### Everyone (from 1.7.1 to 1.7.8)

- **Every menu fills the screen.** The game drew its menus inside the old TV safe area, with a black margin all round. This covers the main menu, mission select, load and save screens, character select, loading screens, and the upgrade and results screens. In widescreen with "Stretch to fill" they now fill the screen edge to edge.
- **Two new options** in F10 and the launcher's settings:
  - **Menus:** "Fill the screen" (the default) or "Original", with the old margin.
  - **Movies in widescreen:** "As made" (4:3, the default) or "Fill the width", which cuts off the films' black bands so they fill a widescreen picture (a little closer).
- **Controller prompts follow your bindings.** Each button prompt shows the button of your own controller bound to that action, in play, on the menus and in the loading screens' hints, as the keyboard prompts already did.
- **Xbox One prompts:** a new Button prompts choice in F10 and the launcher. It has black buttons with coloured letters, LB / RB / LT / RT, View and Menu, and an Xbox One pad on the loading screens' controls pictures.
- **Walk key for keyboard and mouse: B.** Hold it with the move keys and any hero walks instead of running. It's rebindable in the launcher's Controls… and on the F10 Controls page. On a controller, push the stick part way.
- **Controller Start:** the log records the controller's Start presses and whether each one reached the game.

### Extra heroes (still highly experimental, off by default)
- **Sauron's sprint is back** (left stick click / Shift, rebindable): press to toggle, or hold (Controls page). He simply runs faster, easing in and out, with no skip or jump.
- New heroes' weapons no longer turn up in other characters' hands in scenes (the Plains of Rohan's archer on the wall).
- A crash on the upgrade screen after a level (pointing at a leaf) is fixed.
- A new hero who keeps falling through the ground is put back where he came into play.
- **Everyone fights with his own attacks, as in 1.6.** Lurtz's and the Uruk-hai's blades reach a little further.
- **A tougher Lurtz:** 1800 health, much harder to knock back or down, and he gets over his flinches sooner.
- **The last ranged upgrade changes the shots:**
  - Gandalf's beam turns white (White Fire).
  - Sauron's bolts alternate red and blue (Doom Fire).
  - The Uruk-hai throws plain stones until then, then burning rocks (Lava Rocks); every third one bursts like the Orthanc archers' fire pots and leaves burning ground.
  - His first two ranged upgrades are now Crag Rocks and War Rocks.
- **No more drifting in water.** The other new heroes use the heroes' moves where their own sets had a sideways step. Sauron keeps all his own moves.
- **Falling through the ground is caught.** If a new hero drops through the floor (Sauron at the Gates of Moria), he's stood back where he last stood, or where he came into play at a level's start.
- **Pause-screen figures stand upright,** weapon in hand.

Everything from 1.7 is included: see its notes, and the README for the full list.

## Tested
- Windows, Linux and macOS builds compile.
- Windows build under Wine: the pointer in the main menu, options pages (hover, click, values, sliders, wheel, Tab), load screen, mission and character select, results and upgrade screens (Replay, the Yes / No dialog, Continue), the intro film skipped by a click; Sauron's sprint button; the 4.5x resolution; heavy attacks compared with the earlier 1.8 (no change).
- Earlier in 1.8:- Windows, Linux and macOS builds compile.
- Windows build under Wine, at 16:9 with "Stretch to fill":
  - the menus fill the screen; play and the pause menu are unchanged;
  - the two new F10 options are shown and work (movies filling the width, menus with their margin);
  - the Xbox One loading-screen picture appears, and every controller button was drawn in each prompt style;
  - the shots before and after the last ranged upgrade;
  - Sauron at the Gates of Moria: walking and fighting with every upgrade, and the fall-through catch (pushed under the floor on purpose, mid-level and at the start);
  - the stick pushed part way (what Walk does) walks;
  - Start pressed on a simulated controller pauses the game.
- Not tested: a high frame rate (the test machine reaches 5–16 fps); a rebound controller button on the upgrade screen; the macOS build on a Mac.


## Known issues
-  Visual bugs: Character shadow bug. Sometimes the character shadows can appear stretched out or stringy, especially for larger NPC's. Some visual bugs with lighting as well. With the extra heros mod enabled, replaying the second level can sometimes show an odd texture covering the top of the upgrades screen after completing the level. This can be fixed temporarily by turning the mod off. 
- * Vulkan: If you get a black screen when using the Vulkan rendering option, switch back to DX11. Also make sure that the dxvk folder is in the same folder as the games executable. 
- Extra Heroes: Kicking animations and their range during the Deepening level can be off. Kicking works fine against enemies, but some of the new characters have to be angled to the side to kick down the ladders. Again kicking works fine against enemies but not the ladders during this level. NPC's like Sauron, Frodo, and Lurtz can kick them just fine, but Elrond, Boromir, the Uruk and Gandalf may have some trouble and need to be angled to the side.
- Extra heroes: Sauron and Lurtz are too tall for the end-of-level upgrade screen and are cut off.
- Extra heroes: your hero takes Aragorn's place in the cutscenes; where his own character also appears (Boromir, Gandalf and Frodo in Balin's Tomb), such a scene shows him twice. Also when playing as characters that have new weapons and ranged attacks,
some of those weapons may show up on other NPC's. This doesn't happen with the mod turned off.
- Extra heroes: Currently the models used for the upgrade menu in the in game pause menu show up mangled. The models in the actual upgrade screen after a level is completed appear normal and play their animations. 
- Unlocked fps is experimental: One Known issue with frame rates above 60 fps is that some of the NPC animations during the prologue level fail to complete during the end of the in game cutscene for that level. Example is both Sauron and an elf NPC not turning their heads properly. Another known issue is Sauron falling through the Gates of Moria level after the intro cutscene during gameplay. At 60 fps these issues don't occur, so it is advised to set the frame rate to 60 fps before beginning the prologue or when playing that mission with Sauron. Crackling audio can occur at unlocked frame rates as well.
- Widescreen is experimental: a few full-screen effects may not line up at the edges.


Bug reports are welcome, especially for Mac and Linux users. Please include `twotowers_log.txt`, which is next to the game (for the Extra heroes mod, its `[HEROES]` lines).

Screenshots and gameplay: 
<img width="1920" height="1080" alt="TwoTowers-Recompiled-0 12 Screenshot 2026 10 13 - 18 45 30 31" src="https://github.com/user-attachments/assets/40906651-76c2-4405-ab39-2425bb26baa7" />

<img width="1920" height="1080" alt="TwoTowers-Recompiled-0 12 Screenshot 2026 10 13 - 18 45 35 90" src="https://github.com/user-attachments/assets/680d7a28-bb32-499d-8461-72a77a3b0469" />

<img width="1920" height="1080" alt="TwoTowers-Recompiled-0 12 Screenshot 2026 10 13 - 18 45 38 41" src="https://github.com/user-attachments/assets/796889e4-60c0-4210-a187-94949b9f56ba" />


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
