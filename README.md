# The Two Towers Recompiled v1.5

A native PC port of *The Lord of the Rings: The Two Towers* (Xbox, USA, 2002), made by static recompilation. It needs your own copy of the game, either the disc image (ISO) or an extracted folder. No game data is included.

> ⚠ **Work in progress.** The game is playable, but not everything works perfectly yet. Play at **30 fps**, the frame rate the game was made for. If the game crashes or freezes, restart it: the same spot usually works on the next try.

## Downloads

| File | For |
|---|---|
| `TwoTowers-Recompiled-v1.5-win64.zip` | Windows 7 SP1, 8.1, 10, 11 (64-bit) |
| `TwoTowers-Recompiled-v1.5-linux-x86_64.tar.gz` | 64-bit Linux, glibc 2.27 or newer (Ubuntu 18.04+, Debian 10+, Fedora 28+, Steam Deck desktop mode, …) |
| `TwoTowers-Recompiled-v1.5-source.zip` | Source code (LGPL-2.1) |

Updating from 1.3 or 1.2: unpack over your old folder, or into a new one and copy your `game_files` folder (or point the launcher at your extracted folder again). Your saves and settings carry over. The Extra heroes mod's `heroes` folder is rebuilt automatically; the heroes' experience, upgrades and unlocks are kept.

## What's new in 1.5

A fix for the **Extra heroes** mod (still highly experimental and off by default; turn it on in the launcher's settings, **Mod: Extra heroes**).

### Boromir's and Frodo's attacks go where they face
Boromir and Frodo fight with Aragorn's attacks, fitted to their bodies. Aragorn's sword stance turns his body about 40 degrees, and the game makes up for that turn on Aragorn but not on them. So their heavy attacks and kicks went about 40 degrees to one side of the enemy they faced, and you had to stand at an angle to land them. Now their blows go where they face.

Measured in Hornburg Courtyard with heavy attacks pressed throughout, as the share of the sword's hit checks that land:

| Hero | 1.3 | 1.5 |
|---|---|---|
| Boromir | about 10% | about 18% |
| Frodo | about 1% | about 25% |
| Aragorn (for comparison) | about 14% | about 14% |

Also:
- Their shoulders and hips now sit where Aragorn's do during these moves, so their arms and blade follow his swing more closely.
- Boromir's skeleton gets the aiming data every other hero has.
- Gandalf and Lurtz fight with their own moves and are unchanged.

## Everything from 1.3
- The Extra heroes play on their own skeletons, with their own walk, run, block, hit reactions and death.
- Their own weapons, shown everywhere they appear as the hero: Boromir's sword and shield, Gandalf's staff and his beam of light, Frodo's glowing Sting, Lurtz's Uruk-hai sword and bow.
- Their own pictures: character select poses, upgrade figure, end-of-level model, HUD portrait (with the ECTS HUD mod, Boromir, Frodo and Lurtz have their own icons in the `e3hud` folder), upgrade and results pictures.
- Unlocked one at a time by finishing the Tower of Orthanc as Isildur (2nd finish Boromir, 3rd Gandalf, 4th Frodo, 5th Lurtz); "On, unlocked for testing" shows all four.

## Tested
- **Windows:** Boromir and Frodo in Hornburg Courtyard, their hits measured against Aragorn's (table above). Lurtz and Gandalf in the same test.
- **Linux:** the game built from the published source. A level with Frodo builds and plays its opening scene with no crash.
- The release was built from a fresh copy of the source, with the game code generated from scratch by `generate.py`.

## Known issues
- Extra heroes: they have no voices of their own. Sting's glow is a tint of the blade, not a light. Gandalf's staff lands fewer heavy blows than a sword does.
- Rarely, the game froze during the movie before Amon Hen in 1.2 testing; the next try worked.
- With PlayStation prompts, on-screen text still names the Xbox buttons ("Press the A button…"). Only the pictures and button icons change.
- 60 fps and unlocked are experimental: crackling or music cutting out can still happen.
- Widescreen is experimental: a few full-screen effects may not line up at the edges.

Bug reports are welcome. Please include `twotowers_log.txt`, which is next to the game (for the Extra heroes mod, its `[HEROES]` lines).



Screenshots and gameplay: 
<img width="2336" height="940" alt="71a47139-f2d2-4a8e-a85a-1cb04c062484" src="https://github.com/user-attachments/assets/106a8073-de66-4084-bbf6-cafeb3460dc9" />

<img width="1585" height="999" alt="imag4" src="https://github.com/user-attachments/assets/f67df73d-16c4-42a3-9196-b6ddb3aa58c5" />


https://github.com/user-attachments/assets/4ec5e31d-ab8e-4fa5-9df8-f4074c29a3d0



https://github.com/user-attachments/assets/796bfe14-f20b-465d-bde9-e2c76afb346f


https://github.com/user-attachments/assets/ca862d24-921b-450e-8466-92478e4df73b




<img width="1096" height="617" alt="TwoTowers-Recompiled-0 12 Screenshot 2026 10 10 - 10 18 06 90" src="https://github.com/user-attachments/assets/6954ef07-77a4-4c45-b5f6-2e2d5da4d209" />
<img width="1096" height="617" alt="TwoTowers-Recompiled-0 12 Screenshot 2026 10 10 - 10 18 19 39" src="https://github.com/user-attachments/assets/1179ff50-b7e2-45ce-8055-497b7c6b1241" />
<img width="1096" height="617" alt="TwoTowers-Recompiled-0 12 Screenshot 2026 10 10 - 10 18 14 48" src="https://github.com/user-attachments/assets/eb2c1050-7856-4a7d-a8bc-e7526d2673c5" />
<img width="1096" height="617" alt="TwoTowers-Recompiled-0 12 Screenshot 2026 10 10 - 10 25 38 67" src="https://github.com/user-attachments/assets/09f34e4d-3c92-478c-ad88-c273d9071c56" />

<img width="1345" height="1030" alt="image" src="https://github.com/user-attachments/assets/184186dd-bc56-4783-8ee3-610839aeea58" />
<img width="4608" height="1508" alt="Untitled6" src="https://github.com/user-attachments/assets/1d865abd-7658-421a-81db-88167dd4d178" />
<img width="1096" height="617" alt="TwoTowers-Recompiled-0 12 Screenshot 2026 10 10 - 00 06 48 45" src="https://github.com/user-attachments/assets/a04259e8-5b13-40c7-b51c-f124ac166873" />
