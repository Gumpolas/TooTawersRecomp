# Languages (1.9.2)

The game's languages are made here and read by `twotowers/src/lang.c`.

| File | What it is |
|---|---|
| `languages.json` | the languages: code, own name, script, right to left |
| `strings/<code>.tsv` | one language's translations: `key <TAB> translation` |
| `build_lang.py` | makes `twotowers/lang/<code>/` from them (`python3 build_lang.py all`) |
| `extract_source.py` | the English of the game's strings, from **your own** copy of the game |

## Keys

- `G<8 hex digits>` — a string of the game's: the FNV-1a hash of its English (UTF-16 units). The game's own text is never stored here; to see the English behind each key, run `python3 extract_source.py <your game folder> > source.tsv` (keep that file to yourself: it is the game's text). Keys that are not in your game's files are the Extra heroes' versions of the upgrade names (`twotowers/src/heroes_lang.inc` pairs them).
- `P:<English>` — the port's own words written in the game's fonts (Options pages, key names, hints).
- `L:<English>` — the port's own words written with the computer's fonts (the launcher, the F10 panel, the ECTS HUD's tip).

In a translation, `\n` is a line break; `{0}`, `{3}`, `{6}`, `{8}`, `{M}` and `{k}` are replaced by key or button names and must stay as they are. Thai needs a zero-width space (U+200B) between words, where a line may break. Hebrew and Arabic are written in logical order (the game turns each line).

## Building

Needs Python 3 with Pillow built with libraqm, and the fonts named in `build_lang.py` (Lora, FreeSerif, Noto Serif CJK). `python3 build_lang.py fr` (or `all`) writes `twotowers/lang/<code>/`:

- `text.txt` and `game.txt` — the translations, in Unicode and in the game's character codes;
- `glyphs.bin` — the letters the game's four fonts lack, at each font's size: accented letters made from the game's own letter and accents, everything else drawn to the font's measures;
- `info.txt` — the language's name and direction.

Copy `twotowers/lang` beside `twotowers_recomp.exe`. The game makes its copies of the game files with the language's text in `lang_cache` (rebuilt when the pack changes).

## Adding a language

Add it to `languages.json` (`script`: `latin` for alphabets written letter by letter -- Latin, Greek, Cyrillic, Hebrew --, `cjk` with a `cjk` font, `thai`, `indic`, `arabic`; `rtl: true` for right to left), write `strings/<code>.tsv` with every key of an existing one, and build.
