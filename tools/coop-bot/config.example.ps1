# Copy this file to config.ps1 (next to it) and set the paths for your PC. The bot scripts read config.ps1.
# Never point $Inst at your own game install: make a separate test copy of the port's install folder and use that.
$Inst = "D:\TwoTowers-test"                     # the test copy: the folder with game_files\ and twotowers_recomp.ini
$Exe = "D:\TooTawersRecomp\build\twotowers_recomp.exe"   # the co-op build; copied into the test copy as twotowers_recomp_coop.exe
$Out = "D:\TwoTowers-bot-out"                   # where logs, frames and suite summaries go
$SaveBackup = "D:\TwoTowers-test-save"          # a copy of the test copy's game_files\UDATA\45410010 folder, holding one
                                                # save: the save scenarios restore it before and after they run (README)
