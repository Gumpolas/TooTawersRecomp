ULTRA TEXTURES
==============

Turn on "Ultra textures" in the launcher's Graphics settings or in the
game's Display Options.

What is in this folder (comes with the game):
  ui\          the game's own menus, level select (and its pictures of each
               level), text and HUD, upscaled 4x. These work at once.
  masks\       the game textures' transparency, for pack pictures that
               lost it
  recipe.txt   which pack pictures are remade, and how
  pairs.txt    pairings of game textures and pack pictures checked by eye
  index.bin    the list of pictures (rebuilt with the pack)

The GameCube high-resolution texture pack ("GLOS69") is NOT included: it is
its author's work. Download it from them, and put its GLOS69 folder (the
folder full of tex1_... files) in the game's folder, next to
twotowers_recomp.exe -- not in here.

The first time you press Play with Ultra textures on and GLOS69 in place,
the launcher makes the pack ready (once, a few minutes): png\, bleed\ and
alphafix\ appear here (about 1 GB) and index.bin is rebuilt. The GLOS69
folder itself is not changed. On Linux and macOS, TwoTowers.sh /
TwoTowers.command do this, or run:  ./twotowers-setup ultra
