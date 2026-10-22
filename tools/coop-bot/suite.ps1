# The co-op bot suite: each scenario runs the game (test copy only) with bots and test switches, then its log is
# checked. Prints PASS/FAIL per check and writes <Out>\suite\summary_<time>.txt. Exit code: number of failed checks.
# Usage: .\suite.ps1            (all)      .\suite.ps1 -Only kills_post,menus      (some)
# Restores the test copy's ini after each run and its save after the save scenarios. Don't run it while someone plays the test copy.
param([string[]]$Only = @())
. "$PSScriptRoot\config.ps1"   # $Inst, $Out, $Exe, $SaveBackup: see config.example.ps1
$SaveDir = "$Inst\game_files\UDATA\45410010"   # (the game's save folder, by its title id)
$Bot = $PSScriptRoot
$IniBackup = "$Out\test-install-ini.backup"
New-Item -ItemType Directory -Force "$Out\suite" | Out-Null

function Log-Of($s) { if ($s.Runner -eq "menu") { "$Out\suite_$($s.Name)\log.txt" } else { "$Out\p2char\suite_$($s.Name)_log.txt" } }
function Lines($log, $pat) { if (Test-Path $log) { Select-String -Path $log -Pattern $pat } else { @() } }
$script:Captured = @{}

# a check: Has (pattern must appear), Not (must not appear), Min (at least N matches), Test (scriptblock on the log path)
$Scenarios = @(
  @{ Name = "start_place"; Runner = "p2char"; Args = @{ Level = "04BALIN1"; Hero = "aragorn"; P2 = "gimli"; Pad = "empty.txt"; Seconds = 45; Shots = @(30) }; Env = @{ TT_SKIPMOVIES = "1" };
     Checks = @(
       @{ D = "player 2 is Gimli, from the save's record"; Has = "player 2 is gimli: his record" },
       @{ D = "his own character type (Gimli 8)"; Has = "twin's type \d+ -> 8" },
       @{ D = "placed beside player 1 on the pathmap"; Has = "\[COOP2\] place: pathmap" }) },
  @{ Name = "weapons"; Runner = "p2char"; Args = @{ Level = "02GATE2"; Hero = "legolas"; P2 = "gimli"; Pad = "p2_bowspam.txt"; Seconds = 55; Shots = @(44) }; Env = @{ TT_SKIPMOVIES = "1"; TT_COOP_NEARLOG = "1" };
     Checks = @(
       @{ D = "player 2 refuses player 1's weapons"; Has = "player 2 doesn't take .*weaponObj" },
       @{ D = "his axe gets Gimli's weapon id (0x17)"; Has = "id \w+ -> 17" },
       @{ D = "his throws are axes"; Has = "shoots: projectile (19|16) -> 16" },
       @{ D = "his throws leave from his own ranged weapon with Gimli's throwing-axe model (E7EEC090)"; Has = "shoots: projectile (19|16) -> 16, shooter \w+ model \w+ \(key E7EEC090\)" }   # (16 -> 16 since his own moves: Gimli's throw, not Legolas' arrow move),
       @{ D = "his aiming names his own ranged weapon, not player 1's bow"; Has = "player 2's event names \w+ \(player 1's ranged weapon\): his copy" },
       @{ D = "his own ammo goes down, player 1's stays"; Has = "ammo P1 60/60 P2 ([0-9]|1[0-9]|2[0-4])/25" },
       @{ D = "his move picks ask his own hero's bought upgrades (Gimli's), not player 1's"; Has = "COOPMOVES\] player 2's move pick .* asks his own hero 3's upgrades, not player 1's hero 2" }) },
  @{ Name = "weapons_bow"; Runner = "p2char"; Args = @{ Level = "02GATE2"; Hero = "legolas"; P2 = "isildur"; Pad = "p2_bowspam.txt"; Seconds = 55; Shots = @(45) }; Env = @{ TT_SKIPMOVIES = "1"; TT_COOP_P1PORT = "0" };
     Checks = @(
       @{ D = "player 2 Isildur shoots from his own copy (Aragorn's bow, 36AC3F58)"; Has = "player 2 \(isildur\) shoots: .*\(key 36AC3F58\)" },
       @{ D = "never from player 1 Legolas' bow"; Not = "player 2 \(isildur\) shoots: .*\(key 0834F2D4\)" }) },
  @{ Name = "kills_post"; Runner = "p2char"; Args = @{ Level = "04BALIN1"; Hero = "aragorn"; P2 = "gimli"; Pad = "empty.txt"; Seconds = 200; Shots = @(60) };
     Env = @{ TT_SKIPMOVIES = "1"; TT_COOP_P1PORT = "0"; TT_COOP_P2BOT = "1"; TT_COOP_TESTEND = "120000:1"; TT_COOP_KILLLOG = "1"; TT_P1SCRIPT = "$Bot\p1_postxp.txt"; TT_P1SCRIPT_SHOTDIR = "$Out\suite_kills_post"; TT_COOP_TESTUPG = "3:32,3:36,3:13,3:28,1:32,1:36"; TT_COOP_MOVELOG = "1" };   # (upgrades bought in memory for the run: Gimli's, and two of Aragorn's)
     Checks = @(
       @{ D = "a kill by player 2 pays him experience"; Has = "kill by player 2 \(enemy \w+, the game's award in his context\): \+[1-9]" },
       @{ D = "every award for his kills runs in his context"; Test = { param($log) $p2 = (Lines $log "\[KILLLOG\] award .*\(P2\)").Count; $ok = (Lines $log "the game's award in his context").Count; ($p2 -gt 0 -and $p2 -eq $ok) } },
       @{ D = "player 1 gets no award for a kill he didn't make (player 1 idle)"; Not = "\[KILLLOG\] award .*\(P1\)" },
       @{ D = "player 2's stats screen gets his level's experience"; Has = "player 1's stats done: player 2's .*the level's [1-9]" },
       @{ D = "upgrades his hero bought open his moves"; Has = "COOPMOVES\] player 2 asked: hero 3's upgrade (32|36|13|28) = 1" },
       @{ D = "player 1's hero's upgrades never open player 2's moves"; Not = "COOPMOVES\] player 2 asked: hero 1's upgrade \d+ = \d \(from 09" }) },   # (the move picks are 0x09xxxx; the HUD's display asks via 0xD25B0 are not moves)
  @{ Name = "levelup_shops"; Runner = "p2char"; Args = @{ Level = "04BALIN1"; Hero = "legolas"; P2 = "gimli"; Pad = "empty.txt"; Seconds = 98; Shots = @(30) };
     Env = @{ TT_SKIPMOVIES = "1"; TT_COOP_P1PORT = "0"; TT_COOP_TESTEND = "20000:1"; TT_COOP_TESTEXP1 = "4000"; TT_COOP_TESTEXP = "1500"; TT_P1SCRIPT = "$Bot\p1_lvlup2.txt"; TT_P1SCRIPT_SHOTDIR = "$Out\suite_levelup_shops" };
     Checks = @(
       @{ D = "player 1's shop gets his level-up and points (3875 + 4000)"; Has = "player 1's state put back for his shop \(hero 2: level 1, points to come 7875\)" },
       @{ D = "player 2's shop gets his (3875 + 1500)"; Has = "player 2's state put back for his shop \(hero 3: level 1, points to come 5375\)" }) },
  @{ Name = "pickups"; Runner = "p2char"; Args = @{ Level = "04BALIN1"; Hero = "aragorn"; P2 = "legolas"; Pad = "empty.txt"; Seconds = 200; Shots = @(60) };
     Env = @{ TT_SKIPMOVIES = "1"; TT_COOP_P2BOT = "1"; TT_COOP_TESTAMMO = "3,3"; TT_COOP_PICKLOG = "1" };
     Checks = @(
       @{ D = "player 2's quiver touch refills him (player 1 idle, quivers drop where the P2 Bot fights)"; Test = { param($log) foreach ($m in (Lines $log "\(P2\), in his context \| ammo P1 (\d+) -> (\d+), P2 (\d+) -> (\d+)")) { $g = $m.Matches[0].Groups; if ([int]$g[4].Value -gt [int]$g[3].Value -and $g[1].Value -eq $g[2].Value) { return $true } }; $false } },
       @{ D = "no player 2 touch refills player 1"; Test = { param($log) foreach ($m in (Lines $log "\(P2\).* \| ammo P1 (\d+) -> (\d+),")) { $g = $m.Matches[0].Groups; if ([int]$g[2].Value -gt [int]$g[1].Value) { return $false } }; $true } }) },
  @{ Name = "down_respawn"; Runner = "p2char"; Args = @{ Level = "04BALIN1"; Hero = "aragorn"; P2 = "gimli"; Pad = "empty.txt"; Seconds = 70; Shots = @(30) }; Env = @{ TT_SKIPMOVIES = "1"; TT_COOP_TESTDOWN = "15000,99999999" };
     Checks = @(
       @{ D = "player 1 down while player 2 stands: no game over"; Has = "P1 down \(P2 alive\)" },
       @{ D = "player 1 respawns beside player 2"; Has = "P1 respawned beside P2" },
       @{ D = "no game over"; Not = "game over" }) },
  @{ Name = "down_gameover"; Runner = "p2char"; Args = @{ Level = "04BALIN1"; Hero = "aragorn"; P2 = "gimli"; Pad = "empty.txt"; Seconds = 45; Shots = @(35) }; Env = @{ TT_SKIPMOVIES = "1"; TT_COOP_TESTDOWN = "15000,20000" };
     Checks = @( @{ D = "both down: game over"; Has = "game over" }) },
  @{ Name = "menus"; Runner = "menu"; Args = @{ Script = "p1_selprobe.txt"; Seconds = 75 }; Env = @{ TT_SKIPMOVIES = "1" };
     Checks = @(
       @{ D = "the Co-op menu has its own background"; Has = "COOPMENU\] background: the three hunters" },
       @{ D = "the hero select gives each player his hero"; Has = "P2SELECT\] done: player 1 Aragorn, player 2 Legolas" },
       @{ D = "the level starts with player 2 as Legolas"; Has = "player 2 is legolas: his record" }) },
  @{ Name = "camera"; Runner = "p2char"; Args = @{ Level = "02GATE2"; Hero = "legolas"; P2 = "gimli"; Pad = "p2_away.txt"; Seconds = 56; Shots = @(46) }; Env = @{ TT_SKIPMOVIES = "1"; TT_COOP_CAMLOG = "1"; TT_COOP_TAGLOG = "1000"; TT_COOP_ICONS = "1"; TT_COOP_ZPROBELOG = "1" };
     Checks = @(
       @{ D = "the camera zooms out to the Normal cap (1.30) as they part"; Has = "zoom 1\.30 \(eased 1\.30\)" },
       @{ D = "both head markers are placed on screen"; Test = { param($log) (Lines $log "\[COOPTAG\] P1 head .* -> frame").Count -gt 0 -and (Lines $log "\[COOPTAG\] P2 head .* -> frame").Count -gt 0 } },
       @{ D = "the markers' depth probe works: a point under player 1's feet reads as behind the ground"; Test = { param($log) $r = (Lines $log "\[ZPROBE\] test: .*hidden by the ground \(right\)").Count; $w = (Lines $log "\[ZPROBE\] test: .*NOT hidden").Count; $r -ge 10 -and $w * 10 -le $r } }) },   # (a result is two frames late: one in ten may miss while the camera moves)
  @{ Name = "checkpoint"; Runner = "p2char"; Args = @{ Level = "02GATE2"; Hero = "legolas"; P2 = "gimli"; Pad = "empty.txt"; Seconds = 95; Shots = @(45) };   # (the Watcher's checkpoint: respawn id 1)
     Env = @{ TT_SKIPMOVIES = "1"; TT_COOP_P1PORT = "0"; TT_COOP_P2BOT = "1"; TT_COOP_RESPAWNID = "1"; TT_COOP_TESTXP12 = "500,300,7,3"; TT_COOP_TESTCKPT = "6000"; TT_COOP_TESTXP12B = "800,700,20,20"; TT_COOP_TESTDOWN = "20000,21000"; TT_P1SCRIPT = "$Bot\p1_aspam_slow.txt"; TT_P1SCRIPT_INPLAY = "1" };
     Checks = @(
       @{ D = "a checkpoint reload puts player 2's level experience back to his at the checkpoint, as player 1's (500/300 after 800/700)"; Has = "start of the level: player 1's level experience 500 \(was 800.*player 2's kept, as player 1's \(300\)" },
       @{ D = "and his other per-level stats (the kill counts) back to his at the checkpoint (7, 3 after 20, 20)"; Has = "and his other per-level stats \(record \+0x94\.\.\+0xB3\): \w+ 00000007 00000003" },
       @{ D = "at the Watcher the P2 Bot goes near a tentacle and parries"; Has = "\[P2BOT\] boss: parrying by a tentacle" },
       @{ D = "the P2 Bot never shoots"; Not = "COOPSHOT\] player 2" }) },
  @{ Name = "shop_left"; Runner = "menu"; Save = "backup"; Args = @{ Script = "p1_shopleft.txt"; Seconds = 132 }; Env = @{ TT_SKIPMOVIES = "1"; TT_COOP_TESTEND = "15000:1"; TT_COOP_TESTEXP = "1500" };   # (never reaches the save question)
     Checks = @(
       @{ D = "left on the level tabs pages back, in both players' shops"; Test = { param($log) (Lines $log "\[SHOPLEFT\] .*let through \(the level tabs\)").Count -ge 2 } },
       @{ D = "left from the list and from Continue is held back"; Test = { param($log) (Lines $log "\[SHOPLEFT\] .*held back").Count -ge 2 } },
       @{ D = "the shop never focuses its hidden Replay"; Not = "\[SHOPLEFT\] .*leaf focus 2" },
       @{ D = "player 2's stats screen has no Replay (not drawn, left held back)"; Test = { param($log) (Lines $log "player 2's stats screen: Replay not drawn").Count -gt 0 -and (Lines $log "left on player 2's stats screen: held back").Count -gt 0 } }) },
  @{ Name = "save_buy"; Runner = "menu"; Save = "backup"; Args = @{ Script = "p1_buysave.txt"; Seconds = 165 }; Env = @{ TT_SKIPMOVIES = "1"; TT_COOP_TESTEND = "15000:1"; TT_COOP_TESTEXP = "1500" };
     Checks = @(
       @{ D = "player 2's shop closes with his experience committed (5375)"; Has = "player 2's upgrades done \(experience 5375" },
       @{ D = "his record's checksum at the shop's close"; Test = { param($log) $m = Lines $log "record sum (\w+) \(his shop closed\)" | Select-Object -Last 1; if ($m) { $script:Captured["sum"] = $m.Matches[0].Groups[1].Value; $true } else { $false } } },
       @{ D = "the game saves"; Test = { param($log) (Get-ChildItem "$SaveDir\*\game.xsv" | Where-Object { $_.LastWriteTime -gt (Get-Date).AddMinutes(-5) }).Count -gt 0 } }) },
  @{ Name = "save_reload"; Runner = "menu"; Save = "restore"; Args = @{ Script = "p1_buyprobe3.txt"; Seconds = 125 }; Env = @{ TT_SKIPMOVIES = "1"; TT_COOP_TESTEND = "15000:1" };
     Checks = @(
       @{ D = "player 2's experience comes back from the save (5375)"; Has = "player 2 is legolas: his record from the save .*experience 5375" },
       @{ D = "his whole saved record comes back (checksum, upgrades included)"; Test = { param($log) $m = Lines $log "record sum (\w+) \(loaded" | Select-Object -First 1; $m -and $script:Captured["sum"] -and $m.Matches[0].Groups[1].Value -eq $script:Captured["sum"] } }) }
)

