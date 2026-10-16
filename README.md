THE TWO TOWERS RECOMPILED
=========================

An unofficial native Windows (x64) port of The Lord of the Rings: The Two
Towers (original Xbox, USA release), made by static recompilation.

DISCLAIMER
----------
* This is for educational and game-preservation purposes only. It is a
  non-commercial fan research project.
* It is not affiliated with, endorsed by, or connected to Middle-earth
  Enterprises, the Tolkien Estate, New Line Cinema, Warner Bros.
  Entertainment, Electronic Arts, Stormfront Studios, Microsoft, or any of
  their affiliates or licensors.
* The Lord of the Rings, The Two Towers and all related names, characters,
  places, logos and images are trademarks and/or copyrighted material of
  their respective owners. Xbox is a trademark of Microsoft.
* The authors of this project own no rights to any of them. All rights
  remain with their owners.
* This package contains no game data: no movies, sounds, music, textures,
  models, levels, text or cover art.
* The optional ECTS 2002 HUD mod's portrait icons (the e3hud folder) are
  small crops of the publicly shown ECTS 2002 demo footage of the game, and
  (boromir.bmp, frodo.bmp, lurtz.bmp, elrond.bmp, sauron.bmp, uruk.bmp,
  for the Extra heroes mod) of the game's own artwork. They remain the property of their rights holders and are
  included only so the mod can show the demo's HUD; delete the folder if you
  prefer (the mod then makes portraits from your own game's character
  textures).
* The game program (twotowers_recomp.exe) does contain code mechanically
  translated from the original game's executable, as every static
  recompilation does. That code is not covered by this project's licence
  and remains the property of its rights holders.
* That translated code cannot run on its own: the program loads the game's
  original executable and all of its data from your own copy.
* Use this package only with your own legally obtained USA copy of the game.
  If you don't own the game, don't use it. Do not use it for piracy; the
  launcher only accepts the USA retail release.
* This software is provided "as is", without warranty of any kind.

REQUIREMENTS
------------
* Windows 7 SP1, 8.1, 10 or 11, 64-bit, with a Direct3D 11 graphics card.
  On Windows 7 and 8.1 the game also wants the HLSL shader compiler
  (d3dcompiler_47.dll, KB4019990 on Windows 7, or the DirectX End-User
  Runtime); without it the game uses its slower software renderer. Sound
  uses XAudio2 where Windows has it and waveOut otherwise.
* Your own copy of the game, USA Xbox release, in either form:
  - a disc image (.iso / .xiso), or
  - the disc's extracted files (the folder with default.xbe).
  Other regions are not supported; the launcher checks.

INSTALLING
----------
1. Keep this folder on its own; don't put it inside another game's install
   folder. Settings, saves and the log are written here.
2. Run "The Two Towers Recompiled.exe" (the launcher) and choose one of:
   - "Install from ISO..." to pick your disc image. The launcher checks it and
     extracts the game into a "game_files" folder here (a minute or two).
   - "Use extracted folder..." to pick the folder containing default.xbe.
     Nothing is copied.
3. When the status line says "Game files ready", press Play.
   Later you can also start twotowers_recomp.exe directly.

WHAT THE PORT ADDS
------------------
On top of the original game, which plays as it did on the Xbox:
* Runs natively on Windows (and Linux and macOS), from your own disc; the
  launcher installs it from an ISO or an extracted folder.
* Languages (1.9.2): 33 languages besides English for every text in the
  game -- menus, hints, upgrades, credits and all subtitles -- in the
  game's own fonts, plus the launcher and the F10 menu.
* Resolution and widescreen: internal resolution 1x to 12x (4.5x for 4K),
  4:3 to 32:9 with the HUD at the screen's edges, optional widescreen
  cutscenes, menus that fill the screen, windowed or borderless full
  screen at any window size.
* Picture: Direct3D 11, Vulkan (DXVK) or software; MSAA and FXAA,
  anisotropic filtering, mipmaps, VSync, brightness, sharpening, colour;
  a shader cache.
