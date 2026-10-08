# TooTawersRecomp
A native Windows PC port of The Lord of the Rings: The Two Towers (Xbox, USA, 2002), made by static recompilation. It needs your own copy of the game, either the disc image (ISO) or an extracted folder. No game data is included.

# The Two Towers Recompiled v1.2

A native PC port of *The Lord of the Rings: The Two Towers* (Xbox, USA, 2002), made by static recompilation. It needs your own copy of the game, either the disc image (ISO) or an extracted folder. No game data is included.

> ⚠ **Work in progress.** The game is fully playable, but not everything works perfectly yet. It is recommended to play at **30 fps**, the frame rate the game was made for. If the game crashes or freezes, restart it: the same spot usually works on the next try.

## Downloads

| File | For |
|---|---|
| `TwoTowers-Recompiled-v1.2-win64.zip` | Windows 7 SP1, 8.1, 10, 11 (64-bit) |
| `TwoTowers-Recompiled-v1.2-linux-x86_64.tar.gz` | 64-bit Linux, glibc 2.27 or newer (Ubuntu 18.04+, Debian 10+, Fedora 28+, Steam Deck desktop mode, …) |
| `TwoTowers-Recompiled-v1.2-source.zip` | Source code (LGPL-2.1) |

## New in 1.2

### Linux build
- Native 64-bit Linux version. Graphics use OpenGL 3.3, and sound and controllers go through SDL2 (Xbox, PlayStation 3/4/5, Switch Pro and most other pads, with rumble).
- Run `TwoTowers.sh`. On first start it runs `twotowers-setup`, which checks and extracts your disc image (or uses an extracted folder), lets you change a few settings and adds a menu entry. If SDL2 is missing, it tells you how to install it.

### Windows 7 and N editions
- The game now starts on Windows 7 SP1 and 8.1, and on the "N" editions of any Windows. It no longer needs the Universal C Runtime or a Visual C++ redistributable.
- XAudio2, the shader compiler and Media Foundation are loaded only if present. Without them the game falls back to other sound output, the software renderer or no movie audio, instead of refusing to start.
- A graphics card or Windows 7 setup without Direct3D 11 feature level 11_0 now gets the software renderer and a line in the log, instead of a broken picture.

### PlayStation controller support
- **Button prompts:** with a PlayStation pad as player 1, the game's button icons (the upgrade screen's combo lists, "Help", …) show cross, circle, square, triangle and R2 instead of A, B, X, Y and the right trigger. Choose Automatic, Xbox or PlayStation under "Button prompts" in the F10 menu, or set `[Input] prompts=` in the settings file.
- **Loading-screen controller diagrams:** with PlayStation prompts, the control diagrams on the early loading screens show a **DualShock 3, DualShock 4 or DualSense**, whichever pad is plugged in. The game's own lines and labels still point at the right buttons. Use `[Input] ps_pad=auto|ps3|ps4|ps5` to pick one.

### Fixes
- **Controls freezing with the last input held** (Windows), for example Gimli aiming at the breach or Aragorn walking into a wall. The real cause was the thread-local storage in the MinGW Windows builds, which every thread shared by mistake. Pad input also now runs on its own thread, so a slow controller driver (Bluetooth, Steam Input, virtual pads) can't stall it.
- **Crash in Helm's Deep at 60 fps / unlocked**: a race in the game's sound engine.
- **Crash entering Balin's Tomb on Linux.**
- **Timer overflow:** the game would have frozen on its first frame if the PC had been on for about 10.7 days without a restart.

### ECTS 2002 HUD mod
- **Westfold:** the villager counter is now the demo's two rows of villager faces, with a red cross over each one lost. The left villager's mouth is no longer cut off.
- **Westfold:** the villager faces no longer show on the results and upgrade screens after the level.
- **Helm's Deep wall:** the progress-bar picture is centred on the bar.

### Extra heroes mod (new, ⚠ highly experimental, off by default)
> This is an early preview and is **partly broken**: expect visual glitches, missing moves and possibly crashes. Leave it off for a normal game. Your saves are not changed by it.

- Adds **Boromir, Gandalf, Frodo and Lurtz** to the character select (eight heroes), made from the game's own models. Turn it on in the launcher's settings (**Mod: Extra heroes**) or with `[Mods] extra_heroes=1`.
- They unlock once a save has finished Hornburg Courtyard and the Tower of Orthanc. **"On, unlocked for testing"** (`extra_heroes=2`) shows them on any save without changing it.
- Each takes Isildur's place in the level, with his own experience and upgrades, kept in the `heroes` folder. They move with Aragorn's moves, sword and bow.
- Known problems: Frodo and Lurtz are visibly distorted when they move. Gandalf has no staff or magic yet. The upgrade screen and HUD still show Isildur's picture. After leaving a level, the new heroes are sometimes missing from the character select until a save is loaded again. Proper moves, weapons and pictures for each are planned for 1.3.

## Tested
- **Windows:** all 14 levels load and reach gameplay, rotating Aragorn, Legolas and Gimli at 30, 60 and unlocked fps, with no crash. A run in Windows 7 compatibility mode also started and played normally.
- **Linux:** all 14 levels load and reach gameplay. This testing found the Balin's Tomb crash, which is now fixed.

## Known issues
- Rarely (once in about 30 test runs, on a heavily overloaded two-core machine), the game froze during the movie before Amon Hen. The next try worked.
- With PlayStation prompts, on-screen text still names the Xbox buttons ("Press the A button…"). Only the pictures and button icons change.
- 60 fps and unlocked are experimental: crackling or music cutting out can still happen.
- Widescreen is experimental: a few full-screen effects may not line up at the edges.

Bug reports are welcome. Please include `twotowers_log.txt`, which is next to the game.

Sreenshots and gameplay: <img width="1096" height="617" alt="TwoTowers-Recompiled-0 12 Screenshot 2026 10 07 - 15 18 17 87" src="https://github.com/user-attachments/assets/fa06ebdc-558e-4124-b346-4480554b497c" />
<img width="1096" height="617" alt="TwoTowers-Recompiled-0 12 Screenshot 2026 10 07 - 16 14 33 03" src="https://github.com/user-attachments/assets/e5445391-c4bc-4955-a41f-6bd0fea4f21f" />




<img width="2336" height="940" alt="71a47139-f2d2-4a8e-a85a-1cb04c062484" src="https://github.com/user-attachments/assets/6ba425cd-f7bc-4beb-8206-780d7af45ec6" />

<img width="1585" height="999" alt="imag4" src="https://github.com/user-attachments/assets/b7c9f2bc-5707-4218-b0bf-42313ddfac7b" />


<img width="4608" height="1296" alt="Untitled6" src="https://github.com/user-attachments/assets/18cf9c22-c34d-450c-9c88-d679c51cd2fd" />

<img width="1030" height="770" alt="imag44e" src="https://github.com/user-attachments/assets/08fc97f4-3607-4ab9-81a2-e842e8182bba" />

<img width="1345" height="1030" alt="image" src="https://github.com/user-attachments/assets/ecb2e886-0cce-4fbd-9d85-1e0f8df95e18" />

<img width="4608" height="1508" alt="Untitled6" src="https://github.com/user-attachments/assets/ab48d8dc-9225-4cd3-bb01-b58cc8639e14" />

<img width="1096" height="617" alt="tEST Screenshot 2026 10 08 - 15 15 06 05" src="https://github.com/user-attachments/assets/357ae1e5-dcc5-4416-96d3-31db095ed6a4" />

