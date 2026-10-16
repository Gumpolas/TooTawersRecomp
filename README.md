# The Two Towers Recompiled v1.9.2

A native PC port of *The Lord of the Rings: The Two Towers* (Xbox, USA, 2002), made by static recompilation. It needs your own copy of the game, either the disc image (ISO) or an extracted folder. No game data is included.

> ⚠ **Work in progress.** The game is playable, but not everything works perfectly yet. If the game crashes or freezes, restart it: the same spot usually works on the next try.

## Downloads

| File | For |
|---|---|
| `TwoTowers-Recompiled-v1.9.2-win64.zip` | Windows 7 SP1, 8.1, 10, 11 (64-bit) |
| `TwoTowers-Recompiled-v1.9.2-linux-x86_64.tar.gz` | 64-bit Linux, glibc 2.27 or newer |
| `TwoTowers-Recompiled-v1.9.2-macos-x86_64.tar.gz` | macOS 10.15 or newer, Intel or Apple silicon (through Rosetta 2); not yet tested on a Mac |
| `TwoTowers-Recompiled-v1.9.2-source.zip` | Source code |

**Ultra textures, GameCube pack:** download the GameCube high-resolution texture pack (`GLOS69`) separately from its author. It is not part of this release.

Updating from 1.9.1 or earlier: unpack over your old folder, or into a new one and copy your `game_files` folder. Your saves and settings carry over. Copy the new `textures_ultra\pairs.txt` too (it comes with the download); a prepared `GLOS69` pack is not prepared again.

## What the port adds

- **Runs natively** on Windows, Linux and macOS, from your own disc.
- **Languages** *(new)*: 33 languages besides English for every text in the game, subtitles included, plus the launcher and the F10 menu.
- **Resolution and widescreen**: internal resolution 1x–12x, 4:3 to 32:9 with the HUD at the screen's edges, widescreen cutscenes, menus that fill the screen.
- **Picture**: Direct3D 11, Vulkan (DXVK) or OpenGL; MSAA and FXAA, anisotropic filtering, brightness, sharpening and colour.
- **Ultra textures**: sharper menus, text and HUD, and every texture with the GameCube pack.
- **Frame rate**: 30 fps as on the Xbox, or 60 / unlocked (experimental).
- **Keyboard and mouse**: every action rebindable, a Walk key, a mouse pointer in every menu, mouse aiming and mouse steering.
- **Controllers**: Xbox, PlayStation, Switch Pro and others with rumble; prompts that match what you play with.
- **Menus**: Display and Controls options pages in game, the F10 settings panel, an Exit item.
- **Mods**: the ECTS 2002 demo's HUD, orange health pickups, and seven extra playable heroes.

## What's new in 1.9.2

### Languages
- Choose one in the launcher's **Language** list (on Linux and macOS: `./twotowers-setup`, then **l**). English, French, German, Spanish (Spain and Latin America), Italian, Portuguese (Brazil and Portugal), Dutch, Swedish, Danish, Norwegian, Finnish, Polish, Czech, Slovak, Hungarian, Romanian, Croatian, Turkish, Greek, Bulgarian, Russian, Ukrainian, Vietnamese, Indonesian, Japanese, Korean, Chinese (Simplified and Traditional), Thai, Hindi, Hebrew and Arabic.
- Everything the game writes is translated: menus, hints, upgrades, the results and pause screens, credits, and the subtitles of the scenes and the bonus interviews. So are the port's own words: Options pages, key names, button hints, the extra heroes' names and upgrades, the launcher and the F10 panel.
- Letters the game's fonts lack are added to them at each font's size and weight, and to the Ultra textures' upscaled fonts too, so text stays sharp in every language. Hebrew and Arabic read right to left; Chinese, Japanese and Thai lines break between words.
- The `lang` folder holds only the translations. The game's files are copied with the language's text into `lang_cache` on first use (a few seconds); your game files are not changed. Spoken dialogue stays English.

### Fixes
- **Ultra textures were prepared again and again** when you pressed Play. Now the pack is prepared once, and an already prepared pack is recognised.
- **Legolas's mission results picture** showed Aragorn with Ultra textures and the GameCube pack (the pack has no picture of him, and its second picture of Aragorn had been paired with it). It is his own now.
- **Mouse steering**: hold Walk with W and the hero walks while steering. 

## Tested
- Under Wine (Direct3D 11 and software): the title screen and menus in French, Japanese and Arabic; the largest level loads in Japanese and Chinese; the upscaled fonts of Japanese, Polish and Russian (Ultra textures); the results screen as Legolas with Ultra textures; the launcher in Japanese (Wine has no Japanese font, so its words showed as boxes there; Windows has the fonts).
- Linux (OpenGL): the title in Russian with the upscaled font. Windows, Linux and macOS builds compile.
- Not tested by hand: every language through a whole level; subtitles on screen (turn them on in Options); mouse steering's feel; the macOS build on a Mac.