* Ultra textures: sharper menus, text and HUD, and every texture with the
  GameCube high-resolution pack (downloaded separately).
* Frame rate: 30 fps as on the Xbox, or 60 / unlocked (experimental).
* Keyboard and mouse: a full layout, every action rebindable, a Walk key,
  a mouse pointer in every menu, mouse aiming and mouse steering.
* Controllers: Xbox / XInput, PlayStation, Switch Pro and others with
  rumble; button prompts and loading-screen pictures that match what you
  play with and follow your bindings.
* Menus: Display and Controls options pages in game, the F10 settings
  panel, an Exit item, intro films skipped with a click.
* Mods (off by default): the ECTS 2002 demo's HUD, orange health pickups,
  and seven extra playable heroes (Boromir, Gandalf, Frodo, Lurtz, Elrond,
  an Uruk-hai, Sauron), each with his own model, moves, weapons, voice,
  pictures and upgrades.

NEW IN 1.9.2
------------
* Languages: choose one in the launcher's "Language" list (or [Language]
  language= in the settings file). See LANGUAGES below.
* Mouse steering works with every move key: the mouse sets the way ahead
  (from the camera's view); W goes that way, S back, A and D to its sides.
  Walk walks while steering. Let go of the keys and the hero stops.
* The mission results page shows the hero you played: after Balin's Tomb
  as Legolas it could show the previous mission's Aragorn.

LANGUAGES
---------
English, French, German, Spanish (Spain), Spanish (Latin America),
Italian, Portuguese (Brazil), Portuguese (Portugal), Dutch, Swedish,
Danish, Norwegian, Finnish, Polish, Czech, Slovak, Hungarian, Romanian,
Croatian, Turkish, Greek, Bulgarian, Russian, Ukrainian, Vietnamese,
Indonesian, Japanese, Korean, Chinese (Simplified), Chinese (Traditional),
Thai, Hindi, Hebrew and Arabic.
* Everything the game writes is translated, subtitles included, and the
  port's own words too (Options pages, key names, hints, the extra
  heroes' names and upgrades, the launcher, the F10 panel).
* Letters the game's fonts lack are added to them at each font's size;
  Hebrew and Arabic read right to left; Chinese, Japanese and Thai lines
  break between words.
* The "lang" folder holds only the translations. The game's files are
  copied with the language's text into "lang_cache" on first use (a few
  seconds); your game files are not changed.
* Spoken dialogue stays English (the game has only English voices).

