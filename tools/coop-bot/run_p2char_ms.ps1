# Player 2 as a given character (TT_COOP_P2CHAR), player 1 a given hero, player 2 walking (p2_walk.txt);
# frames at the given times (from the launch) in smoke\p2char\<tag>_<s>.bmp and a 2x2 sheet. Test copy only.
param([string]$P2 = "legolas", [string]$Hero = "aragorn", [string]$Level = "01WEAT1", [int]$Seconds = 66, [double[]]$Shots = @(50, 54, 58, 62), [string]$Tag = "", [string]$Pad = "p2_walk.txt")
if (-not $Tag) { $Tag = "${Hero}_$P2" }
. "$PSScriptRoot\config.ps1"   # $Inst, $Out, $Exe, $SaveBackup: see config.example.ps1
Copy-Item $Exe "$Inst\twotowers_recomp_coop.exe" -Force
New-Item -ItemType Directory -Force "$Out\p2char" | Out-Null
$env:TT_TWEAKS = "SKIPMAINMENU=1;HERO=$Hero;LEVEL=$Level"; $env:TT_COOP = "clone"; $env:TT_COOP_PAD2SCRIPT = "$PSScriptRoot\$Pad"; $env:TT_COOP_P2CHAR = $P2
$env:TT_SHOTS = ($Shots | ForEach-Object { "$([int]($_ * 1000)):$Out\p2char\${Tag}_$([int]($_ * 1000)).bmp" }) -join ","
if (Test-Path "$Inst\twotowers_log.txt") { $ol = "$Out\earlier_logs"; New-Item -ItemType Directory -Force $ol | Out-Null; Copy-Item "$Inst\twotowers_log.txt" (Join-Path $ol ("log_" + (Get-Item "$Inst\twotowers_log.txt").LastWriteTime.ToString("yyyyMMdd_HHmmss") + ".txt")) -Force }   # (the last session's log kept: it may be from someone's play)
Remove-Item "$Inst\twotowers_log.txt" -ErrorAction SilentlyContinue
$p2botset = -not $env:TT_COOP_P2BOT; if ($p2botset) { $env:TT_COOP_P2BOT = "0" }   # (the ini's P2 Bot setting: off in bot runs unless a run sets it)
$p = Start-Process "$Inst\twotowers_recomp_coop.exe" -WorkingDirectory $Inst -PassThru; Start-Sleep $Seconds
if ($p2botset) { Remove-Item Env:TT_COOP_P2BOT }
$ex = $p.HasExited; if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
foreach ($v in "TT_TWEAKS", "TT_COOP", "TT_COOP_PAD2SCRIPT", "TT_COOP_P2CHAR", "TT_SHOTS") { Remove-Item "Env:$v" -ErrorAction SilentlyContinue }
Start-Sleep 2
Copy-Item "$Inst\twotowers_log.txt" "$Out\p2char\${Tag}_log.txt" -Force
Add-Type -AssemblyName System.Drawing
$sh = New-Object System.Drawing.Bitmap 1280, 960; $g = [System.Drawing.Graphics]::FromImage($sh); $k = 0
foreach ($s in $Shots) { $f = "$Out\p2char\${Tag}_$s.bmp"; if (Test-Path $f) { $i = [System.Drawing.Image]::FromFile($f); $g.DrawImage($i, ($k % 2) * 640, [math]::Floor($k / 2) * 480, 640, 480); $i.Dispose() }; $k++ }
$sh.Save("$Out\p2char\${Tag}_sheet.png", [System.Drawing.Imaging.ImageFormat]::Png)
"${Tag}: exited by itself $ex"