Known issues

    Visual bugs: Character shadow bug. Sometimes the character shadows can appear stretched out or stringy, especially for larger NPC's. Some visual bugs with lighting as well. With the extra heros mod enabled, replaying the second level can sometimes show an odd texture covering the top of the upgrades screen after completing the level. This can be fixed temporarily by turning the mod off.
        Vulkan: If you get a black screen when using the Vulkan rendering option, switch back to DX11. Also make sure that the dxvk folder is in the same folder as the games executable.
    Extra Heroes: Kicking animations and their range during the Deepening level can be off. Kicking works fine against enemies, but some of the new characters have to be angled to the side to kick down the ladders. Again kicking works fine against enemies but not the ladders during this level. NPC's like Sauron, Frodo, and Lurtz can kick them just fine, but Elrond, Boromir, the Uruk and Gandalf may have some trouble and need to be angled to the side.
    Extra heroes: Sauron and Lurtz are too tall for the end-of-level upgrade screen and are cut off.
    Extra heroes: your hero takes Aragorn's place in the cutscenes; where his own character also appears (Boromir, Gandalf and Frodo in Balin's Tomb), such a scene shows him twice. Also when playing as characters that have new weapons and ranged attacks, some of those weapons may show up on other NPC's. This doesn't happen with the mod turned off.
    Extra heroes: Currently the models used for the upgrade menu in the in game pause menu show up mangled. The models in the actual upgrade screen after a level is completed appear normal and play their animations.
    Unlocked fps is experimental: One Known issue with frame rates above 60 fps is that some of the NPC animations during the prologue level fail to complete during the end of the in game cutscene for that level. Example is both Sauron and an elf NPC not turning their heads properly. Another known issue is Sauron falling through the Gates of Moria level after the intro cutscene during gameplay. At 60 fps these issues don't occur, so it is advised to set the frame rate to 60 fps before beginning the prologue or when playing that mission with Sauron. Crackling audio can occur at unlocked frame rates as well.
    Widescreen is experimental: a few full-screen effects may not line up at the edges.

Bug reports are welcome, especially for Mac and Linux users. Please include twotowers_log.txt, which is next to the game (for the Extra heroes mod, its [HEROES] lines).

Screenshots and Gameplay:

<img width="3840" height="2160" alt="Screenshot 2026-10-16 11-55-43" src="https://github.com/user-attachments/assets/ea3ce898-b730-42ae


<img width="3840" height="2160" alt="Screenshot 2026-10-16 11-56-39" src="https://github.com/user-attachments/assets/d4c031fa-852d-4448-95fd-0143d080b8d5" />


<img width="3840" height="2160" alt="Screenshot 2026-10-16 11-56-43" src="https://github.com/user-attachments/assets/882c535a-8f86-4078-a121-a02b1fd3760e" />

<img width="3840" height="2160" alt="Screenshot 2026-10-16 11-56-56" src="https://github.com/user-attachments/assets/b96fb9ba-cd0e-4372-a379-3bebd560968e" />




https://github.com/user-attachments/assets/449a0282-e7a1-411b-b408-3d6efe263bc8



https://github.com/user-attachments/assets/5f94968f-4495-4255-bf60-1e4cf17cf93e





https://github.com/user-attachments/assets/ac6a6be2-0a31-4150-a950-2fe51930cd3e




https://github.com/user-attachments/assets/b3eb1f43-a06b-4e18-8734-1e3cc1443df6




https://github.com/user-attachments/assets/00546d04-6453-4923-9260-b53e81e554df


<img width="4608" height="1296" alt="Untitled3" src="https://github.com/user-attachments/assets/c9254d2a-92aa-44ca-8b0d-40faea7f567a" />


<img width="4608" height="1296" alt="Untitled" src="https://github.com/user-attachments/assets/5bfd36a7-ee5a-43d2-a0ce-05708546c900" />

<img width="1920" height="1080" alt="New folder (8) Screenshot 2026 10 16 - 11 50 36 24" src="https://github.com/user-attachments/assets/cd27127e-3580-4620-928b-2710676de681" />

<img width="1920" height="1080" alt="New folder (8) Screenshot 2026 10 16 - 12 16 28 12" src="https://github.com/user-attachments/assets/5e621dc1-031b-4191-b3af-47542d20ac47" />


<img width="4608" height="1408" alt="Untitle33d" src="https://github.com/user-attachments/assets/e4df0b16-506f-488c-ad76-4aafbfaf8b5f" />


<img width="3840" height="2160" alt="Screenshot 2026-10-16 12-00-37" src="https://github.com/user-attachments/assets/11b96e05-570a-4d35-ad20-39f3a484b7af" />


