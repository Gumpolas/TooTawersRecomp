# tools/regions: language packs from the user's own USA, PAL and PS2 discs

By The128ReBits. These tools are what makes the language code in `src/lang.c` usable: they build the `lang/voz`, `lang/pal-*` and `lang/fmv` folders that the executable reads. They are pure Python standard library (3.11+), with no FFmpeg, numpy or downloads. They only **read** the discs, and never store anything from them outside the chosen output folder. **The output contains game data and must not be redistributed.**

## One command

```
python twotowers/tools/regions/build_lang_packs.py \
    --game  <port folder | its game_files | USA Xbox ISO> \
    --out   <new empty folder> \
    --pal   "<PAL Xbox ISO>"                       (optional) \
    --ps2   es=<ISO> it=<ISO> fr=<ISO> de=<ISO>    (optional) \
    --voices es it fr de  --texts all  [--fmv]
```

Then copy `<out>/lang/*` over the port's `lang/` folder. The launcher and these settings pick the packs up:

| Setting | Values from these packs |
|---|---|
| `TT_VOICE` / `TT_VOICE_CINE` | `es` `it` `fr` `de` (gameplay voices / in-engine cutscene voices, any pair) |
| `TT_LANG` | `pal-es` `pal-de` `pal-fr` `pal-it` `pal-sv` `pal-nl` `pal-en` (official PAL text) |
| `TT_FMV` | `es` `de` `fr` `it` `uk` (localized PAL films) |

**Disk space**: every voice pair folder holds its own copy of the banks it changes, so ES/IT/FR/DE with every gameplay × cutscene pair (24 folders) takes about **12.6 GB** (12,598,374,775 bytes in our test). Choose fewer `--voices` to need less.

`--community` defaults to the port's own `lang/` folder. The `pal-*` packs take the official PAL strings from the disc, and the port's own words (`P:`/`L:` keys, `text.txt`, glyphs) from the community pack of the same language.

## What each source adds

| Source | Required | Adds | Tool |
|---|---|---|---|
| **USA Xbox (NTSC)** | yes; its `default.xbe` must be the USA retail one (`ae2d1b53…8d30`) | ES/IT/FR/DE voices that are already in the USA banks but unused by the USA executable, plus lines found in other banks | `voces_combo.py` (`voces_pack.py`), `voces_usa_huecos.py` |
| **PAL Xbox** | no (`6bcf4cd9…03f4`); the PAL XBE is never run | official PAL text for 7 languages, PAL credits, localized films, a few extra voice lines | `inspect_pal.py` (XDVDFS reader), `pal_lang_pack.py`, `voces_pal_extra.py` |
| **PS2 PAL** | no; one ISO per language, editions in `ps2_disc_catalog.json` | remaining missing voice lines, matched by exact cue name and converted from PS-ADPCM to mono Xbox ADPCM, with no resampling or retiming | `voces_ps2_extra.py`, `probe_ps2_disc.py` |

Voice coverage (catalogue entries out of 155 gameplay / 116 cutscene; English covers the rest):

| Language | USA | + PAL | + PAL + PS2 | Cutscenes |
|---|---|---|---|---|
| Spanish | 127 | 131 | 135 | 69 |
| Italian | 130 | 133 | 135 | 69 |
| French | 128 | 130 | 134 | 69 |
| German | 128 | 131 | 135 | 69 |

## Safety checks

- The output folder must be new or empty, and must not overlap any input.
- The USA and PAL `default.xbe` digests are checked, and PS2 discs are matched against the catalogue.
- The size and modification time of every input file are compared before and after the build.
- Paths read from a disc image are refused if they are absolute or contain `..`.
- The receipts `build_lang_packs.json`, `idiomas_instalados.json`, `VOCES_ADICIONALES_PAL.json` and `VOCES_ADICIONALES_PS2.json` record what was built and from which disc editions.

The steps run in the same order as the R8 installer (`engine.py`). Each tool can also be used alone. Comments and messages are partly in Spanish. PS2 films are not imported. PS2 and PAL voice lines have passed automatic format and bank checks, but have not been listened to in game yet.
