# The Two Towers Recompiled v1.3

A native PC port of *The Lord of the Rings: The Two Towers* (Xbox, USA, 2002), made by static recompilation. It needs your own copy of the game, either the disc image (ISO) or an extracted folder. No game data is included.

> ⚠ **Work in progress.** The game is playable, but not everything works perfectly yet. It is recommended to play the game at **30 fps**, the frame rate the game was made for. If the game crashes or freezes with unlocked frame rates, restart it: the same spot usually works on the next try. Most tests have no crashes with higher frame rates, and none at 30 fpos

## Downloads

| File | For |
|---|---|
| `TwoTowers-Recompiled-v1.3-win64.zip` | Windows 7 SP1, 8.1, 10, 11 (64-bit) |
| `TwoTowers-Recompiled-v1.3-linux-x86_64.tar.gz` | 64-bit Linux, glibc 2.27 or newer (Ubuntu 18.04+, Debian 10+, Fedora 28+, Steam Deck desktop mode, …) |
| `TwoTowers-Recompiled-v1.3-source.zip` | Source code (LGPL-2.1) |
|  E3hudNEWCHARACTERSICONS.zip | Extra icons for the new characters when using the ECTS 2002 HUD mod  

Updating from 1.2: unpack over your old folder, or into a new one and copy your `game_files` folder (or point the launcher at your extracted folder again). Your saves and settings carry over. If you played the Extra heroes mod in 1.2, its `heroes` folder is rebuilt automatically; the heroes' experience and upgrades are kept.

## New in 1.3: Extra heroes mod fixes/changes  

> ⚠ Still **highly experimental** and **off by default**. Expect the odd visual glitch in cutscenes and during gameplay. Your saves are not changed by it. Toggle it in the launcher's settings (**Mod: Extra heroes**) or with `[Mods] extra_heroes=1`.

1.2 brought Boromir, Gandalf, Frodo and Lurtz to the character select, but they were stretched over Aragorn's body and fought with his sword and bow. In 1.3 each one is himself.

You will need to compensate for the new attack animations. As of now Frodo and Boromir's heavy attacks don't line up perfectly, but with practice and timing around the new animations you can easily complete any level with any of the new characters. Gandalf and Lurtz are the most playable/fun to use so far.

With the mod "on", you can unlock each character one at a time by finishing the Tower of Orthanc as Isildur on that save: Completing the level a second time unlocks Boromir, the
3rd time Gandalf, the 4th time Frodo, and the 5th time Lurtz. A locked hero shows as a faint shadow until they have been unlocked. With "unlocked for testing" enabled, all characters are enabled by default.

### Their own bodies and moves
- They play on their **own skeletons**, with their own walk, run, block, hit reactions and death animations. Frodo and Lurtz are no longer distorted outside of cutscenes.
- Walks and runs are paced to each hero's stride, so their feet keep to the ground.
- **Boromir and Frodo** fight with the heroes' light and heavy attacks, fitted to their bodies. **Gandalf and Lurtz** fight with their own attacks, timed so the blow lands where the swing does.
- **Heavy attacks now hit.** The guests' blades swept beside where the game looked for a hit, so heavy attacks missed. Measured in Hornburg Courtyard, they now land about as often as Aragorn's.

### Their own weapons
- **Boromir:** his sword, and his shield on his other arm.
- **Gandalf:** his staff, always in hand. His ranged attack is a **beam of light from the staff's head**, never arrows.
- **Frodo:** Sting, its blade glowing a pale blue.
- **Lurtz:** his Uruk-hai sword and his own bow.

### Their own pictures
- Each has his own idle pose on the character select, his own pose when chosen and his own move when confirmed.
- His own figure on the pause screen's upgrades and on the end-of-level model.
- His own HUD portrait, upgrade-screen and results-screen pictures.

### Unlocking, one at a time
- Each finish of the Tower of Orthanc **as Isildur** on a save unlocks the next hero: the 2nd finish unlocks Boromir, the 3rd Gandalf, the 4th Frodo and the 5th Lurtz. Locked heroes show as a faint shadow and are skipped.
- **"On, unlocked for testing"** (`extra_heroes=2`) still shows all four on any save, without changing it.

### Fixes
- The new heroes sometimes missing from the character select after returning to the menu.
- ECTS HUD mod: the HUD is drawn only in play.

## Tested
- **Windows:** all 14 levels load and reach gameplay without the mod, rotating Aragorn, Legolas and Gimli at 30, 60 and unlocked fps, with no crash. (The prologue's long opening movie outlasted the test run on the slow test machine, as 1.2 does there too.)
- **Extra heroes on Windows:** all four in the Plains of Rohan and Hornburg Courtyard: moves, weapons, Gandalf's beam, and heavy-attack hits measured against Aragorn's.
- **Linux:** the game built from the published source; a level with Boromir builds and plays its opening scene with no crash.
- The release was built from a fresh copy of the source, with the game code generated from scratch by `generate.py`.

## Known issues
- Character shadow bug. Warped shadows can sometimes appear for the main character. 
- Extra heroes: they have no voices of their own. Frodo's Sting glow is a tint of the blade, not a light on its surroundings. Breaking shields with heavy attacks has not been measured as closely as plain hits; reports are welcome.
- Rarely, the game froze during the movie before Amon Hen in 1.2 testing; the next try worked.
- With PlayStation prompts, on-screen text still names the Xbox buttons ("Press the A button…"). Only the pictures and button icons change. Fixes for button prompts planned for 1.4.
- 60 fps and unlocked are experimental: crackling or music cutting out can still happen.
- Widescreen is experimental: a few full-screen effects may not line up at the edges.

Bug reports are welcome. Please include `twotowers_log.txt`, which is next to the game (for the Extra heroes mod, its `[HEROES]` lines).

Screenshots and gameplay: 
<img width="2336" height="940" alt="71a47139-f2d2-4a8e-a85a-1cb04c062484" src="https://github.com/user-attachments/assets/106a8073-de66-4084-bbf6-cafeb3460dc9" />

<img width="1585" height="999" alt="imag4" src="https://github.com/user-attachments/assets/f67df73d-16c4-42a3-9196-b6ddb3aa58c5" />


https://github.com/user-attachments/assets/4ec5e31d-ab8e-4fa5-9df8-f4074c29a3d0

<img width="1030" height="770" alt="imag44e" src="https://github.com/user-attachments/assets/c580be17-135b-453e-bf71-13eb08a1455b" />
<img width="1345" height="1030" alt="image" src="https://github.com/user-attachments/assets/184186dd-bc56-4783-8ee3-610839aeea58" />
<img width="4608" height="1508" alt="Untitled6" src="https://github.com/user-attachments/assets/1d865abd-7658-421a-81db-88167dd4d178" />