$results = @(); $t0 = Get-Date
foreach ($s in $Scenarios) {
  if ($Only.Count -and $Only -notcontains $s.Name) { continue }
  Write-Host "== $($s.Name)"
  Copy-Item "$Inst\twotowers_recomp.ini" $IniBackup -Force
  if ($s.Save -eq "backup") { Remove-Item $SaveDir -Recurse -Force; Copy-Item $SaveBackup $SaveDir -Recurse }
  if ($s.Env.TT_P1SCRIPT_SHOTDIR) { New-Item -ItemType Directory -Force $s.Env.TT_P1SCRIPT_SHOTDIR | Out-Null }
  foreach ($k in $s.Env.Keys) { Set-Item "Env:$k" $s.Env[$k] }
  $a = $s.Args
  if ($s.Runner -eq "menu") { & "$Bot\run_menu.ps1" -Script $a.Script -Tag "suite_$($s.Name)" -Seconds $a.Seconds | Out-Null }
  else { & "$Bot\run_p2char_ms.ps1" -Level $a.Level -Hero $a.Hero -P2 $a.P2 -Pad $a.Pad -Seconds $a.Seconds -Shots $a.Shots -Tag "suite_$($s.Name)" | Out-Null }
  foreach ($k in $s.Env.Keys) { Remove-Item "Env:$k" -ErrorAction SilentlyContinue }
  Copy-Item $IniBackup "$Inst\twotowers_recomp.ini" -Force
  $log = Log-Of $s
  $checks = @($s.Checks) + @(@{ D = "no crash"; Not = "Access violation" }, @{ D = "frame rate 50+ (median)"; Test = { param($log) $f = @(Lines $log "\[FPS\] (\d+) flips" | ForEach-Object { [int]$_.Matches[0].Groups[1].Value } | Sort-Object); $f.Count -gt 3 -and $f[[int]($f.Count / 2)] -ge 50 } })
  foreach ($c in $checks) {
    $ok = $false
    if (-not (Test-Path $log)) { $ok = $false }
    elseif ($c.Has) { $ok = (Lines $log $c.Has).Count -gt 0 }
    elseif ($c.Not) { $ok = (Lines $log $c.Not).Count -eq 0 }
    elseif ($c.Test) { $ok = [bool](& $c.Test $log) }
    $results += [pscustomobject]@{ Scenario = $s.Name; Check = $c.D; Result = $(if ($ok) { "PASS" } else { "FAIL" }) }
    Write-Host ("  {0}  {1}" -f $(if ($ok) { "PASS" } else { "FAIL" }), $c.D)
  }
  if ($s.Save -eq "restore") { Remove-Item $SaveDir -Recurse -Force; Copy-Item $SaveBackup $SaveDir -Recurse }
}
$fail = @($results | Where-Object Result -eq "FAIL").Count
$sum = "Co-op bot suite, $(Get-Date -Format 'yyyy-MM-dd HH:mm'), $([int]((Get-Date) - $t0).TotalMinutes) min: $($results.Count - $fail) passed, $fail failed of $($results.Count) checks in $(@($results | Select-Object -ExpandProperty Scenario -Unique).Count) scenarios"
$summaryFile = "$Out\suite\summary_$(Get-Date -Format 'yyyyMMdd_HHmm').txt"
($results | Format-Table -AutoSize | Out-String -Width 200) + $sum | Set-Content $summaryFile -Encoding utf8
Write-Host $sum
exit $fail