NEW IN 1.9.1
------------
* Mouse steering works like aiming with the ranged button: hold W and the
  mouse is the stick -- the hero runs the way the mouse pushes (from the
  camera's view) and stops when you let go of W.
* Sauron's sprint and its option are removed.
* The Uruk-hai throws his rocks (and their upgrades) at the Gates of Moria
  too, where he threw arrows; they hurt the Watcher.

NEW IN 1.9
----------
* Ultra textures (Graphics settings / Display Options): sharper menus,
  level select and its pictures, text and HUD come with the game. With the
  GameCube high-resolution texture pack (GLOS69, downloaded separately from
  its author) every texture is replaced: see ULTRA TEXTURES below.
* Internal resolution up to 12x. A picture larger than the window is now
  averaged down to it, so high settings also smooth edges.
* MSAA anti-aliasing: 2x, 4x or 8x, with or without FXAA.
* Up to 512 particle effects at once: the archers' fire pots no longer go
  out when a beam or a fire rock is thrown.
* From 1.8.2: the in-engine scenes stay 4:3 in widescreen unless
  "Widescreen cutscenes" is on; menu buttons no longer press themselves;
  right click backs out of Options and unselects on the character select;
  the Uruk-hai's fire rock burns; the upgrade screen's level leaves follow
  the pointer.

ULTRA TEXTURES
--------------
* Turn on "Ultra textures" in Graphics settings or Display Options. The
  upscaled menus, level select, text and HUD (textures_ultra\ui) work at
  once.
* For every texture: download the GameCube high-resolution texture pack
  (GLOS69) from its author -- it is not included -- and put its GLOS69
  folder (full of tex1_... files) here, next to twotowers_recomp.exe.
* The first time you press Play with the option on and GLOS69 in place,
  the launcher makes the pack ready (once, a few minutes, with a progress
  bar; about 1 GB in textures_ultra). Turned on in game instead, the
  launcher opens and does it while you play.
* The green health pickup keeps its own texture (for the orange pickups
  mod).

NEW IN 1.8.1
------------
* The picture galleries step with a click on the left or right half.
* Replay, Continue and the level leaves on the end-of-level screens answer
  the pointer at high frame rates.
* Window size in Display Options (and F10).
* Mouse steering (Controls page / launcher, off by default): the mouse
  turns the way the move keys go.
* Unlocked frame rate is unlocked throughout again (1.8 used 60 in the
  scenes). If heads stop turning in scenes, or Sauron falls through the
  ground at the start of the Gates of Moria, set 60 fps before them.
* Extra heroes: Frodo's and the Uruk-hai's stones hurt the Watcher.

NEW IN 1.8
----------
* The mouse in every menu: point to choose, click to press, right click
  to go back. Values and sliders in the options are clicked or dragged, the
  mission select picks the place under the pointer, the leaves (Back,
  Continue, Replay) and the upgrade screens' level leaves are clicked, and
  the mouse wheel scrolls. A left click skips the intro films.
  Turn the pointer off with "Mouse pointer in menus".
* Options (main menu, mission select, pause screen) opens on a first page:
  General Options (the game's own), Display Options (every graphics
  setting) and Controls (the mouse and every key binding:
  choose one and press the key; Esc cancels, Delete clears). Tab (Back)
  backs out of them.
* Aiming with the mouse: hold the ranged button and move the mouse (with
  invert, sensitivity and pointer size settings).
* Exit on the main menu. "Press Enter" on the title screen and in the
  menus' messages when you play with the keyboard.
* Internal resolution up to 8x, and 4.5x for 2160 lines (3840 x 2160 at
  16:9).
* Extra heroes: new heroes' weapons no longer turn up in other
  characters' hands; a crash on the upgrade screen after a level is fixed.
* Everything from 1.7.1 to 1.7.8 (menus fill the screen, Xbox One prompts,
  the Walk key, and more): see README.md.

GRAPHICS SETTINGS
-----------------
In the launcher: press "Graphics settings...".

In game: press F10, or hold Back and press Start on a controller.
* Up/Down chooses an option, and Left/Right (or A / Enter) changes it.
* B, Esc or F10 closes the menu.
* Changes apply immediately and are saved.

  Renderer             Launcher only. Direct3D 11, Vulkan (DXVK) or Software.
                       Vulkan (new in 1.6) runs the same renderer through
                       DXVK: download a DXVK release and copy its
                       x64\d3d11.dll and x64\dxgi.dll into a folder named
                       "dxvk" here. Without them Direct3D 11 is used.
  Frame rate           30 (original), 60 or Unlocked. 60 and Unlocked are
                       experimental; use 30 if the game runs too fast.
  Display mode         Windowed or full screen. Alt+Enter or F11 also switches.
  Aspect ratio         4:3, 16:9, 16:10, 21:9 or 32:9. Widescreen is
                       experimental; the 3D view widens after the next load.
  HUD in widescreen    At the screen's edges (new in 1.6, the default) or
                       centred in the 4:3 middle as before.
  Menus                Fill the screen (the default: menus, loading and
                       upgrade screens past the old TV margin) or Original.
  Movies in widescreen As made (4:3, the default) or Fill the width (the
                       films' black bands cut off, a little closer).
  Internal resolution  1x (640x480) to 12x (7680x5760), and 4.5x (2160
                       lines). Sharper, and smoother edges, but costs
                       graphics-card time; 9x-12x need a lot of video memory.
  Anisotropic filter   Off to 16x.
  Mipmaps              On means less shimmer on distant textures.
  Anti-aliasing        Off, FXAA, MSAA 2x / 4x / 8x, or MSAA 4x / 8x + FXAA.
                       MSAA smooths the 3D's edges as drawn; FXAA the
                       finished picture.
  Ultra textures       Off or On. See ULTRA TEXTURES.
  VSync                On means no tearing.
  Picture              Keep shape or stretch to fill.
  Vertex programs      Graphics card (fast) or CPU (for troubleshooting).
  Brightness           Darker to Brightest (new in 1.1).
  Sharpening           Off, Low, Medium or High (new in 1.1).
  Colour               Original, Vivid or Muted (new in 1.1).
  Mod: ECTS 2002 HUD (experimental)
                       Launcher only. See MODS below.
  Mod: Orange health pickups (shown in pre-release & making-of videos)
                       Launcher only. See MODS below.
  Mod: Extra heroes    Launcher only. HIGHLY EXPERIMENTAL. See MODS below.
  Window size          Launcher only.

CONTROLS
--------
An Xbox / XInput controller works out of the box. PlayStation pads
(DualShock 3, DualShock 4, DualSense) work too, with rumble, through the
included SDL2.dll (keep it beside twotowers_recomp.exe). The keyboard and mouse act
as controller 1 by default:

  Move (left stick)   W A S D           Right stick     Numpad 8 4 2 6
  (Mouse steering on: the mouse sets the way ahead, W A S D move by it)
  A                   K, Left click, Space
  B                   L                 X               J, Right click
  Y                   I                 Black / White   C / Z
  Triggers L / R      F / R             D-pad           Arrow keys
  Start               Enter, Esc        Back            Backspace, Tab
  Stick clicks        Shift / Middle click
  Walk (hold)         B  (new in 1.7.6: walk instead of run, any hero)

To remap controls, for keyboard/mouse and controller:
* In the launcher: press "Controls...".
  - Click a key box, then press a key or mouse button.
  - The drop-down list on the right picks your controller's button.
  - "x" clears an action's keys; Defaults resets.
* In game: press F10, go to the "Page" line, and press Right for Controls.
  - Press A or Enter on an action, then press the new key. Esc cancels and
    Delete clears it.
  - Left/Right changes the controller button.

BUTTON PROMPTS
--------------
[Input] prompts=auto|xbox|xboxone|playstation|keyboard (also "Button
prompts" in the F10 menu and the launcher's Graphics settings).
* New in 1.7.8: controller icons follow your bindings. Each icon shows
  the button of your controller bound to that action (rebind A to RB and
  the A icons show RB), and the loading screens' hints name your buttons.
* Xbox One (new in 1.7.8): black buttons with coloured letters, LB / RB,
  LT / RT, View and Menu, and an Xbox One pad on the loading screens'
  controls pictures (drawn for this port; in the "prompts" folder).
* PlayStation: the game's button icons show cross, circle, square,
  triangle and R2, and the loading screens' controls pictures show a
  PlayStation pad: the DualShock 3, DualShock 4 or DualSense you have
  plugged in ([Input] ps_pad=auto|ps3|ps4|ps5 picks one). Those pictures
  are in the "prompts" folder; they are drawn for this port.
* Keyboard / mouse (new in 1.6): each icon shows the key or mouse button
  that action is bound to now (a keycap such as K, SPACE or NUM 8, or a
  mouse with the button lit). Rebinding a key changes it at once.
* Automatic (the default) follows what you play with: keyboard/mouse
  icons once you use the keys or mouse, PlayStation ones with a
  PlayStation pad, Xbox ones otherwise.
The prologue's in-play hints change too (new in 1.6). New in 1.6: with
keyboard / mouse icons the loading screens' controls pictures show a
keyboard and a mouse with the keys you have bound, and the hint under
them names your keys.

MODS (all off by default)
----
Set them in the launcher's Graphics settings (or [Mods] in the settings
file). They take effect when the game starts, and change nothing when off.

Why they exist
  Before release the game was shown at ECTS 2002 (the European Computer
  Trade Show) and in trailers and making-of videos, and that build looked
  different: another HUD, and orange health pickups instead of green ones.
  None of it shipped; the retail game even keeps a debug switch "Display
  EXP and Runes" with the code behind it removed. These mods recreate that
  pre-release look from the footage, for preservation -- to play the game
  as it was shown. They are reconstructions, not recovered code.
  (They were called "E3" in earlier builds; the footage is from ECTS 2002.
  The settings keys keep the old names, e3_hud and e3_pickups.)

ECTS 2002 HUD ([Mods] e3_hud=1) -- experimental
  What changes from retail, as in the demo footage:
  - the hero's portrait in a medallion on the health sword, and a
    cross-guard curved toward the blade (health bar stays green);
  - the ornate experience tube becomes a plain rectangle filling blue;
  - under it, a rune orb whose quarters fill as you enter a combo
    (A speed = blue, Y fierce = green, B knock back = red);
  - the combo's name (Speed / Fierce / Special / Mixed) beside the orb, in
    the game's own lettering, enlarged to match the footage;
  - the kill rating ("Good", "Excellent" ...) moves beside the gauge, and
    an "EXP: current/needed" readout shows progress to the next level;
  - the loading-screen tips about experience and the Skill Meter
    (Fangorn) show the new gauge;
  - the mission results screen shows the new gauge in place of the
    retail one;
  - on the Helm's Deep wall level, the portrait at the end of the
    progress bar is the demo's picture (e3hud\level10.bmp);
  - at Westfold, the villagers left are shown as the demo's two rows of
    villager faces, crossed out as they are lost (e3hud\villager.bmp).
  Experimental: it is drawn over a HUD that was never meant to change, so
  an odd frame may show a piece out of place. Turn it off if anything
  looks wrong.

Orange health pickups ([Mods] e3_pickups=1)
  The green health vials glow orange, as in pre-release and making-of
  videos. Only the vial's glass is recoloured, as the texture is loaded;
  your game files are not changed. To turn another green item orange too:
  set texture_dump=1 in the settings file, play until it is on screen,
  quit, find it among the files in the texture_dump folder (named
  HASH_WxH_fFMT.bmp) and put its HASH in [Mods] e3_pickup_textures=
  (comma separated for several). Then set texture_dump=0 again.

Extra heroes ([Mods] extra_heroes=1) -- HIGHLY EXPERIMENTAL
  !! Much better in 1.3, but still experimental: expect the odd visual
  !! glitch, and possibly crashes. Turn it off again if anything goes
  !! wrong. Your saves are not changed by it.
  Adds Boromir, Gandalf, Frodo, Lurtz, Elrond, the black Uruk-hai and
  Sauron to the character select (eleven heroes), made from the game's own
  models.
  * Unlocking (new in 1.3): one at a time, by finishing the Tower of
    Orthanc as Isildur on that save: the 2nd finish unlocks Boromir, the
    3rd Gandalf, the 4th Frodo, the 5th Lurtz, the 6th Elrond, the 7th the
    Uruk-hai and the 8th Sauron (1.6). Each finish counts on the save you
    play. A locked hero shows as a faint
    shadow and is skipped. "On, unlocked for testing" (extra_heroes=2)
    shows them all on any save; nothing is written to it.
  * Each takes Isildur's place in the level, with his own experience,
    scores and upgrades, kept in the "heroes" folder (heroes\records.dat,
    unlocks in heroes\unlocks.txt). That folder also holds the mod's
    copies of the game files it changes; delete it to reset the new heroes.
  * New in 1.3: they play on their own skeletons, with their own walk,
    run, block, hit reactions and death. Boromir and Frodo fight with the
    heroes' light and heavy attacks fitted to their bodies; Gandalf and
    Lurtz with their own. Heavy attacks hit as the heroes' do, and
    (1.5) Boromir's and Frodo's blows and kicks go where they face.
  * New in 1.5.2: the level's own Boromir, Frodo, Gandalf and Lurtz (an
    ally, or Lurtz the Amon Hen boss) are left alone when you play as one
    of them, so the Boromir fighting beside you in Balin's Tomb no longer
    stands stuck. The prologue is always played by Isildur himself, as in
    the original game. Boromir's and Frodo's bow shot plants their sword
    in the ground as Aragorn's does. On the character select the eight
    stand further apart and all of them keep their idle moving.
  * New in 1.6: the first press of the light attack swings (Frodo fell
    and Boromir flinched before the swing came). Frodo's model is matte
    (it looked wet or shiny) and whole on the end-of-level screen. Levels
    rebuilt for the heroes by an older version are no longer reused (that
    caused Frodo to vanish from Caradhras' opening scene and crashes
    there and in Amon Hen); the "heroes" folder's old *_own_* folders can
    be deleted.
  * New in 1.6: Elrond (the prologue's, with the elves' curved sword),
    the black Uruk-hai (his great sword; he throws rocks, burning ones
    with his last ranged upgrade) and Sauron (his mace; a red bolt of power
    for a ranged attack, and he takes blows better). Frodo throws stones
    instead of shooting a bow. Everyone kicks and parries with the heroes'
    kick and parry, falls when he dies, stands still as he aims and stands
    in his own stance. Their own voices in play (Lurtz and the Uruk-hai
    their own, Elrond and Frodo Legolas's, Sauron Lurtz's made deeper,
    Boromir Aragorn's made deeper, Gandalf Gimli's a little deeper) and
    their own sounds when chosen on the character select, where Sauron
    comes last. Their own
    ammo counter (hudammo folder) and upgrade-screen items (upgitems
    folder), upgrade names, a posed figure on the pause screen, and your own
    upgrade-screen pictures from an upgpics folder. A crash loading
    Fangorn and the Helm's Deep levels as Sauron, Lurtz or the Uruk-hai is
    fixed.
  * New in 1.3, their own weapons: Boromir's sword and shield; Gandalf's
    staff, always in hand (1.6: in his left hand, with a sword in his
    right), and a beam of light from it instead of arrows;
    Frodo's Sting, glowing a pale blue; Lurtz's Uruk-hai sword and bow.
    They carry them wherever they appear as the hero: in play, in the
    cutscenes, on the character select and in the upgrade figure.
  * New in 1.3, their own pictures: select-screen pose, pause-screen and
    end-of-level figure, HUD portrait (e3hud\boromir.bmp, frodo.bmp,
    lurtz.bmp, elrond.bmp, sauron.bmp, uruk.bmp, gandalf.bmp with the ECTS
    HUD mod),
    upgrade and results pictures.
  * New in 1.6: the character select shows the level of every unlocked
    character in the distance, Isildur's and the new heroes' too.
    Isildur no longer loses his sword or stands frozen on the select after
    you played as one of the new heroes. Gandalf is tougher (a little
    less than Sauron: more health, flinches less), Lurtz a little less
    than Gandalf. The Uruk-hai's rising attack is his own getting up.
    Each Orthanc finish as Isildur now counts on exactly the save the game
    writes after it. Their shots have their own sounds (Gandalf's and
    Sauron's bolts, Frodo's stones, the Uruk-hai's burning rocks) and the
    bolts show only the beam. With the ECTS HUD: Frodo's portrait and the
    cave troll's health bar (Balin's Tomb) as in the demo.
  * New in 1.6: the unlocks go with your game. Loading a save brings that
    save's count of Isildur's Orthanc finishes; every save you write
    carries the count of the game you are playing (whichever slot, and
    whichever hero you played since), one more when it is the save after
    his finish. The 2nd finish unlocks Boromir, then one more hero each
    finish. Gandalf's shots have the bow's sounds. Each hero's six named
    moves have names of their own (Sauron's Dance, You Shall Not Pass,
    Sting's Bite, ...), and his ranged weapon is named and described for
    what it is (Frodo's stones, the Uruk-hai's rocks, Gandalf's and
    Sauron's fire) instead of bows.
  * New in 1.7 (the final release): the new heroes are whole in the
    in-game cutscenes and play the heroes' own moves there, fitted to
    their bodies (Fangorn's opening scene, Balin's Tomb's, the Gates of
    Moria's) -- no longer twisted or torn into stretched shapes. Gandalf and Lurtz take
    less damage (the health bar is the same size). Gandalf's voice is
    Gimli's, a little deeper.
  * New in 1.7.1: the last ranged upgrade changes the shots. Gandalf's
    beam turns white (White Fire). Sauron's bolts are red and blue in
    turn (Doom Fire); before it, always red. The Uruk-hai throws plain
    stones until it, then burning rocks (Lava Rocks), and every third one
    bursts where it lands or strikes, as the Orthanc archers' fire pots
    do, leaving fire on the ground that hurts. His first two ranged
    upgrades are now Crag Rocks and War Rocks.
  * New in 1.7.2: Lurtz's and the
    Uruk-hai's blows reach further, and Lurtz is tougher. The new heroes
    no longer go sideways in water. The pause screen's figures stand
    upright. 1.7.3: every new hero fights with his own attacks again, as
    in 1.6. 1.7.4: Sauron's sprint is removed, and he keeps all his own
    moves. 1.7.5: a new hero who falls through the ground (Sauron at the
    Gates of Moria) is put back where he last stood (1.7.8: also when he
    falls fast, or right at the level's start).
  Known problems: Gandalf's, Boromir's, Frodo's and Elrond's voices are
  other characters'; Sting's glow is a tint of the blade, not a light.

SAVES, LOG, PROBLEMS
--------------------
* Saves are in game_files\UDATA (or in your extracted folder's UDATA).
* twotowers_log.txt is written here. Attach it to bug reports, after
  checking that it contains nothing you'd rather not share.
* Slow: lower Internal resolution first, then MSAA, then filtering, and
  turn FXAA off.
  Keep texture_dump off ([Mods] texture_dump=0): writing every texture
  to disk makes the game stutter. Compiled shaders are kept in the
  shadercache folder (new in 1.6): a new effect stutters only the first
  time; deleting the folder is safe. The Vulkan (DXVK) renderer can be
  faster with some drivers.
* Black window with Renderer: Vulkan (seen on a laptop with GeForce and
  Intel graphics): use Direct3D 11. In the NVIDIA Control Panel,
  "Vulkan/OpenGL present method: Prefer layered on DXGI Swapchain" may
  help. The log and DXVK's twotowers_recomp_d3d11.log help find it.
* Sound breaking up in busy scenes: 1.6 keeps a small cushion of sound
  queued and refills it at once. On a slow PC set the environment variable
  RECOMP_AUDIO_CUSHION to a larger number (up to 14) before starting.
* Black window: update your graphics driver.
* Keyboard does nothing: click the game window so it has focus.
* Controller does nothing: the game reads it only while its window is in
  front. Click the window, or set [Input] background_input=1.
* Hero walks on his own with the stick at rest (stick drift): raise
  [Input] stick_deadzone (percent, default 10).
* Recommended: play at 30 fps. If the game crashes or freezes, restart it;
  the same spot usually works on the next try.
* If the game closes on its own, the end of twotowers_log.txt says where
  ([CRASH]); please include it in a report.

LICENCES
--------
* The port's own code is MIT licensed (LICENSE.txt).
* It includes xboxrecomp (MIT) and parts of xemu (LGPL-2.1-or-later); see
  THIRD-PARTY-NOTICES.txt and LGPL-2.1.txt.
* The complete source code, from which these programs can be rebuilt
  (including with modified versions of the LGPL parts), is published
  alongside this package as TwoTowers-Recompiled-v1.9.2-source.zip.


