# Front-end bot run: player 1's pad from a script timed from the main menu; frames in smoke\<tag>\, the menu log on.
# Test copy only; closes at the time limit.
param([string]$Script = "p1_to_charsel.txt", [string]$Tag = "cs", [int]$Seconds = 60)
# (extra env for a run: set it before calling, e.g. TT_HEROES=2)
. "$PSScriptRoot\config.ps1"   # $Inst, $Out, $Exe, $SaveBackup: see config.example.ps1
Copy-Item $Exe "$Inst\twotowers_recomp_coop.exe" -Force
New-Item -ItemType Directory -Force "$Out\$Tag" | Out-Null
foreach ($v in "TT_TWEAKS", "TT_COOP", "TT_COOP_PAD2SCRIPT") { Remove-Item "Env:$v" -ErrorAction SilentlyContinue }
$env:TT_P1SCRIPT = "$PSScriptRoot\$Script"; $env:TT_P1SCRIPT_FROMMENU = "1"; $env:TT_P1SCRIPT_SHOTDIR = "$Out\$Tag"; $env:TT_MENULOG = "1"
if (Test-Path "$Inst\twotowers_log.txt") { $ol = "$Out\earlier_logs"; New-Item -ItemType Directory -Force $ol | Out-Null; Copy-Item "$Inst\twotowers_log.txt" (Join-Path $ol ("log_" + (Get-Item "$Inst\twotowers_log.txt").LastWriteTime.ToString("yyyyMMdd_HHmmss") + ".txt")) -Force }   # (the last session's log kept: it may be from someone's play)
Remove-Item "$Inst\twotowers_log.txt" -ErrorAction SilentlyContinue
$p2botset = -not $env:TT_COOP_P2BOT; if ($p2botset) { $env:TT_COOP_P2BOT = "0" }   # (the ini's P2 Bot setting: off in bot runs unless a run sets it)
$p = Start-Process "$Inst\twotowers_recomp_coop.exe" -WorkingDirectory $Inst -PassThru; Start-Sleep $Seconds
if ($p2botset) { Remove-Item Env:TT_COOP_P2BOT }
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
foreach ($v in "TT_P1SCRIPT", "TT_P1SCRIPT_FROMMENU", "TT_P1SCRIPT_SHOTDIR", "TT_MENULOG") { Remove-Item "Env:$v" -ErrorAction SilentlyContinue }
Copy-Item "$Inst\twotowers_log.txt" "$Out\$Tag\log.txt" -Force
Add-Type -AssemblyName System.Drawing
$fs = Get-ChildItem "$Out\$Tag" -Filter "p1s_*.bmp" | Sort-Object { [int]($_.BaseName -replace 'p1s_', '') }
$n = $fs.Count; $rows = [math]::Ceiling($n / 3)
if ($n) { $sh = New-Object System.Drawing.Bitmap 1440, ([int](360 * $rows)); $g = [System.Drawing.Graphics]::FromImage($sh); $k = 0
  foreach ($f in $fs) { $i = [System.Drawing.Image]::FromFile($f.FullName); $g.DrawImage($i, ($k % 3) * 480, [math]::Floor($k / 3) * 360, 480, 360); $g.DrawString($f.BaseName, (New-Object System.Drawing.Font "Arial", 14), [System.Drawing.Brushes]::Yellow, ($k % 3) * 480 + 4, [math]::Floor($k / 3) * 360 + 4); $i.Dispose(); $k++ }
  $sh.Save("$Out\$Tag\sheet.png", [System.Drawing.Imaging.ImageFormat]::Png) }
"$n frames"
