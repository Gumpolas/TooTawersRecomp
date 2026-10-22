# Co-op bot tests

`suite.ps1` runs the real game fourteen times with bots: scripted pads for player 1 and player 2, the P2 Bot, and
`TT_COOP_*` test switches that set each situation up (a checkpoint, a downed hero, experience to spend). After each
run it checks the game's log (`twotowers_log.txt`) for pass/fail lines. The game's own frame dumps are kept with
each run's log, to look at by eye.

Windows only (PowerShell 5.1). One run of the whole suite takes about 25 minutes.

## Setting up

1. Build the co-op exe ([COOP.md](../../COOP.md#building)).
2. Make a **test copy** of an installed game folder (the port's install, with `game_files\` and
   `twotowers_recomp.ini`). Never use your own install: the suite starts the game there, changes its settings while
   it runs and writes saves.
3. In the test copy, play until you have a save, then copy its `game_files\UDATA\45410010` folder somewhere: the save
   scenarios restore it before and after they run.
4. Copy `config.example.ps1` to `config.ps1` and set the four paths in it.

The checks were written against one save (US retail, partway through the story). With another save, the menu
scripts' timings and some expected numbers (the experience values, the map's next place) may not match: unknown
until someone tries.

## Running

```powershell
.\suite.ps1                             # all fourteen scenarios
.\suite.ps1 -Only kills_post,checkpoint # some of them
```

It prints PASS or FAIL for each check and a summary line, and writes `<Out>\suite\summary_<time>.txt`. The exit code
is the number of failed checks. Each run's log and frames are in `<Out>\p2char\suite_<name>_*` (runs that start in a
level) or `<Out>\suite_<name>\` (runs that start at the main menu). Don't play the test copy while it runs.

The two runners can also be used on their own:

- `run_p2char_ms.ps1 -Level 04BALIN1 -Hero aragorn -P2 gimli -Pad empty.txt -Seconds 60 -Shots 30,45 -Tag mytest`
  starts straight in a level with player 2 as the given hero; frames at the given seconds.
- `run_menu.ps1 -Script p1_selprobe.txt -Tag mytest -Seconds 90` starts at the main menu with player 1's pad driven
  by a script.

Pad scripts are text files of timed presses:

- player 1's (`p1_*.txt`, `TT_P1SCRIPT`): lines `<from ms> <to ms> <control>`, timed from the launch. Controls: A B X Y
  BLACK WHITE LT RT START BACK UP DOWN LEFT RIGHT, the sticks LUP LDOWN LLEFT LRIGHT RUP RDOWN, and SHOT (a frame
  dump). They drive the menus, and play too with `TT_P1SCRIPT_INPLAY=1`.
- player 2's (`p2_*.txt`, `TT_COOP_PAD2SCRIPT`): lines `<from ms> <to ms> <control> <value>`, timed from when player
  2 appears. Controls: LX LY RX RY (-32767 to 32767), A B X Y BLACK WHITE LT RT (0 to 255), UP DOWN LEFT RIGHT START
  BACK (held).

## Scenarios

| Scenario | What it checks |
|---|---|
| start_place | player 2 loads as Gimli from the save's record, with his own character type, placed beside player 1 on the level's pathmap |
| weapons | player 2 refuses player 1's weapons; his axe gets Gimli's weapon id; his throws are axes from his own copy; his own ammo goes down while player 1's stays; his move picks read his own hero's upgrades |
| weapons_bow | player 2 Isildur shoots from his own copy of a bow, never from player 1 Legolas' bow |
| kills_post | a kill by player 2 pays him experience; every award for his kills runs in his context; player 1 (idle) gets none; player 2's stats screen gets his level's experience; upgrades bought for his hero open his moves, player 1's hero's never do |
| levelup_shops | each player's shop gets that player's level-up and points |
| pickups | player 2's quiver touches refill him, never player 1 |
| down_respawn | player 1 down while player 2 stands: no game over, and player 1 respawns beside player 2 |
| down_gameover | both down: game over |
| menus | the Co-op menu's background; the hero select gives each player his hero; the level starts with player 2's hero |
| camera | the camera zooms out to the Normal cap as the heroes part; both head markers are placed on screen; the markers' depth probe reads a point under the ground as hidden |
| checkpoint | from the Watcher's checkpoint: after a game over and Replay, player 2's level experience and kill counts are back at his checkpoint values with player 1's; the P2 Bot goes near a tentacle and parries; it never shoots there |
| shop_left | left pages the level tabs in both shops and is held back where it would reach the hidden Replay; player 2's stats screen has no Replay |
| save_buy | player 2's shop closes with his experience committed, and the game saves (player 2 buys an upgrade) |
| save_reload | player 2's experience and his whole saved record (checksum, the bought upgrade included) come back after loading |

Every scenario also checks for no crash and a median frame rate of 50 or more. The test copy's ini is restored after
each run, and its save after the save scenarios.

("He" and "his" here are the heroes, Gimli, Legolas and the others, as in the game.)
