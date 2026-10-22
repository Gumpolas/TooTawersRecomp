# Couch co-op for The Two Towers Recompiled

Two players on one PC play the story levels together. They share one camera, and each player has their own
hero, controller, weapons, health, ammo, experience and upgrades. There is no networking: one game runs, with a
second hero in it.

Co-op is part of The Two Towers Recompiled from **1.9.6**. It was made by
[Volverine-Dude](https://github.com/Volverine-Dude) in the
[TooTawersRecomp-coop](https://github.com/Volverine-Dude/TooTawersRecomp-coop) fork (on 1.9.4) and brought into the
main release on top of 1.9.5, with player 2's device choice and double pickups added. It is still an **alpha**: see
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
| Camera Zoom | Off, Normal, Wide | The shared camera zooms out as the heroes part, up to 1.3 (Normal) or 1.5 (Wide) |
| P2 Teleport | Off, On | Player 2's hero is put beside player 1's after falling too far behind |
| P2 Bot | Off, On | The game plays player 2 (a stopgap AI, below) |
| Pickups | Normal, Double | Double: a health or ammo pickup can be taken twice, once by each hero (the second at least 1.5 s after the first). Options page only *(1.9.6)* |
| P1 Device, P2 Device | Automatic, Keyboard & Mouse, a controller | Each player's device (above). Options page only *(1.9.6)* |

- **A hero who goes down** comes back beside the other one after the wait. Both down is a game over.
- **After a level:** player 1's stats, player 2's stats, player 1's shop, player 2's shop, then the save. Player 2's
  experience and purchases are saved in their hero's record and come back when the save is loaded.

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
- **HUD:** player 2's health, ammo, rings and experience gauge.
- **The P2 Bot:** a stopgap AI for player 2, to be replaced once there are proper controller settings for player 2:
  - it keeps inside a circle around player 1 and the companions;
  - it melees enemies near it (it walks up and swings only beside them), and shoots from inside the circle at
    enemies further off;
  - it parries now and then;
  - while player 1 is down, it roams and fights;
  - at the Gates of Moria's boss, it parries near a tentacle and attacks one after a parry lands.

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
- The P2 Bot can get stuck at the lake before the Gates of Moria's boss; no bot test reaches that spot.
- Big enemies (the cave troll): whether the P2 Bot gets close enough to hit them is untested.
- A hobbit guest's marker height is a guess; 4:3 screens are unchecked.
- The bot tests drive player 2 with scripted pads; a second physical controller is not covered by them.

## License and credits

MIT, as the port (see [LICENSE](LICENSE)); xboxrecomp's licenses are in `xboxrecomp-0.12.0/`.

- **Co-op by [Volverine-Dude](https://github.com/Volverine-Dude)**, first released in
  [TooTawersRecomp-coop](https://github.com/Volverine-Dude/TooTawersRecomp-coop); everything above is that work unless
  marked 1.9.6.
- [The Two Towers Recompiled](https://github.com/Gumpolas/TooTawersRecomp) by Gumpolas: the port.
- [xboxrecomp](https://github.com/sp00nznet/xboxrecomp) by sp00nznet: the recompilation toolkit it uses.
- [Co-op Anything](https://github.com/cranebeaker/coop-anything): the playbook this work followed.
- [Athelas](https://github.com/Shagg88/Athelas) by Shagg88: its Two Towers notes and headless test harness were
  references.
- The Lord of the Rings: The Two Towers belongs to its rights holders; nothing from the game is included here.
