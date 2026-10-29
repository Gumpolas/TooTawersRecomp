# Couch co-op/Versus modes for The Two Towers Recompiled

** Co-op is not fully playable with the extra heroes mod. It is recommended to play co op with only the four main heroes. The extra characters are playable, but they do not show up in cut scenes properly yet. Even with player 1 as a main hero and player 2 as an extra hero, the in game cut scenes don't show the proper animations for both hero's. Some HUD elements might not show correctly with the extra heros as well (Sauron as player 2 shows his ammo hud for both players)

Cave troll battles and Versus only/exclusive modes and hero's require the extra hero's mod to be enabled. 

Two players on one PC play the story levels together. They share one camera, and each player has their own
hero, controller, weapons, health, ammo, experience and upgrades. There is no networking: one game runs, with a
second hero in it.

Co-op is part of The Two Towers Recompiled from **1.9.6**. It was in development for the main port when
[Volverine-Dude](https://github.com/Volverine-Dude) published his own implementation in the
[TooTawersRecomp-coop](https://github.com/Volverine-Dude/TooTawersRecomp-coop) fork (on 1.9.4). 1.9.6 brings the two
together on top of 1.9.5, thanks in large part to his work, with player 2's device choice and double pickups added. It is still an **alpha**: see
[Known issues](#known-issues).

It needs nothing beyond the port itself: the release build has co-op in it, and it plays from the same game (the US
Xbox retail release). Nothing from the game is in this repository.

## Building

Co-op is built with the port: see the README's [Building from source](README.md#building-from-source).

## Playing

- **Starting:** the main menu has a **Co-op** item. Its New Game and Load Game work as the solo ones, then the hero
  select asks each player for a hero (player 2 can't take player 1's).
- **Devices:** Options > Co-op Options > P1 Device / P2 Device: Automatic, Keyboard & Mouse, or any controller
  connected. Automatic, with a human player 2 (P2 Bot off): one controller gives player 1 the keyboard and mouse and
  player 2 the controller; two controllers give one each (the keyboard and mouse stay player 1's). With the P2 Bot on,
  every device is player 1's. Player 1 runs the menus. Saved as `[Coop] p1_device` / `p2_device` (-1 automatic,
  0 keyboard and mouse, n the n-th controller).
- **Co-op Options** (Options, and the pause menu during a co-op game; the Co-op menu shows the first five; saved in
  the ini's `[Coop]` section):

| Setting | Values | What it does |
|---|---|---|
| Difficulty | Easy, Normal, Expert | Enemy damage x0.75 / 1 / 1.5, heroes' health x1.5 / 1 / 0.75, enemies from spawners come back 0 / 1 / 2 extra times, a downed hero's wait 15 / 30 / 45 s |
| Player Icons | Off, On | "P1" / "P2" over the heroes' heads, dimmed when something is drawn in front of the head (Direct3D 11 only) |
| Camera Zoom | Off, Normal, Wide | The shared camera zooms out as the heroes part, up to 1.3 (Normal) or 1.5 (Wide). **Recommended: Normal or Wide** in the levels, so both heroes stay in view |
| P2 Teleport | Off, On | Player 2's hero is put beside player 1's after falling too far behind |
| P2 Bot | Off, When Idle, Always | The game plays player 2 (a stopgap AI, below). When Idle: any input on player 2's controller hands it to the player for 3 s; Always: the controller is ignored *(1.9.8)* |
| P2 Controls | (a page) | Player 2's own controller buttons. Co-op menu and Options *(1.9.8)* |
| Pickups | Normal, Double | Double: a health or ammo pickup can be taken twice, once by each hero (the second at least 1.5 s after the first). Options page only *(1.9.6)* |
| P1 Device, P2 Device | Automatic, Keyboard & Mouse, a controller | Each player's device (above). Options page only *(1.9.6)* |

- **Camera:** playing the levels with Camera Zoom on **Normal** or **Wide** is recommended; with Off the shared camera
  stays close and a hero can end up off screen. Some areas may work better with the camera set to off, so it's up to players to experiment with those settings. 
- **A hero who goes down** comes back near the other one after the wait, on ground player 1 has stood on (not in a
  wall or over a drop). Both down is a game over.
- **Story levels in co-op** *(1.9.8)*: player 2 is Isildur on the prologue and Legolas on Weathertop (with copies of
  player 1's weapons). A level beaten with a hero shows **Already Beaten** on the mission select for that hero; once
  the game is cleared every story level is open. Films can be skipped the first time they play.
- **Aiming** *(1.9.8)*: while a player holds LT, a dot over the enemy his lock is on, in his colour (player 1 blue,
  player 2 amber), smaller the further it is from the camera. A hero's arrows and thrown axes fly through allies (the
  other hero, the companions) and hit only the enemy locked on, or a wall; every hit counts (the game took some
  enemies' damage only in some of their states: the cave troll ignored most arrows). A lock never stays on an ally.
- **Extra heroes as player 2** *(1.9.8)*: built and drawn in every level, also in Fangorn Forest, the Westfold and
  with player 1 Legolas; his blows on a hand he has no weapon in land on his own weapon, and they are tested for hits
  as his swings.
- **The128ReBits' heroes as player 2** *(1.9.9)*: the Elf Warrior, the Elf Archer, the Cave Troll, the Rohan Captain,
  the Uruk Captain and Gandalf the White can be player 2 in every level, as the first seven.
- **Player 2 as Legolas, Aragorn or Isildur** *(1.9.9)*: beside a different player 1 hero, his blows land and he kicks
  ladders down again (he moves in player 1's hero's machine, which kept his attack flag down for most of his swings:
  on the Deeping Wall only Gimli and the extra heroes hit anything). TT_COOP_P2ATKFLAGGUEST: as 1.9.8.
- **Explosives** *(1.9.8)*: player 2's shots set off the Westfold's bomb stacks and carts, as player 1's do.
- **After a level:** player 1's stats, player 2's stats, player 1's shop, player 2's shop, then the save. Player 2's
  experience and purchases are saved in their hero's record and come back when the save is loaded.
- Player 2 and breaking enemy shields: You may have noticed that player 2 has trouble breaking enemy shields. This is intentional for now until more balancing can be applied. Player 2 can still kill enemies with shields, but it requires a few more heavy attacks.
- More enemies in co op/disappearing enemies: You also may notice in co-op that there are more enemies in most maps than in single player. This is also for balancing reasons, and makes it so that player 1 and 2 can't just steam roll through the levels. With these extra enemies spawning into the levels, they can sometimes disappear or vanish once killed due to them not originally belonging in the level.

## Versus (1.9.7)

Player 1 against player 2, one on one, on the co-op's two heroes.

- **Starting:** the main menu's **Versus** (under Co-op). Choose each player's hero and the arena, then **Start**.
  Heroes: Aragorn, Legolas, Gimli and Isildur, plus the thirteen extra heroes and Versus' own three when the Extra
  heroes mod is on (both players can't be the same hero; on Weathertop player 1 is Aragorn or an extra hero).
- **Mode:** Deathmatch on every arena. **Rounds to win** (the Versus menu): 3, 5 or 7.
- **Versus-only heroes** (not in the character select, the campaign or co-op), each with his own swings, flinches,
  falls and voice, the heroes' kick as his special, and a thrown rock for a ranged attack:
  - **Nazgul** (Weathertop's Ringwraith, his sword). When he falls he flees, as from Aragorn's fire on Weathertop,
    and is gone until the next round.
  - **Shield Orc** (the prologue's brown orc, his spear and his shield). The shield halves every blow on him until it
    breaks: six blows (a heavy one counts twice), then it is gone from his arm for the rest of the round.
  - **Eomer** (Westfold's captain, the Rohirrim's sword and voices).
- **Music:** each arena plays another level's track, so no two sound alike.
- **Voices:** each hero cries out as he swings, when he is struck and when he falls, in his own voice.
- **Amon Hen:** the arena ends 50 units from where the match begins (past it the level is unfinished).
- **Starting spots** 30 units apart. On the bigger arenas (Amon Hen, Fangorn Forest, the Gates of Moria, the Plains of
  Rohan, the Road to Helm's Deep, the Deeping Wall) the rounds after the first go on where the heroes are.
- **Blocking:** a guard breaks after 4 blocks in a row, and that blow lands. Kicks knock back but do almost no damage.
- **Progress:** Versus doesn't touch the campaign. Both players are at the top level with every move for the match
  only; your saves, levels and the extra heroes' records are put back as they were when you leave Versus.
- **Arenas:** every level but Westfold: Weathertop, Balin's Tomb, Amon Hen, Fangorn Forest, Gates of Moria, Plains
  of Rohan, Road to Helm's Deep, the Deeping Wall, the Breached Wall, Hornburg Courtyard, Orthanc, Orthanc's summit
  and the prologue (Mount Doom).
- **The arena:** the level without its story: no other characters, triggers or cut-scenes (the opening one too: the
  match starts as the level loads). The scenery stays.
- **Fixed loadouts:** both heroes get the same health (1500) and every move, whatever the saves hold. The pause menu
  has no Show Upgrades.
- **Rounds:** each round starts with a countdown (the round, then 3, 2, 1 with the Options' tick, then
  FIGHT, with `versus/fight.wav` if it is there); nobody moves until it ends. A hero at 0 health falls and stays down;
  the other wins the round. The first to the rounds chosen wins the match: the **results** (rounds, hits, damage, biggest hit,
  arrows on target, fastest round, times downed, and two awards the match earned) show for 11 seconds over
  `versus/results_bg.bmp`, then a new match starts.
- **Ranged:** each hero is locked on to the other. 10 shots a round, one a second at most; a shot hits if the other
  is still near where he stood when it was loosed (a step aside dodges it). No ranged attacks on Weathertop and the
  prologue.
- **Pickups:** each round a health pickup in the middle and an ammo pickup on each side (not on Weathertop and the
  prologue); first come, first served.
- **The menu's background:** `versus/menu_bg.bmp` (the Co-op one without it).
- **Devices:** as for co-op (Options > Co-op Options > P1 Device / P2 Device).
- **The P2 Bot** (Co-op Options) fights player 1 in Versus: it walks up and swings, and parries now and then.
- **Sounds and pictures:** the `versus` folder beside the game: `fight.wav` (the horn with FIGHT), `match_win.wav`
  (a match won), `menu_bg.bmp` (the Versus menu's background) and `results_bg.bmp` (the results'); add
  `round_win.wav` for a round won. Replace any of them with your own; any that are missing are left out.

## What the co-op changes

- **A second hero:** a twin of player 1's character, built from player 1's placement record, with its own
  character type, weapons (its hero's weapon ids), ranged weapon and ammo. Its updates run with the game's
  "the player" globals swapped to its own copy, so most of the game works for it unchanged.
- **Placement:** player 2's hero starts beside player 1's on a walkable cell of the level's pathmap, follows cutscenes, and is
  put back beside player 1 after a fall into the void.
- **Combat:** enemies pick either hero as a target; player 2's hits, kills and experience count for player 2.
- **Moves:** player 2's move picks read player 2's own hero's bought upgrades, not player 1's.
- **Checkpoints:** after a game over and Replay, player 2's level experience and per-level stats go back to their
  values at the checkpoint, as the game does for player 1.
- **HUD:** player 2's health, ammo, rings and experience gauge. The rings (each player's combo meter) show only while
  a level plays (they stayed on through the stats screens, shops and menus). The head markers and aim dots are drawn
  under every HUD piece; the boss bar (every boss's) sits under player 2's sword at the top right *(1.9.8)*.
- **The P2 Bot:** a stopgap AI for player 2, to be replaced once there are proper controller settings for player 2:
  - it keeps inside a circle around player 1 and the companions;
  - it melees enemies near it (it walks up and swings only beside them), and shoots from inside the circle at
    enemies further off;
  - it parries now and then;
  - while player 1 is down, it roams and fights;
  - at the Gates of Moria's boss, it attacks a random idle tentacle and parries strikes near it;
  - at the cave troll, it stays at reach, attacks, and parries the troll's swings at it *(1.9.8)*;
  - in Versus it fights player 1.

The code is in `twotowers/src/twotowers_manual.c` (most of it), `menus.c` (the Co-op menu, shops, stats screens),
`heroes.c` (player 2's models, records and weapons), `options.c` (Co-op Options) and `e3hud.c`, plus small changes
in `xboxrecomp-0.12.0/src/kernel/` (the marker depth probe in the Direct3D 11 renderer). The research behind it,
with addresses and evidence:
[the Co-op Anything write-up](https://github.com/cranebeaker/coop-anything/tree/main/games/lotr-two-towers).

## Testing

`tools/coop-bot/` runs the real game with bots and checks its log: see its [README](tools/coop-bot/README.md).
The `TT_COOP_*` environment variables set up test situations (each one is described where the code reads it).

## Known issues

- Head markers are dimmed, not hidden, behind walls (the depth test can't tell a wall from an orc in front of the
  head), and only in the Direct3D 11 renderer (OpenGL, Vulkan, Linux and macOS: never dimmed).
- The P2 Bot's attacks don't land on the Watcher's tentacles in bot runs (neither do player 1's there), so how the
  bot should win that fight is still open.
- An arrow that would kill an enemy whose state ignores the hit leaves it at 1 health with the blow still pending: it
  dies by the game's own death when its state takes the blow, or at the next hit *(1.9.8; before, it was set to 0 and
  could stand up again, unkillable)*.
- Player 2 as Aragorn, Legolas or Gimli moves with the animation machine of a companion of his hero when the level has
  one, else with player 1's: in a level without one (Fangorn Forest, the Westfold) his attacks and shots keep player 1's
  hero's pace.
- Player 2's controller doesn't rumble yet.
- A hobbit guest's marker height is a guess; 4:3 screens are unchecked.
- The bot tests drive player 2 with scripted pads; a second physical controller is not covered by them.
- Co-op is not fully playable with the extra heroes mod. It is recommended to play co op with only the four main heroes. The extra characters are playable, but they do not show up in cut scenes properly yet. Even with player 1 as a main hero and player 2 as an extra hero, the in game cut scenes don't show the proper animations for both hero's. Some HUD elements might not show correctly with the extra heros as well (Sauron as player 2 shows his ammo hud for both players)
- Singleplayer/Co-op: Singleplayer is entirely unaffected by co op or versus mode except for the co op setting for double pickups. If the setting is enabled in the co op setting, it also applies in singleplayer. It is recommended to disable the setting while playing the base game, unless you prefer to have more health pickups.
- Versus spawns are made random for each map, but sometimes a player can still get stuck in a spawn. A "reset game/round" setting has been added to the pause menu for when this occurs. It resets the entire match or the specific round while also putting both players in different spawns/locations.
- Enemies/random NPC's around versus maps don't respawn entirely if a match is reset. If they were killed off during one of the rounds, they will stay that way until returning to the main menu and starting the match again.


## License and credits

MIT, as the port (see [LICENSE](LICENSE)); xboxrecomp's licenses are in `xboxrecomp-0.12.0/`.

- **Co-op: thanks in part to [Volverine-Dude](https://github.com/Volverine-Dude)**, whose
  [TooTawersRecomp-coop](https://github.com/Volverine-Dude/TooTawersRecomp-coop) fork much of it builds on.
- [The Two Towers Recompiled](https://github.com/Gumpolas/TooTawersRecomp) by Gumpolas: the port.
- [xboxrecomp](https://github.com/sp00nznet/xboxrecomp) by sp00nznet: the recompilation toolkit it uses.
- [Co-op Anything](https://github.com/cranebeaker/coop-anything): the playbook this work followed.
- [Athelas](https://github.com/Shagg88/Athelas) by Shagg88: its Two Towers notes and headless test harness were
  references.
- The Lord of the Rings: The Two Towers belongs to its rights holders; nothing from the game is included here.
