/* heroes.c -- the Extra Heroes mod (v1.4): Boromir, Gandalf, Frodo and
 * Lurtz as playable characters, next to Aragorn, Legolas, Gimli and Isildur.
 *
 * twotowers_manual.c wraps a few retail functions with tt_hk(id, phase,
 * regs): phase 0 before the original (return nonzero to skip it; regs[0] is
 * then eax and regs[3] esp after the return), phase 1 after. TT_GAMELOG=1
 * (testing) logs the interesting ones.
 *
 * How it works
 * ------------
 * The game builds its hero from a placement record in the level file -- a
 * name ("aragornHero...") and resource keys: mesh (type 100), animation set
 * (150), texture (300) -- and loads the level variant of the chosen hero
 * (.bfa Aragorn, .bfl Legolas, .bfg Gimli, .bfi Isildur).
 *
 * - Character select: the carousel of 3D characters gets seventeen places once
 *   Isildur is unlocked (the final and the secret mission done): the four
 *   retail ones and the thirteen extra heroes. Their models are added to the
 *   menu's file (ui\mainmenu.bff, rebuilt once into heroes\) and four more
 *   characters are created in the menu's character manager (0x49F440, room
 *   for eight); the carousel is drawn by layout() here for eight places, the
 *   same way the game draws three or four.
 * - An extra hero plays as hero 4 (Isildur's records: upgrades, experience,
 *   levels done -- kept per extra hero in heroes\records_v3.dat and swapped in
 *   when one is chosen). In a level, hero 4 opens a rebuilt copy of
 *   Aragorn's variant with the hero's mesh, animations and texture copied in
 *   from the level that has them, and every reference to Aragorn's keys in
 *   the level image points at the hero's (skin_apply). The animation set has
 *   to be the character's own: the skeletons differ.
 *
 * [Mods] extra_heroes=1 turns it on (the launcher's "Extra heroes"). */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "tt_env.h"   /* (1.9.3) getenv looked up once per name */
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include "heroes_records.h"
#include "heroes_size.h"

extern ptrdiff_t xbox_GetMemoryOffset(void);
extern void tt_dbg(const char *fmt, ...);
extern uint32_t tt_gcall(uint32_t va, uint32_t thisptr, int n, const uint32_t *args);
extern uint32_t tt_guest_esp(void);
#define G8(a)  (*(volatile uint8_t *)(xbox_GetMemoryOffset() + (uint32_t)(a)))
#define G16(a) (*(volatile uint16_t *)(xbox_GetMemoryOffset() + (uint32_t)(a)))
#define G32(a) (*(volatile uint32_t *)(xbox_GetMemoryOffset() + (uint32_t)(a)))
#define GF(a)  (*(volatile float *)(xbox_GetMemoryOffset() + (uint32_t)(a)))

static int log_on(void)
{
    static int on = -1;
    if (on < 0) on = getenv("TT_GAMELOG") != NULL;
    return on;
}

static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

/* ===================================================================== */
/* the roster                                                             */
/* ===================================================================== */
/* guest skeleton bone -> Aragorn/Isildur skeleton bone (51 bones), made by
 * tools/rig/chain.py: limbs matched along their chains, the rest following
 * the nearest limb bone above them, or the spine/head by height */
static const uint8_t s_map_boromir[50] = { 2, 2, 2, 2, 5, 4, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 12, 13, 14, 9, 10, 11, 15, 16, 16, 16, 21, 22, 24, 25, 24, 24, 24, 28, 30, 31, 33, 34, 33, 33, 33, 37, 40, 40, 40, 40, 40, 40, 40 };
static const uint8_t s_map_gandalf[66] = { 2, 2, 2, 2, 2, 2, 2, 2, 4, 5, 2, 2, 2, 2, 9, 10, 9, 9, 9, 9, 11, 12, 13, 12, 12, 12, 12, 14, 2, 15, 2, 2, 16, 16, 16, 21, 22, 21, 24, 22, 22, 25, 24, 24, 24, 28, 30, 31, 30, 33, 31, 31, 34, 33, 33, 33, 37, 39, 40, 39, 40, 39, 39, 39, 39, 40 };
/* Frodo onto Gimli's skeleton (43 bones; tools/rig/gimlimap.py): he plays
 * with Gimli's moves, which suit his height */
static const uint8_t s_map_frodo_g[58] = { 3, 3, 3, 3, 13, 14, 16, 16, 16, 13, 13, 13, 3, 3, 3, 13, 3, 3, 3, 13, 3, 3, 3, 17, 18, 19, 20, 19, 19, 19, 19, 20, 24, 25, 26, 27, 30, 27, 27, 27, 27, 30, 31, 33, 33, 33, 4, 5, 3, 3, 7, 8, 8, 9, 10, 11, 11, 12 };
static const uint8_t s_map_frodo[58] = { 2, 2, 2, 2, 15, 16, 16, 16, 15, 15, 15, 2, 2, 2, 2, 2, 2, 2, 2, 15, 2, 2, 2, 21, 22, 24, 25, 24, 24, 24, 24, 25, 28, 30, 31, 33, 34, 33, 33, 33, 33, 34, 37, 40, 40, 39, 4, 5, 2, 2, 9, 10, 10, 11, 12, 13, 13, 14 };
/* Elrond (the prologue's), Sauron (the prologue's), the black Uruk-hai
 * (the Berserker): made by tools/rig automap (nearest hero bone in the bind
 * pose, scaled to height) and, for Elrond, whose attacks are the hero's,
 * refined against the hero's attacks */
static const uint8_t s_map_elrond[44] = { 0, 1, 2, 3, 4, 5, 5, 4, 4, 5, 9, 12, 12, 9, 9, 10, 11, 12, 12, 14, 15, 16, 17, 18, 16, 19, 21, 22, 27, 25, 24, 25, 26, 25, 30, 31, 33, 34, 33, 34, 35, 34, 40, 41 };
static const uint8_t s_map_sauron[65] = { 0, 1, 3, 2, 3, 3, 6, 5, 4, 12, 13, 14, 9, 10, 11, 8, 7, 15, 18, 17, 21, 30, 18, 16, 39, 40, 2, 12, 12, 7, 12, 12, 6, 9, 9, 30, 30, 31, 33, 32, 37, 35, 35, 35, 35, 35, 37, 37, 37, 37, 37, 37, 35, 21, 21, 22, 24, 26, 23, 26, 26, 28, 26, 28, 6 };
static const uint8_t s_map_uruk[61] = { 0, 1, 2, 3, 4, 5, 6, 2, 4, 4, 12, 8, 9, 9, 9, 10, 11, 11, 12, 13, 14, 14, 15, 17, 17, 18, 21, 22, 24, 25, 26, 25, 27, 29, 29, 28, 28, 29, 29, 40, 40, 41, 41, 41, 41, 20, 20, 30, 31, 33, 34, 34, 35, 34, 38, 37, 37, 38, 38, 38, 38 };
/* the Versus-only three (1.9.7; tools/rig automap): the Nazgul (Weathertop's), the prologue's brown shield orc, Eomer
 * (Westfold's captain) */
static const uint8_t s_map_nazgul[70] = { 0,1,2,3,12,12,9,9,10,11,14,13,4,5,9,12,13,10,9,10,11,12,13,14,15,39,17,18,21,22,22,22,24,23,23,23,26,24,24,25,25,25,25,26,28,30,31,31,32,33,32,37,35,32,35,35,35,34,34,40,41,41,15,8,9,9,15,4,12,12 };
static const uint8_t s_map_orc[46] = { 0,1,4,3,5,4,12,13,14,14,9,10,11,11,3,18,15,15,30,31,37,37,37,37,37,37,36,36,21,22,26,28,28,28,28,28,27,27,18,40,18,18,19,15,2,17 };
static const uint8_t s_map_eomer[49] = { 0,1,2,3,5,4,6,4,5,12,9,9,12,8,8,12,13,14,9,10,11,15,15,17,18,21,22,26,28,28,28,28,28,30,31,33,35,35,37,37,37,16,16,17,17,39,19,19,17 };
static const uint8_t s_map_lurtz[78] = { 2, 2, 2, 2, 4, 5, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 9, 10, 11, 12, 13, 14, 15, 16, 16, 16, 21, 22, 24, 24, 24, 25, 24, 28, 24, 24, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 39, 39, 39, 39, 39, 39, 39, 39, 39, 30, 31, 33, 34, 33, 33, 33, 33, 33, 37 };

/* A guest's model is re-rigged onto the heroes' skeleton (Aragorn's, which
 * Isildur shares): the model's bone tables (per part, {skeleton bone, model
 * bone}) are pointed at the matching hero bones, so the guest moves with
 * the hero's own animations -- every move, the weapons in hand, the
 * character select's poses. */
typedef struct {
    const char *name;          /* lower case, file names */
    const char *title;         /* as the game shows it (longer than 7 letters: name_long_set) */
    const char *src;           /* level file that has the resources */
    uint32_t mesh, anim, tex;  /* anim: the guest's own (its skeleton) */
    const uint8_t *map; int nmap;   /* guest bone -> hero bone */
    /* his own weapon, in place of the hero's in that hand (script weapon
     * slot: 0 the bow's hand, 1 the sword's); 0 for none */
    const char *wsrc; uint32_t wmesh, wtex; int wslot;
    /* the level variant he plays (NULL Aragorn's .bfa; ".bfg" Gimli's,
     * moves and all) and his bones for that variant's skeleton */
    const char *lvar; const uint8_t *lmap; int nlmap;
    /* a second weapon of his own, for the sword's hand (slot 1) */
    const char *w2src; uint32_t w2mesh, w2tex;
    /* and a third, for another slot (Boromir's shield) */
    const char *xsrc; uint32_t xmesh, xtex; int xslot;
    /* nothing in the bow's hand (a prop with no triangles, EMPTY_PROP_KEY:
     * Gandalf shoots from his staff), and his shots a beam */
    int nobow, beam;
    int glow;   /* his sword-hand blade lit (Sting's pale blue: its texture tinted) */
} Skin;
#define EMPTY_PROP_KEY 0x6E0B0E00u
#define W(x) "\\Device\\CdRom0\\worlds\\" x
#define MAP(m) m, (int)sizeof m
static const uint8_t s_map_troll[56] = { 2, 2, 2, 2, 5, 4, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 12, 13, 13, 14, 9, 10, 10, 11, 15, 40, 16, 16, 30, 31, 33, 33, 34, 33, 33, 33, 33, 33, 37, 21, 22, 24, 24, 24, 24, 25, 28, 24, 24, 24, 40, 40, 40, 40, 40, 39 };
static const uint8_t s_map_saruman[90] = { 2,2,2,2,4,5,2,2,2,2,9,10,9,9,9,9,9,11,12,13,12,12,12,12,12,14,15,16,16,16,21,22,21,21,21,21,24,22,22,22,22,22,22,24,24,24,25,24,24,24,24,28,24,24,30,31,30,30,30,30,33,31,31,31,31,31,31,34,33,33,33,37,39,39,39,40,40,39,39,39,39,40,40,39,39,39,39,39,39,39 };   /* (1.9.9, tools/rig/chain.py on 21ORTH2) */
static const uint8_t s_map_urukcap[49] = { 2, 2, 2, 2, 4, 5, 2, 2, 2, 2, 2, 2, 9, 10, 11, 12, 13, 14, 15, 16, 16, 16, 21, 22, 24, 24, 24, 25, 24, 28, 24, 24, 40, 40, 40, 40, 40, 39, 39, 30, 31, 33, 34, 33, 33, 33, 33, 33, 37 };
/* (1.9.9) the Ent: Fangorn Forest's Treebeard (TreebeardObj there: mesh 4B2413DE, set F0B6229A, texture 7DED492E -- the
 * placement record's +0x44..+0x4C), 41 bones (tools/rig/chain.py's build against Aragorn's) */
static const uint8_t s_map_ent[41] = { 2, 2, 2, 2, 4, 5, 9, 5, 11, 12, 14, 16, 15, 15, 40, 30, 21, 40, 40, 21, 22, 24, 28, 24, 24, 30, 31, 33, 33, 37, 33, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40 };
static Skin s_skins[] = {
    /* Boromir: his sword, and his shield on his other arm (the bow's hand) */
    { "boromir", "Boromir", W("04BALIN1.bfa"), 0xA6841986u, 0xF9C62B16u, 0xFD8B0956u, MAP(s_map_boromir), NULL, 0, 0, 0,
      NULL, NULL, 0, W("04BALIN1.bfa"), 0xC0F2C17Cu, 0xB798D03Au },   /* his shield: part of his model (shield_mesh, made from the disc) --
      * a weapon in script slot 2 or 3 is drawn in cutscenes alone, and the bow's hand only while he shoots */
    /* Gandalf: a sword in his sword hand (the game has no Glamdring: the
     * ranger's long sword), his staff in his left hand at all times (part
     * of his model, as Boromir's shield), nothing more in the bow's hand;
     * his shots a beam from the staff */
    { "gandalf", "Gandalf", W("04BALIN1.bfa"), 0x3E74698Au, 0xDDAEA31Cu, 0xF2BE2DCCu, MAP(s_map_gandalf), W("04BALIN1.bfa"), 0x13F82514u, 0x29636120u, 1,
      NULL, NULL, 0, NULL, 0, 0, NULL, 0, 0, 0, 1, 1 },
    /* Frodo: Sting (his own, Balin's Tomb); no bow (1.9.1): he throws
     * stones from his empty hand, as the Uruk-hai his burning ones */
    { "frodo",   "Frodo",   W("04BALIN1.bfa"), 0xD7848F26u, 0x4346DB86u, 0xD4263554u, MAP(s_map_frodo), W("04BALIN1.bfa"), 0x69DAC4B8u, 0xBD9E230Bu, 1, ".bfg", MAP(s_map_frodo_g),
      W("04BALIN1.bfa"), 0x69DAC4B8u, 0xBD9E230Bu, NULL, 0, 0, 0, 1, 0, 1 },
    /* Lurtz: his own bow (the Amon Hen boss fight) */
    { "lurtz",   "Lurtz",   W("13AMON1.bfa"),  0x282C7492u, 0xBC42B3A8u, 0x2D8D24C0u, MAP(s_map_lurtz), W("13AMON1.bfa"), 0xFC3884ACu, 0xB5E9F80Au, 0,
      NULL, NULL, 0, W("13AMON1.bfa"), 0xF7C09988u, 0xA2399431u },   /* and his sword (the bow-to-sword cutscene's) */
    /* Elrond: the prologue's, with the elves' curved sword; the hero's attacks fitted to him */
    { "elrond",  "Elrond",  W("00ISUL1.bfa"),  0x1F042846u, 0xAE864136u, 0xDF80247Eu, MAP(s_map_elrond), W("00ISUL1.bfa"), 0xE9C0D2FCu, 0x79C3F38Eu, 1 },
    /* Sauron: the prologue's, his mace, his own swings; a bolt of power for a bow */
    { "sauron",  "Sauron",  W("00ISUL1.bfa"),  0x33B93016u, 0xCD95CCEEu, 0x1748140Cu, MAP(s_map_sauron), W("00ISUL1.bfa"), 0xAD71B240u, 0xB4F6DCDDu, 1,
      NULL, NULL, 0, NULL, 0, 0, NULL, 0, 0, 0, 1, 1 },
    /* the black Uruk-hai: the Berserker (Fangorn, Helm's Deep), his great sword, his own attacks */
    { "uruk",    "Berserker", W("14FANG2.bfa"),  0x0FEDC9FAu, 0x97E4B3C4u, 0xDDF43A28u, MAP(s_map_uruk), W("14FANG2.bfa"), 0x1D32E6CCu, 0x61D9F438u, 1,
      NULL, NULL, 0, NULL, 0, 0, NULL, 0, 0, 0, 1, 0 },   /* (no bow of his own: nothing in that hand) */
    /* (1.9.9) Saruman, in the character select after Sauron: the Orthanc Summit's (21ORTH2: his model of two parts, his 90-bone skeleton, his texture
     * EF4942FD -- the lookups his creation makes there, TT_LKLOG), his black staff (30028EA4); his shots a beam */
    { "saruman", "Saruman", W("21ORTH2.bfa"), 0x8F3B37CAu, 0x56D8D87Cu, 0xEF4942FDu, MAP(s_map_saruman), W("21ORTH2.bfa"), 0x30028EA4u, 0xAEBFF748u, 1,
      NULL, NULL, 0, NULL, 0, 0, NULL, 0, 0, 0, 1, 1 },
    { "urukcap", "Captain", W("18HOLE1.BFA"), 0x99F5D594u, 0xE6F0C52Bu, 0xF5E0EF8Eu, MAP(s_map_urukcap), W("14FANG2.bfa"), 0x1D32E6CCu, 0x61D9F438u, 1 },
    /* (132) the cave troll (Balin's Tomb): his club; he throws (heroes_troll_*.h) */
    { "troll", "Troll", W("04BALIN1.BFA"), 0x712933FEu, 0x29BDD2CAu, 0x964E271Eu, MAP(s_map_troll), W("04BALIN1.bfa"), 0x830B7594u, 0xC941444Eu, 1,
      NULL, NULL, 0, NULL, 0, 0, NULL, 0, 0, 0, 1, 0 },
    /* (1.9.9) Versus only: the Deeping Wall's elven warrior (The128ReBits), 44 bones checked against Elrond's */
    { "elvenwarrior", "Elf", W("17TOP1.bfa"), 0x22C529AAu, 0xB427C34Cu, 0x7064A295u, MAP(s_map_elrond), W("00ISUL1.bfa"), 0xE9C0D2FCu, 0x79C3F38Eu, 1 },
    /* Versus only (1.9.7): not in the character select, the campaign or co-op. The Nazgul: Weathertop's, his sword
     * (weaponObj1-5 there, on the Ringwraiths RW1-5); he throws, as the Uruk-hai */
    { "nazgul",  "Nazgul",  W("01WEAT1.bfa"),  0xAEF3DB04u, 0x066DCD53u, 0xAE89108Eu, MAP(s_map_nazgul), W("01WEAT1.bfa"), 0xC0F2C17Cu, 0xB798D03Au, 1,
      NULL, NULL, 0, NULL, 0, 0, NULL, 0, 0, 0, 1, 0 },
    /* the shield orc: the prologue's brown orc (OrcMel5), his blade (weaponObj239 on orcShieldObj36: id 8) and his
     * shield on his left arm (weaponObj240: id 7, 100 health), part of his model as Boromir's */
    { "orc",     "Orc",     W("00ISUL1.bfa"),  0x2D965DE8u, 0xA1D1A7E9u, 0x3E31996Au, MAP(s_map_orc), W("00ISUL1.bfa"), 0x075091FCu, 0xEC17650Cu, 1,
      NULL, NULL, 0, NULL, 0, 0, NULL, 0, 0, 0, 1, 0 },
    /* Eomer: Westfold's Rohirrim captain (rCaptainObj) */
    { "eomer",   "Eomer",   W("16ROHAN1.bfa"), 0x5654A508u, 0x817EFC59u, 0x4058E474u, MAP(s_map_eomer), W("16ROHAN1.bfa"), 0xC0F2C17Cu, 0xB798D03Au, 1,
      NULL, NULL, 0, NULL, 0, 0, NULL, 0, 0, 0, 1, 0 },   /* (the Rohirrim's sword, weaponObj there; he throws) */
    /* (1.9.9) the other troll: Fangorn Forest's (trollObj there), lighter skinned -- player 2 when both players are the
     * cave troll (Troll Battle), so they are not the same troll. His model (AC0EA11A) and texture (F6036F71) on the cave
     * troll's moves: the same skeleton with its bones in another order (s_map_fang2cave), so he plays the cave troll's
     * own set (the same key: player 1's) and fights as he does. TT_NOTROLL2: a copy, as before */
    { "troll2", "Troll", W("14FANG2.bfa"), 0xAC0EA11Au, 0x29BDD2CAu, 0xF6036F71u, MAP(s_map_troll), W("04BALIN1.bfa"), 0x830B7594u, 0xC941444Eu, 1,
      NULL, NULL, 0, NULL, 0, 0, NULL, 0, 0, 0, 1, 0 },
    /* (1.9.9) Versus only: the Ent (Treebeard), bare-handed -- his "weapons" (the cave troll's club) props with nothing
     * drawn (skin_weapon); his own stance and walk, the hero's swings and falls fitted to him; he throws, as the Uruk-hai */
    { "ent", "Ent", W("14FANG2.bfa"), 0x4B2413DEu, 0xF0B6229Au, 0x7DED492Eu, MAP(s_map_ent), W("04BALIN1.bfa"), 0x830B7594u, 0xC941444Eu, 1,
      NULL, NULL, 0, NULL, 0, 0, NULL, 0, 0, 0, 1, 0 },
    /* co-op only (player 2, the character select): the retail heroes from their own level variants -- the hero
     * record's mesh / animation set / texture (the same keys in every level, local/re/hero_keys.py) and the weapon
     * the level script attaches to them for the sword's hand (@AttachToBone, observed in Balin's Tomb). Their
     * animation sets are whole hero sets: their own skeleton, nothing composed (skin_retail) */
    { "aragorn", "Aragorn", W("04BALIN1.bfa"), 0xE9B808FEu, 0xDE94124Au, 0x66829AC2u, NULL, 0, NULL, 0, 0, 0,
      NULL, NULL, 0, NULL, 0, 0, W("04BALIN1.bfa"), 0x36AC3F58u, 0xD82C5CABu, 0 },   /* his bow (co-op: his ranged weapon's model in another
      * hero's level, as Legolas'; its texture as logged for weaponObj148's copy in Gates of Moria) */
    { "legolas", "Legolas", W("04BALIN1.bfl"), 0x9EDC669Eu, 0xEE4A9EBAu, 0x58C546ADu, NULL, 0, W("04BALIN1.bfl"), 0xEF8EBCECu, 0x9126E92Au, 1,   /* his knife */
      NULL, NULL, 0, NULL, 0, 0, W("04BALIN1.bfl"), 0x0834F2D4u, 0x4167DADCu, 0 },   /* and his bow (co-op: his ranged weapon's model, brought into
      * another hero's level: player 2's shots are drawn with it -- twotowers_manual.c, sub_00085FB0) */
    { "gimli",   "Gimli",   W("04BALIN1.bfg"), 0x71EB3C66u, 0xAAE0DF66u, 0xF35ADE70u, NULL, 0, W("04BALIN1.bfg"), 0x2B9252D0u, 0xAB926BE7u, 1,
      NULL, NULL, 0, NULL, 0, 0, W("04BALIN1.bfg"), 0xE7EEC090u, 0xAB926BE7u, 0, 1, 0 },   /* his axe; no bow in his hand (he throws: his throwing
      * axe, E7EEC090, imported as Legolas' bow above -- observed as the ranged weapon object Gimli takes, TT_COOP_TAKELOG) */
    { "isildur", "Isildur", W("04BALIN1.bfi"), 0x37EA8A4Eu, 0x53DFD442u, 0xCD25E3B2u, NULL, 0, W("04BALIN1.bfi"), 0x13F82514u, 0x29636120u, 1,   /* his sword */
      NULL, NULL, 0, NULL, 0, 0, W("04BALIN1.bfa"), 0x36AC3F58u, 0xD82C5CABu, 0 },   /* (co-op) Aragorn's bow for his shots as player 2: he has no
      * ranged weapon of his own (as player 1 he takes none, TT_COOP_TAKELOG), and without a copy his shots left from
      * player 1's bow */
    { "custom",  "Custom",  NULL, 0, 0, 0, NULL, 0, NULL, 0, 0, 0 },   /* testing: TT_PLAYAS=MESH,ANIM,TEX,LEVEL */
};
#define MENU_ANIM_KEY 1u
#define UPG_FIG_KEY 9u   /* the upgrade screen's 3D figure (upgbigi.BFF: mesh 1050 / texture 1150 key 9) */
#define MENU_SWORD_KEY 2u      /* Aragorn's sword in the character select (types 1200/1250) */       /* the character select's Aragorn: the guests' moves there */
#define N_SEL 7                /* in the carousel (the character select, its menus, voices there, unlocks, records): the seven guests (1.9.9: Saruman is Versus-only again, a playtest) */
#define N_EXTRA 16             /* (1.9.9: 15 with the other troll, player 2's in Troll Battle; 16 with the Ent) */ /* the guests: those and Versus' own seven (1.9.9: the cave troll, the Uruk captain, Saruman, the elven warrior; the Nazgul, the shield orc, Eomer) */
#define SK_TROLL 9             /* the cave troll's skin (heroes_weapon_profile.h and the build's troll patches name 9 too) */
/* (1.9.9) the cave troll's shots: Fangorn's trolls' log -- projectile 8, drawn (the level's dynamicProjectileObj, entry 8)
 * with prop 899C9984, the same log in every level, which only Fangorn Forest textures as bark (545EE9C8); elsewhere it
 * wore a rock's. Levels built with him get the bark texture and entry 8 wears it. TT_TROLLROCK: the rock as before */
#define SK_TROLL2 14            /* the other (Fangorn) troll: player 2 beside a cave troll */
#define TROLL_LOG_PROJ 8
#define TROLL_LOG_PROP 0x899C9984u
#define TROLL_LOG_TEX  0x545EE9C8u
#define N_SKINS ((int)(sizeof s_skins / sizeof s_skins[0]))
/* a retail hero's skin (co-op's player 2) */
static int skin_retail(int sk) { return sk >= N_EXTRA && sk < N_SKINS - 1; }
static const int s_retail_hero[4] = { 1, 2, 3, 4 };   /* aragorn, legolas, gimli, isildur: the hero byte */
#define FE_KEY(g) (0x21u + (uint32_t)(g))   /* their models' keys in the menu file */
#define FE_WKEY(g) (0x31u + (uint32_t)(g))  /* their sword-hand weapons there (prop 1200, texture 1250) */
#define PROP_MAGIC 0x62787270u               /* "prxb" */
#define HERO_BYTE 0x4AB69Du
/* the keys the player's guest model, texture and moves go in under in a
 * level (not the guest's own: the level's own copy of him keeps those) */
#define PLAYER_KEY(k) (getenv("TT_SAMEKEYS") ? (uint32_t)(k) : ((uint32_t)(k) ^ 0x3C5A0000u) + 0x00007E57u)
#define HEROES_BUILD 151u     /* bump when the level rebuild changes (1.9.9: Saruman, the troll's throw; the roster without the archer and the Rohan captain) */

/* skin sk's own weapon for script weapon slot (0 bow's hand, 1 sword's), or 0 */
static int skin_weapon(int sk, int slot, const char **src, uint32_t *mesh, uint32_t *tex)
{
    if (sk < 0 || getenv("TT_NOWEAPON")) return 0;
    if ((slot == 0 || slot == 1) && !strcmp(s_skins[sk].name, "ent") && !getenv("TT_ENTCLUB")) { *src = s_skins[sk].wsrc; *mesh = EMPTY_PROP_KEY; *tex = s_skins[sk].wtex; return 1; }   /* (1.9.9) bare-handed */
    if (slot == 0 && s_skins[sk].nobow && s_skins[sk].wmesh && !getenv("TT_BOW")) { *src = s_skins[sk].wsrc; *mesh = EMPTY_PROP_KEY; *tex = s_skins[sk].wtex; return 1; }
    if (s_skins[sk].wmesh && (getenv("TT_WSLOT") ? atoi(getenv("TT_WSLOT")) : s_skins[sk].wslot) == slot) { *src = s_skins[sk].wsrc; *mesh = s_skins[sk].wmesh; *tex = s_skins[sk].wtex; return 1; }
    if (slot == 1 && getenv("TT_W2")) {   /* testing: LEVEL,MESH,TEX */
        static char lv[64], path[128]; static uint32_t m2, t2;
        if (!m2) { sscanf(getenv("TT_W2"), "%63[^,],%x,%x", lv, &m2, &t2); snprintf(path, sizeof path, "\\Device\\CdRom0\\worlds\\%s", lv); }
        *src = path; *mesh = m2; *tex = t2; return 1;
    }
    if (s_skins[sk].w2mesh && slot == 1) { *src = s_skins[sk].w2src; *mesh = s_skins[sk].w2mesh; *tex = s_skins[sk].w2tex; return 1; }
    return 0;
}
/* Aragorn's arrow (4586A62C): his draw hand holds it as he shoots, while his
 * sword stands in the ground (13F82514, another record). It stays an arrow --
 * as his sword it hung in the guest's raised draw hand -- but for a guest
 * who shoots from his own weapon (Gandalf: his staff stays in his hand) */
static int arrow_is_his(int sk) { return sk >= 0 && s_skins[sk].nobow && !getenv("TT_ARROWSWORD") ? 1 : getenv("TT_ARROWSWORD") != NULL; }
/* a weapon's second texture (the weapon op looks up two for some: Sting's
 * glow, as Frodo carries it in Balin's Tomb), or 0 */
static uint32_t weapon_tex2(uint32_t mesh) { return mesh == 0x69DAC4B8u ? 0xDB3B3452u : 0; }
static char s_ini[600], s_dir[600];
static int s_on = -1;
static int s_test_unlock;   /* [Mods] extra_heroes=2 */
static int s_heroes_mode;
static int s_guest = -1;       /* the extra hero hero 4 is, or -1 (Isildur) */
/* (1.9.7, Versus) Mount Doom (the prologue, Isildur's level) with player 1 a retail hero: hero 4 there, his skin built
 * into the level as a guest's is (the model, moves and weapons), without making him the guest anywhere else */
static int s_vsp1 = -1;
extern int tt_vs_on(void);
static int lvg(void) { return s_guest >= 0 ? s_guest : (s_vsp1 >= 0 && tt_vs_on()) ? s_vsp1 : -1; }
#define LVG lvg()
static int s_at_watcher;       /* the level is the Gates of Moria's (02GATE*) */
static uint32_t s_frodo_pid;   /* Frodo's shot as it was, at the Watcher (0: none) */
static int s_playas = -2;      /* testing: TT_PLAYAS skins hero 1 */
static char s_custom_src[96];

static void paths(void)
{
    char *sl;
    if (s_dir[0]) return;
    GetModuleFileNameA(NULL, s_dir, sizeof s_dir);
    sl = strrchr(s_dir, '\\'); if (!sl) sl = strrchr(s_dir, '/');
    if (sl) *sl = 0;
    snprintf(s_ini, sizeof s_ini, "%s/twotowers_recomp.ini", s_dir);
}

int tt_heroes_on(void)
{
    if (s_on < 0) {
        const char *e = getenv("TT_HEROES");
        paths();
        s_on = GetPrivateProfileIntA("Mods", "extra_heroes", 0, s_ini);
        if (e) s_on = atoi(e);
        /* 2: unlocked for testing (only in memory; the save is not changed) */
        if (s_on == 2 || getenv("TT_HEROES_UNLOCK")) s_test_unlock = 1;
        s_heroes_mode = s_on;
        s_on = s_on != 0;
        tt_dbg("[HEROES] extra heroes %s\n", s_on ? "on" : "off");
    }
    return s_on;
}

static int skin_by_name(const char *e)
{
    int i;
    for (i = 0; e && i < N_SKINS; i++)
        if (!_stricmp(e, s_skins[i].name)) return i;
    return -1;
}
/* (1.9.9) the ranged weapon a character shows as player 2 (co-op and Versus), as he does as player 1: the retail heroes their
 * own (Legolas' bow, Gimli's throwing axe, Aragorn's bow; Isildur Aragorn's), a guest with a bow of his own in the bow's hand
 * (Lurtz) none more, a guest who shoots from an empty hand or a staff (nobow) none, any other guest (Boromir, Elrond, the
 * captain) Aragorn's bow -- what he draws in the campaign, as Aragorn's level variant. 0: none */
static int skin_p2_ranged(int sk, const char **src, uint32_t *m, uint32_t *t)
{
    if (sk < 0 || sk >= N_SKINS || getenv("TT_COOP_NOP2RANGED")) return 0;
    if (skin_retail(sk)) { if (!s_skins[sk].xmesh) return 0; *src = s_skins[sk].xsrc; *m = s_skins[sk].xmesh; *t = s_skins[sk].xtex; return 1; }
    if (s_skins[sk].nobow || sk == SK_TROLL || sk == SK_TROLL2) return 0;
    if (s_skins[sk].wmesh && s_skins[sk].wslot == 0) return 0;   /* (his own bow in that hand at all times already) */
    *src = W("04BALIN1.bfa"); *m = 0x36AC3F58u; *t = 0xD82C5CABu; return 1;
}
int tt_heroes_p2_ranged(const char *who, uint32_t *m, uint32_t *t) { const char *s; return skin_p2_ranged(skin_by_name(who), &s, m, t); }
/* co-op (player 2's character, tt_coop_p2_char, set per level in redirect): the level is built with the skin's model, texture and weapons as for TT_PLAYAS,
 * but the hero's record keeps player 1's; the (hero key -> skin key) pairs are handed to the co-op code
 * (twotowers_manual.c), which builds player 2 and his weapons from records using them */
static int s_coop_skin;
uint32_t tt_coop_skin_pairs[8][2]; int tt_coop_skin_n;
static int playas(void)
{
    if (s_playas == -2) {
        const char *e = getenv("TT_PLAYAS");
        unsigned m, a, t;
        char lv[32];
        if (e && sscanf(e, "%x,%x,%x,%31s", &m, &a, &t, lv) == 4) {
            Skin *c = &s_skins[N_SKINS - 1];
            c->mesh = m; c->anim = a; c->tex = t;
            snprintf(s_custom_src, sizeof s_custom_src, "\\Device\\CdRom0\\worlds\\%s", lv);
            c->src = s_custom_src;
            s_playas = N_SKINS - 1;
            tt_dbg("[HEROES] testing: playing as %08X %08X %08X from %s\n", m, a, t, s_custom_src);
        } else s_playas = skin_by_name(e);
    }
    return s_playas;
}
/* the skin of the hero being played, or -1 */
static int active_skin(void)
{
    if (s_coop_skin && s_playas >= 0 && !(G8(HERO_BYTE) == 4 && LVG >= 0)) return s_playas;   /* co-op: player 2's, on any hero's variant */
    if (G8(HERO_BYTE) == 4 && LVG >= 0) return LVG;
    if (G8(HERO_BYTE) == 1 && playas() >= 0) return s_playas;
    return -1;
}

/* Isildur (and with him the extra heroes) unlocked: the final mission
 * (level 11) and the secret one (13) done, by any hero -- the game's own
 * test (0x00073960 and the checks around it) */
static int unlocked(void)
{
    int h, a = 0, b = 0;
    if (tt_heroes_on() && (s_test_unlock || s_heroes_mode == 3)) return 1;
    for (h = 1; h <= 3; h++) {
        if (G8(0x4AB68Fu + h * 15 + 0xB) == 1) a = 1;
        if (G8(0x4AB68Fu + h * 15 + 0xD) == 1) b = 1;
    }
    return a && b;
}

/* ===================================================================== */
/* files                                                                  */
/* ===================================================================== */
#include "../../xboxrecomp-0.12.0/src/kernel/kernel.h"
extern const char *(*xbox_path_redirect)(const char *xbox_path);

static unsigned char *read_all(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    unsigned char *b;
    long sz;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    b = (unsigned char *)malloc((size_t)sz + 1);
    if (b && fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); b = NULL; }
    fclose(f);
    if (b) *n = (size_t)sz;
    return b;
}
static uint32_t rd32(const unsigned char *b, size_t o) { return (uint32_t)b[o] | (uint32_t)b[o + 1] << 8 | (uint32_t)b[o + 2] << 16 | (uint32_t)b[o + 3] << 24; }
static uint32_t rd16(const unsigned char *b, size_t o) { return (uint32_t)b[o] | (uint32_t)b[o + 1] << 8; }
static void wr16(unsigned char *b, size_t o, uint16_t v) { b[o] = (unsigned char)v; b[o + 1] = (unsigned char)(v >> 8); }
static void wr32(unsigned char *b, size_t o, uint32_t v) { b[o] = (unsigned char)v; b[o + 1] = (unsigned char)(v >> 8); b[o + 2] = (unsigned char)(v >> 16); b[o + 3] = (unsigned char)(v >> 24); }

/* the host path of an Xbox path, as the game would open it */
static int host_path(const char *xpath, char *out, size_t n)
{
    xbox_host_char h[600];
    const char *(*save)(const char *) = xbox_path_redirect;
    BOOL ok;
    xbox_path_redirect = NULL;
    ok = xbox_translate_path(xpath, h, 600);
    xbox_path_redirect = save;
    if (!ok) return 0;
#ifdef _WIN32
    WideCharToMultiByte(CP_UTF8, 0, h, -1, out, (int)n, NULL, NULL);
#else
    snprintf(out, n, "%s", h);
#endif
    return 1;
}

/* level/UI file: [0] version 9, [4] n = offset of the directory; at n:
 * u32 count, then count x {type, offset, key} -- file offsets */
static int res_find(const unsigned char *b, size_t len, uint32_t key, int type, uint32_t *off, uint32_t *end, uint32_t *typ)
{
    uint32_t n = rd32(b, 4), cnt, i, best;
    int found = 0;
    if (n + 4 > len) return 0;
    cnt = rd32(b, n);
    if (n + 4 + (size_t)cnt * 12 > len) return 0;
    best = n;
    for (i = 0; i < cnt; i++)
        if (rd32(b, n + 4 + 12 * i + 8) == key && (type < 0 || rd32(b, n + 4 + 12 * i) == (uint32_t)type)) {
            *typ = rd32(b, n + 4 + 12 * i); *off = rd32(b, n + 4 + 12 * i + 4); found = 1;
        }
    if (!found) return 0;
    for (i = 0; i < cnt; i++) {
        uint32_t o = rd32(b, n + 4 + 12 * i + 4);
        if (o > *off && o < best) best = o;
    }
    *end = best;
    return 1;
}

/* copy resource src_key of level file src (an Xbox path) in as {type, key} */
typedef struct { const char *src; uint32_t src_key; uint32_t type, key; const uint8_t *map; int nmap; int compose; int empty; int tint; } Add;

/* After re-rigging, several of a part's bone slots hold the same hero
 * bone. The part's vertices (48 bytes: position, normal, uv, then two
 * {slot * 3, weight} floats; the first at +4 from the part's vertex offset
 * [part+8], [part+0] of them) are pointed at the first slot of each bone,
 * and the duplicates load the same matrix into that slot -- so the model
 * uses only as many of the GPU's matrix constants as it has distinct bones
 * (Gandalf's 56 slots spilled into constants Fangorn's scenery uses). */
/* The unused index of single-bone vertices holds leftovers from the
 * exporter (or the slot from before merging): made the vertex's own other
 * index, so no address the vertex program forms can reach past the palette. */
static void sanitize_zero_w(unsigned char *m, uint32_t vo, uint32_t vc)
{
    uint32_t i;
    for (i = 0; i < vc; i++) {
        float f0, w0, f1, w1, z = 0;
        unsigned char *v = m + vo + 4 + i * 48 + 32;
        memcpy(&f0, v, 4); memcpy(&w0, v + 4, 4); memcpy(&f1, v + 8, 4); memcpy(&w1, v + 12, 4);
        if (w1 <= 0 && w0 > 0) { memcpy(v + 8, &f0, 4); memcpy(v + 12, &z, 4); }
        else if (w0 <= 0 && w1 > 0) { memcpy(v, &f1, 4); memcpy(v + 4, &z, 4); }
        else if (w0 <= 0 && w1 <= 0) { memcpy(v, &z, 4); memcpy(v + 8, &z, 4); }
    }
}

/* The parts share one vertex stream (the first part's): each vertex's
 * palette indices are into the table of the part whose indices use it
 * (own[v], from the parts' strips). */
static uint32_t merge_slots(unsigned char *m, size_t len, uint32_t p, uint32_t c,
                            uint32_t vo, uint32_t vc, const unsigned char *own, unsigned part, uint32_t reserve)
{
    uint32_t i, k, used = reserve;
    unsigned char first[256], newi[256];
    if (c == 0 || c > 255 || vo + 4 + (size_t)vc * 48 > len) return c;
    /* only for the layout described: every weighted index a slot * 3 */
    for (i = 0; i < vc; i++) {
        if (own[i] != part) continue;
        for (k = 0; k < 2; k++) {
            float f, w;
            memcpy(&f, m + vo + 4 + i * 48 + 32 + 8 * k, 4);
            memcpy(&w, m + vo + 4 + i * 48 + 36 + 8 * k, 4);
            if (w > 0 && (f < 0 || f != (float)(int)f || (int)f % 3 || (uint32_t)f / 3 >= c)) return c;
        }
    }
    memset(first, 0xFF, sizeof first);
    for (i = 0; i < c; i++) {
        unsigned char b = m[p + 0x24 + 2 * i];
        if (first[b] == 0xFF) first[b] = (unsigned char)used++;
        newi[i] = first[b];
    }
    if (used >= c) {   /* nothing to gain: as it is */
        for (i = 0; i < vc; i++) if (own[i] == part) sanitize_zero_w(m, vo + i * 48, 1);
        return c;
    }
    for (i = 0; i < vc; i++) {
        if (own[i] != part) continue;
        for (k = 0; k < 2; k++) {
            float f, w;
            memcpy(&f, m + vo + 4 + i * 48 + 32 + 8 * k, 4);
            memcpy(&w, m + vo + 4 + i * 48 + 36 + 8 * k, 4);
            if (w > 0) { f = (float)(newi[(uint32_t)f / 3] * 3); memcpy(m + vo + 4 + i * 48 + 32 + 8 * k, &f, 4); }
        }
        sanitize_zero_w(m, vo + i * 48, 1);
    }
    /* the table itself: one entry per bone (the game's reflection pass, at
     * least, will not draw a model of more than some 50 slots) */
    {
        unsigned char tab[512];
        uint32_t u = 0;
        /* the first slots stay for the later parts: their own (shorter)
         * palettes are loaded over these, while their strips also use the
         * first part's (the hat's tip on Gandalf's second part) */
        for (; u < reserve; u++) { tab[2 * u] = m[p + 0x24]; tab[2 * u + 1] = (unsigned char)u; }
        for (i = 0; i < c; i++)
            if (newi[i] == u) { tab[2 * u] = m[p + 0x24 + 2 * i]; tab[2 * u + 1] = (unsigned char)u; u++; }
        memcpy(m + p + 0x24, tab, 2 * used);
    }
    tt_dbg("[HEROES]   part %u: %u bone slots merged into %u\n", part, c, used);
    return used;
}

/* a model's bone tables: [0x2C] parts; from 0x30 each part has 0x20 bytes,
 * u32 count, count x {skeleton bone, model bone}, padded to 4 */
static int s_native;   /* the level's guest is on his own skeleton */
static uint8_t s_idmap[256];   /* the identity: a guest on his own bones */
/* (1.9.9) the Fangorn troll's bones -> the cave troll's (the inverse of heroes_troll_throw.h's table; his three bones the
 * cave troll lacks go to their parent's) */
static const uint8_t s_map_fang2cave[56] = { 0, 1, 2, 3, 1, 1, 1, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 20, 21, 22, 24, 25, 26,
    27, 28, 29, 30, 32, 33, 34, 31, 35, 36, 37, 38, 39, 40, 41, 43, 44, 45, 42, 46, 47, 48, 49, 50, 51, 52, 53, 54 };
static int s_remap_noshadow;   /* set by the caller: blank the shadow data anyway */
static void remap_bones(unsigned char *m, size_t len, const uint8_t *map, int nmap)
{
    uint32_t parts, p = 0x30, k, i, c, n = 0;
    if (len < 0x60 || memcmp(m, " XBG", 4)) return;
    if (getenv("TT_MSCALE")) {   /* testing: scale part 1's positions */
        float sc = (float)atof(getenv("TT_MSCALE"));
        uint32_t pp = 0x30, vc = rd32(m, pp), vo = rd32(m, pp + 8), j, a;
        for (j = 0; j < vc && vo + 4 + j * 48 + 12 <= len; j++)
            for (a = 0; a < 3; a++) { float f; memcpy(&f, m + vo + 4 + j * 48 + 4 * a, 4); f *= sc; memcpy(m + vo + 4 + j * 48 + 4 * a, &f, 4); }
    }
    if (getenv("TT_MFLAG")) wr32(m, 0x14, (uint32_t)atoi(getenv("TT_MFLAG")));   /* testing */
    {   /* Frodo's model (Balin's Tomb's, made for the cutscenes' close-ups)
         * has its shine on, white and strong (header +0x14 on, +0x18..0x20
         * its colour, +0x24 its strength 6 -- Aragorn's 1.2): played, he
         * looked wet or metallic on the select and in play. Made matte, as
         * the other guests' models are. */
        float pw; memcpy(&pw, m + 0x24, 4);
        if (rd32(m, 0x14) == 1 && pw > 2.0f && !getenv("TT_KEEPSHINE")) {
            float half = 0.50196f, one = 1.0f, three = 3.0f;
            wr32(m, 0x14, 0);
            memcpy(m + 0x18, &half, 4); memcpy(m + 0x1C, &half, 4); memcpy(m + 0x20, &half, 4);
            memcpy(m + 0x24, &one, 4); memcpy(m + 0x28, &three, 4);
            tt_dbg("[HEROES] his model made matte\n");
        }
    }
    parts = rd32(m, 0x2C);
    if (getenv("TT_ONEPART") && parts > 1) { wr32(m, 0x2C, 1); parts = 1; }   /* testing */
    {
        /* the parts' headers and tables are packed one after the other:
         * rebuilt into a copy, shorter where slots merged */
        unsigned char out[0x2000];
        uint32_t q = 0, start = p, end;
        /* the vertices (48 bytes each, shared by the parts) start 0x34 bytes
         * after a part's vertex offset, the strip indices 0x4C after its
         * index offset (the push-buffer objects' headers come first) */
        uint32_t vc0 = rd32(m, 0x30), vo0 = rd32(m, 0x38) + 0x30;
        unsigned char *own = (unsigned char *)calloc(vc0 ? vc0 : 1, 1);
        if (!own) return;
        memset(own, 0xFF, vc0);
        /* every vertex's palette indices are into the first part's table
         * (the second part's strips use the same vertices and numbering) */
        memset(own, 0, vc0);
        for (k = 0; k < parts && k < 16; k++) {
            uint32_t nc;
            c = rd32(m, p + 0x20);
            if (c > 255 || p + 0x24 + 2 * c > len) { free(own); return; }
            for (i = 0; i < c; i++) {
                unsigned char *b = m + p + 0x24 + 2 * i;
                if (*b < nmap) { *b = map[*b]; n++; }
                if (getenv("TT_BONEKEEP")) {   /* testing: hero bones not listed become the pelvis (2) */
                    char t[8]; snprintf(t, sizeof t, ",%d,", *b);
                    if (!strstr(getenv("TT_BONEKEEP"), t)) *b = 2;
                }
                if (getenv("TT_BONEX")) {   /* testing: TT_BONEX=from,to */
                    int f0 = -1, t0 = -1; sscanf(getenv("TT_BONEX"), "%d,%d", &f0, &t0);
                    if (*b == f0) *b = (unsigned char)t0;
                }
            }
            {   /* the later parts' palette sizes */
                uint32_t pp = (p + 0x24 + 2 * c + 3) & ~3u, kk, rsv = 0;
                for (kk = k + 1; kk < parts && kk < 16; kk++) {
                    uint32_t cc = rd32(m, pp + 0x20);
                    if (cc > 255) break;
                    if (cc > rsv) rsv = cc;
                    pp = (pp + 0x24 + 2 * cc + 3) & ~3u;
                }
                /* a model of several parts keeps its palette as it is: the
                 * later parts' strips use the first part's slots as they
                 * stand (Gandalf's hat tip) */
                nc = (getenv("TT_NOMERGE") || (parts > 1 && !getenv("TT_MERGE2"))) ? c : merge_slots(m, len, p, c, vo0, vc0, own, k, 0);
                if (nc == c) {   /* the unused second index made safe all the same */
                    uint32_t j;
                    if (k == 0) for (j = 0; j < vc0; j++) sanitize_zero_w(m, vo0 + j * 48, 1);
                }
                (void)rsv;
            }
            if (q + 0x24 + 2 * nc + 4 > sizeof out) { free(own); return; }
            memcpy(out + q, m + p, 0x24 + 2 * nc);
            wr32(out, q + 0x20, nc);
            q = (q + 0x24 + 2 * nc + 3) & ~3u;
            p = (p + 0x24 + 2 * c + 3) & ~3u;
        }
        end = p;
        free(own);
        if (q < end - start) {
            memset(out + q, 0, end - start - q);
            memcpy(m + start, out, end - start);
        }
        /* What follows the tables (up to the first part's indices) is the
         * shadow volume's data, laid out by the model's own skeleton: with
         * a skeleton much unlike the hero's (Gandalf, Lurtz: over 50 bones
         * in use) the volumes come out torn and the stencil shadow smears
         * wedges over the scene, so theirs is blanked (no stencil shadow). */
        if ((n > 50 && map != s_idmap) || s_remap_noshadow || getenv("TT_NOSHADOW")) {
            uint32_t io = rd32(m, 0x30 + 0x1C);
            if (io > end && io <= len) memset(m + end, 0, io - end);
        }
    }
    tt_dbg("[HEROES] model re-rigged: %u bone slots in %u part(s)\n", n, parts);
}

/* ===================================================================== */
/* the guest's own animations as the hero's moves                         */
/* ===================================================================== */
/* An animation set ("aniz"): +6 u16 slot count, +0xC size, +0x10 the slot
 * table (u32 record offsets, 0 none); the skeleton ("hier", with its
 * "skel" chunk of attach points and hit volumes) from +0x40 to the first
 * record. A record ("anim"): 16 header words -- [2] length in ms, [4]
 * flags, [5] size, [6] events, [7] the events (offsets from their start,
 * then each: u32 time in ms, ...), [8] the bones' table (u32 offsets of
 * their key data, which starts at +0x40), [9] a time or 0xFFFF, [10] the
 * tail (combo windows: [12] u16 time pairs from [13], then a block from
 * [14]) -- the hero's game logic runs on the events and the tail.
 *
 * The guest plays as himself, on his own skeleton: a set with his
 * skeleton whose every slot the hero has holds one of the guest's own
 * motions, with the hero's events and tail for that slot (their times
 * scaled to the guest's motion). Which motion: his own for that slot, else
 * one from his attacks for the hero's (speed, fierce, ranged, ...). */
enum { AC_IDLE, AC_MOVE, AC_SPEED, AC_FIERCE, AC_PARRY, AC_RANGED, AC_HIT, AC_KNOCK, AC_DEATH, AC_KICK, AC_N };
static int anim_class(int s)
{
    switch (s) {
    case 152: case 153: case 154: case 155: case 196: case 197: case 41: case 42: case 44: case 47:
    case 57: case 59: case 191: case 193: case 81: case 82: case 83: case 86: case 87: case 88: case 90:
    case 35:   /* (1.9.9) the first swing from the stance (600 ms, a lunge 4.5 ahead): counted as no move, the guests got
                * Aragorn's retargeted and swung his first, then their own (a playtest: Lurtz, the trolls) */
        return AC_SPEED;
    case 45: case 46: case 43: case 48: case 54: case 58: case 60: case 104: case 194: case 133: case 134:
        return AC_FIERCE;
    case 50: case 105: return AC_KICK;
    case 55: case 56: return AC_PARRY;   /* the parry (A): 55 then 56 */
    case 40: case 144: case 145: case 146: case 147: case 148: case 149: return AC_RANGED;
    case 20: case 21: return AC_HIT;
    case 27: case 28: case 29: case 30: return AC_KNOCK;
    case 24: return AC_DEATH;
    case 2: case 3: case 4: case 5: case 9: case 10: case 11: case 15: return AC_MOVE;
    default: return AC_IDLE;
    }
}
/* per guest: the motions for each kind, comma lists of his slots
 * (TT_ANIMS_<name>=speed:41,42;fierce:43 overrides, for testing) */
/* rtscale: his attacks (speed, fierce, kick, ranged) are the hero's own,
 * retargeted onto his skeleton (retarget_rec), the root's moves scaled by
 * it -- Boromir's and Frodo's own swings looked like falling back; the lunges kept the hero's full length, so heavy blows reach as his do */
typedef struct { const char *name; const char *pools[AC_N]; float rtscale; const char *rtcls; } AnimPools;
/* the Uruk-hai's heavy attacks (1.8): his two swings that strike forward
 * (43, 69; measured: the sword hand's reach ahead of him at its fastest).
 * Before, the second blow of the chain was 113, a guard whose blade never
 * comes forward (it looked like a parry and struck nothing), and the first
 * 41, a charge that carried him 9 units on */
/* death (1.8): the fall his own set has (the enemies' knockdown, 27, which
 * ends on the ground; the Uruk-hai's own death, 24) -- the slot 24 of
 * theirs is not always a death, and Boromir and Gandalf stood back up with
 * 30, the getting up; none (Boromir's ends on his knees; Lurtz's and
 * Sauron's deaths are their bosses' and leave them standing): the hero's,
 * fitted to him */
/* rtcls: which kinds are the hero's (s speed, f fierce, k kick, r ranged, p parry);
 * Gandalf and Lurtz keep their own swings */
/* idle (1.7): their stances (34, 33; else 0) -- their 0 is a walk: standing
 * in a slot of the hero's (160, his stance between moves) they walked in place */
static const AnimPools s_pools[] = {
    /*                idle  move  speed            fierce        parry  ranged  hit   knock death kick */
    { "boromir", { "34,33", "9", "41",                "43",         "21",  "21",   "20", "27", "",   "69" }, 1.0f, "krp" },
    { "gandalf", { "34,33", "9", "41,43,55",          "57,55",      "56",  "35",   "20", "27", "27", "43" }, 0, "" },
    { "frodo",   { "34,33", "9", "41",               "43",         "176", "20",   "20", "27", "27", "175" }, 1.0f, "krp" },
    /* 25 es la reaccion del jefe atrapado en el arbol, no una caida.
     * Retargetear solo las caidas/levantadas del jugador; ataques propios. */
    { "lurtz",   { "34,33", "9", "45,74",            "43,69",      "111", "187", "20", "", "", "188" }, 1.0f, "n" },   /* (1.9.9: not the hero's thrust, 't' -- in the light chain, Aragorn's swing came first; his own only) */
    /* (no parry of their own: the kick, or for Sauron a swing) */
    { "elrond",  { "34,33", "9", "66",                "66",         "20",  "20",   "20", "27", "27", "20" }, 1.0f, "krp" },
    { "sauron",  { "34,33", "2,0,1", "43,41",            "41,43",      "43",  "43",   "20", "27", "", "43" }, 0, "" },
    { "uruk",    { "34,33", "9", "104,43,69",         "43,69",      "69",  "110",  "20", "27", "24", "69" }, 0, "" },
    /* Saruman: no walk of his own (he stands on Orthanc): the hero's moves fitted to him ('m'); his casts */
    { "saruman", { "34,33", "", "64,62", "65,66", "20", "49", "22", "", "", "20" }, 1.0f, "kpn" },
    { "urukcap", { "34,33", "9", "41", "172,69", "20", "20", "20,21", "27", "24", "20" }, 1.0f, "krp" },   /* (1.9.9: his own falls, the Uruk-hai's -- the hero's, retargeted, laid him flat in the air) */
    { "troll", { "34,33", "9", "41,43", "45,43,113", "20", "20", "20,21", "", "24", "20" }, 1.0f, "krpn" },   /* (ranged: his throw, heroes_troll_throw.h) */
    /* (Versus only) the elven warrior: his own swing, the hero's falls */
    { "elvenwarrior", { "34,33", "9", "66", "66", "20", "20", "20", "", "", "20" }, 1.0f, "krpn" },
    /* the Versus-only three (with the elven warrior above): their own swings, flinches, falls and deaths; the hero's kick (their special), parry and
     * throw fitted to them. The Nazgul has no fall of his own: his flight (2, the run he takes from Weathertop's fire) */
    { "nazgul",  { "34,33", "9", "69,41,45",          "43,45",      "37",  "41",   "20,21", "2,2,2,34", "2", "41" }, 1.0f, "kpr" },
    { "orc",     { "34,33", "9", "41,104,69",         "43,45",      "40",  "41",   "20,21", "27", "24", "69" }, 1.0f, "kpr" },
    { "eomer",   { "34,33", "9", "41,69",             "43,41",      "41",  "41",   "20,21", "27", "24", "69" }, 1.0f, "kpr" },
    /* (1.9.9) the other troll (player 2 beside a cave troll): the cave troll's, on his set */
    { "troll2", { "34,33", "9", "41,43", "45,43,113", "20", "20", "20,21", "", "24", "20" }, 1.0f, "krpn" },   /* (ranged: his throw, heroes_troll_throw.h) */
    /* (1.9.9) the Ent: his set has four motions -- 0 his walk, 33 and 34 his stance, 171 (a scene's) -- so his own stance and walk,
     * and the hero's swings, parry, kick, throw and falls fitted to his skeleton; a blow taken: his stance (no flinch of his own) */
    { "ent", { "34,33", "0", "", "", "", "", "33", "", "", "" }, 1.0f, "sfkrpn" },
};
static int pool_pick(const char *list, int n, const uint32_t *gtab, uint32_t gns)
{
    int v[32], c = 0;
    const char *p = list;
    while (p && *p && c < 32) {
        int x = atoi(p);
        if (x >= 0 && (uint32_t)x < gns && gtab[x]) v[c++] = x;
        p = strchr(p, ','); if (p) p++;
    }
    return c ? v[n % c] : -1;
}
static const char *pool_list(int g, int cls)
{
    static char buf[AC_N][128];
    static const char *key[AC_N] = { "idle", "move", "speed", "fierce", "parry", "ranged", "hit", "knock", "death", "kick" };
    char ev[64];
    const char *e, *q;
    snprintf(ev, sizeof ev, "TT_ANIMS_%s", s_pools[g].name);
    if ((e = getenv(ev)) && (q = strstr(e, key[cls])) && q[strlen(key[cls])] == ':') {
        size_t i;
        q += strlen(key[cls]) + 1;
        for (i = 0; i < sizeof buf[cls] - 1 && q[i] && q[i] != ';'; i++) buf[cls][i] = q[i];
        buf[cls][i] = 0;
        return buf[cls];
    }
    return s_pools[g].pools[cls];
}
static uint32_t s_cyc[64][3], s_ncyc, s_comp_size;
static int s_compose_menu;
static uint32_t s_compose_hkey = 1u;   /* the hero's set (type 1100) in a menu file: the select's Aragorn, or the upgrade figure's Isildur */
static int s_fe_native;   /* the character select's guests on their own skeletons */
static const char *s_menu_moves[N_SEL][3] = {   /* stance, chosen pose, confirm */
    { "34,33", "41,34", "43,41" },          /* boromir: saludar con el arma, no caer (27) */
    { "34,33", "36,35", "35,57" },          /* gandalf */
    { "34,33", "2,1", "41,43" },            /* frodo: looking about (30 was his fall), his blade raised */
    { "34,33", "187,113", "43,69" },        /* lurtz */
    { "34,33", "66,33", "66,33" },          /* elrond: his sword swing when chosen too (1.9.1; 33 is a stance: nothing moved) */
    { "34", "41,43", "43,41" },             /* sauron */
    { "34,33", "43,41", "41,43" },          /* uruk */
};
/* the composed set's settings, for the cache name */
static void hudammo_path(int sk, char *p2, size_t n);
static uint32_t anims_hash(int g)
{
    char ev[64]; const char *e; uint32_t h = 0;
    if (g < 0 || g >= N_SEL) return 0;
    snprintf(ev, sizeof ev, "TT_ANIMS_%s", s_pools[g].name);
    for (e = getenv(ev); e && *e; e++) h = h * 31u + (unsigned char)*e;
    if (getenv("TT_ANIMCYCLE") || getenv("TT_CYCSLOTS")) h ^= GetTickCount();
    {   /* his ready motions (heroes\<name>\anim_<slot>.bin): sizes and times */
        char pth[700]; int s; struct stat fa;
        paths();
        for (s = 0; s < 256; s++) {
            snprintf(pth, sizeof pth, "%s/heroes/%s/anim_%d.bin", s_dir, s_pools[g].name, s);
            if (!stat(pth, &fa))
                h = h * 31u + (uint32_t)s * 7919u + (uint32_t)fa.st_size + (uint32_t)fa.st_mtime;
        }
    }
    if (getenv("TT_XSLOT")) h += 101u * (uint32_t)atoi(getenv("TT_XSLOT"));
    if (getenv("TT_SHBONE")) h += 107u * (uint32_t)atoi(getenv("TT_SHBONE"));
    if (getenv("TT_SHSKIN")) h += 109u * (uint32_t)atoi(getenv("TT_SHSKIN"));
    if (getenv("TT_XENT")) h += 103u * (uint32_t)atoi(getenv("TT_XENT"));
    if (getenv("TT_NOHITBONE")) h += 0x5151u;
    if (getenv("TT_ALLSTANCE")) h += 0x7373u;
    { const char *rc = getenv("TT_RTCLASSES"); for (; rc && *rc; rc++) h = h * 37u + (unsigned char)*rc; }
    if (getenv("TT_RT_NOROOT")) h += 0x2929u;
    if (getenv("TT_KICKYAW")) h += 0x6161u * (uint32_t)(atoi(getenv("TT_KICKYAW")) + 1000);
    {   /* his ammo counter's picture */
        char p2[700]; struct stat fa;
        hudammo_path(g, p2, sizeof p2);
        if (!stat(p2, &fa)) h = h * 31u + (uint32_t)fa.st_size + (uint32_t)fa.st_mtime;
        if (getenv("TT_NOHUDAMMO")) h += 0x5353u;
    }
    if (getenv("TT_TESTHP")) h += 0x7171u * (uint32_t)(atoi(getenv("TT_TESTHP")) + 1);
    if (getenv("TT_NORETARGET")) h += 0x7373u;
    if (getenv("TT_RT_NOHAND")) h += 0x4141u;
    if (getenv("TT_RT_OLDMAP")) h += 0x3131u;
    if (getenv("TT_NOAIM2")) h += 0x3737u;
    if (getenv("TT_SAMEKEYS")) h += 0x6161u;
    { const char *ry = getenv("TT_RTYAW"); for (; ry && *ry; ry++) h = h * 41u + (unsigned char)*ry; }
    if (getenv("TT_RTNOTURN")) h += 0x5353u;
    if (getenv("TT_RT_NOBRANCH")) h += 0x5757u;
    if (getenv("TT_RT_NOVSCALE")) h += 0x5959u;
    if (getenv("TT_STRIKEKEEP")) h += 0x6363u;
    if (getenv("TT_OWNROOT")) h += 0x6565u;
    if (getenv("TT_NOEASE")) h += 0x7979u;
    if (getenv("TT_NOAIM")) h += 0x3939u;
    { const char *w2 = getenv("TT_W2"); for (; w2 && *w2; w2++) h = h * 41u + (unsigned char)*w2; }
    if (getenv("TT_HITBONE")) h += 113u * (uint32_t)atoi(getenv("TT_HITBONE"));
    /* 126: la opcion cambia el banco generado; separar ambas caches para
     * que el retorno a la preparacion completa funcione sin borrar nada. */
    if (g == SK_TROLL && getenv("TT_TROLLFULLWIND")) h ^= 0x126F011Du;
    return h;
}
/* ---- retargeting: the hero's motion onto a guest's skeleton ----
 * Quaternions (w,x,y,z). A set's skeleton ("hier" at +0x40): bone count
 * +8, 128-byte matrices at +0xC (the second 4x4, row vectors: the bind
 * pose's world transform), parents (+1) at +0x10. A record's rotation keys
 * {u16 tiempo/signo, 3 x i16 = x,y,z * 32768} turn the bind's local rotation:
 * local = bind_local * key. Each guest bone takes the world turn of the
 * hero bone it maps to (s_map_<name>): world = turn * own bind world. */
typedef struct { float w, x, y, z; } Quat;
static Quat qmulq(Quat a, Quat b)
{
    Quat r = { a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
               a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w };
    return r;
}
static Quat qconj(Quat a) { Quat r = { a.w, -a.x, -a.y, -a.z }; return r; }
static Quat qnormq(Quat a)
{
    float n = sqrtf(a.w * a.w + a.x * a.x + a.y * a.y + a.z * a.z);
    if (n > 0) { a.w /= n; a.x /= n; a.y /= n; a.z /= n; }
    return a;
}
static void qrotv(Quat q, const float *v, float *r)
{
    Quat p = { 0, v[0], v[1], v[2] }, t = qmulq(qmulq(q, p), qconj(q));
    r[0] = t.x; r[1] = t.y; r[2] = t.z;
}
/* the bind pose's world rotations and local ones */
static int rt_binds(const unsigned char *S, size_t len, int *par, Quat *W, Quat *BL)
{
    uint32_t h = 0x40, nb = rd32(S, h + 8), mo = h + rd32(S, h + 0xC), po = h + rd32(S, h + 0x10), i;
    if (nb == 0 || nb > 128 || mo + nb * 128 > len || po + nb > len) return 0;
    for (i = 0; i < nb; i++) {
        float m[3][3], t, sq;   /* m = R transposed (column convention) */
        int r, c;
        Quat q;
        for (r = 0; r < 3; r++) for (c = 0; c < 3; c++) { uint32_t v = rd32(S, mo + i * 128 + 64 + (uint32_t)(r * 4 + c) * 4); memcpy(&m[c][r], &v, 4); }
        t = m[0][0] + m[1][1] + m[2][2];
        if (t > 0) { sq = sqrtf(t + 1) * 2; q.w = sq / 4; q.x = (m[2][1] - m[1][2]) / sq; q.y = (m[0][2] - m[2][0]) / sq; q.z = (m[1][0] - m[0][1]) / sq; }
        else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) { sq = sqrtf(1 + m[0][0] - m[1][1] - m[2][2]) * 2; q.w = (m[2][1] - m[1][2]) / sq; q.x = sq / 4; q.y = (m[0][1] + m[1][0]) / sq; q.z = (m[0][2] + m[2][0]) / sq; }
        else if (m[1][1] > m[2][2]) { sq = sqrtf(1 + m[1][1] - m[0][0] - m[2][2]) * 2; q.w = (m[0][2] - m[2][0]) / sq; q.x = (m[0][1] + m[1][0]) / sq; q.y = sq / 4; q.z = (m[1][2] + m[2][1]) / sq; }
        else { sq = sqrtf(1 + m[2][2] - m[0][0] - m[1][1]) * 2; q.w = (m[1][0] - m[0][1]) / sq; q.x = (m[0][2] + m[2][0]) / sq; q.y = (m[1][2] + m[2][1]) / sq; q.z = sq / 4; }
        if (q.w < 0) { q.w = -q.w; q.x = -q.x; q.y = -q.y; q.z = -q.z; }
        W[i] = q;
        par[i] = (int)S[po + i] - 1;
    }
    for (i = 0; i < nb; i++) BL[i] = par[i] < 0 || par[i] >= (int)nb ? W[i] : qnormq(qmulq(qconj(W[par[i]]), W[i]));
    return (int)nb;
}
/* Xbox: el tiempo ocupa 15 bits; el bit alto elige el signo global del
 * cuaternion. Ver sub_000181E0 / sub_00018270 originales (solo lectura). */
static unsigned rt_key_time(const unsigned char *k){return rd16(k,0)&0x7fffu;}
static int16_t rt_qpack(float v){long x=lroundf(v*32768.0f);return (int16_t)(x>32767?32767:x< -32768?-32768:x);}
static Quat rt_qkey(const unsigned char *k){
    Quat q;q.x=(int16_t)rd16(k,2)/32768.0f;q.y=(int16_t)rd16(k,4)/32768.0f;q.z=(int16_t)rd16(k,6)/32768.0f;
    q.w=sqrtf(fmaxf(0,1-q.x*q.x-q.y*q.y-q.z*q.z));
    if(rd16(k,0)&0x8000u){q.w=-q.w;q.x=-q.x;q.y=-q.y;q.z=-q.z;}return qnormq(q);
}
static int rt_qsample(const unsigned char *k,int n,float t,Quat *out){
    if(n<=0)return 0;if(n==1||t<=rt_key_time(k)){*out=rt_qkey(k);return 1;}
    for(int i=0;i+1<n;i++){
        float a=rt_key_time(k+8*i),b=rt_key_time(k+8*(i+1));if(t<a||t>b)continue;
        float f=b>a?(t-a)/(b-a):0;Quat q=rt_qkey(k+8*i),r=rt_qkey(k+8*(i+1));
        if(q.w*r.w+q.x*r.x+q.y*r.y+q.z*r.z<0){r.w=-r.w;r.x=-r.x;r.y=-r.y;r.z=-r.z;}
        Quat z={q.w+(r.w-q.w)*f,q.x+(r.x-q.x)*f,q.y+(r.y-q.y)*f,q.z+(r.z-q.z)*f};*out=qnormq(z);return 1;
    }*out=rt_qkey(k+8*(n-1));return 1;
}
/* a translation key list's value at time t */
static int rt_sample(const unsigned char *k, int n, float t, float *v)
{
    int i;
    if (n <= 0) return 0;
    if (t <= rt_key_time(k)) { for (i = 0; i < 3; i++) v[i] = (float)(int16_t)rd16(k, 2 + 2 * i); return 1; }
    for (i = 0; i + 1 < n; i++) {
        float t0 = rt_key_time(k+8*i), t1 = rt_key_time(k+8*(i+1));
        if (t0 <= t && t <= t1) {
            float f = t1 > t0 ? (t - t0) / (t1 - t0) : 0; int c;
            for (c = 0; c < 3; c++) { float a = (float)(int16_t)rd16(k, 8 * i + 2 + 2 * c), b = (float)(int16_t)rd16(k, 8 * (i + 1) + 2 + 2 * c); v[c] = a + (b - a) * f; }
            return 1;
        }
    }
    for (i = 0; i < 3; i++) v[i] = (float)(int16_t)rd16(k, 8 * (n - 1) + 2 + 2 * i);
    return 1;
}
/* the rig map (guest bone -> hero bone), put right where it skips a joint
 * the hero has: see retarget_rec */
static void rig_map_fix(const int *hpar, int hn, const int *gpar, int gn, const uint8_t *map, int nmap, int *rm)
{
    int b;
    for (b = 0; b < gn; b++) {
        int h = b < nmap ? map[b] : 0, c;
        if (h >= hn) h = 0;
        rm[b] = h;
        if (getenv("TT_RT_OLDMAP")) continue;
        if (b <= 1 && b < hn && gpar[b] == hpar[b]) { rm[b] = b; continue; }
        if (gpar[b] < 0) continue;
        for (c = b + 1; c < gn; c++) {
            int hc, cand;
            if (gpar[c] != b) continue;
            hc = c < nmap ? map[c] : 0;
            if (hc >= hn) continue;
            cand = hpar[hc];
            if (cand >= 0 && cand < hn && hpar[cand] == rm[gpar[b]] && cand != rm[b]) { rm[b] = cand; break; }
        }
    }
    /* and a bone the map hangs on another branch of the hero than its
     * parent's (Boromir's coat panels and belt, under his pelvis, given the
     * hero's spine) follows its parent's bone: his coat swings with his
     * hips, not his chest */
    if (!getenv("TT_RT_OLDMAP") && !getenv("TT_RT_NOBRANCH"))
        for (b = 2; b < gn; b++) {
            int p = gpar[b], h = rm[b], ok = 0, d;
            if (p < 0 || p >= gn) continue;
            for (d = 0; h >= 0 && h < hn && d < 128; h = hpar[h], d++) if (h == rm[p]) { ok = 1; break; }
            if (!ok) rm[b] = rm[p];
        }
}
/* hero slot hs of set A as a record for guest set G (bones mapped by map);
 * malloc'd, its size in *len */
static uint32_t skel_point_bone(const unsigned char *S, size_t len, int k);
/* The kick (the hero's, retargeted) turned by this much (degrees, + to his
 * left): measured at the Deeping Wall's first ladder, a guest standing
 * square to it, beside it to his left or to his right, kicking once.
 * Aragorn's kick fells it square on and to his right; Boromir's only with
 * the ladder to his left -- turned 30 degrees right, square on and to
 * either side; Sauron's only square on -- turned, as Aragorn's. Elrond,
 * Gandalf, Lurtz and the Uruk-hai fell it as Aragorn does or wider, and
 * from further off (Elrond 4.5 back, where Aragorn's misses). */
static const float s_kickyaw[N_EXTRA] = { -30.0f, 0, 0, 0, 0, -30.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
/* (a cutscene's clips, cine_clips_build: 255) the most rotation
 * keys a bone gets, and root position keys kept */
static int s_rt_maxnt = 96, s_rt_maxpos = 64;
static unsigned char *rt_ground_fix(const unsigned char *A, size_t alen, uint32_t rec, const unsigned char *G, size_t glen,
                                    const unsigned char *o, size_t *plen, uint32_t dur, float vscale);
static unsigned char *retarget_rec(const unsigned char *A, size_t alen, uint32_t hs, const unsigned char *G, size_t glen,
                                   const uint8_t *map, int nmap, float scale, size_t *len)
{
    static int hpar[128], gpar[128];
    static Quat hW[128], hBL[128], gW[128], gBL[128], Q[128], Wg[128];
    static int16_t keys[128][255][3];
    int hn = rt_binds(A, alen, hpar, hW, hBL), gn = rt_binds(G, glen, gpar, gW, gBL), b, nt, i;
    uint32_t atab = rd32(A, 0x10), rec, dur, bt, gtab = rd32(G, 0x10), donor;
    const unsigned char *hb[128];
    int hnr[128], hnp[128];
    unsigned char *o; size_t p;
    uint32_t offs[128];
    uint32_t hp[2], gp[2];
    int rm[128];
    Quat turn = { 1, 0, 0, 0 };
    float vscale = 1.0f;
    if (!hn || !gn || hs >= rd16(A, 6) || !(rec = rd32(A, atab + 4 * hs)) || !(donor = rd32(G, gtab))) return NULL;
    /* the rig map, put right where it skips a joint the hero has: the
     * map gives his root, his root's child and his pelvis the hero's lower
     * spine (2), and his collarbones the hero's neck (16). His root (0) and
     * its child (1) take the hero's own -- the hero's root never turns (no
     * rotation keys: the game moves the actor by it), his child carries the
     * stance's turn -- and a bone between two mapped ones takes the hero's
     * bone between them (his pelvis the hero's, his collarbones the
     * hero's). World rotations are copied either way; what changes is where
     * his joints sit: his shoulders, so his hands and blade, his hips, so
     * his feet, are where the hero's are. */
    rig_map_fix(hpar, hn, gpar, gn, map, nmap, rm);
    hp[0] = skel_point_bone(A, alen, 0); hp[1] = skel_point_bone(A, alen, 1);
    gp[0] = skel_point_bone(G, glen, 0); gp[1] = skel_point_bone(G, glen, 1);
    dur = rd32(A, rec + 8); bt = rec + rd32(A, rec + 0x20);
    if (bt + 4u * (uint32_t)hn > alen || donor + 0x40 > glen) return NULL;
    for (b = 0; b < hn; b++) {
        uint32_t bo = rd32(A, bt + 4 * b);
        hb[b] = NULL; hnr[b] = hnp[b] = 0;
        if (bo && rec + bo + 4 <= alen) { hb[b] = A + rec + bo; hnr[b] = hb[b][0]; hnp[b] = hb[b][1]; }
    }
    nt = (int)(dur / 33) + 1; if (nt < 2) nt = 2; if (nt > 255) nt = 255; if (nt > s_rt_maxnt) nt = s_rt_maxnt < 2 ? 2 : s_rt_maxnt;
    {   /* His melee moves turned back by the hero's stance (the turn his
         * root's child, bone 1, holds in his fighting stance: about 40
         * degrees for Aragorn's sword stance). On the hero the game allows
         * for it; on a guest it does not, and his blows went that far
         * beside the one he faced (measured in Hornburg Courtyard: his
         * blade's bearing at each hit test, his hits). */
        int cls = anim_class((int)hs);
        float an = 0;
        if (getenv("TT_RTYAW")) an = (float)atof(getenv("TT_RTYAW")) * 0.0174533f;   /* testing: this turn */
        else if (cls == AC_KICK && getenv("TT_KICKYAW")) an = (float)atof(getenv("TT_KICKYAW")) * 0.0174533f;   /* testing: the kick's */
        else if (cls == AC_KICK) {   /* the kick turned for those whose kick lands beside the hero's (kick_yaw) */
            int q;
            for (q = 0; q < N_EXTRA; q++) if (s_skins[q].map == map) an = s_kickyaw[q] * 0.0174533f;
        }
        else if ((cls == AC_SPEED || cls == AC_FIERCE || cls == AC_PARRY || (cls == AC_KICK && getenv("TT_KICKTURN"))) && hn > 1 && hpar[1] == 0 && !getenv("TT_RTNOTURN")) {
            /* (not the kick, 1.8: turned with the rest his kick went 39
             * degrees beside the hero's -- the ladders on the Deeping Wall
             * fell only to a kick from the side, and most enemies stood) */
            /* the stance: his fighting stance's (slot 34), as it starts */
            uint32_t st = rd16(A, 6) > 34 ? rd32(A, atab + 4 * 34) : 0, sbt, sbo;
            Quat lq = hBL[1], d; float x[3] = { 1, 0, 0 }, r[3];
            if (st && (sbt = st + rd32(A, st + 0x20)) + 8 <= alen && (sbo = rd32(A, sbt + 4)) && st + sbo + 4 <= alen
                && rt_qsample(A + st + sbo + 4, A[st + sbo], 0, &d)) {
                lq = qmulq(lq, d);
            }
            if (hpar[0] < 0) lq = qmulq(hBL[0], lq);   /* (bone 0 carries no turn of its own) */
            qrotv(qmulq(lq, qconj(hW[1])), x, r);
            an = -atan2f(r[1], r[0]);
        }
        turn.w = cosf(an * 0.5f); turn.x = 0; turn.y = 0; turn.z = sinf(an * 0.5f);
        if (an != 0 && log_on()) tt_dbg("[HEROES]   slot %u: turned %.0f degrees\n", hs, an * 57.29578f);
    }
    for (i = 0; i < nt; i++) {
        float t = (float)dur * i / (nt - 1);
        for (b = 0; b < hn; b++) {   /* the hero's pose: world rotations */
            Quat lq = hBL[b],d;
            if (hb[b] && rt_qsample(hb[b] + 4, hnr[b], t, &d)) {
                lq = qmulq(lq, d);
            }
            Q[b] = hpar[b] < 0 ? lq : qnormq(qmulq(Q[hpar[b]], lq));
        }
        for (b = 0; b < gn; b++) {
            int h = rm[b];
            /* his hands (the weapons' attach points) turn as the hero's
             * hands do, not as the bone the map gives them (Boromir's sword
             * hand maps to the hero's wrist): the blade then sweeps where
             * the hero's does, and the game's hit test (along the blade)
             * finds the enemies the hero's would */
            if (!getenv("TT_RT_NOHAND")) { if ((uint32_t)b == gp[0] && hp[0] < (uint32_t)hn) h = (int)hp[0]; else if ((uint32_t)b == gp[1] && hp[1] < (uint32_t)hn) h = (int)hp[1]; }
            if (h >= hn) h = 0;
            Wg[b] = qnormq(qmulq(qmulq(Q[h], qconj(hW[h])), gW[b]));
            if (b >= 1 && turn.w < 0.99999f) Wg[b] = qnormq(qmulq(turn, Wg[b]));
        }
        for (b = 0; b < gn; b++) {
            Quat L = gpar[b] < 0 ? Wg[b] : qmulq(qconj(Wg[gpar[b]]), Wg[b]), d = qnormq(qmulq(qconj(gBL[b]), L));
            if (d.w < 0) { d.w = -d.w; d.x = -d.x; d.y = -d.y; d.z = -d.z; }
            keys[b][i][0] = rt_qpack(d.x); keys[b][i][1] = rt_qpack(d.y); keys[b][i][2] = rt_qpack(d.z);
        }
    }
    {   /* the root's ups and downs to his height: the hero's crouch and
         * lunge (his hips dropping a fixed distance) put a short guest on
         * the ground -- Frodo's first swing (196) looked like a fall. The
         * moves along the ground keep the hero's length (his reach). */
        uint32_t hm = 0x40 + rd32(A, 0x40 + 0xC), gm = 0x40 + rd32(G, 0x40 + 0xC);
        float hz, gz; uint32_t v;
        v = rd32(A, hm + 128 + 64 + 56); memcpy(&hz, &v, 4);
        v = rd32(G, gm + 128 + 64 + 56); memcpy(&gz, &v, 4);
        vscale = scale;
        if (hz > 0.5f && gz > 0.2f && !getenv("TT_RT_NOVSCALE")) { vscale = gz / hz; if (vscale > 1.3f) vscale = 1.3f; if (vscale < 0.3f) vscale = 0.3f; }
    }
    o = (unsigned char *)calloc(1, 0x40 + (size_t)gn * (4 + 8 * (nt + s_rt_maxpos)) + 4 * gn + 16);
    if (!o) return NULL;
    memcpy(o, G + donor, 0x40);
    p = 0x40;
    for (b = 0; b < gn; b++) {
        int nr = nt, np = 0, k, same = 1;
        const unsigned char *pk = NULL;
        for (k = 1; k < nt && same; k++)
            if (abs(keys[b][k][0] - keys[b][0][0]) >= 3 || abs(keys[b][k][1] - keys[b][0][1]) >= 3 || abs(keys[b][k][2] - keys[b][0][2]) >= 3) same = 0;
        if (same) nr = 1;
        if (b == 0 && rm[0] == 0 && (!hb[0] || !hnr[0]) && !getenv("TT_RT_OLDMAP")) nr = 0;   /* his root unturned, as the hero's */
        if (b <= 1 && b < hn && hb[b] && hnp[b] && hnp[b] <= s_rt_maxpos && !getenv("TT_RT_NOROOT")) { pk = hb[b] + 4 + 8 * hnr[b]; np = hnp[b]; }   /* the root's moves */
        offs[b] = (uint32_t)p;
        o[p] = (unsigned char)nr; o[p + 1] = (unsigned char)np; o[p + 2] = 0; o[p + 3] = 0; p += 4;
        for (k = 0; k < nr; k++) {
            wr16(o, p, (uint16_t)lroundf((float)dur * k / (nt - 1))); wr16(o, p + 2, (uint16_t)keys[b][k][0]); wr16(o, p + 4, (uint16_t)keys[b][k][1]); wr16(o, p + 6, (uint16_t)keys[b][k][2]); p += 8;
        }
        for (k = 0; k < np; k++) {
            int c;
            wr16(o, p, rd16(pk, 8 * k));
            for (c = 0; c < 3; c++) wr16(o, p + 2 + 2 * c, (uint16_t)(int16_t)lroundf((float)(int16_t)rd16(pk, 8 * k + 2 + 2 * c) * (c == 2 ? vscale : scale)));
            p += 8;
        }
    }
    p = (p + 3) & ~(size_t)3;
    {
        size_t evo = p;
        for (b = 0; b < gn; b++) { wr32(o, p, offs[b]); p += 4; }
        wr32(o, 8, dur);
        wr32(o, 0x14, (uint32_t)p); wr32(o, 0x18, 0); wr32(o, 0x1C, (uint32_t)evo); wr32(o, 0x20, (uint32_t)evo);
        wr32(o, 0x24, 0xFFFF); wr32(o, 0x28, (uint32_t)p); wr32(o, 0x2C, 0);
        wr32(o, 0x30, 0); wr32(o, 0x34, 0); wr32(o, 0x38, 0);
    }
    {   /* (1.9.9) a fall or a hit: his lowest point kept where the hero's is (in proportion), by his root's height -- turned as
         * the hero turns, his longer body lay in the air: the Uruk captain knocked down flat at another hero's chest, Lurtz the
         * same (a playtest video). TT_RT_NOGROUND: as before */
        int cl = anim_class((int)hs);
        if ((cl == AC_KNOCK || cl == AC_HIT || cl == AC_DEATH) && !getenv("TT_RT_NOGROUND")) {
            unsigned char *o2 = rt_ground_fix(A, alen, rec, G, glen, o, &p, dur, vscale);
            if (o2) { free(o); o = o2; }
        }
    }
    *len = p;
    return o;
}
/* ---- a record's pose: the bones' positions (model space) at time t ----
 * local rotation = bind local * key; local offset = bind offset + position
 * key / 256 (as the game plays them) */
static Quat s_pose_Q[128];   /* the last pose's world rotations */
static int s_pose_noroot;    /* (1.9.9) rt_pose: 1 the root bone's (0) x/y moves left out (the game moves the actor by them), 2 all of them */
static int rt_pose(const unsigned char *S, size_t len, const unsigned char *rec, size_t rlen, float t, float (*P)[3])
{
    static int par[128]; static Quat W[128], BL[128], Q[128];
    static float BT[128][3], T[128][3];
    static const unsigned char *last; static size_t lastlen;
    int nb, b, c;
    uint32_t bt;
    if (S != last || len != lastlen) {   /* the skeleton's bind offsets, once per set */
        uint32_t mo;
        nb = rt_binds(S, len, par, W, BL);
        if (!nb) return 0;
        mo = 0x40 + rd32(S, 0x40 + 0xC);
        for (b = 0; b < nb; b++) for (c = 0; c < 3; c++) { uint32_t v = rd32(S, mo + (uint32_t)b * 128 + 64 + 48 + 4 * (uint32_t)c); memcpy(&T[b][c], &v, 4); }
        for (b = 0; b < nb; b++) {
            if (par[b] < 0) { for (c = 0; c < 3; c++) BT[b][c] = T[b][c]; continue; }
            { float d[3] = { T[b][0] - T[par[b]][0], T[b][1] - T[par[b]][1], T[b][2] - T[par[b]][2] }; qrotv(qconj(W[par[b]]), d, BT[b]); }
        }
        last = S; lastlen = len;
    }
    nb = (int)rd32(S, 0x40 + 8);
    if (nb <= 0 || nb > 128) return 0;
    bt = rd32(rec, 0x20);
    if (bt + 4u * (uint32_t)nb > rlen) return 0;
    for (b = 0; b < nb; b++) {
        uint32_t bo = rd32(rec, bt + 4 * (uint32_t)b);
        Quat lq = BL[b]; float lt[3] = { BT[b][0], BT[b][1], BT[b][2] }, v[3];
        if (bo && bo + 4 <= rlen) {
            int nr = rec[bo], np = rec[bo + 1];
            if (bo + 4 + 8u * (uint32_t)(nr + np) <= rlen) {
                Quat d;
                if (rt_qsample(rec + bo + 4, nr, t, &d)) {
                    lq = qmulq(lq, d);
                }
                if (rt_sample(rec + bo + 4 + 8 * nr, np, t, v)) for (c = 0; c < 3; c++) if (!(b == 0 && s_pose_noroot && (s_pose_noroot == 2 || c < 2))) lt[c] += v[c] / 256.0f;
            }
        }
        if (par[b] < 0 || par[b] >= nb) { Q[b] = lq; for (c = 0; c < 3; c++) P[b][c] = lt[c]; }
        else {
            float r[3];
            Q[b] = qnormq(qmulq(Q[par[b]], lq));
            qrotv(Q[par[b]], lt, r);
            for (c = 0; c < 3; c++) P[b][c] = P[par[b]][c] + r[c];
        }
    }
    memcpy(s_pose_Q, Q, sizeof(Quat) * (size_t)nb);
    return nb;
}
/* (1.9.9) the lowest bone's height in set S's record at time t (1e9: none) */
static float rt_lowest(const unsigned char *S, size_t len, const unsigned char *rec, size_t rlen, float t)
{
    static float P[128][3]; int n = rt_pose(S, len, rec, rlen, t, P), b; float lo = 1e9f;
    for (b = 0; b < n; b++) if (P[b][2] < lo) lo = P[b][2];
    return lo;
}
/* standing: the lowest bone at the start of his stance (34, else 0) */
static float rt_stand_low(const unsigned char *S, size_t len)
{
    uint32_t tab = rd32(S, 0x10), ns = rd16(S, 6), r = ns > 34 ? rd32(S, tab + 4 * 34) : 0;
    if (!r && ns) r = rd32(S, tab);
    if (!r || r >= len) return 1e9f;
    return rt_lowest(S, len, S + r, len - r, 0);
}
static unsigned char *rt_ground_fix(const unsigned char *A, size_t alen, uint32_t rec, const unsigned char *G, size_t glen,
                                    const unsigned char *o, size_t *plen, uint32_t dur, float vscale)
{
    /* (1.9.9) measured as the game draws him: his root bone's (0) moves are left out -- the game moves the actor by them,
     * and their height is never drawn (Lurtz, lying, was still 3.8 up after his root was lowered). The height is put on his
     * root's child (bone 1) instead, the hero's lowest point kept over the floor in proportion (TT_RT_GROUNDK, 1) */
    size_t olen = *plen, q; uint32_t gbt = rd32(o, 0x20), gn = rd32(G, 0x40 + 8), b, off1, nr1, np1, K, k;
    float hl0, gl0, dz[64], most = 0;
    float kf = getenv("TT_RT_GROUNDK") ? (float)atof(getenv("TT_RT_GROUNDK")) : 1.0f;
    unsigned char *o2; int16_t px[64][3];
    static int gpar[128]; static Quat gW[128], gBL[128];
    if (gn < 2 || gn > 128 || gbt + 4 * gn > olen || dur == 0 || rt_binds(G, glen, gpar, gW, gBL) < 2 || gpar[1] != 0) return NULL;
    s_pose_noroot = 2;
    hl0 = rt_stand_low(A, alen); gl0 = rt_stand_low(G, glen);
    if (hl0 > 1e8f || gl0 > 1e8f) { s_pose_noroot = 0; return NULL; }
    off1 = rd32(o, gbt + 4); if (!off1 || off1 + 4 > olen) { s_pose_noroot = 0; return NULL; }
    nr1 = o[off1]; np1 = o[off1 + 1];
    K = (uint32_t)s_rt_maxpos < 64u ? (uint32_t)s_rt_maxpos : 64u; { uint32_t want = dur / 33u + 2u; if (want < K) K = want; } if (K < 2) K = 2;
    for (k = 0; k < K; k++) {
        float t = (float)dur * (float)k / (float)(K - 1), hl = rt_lowest(A, alen, A + rec, alen - rec, t), gl, v[3] = { 0, 0, 0 }, want, w[3], l[3];
        Quat q0;
        gl = rt_lowest(G, glen, o, olen, t); q0 = s_pose_Q[0];
        if (hl > 1e8f || gl > 1e8f) { s_pose_noroot = 0; return NULL; }
        want = gl0 + (hl - hl0) * vscale * kf;
        dz[k] = want - gl; if (dz[k] > 4.0f) dz[k] = 4.0f; if (dz[k] < -4.0f) dz[k] = -4.0f;
        if (fabsf(dz[k]) > fabsf(most)) most = dz[k];
        if (np1) rt_sample(o + off1 + 4 + 8 * nr1, (int)np1, t, v);
        w[0] = 0; w[1] = 0; w[2] = dz[k]; qrotv(qconj(q0), w, l);   /* (the world's up in his root's frame) */
        px[k][0] = (int16_t)lroundf(v[0] + l[0] * 256.0f); px[k][1] = (int16_t)lroundf(v[1] + l[1] * 256.0f); px[k][2] = (int16_t)lroundf(v[2] + l[2] * 256.0f);
    }
    s_pose_noroot = 0;
    if (fabsf(most) < 0.15f) return NULL;   /* (close enough already) */
    o2 = (unsigned char *)calloc(1, olen + 8 * K + 16);
    if (!o2) return NULL;
    memcpy(o2, o, 0x40); q = 0x40;
    {   uint32_t offs[128];
        for (b = 0; b < gn; b++) {
            uint32_t ob = rd32(o, gbt + 4 * b), nr = ob ? o[ob] : 0, np = ob ? o[ob + 1] : 0;
            offs[b] = (uint32_t)q;
            if (b == 1) {
                o2[q] = (unsigned char)nr; o2[q + 1] = (unsigned char)K; q += 4;
                memcpy(o2 + q, o + ob + 4, 8 * nr); q += 8 * nr;
                for (k = 0; k < K; k++) { wr16(o2, q, (uint16_t)lroundf((float)dur * (float)k / (float)(K - 1))); wr16(o2, q + 2, (uint16_t)px[k][0]); wr16(o2, q + 4, (uint16_t)px[k][1]); wr16(o2, q + 6, (uint16_t)px[k][2]); q += 8; }
            } else if (!ob) { offs[b] = 0; }
            else { memcpy(o2 + q, o + ob, 4 + 8 * (nr + np)); q += 4 + 8 * (nr + np); }
        }
        q = (q + 3) & ~(size_t)3;
        {   size_t evo = q;
            for (b = 0; b < gn; b++) { wr32(o2, q, offs[b]); q += 4; }
            wr32(o2, 0x14, (uint32_t)q); wr32(o2, 0x1C, (uint32_t)evo); wr32(o2, 0x20, (uint32_t)evo); wr32(o2, 0x28, (uint32_t)q);
        }
    }
    if (log_on()) tt_dbg("[HEROES]   ground: his lowest point kept to the hero's (his root's child moved up to %.2f)\n", most);
    *plen = q;
    return o2;
}
/* when a swing is at its fastest: the hand bone's speed relative to the
 * root, sampled every 33 ms; its time in ms, or -1 */
static int swing_peak(const unsigned char *S, size_t len, const unsigned char *rec, size_t rlen, int hand, uint32_t dur, float *speed)
{
    static float P[128][3], prev[3];
    float best = -1, t;
    if (speed) *speed = 0;
    if (hand < 0 || hand >= 128) return -1;
    int bt = -1, have = 0;
    for (t = 0; t <= (float)dur; t += 33.0f) {
        float h[3], sp;
        int c;
        if (rt_pose(S, len, rec, rlen, t, P) <= hand) return -1;
        for (c = 0; c < 3; c++) h[c] = P[hand][c] - P[0][c];
        if (have && t > dur * 0.12f && t < dur * 0.92f) {   /* (not the blends in and out) */
            sp = (h[0] - prev[0]) * (h[0] - prev[0]) + (h[1] - prev[1]) * (h[1] - prev[1]) + (h[2] - prev[2]) * (h[2] - prev[2]);
            if (sp > best) { best = sp; bt = (int)(t - 16.5f); }
        }
        for (c = 0; c < 3; c++) prev[c] = h[c];
        have = 1;
    }
    if (speed) *speed = best;
    return bt;
}
#include "heroes_native_motion.h"
#include "heroes_troll_native.h"
#include "heroes_troll_attack.h"
/* a foot's travel over a cycle (its longest extent in the ground plane):
 * a walk's or run's stride */
static float stride_len(const unsigned char *S, size_t len, const unsigned char *rec, size_t rlen, int foot, uint32_t dur)
{
    static float P[128][3];
    float lo[2] = { 1e9f, 1e9f }, hi[2] = { -1e9f, -1e9f }, t;
    int c;
    for (t = 0; t <= (float)dur; t += 33.0f) {
        if (rt_pose(S, len, rec, rlen, t, P) <= foot) return 0;
        for (c = 0; c < 2; c++) { float v = P[foot][c] - P[0][c]; if (v < lo[c]) lo[c] = v; if (v > hi[c]) hi[c] = v; }
    }
    return hi[0] - lo[0] > hi[1] - lo[1] ? hi[0] - lo[0] : hi[1] - lo[1];
}
/* a hero time (ms) in the composed move: scaled, or with the strike moved
 * (hero strike sh -> his sg; -1 none) */
static uint32_t strike_map(uint32_t t, float scale, int sh, int sg, uint32_t hdur, uint32_t target)
{
    float r;
    if (sh <= 0 || sg <= 0 || !hdur) return (uint32_t)(t * scale + 0.5f);
    if ((int)t <= sh) r = (float)t * (float)sg / (float)sh;
    else if (hdur > (uint32_t)sh) r = (float)sg + ((float)t - (float)sh) * ((float)target - (float)sg) / ((float)hdur - (float)sh);
    else r = (float)t * scale;
    return r < 0 ? 0u : (uint32_t)(r + 0.5f);
}
/* the first bone of a set's skeleton's attach point k ("skel" chunk:
 * offsets of bone lists {count, bones} from its table), or 0xFFFF */
static uint32_t skel_point_bone(const unsigned char *S, size_t len, int k)
{
    uint32_t gs = 0x40 + rd32(S, 0x40 + 0x14), t, o;
    if (gs + 0x40 > len || memcmp(S + gs, "skel", 4) || rd32(S, gs + 8) <= (uint32_t)k) return 0xFFFFu;
    t = gs + rd32(S, gs + 0xC);
    if (t + 4 * (uint32_t)k + 4 > len || !(o = rd32(S, t + 4 * (uint32_t)k)) || t + o + 8 > len || !rd32(S, t + o)) return 0xFFFFu;
    return rd32(S, t + o + 4);
}
/* a bone's place in the bind pose (set S) */
static void skel_bind_pos(const unsigned char *S, uint32_t b, float *p)
{
    uint32_t mo = 0x40 + rd32(S, 0x40 + 0xC) + b * 128 + 64 + 48, c;
    for (c = 0; c < 3; c++) { uint32_t v = rd32(S, mo + 4 * c); memcpy(&p[c], &v, 4); }
}
/* the guest's bone for the hero's bone h: the same attach point's, else the
 * nearest (in the bind pose, scaled to his height) of those the rig map
 * gives it, else the nearest of all */
static uint32_t guest_bone_for(const unsigned char *A, size_t alen, const unsigned char *G, size_t glen, int g, uint32_t h)
{
    static const int pts[] = { 0, 1, 2, 3, 4, 5, 8 };
    uint32_t nA = rd32(A, 0x40 + 8), nG = rd32(G, 0x40 + 8), b, best = 0, k;
    float hp[3], gp[3], hz = 0, gz = 0, sc, bd = 1e30f;
    int pass;
    static int hpar[128], gpar[128], rm[128]; static Quat W[128], BL[128];
    int hn = rt_binds(A, alen, hpar, W, BL), gn = rt_binds(G, glen, gpar, W, BL);
    if (g >= 0 && g < N_SKINS && s_skins[g].map && hn && gn) rig_map_fix(hpar, hn, gpar, gn, s_skins[g].map, s_skins[g].nmap, rm);
    else gn = 0;
    for (k = 0; k < sizeof pts / sizeof pts[0]; k++) {
        uint32_t hb = skel_point_bone(A, alen, pts[k]), gb = skel_point_bone(G, glen, pts[k]);
        if (hb == h && gb < nG) return gb;
    }
    if (h >= nA || !nG) return 0;
    for (b = 0; b < nA; b++) { skel_bind_pos(A, b, hp); if (hp[2] > hz) hz = hp[2]; }
    for (b = 0; b < nG; b++) { skel_bind_pos(G, b, gp); if (gp[2] > gz) gz = gp[2]; }
    sc = hz > 0 ? gz / hz : 1;
    skel_bind_pos(A, h, hp);
    for (k = 0; k < 3; k++) hp[k] *= sc;
    for (pass = 0; pass < 2 && bd >= 1e30f; pass++)
        for (b = 0; b < nG; b++) {
            float d;
            if (!pass && !((int)b < gn && rm[b] == (int)h)) continue;
            if (pass && b <= 1 && h > 1) continue;   /* (the root and its child sit where the pelvis does) */
            skel_bind_pos(G, b, gp);
            d = (gp[0] - hp[0]) * (gp[0] - hp[0]) + (gp[1] - hp[1]) * (gp[1] - hp[1]) + (gp[2] - hp[2]) * (gp[2] - hp[2]);
            if (d < bd) { bd = d; best = b; }
        }
    return best;
}
/* the furthest a record's root (bone 0) goes from its start along the
 * ground, in key units; -1 if it has no position keys */
static float bone_travel(const unsigned char *r, uint32_t lim, uint32_t bone)
{
    uint32_t bt0 = rd32(r, 0x20) + 4 * bone, bo, nr, np, q, k0;
    float x0, y0, m = 0;
    if (bt0 + 4 > lim || !(bo = rd32(r, bt0)) || bo + 4 > lim) return -1;
    nr = r[bo]; np = r[bo + 1];
    if (!np || bo + 4 + 8 * (nr + np) > lim) return -1;
    k0 = bo + 4 + 8 * nr;
    if (getenv("TT_ROOTKEYS") && bone == 0) {   /* testing */
        char ln[400]; int l = 0;
        for (q = 0; q < np && q < 12; q++) l += snprintf(ln + l, sizeof ln - l, " [%u %d %d %d]", rd16(r, k0 + 8 * q), (int16_t)rd16(r, k0 + 8 * q + 2), (int16_t)rd16(r, k0 + 8 * q + 4), (int16_t)rd16(r, k0 + 8 * q + 6));
        tt_dbg("[ROOTK] %u keys:%s\n", np, ln);
    }
    x0 = (int16_t)rd16(r, k0 + 2); y0 = (int16_t)rd16(r, k0 + 4);
    for (q = 0; q < np; q++) {
        float dx = (int16_t)rd16(r, k0 + 8 * q + 2) - x0, dy = (int16_t)rd16(r, k0 + 8 * q + 4) - y0, d = sqrtf(dx * dx + dy * dy);
        if (d > m) m = d;
    }
    return m;
}
static float root_travel(const unsigned char *r, uint32_t lim) { return bone_travel(r, lim, 0); }
/* a motion's root: where it ends up from where it starts, along the ground
 * (bone 0's first and last position keys; 0 if it has none) */
static int root_net(const unsigned char *r, uint32_t lim, float *dx, float *dy)
{
    uint32_t bt, o, nr, np, p0, p1;
    *dx = *dy = 0;
    if (lim < 0x30) return 0;
    bt = rd32(r, 0x20);
    if (bt + 4 > lim) return 0;
    o = rd32(r, bt);
    if (!o || o + 4 > lim) return 0;
    nr = r[o]; np = r[o + 1];
    if (!np) return 0;
    p0 = o + 4 + 8 * nr; p1 = p0 + 8 * (np - 1);
    if (p1 + 8 > lim) return 0;
    *dx = ((int16_t)rd16(r, p1 + 2) - (int16_t)rd16(r, p0 + 2)) / 256.0f;
    *dy = ((int16_t)rd16(r, p1 + 4) - (int16_t)rd16(r, p0 + 4)) / 256.0f;
    return 1;
}
/* Retroceso jugable del troll: usar su paso atras y conservar el recorrido
 * y el reloj de la accion del jugador. No sustituir saltos/interacciones.
 * Fallar cerrado si un banco no contiene los dos desplazamientos previstos. */
static int npc_backstep(int g,unsigned slot,const unsigned char *a,size_t alen,
 const unsigned char *G,size_t glen){
    if(g<0||g>=N_EXTRA||strcmp(s_pools[g].name,"troll")||slot!=11||alen<64||glen<0x58)return 0;
    unsigned n=rd16(G,6),tab=rd32(G,0x10);if(n<2||tab>glen||8>glen-tab)return 0;
    unsigned off=rd32(G,tab+4);if(!off||off>glen||64>glen-off)return 0;
    const unsigned char *r=G+off;unsigned rl=rd32(r,0x14),du=rd32(r,8);
    if(rl<64||rl>glen-off||du<400||du>1500||rd32(a,0x14)>alen||!rd32(a,8))return 0;
    float hx,hy,gx,gy;
    if(!root_net(a,(uint32_t)alen,&hx,&hy)||!root_net(r,rl,&gx,&gy))return 0;
    return fabsf(hx)<.25f&&hy>1&&fabsf(gx)<.25f&&gy>1&&hy/gy<=2.5f;
}
/* the hero stays in place for this move where the guest's own goes on */
static int hero_still(const unsigned char *a, const unsigned char *gr, uint32_t glim)
{
    float h = root_travel(a, rd32(a, 0x14)), g = root_travel(gr, glim);
    return h >= 0 && g > 0 && h * 4 < g;
}
#include "heroes_motion_quality.h"
#include "heroes_troll_reaction.h"
#include "heroes_troll_throw.h"
#include "heroes_ground_reaction.h"
/* the shots' own sounds (1.7): which (synth_shot's kinds: 0 Gandalf's bolt,
 * 1 Sauron's, 2 a stone, 3 a burning rock), or -1 (the bow's, as the hero) */
#define SHOT_SND_ID  0x7E01u   /* his shot (the bow's release) */
#define SHOT_SND_ID2 0x7E02u   /* (the arrow's flight: a breath of silence) */
static int shot_kind(int g)
{
    if (g < 0 || g >= N_EXTRA) return -1;
    if (!strcmp(s_skins[g].name, "gandalf") && getenv("TT_GANDALF_SHOTSND")) return 0;   /* (1.8: his is the bow's again, as asked) */
    if (!strcmp(s_skins[g].name, "sauron")) return 1;
    if (!strcmp(s_skins[g].name, "frodo")) return 2;
    if (!strcmp(s_skins[g].name, "uruk") || !strcmp(s_skins[g].name, "troll")) return 3;
    if (!strcmp(s_skins[g].name, "nazgul") || !strcmp(s_skins[g].name, "orc") || !strcmp(s_skins[g].name, "eomer")) return 2;   /* (Versus' three: a stone) */
    return -1;
}
/* the composed set into out (cap bytes); its size, or 0 */
static uint32_t s_hat[256];   /* (testing, TT_HITLOG: the hero's records) */
static size_t compose_anims(const unsigned char *A, size_t alen, const unsigned char *G, size_t glen, int g,
                            unsigned char *out, size_t cap)
{
    uint32_t ans = rd16(A, 6), gns = rd16(G, 6), atab = rd32(A, 0x10), gtab = rd32(G, 0x10), s, first = 0xFFFFFFFFu;
    uint32_t gt[256], at[256], nbG, pos, used[AC_N], hand[2], ghand[2], hfoot, gfoot;
    int sh, sg, sp;
    static unsigned char *ovr[256];
    if (memcmp(A, "aniz", 4) || memcmp(G, "aniz", 4) || ans > 256 || gns > 256 || atab + 4 * ans > alen || gtab + 4 * gns > glen) return 0;
    for (s = 0; s < gns; s++) { gt[s] = rd32(G, gtab + 4 * s); if (gt[s] && gt[s] < first) first = gt[s]; }
    for (s = 0; s < ans; s++) at[s] = rd32(A, atab + 4 * s);
    if (first == 0xFFFFFFFFu || first > glen || first > cap) return 0;
    nbG = rd32(G, 0x40 + 8);
    if (getenv("TT_STABSCAN")) {   /* testing: each hero motion's sword hand, which way its axes point (z: up) */
        static float P[128][3];
        uint32_t hb = skel_point_bone(A, alen, 0);
        for (s = 0; s < ans && hb < 128; s++) {
            uint32_t r = at[s], du; char ln[300]; int l = 0, q;
            if (!r || r + 0x30 > alen) continue;
            du = rd32(A, r + 8);
            for (q = 1; q <= 3; q++) {
                float x[3] = { 1, 0, 0 }, y[3] = { 0, 1, 0 }, z[3] = { 0, 0, 1 }, a[3], b[3], c[3];
                if (rt_pose(A, alen, A + r, alen - r, du * q / 4.0f, P) <= (int)hb) break;
                qrotv(s_pose_Q[hb], x, a); qrotv(s_pose_Q[hb], y, b); qrotv(s_pose_Q[hb], z, c);
                l += snprintf(ln + l, sizeof ln - l, " | x%+.2f y%+.2f z%+.2f hz %.1f", a[2], b[2], c[2], P[hb][2] - P[0][2]);
            }
            tt_dbg("[STAB] slot %u dur %u%s\n", s, du, ln);
        }
    }
    {   /* testing: heroes\<name>\anim_<slot>.bin -- a ready motion for a hero slot */
        char pth[700]; size_t l2;
        paths();
        for (s = 0; s < ans && s < 256; s++) {
            ovr[s] = NULL;
            snprintf(pth, sizeof pth, "%s/heroes/%s/anim_%u.bin", s_dir, s_pools[g].name, s);
            if ((ovr[s] = read_all(pth, &l2)) && (l2 < 0x40 || memcmp(ovr[s], "anim", 4))) { free(ovr[s]); ovr[s] = NULL; }
            if(!ovr[s]&&!s_compose_menu&&!getenv("TT_TROLL_LEGACY_KNOCK")&&at[s])
                ovr[s]=npc_troll_reaction(g,s,A+at[s],alen-at[s],G,glen,&l2);
            if(!ovr[s]&&!s_compose_menu&&!getenv("TT_LEGACY_GROUND")&&at[s])
                ovr[s]=npc_ground_hold(g,s,A+at[s],alen-at[s],G,glen,&l2);
            if(!ovr[s]&&!s_compose_menu&&g==SK_TROLL&&at[s]&&anim_class((int)s)==AC_RANGED)   /* (1.9.9) his own throw */
                ovr[s]=troll_throw_rec(G,glen,&l2);
            /* the kick is the hero's for every guest (1.8): the game's kick
             * lands where his foot goes (knocking enemies back, and the
             * Deeping Wall's ladders off the wall), and his own swings in
             * the kick's place reached nothing -- they keep their own
             * attacks for everything else */
            int kick_all = anim_class((int)s) == AC_KICK && s_skins[g].map && !getenv("TT_OWNKICK");
            /* and the parry (1.8): his own in its place (a swing, a kick,
             * a stance) held off fewer blows than the hero's */
            if (anim_class((int)s) == AC_PARRY && s_skins[g].map && !getenv("TT_OWNPARRY")) kick_all = 1;
            if (anim_class((int)s) == AC_DEATH && s_skins[g].map && !*pool_list(g, AC_DEATH)) kick_all = 1;   /* (no fall of his own) */
            /* (1.7 final) the hero's own motions for the scenes -- the ones
             * no fight uses (not his stances, 0, 1, 33, 34 and 160): the
             * cutscenes play them by number (a look over the edge, a draw of
             * the bow at Balin's door); his own of the same number was some
             * other motion of his, or none */
            /* (1.7.3: only the long scene moves, 39 and 136 -- 37 and 38 play
             * as a fight starts, and the short ones are fight moves: their
             * own there, as in 1.6) */
            int special = (s == 39 || s == 136 || (getenv("TT_ALLSPECIAL") && anim_class((int)s) == AC_IDLE && s != 0 && s != 1 && s != 33 && s != 34 && s != 160))
                          && s_skins[g].map && !getenv("TT_OWNSPECIAL");
            if (special) kick_all = 1;
            /* (1.7 final) the finishing thrust -- the blade turned point down
             * and driven into the foe on the ground (57 its rise, 59 the
             * thrust; 191 and 193 the upgraded one's): the hero's for every
             * guest. Gandalf's, Lurtz's, Sauron's and the Uruk-hai's own
             * swings stood in for it and the blade never turned down */
            /* (1.7.3: undone -- these are in the light attacks' chain, and
             * Gandalf's and Sauron's light attacks became Aragorn's; their own
             * again, as in 1.6. TT_HEROSTAB: the hero's, testing) */
            /* (131) 't' en rtcls: la estocada del heroe para ese invitado.
             * Lurtz es espadachin como Isildur: su remate R2 eran tajos de
             * pie y la hoja nunca bajaba al enemigo caido. */
            int stab = (s == 57 || s == 59 || s == 191 || s == 193) && s_skins[g].map
                && (getenv("TT_HEROSTAB") || (strchr(s_pools[g].rtcls, 't') && !getenv("TT_OWNSTAB")));
            if (stab) kick_all = 1;
            /* Acciones ausentes: adaptar el heroe. Las posturas, incluida
             * 160 entre ataques, mantienen el estilo nativo. Los movimientos
             * nativos se validan abajo por direccion antes de sustituirlos. */
            int action_class = anim_class((int)s);
            int action_needed = s_skins[g].map &&
                (action_class == AC_IDLE && s != 0 && s != 1 && s != 33 && s != 34 && s != 160 &&
                 (s >= gns || !gt[s]));
            if (!ovr[s] && (s_pools[g].rtscale > 0 || kick_all || action_needed) && !s_compose_menu && rd32(A, atab + 4 * s) && !getenv("TT_NORETARGET")
                && !(s == 48 && !strcmp(s_pools[g].name, "uruk") && !getenv("TT_NORISING"))) {   /* (the Uruk-hai's rising attack: his own getting up, below) */
                int c = anim_class((int)s);
                const char *rc = getenv("TT_RTCLASSES") ? getenv("TT_RTCLASSES") : s_pools[g].rtcls;   /* testing: s f k r */
                if (action_needed || (c == AC_SPEED && strchr(rc, 's')) || (c == AC_FIERCE && strchr(rc, 'f')) || (c == AC_KICK && (strchr(rc, 'k') || kick_all)) || (c == AC_RANGED && strchr(rc, 'r')) || (c == AC_PARRY && (strchr(rc, 'p') || kick_all)) || (c == AC_KNOCK && strchr(rc, 'n')) || (c == AC_DEATH && kick_all) || special || stab) {
                    ovr[s] = retarget_rec(A, alen, s, G, glen, s_skins[g].map, s_skins[g].nmap, s_pools[g].rtscale > 0 ? s_pools[g].rtscale : 1.0f, &l2);
                    if (!ovr[s]) tt_dbg("[HEROES]   slot %u: could not retarget\n", s);
                    else if (getenv("TT_RTDUMP")) {   /* testing */
                        FILE *df; snprintf(pth, sizeof pth, "%s/heroes/rt_%s_%u.bin", s_dir, s_pools[g].name, s);
                        if ((df = fopen(pth, "wb"))) { fwrite(ovr[s], 1, l2, df); fclose(df); }
                    }
                }
            }
        }
    }
    memcpy(out, G, first);           /* header and his skeleton */
    pos = first;
    {   /* The playable heroes' skeletons carry two more blocks in their
         * "skel" chunk (aiming limits: the bow's and the head's), the others
         * none -- without them the hero cannot aim, and his ranged attack
         * never fires. The hero's, the same for every hero, go in before the
         * chunk's last word. */
        uint32_t gs = 0x40 + rd32(G, 0x40 + 0x14), as = 0x40 + rd32(A, 0x40 + 0x14);
        if (!memcmp(G + gs, "skel", 4) && !memcmp(A + as, "skel", 4) && !rd32(G, gs + 0x1C) && rd32(A, as + 0x1C)
            && !getenv("TT_NOAIM")) {
            uint32_t ab = rd32(A, as + 0x1C), at2 = rd32(A, as + 0x3C), L = at2 - ab, gt2 = rd32(G, gs + 0x3C);
            uint32_t gend = gs + rd32(G, gs + 0x10);
            if (L && L < 0x100 && gt2 && gs + gt2 <= gend && first + L <= cap && gend <= first) {
                memcpy(out + gs + gt2 + L, G + gs + gt2, first - (gs + gt2));        /* the tail and what follows, moved up */
                memcpy(out + gs + gt2, A + as + ab, L);                               /* the blocks */
                wr32(out, gs + 0x1C, gt2);
                wr32(out, gs + 0x38, gt2 + (rd32(A, as + 0x38) - ab));
                wr32(out, gs + 0x3C, gt2 + L);
                wr32(out, gs + 0x10, rd32(G, gs + 0x10) + L);
                wr32(out, 0x40 + 0x18, rd32(G, 0x40 + 0x18) + L);
                pos = first + L;
            } else if (L && L < 0x100 && !gt2 && gend == first && rd32(G, gs + 8) > 7 && rd32(A, as + 8) > 7 && first + L + 4 + 2 * 68 <= cap
                       && !s_compose_menu && !getenv("TT_NOAIM2")) {
                /* Boromir's skeleton has neither the chunk's last word (the
                 * blocks go in before it) nor the attach points 6 (the bones
                 * the game turns toward his target: the spine's) and 7 (head,
                 * spine, hands, pelvis, knees, feet) that every playable
                 * hero's has, and the other guests' too. All go at the
                 * chunk's end (nothing follows it): the blocks, the hero's
                 * last word, and the hero's two lists, his bones for the
                 * hero's. */
                uint32_t sz = rd32(G, gs + 0x10), p0 = gs + sz, added, k, t = gs + rd32(G, gs + 0xC), ht = as + rd32(A, as + 0xC);
                memcpy(out + p0, A + as + ab, L);
                memcpy(out + p0 + L, A + as + at2, 4);
                wr32(out, gs + 0x1C, sz);
                wr32(out, gs + 0x38, sz + (rd32(A, as + 0x38) - ab));
                wr32(out, gs + 0x3C, sz + L);
                added = L + 4;
                for (k = 6; k <= 7; k++) {
                    uint32_t lo = rd32(A, ht + 4 * k), n, i, lp;
                    if (rd32(out, t + 4 * k) || !lo) continue;
                    n = rd32(A, ht + lo);
                    if (n > 16) continue;
                    lp = p0 + added;
                    wr32(out, lp, n);
                    for (i = 0; i < n; i++) wr32(out, lp + 4 + 4 * i, guest_bone_for(A, alen, G, glen, g, rd32(A, ht + lo + 4 + 4 * i)));
                    wr32(out, t + 4 * k, lp - t);
                    added += 4 + 4 * n;
                    tt_dbg("[HEROES]   attach point %u: %u bone(s), his %u...\n", k, n, rd32(out, lp + 4));
                }
                wr32(out, gs + 0x10, sz + added);
                wr32(out, 0x40 + 0x18, rd32(G, 0x40 + 0x18) + added);
                pos = first + added;
                tt_dbg("[HEROES]   the aiming blocks added at his skeleton's end (%u bytes)\n", added);
            }
        }
    }
    {   /* the attach points every playable hero has and his skeleton lacks
         * (4 head, 6 the spine turned to the target, 7 head, spine, hands,
         * pelvis, knees and feet, 8 head): the hero's lists, his bones for
         * the hero's, at the chunk's end. Sauron's had only 0-3 and 5: with
         * no 7 he fell through the ground (Caradhras), Elrond lacked 4, 6, 7. */
        uint32_t gs = 0x40 + rd32(out, 0x40 + 0x14), as = 0x40 + rd32(A, 0x40 + 0x14);
        if (g >= 4 && g < N_EXTRA && !s_compose_menu && !getenv("TT_NOPTADD") && !memcmp(out + gs, "skel", 4) && !memcmp(A + as, "skel", 4)
            && gs + rd32(out, gs + 0x10) == pos && rd32(out, gs + 8) > 8 && rd32(A, as + 8) > 8) {
            uint32_t t = gs + rd32(out, gs + 0xC), ht = as + rd32(A, as + 0xC), added = 0, k;
            static const uint32_t want[] = { 4, 6, 7, 8 };
            for (k = 0; k < sizeof want / sizeof want[0]; k++) {
                uint32_t q = want[k], lo = rd32(A, ht + 4 * q), n, i, lp = pos + added;
                if (rd32(out, t + 4 * q) || !lo) continue;
                n = rd32(A, ht + lo);
                if (n > 16 || lp + 4 + 4 * n > cap) continue;
                wr32(out, lp, n);
                for (i = 0; i < n; i++) wr32(out, lp + 4 + 4 * i, guest_bone_for(A, alen, G, glen, g, rd32(A, ht + lo + 4 + 4 * i)));
                wr32(out, t + 4 * q, lp - t);
                added += 4 + 4 * n;
                tt_dbg("[HEROES]   attach point %u added: %u bone(s), his %u...\n", q, n, rd32(out, lp + 4));
            }
            if (added) {
                wr32(out, gs + 0x10, rd32(out, gs + 0x10) + added);
                wr32(out, 0x40 + 0x18, rd32(out, 0x40 + 0x18) + added);
                pos += added;
            }
        }
    }
    {   /* his third weapon: the weapon op puts script slot 2 on the skeleton's
         * attach point 8 (the head's) and slot 3 on point 12 (none in the
         * guests' skeletons) -- point 12 made the bow hand's (point 0's
         * bone list), so Boromir's shield is on his left arm */
        uint32_t gs = 0x40 + rd32(G, 0x40 + 0x14);
        int xs = getenv("TT_XSLOT") ? atoi(getenv("TT_XSLOT")) : (g >= 0 && g < N_SKINS && s_skins[g].xmesh ? s_skins[g].xslot : 0);
        if (xs == 3 && !memcmp(out + gs, "skel", 4) && rd32(out, gs + 8) > 12 && !s_compose_menu) {
            uint32_t t = gs + rd32(out, gs + 0xC);
            if (rd32(out, t) && t + 4 * 13 <= first) {
                tt_dbg("[HEROES]   attach point 12 (was %X) -> point 0's (bone %u)\n", rd32(out, t + 48), rd32(out, t + rd32(out, t) + 4));
                if (getenv("TT_XENT")) wr32(out, t + 48, rd32(out, t + 4 * (uint32_t)atoi(getenv("TT_XENT"))));   /* testing */
                else wr32(out, t + 48, rd32(out, t));
            }
        }
    }
    if (g >= 0 && g < N_SKINS && !strcmp(s_skins[g].name, "sauron") && !getenv("TT_NOPTFIX")) {
        /* Sauron's skeleton (a boss's) names his helm's spike as both feet
         * (attach points 2 and 3): the game stands the hero on those, and he
         * fell through the ground on slopes (Caradhras). His ankles there. */
        uint32_t gs = 0x40 + rd32(G, 0x40 + 0x14);
        if (!memcmp(out + gs, "skel", 4) && rd32(out, gs + 8) > 3) {
            uint32_t t = gs + rd32(out, gs + 0xC), q;
            static const uint32_t fix[2][2] = { { 2, 10 }, { 3, 13 } };
            if (!getenv("TT_NOROOTLOW")) {
                /* and his root (bones 0 and 1, 6 high -- the heroes' 4.3):
                 * the game probes the ground from the root's height, and
                 * from his found the overhanging rock at Caradhras' start,
                 * stood him on it and he slid off the cliff. The bones below
                 * keep their places (the game takes their offsets from the
                 * world matrices); his root's world and inverse moved down. */
                uint32_t mo = 0x40 + rd32(out, 0x40 + 0xC), bb;
                for (bb = 0; bb < 2; bb++) {
                    float z, nz = 4.3f; uint32_t v = rd32(out, mo + bb * 128 + 64 + 56); memcpy(&z, &v, 4);
                    if (z > 5.0f) {
                        float m13; uint32_t w2 = rd32(out, mo + bb * 128 + 52); memcpy(&m13, &w2, 4);
                        memcpy(&v, &nz, 4); wr32(out, mo + bb * 128 + 64 + 56, v);
                        m13 = m13 * nz / z; memcpy(&w2, &m13, 4); wr32(out, mo + bb * 128 + 52, w2);
                    }
                }
                tt_dbg("[HEROES]   his root at the heroes' height\n");
            }
            for (q = 0; q < 2; q++) {
                uint32_t o = rd32(out, t + 4 * fix[q][0]);
                if (o && t + o + 8 <= first && rd32(out, t + o) == 1) { wr32(out, t + o + 4, fix[q][1]); tt_dbg("[HEROES]   attach point %u: bone %u\n", fix[q][0], fix[q][1]); }
            }
        }
    }
    if (g >= 0 && g < N_SKINS && getenv("TT_ONEHAND") && !s_compose_menu) {   /* testing: the bow's attach point the sword hand's */
        /* his bow's attach point his sword hand's (point 1's bone list) */
        uint32_t gs = 0x40 + rd32(G, 0x40 + 0x14);
        if (!memcmp(out + gs, "skel", 4) && rd32(out, gs + 8) > 1) {
            uint32_t t = gs + rd32(out, gs + 0xC);
            if (rd32(out, t + 4)) { wr32(out, t, rd32(out, t + 4)); tt_dbg("[HEROES]   attach point 0 -> point 1's\n"); }
        }
    }
    /* the hands' bones (attach points 0 and 1), the hero's and his */
    hand[0] = skel_point_bone(A, alen, 0); hand[1] = skel_point_bone(A, alen, 1);
    ghand[0] = skel_point_bone(out, first, 0); ghand[1] = skel_point_bone(out, first, 1);
    hfoot = skel_point_bone(A, alen, 2); gfoot = skel_point_bone(out, first, 2);
    memset(used, 0, sizeof used);
    wr16(out, 6, (uint16_t)ans);
    for (s = 0; s < ans; s++) {
        const unsigned char *a, *gr;
        uint32_t src, a5, a7, a8, a10, g2, gev, k, nev, tail, newbt, newtail, n, target;
        float scale;
        int cls;unsigned serial=0;size_t grlen;int backstep=0,native_troll=0,troll_exit=0;
        static unsigned char native_clip[1024*1024];
        if (!at[s]) continue;
        if (s_compose_menu && s_compose_hkey == MENU_ANIM_KEY && s != 0x22 && s != 0x25 && s != 0x28 && !getenv("TT_MENUALL")) { at[s] = 0; continue; }   /* the select plays only these: with seven guests the menu file grew too big to load */
        cls = anim_class((int)s);
        backstep=!s_compose_menu&&!ovr[s]&&npc_backstep(g,s,A+at[s],alen-at[s],G,glen);
        serial=used[cls];
        if ((s_compose_menu && s_compose_hkey == UPG_FIG_KEY && !getenv("TT_FIGMOVES")) || (!s_compose_menu && getenv("TT_ALLSTANCE"))) {   /* (TT_ALLSTANCE: testing, every slot of a level's set) */
            /* (1.7.2) the pause screen's figure: his stance in every slot,
             * as the select's -- his own moves in the slots that show the
             * upgrades' moves stood him in a twisted pose */
            int pk = g < N_SEL ? pool_pick(s_menu_moves[g][0], 0, gt, gns) : -1;
            if (pk < 0) pk = pool_pick("34,33,0", 0, gt, gns);
            if (pk < 0) return 0;
            src = (uint32_t)pk;
        } else
        if (s_compose_menu && (s == 0x22 || s == 0x25 || s == 0x28)) {
            /* the character select: his stance, his pose when chosen, his move when confirmed */
            int pk = g < N_SEL ? pool_pick(s_menu_moves[g][s == 0x22 ? 0 : s == 0x25 ? 1 : 2], 0, gt, gns) : -1;
            if (pk < 0) pk = pool_pick("34,33,0", 0, gt, gns);
            if (pk < 0) return 0;
            src = (uint32_t)pk;
        } else
        if (!s_compose_menu && s == 160) {
            int pk=pool_pick(pool_list(g,AC_IDLE),0,gt,gns);
            if(pk<0)pk=pool_pick("34,33,0",0,gt,gns);
            if(pk<0)return 0;src=(uint32_t)pk; /* postura de combate estable */
        } else
        if(backstep)src=1;
        else
        if (s == 48 && !strcmp(s_pools[g].name, "uruk") && 30 < gns && gt[30] && !getenv("TT_NORISING")) {
            /* the rising attack (the hero's 48: from lying to his feet,
             * striking): the Uruk-hai's own, his getting up (1.7) -- not a
             * swing from standing, which had him spring up from the ground */
            src = 30;
        } else
        if (s == 21 && !strcmp(s_pools[g].name, "lurtz") && 20 < gns && gt[20] && !getenv("TT_LURTZOWN21")) src = 20;   /* (1.9.9) his 21 is a stagger 3.8 units back and a unit up: hit, he left the ground (a playtest) -- his short flinch for both */
        else
        if (s < gns && gt[s] && cls != AC_SPEED && cls != AC_FIERCE && cls != AC_RANGED && cls != AC_KICK && cls != AC_DEATH
            && !((s == 9 || s == 10) && !strcmp(s_pools[g].name, "sauron"))) src = s;   /* (Sauron's 9 and 10 stand still: the heroes' move) */
        else {
            int pk = pool_pick(pool_list(g, cls), (int)used[cls]++, gt, gns);
            if (pk < 0) pk = pool_pick("34,0,1", 0, gt, gns);
            if (pk < 0) return 0;
            src = (uint32_t)pk;
        }
        a = A + at[s];
        if(!ovr[s]&&!s_compose_menu&&g==SK_TROLL){
            troll_exit=troll_recovery(g,s,a,rd32(a,0x14));
            unsigned pair[2];
            int melee=troll_damage_pair(a,rd32(a,0x14),pair);
            /* (131) El 43 NPC es un barrido largo: su maza no toca nada entre
             * 1,5 y 3,5 delante. Los ligeros que lo recibian (47/59/86/191,
             * segundo y siguientes golpes del combo) fallaban de cerca; usan
             * el 45, que cubre todo alrededor a 1,5-2 y delante hasta 4. */
            /* (133) En partida el 45 (barrido bajo) no servia: primer golpe
             * del combo inutil y fuertes 48/58 que fallaban. Los ligeros son
             * siempre el mazazo al suelo 41 completo (el que mata); los
             * fuertes, el 41 (ranura 43) o el 43, comprimidos. Nunca el 45. */
            unsigned pick=troll_exit?34:melee&&cls==AC_SPEED?41:melee&&s==43?41:melee&&(src==113||src==45)?43:src;
            if(pick>=gns||!gt[pick])return 0;
            src=pick;if(troll_exit)cls=AC_IDLE;
        }
        gr = G + gt[src];
        s_hat[s] = at[s];
        if (log_on() && (cls == AC_SPEED || cls == AC_FIERCE || cls == AC_PARRY || cls == AC_DEATH || cls == AC_KICK || cls == AC_RANGED || cls == AC_MOVE || cls == AC_IDLE)) tt_dbg("[HEROES]   slot %u (class %d) <- %s %u\n", s, cls, ovr[s] ? "the hero's, retargeted" : "his", ovr[s] ? s : src);
        if (cls == AC_MOVE && !ovr[s] && !s_compose_menu && s_skins[g].map && !getenv("TT_OWNMOVES")
            && strcmp(s_pools[g].name, "sauron")) {   /* (1.7.4: not Sauron's -- the hero's root heights on his lowered root, and he fell through the ground at the Gates of Moria) */
            /* (1.7.2) a move that takes him another way than the hero's: the
             * hero's, fitted to him. The game moves him by his motion's own
             * steps, and the moves his set lacks were all one of his (his
             * 9: Lurtz's a step to his left) -- in water, and wherever the
             * game plays them, he went sideways or back (Sauron's 4, 5, 9
             * and 10 too). His own that go the hero's way are kept */
            float hx, hy, gx, gy, hl, gl;
            root_net(a, rd32(a, 0x14), &hx, &hy);
            root_net(G + gt[src], (uint32_t)(glen - gt[src]), &gx, &gy);
            hl = sqrtf(hx * hx + hy * hy); gl = sqrtf(gx * gx + gy * gy);
            /* (1.9.7) the Versus heroes (the Nazgul, the Shield Orc, Eomer):
             * their own walks turn as they go (the orc's spun him round on a
             * stick held one way) -- every move of theirs is the hero's */
            int vsg = !strcmp(s_pools[g].name, "orc") || !strcmp(s_pools[g].name, "eomer") || !strcmp(s_pools[g].name, "nazgul");
            if (hl > 1.0f && (vsg || gl < 0.3f * hl || (hx * gx + hy * gy) < 0.7f * hl * gl)) {
                size_t l2;
                ovr[s] = retarget_rec(A, alen, s, G, glen, s_skins[g].map, s_skins[g].nmap, 1.0f, &l2);
                tt_dbg("[HEROES]   slot %u: his %u goes %.1f,%.1f, the hero's %.1f,%.1f: the hero's, retargeted%s\n", s, src, gx, gy, hx, hy, ovr[s] ? "" : " (failed)");
            }
        }
        if (ovr[s]) gr = ovr[s];   /* a motion made for this slot (retargeted) */
        grlen=ovr[s]?rd32(gr,0x14):glen-gt[src];
        native_troll=!ovr[s]&&!s_compose_menu&&troll_native_attack(g,cls,a,rd32(a,0x14),gr,grlen);
        /* (132) los ligeros, el clip NPC completo y a su ritmo: comprimida, la
         * preparacion parecia un golpe inventado. Los fuertes, comprimidos. */
        if(native_troll&&cls!=AC_SPEED&&!getenv("TT_TROLLFULLWIND")){
            /* (126) arma abre 238-630 ms (antes 566-900), segun clip. */
            unsigned open=0,w=0;size_t cut=troll_attack_wind(G,glen,gr,grlen,native_clip,sizeof native_clip,&open,&w);
            if(cut){gr=native_clip;grlen=cut;tt_dbg("[TROLL-NPC] slot %u <- %u: arma abre %u -> %u ms\n",s,src,open,w);}
        }
        if(!native_troll&&!ovr[s]&&!s_compose_menu&&(cls==AC_SPEED||cls==AC_FIERCE)){
            /* El ataque 66 de Elrond/guerrero tiene dos golpes: empezar
             * fuerte por el segundo para diferenciar ambos botones. */
            unsigned part=serial+((src==66&&cls==AC_FIERCE)?1:0);
            unsigned begin=0;size_t cut=npc_attack_slice(g,src,part,G,glen,gr,grlen,native_clip,sizeof native_clip,&begin);
            if(cut){gr=native_clip;grlen=cut;tt_dbg("[NPC] %s slot %u <- nativo %u, fragmento desde %u ms, %u ms\n",s_skins[g].name,s,src,begin,rd32(gr,8));}
        }
        a5 = rd32(a, 0x14); a7 = rd32(a, 0x1C); a8 = rd32(a, 0x20); a10 = rd32(a, 0x28);
        g2 = rd32(gr, 8); gev = rd32(gr, 0x1C);
        if (a10 > a5 || a7 > a8 || at[s] + a5 > alen || gev > grlen) return 0;
        nev = rd32(a, 0x18);
        tail = a5 - a10;
        n = gev + (a8 - a7) + 4 * nbG + tail;
        pos = (pos + 15) & ~15u;
        if (pos + n > cap) return 0;
        {   /* a move of his for one of the hero's: played at near the hero's pace
             * (attacks no slower than a little over his: they must answer the
             * button as the hero's do) */
            uint32_t ad = rd32(a, 8), lo = ad * 9 / 10, hi = !ovr[s] && (cls==AC_SPEED ||cls==AC_FIERCE) ? mq_attack_limit(g,ad) : ad * 115 / 100 + 50;
            if ((cls == AC_SPEED || cls == AC_FIERCE || cls == AC_KICK || cls == AC_RANGED || cls == AC_PARRY) && ad && !getenv("TT_NOSTRETCH"))
                target = g2 < lo ? lo : g2 > hi ? hi : g2;
            else target = g2;
            if(backstep)target=ad; /* misma respuesta/ventana de la accion jugable */
            if(native_troll)target=g2; /* preparacion, golpe y recuperacion NPC completos */
            if(troll_exit)target=ad; /* postura propia durante la salida, reloj del combo intacto */
            if ((cls == AC_HIT || cls == AC_KNOCK) && ad && !s_compose_menu && !getenv("TT_NOTOUGH")) {
                /* Sauron shrugs blows off: his flinch over in about half
                 * the hero's time, so he is not held in place; Gandalf in a
                 * little more, Lurtz a little more again (1.7) */
                const char *nm = s_pools[g].name;
                uint32_t q2 = g2 < ad ? g2 : ad;
                int pct = !strcmp(nm, "sauron") ? 50 : !strcmp(nm, "gandalf") ? 60 : !strcmp(nm, "lurtz") ? 52 : 0;   /* (1.7.2: Lurtz 72 -> 52) */
                /* La caida/levantada nueva de Lurtz debe verse completa;
                 * comprimirla al 52% producia otra sacudida. */
                if(g==3&&cls==AC_KNOCK)pct=0;
                if (pct) { q2 = q2 * (uint32_t)pct / 100; target = q2 < 150 ? 150 : q2; }
            }
            /* Troll, Frodo y berserker: respetar el reloj de sus pasos
             * propios. La compensacion de zancada comprimia hasta al 60%
             * el paseo del troll y lo convertia en una carrera. La raiz
             * nativa sigue siendo la que desplaza al actor: no se modifica
             * la velocidad global ni se mueve al jugador tras la colision. */
            int native_gait = !strcmp(s_pools[g].name,"troll") || !strcmp(s_pools[g].name,"frodo") || !strcmp(s_pools[g].name,"uruk");
            if (!native_gait && (cls == AC_MOVE || ((s == 0 || s == 1) && !strcmp(s_pools[g].name, "sauron"))) && !ovr[s] && ad && g2 && !s_compose_menu && !getenv("TT_NOCADENCE")) {   /* Sauron conserva su comportamiento de la entrega anterior. */
                /* walks and runs: his steps' length over the hero's, so his
                 * feet keep pace with the ground (the game moves him at the
                 * hero's speed) */
                float lh = stride_len(A, alen, a, alen - at[s], (int)hfoot, ad), lg = stride_len(G, glen, gr, grlen, (int)gfoot, g2);
                if (lh > 0.3f && lg > 0.3f) {
                    float t2 = (float)ad * lg / lh;
                    if (t2 < g2 * 0.6f) t2 = g2 * 0.6f;
                    if (t2 > g2 * 1.6f) t2 = g2 * 1.6f;
                    target = (uint32_t)(t2 + 0.5f);
                    tt_dbg("[HEROES]   slot %u: stride %.2f (hero %.2f): %u ms (his %u, hero %u)\n", s, lg, lh, target, g2, ad);
                }
            }
        }
        scale = rd32(a, 8) ? (float)target / (float)rd32(a, 8) : 1.0f;
        /* his swing's strike at the hero's strike moment: the hero's events
         * (the strike, the swoosh, the next move's input window) timed by a
         * piecewise map that puts the hero's strike (its first strike event,
         * type 5) on his swing's fastest moment */
        sh = -1; sg = -1; sp = -1;
        if (!native_troll && !ovr[s] && (cls == AC_SPEED || cls == AC_FIERCE || cls == AC_KICK) && !s_compose_menu && !getenv("TT_NOALIGN")) {
            uint32_t k2;
            for (k2 = 0; k2 < nev && 4 * k2 < a8 - a7; k2++) {
                uint32_t eo2 = a7 + rd32(a, a7 + 4 * k2);
                if (eo2 + 16 <= a8 && rd32(a, eo2 + 12) == 5) { int t0 = (int)rd32(a, eo2); if (sh < 0 || t0 < sh) sh = t0; }
            }
            if (sh > 0 && (uint32_t)sh < rd32(a, 8)) {
                float speed;
                /* Los golpes de melee proceden de la mano del arma (0).
                 * Antes, la mano libre podia elegir el instante del golpe:
                 * un gesto de la mano libre podia adelantar el impacto. */
                int pk2 = npc_weapon_peak(G,glen,gr,grlen,(int)ghand[0],g2,&speed);
                if (pk2 > 0 && g2) sp = (int)((float)pk2 * (float)target / (float)g2 + 0.5f);   /* his peak, in the new length */
                if (sp > 0 && sp < (int)target) {
                    /* the strike lands near the hero's moment (a little of his
                     * own wind-up kept): his motion warped to it, the events too */
                    int hs = (int)((float)sh * scale + 0.5f);
                    /* (1.7: on the hero's moment exactly -- 30% of his own
                     * lateness kept, the blow came after the hit window and
                     * missed; TT_STRIKEKEEP=1 the old way) */
                    sg = getenv("TT_STRIKEKEEP") ? (sp > hs ? hs + (sp - hs) * 3 / 10 : sp) : hs;
                    if (sg < 1) sg = 1;
                    if (sg >= (int)target) sg = (int)target - 1;
                    tt_dbg("[HEROES]   slot %u (his %u): hero strike %d ms, his swing %d -> %d of %u\n", s, src, sh, sp, sg, target);
                } else sh = -1;
            } else sh = -1;
        }
        memcpy(out + pos, gr, gev);                                   /* his motion */
        if ((target != g2 || sh > 0) && g2) {   /* its keys' times (u16 first in each 8-byte key) to the new length (and his swing to the strike) */
            float ks = (float)target / (float)g2;
            uint32_t bi, bt0 = rd32(gr, 0x20);
            for (bi = 0; bi < nbG; bi++) {
                uint32_t bo = rd32(gr, bt0 + 4 * bi), nk, q;
                if (!bo || bo + 4 > gev) continue;
                nk = (uint32_t)gr[bo] + gr[bo + 1] + gr[bo + 2] + gr[bo + 3];
                for (q = 0; q < nk && bo + 4 + 8 * q + 2 <= gev; q++) {
                    uint32_t ko = bo + 4 + 8 * q;
                    unsigned packed=rd16(gr,ko);
                    float kt = (packed&0x7fffu) * ks;
                    if (sh > 0 && sp > 0) kt = kt <= sp ? kt * (float)sg / (float)sp : (float)sg + (kt - sp) * ((float)target - sg) / ((float)target - sp);
                    unsigned time=(unsigned)(kt+0.5f);if(time>32767)time=32767;
                    wr16(out + pos, ko, (uint16_t)((packed&0x8000u)|time));
                }
            }
        }
        if (!native_troll && !ovr[s] && (cls == AC_SPEED || cls == AC_FIERCE || cls == AC_KICK || cls == AC_RANGED || cls == AC_PARRY || cls == AC_IDLE
                        || backstep || (cls == AC_MOVE && hero_still(a, gr, (uint32_t)grlen)))
            && !s_compose_menu && rd32(a, 8) && !getenv("TT_OWNROOT")) {
            /* (1.9.1: also his aiming, parrying and standing moves, and a
             * move of his in a slot where the hero stays where he is -- his
             * own steps there walked him forward on their own: Lurtz,
             * Sauron and Gandalf as they aimed, Boromir standing) */
            /* his own swing, the hero's footwork: his root's moves along the
             * ground (bone 0's position keys, x and y) the hero's for this
             * move, so he steps, lunges and turns to where the hero does --
             * the game aims the hero's blows from there. His own (Sauron's,
             * the Uruk-hai's sidelong step) took him off his target. */
            /* (1.9.1: his bones' table is after his events -- the test here
             * had it before, and his own steps were never replaced) */
            uint32_t bt0 = rd32(gr, 0x20), bo = (size_t)bt0 + 4 <= grlen ? rd32(gr, bt0) : 0, abt = rd32(a, 0x20), hbo = rd32(a, abt);
            if (bo && bo + 4 <= gev) {   /* (the hero's root with no keys there: his at the start, where the hero's stays) */
                uint32_t nr = gr[bo], np = gr[bo + 1], hnr = hbo ? a[hbo] : 0, hnp = hbo ? a[hbo + 1] : 0, q;
                float hs2 = (float)target / (float)rd32(a, 8);
                for (q = 0; q < np && bo + 4 + 8 * (nr + q) + 8 <= gev; q++) {
                    uint32_t ko = bo + 4 + 8 * (nr + q);
                    float th = (float)rd16(out + pos, ko) / hs2, x = 0, y = 0;
                    if (hnp) {   /* the hero's root there (linear between his keys) */
                        uint32_t j, k0 = hbo + 4 + 8 * hnr;
                        for (j = 0; j + 1 < hnp && rd16(a, k0 + 8 * (j + 1)) < th; j++) {}
                        if (j + 1 < hnp && rd16(a, k0 + 8 * (j + 1)) > rd16(a, k0 + 8 * j)) {
                            float t0 = rd16(a, k0 + 8 * j), t1 = rd16(a, k0 + 8 * (j + 1)), f = (th - t0) / (t1 - t0);
                            if (f < 0) f = 0; if (f > 1) f = 1;
                            x = (int16_t)rd16(a, k0 + 8 * j + 2) * (1 - f) + (int16_t)rd16(a, k0 + 8 * (j + 1) + 2) * f;
                            y = (int16_t)rd16(a, k0 + 8 * j + 4) * (1 - f) + (int16_t)rd16(a, k0 + 8 * (j + 1) + 4) * f;
                        } else { x = (int16_t)rd16(a, k0 + 8 * j + 2); y = (int16_t)rd16(a, k0 + 8 * j + 4); }
                    }
                    wr16(out + pos, ko + 2, (uint16_t)(int16_t)lroundf(x));
                    wr16(out + pos, ko + 4, (uint16_t)(int16_t)lroundf(y));
                }
            }
        }
        memcpy(out + pos, a, 0x40);                                   /* the hero's header */
        memcpy(out + pos + gev, a + a7, a8 - a7);                     /* the hero's events */
        for (k = 0; k < nev && 4 * k < a8 - a7; k++) {
            uint32_t eo = gev + rd32(a, a7 + 4 * k);
            if (eo + 4 <= gev + (a8 - a7)) wr32(out + pos, eo, strike_map(rd32(out + pos, eo), scale, sh, sg, rd32(a, 8), target));
        }
        newbt = gev + (a8 - a7);
        memcpy(out + pos + newbt, gr + rd32(gr, 0x20), 4 * nbG);       /* his bones' table */
        newtail = newbt + 4 * nbG;
        memcpy(out + pos + newtail, a + a10, tail);                   /* the hero's tail */
        if (rd32(a, 0x30)) {
            uint32_t a13 = rd32(a, 0x34), j;
            for (j = 0; j < rd32(a, 0x30) && a13 + 4 * j + 4 <= a5; j++) {
                /* {time, bone}: a strike's window and the bone it hits
                 * from -- the hero's sword hand (36) is another bone on
                 * the guest's skeleton (Boromir's upper arm): his own */
                uint32_t o2 = newtail + (a13 - a10) + 4 * j, hbn = rd16(out + pos, o2 + 2), gbn = hbn;
                wr16(out + pos, o2, (uint16_t)strike_map(rd16(out + pos, o2), scale, sh, sg, rd32(a, 8), target));
                if (!getenv("TT_NOHITBONE")) {
                    if (getenv("TT_HITBONE")) gbn = (uint32_t)atoi(getenv("TT_HITBONE"));   /* testing */
                    else if (hbn == hand[0] && ghand[0] != 0xFFFFu) gbn = ghand[0];
                    else if (hbn == hand[1] && ghand[1] != 0xFFFFu) gbn = ghand[1];
                    else if (g >= 0 && g < N_SKINS && s_skins[g].map) {   /* the last of his bones the map gives it */
                        int q2;
                        for (q2 = s_skins[g].nmap - 1; q2 >= 0; q2--) if (s_skins[g].map[q2] == hbn) { gbn = (uint32_t)q2; break; }
                    }
                    if (gbn >= nbG) gbn = hbn < nbG ? hbn : 0;
                    if (getenv("TT_HITLOG")) {   /* testing: each strike's bone, the hero's and his, where they sit in the bind pose */
                        float hp[3], gp[3];
                        skel_bind_pos(A, hbn, hp); skel_bind_pos(out, gbn, gp);
                        tt_dbg("[HITB] slot %u cls %d t %u hero %u (%.2f %.2f %.2f) guest %u (%.2f %.2f %.2f)\n", s, anim_class((int)s), rd16(out + pos, o2), hbn, hp[0], hp[1], hp[2], gbn, gp[0], gp[1], gp[2]);
                    }
                    wr16(out + pos, o2 + 2, (uint16_t)gbn);
                }
            }
            wr32(out + pos, 0x34, newtail + (a13 - a10));
            if (rd32(a, 0x38)) wr32(out + pos, 0x38, newtail + (rd32(a, 0x38) - a10));
        }
        if (rd32(a, 0x2C)) wr32(out + pos, 0x2C, newtail + (rd32(a, 0x2C) - a10));
        if (rd32(a, 0x24) != 0xFFFFu) wr32(out + pos, 0x24, strike_map(rd32(a, 0x24), scale, sh, sg, rd32(a, 8), target));
        wr32(out + pos, 8, target);
        wr32(out + pos, 0x14, n);
        wr32(out + pos, 0x1C, gev);
        wr32(out + pos, 0x20, newbt);
        wr32(out + pos, 0x28, newtail);
        if(native_troll){
            if(!troll_native_damage(out+pos,n,gr,grlen))return 0;
            tt_dbg("[TROLL-NPC] slot %u <- %u: %u ms completos, mano/ordenes/tiempos originales\n",s,src,target);
        }else if(!s_compose_menu && !getenv("TT_NOEASE")) mq_ease(out+pos,n,G,glen,cls);
        if (getenv("TT_ROOTLOG") && !s_compose_menu)   /* testing: how far each move's root and its child go, his and the hero's */
            tt_dbg("[ROOT] slot %u class %d src %u%s: his %.0f %.0f %.0f  hero %.0f %.0f %.0f\n", s, cls, src, ovr[s] ? " (retargeted)" : "",
                   bone_travel(out + pos, n, 0), bone_travel(out + pos, n, 1), bone_travel(out + pos, n, 2), bone_travel(a, a5, 0), bone_travel(a, a5, 1), bone_travel(a, a5, 2));
        if (getenv("TT_ZSCAN") && !s_compose_menu) {   /* testing: each composed move's root height (lowest, and over the lowest bone), his and the hero's */
            static float PZ[128][3];
            float mr = 1e9f, mg = 1e9f, hr = 1e9f, hg = 1e9f, t3, lmin = 1e9f, lmax = -1e9f; int nb4, b4;
            for (t3 = 0; t3 <= (float)target; t3 += 33.0f) {
                nb4 = rt_pose(out, cap, out + pos, n, t3, PZ);
                if (nb4 > 0) { float lo4 = 1e9f; for (b4 = 0; b4 < nb4; b4++) if (PZ[b4][2] < lo4) lo4 = PZ[b4][2]; if (PZ[0][2] < mr) mr = PZ[0][2]; if (PZ[0][2] - lo4 < mg) mg = PZ[0][2] - lo4; if (lo4 < lmin) lmin = lo4; if (lo4 > lmax) lmax = lo4; }
            }
            {   int m; for (m = 1; m <= 2; m++) { float l0 = 1e9f, l1 = -1e9f, h0 = 1e9f, h1 = -1e9f, t5; s_pose_noroot = m;
                    for (t5 = 0; t5 <= (float)target; t5 += 33.0f) { float v5 = rt_lowest(out, cap, out + pos, n, t5); if (v5 < l0) l0 = v5; if (v5 > l1) l1 = v5; }
                    for (t5 = 0; t5 <= (float)rd32(a, 8); t5 += 33.0f) { float v5 = rt_lowest(A, alen, a, a5, t5); if (v5 < h0) h0 = v5; if (v5 > h1) h1 = v5; }
                    s_pose_noroot = 0;
                    tt_dbg("[ZSCAN] slot %u noroot%d: his lowest %.2f..%.2f, the hero's %.2f..%.2f\n", s, m, l0, l1, h0, h1); } }
            tt_dbg("[ZSCAN] slot %u: his lowest point from %.2f to %.2f (off the ground %.2f), at the end %.2f (the hero's: standing %.2f, at the end %.2f)\n", s, lmin, lmax, lmax - lmin, rt_lowest(out, cap, out + pos, n, (float)target), rt_stand_low(A, alen), rt_lowest(A, alen, a, a5, (float)rd32(a, 8)));
            for (t3 = 0; t3 <= (float)rd32(a, 8); t3 += 33.0f) {
                nb4 = rt_pose(A, alen, a, a5, t3, PZ);
                if (nb4 > 0) { float lo4 = 1e9f; for (b4 = 0; b4 < nb4; b4++) if (PZ[b4][2] < lo4) lo4 = PZ[b4][2]; if (PZ[0][2] < hr) hr = PZ[0][2]; if (PZ[0][2] - lo4 < hg) hg = PZ[0][2] - lo4; }
            }
            tt_dbg("[ZSCAN] slot %u class %d src %u%s: his root low %.2f (over lowest %.2f)  hero %.2f (%.2f)\n", s, cls, src, ovr[s] ? " (retargeted)" : "", mr, mg, hr, hg);
        }
        at[s] = pos;
        pos += n;
    }
    if (getenv("TT_SLOTSCAN")) {   /* testing: each motion's top (the highest bone) at its start and end, his and the hero's */
        int w2;
        for (w2 = 0; w2 < 2; w2++) {
            const unsigned char *S2 = w2 ? G : A; size_t L2 = w2 ? glen : alen; uint32_t tb = w2 ? gtab : atab, ns2 = w2 ? gns : ans, q;
            static float PP[128][3];
            for (q = 0; q < ns2; q++) {
                uint32_t r = rd32(S2, tb + 4 * q), du; float top[2], lo[2]; int e, nb2, b2;
                if (!r || r + 0x40 > L2) continue;
                du = rd32(S2, r + 8);
                for (e = 0; e < 2; e++) {
                    nb2 = rt_pose(S2, L2, S2 + r, L2 - r, e ? (float)du : 0.0f, PP);
                    top[e] = -1e9f; lo[e] = 1e9f;
                    for (b2 = 0; b2 < nb2; b2++) { if (PP[b2][2] > top[e]) top[e] = PP[b2][2]; if (PP[b2][2] < lo[e]) lo[e] = PP[b2][2]; }
                }
                {
                    int hb = (int)skel_point_bone(S2, L2, 1); float spd = 0, r0[3] = { 0, 0, 0 }, r1[3] = { 0, 0, 0 }, reach = -1e9f, tt2; int pk = -1;
                    if (hb >= 0 && hb < 128) {
                        pk = swing_peak(S2, L2, S2 + r, L2 - r, hb, du, &spd);
                        for (tt2 = 0; tt2 <= (float)du; tt2 += 33.0f) {
                            int nb3 = rt_pose(S2, L2, S2 + r, L2 - r, tt2, PP);
                            if (nb3 > hb && -(PP[hb][1] - PP[0][1]) > reach) reach = -(PP[hb][1] - PP[0][1]);
                            if (tt2 == 0) { r0[0] = PP[0][0]; r0[1] = PP[0][1]; }
                            r1[0] = PP[0][0]; r1[1] = PP[0][1];
                        }
                    }
                    if (!w2 && getenv("TT_TAILDUMP")) {   /* testing: the hero's record's header and tail */
                    uint32_t hq, tl0 = rd32(S2, r + 0x28), sz = rd32(S2, r + 0x14);
                    char hx[300]; int l2 = 0;
                    for (hq = 0; hq < 16; hq++) l2 += snprintf(hx + l2, sizeof hx - l2, "%X ", rd32(S2, r + 4 * hq));
                    tt_dbg("[TAIL] %u hdr %s\n", q, hx);
                    l2 = 0;
                    for (hq = tl0; hq < sz && hq < tl0 + 0x60; hq += 2) l2 += snprintf(hx + l2, sizeof hx - l2, "%04X ", rd16(S2, r + hq));
                    tt_dbg("[TAIL] %u tail %s\n", q, hx);
                }
                {   /* (1.9.9) how high his lowest point goes during it: off the ground */
                    float lmx = -1e9f, t4; int b5, n5;
                    for (t4 = 0; t4 <= (float)du; t4 += 33.0f) { float lo5 = 1e9f; n5 = rt_pose(S2, L2, S2 + r, L2 - r, t4, PP); for (b5 = 0; b5 < n5; b5++) if (PP[b5][2] < lo5) lo5 = PP[b5][2]; if (n5 > 0 && lo5 > lmx) lmx = lo5; }
                    tt_dbg("[SLOTLIFT] %s %u highest low %.2f\n", w2 ? "his " : "hero", q, lmx);
                }
                tt_dbg("[SLOT] %s %u dur %u height %.2f -> %.2f (low %.2f -> %.2f) swing %.2f at %d reach %.2f root moves %.2f fwd %.2f side\n", w2 ? "his " : "hero", q, du, top[0] - lo[0], top[1] - lo[1], lo[0], lo[1],
                           sqrtf(spd), pk, reach, -(r1[1] - r0[1]), r1[0] - r0[0]);
                }
            }
        }
    }
    if (getenv("TT_HITLOG")) {   /* testing: each kick strike's bone, the hero's and his, posed at the strike */
        for (s = 0; s < ans; s++) {
            static float HP[128][3], GP[128][3];
            const unsigned char *hr, *gr2; uint32_t j, ns, so, hso;
            if (!at[s] || anim_class((int)s) != AC_KICK || !s_hat[s]) continue;
            hr = A + s_hat[s]; gr2 = out + at[s];
            ns = rd32(gr2, 0x30); so = rd32(gr2, 0x34); hso = rd32(hr, 0x34);
            for (j = 0; j < ns; j++) {
                uint32_t gt2 = rd16(gr2, so + 4 * j), gb = rd16(gr2, so + 4 * j + 2), ht = rd16(hr, hso + 4 * j), hb = rd16(hr, hso + 4 * j + 2);
                int hn = rt_pose(A, alen, hr, alen - s_hat[s], (float)ht, HP), gn2 = rt_pose(out, pos, gr2, pos - at[s], (float)gt2, GP), b;
                tt_dbg("[HITP] slot %u strike %u: hero bone %u at %u ms (%.2f %.2f %.2f) | his %u at %u ms (%.2f %.2f %.2f)\n", s, j, hb, ht,
                       hn > (int)hb ? HP[hb][0] : 0, hn > (int)hb ? HP[hb][1] : 0, hn > (int)hb ? HP[hb][2] : 0, gb, gt2,
                       gn2 > (int)gb ? GP[gb][0] : 0, gn2 > (int)gb ? GP[gb][1] : 0, gn2 > (int)gb ? GP[gb][2] : 0);
                {   /* the attach points' bones there */
                    int w2;
                    for (w2 = 0; w2 < 2; w2++) {
                        const unsigned char *S2 = w2 ? out : A; float (*PP)[3] = w2 ? GP : HP; int nn = w2 ? gn2 : hn;
                        uint32_t gs = 0x40 + rd32(S2, 0x40 + 0x14), t, np, k, i;
                        if (memcmp(S2 + gs, "skel", 4)) continue;
                        t = gs + rd32(S2, gs + 0xC); np = rd32(S2, gs + 8);
                        if (j == 0) {
                            uint32_t q, sz = rd32(S2, gs + 0x10);
                            char hx[200]; int l = 0;
                            for (q = 0; q < 16; q++) l += snprintf(hx + l, sizeof hx - l, "%X ", rd32(S2, gs + 4 * q));
                            tt_dbg("[HITP]   %s skel %s\n", w2 ? "his " : "hero", hx);
                            for (q = 0x40; q < sz && q < 0x600; q += 16) tt_dbg("[HITP]   %s +%03X: %08X %08X %08X %08X\n", w2 ? "his " : "hero", q, rd32(S2, gs + q), rd32(S2, gs + q + 4), rd32(S2, gs + q + 8), rd32(S2, gs + q + 12));
                        }
                        for (k = 0; k < np && k < 16; k++) {
                            uint32_t lo = rd32(S2, t + 4 * k), n2;
                            if (!lo) continue;
                            n2 = rd32(S2, t + lo);
                            for (i = 0; i < n2 && i < 12; i++) {
                                uint32_t bb = rd32(S2, t + lo + 4 + 4 * i);
                                tt_dbg("[HITP]   %s point %u[%u] bone %u (%.2f %.2f %.2f)\n", w2 ? "his " : "hero", k, i, bb,
                                       (int)bb < nn ? PP[bb][0] : 0, (int)bb < nn ? PP[bb][1] : 0, (int)bb < nn ? PP[bb][2] : 0);
                            }
                        }
                    }
                }
                (void)b;
            }
        }
    }
    if (getenv("TT_ANIMCYCLE")) {   /* testing: every motion of his in a free slot, to be shown in turn */
        uint32_t gs, e = 0;
        s_ncyc = 0;
        for (gs = 0; gs < gns && s_ncyc < 64; gs++) {
            uint32_t n = rd32(G + gt[gs], 0x14);
            if (!gt[gs]) continue;
            while (e < ans && at[e]) e++;
            if (e >= ans) break;
            pos = (pos + 15) & ~15u;
            if (pos + n > cap) break;
            memcpy(out + pos, G + gt[gs], n);
            at[e] = pos; s_cyc[s_ncyc][0] = gs; s_cyc[s_ncyc][1] = e; s_cyc[s_ncyc][2] = pos; s_ncyc++;
            pos += n;
        }
    }
    if (getenv("TT_CYCSLOTS")) {   /* testing: these composed slots, shown in turn */
        const char *q = getenv("TT_CYCSLOTS");
        s_ncyc = 0;
        while (*q && s_ncyc < 64) {
            uint32_t v = (uint32_t)atoi(q);
            if (v < ans && at[v]) { s_cyc[s_ncyc][0] = v; s_cyc[s_ncyc][1] = v; s_cyc[s_ncyc][2] = at[v]; s_ncyc++; }
            q = strchr(q, ','); if (!q) break; q++;
        }
    }
    if (!s_compose_menu && shot_kind(g) >= 0 && !getenv("TT_NOSHOTSND")) {
        /* (1.7) his shots' own sounds: the hero's bow (its release 192, the
         * arrow's flight 193, on the shooting moves' events) becomes his
         * own -- ids the level's bank has only for him (voice_bank) */
        int nfix = 0;
        for (s = 0; s < ans; s++) {
            uint32_t r, ev, ne, i;
            if (!at[s] || anim_class((int)s) != AC_RANGED) continue;
            r = at[s];
            ne = rd32(out, r + 4 * 6); ev = r + rd32(out, r + 4 * 7);
            if (ne > 64 || ev + 4 * ne > pos) continue;
            for (i = 0; i < ne; i++) {
                uint32_t e = ev + rd32(out, ev + 4 * i), ty, v;
                if (e + 28 > pos) continue;
                ty = rd32(out, e + 12); v = rd32(out, e + 20);
                if ((ty == 1 || ty == 2) && (v == 192 || v == 193)) { wr32(out, e + 20, v == 192 ? SHOT_SND_ID : SHOT_SND_ID2); nfix++; }
            }
        }
        tt_dbg("[HEROES]   his shots' sounds: %d event(s)\n", nfix);
    }
    pos = (pos + 15) & ~15u;
    if (pos + 4 * ans > cap) return 0;
    for (s = 0; s < ans; s++) wr32(out, pos + 4 * s, at[s]);
    wr32(out, 0x10, pos);
    pos += 4 * ans;
    wr32(out, 0xC, pos);
    s_comp_size = pos;
    return pos;
}
#define MAX_ADDS (5 * N_EXTRA + 8) /* modelos, animaciones, texturas y armas del roster completo */

/* target + resources -> out: each Add's data is appended; a directory
 * entry with its {type, key} is repointed at it, else one is added (unless
 * the target has that resource already). 1 on success. */
/* ---- Boromir's shield, part of his own model ----
 * A weapon in the bow's hand is drawn only while the hero shoots, and the
 * script's other slots in cutscenes only: so the shield (a prop: "prxb",
 * 32-byte vertices {pos, normal, uv} at +0x1000, a u16 strip at +0x1000 +
 * [+0x1F0], [+0x204] bytes of it) goes into his mesh, on his left forearm
 * (the hand has no palette slot), placed by the hand's bind pose as the
 * weapon op places props; and his texture becomes 256x512, his own above
 * and the shield's below (UVs remapped). DXT1 throughout. */
typedef struct { int skin; uint32_t mesh, tex, anim, prop, ptex; int bone, skinbone; } Shield;
/* his skeleton's sword hand is bone 40 (attach point 0 -- x < 0 is his
 * right); the shield goes on the other: placed by hand 32, on forearm 29 */
/* Gandalf's staff, in his left hand at all times (1.7: his sword hand
 * holds a sword): placed by his left hand 43 (attach point 1; his sword
 * hand is point 0's bone 54, as Boromir's is), on forearm 38 */
static const Shield s_shields[] = { { 0, 0xA6841986u, 0xFD8B0956u, 0xF9C62B16u, 0x0D473744u, 0xDEF52D38u, 32, 29 },
                                    { 1, 0x3E74698Au, 0xF2BE2DCCu, 0xDDAEA31Cu, 0x9556E30Cu, 0xE6C4CFA9u, 43, 38 },
/* (Versus) the shield orc's: placed by his left hand 33 (attach point 1, where the prologue's script puts it), on forearm 30
 * (1.9.9: skin 12 -- it still said 14, his place before the roster changed: the shield was never his to break, and
 * Gandalf the White's staff entry, gone with him, sat on 12) */
                                    { 12, 0x2D965DE8u, 0x3E31996Au, 0xA1D1A7E9u, 0x4B82EA5Cu, 0x484A7EBAu, 33, 30 } };
static int shield_on(int g) { size_t i; if (getenv("TT_NOSHIELD")) return 0; for (i = 0; i < sizeof s_shields / sizeof s_shields[0]; i++) if (s_shields[i].skin == g) return 1; return 0; }

static void dxt1a_decode(const unsigned char *d, int w, int h, unsigned char *rgba)
{
    int bx, by, i;
    for (by = 0; by < (h + 3) / 4; by++)
        for (bx = 0; bx < (w + 3) / 4; bx++, d += 8) {
            unsigned c0 = rd16(d, 0), c1 = rd16(d, 2); uint32_t bits = rd32(d, 4);
            unsigned char pal[4][4];
            for (i = 0; i < 2; i++) {
                unsigned c = i ? c1 : c0;
                pal[i][0] = (unsigned char)(((c >> 11) & 31) * 255 / 31); pal[i][1] = (unsigned char)(((c >> 5) & 63) * 255 / 63); pal[i][2] = (unsigned char)((c & 31) * 255 / 31); pal[i][3] = 255;
            }
            for (i = 0; i < 3; i++) {
                if (c0 > c1) { pal[2][i] = (unsigned char)((2 * pal[0][i] + pal[1][i]) / 3); pal[3][i] = (unsigned char)((pal[0][i] + 2 * pal[1][i]) / 3); }
                else { pal[2][i] = (unsigned char)((pal[0][i] + pal[1][i]) / 2); pal[3][i] = 0; }
            }
            pal[2][3] = 255; pal[3][3] = c0 > c1 ? 255 : 0;
            for (i = 0; i < 16; i++) {
                int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                if (x < w && y < h) memcpy(rgba + 4 * ((size_t)y * w + x), pal[(bits >> (2 * i)) & 3], 4);
            }
        }
}
static unsigned rgb565(const float *c)
{
    int r = (int)(c[0] * 31 / 255 + 0.5f), g = (int)(c[1] * 63 / 255 + 0.5f), b = (int)(c[2] * 31 / 255 + 0.5f);
    r = r < 0 ? 0 : r > 31 ? 31 : r; g = g < 0 ? 0 : g > 63 ? 63 : g; b = b < 0 ? 0 : b > 31 ? 31 : b;
    return (unsigned)(r << 11 | g << 5 | b);
}
/* one block: the colours' main axis (a few power steps), its ends */
static void dxt1_block(const unsigned char px[16][4], unsigned char *o)
{
    float mean[3] = { 0, 0, 0 }, cov[3][3] = { { 0 } }, ax[3] = { 1, 1, 1 }, lo = 1e9f, hi = -1e9f, e0[3], e1[3], pal[4][3];
    int i, j, k, n = 0, trans = 0;
    unsigned c0, c1; uint32_t bits = 0;
    for (i = 0; i < 16; i++) { if (px[i][3] < 128) { trans = 1; continue; } for (j = 0; j < 3; j++) mean[j] += px[i][j]; n++; }
    if (!n) { wr16(o, 0, 0); wr16(o, 2, 0xFFFF); wr32(o, 4, 0xFFFFFFFFu); return; }
    for (j = 0; j < 3; j++) mean[j] /= (float)n;
    for (i = 0; i < 16; i++) if (px[i][3] >= 128) for (j = 0; j < 3; j++) for (k = 0; k < 3; k++) cov[j][k] += (px[i][j] - mean[j]) * (px[i][k] - mean[k]);
    for (k = 0; k < 6; k++) {
        float t[3], m = 0;
        for (j = 0; j < 3; j++) t[j] = cov[j][0] * ax[0] + cov[j][1] * ax[1] + cov[j][2] * ax[2];
        for (j = 0; j < 3; j++) if (fabsf(t[j]) > m) m = fabsf(t[j]);
        if (m < 1e-6f) break;
        for (j = 0; j < 3; j++) ax[j] = t[j] / m;
    }
    for (i = 0; i < 16; i++) if (px[i][3] >= 128) {
        float d = (px[i][0] - mean[0]) * ax[0] + (px[i][1] - mean[1]) * ax[1] + (px[i][2] - mean[2]) * ax[2];
        if (d < lo) lo = d; if (d > hi) hi = d;
    }
    { float n2 = ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]; if (n2 < 1e-9f) n2 = 1; for (j = 0; j < 3; j++) { e0[j] = mean[j] + ax[j] * hi / n2; e1[j] = mean[j] + ax[j] * lo / n2; } }
    c0 = rgb565(e0); c1 = rgb565(e1);
    if (trans ? c0 > c1 : c0 < c1) { unsigned t = c0; c0 = c1; c1 = t; }
    if (!trans && c0 == c1) { wr16(o, 0, (uint16_t)c0); wr16(o, 2, (uint16_t)c1); wr32(o, 4, 0); return; }
    for (i = 0; i < 2; i++) { unsigned c = i ? c1 : c0; pal[i][0] = ((c >> 11) & 31) * 255.0f / 31; pal[i][1] = ((c >> 5) & 63) * 255.0f / 63; pal[i][2] = (c & 31) * 255.0f / 31; }
    for (j = 0; j < 3; j++) {
        if (!trans) { pal[2][j] = (2 * pal[0][j] + pal[1][j]) / 3; pal[3][j] = (pal[0][j] + 2 * pal[1][j]) / 3; }
        else { pal[2][j] = (pal[0][j] + pal[1][j]) / 2; pal[3][j] = -1e6f; }
    }
    for (i = 0; i < 16; i++) {
        int best = 0; float bd = 1e30f;
        if (trans && px[i][3] < 128) best = 3;
        else for (k = 0; k < (trans ? 3 : 4); k++) {
            float d = 0; for (j = 0; j < 3; j++) d += (px[i][j] - pal[k][j]) * (px[i][j] - pal[k][j]);
            if (d < bd) { bd = d; best = k; }
        }
        bits |= (uint32_t)best << (2 * i);
    }
    wr16(o, 0, (uint16_t)c0); wr16(o, 2, (uint16_t)c1); wr32(o, 4, bits);
}
static size_t dxt1a_encode(const unsigned char *rgba, int w, int h, unsigned char *o)
{
    int bx, by, i;
    size_t n = 0;
    for (by = 0; by < (h + 3) / 4; by++)
        for (bx = 0; bx < (w + 3) / 4; bx++) {
            unsigned char px[16][4];
            for (i = 0; i < 16; i++) {
                int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                if (x >= w) x = w - 1; if (y >= h) y = h - 1;
                memcpy(px[i], rgba + 4 * ((size_t)y * w + x), 4);
            }
            dxt1_block((const unsigned char (*)[4])px, o + n); n += 8;
        }
    return n;
}
/* the same for a model of two parts (Gandalf's: his hat's tip the second).
 * Each part's header (vertex count +0, vertex offset +8 from header+4,
 * strip length +0x18, strip offset +0x1C from header+0x1C, palette +0x20)
 * points forward from itself; the strips lie one after the other, then
 * room, then the vertices one part after the other. The staff goes into
 * the first part: its strip lengthened (the second's moved along), its
 * vertices after the first part's (the second's moved along). */
static unsigned char *shield_mesh2(const Shield *sh, const unsigned char *S, size_t sl, size_t *len,
                                   uint32_t ma, uint32_t me, uint32_t pa, uint32_t pe, uint32_t aa, uint32_t ae)
{
    static int par[128]; static Quat W[128], BL[128];
    uint32_t h0 = 0x30, h1, vc0, vo0, ic0, io0, pal0, vc1, vo1, ic1, io1, i, nv = 0, ioff, inb, slot = 0xFFFFFFFFu;
    uint32_t i0, i1e, v0, v0e, v1e, add, D, newsize, mlen = me - ma, cnt;
    unsigned char *m; float T[3], minv = 1e9f, fl; int nb;
    (void)sl; (void)pe;
    nb = rt_binds(S + aa, ae - aa, par, W, BL);
    if (nb <= sh->bone) return NULL;
    { uint32_t mo = 0x40 + rd32(S + aa, 0x40 + 0xC) + (uint32_t)sh->bone * 128 + 64 + 48; int c; for (c = 0; c < 3; c++) { uint32_t v = rd32(S + aa, mo + 4 * c); memcpy(&T[c], &v, 4); } }
    ioff = 0x1000 + rd32(S, pa + 0x1F0); inb = rd32(S, pa + 0x204);
    if (inb < 6) return NULL;
    for (i = 0; i < inb / 2; i++) if (rd16(S, pa + ioff + 2 * i) + 1u > nv) nv = rd16(S, pa + ioff + 2 * i) + 1u;
    if (pa + 0x1000 + nv * 32 > pa + ioff) return NULL;
    vc0 = rd32(S, ma + h0); vo0 = rd32(S, ma + h0 + 8); ic0 = rd32(S, ma + h0 + 0x18); io0 = rd32(S, ma + h0 + 0x1C); pal0 = rd32(S, ma + h0 + 0x20);
    if (pal0 > 255) return NULL;
    h1 = (h0 + 0x24 + 2 * pal0 + 3) & ~3u;
    vc1 = rd32(S, ma + h1); vo1 = rd32(S, ma + h1 + 8); ic1 = rd32(S, ma + h1 + 0x18); io1 = rd32(S, ma + h1 + 0x1C);
    for (i = 0; i < pal0; i++) if (S[ma + 0x54 + 2 * i] == sh->skinbone) slot = S[ma + 0x55 + 2 * i];
    i0 = io0 + h0 + 0x1C; i1e = io1 + h1 + 0x1C + 2 * ic1;
    v0 = vo0 + h0 + 4; v0e = v0 + 48 * vc0; v1e = vo1 + h1 + 4 + 48 * vc1;
    cnt = 2 + inb / 2; if (cnt & 1) cnt++;   /* whole words: the second part's strip stays aligned */
    D = 2 * cnt; add = 48 * nv;
    if (slot == 0xFFFFFFFFu || ic0 % 2 || i0 + 2 * ic0 != io1 + h1 + 0x1C || vo1 + h1 + 4 != v0e || i1e + D > v0 || v1e > mlen || rd32(S, ma + 8) != mlen) {
        tt_dbg("[HEROES] his staff: the model's layout not as expected\n"); return NULL;
    }
    newsize = (mlen + add + 0xFFF) & ~0xFFFu;
    m = (unsigned char *)calloc(1, newsize);
    if (!m) return NULL;
    memcpy(m, S + ma, mlen);
    /* the second part's strip moved along, the first's lengthened */
    memmove(m + io1 + h1 + 0x1C + D, m + io1 + h1 + 0x1C, 2 * ic1);
    {
        uint32_t k = ic0, last = rd16(m, i0 + 2 * (ic0 - 1));
        wr16(m, i0 + 2 * k, (uint16_t)last); k++;
        wr16(m, i0 + 2 * k, (uint16_t)(rd16(S, pa + ioff) + vc0)); k++;
        for (i = 0; i < inb / 2; i++) { wr16(m, i0 + 2 * k, (uint16_t)(rd16(S, pa + ioff + 2 * i) + vc0)); k++; }
        while (k < ic0 + cnt) { wr16(m, i0 + 2 * k, rd16(m, i0 + 2 * (k - 1))); k++; }
        wr32(m, h0 + 0x18, ic0 + cnt);
        wr32(m, h1 + 0x1C, io1 + D);
    }
    /* the vertices: the second part's (and anything after) moved along */
    memmove(m + v0e + add, m + v0e, mlen - v0e);
    memset(m + v0e, 0, add);
    for (i = 0; i < vc0; i++) { uint32_t a = v0 + 48 * i + 28; memcpy(&fl, m + a, 4); fl *= 0.5f; memcpy(m + a, &fl, 4); }   /* his texture: the top half */
    for (i = 0; i < vc1; i++) { uint32_t a = v0e + add + 48 * i + 28; memcpy(&fl, m + a, 4); fl *= 0.5f; memcpy(m + a, &fl, 4); }
    for (i = 0; i < nv; i++) { float v; memcpy(&v, S + pa + 0x1000 + 32 * i + 28, 4); if (v < minv) minv = v; }
    minv = floorf(minv);
    fl = (float)(slot * 3);
    for (i = 0; i < nv; i++) {
        const unsigned char *v = S + pa + 0x1000 + 32 * i;
        float p[3], n[3], r[8], f[12]; int c;
        for (c = 0; c < 8; c++) memcpy(&r[c], v + 4 * c, 4);
        qrotv(W[sh->bone], r, p); qrotv(W[sh->bone], r + 3, n);
        for (c = 0; c < 3; c++) { f[c] = p[c] + T[c]; f[3 + c] = n[c]; }
        f[6] = r[6]; f[7] = 0.5f + 0.5f * (r[7] - minv);
        f[8] = fl; f[9] = 1; f[10] = fl; f[11] = 0;
        memcpy(m + v0e + 48 * i, f, 48);
    }
    wr32(m, h0, vc0 + nv);
    wr32(m, h1 + 8, vo1 + add);
    wr32(m, 8, newsize);
    *len = newsize;
    tt_dbg("[HEROES] his staff in his model: %u vertices on bone %d (two parts)\n", nv, sh->skinbone);
    return m;
}
/* (Versus) the shield orc's shield, breakable: its vertices as built (48 bytes each), found in the level's memory and
 * folded onto one point (gone) or put back. tt_heroes_shield_show(on) */
static unsigned char s_shv_sig[48 * 64], s_shv_save[48 * 64]; static uint32_t s_shv_n, s_shv_at[4], s_shv_nat; static int s_shv_level, s_shv_hidden;
int tt_heroes_shield_show(int on, int level)
{
    uint32_t i, k, a, nb;
    if (!s_shv_n || s_shv_n > 64) return 0;
    nb = 48u * s_shv_n;
    if (s_shv_level != level) {   /* a new level: find them (the model's copy in the level's contiguous memory) */
        const volatile uint32_t *w = (const volatile uint32_t *)(xbox_GetMemoryOffset() + 0x80000000u);
        uint32_t first, nw = (0x04000000u - nb) / 4u; s_shv_nat = 0; s_shv_hidden = 0;
        memcpy(&first, s_shv_sig, 4);
        for (i = 0; i < nw && s_shv_nat < 4; i++) if (w[i] == first) {
            a = 0x80000000u + 4u * i;
            for (k = 0; k < nb; k++) if (G8(a + k) != s_shv_sig[k]) break;
            if (k == nb) s_shv_at[s_shv_nat++] = a;
        }
        s_shv_level = level;
        tt_dbg("[HEROES] the shield orc's shield: %u cop%s in memory\n", s_shv_nat, s_shv_nat == 1 ? "y" : "ies");
    }
    if (!on == s_shv_hidden) return (int)s_shv_nat;
    for (k = 0; k < s_shv_nat; k++) {
        a = s_shv_at[k];
        for (i = 0; i < s_shv_n; i++) {
            uint32_t v = a + 48u * i, c;
            for (c = 0; c < 3; c++) G32(v + 4 * c) = on ? rd32(s_shv_sig, 48 * i + 4 * c) : rd32(s_shv_sig, 4 * c);   /* (off: every one at the first's place) */
        }
    }
    s_shv_hidden = !on;
    (void)s_shv_save;
    return (int)s_shv_nat;
}
static unsigned char *shield_mesh(const Shield *sh, const unsigned char *S, size_t sl, size_t *len);
static void shield_sig_fill(void)
{
    size_t i, sl, ml; unsigned char *S, *m; char pth[700];
    for (i = 0; i < sizeof s_shields / sizeof s_shields[0]; i++) {
        int g = s_shields[i].skin;
        if (g < 0 || g >= N_SKINS || strcmp(s_skins[g].name, "orc")) continue;
        if (!host_path(s_skins[g].src, pth, sizeof pth) || !(S = read_all(pth, &sl))) return;
        if ((m = shield_mesh(&s_shields[i], S, sl, &ml)) != NULL) free(m);
        free(S);
        return;
    }
}
static unsigned char *shield_mesh(const Shield *sh, const unsigned char *S, size_t sl, size_t *len)
{
    uint32_t ma, me, ty, pa, pe, aa, ae, vc, vo, ic, io, ib, np, i, nv = 0, pal, slot = 0xFFFFFFFFu, ioff, inb, newsize, end;
    static int par[128]; static Quat W[128], BL[128];
    unsigned char *m;
    float T[3], minv = 1e9f, fl;
    int nb;
    if (!res_find(S, sl, sh->mesh, 100, &ma, &me, &ty) || !res_find(S, sl, sh->prop, 200, &pa, &pe, &ty) || !res_find(S, sl, sh->anim, 150, &aa, &ae, &ty)) return NULL;
    if (memcmp(S + pa, "prxb", 4)) return NULL;
    if (rd32(S, ma + 0x2C) == 2) return shield_mesh2(sh, S, sl, len, ma, me, pa, pe, aa, ae);
    if (rd32(S, ma + 0x2C) != 1) return NULL;
    if (getenv("TT_SHBONE") || getenv("TT_SHSKIN")) {   /* testing: other bones */
        static Shield t; t = *sh;
        if (getenv("TT_SHBONE")) t.bone = atoi(getenv("TT_SHBONE"));
        if (getenv("TT_SHSKIN")) t.skinbone = atoi(getenv("TT_SHSKIN"));
        sh = &t;
    }
    nb = rt_binds(S + aa, ae - aa, par, W, BL);
    if (nb <= sh->bone) return NULL;
    { uint32_t mo = 0x40 + rd32(S + aa, 0x40 + 0xC) + (uint32_t)sh->bone * 128 + 64 + 48; int c; for (c = 0; c < 3; c++) { uint32_t v = rd32(S + aa, mo + 4 * c); memcpy(&T[c], &v, 4); } }
    ioff = 0x1000 + rd32(S, pa + 0x1F0); inb = rd32(S, pa + 0x204);
    if (pa + ioff + inb > pe || inb < 6) return NULL;
    for (i = 0; i < inb / 2; i++) if (rd16(S, pa + ioff + 2 * i) + 1u > nv) nv = rd16(S, pa + ioff + 2 * i) + 1u;
    if (pa + 0x1000 + nv * 32 > pa + ioff) return NULL;
    vc = rd32(S, ma + 0x30); vo = rd32(S, ma + 0x38); ic = rd32(S, ma + 0x48); io = rd32(S, ma + 0x4C); pal = rd32(S, ma + 0x50);
    for (i = 0; i < pal && i < 255; i++) if (S[ma + 0x54 + 2 * i] == sh->skinbone) slot = S[ma + 0x55 + 2 * i];
    ib = io + 0x4C; end = vo + 0x34 + 48 * vc;
    if (slot == 0xFFFFFFFFu || ib + 2 * (ic + 3 + inb / 2) > vo || rd32(S, ma + 8) > me - ma || end > me - ma) {   /* (an odd strip: one more join, below) */
        tt_dbg("[HEROES] shield: not made (palette slot %d, indices %u, vertices at %X, size %X of %X)\n", (int)slot, ic, vo, rd32(S, ma + 8), me - ma); return NULL; }
    newsize = (end + 48 * nv + 0xFFF) & ~0xFFFu;
    if (newsize < me - ma) newsize = (me - ma + 0xFFF) & ~0xFFFu;
    m = (unsigned char *)calloc(1, newsize);
    if (!m) return NULL;
    memcpy(m, S + ma, me - ma);
    for (i = 0; i < vc; i++) { uint32_t a = vo + 0x34 + 48 * i + 28; memcpy(&fl, m + a, 4); fl *= 0.5f; memcpy(m + a, &fl, 4); }   /* his texture: the top half */
    for (i = 0; i < nv; i++) { float v; memcpy(&v, S + pa + 0x1000 + 32 * i + 28, 4); if (v < minv) minv = v; }
    minv = floorf(minv);
    fl = (float)(slot * 3);
    for (i = 0; i < nv; i++) {
        const unsigned char *v = S + pa + 0x1000 + 32 * i;
        float p[3], n[3], r[8], f[12]; int c;
        for (c = 0; c < 8; c++) memcpy(&r[c], v + 4 * c, 4);
        qrotv(W[sh->bone], r, p); qrotv(W[sh->bone], r + 3, n);
        for (c = 0; c < 3; c++) { f[c] = p[c] + T[c]; f[3 + c] = n[c]; }
        f[6] = r[6]; f[7] = 0.5f + 0.5f * (r[7] - minv);
        f[8] = fl; f[9] = 1; f[10] = fl; f[11] = 0;
        memcpy(m + end + 48 * i, f, 48);
    }
    wr32(m, 8, newsize);
    {   /* the strip continued: a degenerate join, the parity kept */
        uint32_t last = rd16(m, ib + 2 * (ic - 1)), k = ic;
        wr16(m, ib + 2 * k++, (uint16_t)last); if (ic & 1) wr16(m, ib + 2 * k++, (uint16_t)last);   /* (the shield orc's strip is odd: its winding kept) */
        wr16(m, ib + 2 * k++, (uint16_t)(rd16(S, pa + ioff) + vc));
        for (i = 0; i < inb / 2; i++) wr16(m, ib + 2 * k++, (uint16_t)(rd16(S, pa + ioff + 2 * i) + vc));
        wr32(m, 0x30, vc + nv); wr32(m, 0x48, k);
    }
    np = 0; (void)np;
    if (sh->skin >= 0 && sh->skin < N_SKINS && !strcmp(s_skins[sh->skin].name, "orc")) {   /* (Versus) his shield breaks: its vertices, to find in memory */
        size_t q = (size_t)48 * nv; if (q > sizeof s_shv_sig) q = sizeof s_shv_sig;
        memcpy(s_shv_sig, m + end, q); s_shv_n = nv; s_shv_level = 0;
    }
    *len = newsize;
    tt_dbg("[HEROES] his shield in his model: %u vertices on bone %d\n", nv, sh->skinbone);
    return m;
}
static unsigned char *shield_tex(const Shield *sh, const unsigned char *S, size_t sl, size_t *len)
{
    uint32_t ta, te, sa, se, ty;
    int w, h, sw, shh, lv = 0, x, y;
    unsigned char *atl, *st, *o, *mip;
    size_t n = 0, ocap;
    if (!res_find(S, sl, sh->tex, 300, &ta, &te, &ty) || !res_find(S, sl, sh->ptex, 300, &sa, &se, &ty)) return NULL;
    w = (int)rd32(S, ta + 4); h = (int)rd32(S, ta + 8); sw = (int)rd32(S, sa + 4); shh = (int)rd32(S, sa + 8);
    if (rd32(S, ta + 12) != 0x11 || rd32(S, sa + 12) != 0x11 || w != h || w < 64 || w > 1024 || sw > 1024 || shh > 1024) return NULL;
    atl = (unsigned char *)malloc((size_t)w * h * 2 * 4); st = (unsigned char *)malloc((size_t)sw * shh * 4);
    /* DXT1, half a byte a texel: w x 2h is w*h bytes, its mips a third more;
     * twice that, page-rounded, is room for all with the padding */
    ocap = (0x80 + 2 * (size_t)w * h + 0x2000 + 0xFFF) & ~(size_t)0xFFF;
    o = (unsigned char *)calloc(1, ocap);
    mip = (unsigned char *)malloc((size_t)w * h * 2 * 4);
    if (!atl || !st || !o || !mip) { free(atl); free(st); free(o); free(mip); return NULL; }
    dxt1a_decode(S + ta + 0x80, w, h, atl);
    dxt1a_decode(S + sa + 0x80, sw, shh, st);
    for (y = 0; y < h; y++)   /* the shield's, scaled up (bilinear) */
        for (x = 0; x < w; x++) {
            float fx = (x + 0.5f) * sw / w - 0.5f, fy = (y + 0.5f) * shh / h - 0.5f;
            int x0 = (int)floorf(fx), y0 = (int)floorf(fy), c;
            float ax = fx - x0, ay = fy - y0;
            for (c = 0; c < 4; c++) {
                int xa = x0 < 0 ? 0 : x0, xb = x0 + 1 >= sw ? sw - 1 : x0 + 1, ya = y0 < 0 ? 0 : y0, yb = y0 + 1 >= shh ? shh - 1 : y0 + 1;
                float v = (1 - ax) * (1 - ay) * st[4 * (ya * sw + xa) + c] + ax * (1 - ay) * st[4 * (ya * sw + xb) + c]
                        + (1 - ax) * ay * st[4 * (yb * sw + xa) + c] + ax * ay * st[4 * (yb * sw + xb) + c];
                atl[4 * ((size_t)(h + y) * w + x) + c] = (unsigned char)(v + 0.5f);
            }
        }
    memcpy(o, S + ta, 0x80);
    {
        int cw = w, ch = 2 * h;
        memcpy(mip, atl, (size_t)w * h * 2 * 4);
        for (;;) {
            n += dxt1a_encode(mip, cw, ch, o + 0x80 + n); lv++;
            if (cw == 1 && ch == 1) break;
            {   /* the next level: 2x2 averages */
                int nw = cw > 1 ? cw / 2 : 1, nh = ch > 1 ? ch / 2 : 1, c;
                for (y = 0; y < nh; y++) for (x = 0; x < nw; x++) for (c = 0; c < 4; c++) {
                    int xa = cw > 1 ? 2 * x : x, ya = ch > 1 ? 2 * y : y, xb = cw > 1 ? xa + 1 : xa, yb = ch > 1 ? ya + 1 : ya;
                    mip[4 * (y * nw + x) + c] = (unsigned char)((mip[4 * (ya * cw + xa) + c] + mip[4 * (ya * cw + xb) + c] + mip[4 * (yb * cw + xa) + c] + mip[4 * (yb * cw + xb) + c] + 2) / 4);
                }
                cw = nw; ch = nh;
            }
        }
    }
    {   /* the header: 256 x 512 (the NV2A format word: levels, log2 sizes) */
        uint32_t f = rd32(o, 0x38), lw = 0, lh = 0;
        while ((1 << lw) < w) lw++; while ((1 << lh) < 2 * h) lh++;
        wr32(o, 8, (uint32_t)(2 * h)); wr32(o, 0x18, (uint32_t)lv);
        wr32(o, 0x38, (f & 0xFFFFu) | (uint32_t)lv << 16 | lw << 20 | lh << 24);
    }
    free(atl); free(st); free(mip);
    *len = (0x80 + n + 0xFFF) & ~(size_t)0xFFF;
    if (*len > ocap) { free(o); return NULL; }   /* (cannot happen: ocap holds it) */
    tt_dbg("[HEROES] his texture with his shield's: %dx%d, %d levels\n", w, 2 * h, lv);
    return o;
}
/* a resource made for a guest: Boromir's model with his shield, his
 * texture with the shield's; or (testing) heroes\<name>\res_<KEY>.bin */
static unsigned char *res_override(const unsigned char *S, size_t sl, uint32_t key, size_t *len)
{
    char pth[700]; int g; unsigned char *d;
    size_t i;
    if (getenv("TT_NORESOVR")) return NULL;
    paths();
    for (g = 0; g < N_SKINS; g++) {
        snprintf(pth, sizeof pth, "%s/heroes/%s/res_%08X.bin", s_dir, s_skins[g].name, key);
        if ((d = read_all(pth, len)) != NULL) return d;
    }
    if (getenv("TT_NOSHIELD")) return NULL;
    for (i = 0; i < sizeof s_shields / sizeof s_shields[0]; i++) {
        if (key == s_shields[i].mesh) return shield_mesh(&s_shields[i], S, sl, len);
        if (key == s_shields[i].tex) return shield_tex(&s_shields[i], S, sl, len);
    }
    return NULL;
}
/* the skin a level file is being built for (-1 none) */
static int s_build_skin = -1;
/* a guest's own toughness: the character database (type 910) of a level
 * built for him -- its first record, the player's -- with his health and
 * his resistance to flinching and knockdowns (Sauron: a dark lord, not a
 * ranger; values near the cave troll's). Records are {count, {field hash,
 * value} pairs}; the fields exist in the hero's record, only values change */
static void char_db_patch(unsigned char *o, const unsigned char *t, size_t tl, size_t len, int sk)
{
    uint32_t a, e2, ty, n, i, k;
    static const struct { const char *skin; uint32_t field, value; } P[] = {
        { "troll", 0x056A06A4u, 1750 },
        { "troll", 0xF3023D1Du, 550 },
        { "troll", 0x2DD1F42Bu, 0x461C4000u },
        { "urukcap", 0x056A06A4u, 1450 },
        { "urukcap", 0xF3023D1Du, 400 },
        { "urukcap", 0x2DD1F42Bu, 0x447A0000u },
        { "saruman", 0x056A06A4u, 1575 },
        { "saruman", 0xF3023D1Du, 480 },
        { "saruman", 0x2DD1F42Bu, 0x459C4000u },
        { "sauron", 0x056A06A4u, 1600 },      /* health (the heroes' 1000) */
        { "sauron", 0xF3023D1Du, 600 },       /* ENEMY_EXPERIENCE (200; the names: the game's hash, sub_0006BE30) */
        { "sauron", 0x2DD1F42Bu, 0x461C4000u },   /* his weight against knockbacks: 10000.0f (a float; the heroes 100, the troll 10000) */
        /* Gandalf a little short of Sauron, Lurtz a little short of Gandalf
         * (1.7: 1450 and 1300; the release after: more -- the bar stays as
         * long, the game draws it as a share of the most he has: a blow
         * takes less of it) */
        { "gandalf", 0x056A06A4u, 1575 },
        { "gandalf", 0xF3023D1Du, 480 },
        { "gandalf", 0x2DD1F42Bu, 0x459C4000u },  /* 5000.0f */
        { "lurtz", 0x056A06A4u, 1800 },           /* (1.7.2: 1500 -> 1800) */
        { "lurtz", 0xF3023D1Du, 380 },
        { "lurtz", 0x2DD1F42Bu, 0x45DAC000u },    /* 7000.0f (1.7.2: 2500 -> 7000: knocked back and down less) */
    };
    if (sk < 0 || sk >= N_SKINS) return;
    if (!res_find(t, tl, 0, 910, &a, &e2, &ty) || e2 > len || e2 - a < 0x30 || memcmp(o + a, "DBDB", 4)) { if (getenv("TT_TESTHP")) tt_dbg("[HEROES]   (testing) no record: %d %X %X\n", res_find(t, tl, 0, 910, &a, &e2, &ty), a, e2); return; }
    n = rd32(o, a + 0x20);
    if (n > 64 || a + 0x24 + 8 * n > e2) return;
    if (getenv("TT_TESTHP")) for (i = 0; i < n; i++) if (rd32(o, a + 0x24 + 8 * i) == 0x056A06A4u) { tt_dbg("[HEROES]   (testing) health %u -> %s\n", rd32(o, a + 0x28 + 8 * i), getenv("TT_TESTHP")); wr32(o, a + 0x28 + 8 * i, (uint32_t)atoi(getenv("TT_TESTHP"))); }   /* testing: his health */
    if (getenv("TT_NOTOUGH")) return;
    for (k = 0; k < sizeof P / sizeof P[0]; k++) {
        if (strcmp(P[k].skin, s_skins[sk].name)) continue;
        for (i = 0; i < n; i++) if (rd32(o, a + 0x24 + 8 * i) == P[k].field) {
            tt_dbg("[HEROES]   his record: field %08X %u -> %u\n", P[k].field, rd32(o, a + 0x28 + 8 * i), P[k].value);
            wr32(o, a + 0x28 + 8 * i, P[k].value);
        }
    }
}
/* ---- the cutscenes' own clips (1.7 final) ----
 * A level's in-engine cutscenes (type 750, "brxb": a 0x30 header, +8 its
 * size, then records {u16 size, u16 type}, ending with type 0x100) carry
 * motions of their own: each record 0x0301 {actor, offset} gives an actor a
 * bank ("aniz": +6 its count, +0xC its size, +0x10 its table, its "hier" at
 * +0x14 with the bone count at +0x1C) of clips, played as motions 0x8000 +
 * n. The hero's were made for the hero's skeleton (Aragorn's, 51 bones): on
 * a guest -- his own skeleton -- they twisted him out of shape (Fangorn's
 * opening scene, Balin's Tomb's, the Gates of Moria's). In his level each
 * such bank is made again for his skeleton, every clip retargeted
 * (retarget_rec, as the hero's kick and parry are: the same motion, his
 * bones turning as the hero's do, a key every 33 ms), and put at the
 * cutscene's end; the record points to it. The cutscene, grown, goes at the
 * file's end (build_file), its entry repointed. TT_NOCINE: as before. */
typedef struct { uint32_t key; unsigned char *buf; size_t len; } CineRel;
static CineRel s_cine[96];
static int s_ncine;
static size_t cine_clips_build(const unsigned char *t, size_t tl, size_t n, int sk)
{
    uint32_t a, e2, ty, hn, cnt, i, done = 0, failed = 0;
    const unsigned char *A;
    size_t alen, gl, extra = 0;
    unsigned char *gs = NULL;
    const unsigned char *G;
    size_t glen;
    uint32_t gn;
    char hp[600];
    s_ncine = 0;
    if (sk < 0 || sk >= N_EXTRA || !s_skins[sk].map || !s_native || getenv("TT_NOCINE")) return 0;
    if (!res_find(t, tl, 0xDE94124Au, 150, &a, &e2, &ty) || e2 <= a) return 0;
    A = t + a; alen = e2 - a;
    hn = rd32(A, 0x40 + 8);
    if (hn == 0 || hn > 128) return 0;
    if (!host_path(s_skins[sk].src, hp, sizeof hp) || !(gs = read_all(hp, &gl))) return 0;
    if (!res_find(gs, gl, s_skins[sk].anim, 150, &a, &e2, &ty) || e2 <= a) { free(gs); return 0; }
    G = gs + a; glen = e2 - a;
    gn = rd32(G, 0x40 + 8);
    cnt = rd32(t, n);
    for (i = 0; i < cnt && n + 4 + 12 * i + 12 <= tl && s_ncine < (int)(sizeof s_cine / sizeof s_cine[0]); i++) {
        uint32_t rt = rd32(t, n + 4 + 12 * i), r0 = rd32(t, n + 4 + 12 * i + 4), rk = rd32(t, n + 4 + 12 * i + 8), tot, p, q;
        uint32_t recs[64], nrec = 0, newoff[64];
        const unsigned char *C;
        unsigned char *nb_ = NULL;
        size_t cl, cap2;
        int bad = 0;
        if (rt != 750 || r0 + 0x30 > n || memcmp(t + r0, "brxb", 4)) continue;
        C = t + r0; tot = rd32(C, 8);
        if (tot < 0x34 || r0 + tot > n) continue;
        for (p = 0x30; p + 4 <= tot; ) {   /* the records */
            uint32_t sz = rd16(C, p), rty = rd16(C, p + 2), off, ns, tab, s;
            if (!sz || rty == 0x100) break;
            if (rty == 0x0301 && sz >= 0xC && nrec < 64) {
                off = rd32(C, p + 8);
                if (off + 0x30 <= tot && !memcmp(C + off, "aniz", 4)) {
                    ns = rd16(C, off + 6); tab = rd32(C, off + 0x10);
                    for (s = 0; s < ns && off + tab + 4 * s + 4 <= tot; s++) {
                        uint32_t so = rd32(C, off + tab + 4 * s), r = off + so;
                        if (so && r + 0x30 <= tot && !memcmp(C + r, "anim", 4) && rd32(C, r + 0x28) > rd32(C, r + 0x20)
                            && (rd32(C, r + 0x28) - rd32(C, r + 0x20)) / 4 == hn) { recs[nrec++] = p; break; }
                    }
                }
            }
            p += sz;
        }
        if (!nrec) continue;
        cap2 = tot + 16;
        for (q = 0; q < nrec; q++) {
            uint32_t off = rd32(C, recs[q] + 8), ns = rd16(C, off + 6);
            cap2 += 0x40 + 4 * ns + 16 + (size_t)ns * (0x40 + (size_t)gn * (4 + 8 * (255 + 255)) + 4 * gn + 16);
        }
        if (!(nb_ = (unsigned char *)calloc(1, cap2))) break;
        memcpy(nb_, C, tot);
        cl = (tot + 15) & ~(size_t)15;
        for (q = 0; q < nrec && !bad; q++) {
            uint32_t off = rd32(C, recs[q] + 8), ns = rd16(C, off + 6), tab = rd32(C, off + 0x10), s, w, prev;
            size_t bp;
            for (prev = 0; prev < q; prev++) if (rd32(C, recs[prev] + 8) == off) break;
            if (prev < q) { newoff[q] = newoff[prev]; wr32(nb_, recs[q] + 8, newoff[q]); continue; }   /* (the same bank) */
            if (tab < 0x14 || tab > 0x100) { bad = 1; break; }
            bp = cl;
            memcpy(nb_ + bp, C + off, tab);                     /* its header */
            if (!memcmp(C + off + 0x14, "hier", 4)) wr32(nb_, bp + 0x1C, gn);   /* his bones */
            w = (uint32_t)((tab + 4 * ns + 3) & ~3u);
            for (s = 0; s < ns; s++) {
                uint32_t so = rd32(C, off + tab + 4 * s), r = off + so, sz2;
                if (!so || r + 0x30 > tot) { wr32(nb_, bp + tab + 4 * s, 0); continue; }
                sz2 = rd32(C, r + 0x14);
                if (sz2 < 0x30 || r + sz2 > tot) { bad = 1; break; }
                wr32(nb_, bp + tab + 4 * s, w);
                if (rd32(C, r + 0x28) > rd32(C, r + 0x20) && (rd32(C, r + 0x28) - rd32(C, r + 0x20)) / 4 == hn) {
                    size_t clip = (alen + 15) & ~(size_t)15, l = 0;
                    unsigned char *buf = (unsigned char *)malloc(clip + sz2 + 16), *rr;
                    if (!buf) { bad = 1; break; }
                    memcpy(buf, A, alen);
                    memset(buf + alen, 0, clip - alen);
                    memcpy(buf + clip, C + r, sz2);
                    wr32(buf, rd32(buf, 0x10), (uint32_t)clip);   /* (slot 0 of the copy: the clip) */
                    s_rt_maxnt = 255; s_rt_maxpos = 255;
                    rr = retarget_rec(buf, clip + sz2, 0, G, glen, s_skins[sk].map, s_skins[sk].nmap, 1.0f, &l);
                    s_rt_maxnt = 96; s_rt_maxpos = 64;
                    free(buf);
                    if (!rr) { bad = 1; break; }
                    memcpy(nb_ + bp + w, rr, l);
                    free(rr);
                    w += (uint32_t)((l + 3) & ~(size_t)3);
                    done++;
                } else {
                    memcpy(nb_ + bp + w, C + r, sz2);
                    w += (sz2 + 3) & ~3u;
                }
            }
            if (bad) break;
            wr32(nb_, bp + 0xC, w);
            newoff[q] = (uint32_t)bp;
            wr32(nb_, recs[q] + 8, newoff[q]);
            cl = (bp + w + 15) & ~(size_t)15;
        }
        if (bad) { free(nb_); failed++; continue; }
        wr32(nb_, 8, (uint32_t)cl);
        s_cine[s_ncine].key = rk; s_cine[s_ncine].buf = nb_; s_cine[s_ncine].len = cl;
        s_ncine++;
        extra += cl + 0x1010;
        tt_dbg("[HEROES]   cutscene %08X: %u of the hero's banks made for %s (%u -> %u bytes)\n", rk, nrec, s_skins[sk].name, tot, (unsigned)cl);
    }
    free(gs);
    if (done || failed) tt_dbg("[HEROES]   the cutscenes' clips of the hero: %u retargeted to %s%s\n", done, s_skins[sk].name, failed ? " (some left as they were)" : "");
    return extra;
}
/* ---- the ammo counter (1.8) ----
 * The HUD's textures in a level are type 350 resources (keys 0..0x60, a
 * 0x80 header: +4 width, +8 height, then swizzled BGRA): the counter is key
 * 6 (as drawn) and key 0xD (its bright copy), 64x128 -- a disc with the
 * hero's ammunition behind it, the arrows the guests had from Aragorn's.
 * Elrond's: Legolas's own (the same keys in the level's Legolas variant,
 * .bfl). Gandalf's, Frodo's, the Uruk-hai's and Sauron's: their pictures
 * (hudammo\<name>.bmp beside the program, 64x128 with alpha) behind the
 * level's own disc; the bright copy as the game makes it (each channel
 * 0.62 c + 163, measured on Aragorn's and Legolas's). Boromir and Lurtz
 * keep the arrows. */
static uint32_t swz_index(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    uint32_t o = 0, bit = 0;
    while (w > 1 || h > 1) {
        if (w > 1) { o |= (x & 1u) << bit; bit++; x >>= 1; w >>= 1; }
        if (h > 1) { o |= (y & 1u) << bit; bit++; y >>= 1; h >>= 1; }
    }
    return o;
}
/* a 32-bit (or 24-bit) BMP, alpha kept: ARGB */
static uint32_t *bmp_load_argb(const char *p2, int *w, int *h, long *sz)
{
    FILE *f; unsigned char hd[54]; uint32_t off, *out; int x, y, bpp, bu, rowb;
    if (!(f = fopen(p2, "rb"))) return NULL;
    fseek(f, 0, SEEK_END); if (sz) *sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (fread(hd, 1, 54, f) != 54 || hd[0] != 'B' || hd[1] != 'M' || (hd[28] != 24 && hd[28] != 32)) { fclose(f); return NULL; }
    bpp = hd[28] / 8;
    off = rd32(hd, 10); *w = (int)rd32(hd, 18); *h = (int)rd32(hd, 22);
    bu = *h > 0; if (*h < 0) *h = -*h;
    if (*w <= 0 || *w > 2048 || *h <= 0 || *h > 2048) { fclose(f); return NULL; }
    rowb = (*w * bpp + 3) & ~3;
    out = (uint32_t *)malloc((size_t)*w * *h * 4);
    if (!out) { fclose(f); return NULL; }
    for (y = 0; y < *h; y++) {
        unsigned char row[2048 * 4];
        fseek(f, (long)(off + (uint32_t)y * (uint32_t)rowb), SEEK_SET);
        if (fread(row, 1, (size_t)rowb, f) != (size_t)rowb) { free(out); fclose(f); return NULL; }
        for (x = 0; x < *w; x++)
            out[(size_t)(bu ? *h - 1 - y : y) * *w + x] = (bpp == 4 ? (uint32_t)row[x * 4 + 3] << 24 : 0xFF000000u)
                | (uint32_t)row[x * bpp + 2] << 16 | (uint32_t)row[x * bpp + 1] << 8 | row[x * bpp];
    }
    fclose(f);
    return out;
}
/* (132) el troll lanza las rocas del Uruk-hai: sus dibujos de municion y mejoras */
static const char *ranged_art(int sk) { return !strcmp(s_skins[sk].name, "troll") || !strcmp(s_skins[sk].name, "troll2") ? (getenv("TT_TROLLURUKICON") ? "uruk" : "troll") : s_skins[sk].name; }   /* (1.9.9: the troll's own, hudammo\\troll.bmp -- was the Uruk-hai's burning rock) */
static void hudammo_path(int sk, char *p2, size_t n) { paths(); snprintf(p2, n, "%s/hudammo/%s.bmp", s_dir, ranged_art(sk)); }
static void hud_ammo_patch(unsigned char *o, const unsigned char *t, size_t tl, int sk, const char *target)
{
    static const uint32_t keys[2] = { 6, 0xD };
    uint32_t off[2], e2, ty, x, y;
    int v;
    if (sk < 0 || sk >= N_EXTRA || getenv("TT_NOHUDAMMO")) return;
    for (v = 0; v < 2; v++) {
        if (!res_find(t, tl, keys[v], 350, &off[v], &e2, &ty) || e2 - off[v] < 0x80 + 0x8000 || rd32(t, off[v] + 4) != 64 || rd32(t, off[v] + 8) != 128) return;
    }
    if (!strcmp(s_skins[sk].name, "elrond") || !strcmp(s_skins[sk].name, "elvenwarrior")) {   /* Legolas's */
        char p2[700]; size_t l2, n2; unsigned char *b;
        snprintf(p2, sizeof p2, "%s", target);
        l2 = strlen(p2);
        if (l2 < 4) return;
        p2[l2 - 1] = (char)(p2[l2 - 1] >= 'a' ? 'l' : 'L');
        if (!(b = read_all(p2, &n2))) return;
        for (v = 0; v < 2; v++) {
            uint32_t lo, le, lt;
            if (res_find(b, n2, keys[v], 350, &lo, &le, &lt) && le - lo >= 0x80 + 0x8000 && rd32(b, lo + 4) == 64 && rd32(b, lo + 8) == 128)
                memcpy(o + off[v] + 0x80, b + lo + 0x80, 0x8000);
        }
        free(b);
        tt_dbg("[HEROES]   the ammo counter: Legolas's (%s)\n", p2);
        return;
    }
    {
        char p2[700]; int w = 0, h = 0; uint32_t *art;
        hudammo_path(sk, p2, sizeof p2);
        if (!(art = bmp_load_argb(p2, &w, &h, NULL))) return;
        if (w != 64 || h != 128) { free(art); return; }
        for (v = 0; v < 2; v++) {
            static uint32_t disc[64 * 128];
            const unsigned char *src = t + off[v] + 0x80;
            unsigned char *dst = o + off[v] + 0x80;
            for (y = 0; y < 128; y++) for (x = 0; x < 64; x++) disc[y * 64 + x] = rd32(src, 4 * swz_index(x, y, 64, 128));
            for (y = 0; y < 128; y++) for (x = 0; x < 64; x++) {
                float dx = (float)x + 0.5f - 33.5f, dy = (float)y + 0.5f - 66.5f, d = sqrtf(dx * dx + dy * dy), m = 22.0f - d, an;
                uint32_t dc = disc[y * 64 + x], ic = art[y * 64 + x], oc = 0;
                float da, ia, oa, c3[3];
                int c;
                if (m > 1) m = 1; if (m < 0) m = 0;
                an = atan2f(dy, dx) * 57.29578f;
                if (d > 15.0f && d < 24.0f && (fabsf(an + 90.0f) < 30.0f || fabsf(an - 90.0f) < 30.0f)) {   /* the arrows' shafts on the rim: the rim from the side */
                    float t2 = (an + 90.0f) * 0.0174533f; int sx = (int)lroundf(33.5f + d * cosf(t2) - 0.5f), sy = (int)lroundf(66.5f + d * sinf(t2) - 0.5f);
                    if (sx >= 0 && sx < 64 && sy >= 0 && sy < 128) dc = disc[sy * 64 + (uint32_t)sx];
                }
                da = (float)(dc >> 24) / 255.0f * m; ia = (float)(ic >> 24) / 255.0f;
                for (c = 0; c < 3; c++) {
                    float dcv = (float)((dc >> (16 - 8 * c)) & 255), icv = (float)((ic >> (16 - 8 * c)) & 255);
                    if (v) icv = icv * 0.62f + 163.0f;
                    c3[c] = (dcv * da + icv * ia * (1 - da));
                }
                oa = da + ia * (1 - da);
                if (oa > 0.0f) {
                    for (c = 0; c < 3; c++) { float q = c3[c] / oa; if (q > 255) q = 255; oc |= (uint32_t)(q + 0.5f) << (16 - 8 * c); }
                    oc |= (uint32_t)(oa * 255.0f + 0.5f) << 24;
                }
                wr32(dst, 4 * swz_index(x, y, 64, 128), oc);
            }
        }
        free(art);
        tt_dbg("[HEROES]   the ammo counter: %s\n", p2);
    }
}
/* co-op: player 2's ammo icon for his HUD (twotowers_manual.c): the HUD's quiver texture (type 350, 64 x 128 swizzled
 * ARGB, key 6 or 0xD -- whichever player 1's pixels at cur are) from player 2's own hero variant (Balin's Tomb's: the
 * icons are the hero's in every level), or a guest's as hud_ammo_patch makes it. 1 = out (0x8000 bytes) filled */
int tt_coop_p2_ammo_pixels(uint32_t cur, unsigned char *out)
{
    static const char *const ext[5] = { ".bfa", ".bfa", ".bfl", ".bfg", ".bfi" };
    static const uint32_t keys[2] = { 6, 0xD };
    static int last_sk = -2; static unsigned char cache[2][0x8000]; static int have[2];
    char xp[96], hp[700];
    unsigned char *t, *o; size_t tl;
    uint32_t off, e2, ty;
    int sk = s_coop_skin ? s_playas : -1, hb = G8(HERO_BYTE), v, pick = -1;
    if (sk < 0 || getenv("TT_COOP_NOAMMOICON")) return 0;
    if (sk != last_sk) {   /* this player 2's two icons */
        int retail = skin_retail(sk);
        last_sk = sk; have[0] = have[1] = 0;
        snprintf(xp, sizeof xp, "\\Device\\CdRom0\\worlds\\04BALIN1%s", retail ? ext[s_retail_hero[sk - N_EXTRA]] : ".bfa");
        if (!host_path(xp, hp, sizeof hp) || !(t = read_all(hp, &tl))) return 0;
        o = t;
        if (!retail) { o = (unsigned char *)malloc(tl); if (!o) { free(t); return 0; } memcpy(o, t, tl); hud_ammo_patch(o, t, tl, sk, hp); }
        for (v = 0; v < 2; v++)
            if (res_find(t, tl, keys[v], 350, &off, &e2, &ty) && e2 - off >= 0x80 + 0x8000) { memcpy(cache[v], o + off + 0x80, 0x8000); have[v] = 1; }
        if (o != t) free(o);
        free(t);
        tt_dbg("[HEROES] co-op: player 2's ammo icons (%s) from %s: %d %d\n", s_skins[sk].name, xp, have[0], have[1]);
    }
    {   /* which of the two player 1's is: from his own variant */
        snprintf(xp, sizeof xp, "\\Device\\CdRom0\\worlds\\04BALIN1%s", ext[hb >= 1 && hb <= 4 ? hb : 1]);
        if (host_path(xp, hp, sizeof hp) && (t = read_all(hp, &tl))) {
            for (v = 0; v < 2 && pick < 0; v++)
                if (res_find(t, tl, keys[v], 350, &off, &e2, &ty) && e2 - off >= 0x80 + 0x8000) {
                    uint32_t k, same = 1;
                    for (k = 0; k < 256 && same; k++) if (t[off + 0x80 + k] != G8(cur + k)) same = 0;
                    if (same) pick = v;
                }
            free(t);
        }
    }
    if (pick < 0) pick = 0;
    if (!have[pick]) return 0;
    memcpy(out, cache[pick], 0x8000);
    return 1;
}
#include "heroes_weapon_profile.h"
/* (1.9.9) the level's projectile models (its dynamicProjectileObj: entries {index, prop, 0, 0, texture}): entry
 * TROLL_LOG_PROJ, the log prop, wears the bark (TROLL_LOG_TEX, added from Fangorn Forest with him) */
static void troll_log_patch(unsigned char *o, const unsigned char *t, uint32_t n)
{
    uint32_t cnt = rd32(t, n), i, a, q, sz;
    if (getenv("TT_TROLLROCK")) return;
    for (i = 0; i < cnt; i++) {
        if (rd32(t, n + 4 + 12 * i) != 500) continue;
        a = rd32(t, n + 4 + 12 * i + 4);
        if (a + 0x200 > n || memcmp(t + a + 8, "dynamicProjectileObj", 21)) continue;
        sz = rd32(t, a + 0x2C);
        for (q = 0; q < sz && q < 64; q++) {
            uint32_t e = a + 0x30 + 0x14 * q;
            if (e + 0x14 > n || rd32(t, e) != q) break;
            if (q == TROLL_LOG_PROJ && rd32(t, e + 4) == TROLL_LOG_PROP) {
                wr32(o, e + 0x10, TROLL_LOG_TEX);
                tt_dbg("[HEROES] the troll's log: projectile %u's texture %08X -> the bark %08X\n", q, rd32(t, e + 0x10), TROLL_LOG_TEX);
            }
        }
    }
}
static int build_file(const char *target, const Add *adds, int nadds, const char *out)
{
    size_t tl, cap, pos, sl[MAX_ADDS];
    unsigned char *t = read_all(target, &tl), *o = NULL, *s[MAX_ADDS], *ov[MAX_ADDS];
    uint32_t n, cnt, i, k, newoff[MAX_ADDS], extra = 0;
    size_t ovl[MAX_ADDS];
    int ok = 0, repoint[MAX_ADDS];
    FILE *f;
    memset(s, 0, sizeof s);
    memset(ov, 0, sizeof ov);
    if (!t || t[0] != 9 || nadds > MAX_ADDS) goto done;
    cap = tl + 0x10000;
    for (k = 0; k < (uint32_t)nadds; k++) {
        uint32_t a, e2, ty, j;
        char hp[600];
        for (j = 0; j < k; j++) if (!strcmp(adds[j].src, adds[k].src)) { s[k] = s[j]; sl[k] = sl[j]; break; }
        if (!s[k]) {
            if (!host_path(adds[k].src, hp, sizeof hp) || !(s[k] = read_all(hp, &sl[k])) || s[k][0] != 9) {
                tt_dbg("[HEROES] cannot read %s\n", adds[k].src); goto done;
            }
        }
        if (!res_find(s[k], sl[k], adds[k].src_key, -1, &a, &e2, &ty)) { tt_dbg("[HEROES] %08X not in %s\n", adds[k].src_key, adds[k].src); goto done; }
        ov[k] = res_override(s[k], sl[k], adds[k].src_key, &ovl[k]);
        cap += e2 - a + 0x2000 + (ov[k] ? ovl[k] : 0);
        if (adds[k].compose) cap += 0x200000;
    }
    cap += cine_clips_build(t, tl, rd32(t, 4), s_build_skin);
    o = (unsigned char *)calloc(1, cap);
    if (!o) goto done;
    n = rd32(t, 4); cnt = rd32(t, n);
    memcpy(o, t, n);
    char_db_patch(o, t, tl, n, s_build_skin);
    weapon_db_patch(o, t, tl, n, s_build_skin);
    if(s_build_skin==SK_TROLL&&!troll_weapon_db_patch(o,t,tl,n))goto done;
    if (s_build_skin == SK_TROLL || (s_coop_skin && (s_playas == SK_TROLL || s_playas == SK_TROLL2))) troll_log_patch(o, t, n);
    if(s_build_skin==SK_TROLL&&!troll_weapon_damage_patch(o,t,tl,n))tt_dbg("[TROLL-NPC] dano del arma sin cambiar (DB inesperada)\n");
    if (!s_coop_skin || s_build_skin == s_guest) hud_ammo_patch(o, t, tl, s_build_skin, target);   /* (player 1 a guest: his own icon) */   /* (co-op: the HUD's icon is player 1's; player 2's is his own texture, tt_coop_p2_ammo_pixels) */
    pos = n;
    for (k = 0; k < (uint32_t)nadds; k++) {
        uint32_t a, e2, ty, d1, d2, d3;
        repoint[k] = 0; newoff[k] = 0;
        for (i = 0; i < cnt; i++)
            if (rd32(t, n + 4 + 12 * i) == adds[k].type && rd32(t, n + 4 + 12 * i + 8) == adds[k].key) repoint[k] = 1;
        if (ov[k] && res_find(t, tl, adds[k].key, (int)adds[k].type, &d1, &d2, &d3)) repoint[k] = 1;   /* ours over the level's own */
        if (!repoint[k] && res_find(t, tl, adds[k].key, (int)adds[k].type, &d1, &d2, &d3)) continue;   /* there already */
        res_find(s[k], sl[k], adds[k].src_key, -1, &a, &e2, &ty);
        /* same offset within a page as in the source: texture data starts
         * on a page boundary there, and the renderer watches those pages */
        pos = ((pos + 0xFFF) & ~(size_t)0xFFF) + (a & 0xFFF);
        if (adds[k].compose) {
            /* the hero's set in the target: Aragorn's */
            /* (1.9.8) and last the player's own set (0xAAE0DF66, in every level: Fangorn Forest and the Westfold have none of
             * the four -- an extra hero as co-op's player 2 there could not be built, and was not drawn at all, a playtest) */
            /* (1.9.8) Gimli's set (0xAAE0DF66) is only in his own variants of a level (.bfg); Legolas' (0xEE4A9EBA) in his
             * (.bfl): with player 1 Legolas in the Westfold player 2 Sauron could not be built -- unseen, and leaving the level
             * froze on the loading screen (a playtest) */
            static const uint32_t hk[] = { 0xDE94124Au, 0xE39B82A6u, 0x44E6E72Au, 0x53DFD442u, 0xAAE0DF66u, 0xEE4A9EBAu };
            uint32_t ha = 0, he = 0, ht, q, nq = getenv("TT_HEROES_NOPLAYERSET") ? 4u : 6u;
            size_t got = 0;
            for (q = 0; q < nq && !got; q++)
                if (adds[k].type == 1100 ? (q == 0 && res_find(t, tl, s_compose_hkey, 1100, &ha, &he, &ht)) : res_find(t, tl, hk[q], 150, &ha, &he, &ht))
                    got = compose_anims(t + ha, he - ha, s[k] + a, e2 - a, adds[k].compose - 1, o + pos, cap - pos - 0x10000);
            if (!got) { tt_dbg("[HEROES] could not compose the animations\n"); goto done; }
            tt_dbg("[HEROES] his own animations as the hero's: %u bytes\n", (unsigned)got);
            newoff[k] = (uint32_t)pos;
            pos += got;
            if (!repoint[k]) extra++;
            continue;
        }
        if (ov[k]) {   /* made for him (heroes\<name>\res_<key>.bin) */
            a = 0; e2 = (uint32_t)ovl[k];   /* at the source's offset in its page (a texture's data on a page start) */
            memcpy(o + pos, ov[k], ovl[k]);
            tt_dbg("[HEROES] %08X: his own version (%u bytes)\n", adds[k].src_key, (unsigned)ovl[k]);
        } else
        memcpy(o + pos, s[k] + a, e2 - a);
        if (adds[k].map) remap_bones(o + pos, e2 - a, adds[k].map, adds[k].nmap);
        if (adds[k].tint && (adds[k].type == 300 || adds[k].type == 1250) && e2 - a > 0x88 && (rd32(o + pos, 0xC) & 0xFF) == 0x11) {
            /* Sting lit: its bright metal (the blade) a pale, glowing blue
             * -- each DXT1 block's two end colours */
            size_t q;
            for (q = 0x80; q + 8 <= e2 - a; q += 8) {
                unsigned c0 = rd16(o + pos, q), c1 = rd16(o + pos, q + 2), nc[2];
                int e;
                for (e = 0; e < 2; e++) {
                    unsigned c = e ? c1 : c0, r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
                    unsigned lum = r * 2 + g + b * 2;   /* about 0..250 */
                    int mx = (int)r, mn = (int)r;
                    if ((int)(g / 2) > mx) mx = (int)(g / 2); if ((int)(g / 2) < mn) mn = (int)(g / 2);
                    if ((int)b > mx) mx = (int)b; if ((int)b < mn) mn = (int)b;
                    nc[e] = c;
                    if (lum > 90 && mx - mn < 7) {   /* light, unsaturated: the steel */
                        unsigned nr = r * 2 / 3 + 6, ng = g * 7 / 8 + 20, nb = b + 14;
                        if (nr > 31) nr = 31; if (ng > 63) ng = 63; if (nb > 31) nb = 31;
                        nc[e] = (nr << 11) | (ng << 5) | nb;
                    }
                }
                if (c0 > c1) {   /* the four-colour mode kept: c0 still the greater */
                    if (nc[0] == nc[1]) { if (nc[1] & 31) nc[1]--; else nc[0]++; }
                    if (nc[0] < nc[1]) {   /* swapped: the indices too (0<->1, 2<->3) */
                        unsigned t = nc[0]; uint32_t ix = rd32(o + pos, q + 4);
                        nc[0] = nc[1]; nc[1] = t;
                        wr32(o + pos, q + 4, ix ^ 0x55555555u);
                    }
                } else if (nc[0] > nc[1]) {   /* the three-colour mode kept */
                    unsigned t = nc[0]; uint32_t ix = rd32(o + pos, q + 4), out = 0; int j;
                    nc[0] = nc[1]; nc[1] = t;
                    for (j = 0; j < 16; j++) { uint32_t v = (ix >> (2 * j)) & 3; if (v < 2) v ^= 1; out |= v << (2 * j); }
                    wr32(o + pos, q + 4, out);
                }
                wr16(o + pos, q, (uint16_t)nc[0]); wr16(o + pos, q + 2, (uint16_t)nc[1]);
            }
        }
        if (adds[k].empty && e2 - a >= 0x1000 && !memcmp(o + pos, "prxb", 4)) {
            /* a prop drawn as nothing: its strip all one index */
            uint32_t io2 = 0x1000 + rd32(o + pos, 0x1F0), nb2 = rd32(o + pos, 0x204);
            if (io2 + nb2 <= e2 - a) memset(o + pos + io2, 0, nb2);
        }

        else if (getenv("TT_IDMAP") && adds[k].type == 100) {   /* testing: the remap's other steps alone */
            static uint8_t idm[256]; int q; for (q = 0; q < 256; q++) idm[q] = (uint8_t)q;
            remap_bones(o + pos, e2 - a, idm, 256);
        }
        newoff[k] = (uint32_t)pos;
        pos += e2 - a;
        if (!repoint[k]) extra++;
    }
    {   /* the cutscenes made for him (cine_clips_build), at the end */
        int c;
        for (c = 0; c < s_ncine; c++) {
            pos = (pos + 0xF) & ~(size_t)0xF;
            memcpy(o + pos, s_cine[c].buf, s_cine[c].len);
            s_cine[c].len = pos;   /* (now its place) */
            pos += rd32(s_cine[c].buf, 8);
            free(s_cine[c].buf); s_cine[c].buf = NULL;
        }
    }
    pos = (pos + 0xF) & ~(size_t)0xF;
    wr32(o, 4, (uint32_t)pos);
    wr32(o, pos, cnt + extra);
    memcpy(o + pos + 4, t + n + 4, (size_t)cnt * 12);
    {
        int c;
        for (c = 0; c < s_ncine; c++)
            for (i = 0; i < cnt; i++) {
                size_t e = pos + 4 + (size_t)i * 12;
                if (rd32(o, e) == 750 && rd32(o, e + 8) == s_cine[c].key) wr32(o, e + 4, (uint32_t)s_cine[c].len);
            }
        s_ncine = 0;
    }
    for (k = 0; k < (uint32_t)nadds; k++) {
        if (!newoff[k]) continue;
        if (repoint[k]) {
            for (i = 0; i < cnt; i++) {
                size_t e = pos + 4 + (size_t)i * 12;
                if (rd32(o, e) == adds[k].type && rd32(o, e + 8) == adds[k].key) wr32(o, e + 4, newoff[k]);
            }
        } else {
            size_t e = pos + 4 + (size_t)cnt * 12;
            wr32(o, e, adds[k].type); wr32(o, e + 4, newoff[k]); wr32(o, e + 8, adds[k].key);
            cnt++;
        }
    }
    f = fopen(out, "wb");
    if (f) {
        size_t total = pos + 4 + (size_t)cnt * 12;
        ok = fwrite(o, 1, total, f) == total;
        fclose(f);
        if (!ok) remove(out);
    }
done:
    {   /* (the cutscenes made for him, if not written) */
        int c;
        for (c = 0; c < s_ncine; c++) { free(s_cine[c].buf); s_cine[c].buf = NULL; }
        s_ncine = 0;
    }
    for (k = 0; k < MAX_ADDS; k++) {
        uint32_t j, dup = 0;
        for (j = 0; j < k; j++) if (s[j] == s[k]) dup = 1;
        if (!dup) free(s[k]);
        free(ov[k]);
    }
    free(o); free(t);
    return ok;
}

static void mkdirs(const char *path)
{
    char p[600];
    size_t i;
    snprintf(p, sizeof p, "%s", path);
    for (i = 1; p[i]; i++)
        if (p[i] == '/' || p[i] == '\\') {
            char c = p[i]; p[i] = 0;
#ifdef _WIN32
            CreateDirectoryA(p, NULL);
#else
            mkdir(p, 0755);
#endif
            p[i] = c;
        }
}

/* the rebuilt file heroes\<sub>\<outname> (built now if missing) or NULL */
static const char *cached(const char *sub, const char *xtarget, const char *outname, const Add *adds, int nadds)
{
    static char out[600];
    char tgt[600];
    FILE *f;
    paths();
    snprintf(out, sizeof out, "%s/heroes/%s/%s", s_dir, sub, outname);
    if ((f = fopen(out, "rb"))) { fclose(f); return out; }
    if (!host_path(xtarget, tgt, sizeof tgt)) return NULL;
    mkdirs(out);
    if (build_file(tgt, adds, nadds, out)) { tt_dbg("[HEROES] built %s\n", out); return out; }
    tt_dbg("[HEROES] could not build %s\n", out);
    return NULL;
}

/* Solo recursos privados PLAYER_KEY. El hilo de entrada entrega una peticion;
 * la validacion y la escritura se hacen en el hilo del juego. */
static TTSizePlan s_size_plan;
static uint32_t s_size_base, s_size_tab;
static size_t s_size_len;
static int s_size_guest = -1;
static float s_size_scale = 1.0f;
static volatile LONG s_size_enabled, s_size_request, s_size_port;
static void hero_size_reset(void)
{
    InterlockedExchange(&s_size_enabled,0);
    InterlockedExchange(&s_size_request,0);
    s_size_base=0;s_size_guest=-1;s_size_scale=1.0f;ts_free(&s_size_plan);
}
static void hero_size_prepare(const char *path,int g)
{
    hero_size_reset();
    if(!path||g<0||g>=N_EXTRA||!s_skins[g].map||getenv("TT_NOSIZE"))return;
    size_t len;unsigned char *d=read_all(path,&len);uint32_t b,e,t;
    if(!d)return;
    int ok=res_find(d,len,PLAYER_KEY(s_skins[g].mesh),100,&b,&e,&t) && ts_mesh(&s_size_plan,d,len,b,e)
        &&res_find(d,len,PLAYER_KEY(s_skins[g].anim),150,&b,&e,&t) && ts_aniz(&s_size_plan,d,len,b,e)
        &&ts_finish(&s_size_plan);
    if(ok){s_size_len=len;s_size_tab=rd32(d,4);s_size_guest=g;}
    else {ts_free(&s_size_plan);tt_dbg("[SIZE] plan rechazado para %s\n",s_skins[g].name);}
    free(d);
}
static void hero_size_bind(uint32_t base,int g)
{
    if(g!=s_size_guest||!s_size_plan.count||base<0x80000000u||s_size_len>0x84000000u-base)return;
    if(G32(base)!=9||G32(base+4)!=s_size_tab)return;
    unsigned char *d=(unsigned char *)(xbox_GetMemoryOffset()+base);
    if(!ts_apply(&s_size_plan,d,s_size_len,1.0f,1.0f)){
        tt_dbg("[SIZE] datos residentes distintos: desactivado sin escribir\n");return;
    }
    s_size_base=base;
    tt_dbg("[SIZE] %s: %zu valores privados verificados\n",s_skins[g].name,s_size_plan.count);
}
/* XState Xbox: paquete u32, botones u16. Sin acceso a memoria invitada aqui. */
void tt_hero_size_filter(unsigned long port,void *state)
{
    static int last[4];
    if(port>=4)return;
    uint16_t buttons;memcpy(&buttons,(unsigned char *)state+4,2);
    int active=InterlockedCompareExchange(&s_size_enabled,0,0) && port==(unsigned long)InterlockedCompareExchange(&s_size_port,0,0);
    int dir=ts_size_chord(buttons);
    if(active&&dir){
        if(dir!=last[port])InterlockedExchange(&s_size_request,dir);
        buttons&=(uint16_t)~0x00C3u;memcpy((unsigned char *)state+4,&buttons,2);
    }
    last[port]=dir;
}

static int ends_with(const char *s, const char *e)
{
    size_t a = strlen(s), b = strlen(e);
    return a >= b && !_stricmp(s + a - b, e);
}

/* ===================================================================== */
/* unlocking the guests one by one                                        */
/* ===================================================================== */
/* Each finish of the Tower of Orthanc (21ORTH2) as Isildur himself counts:
 * 2 unlock Boromir, 3 Gandalf, 4 Frodo, 5 Lurtz, 6 Elrond, 7 the Uruk-hai,
 * 8 Sauron (s_unlock_at). (1.8) The count belongs to the game in play, as the
 * game's own progress does: a save loaded brings its count; every save
 * written carries the count of the game in play (one more if it is the save
 * after Isildur's finish), whatever slot it goes to and whoever was played
 * since; before any save is loaded, the newest save's. Kept in
 * heroes/unlocks.txt by save folder (UDATA\45410010\<save>), "@newest" the
 * last written. */
static char s_save_key[16];                 /* the save in play */
static int s_isildur_wins = -1;             /* its count, -1 not read */
static int s_orth_pending;                  /* Isildur is playing 21ORTH2 */
static int s_orth_saving;                   /* ... and the save after it is being written */
static unsigned s_xsv_opens, s_xsv_at_save; /* saves' game.xsv opened (the save screen's list opens every save's) */
static char s_loaded_key[16];               /* the save loaded */
static char s_read_key[16];                 /* the save whose game.xsv was last read */

static void wins_file(char *out, size_t n) { paths(); snprintf(out, n, "%s/heroes/unlocks.txt", s_dir); }

static int wins_read(const char *key)
{
    char fn[700], line[96], k[32];
    int v, best = 0;
    FILE *f;
    if (!key[0]) return 0;
    wins_file(fn, sizeof fn);
    if (!(f = fopen(fn, "r"))) return 0;
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "%31s %d", k, &v) == 2 && !strcmp(k, key)) best = v;
    fclose(f);
    return best;
}

#include "heroes_unlocks.h"
static int wins_write(const char *key, int v)
{
    char fn[700];
    if (!key[0]) return 0;
    wins_file(fn, sizeof fn);
    mkdirs(fn);
    return tt_unlocks_store(fn, key, v);
}

static int isildur_wins(void)
{
    if (s_isildur_wins < 0) {   /* nothing loaded yet: the newest save's */
        char fn[700], line[96], k[32], newest[32] = "";
        int v, most = 0;
        FILE *f;
        wins_file(fn, sizeof fn);
        if ((f = fopen(fn, "r"))) {
            while (fgets(line, sizeof line, f)) {
                if (sscanf(line, "@newest %31s", k) == 1) snprintf(newest, sizeof newest, "%s", k);
                else if (sscanf(line, "%31s %d", k, &v) == 2 && v > most) most = v;
            }
            fclose(f);
        }
        /* (a file from before 1.8 names no newest: the most any save has) */
        s_isildur_wins = newest[0] ? wins_read(newest) : most;
        tt_dbg("[HEROES] unlocks: the newest save's (%s): Isildur has finished the Tower of Orthanc %d time(s)\n",
               newest[0] ? newest : "none", s_isildur_wins);
    }
    return s_isildur_wins;
}
/* the save the game writes: the count of the game in play goes with it */
static void wins_saved(const char *key)
{
    int n = isildur_wins();
    if (s_orth_saving) { s_orth_saving = 0; n++; tt_dbg("[HEROES] Isildur has finished the Tower of Orthanc: %d time(s)\n", n); }
    s_isildur_wins = n;
    /* Contador y @newest en la misma sustitucion: no queda un estado a medias. */
    if (!wins_write(key, n))
        tt_dbg("[HEROES] WARNING: unlocks for save %s could not be committed; previous file preserved\n", key);
    snprintf(s_save_key, sizeof s_save_key, "%s", key);
    snprintf(s_loaded_key, sizeof s_loaded_key, "%s", key);
    tt_dbg("[HEROES] save %s: Isildur has %d finish(es) in memory\n", key, n);
}

/* guest g's own unlock: Isildur's finishes of the secret level -- the 2nd
 * Boromir, 3rd Gandalf, 4th Frodo, 5th Lurtz, 6th Elrond, 7th the Uruk-hai,
 * 8th Sauron. TT_WINS=n: as if n finishes (testing, over extra_heroes=2) */
static const int s_unlock_at[N_SEL] = { 2, 3, 4, 5, 6, 8, 7 };
static int guest_unlocked(int g)
{
    if (getenv("TT_WINS")) return g >= 0 && g < N_SEL && atoi(getenv("TT_WINS")) >= s_unlock_at[g];
    if (s_test_unlock || s_heroes_mode == 3) return 1;
    return g >= 0 && g < N_SEL && isildur_wins() >= s_unlock_at[g];
}


/* ===================================================================== */
/* the guests' upgrade-screen pictures                                   */
/* ===================================================================== */
/* UI\upgbigi.BFF and UI\upgradei.BFF hold Isildur's picture (key 0, 512x256
 * DXT1) for the upgrade and results screens. A guest's copy has its left
 * part replaced: a film still from the game's own UI\stills*.BFF (Gandalf,
 * Frodo, Lurtz) or, for Boromir (no still of him), a cameo made from his
 * model's face texture, faded into the background, and the name. */
#include "heroes_names.inc"
typedef struct { const char *stills; uint32_t key; int x0, y0, x1, y1; } UpgSrc;
static const UpgSrc s_upg_src[N_SEL] = {
    { NULL, 0, 0, 0, 46, 84 },                           /* Boromir: his face texture */
    { "stills2.BFF", 6, 0, 30, 380, 448 },               /* Gandalf at Saruman's table */
    { "stills1.BFF", 2, 40, 0, 520, 448 },               /* Frodo */
    { "stills2.BFF", 5, 0, 0, 330, 448 },                /* Lurtz */
    { NULL, 0, 0, 0, 40, 80 },                           /* Elrond: his face texture */
    { NULL, 0, 0, 0, 70, 100 },                          /* Sauron: his helm */
    { "stills3.BFF", 7, 120, 0, 520, 448 },              /* the Uruk-hai host */
};
static const struct { const uint8_t *fill, *edge; int w, h; } s_name_img[N_SEL] = {
    { s_name_boromir_fill, s_name_boromir_edge, NAME_BOROMIR_W, NAME_BOROMIR_H },
    { s_name_gandalf_fill, s_name_gandalf_edge, NAME_GANDALF_W, NAME_GANDALF_H },
    { s_name_frodo_fill, s_name_frodo_edge, NAME_FRODO_W, NAME_FRODO_H },
    { s_name_lurtz_fill, s_name_lurtz_edge, NAME_LURTZ_W, NAME_LURTZ_H },
    { s_name_elrond_fill, s_name_elrond_edge, NAME_ELROND_W, NAME_ELROND_H },
    { s_name_sauron_fill, s_name_sauron_edge, NAME_SAURON_W, NAME_SAURON_H },
    { s_name_urukhai_fill, s_name_urukhai_edge, NAME_URUKHAI_W, NAME_URUKHAI_H },
};

static void c565(uint16_t c, int *o) { o[0] = ((c >> 11) & 31) * 255 / 31; o[1] = ((c >> 5) & 63) * 255 / 63; o[2] = (c & 31) * 255 / 31; }
/* DXT1 -> 0xRRGGBB */
static void dxt1_decode(const unsigned char *d, int w, int h, uint32_t *out)
{
    int bx, by, i, k;
    for (by = 0; by < h; by += 4)
        for (bx = 0; bx < w; bx += 4, d += 8) {
            uint16_t a = (uint16_t)(d[0] | d[1] << 8), b = (uint16_t)(d[2] | d[3] << 8);
            uint32_t bits = rd32(d, 4), pal[4];
            int ca[3], cb[3], c[3];
            c565(a, ca); c565(b, cb);
            for (k = 0; k < 4; k++) {
                for (i = 0; i < 3; i++) {
                    if (k < 2) c[i] = k ? cb[i] : ca[i];
                    else if (a > b) c[i] = k == 2 ? (2 * ca[i] + cb[i]) / 3 : (ca[i] + 2 * cb[i]) / 3;
                    else c[i] = k == 2 ? (ca[i] + cb[i]) / 2 : 0;
                }
                pal[k] = (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | (uint32_t)c[2];
            }
            for (i = 0; i < 16; i++) out[(by + i / 4) * w + bx + i % 4] = pal[(bits >> (2 * i)) & 3];
        }
}
static uint16_t to565(int r, int g, int b) { return (uint16_t)(((r * 31 + 127) / 255) << 11 | ((g * 63 + 127) / 255) << 5 | ((b * 31 + 127) / 255)); }
static void dxt1_encode(const uint32_t *in, int w, int h, unsigned char *d)
{
    int bx, by, i, k;
    for (by = 0; by < h; by += 4)
        for (bx = 0; bx < w; bx += 4, d += 8) {
            uint32_t px[16], bits = 0;
            int lo = 0, hi = 0, lmin = 1 << 30, lmax = -1, ca[3], cb[3], pal[4][3];
            uint16_t a, b;
            for (i = 0; i < 16; i++) {
                uint32_t c = px[i] = in[(by + i / 4) * w + bx + i % 4];
                int l = (int)((c >> 16) & 255) * 3 + (int)((c >> 8) & 255) * 6 + (int)(c & 255);
                if (l < lmin) { lmin = l; lo = i; }
                if (l > lmax) { lmax = l; hi = i; }
            }
            a = to565((px[hi] >> 16) & 255, (px[hi] >> 8) & 255, px[hi] & 255);
            b = to565((px[lo] >> 16) & 255, (px[lo] >> 8) & 255, px[lo] & 255);
            if (a < b) { uint16_t t = a; a = b; b = t; }
            if (a == b) { if (a) b = (uint16_t)(a - 1); else a = 1; }   /* keep the 4-colour mode */
            c565(a, ca); c565(b, cb);
            for (i = 0; i < 3; i++) { pal[0][i] = ca[i]; pal[1][i] = cb[i]; pal[2][i] = (2 * ca[i] + cb[i]) / 3; pal[3][i] = (ca[i] + 2 * cb[i]) / 3; }
            for (i = 0; i < 16; i++) {
                int best = 0, bd = 1 << 30;
                for (k = 0; k < 4; k++) {
                    int dr = (int)((px[i] >> 16) & 255) - pal[k][0], dg = (int)((px[i] >> 8) & 255) - pal[k][1], db = (int)(px[i] & 255) - pal[k][2];
                    int dd = dr * dr + dg * dg + db * db;
                    if (dd < bd) { bd = dd; best = k; }
                }
                bits |= (uint32_t)best << (2 * i);
            }
            d[0] = (unsigned char)a; d[1] = (unsigned char)(a >> 8); d[2] = (unsigned char)b; d[3] = (unsigned char)(b >> 8);
            wr32(d, 4, bits);
        }
}
static uint32_t bilerp(const uint32_t *p, int w, int h, float x, float y)
{
    int x0 = (int)floorf(x), y0 = (int)floorf(y), k, i;
    float fx = x - x0, fy = y - y0, acc[3] = { 0, 0, 0 };
    for (k = 0; k < 4; k++) {
        int xx = x0 + (k & 1), yy = y0 + (k >> 1);
        float wt = ((k & 1) ? fx : 1 - fx) * ((k >> 1) ? fy : 1 - fy);
        uint32_t c;
        if (xx < 0) xx = 0;
        if (yy < 0) yy = 0;
        if (xx >= w) xx = w - 1;
        if (yy >= h) yy = h - 1;
        c = p[yy * w + xx];
        for (i = 0; i < 3; i++) acc[i] += wt * (float)((c >> (16 - 8 * i)) & 255);
    }
    return (uint32_t)acc[0] << 16 | (uint32_t)acc[1] << 8 | (uint32_t)acc[2];
}

#define PIC_W 280
#define PIC_FADE 80
/* the left PIC_W x 256 picture for guest g, or 0 */
/* a picture of the player's own for guest g: upgpics\<name>.bmp beside the
 * program (24- or 32-bit BMP, any size up to 2048), fitted to cover the
 * PIC_W x 256 area; its size in *sz (for the cache folder's name); or NULL */
static uint32_t *upg_custom_load(int g, int *w, int *h, long *sz)
{
    char p2[700]; FILE *f; unsigned char hd[54]; uint32_t off, *out; int x, y, bpp, bu, rowb;
    paths();
    snprintf(p2, sizeof p2, "%s/upgpics/%s.bmp", s_dir, s_skins[g].name);
    if (!(f = fopen(p2, "rb"))) return NULL;
    fseek(f, 0, SEEK_END); if (sz) *sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (fread(hd, 1, 54, f) != 54 || hd[0] != 'B' || hd[1] != 'M' || (hd[28] != 24 && hd[28] != 32)) { fclose(f); return NULL; }
    bpp = hd[28] / 8;
    off = rd32(hd, 10); *w = (int)rd32(hd, 18); *h = (int)rd32(hd, 22);
    bu = *h > 0; if (*h < 0) *h = -*h;
    if (*w <= 0 || *w > 2048 || *h <= 0 || *h > 2048) { fclose(f); return NULL; }
    rowb = (*w * bpp + 3) & ~3;
    out = (uint32_t *)malloc((size_t)*w * *h * 4);
    if (!out) { fclose(f); return NULL; }
    for (y = 0; y < *h; y++) {
        unsigned char row[2048 * 4];
        fseek(f, (long)(off + (uint32_t)y * (uint32_t)rowb), SEEK_SET);
        if (fread(row, 1, (size_t)rowb, f) != (size_t)rowb) { free(out); fclose(f); return NULL; }
        for (x = 0; x < *w; x++) out[(size_t)(bu ? *h - 1 - y : y) * *w + x] = (uint32_t)row[x * bpp + 2] << 16 | (uint32_t)row[x * bpp + 1] << 8 | row[x * bpp];
    }
    fclose(f);
    /* La imagen nueva debe invalidar cache aunque el BMP tenga igual tamano. */
    if(sz){uint32_t hash=2166136261u;for(size_t i=0;i<(size_t)*w * *h;i++)hash=(hash^out[i])*16777619u;
        *sz=(long)(hash^(uint32_t)*w*131u^(uint32_t)*h*977u);}
    return out;
}
static int upg_picture(int g, const char *ui_dir, uint32_t *pic)
{
    const UpgSrc *u = &s_upg_src[g];
    {   /* the player's own picture first */
        int cw, ch, x2, y2; uint32_t *cp = upg_custom_load(g, &cw, &ch, NULL);
        if (cp) {
            float r = PIC_W / (float)cw > 256.0f / ch ? PIC_W / (float)cw : 256.0f / ch, ox = (cw * r - PIC_W) * 0.5f, oy = (ch * r - 256) * 0.5f;
            for (y2 = 0; y2 < 256; y2++) for (x2 = 0; x2 < PIC_W; x2++) pic[y2 * PIC_W + x2] = bilerp(cp, cw, ch, (x2 + ox) / r, (y2 + oy) / r);
            free(cp);
            tt_dbg("[HEROES] %s: his upgrade picture from upgpics\\%s.bmp\n", s_skins[g].name, s_skins[g].name);
            return 1;
        }
    }
    char path[700], hp[700];
    unsigned char *f;
    size_t fl;
    uint32_t off, end, ty;
    int x, y;
    if (u->stills) {
        uint32_t *src;
        float sw = (float)(u->x1 - u->x0), sh = (float)(u->y1 - u->y0), r, ox;
        snprintf(path, sizeof path, "%s\\%s", ui_dir, u->stills);
        if (!host_path(path, hp, sizeof hp) || !(f = read_all(hp, &fl))) return 0;
        if (!res_find(f, fl, u->key, 350, &off, &end, &ty) || off + 0x80 + 640 * 448 * 4 > fl) { free(f); return 0; }
        src = (uint32_t *)malloc(640 * 448 * 4);
        if (!src) { free(f); return 0; }
        for (y = 0; y < 448; y++)
            for (x = 0; x < 640; x++) src[y * 640 + x] = rd32(f, off + 0x80 + (size_t)(y * 640 + x) * 4) & 0xFFFFFFu;
        free(f);
        /* cover: fill PIC_W x 256 from the crop, centred across */
        r = PIC_W / sw > 256.0f / sh ? PIC_W / sw : 256.0f / sh;
        ox = (sw * r - PIC_W) * 0.5f;
        for (y = 0; y < 256; y++)
            for (x = 0; x < PIC_W; x++)
                pic[y * PIC_W + x] = bilerp(src, 640, 448, u->x0 + (x + ox) / r, u->y0 + y / r);
        free(src);
        return 1;
    } else {
        /* Boromir, Elrond (Sauron his helm): half a face against his
         * texture's left edge; mirrored into a round cameo on black */
        uint32_t *tex;
        if (!host_path(s_skins[g].src, hp, sizeof hp) || !(f = read_all(hp, &fl))) return 0;
        if (!res_find(f, fl, s_skins[g].tex, 300, &off, &end, &ty) || off + 0x80 + 0x8000 > fl) { free(f); return 0; }
        tex = (uint32_t *)malloc(256 * 256 * 4);
        if (!tex) { free(f); return 0; }
        dxt1_decode(f + off + 0x80, 256, 256, tex);
        free(f);
        for (y = 0; y < 256; y++)
            for (x = 0; x < PIC_W; x++) {
                float cx = 135.0f, cy = 141.0f, R = 82.0f, dx = x - cx, dy = y - cy, d = sqrtf(dx * dx + dy * dy);
                float a = (R - d) / 14.0f;
                uint32_t bgc = 0x0C0A09u, c;
                if (a <= 0) { pic[y * PIC_W + x] = bgc; continue; }
                if (a > 1) a = 1;
                c = bilerp(tex, 256, 256, fabsf(dx) / R * (float)u->x1, (float)u->y0 + (dy / R * 0.5f + 0.5f) * (float)(u->y1 - u->y0));
                pic[y * PIC_W + x] = (uint32_t)(((c >> 16) & 255) * a + 12 * (1 - a)) << 16 | (uint32_t)(((c >> 8) & 255) * a + 10 * (1 - a)) << 8 | (uint32_t)((c & 255) * a + 9 * (1 - a));
            }
        free(tex);
        return 1;
    }
}


/* ===================================================================== */
/* the guest's figure for the upgrade screens                             */
/* ===================================================================== */
/* upgradei.BFF / upgbigi.BFF keys 0x36 / 0x37: Isildur standing, 128 x 256
 * in two DXT5 halves. The guest's in his place: his model drawn here, in
 * its own pose with the arms lowered, lit from the front left. */
static void dxt5_encode(const uint32_t *in, int w, int h, unsigned char *d)
{
    int bx, by;
    for (by = 0; by < h; by += 4)
        for (bx = 0; bx < w; bx += 4, d += 16) {
            uint32_t px[16];
            int i, amin = 255, amax = 0;
            uint64_t bits = 0;
            for (i = 0; i < 16; i++) {
                int a;
                px[i] = in[(by + i / 4) * w + bx + i % 4];
                a = (int)(px[i] >> 24);
                if (a < amin) amin = a;
                if (a > amax) amax = a;
            }
            d[0] = (unsigned char)amax; d[1] = (unsigned char)amin;
            for (i = 0; i < 16; i++) {
                int a = (int)(px[i] >> 24), k, best = 0, bd = 1 << 30;
                for (k = 0; k < 8; k++) {
                    int v = k == 0 ? amax : k == 1 ? amin : (amax == amin ? amax : ((8 - k) * amax + (k - 1) * amin) / 7);
                    int dd = (v - a) * (v - a);
                    if (dd < bd) { bd = dd; best = k; }
                }
                bits |= (uint64_t)best << (3 * i);
            }
            for (i = 0; i < 6; i++) d[2 + i] = (unsigned char)(bits >> (8 * i));
            dxt1_encode(px, 4, 4, d + 8);
        }
}
/* triangles of one strip into img: X, Nv, UV per vertex (already turned) */
typedef struct { float mnx, mxx, mnz, sc, lx, ly, lz; int W, H; uint32_t *img; float *zb; } FigView;
static void fig_strip(const FigView *v, const unsigned char *idx, uint32_t n, uint32_t base, uint32_t nv,
                      const float *X, const float *Nv, const float *UV, const uint32_t *tex, int tw, int th)
{
    uint32_t j;
    int W = v->W, H = v->H;
    for (j = 2; j < n; j++) {
        uint32_t t[3], k;
        float sx[3], sy2[3], sz[3], sh[3], u[3], vv[3], den, minx, maxx, miny, maxy;
        int x, y;
        t[0] = rd16(idx, 2 * (j - 2)); t[1] = rd16(idx, 2 * (j - 1)); t[2] = rd16(idx, 2 * j);
        if (t[0] == t[1] || t[1] == t[2] || t[0] == t[2] || t[0] >= nv || t[1] >= nv || t[2] >= nv) continue;
        for (k = 0; k < 3; k++) {
            const float *p = X + 3 * (base + t[k]), *nn = Nv + 3 * (base + t[k]);
            float d = nn[0] * v->lx + nn[1] * v->ly + nn[2] * v->lz;
            sx[k] = (p[0] - (v->mnx + v->mxx) * 0.5f) * v->sc + W * 0.5f; sy2[k] = H - 4 - (p[2] - v->mnz) * v->sc; sz[k] = -p[1];
            sh[k] = 0.4f + 0.75f * (d > 0 ? d : 0);
            u[k] = UV[2 * (base + t[k])]; vv[k] = UV[2 * (base + t[k]) + 1];
        }
        den = (sy2[1] - sy2[2]) * (sx[0] - sx[2]) + (sx[2] - sx[1]) * (sy2[0] - sy2[2]);
        if (fabsf(den) < 1e-9f) continue;
        minx = fminf(sx[0], fminf(sx[1], sx[2])); maxx = fmaxf(sx[0], fmaxf(sx[1], sx[2]));
        miny = fminf(sy2[0], fminf(sy2[1], sy2[2])); maxy = fmaxf(sy2[0], fmaxf(sy2[1], sy2[2]));
        for (y = (int)fmaxf(0, floorf(miny)); y <= (int)fminf(H - 1, ceilf(maxy)); y++)
            for (x = (int)fmaxf(0, floorf(minx)); x <= (int)fminf(W - 1, ceilf(maxx)); x++) {
                float gx = x + 0.5f, gy = y + 0.5f, w0, w1, w2, z, uu, vq, l;
                uint32_t tc, r, gg, bb;
                w0 = ((sy2[1] - sy2[2]) * (gx - sx[2]) + (sx[2] - sx[1]) * (gy - sy2[2])) / den;
                w1 = ((sy2[2] - sy2[0]) * (gx - sx[2]) + (sx[0] - sx[2]) * (gy - sy2[2])) / den;
                w2 = 1 - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                z = w0 * sz[0] + w1 * sz[1] + w2 * sz[2];
                if (z <= v->zb[y * W + x]) continue;
                uu = w0 * u[0] + w1 * u[1] + w2 * u[2]; vq = w0 * vv[0] + w1 * vv[1] + w2 * vv[2];
                uu -= floorf(uu); vq -= floorf(vq);
                tc = tex ? tex[((int)(vq * th) % th) * tw + ((int)(uu * tw) % tw)] : 0x909090u;
                l = w0 * sh[0] + w1 * sh[1] + w2 * sh[2];
                r = (uint32_t)fminf(255, ((tc >> 16) & 255) * l); gg = (uint32_t)fminf(255, ((tc >> 8) & 255) * l); bb = (uint32_t)fminf(255, (tc & 255) * l);
                v->zb[y * W + x] = z;
                v->img[y * W + x] = 0xFF000000u | r << 16 | gg << 8 | bb;
            }
    }
}
/* a DXT1 texture resource (header 0x80) decoded, its size in *w, *h; or NULL */
static uint32_t *fig_tex(const unsigned char *T, size_t tl, int *w, int *h)
{
    uint32_t *p;
    if (tl < 0x88) return NULL;
    *w = (int)rd32(T, 4); *h = (int)rd32(T, 8);
    if ((rd32(T, 0xC) & 0xFF) != 0x11 || *w < 4 || *h < 4 || *w > 1024 || *h > 1024 || 0x80 + (size_t)*w * *h / 2 > tl) return NULL;
    if ((p = (uint32_t *)malloc((size_t)*w * *h * 4)) != NULL) dxt1_decode(T + 0x80, *w, *h, p);
    return p;
}
/* vertices a..b-1 (and their normals) turned by an about the unit axis ax through G */
static void fig_turn(float *X, float *Nv, uint32_t a, uint32_t b, const float *G, const float *ax, float an)
{
    float cs = cosf(an), sn = sinf(an);
    uint32_t i;
    for (i = a; i < b; i++) {
        float *pp[2] = { X + 3 * i, Nv + 3 * i };
        int h2, k;
        for (h2 = 0; h2 < 2; h2++) {
            float v[3], cr[3], dt, *p = pp[h2];
            for (k = 0; k < 3; k++) v[k] = h2 ? p[k] : p[k] - G[k];
            cr[0] = ax[1] * v[2] - ax[2] * v[1]; cr[1] = ax[2] * v[0] - ax[0] * v[2]; cr[2] = ax[0] * v[1] - ax[1] * v[0];
            dt = ax[0] * v[0] + ax[1] * v[1] + ax[2] * v[2];
            for (k = 0; k < 3; k++) p[k] = v[k] * cs + cr[k] * sn + ax[k] * dt * (1 - cs) + (h2 ? 0 : G[k]);
        }
    }
}
static uint32_t *figure_render(int g)
{
    char hp[700];
    unsigned char *f, *wf = NULL, *mov = NULL, *tov = NULL;
    const unsigned char *M, *TX, *P = NULL;
    size_t fl, wl = 0, ml, txl, movl = 0, tovl = 0;
    uint32_t mo, me, ao, ae, to, te, ty, nb, mo2, po, vc, vo, ic, io, c, i, nw = 0, pioff = 0, pinb = 0, pa = 0, pe = 0;
    int W = 128, H = 256, tw = 0, th = 0, wtw = 0, wth = 0, hb = -1, posed = 0;
    uint32_t *img = NULL, *tex = NULL, *wtex = NULL;
    float *X = NULL, *zb = NULL, *Nv = NULL, *UV = NULL;
    float (*bp)[3] = NULL;
    int par[256], slot2bone[256];
    const char *ws; uint32_t wm, wt;
    if (!host_path(s_skins[g].src, hp, sizeof hp) || !(f = read_all(hp, &fl))) return NULL;
    if (!res_find(f, fl, s_skins[g].mesh, 100, &mo, &me, &ty) || !res_find(f, fl, s_skins[g].anim, 150, &ao, &ae, &ty)
        || !res_find(f, fl, s_skins[g].tex, 300, &to, &te, &ty)) { free(f); return NULL; }
    /* his model and texture as he plays (Boromir's with his shield) */
    M = f + mo; ml = me - mo; TX = f + to; txl = te - to;
    if ((mov = res_override(f, fl, s_skins[g].mesh, &movl)) != NULL) { M = mov; ml = movl; }
    if ((tov = res_override(f, fl, s_skins[g].tex, &tovl)) != NULL) { TX = tov; txl = tovl; }
    /* skeleton: parents and bind positions */
    nb = rd32(f, ao + 0x48); mo2 = rd32(f, ao + 0x4C); po = rd32(f, ao + 0x50);
    if (nb > 256) goto fail;
    bp = (float (*)[3])calloc(nb, sizeof *bp);
    for (i = 0; i < nb; i++) { par[i] = (int)f[ao + 0x40 + po + i] - 1; memcpy(bp[i], f + ao + 0x40 + mo2 + i * 128 + 28 * 4, 12); }
    /* the mesh's first part: palette, vertices (48 bytes from +0x34), strips (+0x4C) */
    vc = rd32(M, 0x30); vo = rd32(M, 0x38) + 0x34; ic = rd32(M, 0x48); io = rd32(M, 0x4C) + 0x4C; c = rd32(M, 0x50);
    for (i = 0; i < 256; i++) slot2bone[i] = -1;
    for (i = 0; i < c && i < 256; i++) slot2bone[M[0x54 + 2 * i + 1]] = M[0x54 + 2 * i];
    if (vo + vc * 48 > ml || io + ic * 2 > ml) goto fail;
    /* his sword-hand weapon: its prop ("prxb": 32-byte vertices from +0x1000,
     * a strip), at his hand's attach point in the bind pose */
    if (skin_weapon(g, 1, &ws, &wm, &wt) && wm != EMPTY_PROP_KEY && !getenv("TT_NOFIGW")) {
        if (!strcmp(ws, s_skins[g].src)) { wf = NULL; P = f; wl = fl; }
        else if (host_path(ws, hp, sizeof hp) && (wf = read_all(hp, &wl)) != NULL) P = wf;
        hb = (int)skel_point_bone(f + ao, ae - ao, 0);
        if (P && hb >= 0 && hb < (int)nb && res_find(P, wl, wm, 200, &pa, &pe, &ty) && !memcmp(P + pa, "prxb", 4)) {
            pioff = 0x1000 + rd32(P, pa + 0x1F0); pinb = rd32(P, pa + 0x204);
            if (pa + pioff + pinb <= pe && pinb >= 6) {
                for (i = 0; i < pinb / 2; i++) if (rd16(P, pa + pioff + 2 * i) + 1u > nw) nw = rd16(P, pa + pioff + 2 * i) + 1u;
                if (pa + 0x1000 + nw * 32 > pa + pioff) nw = 0;
            }
            if (nw) {
                uint32_t wo, we;
                if (res_find(P, wl, wt, 300, &wo, &we, &ty) && (wtex = fig_tex(P + wo, we - wo, &wtw, &wth)) != NULL && s_skins[g].glow && !getenv("TT_NOTINT"))
                    for (i = 0; i < (uint32_t)(wtw * wth); i++) {   /* Sting's pale blue, as the game's copy */
                        uint32_t q = wtex[i]; int r = (int)((q >> 16) & 255) * 2 / 3 + 6, gg = (int)((q >> 8) & 255) * 7 / 8 + 20, b = (int)(q & 255) + 14;
                        wtex[i] = (uint32_t)(r > 255 ? 255 : r) << 16 | (uint32_t)(gg > 255 ? 255 : gg) << 8 | (uint32_t)(b > 255 ? 255 : b);
                    }
            }
        }
    }
    X = (float *)malloc((vc + nw + 1) * 3 * sizeof(float)); Nv = (float *)malloc((vc + nw + 1) * 3 * sizeof(float)); UV = (float *)malloc((vc + nw + 1) * 2 * sizeof(float));
    img = (uint32_t *)calloc(W * H, 4); zb = (float *)malloc(W * H * sizeof(float));
    tex = fig_tex(TX, txl, &tw, &th);
    if (!X || !Nv || !UV || !tex || !img || !zb) goto fail;
    for (i = 0; i < vc; i++) { memcpy(X + 3 * i, M + vo + i * 48, 12); memcpy(Nv + 3 * i, M + vo + i * 48 + 12, 12); memcpy(UV + 2 * i, M + vo + i * 48 + 24, 8); }
    if (nw) {   /* the hand's bind matrix (rows; the last its place) */
        const unsigned char *B = f + ao + 0x40 + mo2 + (uint32_t)hb * 128 + 64;
        float m[4][3]; int r, k;
        for (r = 0; r < 4; r++) for (k = 0; k < 3; k++) memcpy(&m[r][k], B + (r * 4 + k) * 4, 4);
        for (i = 0; i < nw; i++) {
            float q[8];
            memcpy(q, P + pa + 0x1000 + 32 * i, 32);
            for (k = 0; k < 3; k++) {
                X[3 * (vc + i) + k] = q[0] * m[0][k] + q[1] * m[1][k] + q[2] * m[2][k] + m[3][k];
                Nv[3 * (vc + i) + k] = q[3] * m[0][k] + q[4] * m[1][k] + q[5] * m[2][k];
            }
            UV[2 * (vc + i)] = q[6]; UV[2 * (vc + i) + 1] = q[7];
        }
        for (k = 0; k < 3; k++) { X[3 * (vc + nw) + k] = m[3][k]; Nv[3 * (vc + nw) + k] = 0; }   /* and the grip, unseen */
        UV[2 * (vc + nw)] = UV[2 * (vc + nw) + 1] = 0;
    }
    {   /* 1.8: in his own stance -- his idle's first frame, the mesh
         * skinned to it (each vertex by its two bones: posed world times
         * the bind world's inverse), the weapon with his hand; before, the
         * bind pose with the arms swung down, which lost arms into the body
         * and cut off heads (TT_FIGBIND the old way) */
        const unsigned char *S = f + ao; size_t sl2 = ae - ao;
        int pk = -1, nbp; uint32_t ns = rd16(S, 6), atab = rd32(S, 0x10);
        static float PP[128][3]; static Quat BW[128], BLq[128]; static int bpar[128]; float BT[128][3];
        /* (1.7.2) the bind pose with the arms lowered, for all: each one's
         * first moment of a motion of his own stood him mid-stride or
         * twisted (Lurtz's walk: his legs run into one). TT_FIGSTANCE: his
         * stance as before, TT_FIGSLOT a slot (testing) */
        if (getenv("TT_FIGSTANCE") || getenv("TT_FIGSLOT")) {
            int gi = -1, k;
            for (k = 0; k < (int)(sizeof s_pools / sizeof s_pools[0]); k++) if (!strcmp(s_pools[k].name, s_skins[g].name)) gi = k;
            if (gi >= 0) pk = atoi(s_pools[gi].pools[AC_IDLE]);
            {   /* (the poses that read best, picked by eye: their stances) */
                static const struct { const char *n; int slot; } FS[] = { { "boromir", 34 }, { "gandalf", 34 }, { "frodo", 34 }, { "elrond", 34 }, { "lurtz", 0 }, { "sauron", 0 }, { "uruk", 0 } };
                for (k = 0; k < (int)(sizeof FS / sizeof FS[0]); k++) if (!strcmp(FS[k].n, s_skins[g].name)) pk = FS[k].slot;
            }
            if (pk < 0 || (uint32_t)pk >= ns || !rd32(S, atab + 4 * (uint32_t)pk)) pk = 0;
            if (getenv("TT_FIGSLOT")) pk = atoi(getenv("TT_FIGSLOT"));
        }
        if (pk >= 0 && (uint32_t)pk < ns && rd32(S, atab + 4 * (uint32_t)pk) && (nbp = rt_binds(S, sl2, bpar, BW, BLq)) > 0
            && rt_pose(S, sl2, S + rd32(S, atab + 4 * (uint32_t)pk), sl2 - rd32(S, atab + 4 * (uint32_t)pk), getenv("TT_FIGT") ? (float)atof(getenv("TT_FIGT")) : 0.0f, PP) == nbp) {
            uint32_t mo3 = 0x40 + rd32(S, 0x40 + 0xC), b2; int k;
            Quat SK[128];
            for (b2 = 0; b2 < (uint32_t)nbp; b2++) { for (k = 0; k < 3; k++) { uint32_t v = rd32(S, mo3 + b2 * 128 + 64 + 48 + 4 * (uint32_t)k); memcpy(&BT[b2][k], &v, 4); } SK[b2] = qnormq(qmulq(s_pose_Q[b2], qconj(BW[b2]))); }
            for (i = 0; i < vc + (nw ? nw + 1 : 0); i++) {
                float acc[3] = { 0, 0, 0 }, nacc[3] = { 0, 0, 0 }, wsum = 0;
                int bs[2] = { -1, -1 }; float wt2[2] = { 0, 0 }, sx, wx, sy, wy, *p = X + 3 * i, *n = Nv + 3 * i;
                if (i < vc) {
                    const unsigned char *v = M + vo + i * 48;
                    memcpy(&sx, v + 32, 4); memcpy(&wx, v + 36, 4); memcpy(&sy, v + 40, 4); memcpy(&wy, v + 44, 4);
                    if (wx > 0 && sx >= 0 && sx < 768) { bs[0] = slot2bone[(int)sx / 3]; wt2[0] = wx; }
                    if (wy > 0 && sy >= 0 && sy < 768) { bs[1] = slot2bone[(int)sy / 3]; wt2[1] = wy; }
                } else { bs[0] = hb; wt2[0] = 1; }
                for (k = 0; k < 2; k++) {
                    float d[3], r[3], rn[3]; int c2;
                    if (bs[k] < 0 || bs[k] >= nbp || wt2[k] <= 0) continue;
                    for (c2 = 0; c2 < 3; c2++) d[c2] = p[c2] - BT[bs[k]][c2];
                    qrotv(SK[bs[k]], d, r); qrotv(SK[bs[k]], n, rn);
                    for (c2 = 0; c2 < 3; c2++) { acc[c2] += wt2[k] * (r[c2] + PP[bs[k]][c2]); nacc[c2] += wt2[k] * rn[c2]; }
                    wsum += wt2[k];
                }
                if (wsum > 0) for (k = 0; k < 3; k++) { p[k] = acc[k] / wsum; n[k] = nacc[k] / wsum; }
            }
            posed = 1;
            tt_dbg("[HEROES] %s: his figure in his stance (slot %d)\n", s_skins[g].name, pk);
        }
    }
    if (!posed) {   /* the arms down: each arm (from the hand, the leaf farthest out on its
         * side, up to the bone nearest the middle) turned at its shoulder */
        int side;
        for (side = -1; side <= 1; side += 2) {
            int hand = -1, sh = -1, b;
            float best = 0, ang = 1.15f * side, cs = cosf(ang), sn = sinf(ang);
            unsigned char inarm[256];
            for (b = 0; b < (int)nb; b++) {
                int leaf = 1; uint32_t k;
                for (k = 0; k < nb; k++) if (par[k] == b) leaf = 0;
                if (leaf && bp[b][0] * side > best) { best = bp[b][0] * side; hand = b; }
            }
            if (hand < 0) continue;
            for (b = hand; b >= 0 && fabsf(bp[b][0]) >= 0.2f; b = par[b]) sh = b;
            if (sh < 0) continue;
            memset(inarm, 0, sizeof inarm);
            for (b = 0; b < (int)nb; b++) { int q = b; while (q >= 0 && q != sh) q = par[q]; if (q == sh) inarm[b] = 1; }
            for (i = 0; i < vc + (nw ? nw + 1 : 0); i++) {
                float w = 0, s0, w0, s1, w1, px, pz, nx, nz;
                if (i < vc) {
                    const unsigned char *v = M + vo + i * 48;
                    memcpy(&s0, v + 32, 4); memcpy(&w0, v + 36, 4); memcpy(&s1, v + 40, 4); memcpy(&w1, v + 44, 4);
                    if (w0 > 0 && s0 >= 0 && s0 < 768 && slot2bone[(int)s0 / 3] >= 0 && inarm[slot2bone[(int)s0 / 3]]) w += w0;
                    if (w1 > 0 && s1 >= 0 && s1 < 768 && slot2bone[(int)s1 / 3] >= 0 && inarm[slot2bone[(int)s1 / 3]]) w += w1;
                } else if (inarm[hb]) w = 1;   /* the weapon goes with its hand */
                if (w <= 0) continue;
                px = X[3 * i] - bp[sh][0]; pz = X[3 * i + 2] - bp[sh][2];
                X[3 * i] += w * (cs * px + sn * pz - px); X[3 * i + 2] += w * (-sn * px + cs * pz - pz);
                nx = Nv[3 * i]; nz = Nv[3 * i + 2];
                Nv[3 * i] += w * (cs * nx + sn * nz - nx); Nv[3 * i + 2] += w * (-sn * nx + cs * nz - nz);
            }
        }
    }
    if (nw && !posed && !getenv("TT_FIGWRAW")) {   /* the weapon hung from the hand: a blade's tip
         * down by his side, a staff upright (its far end turned to t) */
        const float *G = X + 3 * (vc + nw);
        float d[3] = { 0, 0, 0 }, best = -1, t[3], ax[3], cs, sn, an, l;
        int beam = s_skins[g].beam, k;
        for (i = vc; i < vc + nw; i++) {
            float e[3] = { X[3 * i] - G[0], X[3 * i + 1] - G[1], X[3 * i + 2] - G[2] }, q = e[0] * e[0] + e[1] * e[1] + e[2] * e[2];
            if (q > best) { best = q; d[0] = e[0]; d[1] = e[1]; d[2] = e[2]; }
        }
        t[0] = (G[0] > 0 ? 1 : -1) * (beam ? 0.05f : 0.45f); t[1] = beam ? 0.0f : -0.35f; t[2] = beam ? 1.0f : -1.0f;
        l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]); if (l > 0) for (k = 0; k < 3; k++) d[k] /= l;
        l = sqrtf(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]); for (k = 0; k < 3; k++) t[k] /= l;
        ax[0] = d[1] * t[2] - d[2] * t[1]; ax[1] = d[2] * t[0] - d[0] * t[2]; ax[2] = d[0] * t[1] - d[1] * t[0];
        sn = sqrtf(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]); cs = d[0] * t[0] + d[1] * t[1] + d[2] * t[2];
        an = atan2f(sn, cs);
        if (sn > 1e-6f) { for (k = 0; k < 3; k++) ax[k] /= sn; fig_turn(X, Nv, vc, vc + nw, G, ax, an); }
        {   /* and about its length, the flat of the blade to the front */
            float u[3] = { 0, 0, 0 }, wv[3] = { cosf(0.35f), -sinf(0.35f), 0 }, dt, s2;
            best = -1;
            for (i = vc; i < vc + nw; i++) {
                float e[3] = { X[3 * i] - G[0], X[3 * i + 1] - G[1], X[3 * i + 2] - G[2] }, q;
                dt = e[0] * t[0] + e[1] * t[1] + e[2] * t[2];
                for (k = 0; k < 3; k++) e[k] -= dt * t[k];
                q = e[0] * e[0] + e[1] * e[1] + e[2] * e[2];
                if (q > best) { best = q; u[0] = e[0]; u[1] = e[1]; u[2] = e[2]; }
            }
            dt = wv[0] * t[0] + wv[1] * t[1] + wv[2] * t[2];
            for (k = 0; k < 3; k++) wv[k] -= dt * t[k];
            l = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]); s2 = sqrtf(wv[0] * wv[0] + wv[1] * wv[1] + wv[2] * wv[2]);
            if (l > 1e-6f && s2 > 1e-6f) {
                for (k = 0; k < 3; k++) { u[k] /= l; wv[k] /= s2; }
                if (u[0] * wv[0] + u[1] * wv[1] + u[2] * wv[2] < 0) for (k = 0; k < 3; k++) u[k] = -u[k];
                ax[0] = u[1] * wv[2] - u[2] * wv[1]; ax[1] = u[2] * wv[0] - u[0] * wv[2]; ax[2] = u[0] * wv[1] - u[1] * wv[0];
                sn = ax[0] * t[0] + ax[1] * t[1] + ax[2] * t[2]; cs = u[0] * wv[0] + u[1] * wv[1] + u[2] * wv[2];
                if (!beam) fig_turn(X, Nv, vc, vc + nw, G, t, atan2f(sn, cs));
            }
        }
    }
    {   /* a quarter turn to his left, fitted to the picture */
        float yaw = 0.35f, cy = cosf(yaw), sy = sinf(yaw), mxz = -1e9f, ln;
        FigView v;
        v.mnx = 1e9f; v.mxx = -1e9f; v.mnz = 1e9f; v.lx = 0.45f; v.ly = -0.65f; v.lz = 0.6f; v.W = W; v.H = H; v.img = img; v.zb = zb;
        ln = sqrtf(v.lx * v.lx + v.ly * v.ly + v.lz * v.lz);
        v.lx /= ln; v.ly /= ln; v.lz /= ln;
        for (i = 1; i < vc + nw; i++) {
            float x = X[3 * i], y = X[3 * i + 1];
            X[3 * i] = cy * x - sy * y; X[3 * i + 1] = sy * x + cy * y;
            x = Nv[3 * i]; y = Nv[3 * i + 1];
            Nv[3 * i] = cy * x - sy * y; Nv[3 * i + 1] = sy * x + cy * y;
            if (X[3 * i] < v.mnx) v.mnx = X[3 * i]; if (X[3 * i] > v.mxx) v.mxx = X[3 * i];
            if (X[3 * i + 2] < v.mnz) v.mnz = X[3 * i + 2]; if (X[3 * i + 2] > mxz) mxz = X[3 * i + 2];
        }
        v.sc = (W - 10) / (v.mxx - v.mnx); if ((H - 8) / (mxz - v.mnz) < v.sc) v.sc = (H - 8) / (mxz - v.mnz);
        for (i = 0; i < (uint32_t)(W * H); i++) zb[i] = -1e9f;
        fig_strip(&v, M + io, ic, 0, vc, X, Nv, UV, tex, tw, th);
        if (nw) fig_strip(&v, P + pa + pioff, pinb / 2, vc, nw, X, Nv, UV, wtex, wtw, wth);
    }
    tt_dbg("[HEROES] %s: his figure for the upgrades, %u vertices of weapon on bone %d\n", s_skins[g].name, nw, hb);
    free(X); free(Nv); free(UV); free(tex); free(wtex); free(zb); free(bp); free(wf); free(mov); free(tov); free(f);
    return img;
fail:
    free(X); free(Nv); free(UV); free(tex); free(wtex); free(zb); free(img); free(bp); free(wf); free(mov); free(tov); free(f);
    return NULL;
}
/* the figure into the file image t (keys 0x36 top, 0x37 bottom), if it has them */
/* ---- the upgrade screens' item pictures (1.8) ----
 * upgradei.BFF and upgbigi.BFF hold the pictures shown beside the upgrades
 * (type 350, 128x256, DXT5): keys 0x11-0x16 the bow and arrows (plain, then
 * glowing blue, green, white, purple, red: the ranged upgrades), 0x17-0x1A
 * the sword (plain, blue, green, red). For a guest: his own weapon, drawn
 * from its model (the sword hand's: Sauron's mace, the Uruk-hai's great
 * sword, Sting...), and for his ranged attack his picture
 * (upgitems\<name>_ranged.bmp, 128x256 with alpha: Gandalf's staff,
 * Frodo's stone, the Uruk-hai's burning stone, Sauron's hand) or his own
 * bow's model (Lurtz). Each glowing copy: the plain picture in a halo of
 * that copy's colour (measured on the game's). */
static int weapon_picture(int g, int slot, uint32_t *out)
{
    const char *ws; uint32_t wm, wt, pa, pe, ty, pioff, pinb, nw = 0, i, wo, we;
    char hp[700]; unsigned char *P; size_t wl;
    float *X = NULL, *Nv = NULL, *UV = NULL, *zb = NULL;
    uint32_t *img = NULL, *wtex = NULL;
    int wtw = 0, wth = 0, ok = 0, W = 256, H = 512, x, y;
    if (!skin_weapon(g, slot, &ws, &wm, &wt) || wm == EMPTY_PROP_KEY || !host_path(ws, hp, sizeof hp) || !(P = read_all(hp, &wl))) return 0;
    if (!res_find(P, wl, wm, 200, &pa, &pe, &ty) || memcmp(P + pa, "prxb", 4)) goto done;
    pioff = 0x1000 + rd32(P, pa + 0x1F0); pinb = rd32(P, pa + 0x204);
    if (pa + pioff + pinb > pe || pinb < 6) goto done;
    for (i = 0; i < pinb / 2; i++) if (rd16(P, pa + pioff + 2 * i) + 1u > nw) nw = rd16(P, pa + pioff + 2 * i) + 1u;
    if (!nw || pa + 0x1000 + nw * 32 > pa + pioff) goto done;
    if (res_find(P, wl, wt, 300, &wo, &we, &ty)) wtex = fig_tex(P + wo, we - wo, &wtw, &wth);
    if (wtex && s_skins[g].glow && slot == 1)
        for (i = 0; i < (uint32_t)(wtw * wth); i++) {   /* Sting's pale blue */
            uint32_t q = wtex[i]; int r = (int)((q >> 16) & 255) * 2 / 3 + 6, gg = (int)((q >> 8) & 255) * 7 / 8 + 20, b = (int)(q & 255) + 14;
            wtex[i] = (uint32_t)(r > 255 ? 255 : r) << 16 | (uint32_t)(gg > 255 ? 255 : gg) << 8 | (uint32_t)(b > 255 ? 255 : b);
        }
    X = (float *)malloc(nw * 3 * sizeof(float)); Nv = (float *)malloc(nw * 3 * sizeof(float)); UV = (float *)malloc(nw * 2 * sizeof(float));
    img = (uint32_t *)calloc((size_t)W * H, 4); zb = (float *)malloc((size_t)W * H * sizeof(float));
    if (!X || !Nv || !UV || !img || !zb) goto done;
    {   /* its length (from the grip, the model's origin, to its farthest point:
         * the tip) down to the left, its flat to the viewer */
        float u[3] = { 0, 0, 0 }, sd[3] = { 0, 0, 0 }, n[3], best = -1, l, an = !strcmp(s_skins[g].name, "sauron") ? 0.40f + 3.14159f : 0.40f, ca = cosf(an), sa = sinf(an), mnx = 1e9f, mxx = -1e9f, mnz = 1e9f, mxz = -1e9f;   /* (a mace's head up) */
        int k;
        for (i = 0; i < nw; i++) { float q[3], d; memcpy(q, P + pa + 0x1000 + 32 * i, 12); d = q[0] * q[0] + q[1] * q[1] + q[2] * q[2]; if (d > best) { best = d; u[0] = q[0]; u[1] = q[1]; u[2] = q[2]; } }
        l = sqrtf(best); if (l < 1e-6f) goto done;
        for (k = 0; k < 3; k++) u[k] /= l;
        best = -1;
        for (i = 0; i < nw; i++) {
            float q[3], dt, e[3], d;
            memcpy(q, P + pa + 0x1000 + 32 * i, 12);
            dt = q[0] * u[0] + q[1] * u[1] + q[2] * u[2];
            for (k = 0; k < 3; k++) e[k] = q[k] - dt * u[k];
            d = e[0] * e[0] + e[1] * e[1] + e[2] * e[2];
            if (d > best) { best = d; sd[0] = e[0]; sd[1] = e[1]; sd[2] = e[2]; }
        }
        l = sqrtf(best); if (l < 1e-6f) { sd[0] = -u[1]; sd[1] = u[0]; sd[2] = 0; l = sqrtf(sd[0] * sd[0] + sd[1] * sd[1]); if (l < 1e-6f) { sd[0] = 1; sd[1] = 0; l = 1; } }
        for (k = 0; k < 3; k++) sd[k] /= l;
        n[0] = u[1] * sd[2] - u[2] * sd[1]; n[1] = u[2] * sd[0] - u[0] * sd[2]; n[2] = u[0] * sd[1] - u[1] * sd[0];
        for (i = 0; i < nw; i++) {
            float q[8], a, b, c, na, nb2, nc, sx, sy;
            memcpy(q, P + pa + 0x1000 + 32 * i, 32);
            a = q[0] * u[0] + q[1] * u[1] + q[2] * u[2]; b = q[0] * sd[0] + q[1] * sd[1] + q[2] * sd[2]; c = q[0] * n[0] + q[1] * n[1] + q[2] * n[2];
            na = q[3] * u[0] + q[4] * u[1] + q[5] * u[2]; nb2 = q[3] * sd[0] + q[4] * sd[1] + q[5] * sd[2]; nc = q[3] * n[0] + q[4] * n[1] + q[5] * n[2];
            /* screen: x right, z up; the tip (+a) down and to the left */
            sx = b * ca - a * sa; sy = -(b * sa + a * ca);
            X[3 * i] = sx; X[3 * i + 1] = -c; X[3 * i + 2] = sy;
            Nv[3 * i] = nb2 * ca - na * sa; Nv[3 * i + 1] = -nc; Nv[3 * i + 2] = -(nb2 * sa + na * ca);
            UV[2 * i] = q[6]; UV[2 * i + 1] = q[7];
            if (sx < mnx) mnx = sx; if (sx > mxx) mxx = sx; if (sy < mnz) mnz = sy; if (sy > mxz) mxz = sy;
        }
        {
            FigView v; float ln;
            v.mnx = mnx; v.mxx = mxx; v.mnz = mnz; v.W = W; v.H = H; v.img = img; v.zb = zb;
            v.lx = 0.35f; v.ly = -0.75f; v.lz = 0.55f; ln = sqrtf(v.lx * v.lx + v.ly * v.ly + v.lz * v.lz); v.lx /= ln; v.ly /= ln; v.lz /= ln;
            v.sc = (W - 40) / (mxx - mnx); if ((H - 40) / (mxz - mnz) < v.sc) v.sc = (H - 40) / (mxz - mnz);
            v.mnz = mnz - 16.0f / v.sc;
            for (i = 0; i < (uint32_t)(W * H); i++) zb[i] = -1e9f;
            fig_strip(&v, P + pa + pioff, pinb / 2, 0, nw, X, Nv, UV, wtex, wtw, wth);
        }
    }
    for (y = 0; y < 256; y++) for (x = 0; x < 128; x++) {   /* half size, smoothed */
        uint32_t acc[4] = { 0, 0, 0, 0 }; int k, q;
        for (q = 0; q < 4; q++) {
            uint32_t c = img[(2 * y + q / 2) * W + 2 * x + q % 2];
            for (k = 0; k < 4; k++) acc[k] += (c >> (24 - 8 * k)) & 255;
        }
        out[y * 128 + x] = 0;
        if (acc[0]) {   /* colour by coverage */
            uint32_t a = acc[0] / 4, r = 0, gg = 0, b = 0;
            for (q = 0; q < 4; q++) { uint32_t c = img[(2 * y + q / 2) * W + 2 * x + q % 2]; if (c >> 24) { r += (c >> 16) & 255; gg += (c >> 8) & 255; b += c & 255; } }
            k = (int)(acc[0] / 255);
            out[y * 128 + x] = a << 24 | (r / (uint32_t)k) << 16 | (gg / (uint32_t)k) << 8 | (b / (uint32_t)k);
        }
    }
    ok = 1;
done:
    free(X); free(Nv); free(UV); free(zb); free(img); free(wtex); free(P);
    return ok;
}
/* pic (128x256 ARGB) in a halo of colour col (0 none): into o */
static void item_glow(const uint32_t *pic, uint32_t col, uint32_t *o)
{
    static float a1[128 * 256], a2[128 * 256];
    int x, y, k, R = 7;
    if (!col) { memcpy(o, pic, 128 * 256 * 4); return; }
    for (y = 0; y < 256; y++) for (x = 0; x < 128; x++) {   /* the alpha blurred: rows, then columns */
        float sacc = 0; int c = 0;
        for (k = -R; k <= R; k++) if (x + k >= 0 && x + k < 128) { sacc += (float)(pic[y * 128 + x + k] >> 24); c++; }
        a1[y * 128 + x] = sacc / (float)(2 * R + 1);
    }
    for (y = 0; y < 256; y++) for (x = 0; x < 128; x++) {
        float sacc = 0;
        for (k = -R; k <= R; k++) if (y + k >= 0 && y + k < 256) sacc += a1[(y + k) * 128 + x];
        a2[y * 128 + x] = sacc / (float)(2 * R + 1);
    }
    for (y = 0; y < 256 * 128; y++) {
        float ga = a2[y] / 255.0f * 1.6f, pa = (float)(pic[y] >> 24) / 255.0f, oa, cc[3];
        uint32_t oc = 0;
        if (ga > 0.6f) ga = 0.6f;
        oa = pa + ga * (1 - pa);
        for (k = 0; k < 3; k++) {
            float pc = (float)((pic[y] >> (16 - 8 * k)) & 255), gc = (float)((col >> (16 - 8 * k)) & 255);
            pc = pc * (1 - 0.10f * ga) + gc * 0.10f * ga;   /* (the metal catching the light) */
            cc[k] = oa > 0 ? (pc * pa + gc * ga * (1 - pa)) / oa : 0;
            oc |= (uint32_t)(cc[k] > 255 ? 255 : cc[k]) << (16 - 8 * k);
        }
        o[y] = oc | (uint32_t)(oa * 255.0f) << 24;
    }
}
static void upg_items_put(int g, unsigned char *t, size_t tl)
{
    static const uint32_t bowcol[6] = { 0, 0xB9D6FFu, 0xBEFFB5u, 0xFFF4E0u, 0xFFCEF2u, 0xFF7647u };
    static const uint32_t swcol[4] = { 0, 0x8BF0FFu, 0xB2FFBCu, 0xFF2B1Eu };
    static uint32_t pic[128 * 256], o2[128 * 256];
    uint32_t off, end, ty, k;
    int have, w = 0, h = 0;
    char p2[700];
    if (g < 0 || g >= N_SEL || getenv("TT_NOUPGITEMS")) return;
    /* the ranged pictures */
    paths();
    snprintf(p2, sizeof p2, "%s/upgitems/%s_ranged.bmp", s_dir, ranged_art(g));
    {
        uint32_t *art = bmp_load_argb(p2, &w, &h, NULL);
        have = 0;
        if (art && w == 128 && h == 256) { memcpy(pic, art, sizeof pic); have = 1; }
        free(art);
        if (!have && !strcmp(s_skins[g].name, "lurtz")) have = weapon_picture(g, 0, pic);   /* his own bow */
    }
    if (have)
        for (k = 0; k < 6; k++)
            if (res_find(t, tl, 0x11 + k, 350, &off, &end, &ty) && end - off >= 0x80 + 128 * 256 && rd32(t, off + 4) == 128 && rd32(t, off + 8) == 256 && (rd32(t, off + 0xC) & 0xFF) == 0x13) {
                item_glow(pic, bowcol[k], o2); dxt5_encode(o2, 128, 256, t + off + 0x80);
            }
    /* the melee ones: his sword hand's weapon */
    if (weapon_picture(g, 1, pic)) {
        for (k = 0; k < 4; k++)
            if (res_find(t, tl, 0x17 + k, 350, &off, &end, &ty) && end - off >= 0x80 + 128 * 256 && rd32(t, off + 4) == 128 && rd32(t, off + 8) == 256 && (rd32(t, off + 0xC) & 0xFF) == 0x13) {
                item_glow(pic, swcol[k], o2); dxt5_encode(o2, 128, 256, t + off + 0x80);
            }
        tt_dbg("[HEROES] %s: his weapon on the upgrade screens%s\n", s_skins[g].name, have ? ", and his ranged picture" : "");
    }
    if (getenv("TT_UPGITEMDUMP")) {   /* testing: the pictures as made */
        FILE *f; snprintf(p2, sizeof p2, "%s/heroes/items_%s.raw", s_dir, s_skins[g].name);
        if ((f = fopen(p2, "wb"))) { fwrite(pic, 4, 128 * 256, f); fclose(f); }
    }
}
static void figure_put(int g, unsigned char *t, size_t tl)
{
    uint32_t o[2], e, ty, *img;
    int h;
    if (!res_find(t, tl, 0x36, 350, &o[0], &e, &ty) || !res_find(t, tl, 0x37, 350, &o[1], &e, &ty)) return;
    for (h = 0; h < 2; h++) if (rd32(t, o[h] + 4) != 128 || rd32(t, o[h] + 8) != 128 || o[h] + 0x80 + 0x4000 > tl) return;
    if (!(img = figure_render(g))) return;
    for (h = 0; h < 2; h++) dxt5_encode(img + h * 128 * 128, 128, 128, t + o[h] + 0x80);
    free(img);
}

/* the guest's copy of an upgrade-picture file (target: host path) */
static int build_upg(int g, const char *target, const char *ui_dir, const char *out, uint32_t pkey)
{
    size_t tl;
    unsigned char *t = read_all(target, &tl);
    uint32_t off, end, ty, *img = NULL, *pic = NULL;
    int x, y, ok = 0;
    FILE *f;
    if (!t || !res_find(t, tl, pkey, 350, &off, &end, &ty) || off + 0x80 + 0x10000 > tl || rd32(t, off + 4) != 512 || rd32(t, off + 8) != 256) goto done;
    img = (uint32_t *)malloc(512 * 256 * 4);
    pic = (uint32_t *)malloc(PIC_W * 256 * 4);
    if (!img || !pic) goto done;
    dxt1_decode(t + off + 0x80, 512, 256, img);
    if (!upg_picture(g, ui_dir, pic)) goto done;
    for (y = 0; y < 256; y++)
        for (x = 0; x < PIC_W; x++) {
            float a = x < PIC_W - PIC_FADE ? 1.0f : (float)(PIC_W - x) / PIC_FADE, dk;
            uint32_t b = img[y * 512 + x], c = pic[y * PIC_W + x], o = 0;
            int k;
            a = a * a * (3 - 2 * a);
            dk = 0.25f + 0.75f * (1 - a);   /* Isildur behind the fade: darkened */
            for (k = 0; k < 3; k++) {
                float v = (float)((c >> (16 - 8 * k)) & 255) * a + (float)((b >> (16 - 8 * k)) & 255) * dk * (1 - a);
                o |= (uint32_t)(v > 255 ? 255 : v) << (16 - 8 * k);
            }
            img[y * 512 + x] = o;
        }
    {   /* the name: cream letters with a dark edge, where Isildur's was */
        int nw = s_name_img[g].w, nh = s_name_img[g].h, X0 = 32, Y0 = 0;
        for (y = 0; y < nh && Y0 + y < 256; y++)
            for (x = 0; x < nw && X0 + x < 512; x++) {
                uint32_t *p = &img[(Y0 + y) * 512 + X0 + x];
                float e = s_name_img[g].edge[y * nw + x] / 255.0f, fl = s_name_img[g].fill[y * nw + x] / 255.0f;
                int k;
                uint32_t o = 0;
                static const int dark[3] = { 20, 12, 6 }, cream[3] = { 238, 228, 200 };
                for (k = 0; k < 3; k++) {
                    float v = (float)((*p >> (16 - 8 * k)) & 255);
                    v = v * (1 - e) + dark[k] * e;
                    v = v * (1 - fl) + cream[k] * fl;
                    o |= (uint32_t)v << (16 - 8 * k);
                }
                *p = o;
            }
    }
    dxt1_encode(img, 512, 256, t + off + 0x80);
    if (!getenv("TT_NOFIG")) figure_put(g, t, tl);
    upg_items_put(g, t, tl);
    mkdirs(out);
    if ((f = fopen(out, "wb"))) { ok = fwrite(t, 1, tl, f) == tl; fclose(f); if (!ok) remove(out); }
done:
    free(img); free(pic); free(t);
    return ok;
}

/* The guest's copies of the pictures that name hero 4: the upgrade screens
 * (upgbigi.BFF, upgradei.BFF: picture key 0) and the mission results
 * (endlvl.BFF: key 3, Isildur's of the four). upgbigi.BFF also has the 3D
 * figure that shows the moves (Isildur's: mesh 1050 / texture 1150 key 9,
 * on the heroes' skeleton): his model, re-rigged, in its place. */
static const char *upg_redirect(const char *xp, const char *base)
{
    static char out[700];
    char tgt[700], dir[600], tmp[720];
    const char *sl;
    FILE *f;
    int g = s_guest, end = !_stricmp(base, "endlvl.BFF"), fig = !_stricmp(base, "upgbigi.BFF");
    if (g < 0 || g >= N_SEL || (G8(HERO_BYTE) != 4 && !getenv("TT_UPG_SELFTEST"))) return NULL;
    paths();
    {   /* (the player's own picture, if any, in the folder's name: a new one is used) */
        int cw, ch; long csz = 0; uint32_t *cp = upg_custom_load(g, &cw, &ch, &csz);
        free(cp);
        char ip[700]; struct stat fa; long isz = 0;
        snprintf(ip, sizeof ip, "%s/upgitems/%s_ranged.bmp", s_dir, ranged_art(g));
        if (!stat(ip, &fa)) isz = (long)fa.st_size + (long)(fa.st_mtime & 0xFFFF);   /* (and his ranged picture's) */
        if (cp) snprintf(out, sizeof out, "%s/heroes/%s/upg%u_p%ld_i%ld/%s", s_dir, s_skins[g].name, HEROES_BUILD, csz, isz, base);
        else snprintf(out, sizeof out, "%s/heroes/%s/upg%u_i%ld/%s", s_dir, s_skins[g].name, HEROES_BUILD, isz, base);
    }
    if ((f = fopen(out, "rb"))) { fclose(f); return out; }
    if (!host_path(xp, tgt, sizeof tgt)) return NULL;
    sl = strrchr(xp, '\\');
    snprintf(dir, sizeof dir, "%.*s", (int)(sl ? sl - xp : 0), xp);
    snprintf(tmp, sizeof tmp, "%s.tmp", out);
    if (!build_upg(g, tgt, dir, fig ? tmp : out, end ? 3u : 0u)) { tt_dbg("[HEROES] could not build %s\n", out); return NULL; }
    if (fig && !getenv("TT_NOUPGFIG")) {
        Add a[5];
        int na = 2;
        const char *ws; uint32_t wm, wt;
        memset(a, 0, sizeof a);
        a[0].src = s_skins[g].src; a[0].src_key = s_skins[g].mesh; a[0].type = 1050; a[0].key = UPG_FIG_KEY;
        /* on his own skeleton with his own moves (as on the select): re-rigged
         * onto Isildur's, Frodo's head and coat came apart (TT_FIGREMAP: the old
         * way). Lurtz keeps the old way: on his own he is too tall for the screen. */
        const int figremap = getenv("TT_FIGREMAP") != NULL;   /* (1.7.2: Lurtz too, on his own skeleton: re-rigged onto Isildur's his legs ran into one) */
        if (figremap) { a[0].map = s_skins[g].map; a[0].nmap = s_skins[g].nmap; }
        else { a[0].map = s_idmap; a[0].nmap = 256; }
        a[1].src = s_skins[g].src; a[1].src_key = s_skins[g].tex; a[1].type = 1150; a[1].key = UPG_FIG_KEY;
        if (skin_weapon(g, 1, &ws, &wm, &wt) && wm != EMPTY_PROP_KEY && !getenv("TT_NOMENUWEAPON")) {   /* his weapon in the figure's hand (Isildur's sword, key 10) */
            a[2].src = ws; a[2].src_key = wm; a[2].type = 1200; a[2].key = UPG_FIG_KEY + 1;
            a[3].src = ws; a[3].src_key = wt; a[3].type = 1250; a[3].key = UPG_FIG_KEY + 1; a[3].tint = s_skins[g].glow && !getenv("TT_NOTINT");
            na = 4;
        }
        if (!figremap) { a[na].src = s_skins[g].src; a[na].src_key = s_skins[g].anim; a[na].type = 1100; a[na].key = UPG_FIG_KEY; a[na].compose = g + 1; na++; }
        s_remap_noshadow = 1; s_compose_menu = 1; s_compose_hkey = UPG_FIG_KEY;
        f = NULL;
        if (!build_file(tmp, a, na, out)) { s_remap_noshadow = 0; s_compose_menu = 0; s_compose_hkey = MENU_ANIM_KEY; remove(tmp); tt_dbg("[HEROES] could not build %s\n", out); return NULL; }
        s_remap_noshadow = 0; s_compose_menu = 0; s_compose_hkey = MENU_ANIM_KEY;
        remove(tmp);
    } else if (fig) rename(tmp, out);
    tt_dbg("[HEROES] built %s\n", out);
    return out;
}

/* ===================================================================== */
/* the guests' voices                                                     */
/* ===================================================================== */
/* A level's sound bank (sound\WB_<level>.xwb, "WBND" version 1): a 0x50
 * header ([0xC] the wave count), the waves' entries (20 bytes: format,
 * offset in the data, length, loop start, loop length), then the data --
 * Xbox ADPCM, 36-byte blocks of 64 samples. The format: bits 0-1 the
 * codec (3 ADPCM), 2-4 channels - 1, 5-22 the rate. sound\<level>.tbl
 * (0xDEADBEEF, count, {id, ...} pairs) names the waves: the id's place in
 * it is the wave's index. The game plays the hero's voice (his attacks,
 * blows taken, falls and death) by id -- Aragorn's 1, 3, 4, 6 (efforts), 7,
 * 8, 10 (struck), 11 (the death cry) and 12 (the last breath), and they are
 * the guest's too (he plays on Aragorn's moves). For the guest, a copy of the
 * level's bank whose entries for those ids point at the guest's voice: data
 * added after the bank's own (the game loads the whole file into its 3 MB of
 * sound memory and plays an entry from wherever it points). The count stays:
 * entries past the original count are not played.
 *
 * The voices come from the game's own banks: other levels' (Legolas's, the
 * Uruk-hai's, Lurtz's) and the streamed ones (S_WB_<level>.xwb, "WBND"
 * version 2: named waves, Frodo's and Gandalf's lines), pitched by their
 * rate (slower is lower), shortened with a fade where long. */
#define VOX_N 9
static const uint32_t s_vox_ids[VOX_N] = { 1, 3, 4, 6, 7, 8, 10, 11, 12 };
/* bank: a level ("17TOP1"), "S:<level>" a streamed bank (by name), NULL
 * the level being played; pitch (the rate's factor); longest (seconds) */
typedef struct { const char *bank; uint32_t id; const char *name; float pitch, longest; } VSrc;
#define VA(b, i, p, l) { b, i, NULL, p, l }
typedef struct { VSrc v[VOX_N]; VSrc select; float lead; } Voice;
static const Voice s_voice[N_EXTRA] = {
    /* Boromir: Aragorn's, deeper; on the character select Aragorn's line, deeper */
    { { VA(NULL, 1, 0.90f, 1.5f), VA(NULL, 3, 0.90f, 1.5f), VA(NULL, 4, 0.90f, 1.5f), VA(NULL, 6, 0.90f, 1.5f),
        VA(NULL, 7, 0.90f, 1.5f), VA(NULL, 8, 0.90f, 1.5f), VA(NULL, 10, 0.90f, 1.5f), VA(NULL, 11, 0.90f, 1.5f), VA(NULL, 12, 0.90f, 2.0f) },
      VA("INTRFACE", 2729, 0.90f, 4.0f), 0.0f },
    /* Gandalf: Gimli's (the Deeping Wall's), a little deeper, in play and
     * on the select (his line there) -- as asked for the 1.6 release after;
     * before, Aragorn's own */
    { { VA("17TOP1", 265, 0.93f, 1.5f), VA("17TOP1", 266, 0.93f, 1.5f), VA("17TOP1", 267, 0.93f, 1.5f), VA("17TOP1", 271, 0.93f, 1.5f),
        VA("17TOP1", 268, 0.93f, 1.5f), VA("17TOP1", 269, 0.93f, 1.5f), VA("17TOP1", 273, 0.93f, 1.5f), VA("17TOP1", 272, 0.93f, 1.5f), VA("17TOP1", 269, 0.86f, 1.5f) },
      VA("INTRFACE", 2731, 0.93f, 4.0f), 0.0f },
    /* Frodo: Legolas's, in play and on the select (1.9.1) */
    { { VA("17TOP1", 278, 1.0f, 1.5f), VA("17TOP1", 279, 1.0f, 1.5f), VA("17TOP1", 280, 1.0f, 1.5f), VA("17TOP1", 284, 1.0f, 1.5f),
        VA("17TOP1", 281, 1.0f, 1.5f), VA("17TOP1", 282, 1.0f, 1.5f), VA("17TOP1", 283, 1.0f, 1.5f), VA("17TOP1", 285, 1.0f, 1.5f), VA("17TOP1", 282, 0.92f, 1.5f) },
      VA("INTRFACE", 2733, 1.0f, 4.0f), 0.0f },
    /* Lurtz: his own (Amon Hen) */
    { { VA("13AMON1", 1606, 1.0f, 1.3f), VA("13AMON1", 1607, 1.0f, 1.3f), VA("13AMON1", 1608, 1.0f, 1.3f), VA("13AMON1", 1609, 1.0f, 1.3f),
        VA("13AMON1", 1610, 1.0f, 1.2f), VA("13AMON1", 1611, 1.0f, 1.2f), VA("13AMON1", 1612, 1.0f, 1.2f), VA("13AMON1", 1614, 1.0f, 1.6f), VA("13AMON1", 1613, 1.0f, 1.6f) },
      VA("13AMON1", 1606, 1.0f, 1.8f), 0.3f },
    /* Elrond: Legolas's; on the character select Legolas's own */
    { { VA("17TOP1", 278, 1.0f, 1.5f), VA("17TOP1", 279, 1.0f, 1.5f), VA("17TOP1", 280, 1.0f, 1.5f), VA("17TOP1", 284, 1.0f, 1.5f),
        VA("17TOP1", 281, 1.0f, 1.5f), VA("17TOP1", 282, 1.0f, 1.5f), VA("17TOP1", 283, 1.0f, 1.5f), VA("17TOP1", 285, 1.0f, 1.5f), VA("17TOP1", 282, 0.92f, 1.5f) },
      VA("INTRFACE", 2733, 1.0f, 4.0f), 0.0f },
    /* Sauron: Lurtz's, far deeper */
    { { VA("13AMON1", 1606, 0.78f, 1.4f), VA("13AMON1", 1607, 0.78f, 1.4f), VA("13AMON1", 1608, 0.78f, 1.4f), VA("13AMON1", 1609, 0.78f, 1.4f),
        VA("13AMON1", 1610, 0.78f, 1.3f), VA("13AMON1", 1611, 0.78f, 1.3f), VA("13AMON1", 1612, 0.78f, 1.3f), VA("13AMON1", 1614, 0.75f, 1.8f), VA("13AMON1", 1613, 0.72f, 1.8f) },
      VA("13AMON1", 1608, 0.72f, 2.2f), 0.3f },
    /* the Uruk-hai: theirs (the Deeping Wall's) */
    { { VA("17TOP1", 826, 1.0f, 1.3f), VA("17TOP1", 827, 1.0f, 1.3f), VA("17TOP1", 826, 1.08f, 1.3f), VA("17TOP1", 827, 1.08f, 1.3f),
        VA("17TOP1", 811, 1.0f, 1.0f), VA("17TOP1", 812, 1.0f, 1.0f), VA("17TOP1", 834, 1.0f, 1.0f), VA("17TOP1", 814, 1.0f, 1.6f), VA("17TOP1", 817, 1.0f, 1.8f) },
      VA("17TOP1", 826, 1.0f, 2.2f), 0.3f },
    /* Saruman: Gandalf's (Gimli's, deeper), far deeper still */
    { { VA("17TOP1", 265, 0.84f, 1.5f), VA("17TOP1", 266, 0.84f, 1.5f), VA("17TOP1", 267, 0.84f, 1.5f), VA("17TOP1", 271, 0.84f, 1.5f),
        VA("17TOP1", 268, 0.84f, 1.5f), VA("17TOP1", 269, 0.84f, 1.5f), VA("17TOP1", 273, 0.84f, 1.5f), VA("17TOP1", 272, 0.84f, 1.5f), VA("17TOP1", 269, 0.78f, 1.5f) },
      VA("21ORTH2", 2621, 1.0f, 2.5f), 0.0f },   /* (1.9.9) on the select: his laugh, Orthanc's own (2621, the one voice only his levels have) */
    { { VA("17TOP1", 826, 1.0f, 1.3f), VA("17TOP1", 827, 1.0f, 1.3f), VA("17TOP1", 826, 1.08f, 1.3f), VA("17TOP1", 827, 1.08f, 1.3f),
        VA("17TOP1", 811, 1.0f, 1.0f), VA("17TOP1", 812, 1.0f, 1.0f), VA("17TOP1", 834, 1.0f, 1.0f), VA("17TOP1", 814, 1.0f, 1.6f), VA("17TOP1", 817, 1.0f, 1.8f) },
      VA("17TOP1", 826, 1.0f, 2.2f), 0.3f }, /* the Uruk captain: the Uruk-hai's */
    /* the cave troll: his own, Balin's Tomb's (his motions' sound events there: 1406 and 1408 as he swings, 1409 struck,
     * 1410, 1412 and 1414 as he dies) */
    { { VA("04BALIN1", 1406, 1.0f, 1.6f), VA("04BALIN1", 1408, 1.0f, 1.6f), VA("04BALIN1", 1406, 0.94f, 1.6f), VA("04BALIN1", 1408, 0.94f, 1.6f),
        VA("04BALIN1", 1409, 1.0f, 1.4f), VA("04BALIN1", 1410, 1.0f, 1.4f), VA("04BALIN1", 1409, 0.92f, 1.4f), VA("04BALIN1", 1412, 1.0f, 2.2f), VA("04BALIN1", 1414, 1.0f, 2.4f) },
      VA("04BALIN1", 1406, 1.0f, 2.2f), 0.3f },
    /* (Versus only) the elven warrior: Legolas's */
    { { VA("17TOP1", 278, 1.0f, 1.5f), VA("17TOP1", 279, 1.0f, 1.5f), VA("17TOP1", 280, 1.0f, 1.5f), VA("17TOP1", 284, 1.0f, 1.5f),
        VA("17TOP1", 281, 1.0f, 1.5f), VA("17TOP1", 282, 1.0f, 1.5f), VA("17TOP1", 283, 1.0f, 1.5f), VA("17TOP1", 285, 1.0f, 1.5f), VA("17TOP1", 282, 0.92f, 1.5f) },
      VA("INTRFACE", 2733, 1.0f, 4.0f), 0.0f },
    /* (Versus only) the Nazgul: Weathertop's own -- the efforts his swings play (1736, 1737), short shrieks struck,
     * the long scream (1746) and the cry his flight plays (1701) */
    { { VA("01WEAT1", 1736, 1.0f, 1.3f), VA("01WEAT1", 1737, 1.0f, 1.3f), VA("01WEAT1", 1736, 1.08f, 1.3f), VA("01WEAT1", 1737, 1.08f, 1.3f),
        VA("01WEAT1", 1715, 1.0f, 1.0f), VA("01WEAT1", 1716, 1.0f, 1.0f), VA("01WEAT1", 1717, 1.0f, 1.0f), VA("01WEAT1", 1746, 1.0f, 2.4f), VA("01WEAT1", 1701, 1.0f, 2.4f) },
      VA("01WEAT1", 1746, 1.0f, 2.4f), 0.3f },
    /* the shield orc: the prologue's orcs (their swings 526, 527; struck 530-532; death 533, 534) */
    { { VA("00ISUL1", 526, 1.0f, 1.3f), VA("00ISUL1", 527, 1.0f, 1.3f), VA("00ISUL1", 526, 1.08f, 1.3f), VA("00ISUL1", 527, 1.08f, 1.3f),
        VA("00ISUL1", 530, 1.0f, 1.0f), VA("00ISUL1", 531, 1.0f, 1.0f), VA("00ISUL1", 532, 1.0f, 1.0f), VA("00ISUL1", 533, 1.0f, 1.6f), VA("00ISUL1", 534, 1.0f, 1.8f) },
      VA("00ISUL1", 526, 1.0f, 2.0f), 0.3f },
    /* Eomer: the Rohirrim's (the Plains of Rohan's: swings 2101-2103, struck 2105, 2106, falling 2109) */
    { { VA("16ROHAN2", 2101, 1.0f, 1.3f), VA("16ROHAN2", 2102, 1.0f, 1.3f), VA("16ROHAN2", 2103, 1.0f, 1.3f), VA("16ROHAN2", 2101, 1.05f, 1.3f),
        VA("16ROHAN2", 2105, 1.0f, 1.0f), VA("16ROHAN2", 2106, 1.0f, 1.0f), VA("16ROHAN2", 2105, 0.95f, 1.0f), VA("16ROHAN2", 2109, 1.0f, 1.6f), VA("16ROHAN2", 2107, 0.9f, 1.6f) },
      VA("16ROHAN2", 2101, 1.0f, 2.0f), 0.0f },
    /* (the other troll) the cave troll's own, Balin's Tomb's (his motions' sound events there: 1406 and 1408 as he swings, 1409 struck,
     * 1410, 1412 and 1414 as he dies) */
    { { VA("04BALIN1", 1406, 1.0f, 1.6f), VA("04BALIN1", 1408, 1.0f, 1.6f), VA("04BALIN1", 1406, 0.94f, 1.6f), VA("04BALIN1", 1408, 0.94f, 1.6f),
        VA("04BALIN1", 1409, 1.0f, 1.4f), VA("04BALIN1", 1410, 1.0f, 1.4f), VA("04BALIN1", 1409, 0.92f, 1.4f), VA("04BALIN1", 1412, 1.0f, 2.2f), VA("04BALIN1", 1414, 1.0f, 2.4f) },
      VA("04BALIN1", 1406, 1.0f, 2.2f), 0.3f },
};
#undef VA
/* (Versus) a hero's voice for our own sounds (versus.c tt_vs_voice): slot 0-3 his efforts, 4-6 blows taken, 7 the
 * death cry, 8 the last breath -- the bank (a level's name), the id, the pitch. The retail heroes' from the banks that
 * hold them: Aragorn's ids 1-12 (any level's bank), Isildur's the prologue's, Legolas' and Gimli's the Deeping Wall's */
static const uint32_t s_vox_leg[9] = { 278, 279, 280, 284, 281, 282, 283, 285, 282 }, s_vox_gim[9] = { 265, 266, 267, 271, 268, 269, 273, 272, 269 };
/* (1.9.7, Versus) a sound id the heroes' motions play that is a voice of Aragorn's, Legolas' or Gimli's: its slot */
int tt_heroes_voice_slot(uint32_t id)
{
    int k;
    for (k = 0; k < VOX_N; k++) if (s_vox_ids[k] == id || s_vox_leg[k] == id || s_vox_gim[k] == id) return k;
    return -1;
}
int tt_heroes_voice_src(const char *skin, int slot, char *bank, size_t bn, uint32_t *id, float *pitch)
{
    const uint32_t *leg = s_vox_leg, *gim = s_vox_gim;
    int g;
    if (!skin || slot < 0 || slot >= VOX_N) return 0;
    *pitch = 1.0f;
    if (!_stricmp(skin, "aragorn")) { snprintf(bank, bn, "04BALIN1"); *id = s_vox_ids[slot]; return 1; }
    if (!_stricmp(skin, "isildur")) { snprintf(bank, bn, "00ISUL1"); *id = s_vox_ids[slot]; return 1; }
    if (!_stricmp(skin, "legolas")) { snprintf(bank, bn, "17TOP1"); *id = leg[slot]; return 1; }
    if (!_stricmp(skin, "gimli")) { snprintf(bank, bn, "17TOP1"); *id = gim[slot]; return 1; }
    g = skin_by_name(skin);
    if (g < 0 || g >= N_EXTRA || !s_voice[g].v[slot].id) return 0;
    snprintf(bank, bn, "%s", s_voice[g].v[slot].bank ? s_voice[g].v[slot].bank : "04BALIN1");
    *id = s_voice[g].v[slot].id; *pitch = s_voice[g].v[slot].pitch;
    return 1;
}

/* Xbox ADPCM (IMA steps), mono */
static const int16_t s_ima_step[89] = { 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707,
    1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289,
    16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767 };
static const int8_t s_ima_idx[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
/* 64 samples a block: its first, then 63 from its nibbles */
static int16_t *adpcm_dec(const unsigned char *d, size_t len, size_t *n)
{
    size_t nb = len / 36, b;
    int16_t *o = (int16_t *)malloc((nb ? nb : 1) * 64 * sizeof(int16_t));
    if (!o) return NULL;
    for (b = 0; b < nb; b++) {
        const unsigned char *p = d + 36 * b;
        int pred = (int16_t)(p[0] | p[1] << 8), idx = p[2] > 88 ? 88 : p[2], k;
        o[64 * b] = (int16_t)pred;
        for (k = 0; k < 63; k++) {
            int nib = (p[4 + k / 2] >> ((k & 1) * 4)) & 15, st = s_ima_step[idx], df = st >> 3;
            if (nib & 1) df += st >> 2;
            if (nib & 2) df += st >> 1;
            if (nib & 4) df += st;
            pred += (nib & 8) ? -df : df;
            if (pred > 32767) pred = 32767; if (pred < -32768) pred = -32768;
            idx += s_ima_idx[nib & 7]; if (idx < 0) idx = 0; if (idx > 88) idx = 88;
            o[64 * b + 1 + k] = (int16_t)pred;
        }
    }
    *n = nb * 64;
    return o;
}
static unsigned char *adpcm_enc(const int16_t *s, size_t n, size_t *len)
{
    size_t nb = (n + 63) / 64, b;
    unsigned char *o = (unsigned char *)calloc(nb ? nb : 1, 36);
    int idx = 0;
    if (!o) return NULL;
    for (b = 0; b < nb; b++) {
        unsigned char *p = o + 36 * b;
        int pred = 64 * b < n ? s[64 * b] : 0, k;
        p[0] = (unsigned char)(pred & 0xFF); p[1] = (unsigned char)((pred >> 8) & 0xFF); p[2] = (unsigned char)idx; p[3] = 0;
        for (k = 0; k < 64; k++) {
            size_t i = 64 * b + 1 + k;
            int v = i < n && k < 63 ? s[i] : (i - 1 < n ? s[i - 1] : 0), st = s_ima_step[idx], diff = v - pred, nib = 0, df = st >> 3;
            if (diff < 0) { nib = 8; diff = -diff; }
            if (diff >= st) { nib |= 4; diff -= st; df += st; }
            if (diff >= st >> 1) { nib |= 2; diff -= st >> 1; df += st >> 1; }
            if (diff >= st >> 2) { nib |= 1; df += st >> 2; }
            pred += (nib & 8) ? -df : df;
            if (pred > 32767) pred = 32767; if (pred < -32768) pred = -32768;
            idx += s_ima_idx[nib & 7]; if (idx < 0) idx = 0; if (idx > 88) idx = 88;
            p[4 + k / 2] |= (unsigned char)(nib << ((k & 1) * 4));
        }
    }
    *len = nb * 36;
    return o;
}

/* a wave of a bank, decoded (mono ADPCM only): its samples and rate */
static int16_t *bank_wave(const char *bank, const char *level, uint32_t id, const char *name, size_t *n, uint32_t *rate)
{
    char xp[160], hp[600];
    int16_t *pcm = NULL;
    FILE *f;
    if (!bank) bank = level;
    if (!bank) return NULL;
    if (!strncmp(bank, "S:", 2)) {   /* streamed: version 2, named */
        unsigned char h[0x30], *meta = NULL, *names = NULL;
        uint32_t bd, n2, esz, nsz, em, nm, data, i;
        snprintf(xp, sizeof xp, "\\Device\\CdRom0\\sound\\S_WB_%s.xwb", bank + 2);
        if (!host_path(xp, hp, sizeof hp) || !(f = fopen(hp, "rb"))) return NULL;
        if (fread(h, 1, sizeof h, f) == sizeof h && rd32(h, 0) == 0x444E4257u && rd32(h, 4) == 2) {
            unsigned char b[0x28];
            bd = rd32(h, 8); em = rd32(h, 0x10); nm = rd32(h, 0x18); data = rd32(h, 0x20);
            if (!fseek(f, (long)bd, SEEK_SET) && fread(b, 1, sizeof b, f) == sizeof b) {
                n2 = rd32(b, 4); esz = rd32(b, 0x18); nsz = rd32(b, 0x1C);
                if (n2 && n2 < 4096 && esz >= 16 && esz <= 64 && nsz && nsz <= 64 && (meta = (unsigned char *)malloc(n2 * esz)) && (names = (unsigned char *)malloc(n2 * nsz))
                    && !fseek(f, (long)em, SEEK_SET) && fread(meta, esz, n2, f) == n2 && !fseek(f, (long)nm, SEEK_SET) && fread(names, nsz, n2, f) == n2) {
                    for (i = 0; i < n2; i++) {
                        char nmb[65];
                        memcpy(nmb, names + nsz * i, nsz); nmb[nsz] = 0;
                        if (!_stricmp(nmb, name)) {
                            uint32_t fmt = rd32(meta, esz * i + 4), off = rd32(meta, esz * i + 8), len = rd32(meta, esz * i + 12);
                            unsigned char *d;
                            if ((fmt & 3) != 1 || ((fmt >> 2) & 7) != 1 || len > 8000000u) break;   /* mono ADPCM */
                            if ((d = (unsigned char *)malloc(len)) && !fseek(f, (long)(data + off), SEEK_SET) && fread(d, 1, len, f) == len) {
                                pcm = adpcm_dec(d, len, n); *rate = fmt >> 5;
                            }
                            free(d);
                            break;
                        }
                    }
                }
            }
        }
        free(meta); free(names); fclose(f);
        return pcm;
    }
    {   /* a level's: version 1, by id (the last bank read is kept) */
        static char lb[32];
        static unsigned char *t, *w;
        static size_t tl, wl;
        uint32_t i, cnt, pos = 0xFFFFFFFFu;
        if (_stricmp(lb, bank)) {
            free(t); free(w); t = w = NULL; tl = wl = 0;
            snprintf(lb, sizeof lb, "%s", bank);
            snprintf(xp, sizeof xp, "\\Device\\CdRom0\\sound\\%s.tbl", bank);
            if (host_path(xp, hp, sizeof hp)) t = read_all(hp, &tl);
            snprintf(xp, sizeof xp, "\\Device\\CdRom0\\sound\\WB_%s.xwb", bank);
            if (host_path(xp, hp, sizeof hp)) w = read_all(hp, &wl);
        }
        if (!t || !w) return NULL;
        if (tl >= 8 && rd32(t, 0) == 0xDEADBEEFu)
            for (i = 0, cnt = rd32(t, 4); i < cnt && 8 + 8 * (size_t)i + 8 <= tl; i++) if (rd32(t, 8 + 8 * i) == id) { pos = i; break; }
        if (pos == 0xFFFFFFFFu) return NULL;
        if (wl > 0x50 && rd32(w, 0) == 0x444E4257u && rd32(w, 4) == 1 && pos < rd32(w, 0xC) && 0x50 + 20 * (size_t)rd32(w, 0xC) <= wl) {
            size_t e = 0x50 + 20 * (size_t)pos, base = 0x50 + 20 * (size_t)rd32(w, 0xC);
            uint32_t fmt = rd32(w, e), off = rd32(w, e + 4), len = rd32(w, e + 8);
            if ((fmt & 3) == 3 && !((fmt >> 2) & 7) && base + off + len <= wl) { pcm = adpcm_dec(w + base + off, len, n); *rate = (fmt >> 5) & 0x3FFFF; }
        }
        return pcm;
    }
}

/* the source's wave made ready: trimmed (a fade at the end), lead
 * silence before; ADPCM with its format */
static unsigned char *voice_make(const VSrc *v, const char *level, float lead, size_t most, uint32_t *fmt, size_t *len)
{
    size_t n = 0, cut, fade, pad, i;
    uint32_t rate = 0, r2;
    int16_t *s = bank_wave(v->bank, level, v->id, v->name, &n, &rate), *t;
    unsigned char *o;
    if (!s && v->bank && level) s = bank_wave(level, level, v->id, v->name, &n, &rate);   /* (the level's own, when it has it) */
    if (!s || !n || !rate) { free(s); return NULL; }
    cut = (size_t)(v->longest * rate);
    if (most && (!cut || most < cut)) cut = most;   /* (to fit the bank's room) */
    if (cut && n > cut) {
        fade = (size_t)(0.15f * rate);
        if (fade > cut) fade = cut;
        for (i = 0; i < fade; i++) s[cut - fade + i] = (int16_t)(s[cut - fade + i] * (float)(fade - i) / (float)fade);
        n = cut;
    }
    pad = (size_t)(lead * rate);
    if (!(t = (int16_t *)calloc(n + pad, sizeof(int16_t)))) { free(s); return NULL; }
    memcpy(t + pad, s, n * sizeof(int16_t));
    free(s);
    o = adpcm_enc(t, n + pad, len);
    free(t);
    r2 = (uint32_t)lroundf(rate * v->pitch);
    if (r2 < 4000) r2 = 4000; if (r2 > 96000) r2 = 96000;
    *fmt = 3u | (r2 << 5);
    return o;
}

/* the guest's copy of a level's bank (or, for INTRFACE, the select's with
 * every guest's line added); its path, or NULL */
typedef struct { uint32_t fmt, off, len; } SelVoice;
static SelVoice s_selv[N_SEL];
/* Choosing a hero on the select (A, X, a click) plays his two sounds: as
 * he is chosen his "highlight" (2728 Aragorn's and Isildur's, 2730
 * Gimli's, 2732 Legolas's: a blade), and as the choice is confirmed his
 * "select" line (2729, 2731, 2733: his voice). Hero 4's are Aragorn's
 * 2728 and 2729: a guest's own instead -- his weapon's swing, then his line
 * (s_selv). Gandalf keeps Aragorn's. Nothing on hovering. */
static SelVoice s_conf[N_SEL];
typedef struct { VSrc swing, cry; float delay; } Confirm;
#define VC(b, i, p, l) { b, i, NULL, p, l }
static const Confirm s_confirm[N_SEL] = {
    { VC("17TOP1", 183, 0.90f, 1.0f), VC(NULL, 0, 0, 0), 0 },     /* Boromir: a sword */
    { VC(NULL, 0, 0, 0), VC(NULL, 0, 0, 0), 0 },                  /* Gandalf: the game's own */
    { VC("17TOP1", 189, 1.0f, 1.0f), VC(NULL, 0, 0, 0), 0 },      /* Frodo: a short blade */
    { VC("13AMON1", 1625, 1.0f, 1.2f), VC(NULL, 0, 0, 0), 0 },    /* Lurtz: his sword */
    { VC("17TOP1", 183, 1.0f, 1.0f), VC(NULL, 0, 0, 0), 0 },      /* Elrond: a sword */
    { VC("17TOP1", 859, 0.80f, 1.2f), VC(NULL, 0, 0, 0), 0 },     /* Sauron: a heavy swing */
    { VC("17TOP1", 859, 1.0f, 1.2f), VC(NULL, 0, 0, 0), 0 },      /* the Uruk-hai: his blade */
};
#undef VC
/* a source at 22050 Hz with its pitch applied (resampled) */
static float *voice_pcm22(const VSrc *v, size_t *n)
{
    size_t m = 0, i, k;
    uint32_t rate = 0;
    int16_t *s = bank_wave(v->bank, NULL, v->id, v->name, &m, &rate);
    float *o, step;
    if (!s || !m || !rate || v->pitch <= 0) { free(s); return NULL; }
    if (v->longest > 0 && m > (size_t)(v->longest * rate)) {
        size_t cut = (size_t)(v->longest * rate), fade = (size_t)(0.12f * rate);
        if (fade > cut) fade = cut;
        for (i = 0; i < fade; i++) s[cut - fade + i] = (int16_t)(s[cut - fade + i] * (float)(fade - i) / (float)fade);
        m = cut;
    }
    step = rate * v->pitch / 22050.0f;
    *n = (size_t)(m / step);
    if (!(o = (float *)malloc((*n + 1) * sizeof(float)))) { free(s); return NULL; }
    for (k = 0; k < *n; k++) {
        float x = k * step; size_t a = (size_t)x; float f = x - a;
        o[k] = a + 1 < m ? s[a] * (1 - f) + s[a + 1] * f : (a < m ? s[a] : 0);
    }
    free(s);
    return o;
}
static unsigned char *confirm_make(const Confirm *c, uint32_t *fmt, size_t *len)
{
    size_t na = 0, nb = 0, d, n, i;
    float *a = voice_pcm22(&c->swing, &na), *b = voice_pcm22(&c->cry, &nb);
    int16_t *o;
    unsigned char *r;
    if (!a && !b) return NULL;
    d = (size_t)(c->delay * 22050);
    n = na > nb + d ? na : nb + d;
    if (!(o = (int16_t *)calloc(n ? n : 1, sizeof(int16_t)))) { free(a); free(b); return NULL; }
    for (i = 0; i < n; i++) {
        float v = (i < na && a ? a[i] * 0.8f : 0) + (i >= d && i - d < nb && b ? b[i - d] : 0);
        if (v > 30000) v = 30000 + (v - 30000) * 0.2f; if (v < -30000) v = -30000 + (v + 30000) * 0.2f;   /* soft limit */
        o[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    free(a); free(b);
    r = adpcm_enc(o, n, len);
    free(o);
    *fmt = 3u | (22050u << 5);
    return r;
}
/* ---- the new heroes' shots' own sounds (1.7) ----
 * Gandalf's and Sauron's bolts, Frodo's stones and the Uruk-hai's burning
 * rocks were heard as arrows (the bow's sounds). Theirs are made here, at
 * 16000 Hz: Gandalf's a bright rising shimmer, Sauron's a dark falling roar,
 * Frodo's throw a light whoosh, the Uruk-hai's a heavy whoosh with the
 * crackle of fire. Nothing from any other game: noise, tones and filters. */
#define SYN_SR 16000.0f   /* (small: the level's sound memory is nearly full) */
typedef struct { float b0, b1, b2, a1, a2, x1, x2, y1, y2; } Biq;
static void biq_set(Biq *q, int band, float f, float qv)   /* band 1 band-pass, 0 low-pass */
{
    float w = 2.0f * 3.14159265f * f / SYN_SR, cs = cosf(w), al = sinf(w) / (2.0f * qv), a0 = 1.0f + al;
    if (band) { q->b0 = al / a0; q->b1 = 0; q->b2 = -al / a0; }
    else { q->b0 = (1 - cs) / 2 / a0; q->b1 = (1 - cs) / a0; q->b2 = (1 - cs) / 2 / a0; }
    q->a1 = -2 * cs / a0; q->a2 = (1 - al) / a0;
}
static float biq_run(Biq *q, float x)
{
    float y = q->b0 * x + q->b1 * q->x1 + q->b2 * q->x2 - q->a1 * q->y1 - q->a2 * q->y2;
    q->x2 = q->x1; q->x1 = x; q->y2 = q->y1; q->y1 = y;
    return y;
}
static uint32_t s_syn_rng = 0x12345678u;
static float syn_noise(void) { s_syn_rng = s_syn_rng * 1664525u + 1013904223u; return (float)(int32_t)s_syn_rng / 2147483648.0f; }
/* kind: 0 Gandalf's bolt, 1 Sauron's, 2 a thrown stone, 3 a burning rock */
static int16_t *synth_shot(int kind, size_t *n)
{
    static const float dur[4] = { 0.80f, 0.95f, 0.32f, 0.50f };
    size_t N = (size_t)(dur[kind] * SYN_SR), i;
    float *f = (float *)calloc(N, sizeof(float)), peak = 0, ph[8] = { 0 };
    int16_t *o;
    Biq bp, lp, lp2, lp3;
    if (!f) return NULL;
    memset(&bp, 0, sizeof bp); memset(&lp, 0, sizeof lp); memset(&lp2, 0, sizeof lp2); memset(&lp3, 0, sizeof lp3);
    s_syn_rng = 0x9E3779B9u + (uint32_t)kind * 7919u;
    for (i = 0; i < N; i++) {
        float t = (float)i / SYN_SR, u = t / dur[kind], v = 0;
        if (kind == 0) {
            /* a rising tone (a soft saw, a few harmonics), a shimmer over it, a breath of wind */
            float fr = 260.0f * powf(3.4f, u), env = (1 - expf(-t / 0.012f)) * expf(-u * 3.2f), sh, nz;
            int h;
            ph[0] += fr / SYN_SR;
            for (h = 1; h <= 4; h++) v += sinf(6.2831853f * ph[0] * h) / (float)h;
            v *= 0.55f * env;
            sh = 0;
            for (h = 0; h < 4; h++) { static const float sf[4] = { 1210, 1790, 2630, 3410 }; ph[1 + h] += sf[h] * (1.0f + 0.35f * u) / SYN_SR; sh += sinf(6.2831853f * ph[1 + h]); }
            sh *= 0.16f * (0.6f + 0.4f * sinf(6.2831853f * 17.0f * t)) * (1 - expf(-t / 0.04f)) * expf(-u * 2.6f);
            if (i == 0) biq_set(&bp, 1, 1800.0f, 0.9f);
            if ((i & 63) == 0) biq_set(&bp, 1, 900.0f + 2600.0f * u, 0.9f);
            nz = biq_run(&bp, syn_noise()) * 0.55f * expf(-u * 3.0f) * (1 - expf(-t / 0.03f));
            v += sh + nz;
        } else if (kind == 1) {
            /* a falling growl, driven hard, on a rumble and a thump */
            float fr = 170.0f * powf(0.42f, u), env = (1 - expf(-t / 0.015f)) * expf(-u * 2.4f), g = 0, nz;
            int h;
            ph[0] += fr / SYN_SR; ph[1] += fr * 1.507f / SYN_SR;
            for (h = 1; h <= 6; h++) g += sinf(6.2831853f * ph[0] * h) / (float)h;
            g += 0.5f * sinf(6.2831853f * ph[1]);
            v = tanhf(g * 2.2f) * 0.45f * env;
            if (i == 0) biq_set(&lp, 0, 520.0f, 0.8f);
            nz = biq_run(&lp, syn_noise()) * 1.4f * expf(-u * 2.0f) * (1 - expf(-t / 0.02f));
            ph[2] += 52.0f / SYN_SR;
            v += nz + 0.5f * sinf(6.2831853f * ph[2]) * expf(-t / 0.18f);
        } else {
            /* a whoosh: band-passed noise swept up then down, swelling and dying */
            float lo = kind == 2 ? 700.0f : 260.0f, hi = kind == 2 ? 2300.0f : 900.0f, c, sw;
            sw = sinf(3.14159265f * u);
            c = lo + (hi - lo) * sw;
            if ((i & 31) == 0) biq_set(&bp, 1, c, kind == 2 ? 1.6f : 1.1f);
            v = biq_run(&bp, syn_noise()) * sw * sw * (kind == 2 ? 1.6f : 2.0f);
            if (kind == 2) { if (i == 0) biq_set(&lp3, 0, 2600.0f, 0.7f); v = biq_run(&lp3, v); }
            if (kind == 3) {   /* fire: short crackles, a low roar */
                if (syn_noise() > 0.9975f) ph[0] = 1.0f;
                if (i == 0) biq_set(&lp3, 0, 1800.0f, 0.7f);
                v += biq_run(&lp3, ph[0] * syn_noise()) * 0.9f; ph[0] *= 0.93f;
                if (i == 0) biq_set(&lp2, 0, 300.0f, 0.7f);
                v += biq_run(&lp2, syn_noise()) * 1.2f * sw;
            }
        }
        f[i] = v;
        if (fabsf(v) > peak) peak = fabsf(v);
    }
    if (!(o = (int16_t *)malloc(N * sizeof(int16_t)))) { free(f); return NULL; }
    for (i = 0; i < N; i++) {
        float g = peak > 0 ? f[i] / peak * 26000.0f : 0;
        if (i + 400 > N) g *= (float)(N - i) / 400.0f;   /* (no click at the end) */
        o[i] = (int16_t)g;
    }
    free(f);
    *n = N;
    return o;
}

static uint32_t s_shot_tbl_cnt;   /* (testing: the bank's count when his shots' entries were added) */
/* the level's sound table (<level>.tbl: 0xDEADBEEF, count, {id, index}...)
 * with his shots' two ids after the rest, at the bank's two new entries.
 * (The game reads the bank first, so its count is known.) */
static const char *shot_tbl(const char *xp, const char *base)
{
    static char out[600];
    char hp[600];
    unsigned char *t, *o;
    size_t tl;
    uint32_t n;
    int g = s_guest;
    FILE *f;
    uint32_t bankn = 0;
    if (G8(HERO_BYTE) != 4 || shot_kind(g) < 0 || getenv("TT_NOSHOTSND") || !_stricmp(base, "INTRFACE.tbl")) return NULL;
    {   /* the level's bank beside it (WB_<level>.xwb): the same count, or nothing is changed.
         * (Its two entries more are there if they fitted; if not, the game
         * finds the ids past the bank's count and plays nothing for them.) */
        char bx[600], bh[600];
        const char *sl = strrchr(xp, '\\');
        FILE *bf;
        unsigned char hd[16];
        if (sl && strlen(base) > 4) {
            snprintf(bx, sizeof bx, "%.*sWB_%.*s.xwb", (int)(sl + 1 - xp), xp, (int)(strlen(base) - 4), base);
            if (host_path(bx, bh, sizeof bh) && (bf = fopen(bh, "rb"))) {
                if (fread(hd, 1, 16, bf) == 16 && rd32(hd, 0) == 0x444E4257u) bankn = rd32(hd, 0xC);
                fclose(bf);
            }
        }
    }
    if (!host_path(xp, hp, sizeof hp) || !(t = read_all(hp, &tl))) return NULL;
    if (tl < 8 || rd32(t, 0) != 0xDEADBEEFu || (n = rd32(t, 4)) != bankn || 8 + 8 * (size_t)n > tl) {
        tt_dbg("[HEROES] his shots' sounds: %s does not match the bank\n", base);
        free(t); return NULL;
    }
    if (!(o = (unsigned char *)malloc(tl + 16))) { free(t); return NULL; }
    memcpy(o, t, 8 + 8 * (size_t)n);
    wr32(o, 4, n + 2);
    wr32(o, 8 + 8 * n, SHOT_SND_ID); wr32(o, 12 + 8 * n, n);
    wr32(o, 16 + 8 * n, SHOT_SND_ID2); wr32(o, 20 + 8 * n, n + 1);
    if (tl > 8 + 8 * (size_t)n) memcpy(o + 24 + 8 * n, t + 8 + 8 * n, tl - 8 - 8 * n);
    free(t);
    paths();
    snprintf(out, sizeof out, "%s/heroes/voices/%s_%s", s_dir, s_skins[g].name, base);
    mkdirs(out);
    if (!(f = fopen(out, "wb")) || fwrite(o, 1, tl + 16, f) != tl + 16) { if (f) fclose(f); free(o); return NULL; }
    fclose(f);
    free(o);
    tt_dbg("[HEROES] his shots' sounds: %s\n", out);
    return out;
}
static int s_selv_ok;   /* the select's bank is ours */
#define SND_MEM 0x300000u   /* the game's sound memory */
static const char *voice_bank(const char *xp, const char *base)
{
    static char out[600];
    char hp[600], level[32], sub[64];
    unsigned char *w, *nb;
    size_t wl, nl, cap;
    uint32_t cnt, dbase;
    int intr = !_stricmp(base, "WB_INTRFACE.xwb"), g = s_guest, k, shot;
    FILE *f;
    if (ends_with(base, ".tbl")) return shot_tbl(xp, base);
    if (strlen(base) < 8 || strlen(base) > 28 || _strnicmp(base, "WB_", 3) || !ends_with(base, ".xwb")) return NULL;
    snprintf(level, sizeof level, "%.*s", (int)(strlen(base) - 7), base + 3);
    shot = intr ? -1 : shot_kind(g);
    if (getenv("TT_NOSHOTSND")) shot = -1;
    if (!intr && (G8(HERO_BYTE) != 4 || g < 0 || g >= N_EXTRA || (!s_voice[g].v[0].id && shot < 0))) return NULL;   /* (no voice of his own) */
    paths();
    /* rebuilt at each load (a few voices: quick), one file each */
    snprintf(sub, sizeof sub, "%s.xwb", intr ? "select" : s_skins[g].name);
    snprintf(out, sizeof out, "%s/heroes/voices/%s", s_dir, sub);
    {   /* (the game may ask more than once for one load) */
        static char last[64]; static DWORD at;
        char key[64];
        snprintf(key, sizeof key, "%s|%d", base, intr ? -1 : g);
        if (!strcmp(key, last) && GetTickCount() - at < 15000u) return out;
        snprintf(last, sizeof last, "%s", key); at = GetTickCount();
    }
    if (intr) s_selv_ok = 0;
    if (!host_path(xp, hp, sizeof hp) || !(w = read_all(hp, &wl))) return NULL;
    if (wl <= 0x50 || rd32(w, 0) != 0x444E4257u || rd32(w, 4) != 1 || (cnt = rd32(w, 0xC)) == 0 || cnt > 500 || (dbase = 0x50 + 20 * cnt) > wl) { free(w); return NULL; }
    cap = wl + 2000000;
    if (!(nb = (unsigned char *)malloc(cap))) { free(w); return NULL; }
    memcpy(nb, w, wl); nl = wl;
    if (intr) {
        for (k = 0; k < N_SEL; k++) {
            uint32_t fmt; size_t len;
            unsigned char *d = s_voice[k].select.bank ? voice_make(&s_voice[k].select, NULL, s_voice[k].lead, 0, &fmt, &len) : NULL;
            memset(&s_selv[k], 0, sizeof s_selv[k]);
            if (d && nl + len <= cap && nl + len - dbase < SND_MEM - 0x10000) {
                s_selv[k].fmt = fmt; s_selv[k].off = (uint32_t)(nl - dbase); s_selv[k].len = (uint32_t)len;
                memcpy(nb + nl, d, len); nl += len;
            }
            free(d);
            memset(&s_conf[k], 0, sizeof s_conf[k]);
            if (s_confirm[k].swing.id || s_confirm[k].cry.id) {
                uint32_t f2; size_t l2;
                unsigned char *c2 = confirm_make(&s_confirm[k], &f2, &l2);
                if (c2 && nl + l2 <= cap && nl + l2 - dbase < SND_MEM - 0x10000) {
                    s_conf[k].fmt = f2; s_conf[k].off = (uint32_t)(nl - dbase); s_conf[k].len = (uint32_t)l2;
                    memcpy(nb + nl, c2, l2); nl += l2;
                }
                free(c2);
            }
        }
    } else {
        /* The bank stays no bigger than the level's own: the game's sound
         * memory holds it with the level's other sounds, and Fangorn's
         * (2.48 MB) with Sauron's longer voices added after it ran over
         * (a crash as the level loaded). The data is laid out again: the
         * waves kept, then his; the room of the voices replaced is what his
         * may take, shortened alike (with a fade) when they need more. */
        unsigned char *t, *dv[VOX_N], *shw[2] = { NULL, NULL };
        size_t tl, dl[VOX_N], room, keep = 0, most = 0, total, shl[2] = { 0, 0 }, shot_room = 0;
        uint32_t pos[VOX_N], fm[VOX_N], i, q, *newoff;
        char tx[160], th[600], *rep;
        int tries;
        snprintf(tx, sizeof tx, "\\Device\\CdRom0\\sound\\%s.tbl", level);
        if (!host_path(tx, th, sizeof th) || !(t = read_all(th, &tl))) { free(w); free(nb); return NULL; }
        if (!(rep = (char *)calloc(cnt, 1)) || !(newoff = (uint32_t *)calloc(cnt, sizeof(uint32_t)))) { free(rep); free(t); free(w); free(nb); return NULL; }
        for (k = 0; k < VOX_N; k++) {
            pos[k] = 0xFFFFFFFFu; dv[k] = NULL; dl[k] = 0;
            if (tl >= 8 && rd32(t, 0) == 0xDEADBEEFu)
                for (i = 0; i < rd32(t, 4) && 16 + 8 * (size_t)i <= tl; i++) if (rd32(t, 8 + 8 * i) == s_vox_ids[k]) { pos[k] = i; break; }
            if (pos[k] < cnt) rep[pos[k]] = 1;
        }
        free(t);
        /* what stays: the other entries' data (once each, some share) */
        for (i = 0; i < cnt; i++) {
            uint32_t o = rd32(w, 0x50 + 20 * i + 4), l = rd32(w, 0x50 + 20 * i + 8), dup = 0;
            if (rep[i]) continue;
            for (q = 0; q < i; q++) if (!rep[q] && rd32(w, 0x50 + 20 * q + 4) == o && rd32(w, 0x50 + 20 * q + 8) == l) dup = 1;
            if (!dup) keep += l;
        }
        /* his shots' two waves (1.7): two entries more, their data after the rest */
        if (shot >= 0) {
            size_t n1 = 0, n2;
            int16_t *pcm = synth_shot(shot, &n1), sil[640];
            memset(sil, 0, sizeof sil);
            if (pcm) shw[0] = adpcm_enc(pcm, n1, &shl[0]);
            free(pcm);
            n2 = sizeof sil / sizeof sil[0];
            shw[1] = adpcm_enc(sil, n2, &shl[1]);
            if (!shw[0] || !shw[1]) { free(shw[0]); free(shw[1]); shw[0] = shw[1] = NULL; }
            else shot_room = 40 + shl[0] + shl[1];
        }
        /* (his shots' sounds come on top: the bank may grow by those, under
         * 16 KB -- the voices are not cut for them) */
        room = wl - dbase > keep ? wl - dbase - keep : 0;
        for (tries = 0; tries < 12; tries++) {
            total = 0;
            for (k = 0; k < VOX_N; k++) {
                free(dv[k]); dv[k] = NULL; dl[k] = 0;
                if (pos[k] >= cnt) continue;
                if ((dv[k] = voice_make(&s_voice[g].v[k], level, 0.0f, most, &fm[k], &dl[k]))) total += dl[k];
                else if (!tries) tt_dbg("[HEROES] voice %u of %s: no wave\n", s_vox_ids[k], s_skins[g].name);
            }
            if (total <= room) break;
            {   /* the longest of them cut to fit: a common length (samples) */
                size_t longest = 0;
                for (k = 0; k < VOX_N; k++) if (dl[k] / 36 * 64 > longest) longest = dl[k] / 36 * 64;
                most = (size_t)(longest * 0.85);
                if (most < 2048) most = 2048;
            }
        }
        if (total > room) for (k = 0; k < VOX_N; k++) { free(dv[k]); dv[k] = NULL; }   /* (the level's own, then) */
        /* the new data: what stays, in its order, then his */
        nl = dbase;
        for (i = 0; i < cnt; i++) {
            uint32_t o = rd32(w, 0x50 + 20 * i + 4), l = rd32(w, 0x50 + 20 * i + 8);
            int found = 0;
            if (rep[i]) {
                int mine = 0;
                for (k = 0; k < VOX_N; k++) if (pos[k] == i && dv[k]) mine = 1;
                if (mine) continue;
            }
            for (q = 0; q < i; q++) {
                int qm = 0;
                for (k = 0; k < VOX_N; k++) if (pos[k] == q && dv[k]) qm = 1;
                if (!qm && rd32(w, 0x50 + 20 * q + 4) == o && rd32(w, 0x50 + 20 * q + 8) == l) { newoff[i] = newoff[q]; found = 1; break; }
            }
            if (!found) {
                if (dbase + (size_t)o + l > wl || nl + l > cap) { free(rep); free(newoff); for (k = 0; k < VOX_N; k++) free(dv[k]); free(w); free(nb); return NULL; }
                newoff[i] = (uint32_t)(nl - dbase);
                memcpy(nb + nl, w + dbase + o, l); nl += l;
            }
            wr32(nb, 0x50 + 20 * i + 4, newoff[i]);
        }
        for (k = 0; k < VOX_N; k++) {
            size_t e;
            if (!dv[k] || nl + dl[k] > cap) continue;
            e = 0x50 + 20 * (size_t)pos[k];
            memcpy(nb + nl, dv[k], dl[k]);
            wr32(nb, e, fm[k]); wr32(nb, e + 4, (uint32_t)(nl - dbase)); wr32(nb, e + 8, (uint32_t)dl[k]); wr32(nb, e + 12, 0); wr32(nb, e + 16, 0);
            nl += dl[k];
        }
        if (nl > wl + (shot_room <= 16384 ? shot_room : 0)) {   /* (never bigger than the level's own, but for his shots' sounds) */
            for (k = 0; k < VOX_N; k++) free(dv[k]);
            free(rep); free(newoff); free(w); free(nb);
            tt_dbg("[HEROES] voices: %s's do not fit %s's bank\n", s_skins[g].name, level);
            return NULL;
        }
        if (most) tt_dbg("[HEROES] voices: %s's shortened to %u samples to fit %s's bank\n", s_skins[g].name, (unsigned)most, level);
        if (shw[0] && cnt + 2 <= 500 && nl + shot_room <= wl + (shot_room <= 16384 ? shot_room : 0) && nl + shot_room <= cap) {
            /* two entries more: the data moves 40 bytes on, the entries'
             * offsets (from the data's start) stay */
            size_t dl2 = nl - dbase, e;
            memmove(nb + dbase + 40, nb + dbase, dl2);
            for (e = 0; e < 2; e++) {
                size_t at = 0x50 + 20 * (cnt + e), off = dl2 + (e ? shl[0] : 0);
                wr32(nb, at, 3u | (16000u << 5)); wr32(nb, at + 4, (uint32_t)off); wr32(nb, at + 8, (uint32_t)shl[e]);
                wr32(nb, at + 12, 0); wr32(nb, at + 16, 0);
            }
            nl += 40;
            memcpy(nb + nl, shw[0], shl[0]); nl += shl[0];
            memcpy(nb + nl, shw[1], shl[1]); nl += shl[1];
            wr32(nb, 0xC, cnt + 2);
            s_shot_tbl_cnt = cnt;
            tt_dbg("[HEROES] his shots' sounds: entries %u and %u of %s's bank\n", cnt, cnt + 1, level);
        } else if (shot >= 0) { s_shot_tbl_cnt = 0; tt_dbg("[HEROES] his shots' sounds: no room in %s's bank\n", level); }
        free(shw[0]); free(shw[1]);
        for (k = 0; k < VOX_N; k++) free(dv[k]);
        free(rep); free(newoff);
    }
    free(w);
    mkdirs(out);
    if (!(f = fopen(out, "wb")) || fwrite(nb, 1, nl, f) != nl) { if (f) fclose(f); remove(out); free(nb); return NULL; }
    fclose(f);
    free(nb);
    if (intr) s_selv_ok = 1;
    tt_dbg("[HEROES] voices: %s (%u bytes)\n", out, (unsigned)nl);
    return out;
}

/* On the character select a guest has his own line where Aragorn and
 * Isildur share theirs (2728): as he comes to the front (we play it, as
 * the game does for its four: 0x00073830), as the select opens on him, and
 * when he is chosen (the game plays it for hero 4's character). The select
 * bank's entry for it points at his while the cursor is on him, and back
 * at the bank's own otherwise (each frame of the select). */
#define SND_MGR 0x6B4550u
static DWORD s_sel_tick;          /* when the select last ran (its entries go back once it is gone) */
static int s_sel_set;
static void select_entry(int g);
static void select_gone(void) { if (s_sel_set && GetTickCount() - s_sel_tick > 400u) { select_entry(-1); s_sel_set = 0; } }
static int sel_slot(uint32_t id, int g, const SelVoice *tab)
{
    static uint32_t orig[3][3];
    static int have[3];
    uint32_t n, t, i, e, want[3];
    int k, ours = 0, w = id == 2728 ? 0 : 1;
    if (g < 0 || g >= N_SEL || !tab[g].len) g = -1;
    n = G32(SND_MGR + 0x287C); t = G32(SND_MGR + 0x2880);
    if (!t || n > 500) return 0;
    for (i = 0; i < n; i++) if (G32(t + 8 * i) == id) break;
    if (i >= n || i >= G32(SND_MGR + 0x34)) return 0;
    e = SND_MGR + 0x78 + 20 * i;
    for (k = 0; k < N_SEL; k++) if (tab[k].len && G32(e + 4) == tab[k].off && G32(e + 8) == tab[k].len) ours = 1;
    if (!ours) { orig[w][0] = G32(e); orig[w][1] = G32(e + 4); orig[w][2] = G32(e + 8); have[w] = 1; }   /* the bank's own, as loaded */
    if (!have[w]) return 0;
    if (g >= 0) { want[0] = tab[g].fmt; want[1] = tab[g].off; want[2] = tab[g].len; }
    else memcpy(want, orig[w], sizeof want);
    if (G32(e) != want[0] || G32(e + 4) != want[1] || G32(e + 8) != want[2]) { G32(e) = want[0]; G32(e + 4) = want[1]; G32(e + 8) = want[2]; }
    return g >= 0;
}
static void select_entry(int g)
{
    if (!s_selv_ok || getenv("TT_NOVOICE")) return;
    /* the select's bank in the manager ("INTRFACE") */
    if (G32(SND_MGR + 0x38) != 0x52544E49u || G32(SND_MGR + 0x3C) != 0x45434146u) return;
    sel_slot(2728, g, s_conf);    /* as he is chosen: his weapon */
    sel_slot(2729, g, s_selv);    /* as it is confirmed: his line */
}
static void select_voice(uint32_t menu, int g)
{
    uint32_t a1[1], a6[6] = { 2728, 0x3F800000u, 0, 0, 0x3F800000u, 0 };
    if (getenv("TT_SNDLOG")) tt_dbg("[SND] select voice %d (ok %d)\n", g, s_selv_ok);   /* testing */
    if (!s_selv_ok || g < 0 || g >= N_SEL || getenv("TT_NOVOICE")) return;
    select_entry(g);
    a1[0] = G32(menu + 0x214);
    tt_gcall(0x000C36E0u, 0, 1, a1);                       /* the last one stopped */
    G32(menu + 0x214) = tt_gcall(0x000C3640u, 0, 6, a6);   /* and his */
}

static void tt_ct_arm(const char *xp);
static void name_set(const char *title);
static void upn_set(int g);
static void rec_switch(int g);
int tt_heroes_unlocked_count(void) { int g, n = 0, w; if (!tt_heroes_on()) return 0; w = isildur_wins(); for (g = 0; g < N_SEL; g++) n += w >= s_unlock_at[g]; return n; }   /* (earned: not the test switch) */
static const char *redirect(const char *xp)
{
    { extern void tt_ach_file(const char *xp); tt_ach_file(xp); }   /* (1.9.7: achievements see each file the game opens) */
    /* Reject save writes at the existing file-open hook, before any
     * truncation. Reads still reach the original saves in test mode. */
    extern volatile int xbox_file_open_write;
    if (s_test_unlock && xbox_file_open_write && strstr(xp, "45410010"))
        return "::TT_TEST_MODE_SAVE_DISABLED::";
    tt_ct_arm(xp);
    char x[600], base[128];
    const char *b;
    int sk, g;
    b = strrchr(xp, '\\'); b = b ? b + 1 : xp;
    snprintf(base, sizeof base, "%s", b);
    if (!tt_heroes_on() && playas() < 0 && !strstr(xp, "orlds\\") && !strstr(xp, "ORLDS\\")) return NULL;   /* the mod off (installed for co-op): level files only */
    {   /* The prologue is Isildur's: no guest there -- not his model, his
         * moves, his weapons, his portrait -- whoever hero 4 is on the
         * character select. The guest comes back with the next level or
         * the menu. */
        static int saved = -2;
        int world = strstr(xp, "orlds\\") || strstr(xp, "ORLDS\\");
        if (world && _strnicmp(base, "00ISUL1.", 8) && _strnicmp(base, "01WEAT1.", 8)) s_vsp1 = -1;   /* (Versus' Mount Doom skin: that level only) */
        if (world && (!_strnicmp(base, "00ISUL1.", 8)) && !tt_vs_on()) {
            if (saved == -2) {
                saved = s_guest; s_guest = -1;
                if (saved >= 0) { rec_switch(-1); name_set("Isildur"); upn_set(-1); tt_dbg("[HEROES] the prologue: Isildur himself\n"); }
            }
        } else if (saved != -2 && (world || !_stricmp(base, "mainmenu.BFF") || !_stricmp(base, "startup.BFF"))) {
            if (s_guest < 0 && saved >= 0) { s_guest = saved; rec_switch(saved); name_set(s_skins[saved].title); upn_set(saved); }
            saved = -2;
        }
    }
    /* the save being loaded or written: ...\UDATA\45410010\<save>\game.xsv */
    if (!_stricmp(base, "game.xsv")) {
        const char *u = strstr(xp, "45410010\\");
        if (u) {
            char key[16];
            size_t i;
            u += 9;
            for (i = 0; i < sizeof key - 1 && u[i] && u[i] != '\\'; i++) key[i] = u[i];
            key[i] = 0;
            s_xsv_opens++;
            /* (1.7) the save written after Isildur's finish: the count goes to
             * the file the game writes -- opened for writing -- and to no
             * other (the save screen's list opens every save's to read it) */
            {
                extern volatile int xbox_file_open_write;
                if (xbox_file_open_write) wins_saved(key);
                else snprintf(s_read_key, sizeof s_read_key, "%s", key);   /* (listed or loaded: the count changes only when one is loaded) */
            }
        }
        return NULL;
    }
    /* the character select: the extra heroes' models, keys 0x21.. as
     * mesh 1050, animations 1100, texture 1150 (the menu's own types) */
    if (ends_with(xp, "\\mainmenu.bff") && tt_heroes_on()) {
        /* each on his own skeleton with his own stance, pose and move
         * (TT_REMAP=1: re-rigged, with Aragorn's) */
        const int nat = !getenv("TT_REMAP");
        Add a[5 * N_SEL];
        const char *r;
        int na = 0;
        memset(a, 0, sizeof a);
        for (g = 0; g < N_SEL; g++) {
            a[na].src = s_skins[g].src; a[na].src_key = s_skins[g].mesh; a[na].type = 1050; a[na].key = FE_KEY(g);
            if (!nat) { a[na].map = s_skins[g].map; a[na].nmap = s_skins[g].nmap; }
            else { a[na].map = s_idmap; a[na].nmap = 256; }   /* his own bones: only the leftover indices made safe */
            na++;
            a[na].src = s_skins[g].src; a[na].src_key = s_skins[g].tex;  a[na].type = 1150; a[na].key = FE_KEY(g); na++;
            if (nat) { a[na].src = s_skins[g].src; a[na].src_key = s_skins[g].anim; a[na].type = 1100; a[na].key = FE_KEY(g); a[na].compose = getenv("TT_MENU_PLAIN") ? 0 : g + 1; na++; }
        }
        for (g = 0; g < N_SEL; g++) {   /* and his weapon, for his hand there too (after the models) */
            const char *ws; uint32_t wm, wt;
            if (getenv("TT_MENUWMASK") && !((atoi(getenv("TT_MENUWMASK")) >> g) & 1)) continue;   /* testing */
            if (skin_weapon(g, 1, &ws, &wm, &wt) && wm != EMPTY_PROP_KEY && !getenv("TT_NOMENUWEAPON")) {
                a[na].src = ws; a[na].src_key = wm; a[na].type = 1200; a[na].key = FE_WKEY(g); na++;
                a[na].src = ws; a[na].src_key = wt; a[na].type = 1250; a[na].key = FE_WKEY(g); a[na].tint = s_skins[g].glow && !getenv("TT_NOTINT"); na++;
            }
        }
        s_compose_menu = 1;
        {   /* named by this rebuild's version (the guests' models and moves in it) */
            char sub[32];
            snprintf(sub, sizeof sub, "%s%u%s%s", nat ? (getenv("TT_MENU_PLAIN") ? "menup" : "menu") : "menur", HEROES_BUILD, getenv("TT_MENUWMASK") ? "m" : "", getenv("TT_MENUWMASK") ? getenv("TT_MENUWMASK") : "");
            r = cached(sub, xp, "mainmenu.BFF", a, na);
        }
        s_compose_menu = 0;
        s_fe_native = nat;
        return r;
    }
    /* the guest's voices: the level's bank, and the character select's */
    if (tt_heroes_on() && (strstr(xp, "ound\\") || strstr(xp, "OUND\\")) && !getenv("TT_NOVOICE")) return voice_bank(xp, base);
    if (tt_heroes_on() && (!_stricmp(base, "upgbigi.BFF") || !_stricmp(base, "upgradei.BFF") || !_stricmp(base, "endlvl.BFF"))) return upg_redirect(xp, base);
    if (!strstr(xp, "orlds\\") && !strstr(xp, "ORLDS\\")) return NULL;
    { extern void coop_level_reset(void); coop_level_reset(); }   /* (co-op: a level loads; the last one's state goes) */
    if (!getenv("TT_PLAYAS") && (ends_with(xp, ".bfa") || ends_with(xp, ".bfl") || ends_with(xp, ".bfg") || ends_with(xp, ".bfi"))) {   /* (player 1 a guest: the level is his rebuild; player 2 a copy) */   /* co-op: player 2's character for this level (twotowers_manual.c decides: forced, chosen, or a copy of player 1's hero) */
        extern const char *tt_coop_p2_char(const char *level);
        const char *c = tt_coop_p2_char(base);
        int k = c ? skin_by_name(c) : -1;
        if (k >= 0 && skin_retail(k) && s_retail_hero[k - N_EXTRA] == (int)G8(HERO_BYTE) && !(G8(HERO_BYTE) == 4 && LVG >= 0)) { tt_dbg("[HEROES] co-op: %s is player 1's own hero: a copy\n", c); k = -1; }
        if (k == SK_TROLL && G8(HERO_BYTE) == 4 && LVG == SK_TROLL && !getenv("TT_NOTROLL2")) { k = SK_TROLL2; tt_dbg("[HEROES] co-op: both players the cave troll: player 2 the other troll (Fangorn's)\n"); }
        if (k >= 0 && G8(HERO_BYTE) == 4 && LVG >= 0 && k == LVG) { tt_dbg("[HEROES] co-op: %s is player 1's own (hero 4): a copy\n", c); k = -1; }
        s_coop_skin = k >= 0; s_playas = k; tt_coop_skin_n = 0;
        if (c) tt_dbg("[HEROES] co-op: player 2 in %s is %s%s\n", base, c, k >= 0 ? "" : " (no such skin: a copy of player 1's hero)");
    }
    sk = -1;
    if (ends_with(xp, ".bfi") && G8(HERO_BYTE) == 4 && LVG >= 0) sk = LVG;   /* player 1 a guest: his rebuild (player 2's skin added to it, below) */
    else if (s_coop_skin && s_playas >= 0 && (ends_with(xp, ".bfa") || ends_with(xp, ".bfl") || ends_with(xp, ".bfg") || ends_with(xp, ".bfi"))) sk = s_playas;   /* co-op: player 2's skin into player 1's variant */
    else if (ends_with(xp, ".bfi") && G8(HERO_BYTE) == 4) sk = LVG;
    else if (ends_with(xp, ".bfa") && G8(HERO_BYTE) == 1) sk = playas();
    if (sk < 0) return NULL;
    {
        /* re-rigged guests keep the hero's animations; others bring theirs */
        /* the guest plays on his own skeleton with his own motions (see
         * compose_anims); TT_REMAP=1: on the hero's, with the hero's */
        const int nat = (sk == LVG || (s_coop_skin && !getenv("TT_COOP_REMAP"))) && (sk < N_EXTRA || skin_retail(sk)) && !getenv("TT_REMAP");   /* (co-op: player 2's guest too: re-rigged onto Aragorn's skeleton he came out a mess) */
        const int gv = !nat && sk == LVG && s_skins[sk].lvar != NULL;   /* another hero's variant */
        s_native = nat;
        /* under keys of their own (PLAYER_KEY): the level's own Boromir, Frodo,
         * Gandalf or Lurtz -- an ally or a foe there -- keeps his model, his
         * texture and his moves */
        const int p2only = s_coop_skin && sk == s_playas, p2too = s_coop_skin && s_playas >= 0 && sk == LVG && s_playas != LVG;
        Add a[24] = { { s_skins[sk].src, s_skins[sk].mesh, 100, PLAYER_KEY(s_skins[sk].mesh), gv ? s_skins[sk].lmap : s_skins[sk].map, gv ? s_skins[sk].nlmap : s_skins[sk].nmap },
                     { s_skins[sk].src, s_skins[sk].tex,  300, PLAYER_KEY(s_skins[sk].tex), NULL, 0 },
                     { s_skins[sk].src, s_skins[sk].anim, 150, PLAYER_KEY(s_skins[sk].anim), NULL, 0 } };
        char sub[64];
        int na = (s_skins[sk].map && !nat && !getenv("TT_ADDANIM")) ? 2 : 3;
        if (nat) { a[0].map = s_idmap; a[0].nmap = 256; if (sk < N_EXTRA) a[2].compose = sk + 1; }   /* (a retail hero's set is whole: nothing composed) */
        {   /* his weapons: model (a prop) and texture */
            int sl;
            for (sl = 0; sl < 3; sl++) {
                const char *ws; uint32_t wm, wt;
                if (sl == 2) { if (!s_skins[sk].xmesh || getenv("TT_NOWEAPON")) continue; ws = s_skins[sk].xsrc; wm = s_skins[sk].xmesh; wt = s_skins[sk].xtex; }
                else if (!skin_weapon(sk, sl, &ws, &wm, &wt)) continue;
                a[na].src = ws; a[na].src_key = a[na].key = wm; a[na].type = 200; a[na].map = NULL; a[na].nmap = 0; a[na].empty = 0;
                if (wm == EMPTY_PROP_KEY) { a[na].src_key = s_skins[sk].wmesh; a[na].empty = 1; }
                na++;
                {   /* one weapon for both hands (Gandalf's staff): once */
                    int q, dup = 0;
                    for (q = 3; q < na - 1; q++) if (a[q].type == 200 && a[q].key == wm) dup = 1;
                    if (dup) { na--; continue; }
                }
                {   /* a texture once */
                    int q, dup = 0;
                    for (q = 3; q < na; q++) if (a[q].type == 300 && a[q].key == wt) dup = 1;
                    if (dup) continue;
                }
                a[na].src = ws; a[na].src_key = a[na].key = wt; a[na].type = 300; a[na].map = NULL; a[na].nmap = 0; a[na].empty = 0;
                a[na].tint = sl == 1 && s_skins[sk].glow && !getenv("TT_NOTINT"); na++;
                if (weapon_tex2(wm)) { a[na].src = ws; a[na].src_key = a[na].key = weapon_tex2(wm); a[na].type = 300; a[na].map = NULL; a[na].nmap = 0; a[na].empty = 0; na++; }
            }
        }
        if (p2too) {   /* player 2's character too (player 1 a guest): his model, texture, moves and weapons under his own keys */
            int q2 = s_playas, sl;
            a[na].src = s_skins[q2].src; a[na].src_key = s_skins[q2].mesh; a[na].type = 100; a[na].key = PLAYER_KEY(s_skins[q2].mesh); a[na].map = q2 == SK_TROLL2 ? s_map_fang2cave : s_idmap; a[na].nmap = q2 == SK_TROLL2 ? 56 : 256; a[na].compose = 0; a[na].empty = 0; a[na].tint = 0; na++;
            a[na].src = s_skins[q2].src; a[na].src_key = s_skins[q2].tex; a[na].type = 300; a[na].key = PLAYER_KEY(s_skins[q2].tex); a[na].map = NULL; a[na].nmap = 0; a[na].compose = 0; a[na].empty = 0; a[na].tint = 0; na++;
            if (s_skins[q2].anim != s_skins[sk].anim) {   /* (the other troll: player 1's set, already in) */
            a[na].src = s_skins[q2].src; a[na].src_key = s_skins[q2].anim; a[na].type = 150; a[na].key = PLAYER_KEY(s_skins[q2].anim); a[na].map = NULL; a[na].nmap = 0; a[na].compose = q2 < N_EXTRA ? q2 + 1 : 0; a[na].empty = 0; a[na].tint = 0; na++; }
            for (sl = 0; sl < 2 && na < 22; sl++) {
                const char *ws; uint32_t wm, wt; int q, dupm = 0, dupt = 0;
                if (!skin_weapon(q2, sl, &ws, &wm, &wt)) continue;
                for (q = 3; q < na; q++) { if (a[q].type == 200 && a[q].key == wm) dupm = 1; if (a[q].type == 300 && a[q].key == wt) dupt = 1; }
                if (!dupm) { a[na].src = ws; a[na].src_key = a[na].key = wm; a[na].type = 200; a[na].map = NULL; a[na].nmap = 0; a[na].compose = 0; a[na].empty = 0; a[na].tint = 0;
                    if (wm == EMPTY_PROP_KEY) { a[na].src_key = s_skins[q2].wmesh; a[na].empty = 1; } na++; }
                if (!dupt) { a[na].src = ws; a[na].src_key = a[na].key = wt; a[na].type = 300; a[na].map = NULL; a[na].nmap = 0; a[na].compose = 0; a[na].empty = 0; a[na].tint = 0; na++; }
            }
            tt_dbg("[HEROES] co-op: player 2's %s added to %s's level (%d resources)\n", s_skins[q2].name, s_skins[sk].name, na);
        }
        if ((p2only || p2too) && na < 22) {   /* (1.9.9) player 2's ranged weapon's model and texture (skin_p2_ranged), when not in already */
            const char *ws; uint32_t wm, wt; int q, dupm = 0, dupt = 0;
            if (skin_p2_ranged(s_playas, &ws, &wm, &wt)) {
                for (q = 3; q < na; q++) { if (a[q].type == 200 && a[q].key == wm) dupm = 1; if (a[q].type == 300 && a[q].key == wt) dupt = 1; }
                if (!dupm) { a[na].src = ws; a[na].src_key = a[na].key = wm; a[na].type = 200; a[na].map = NULL; a[na].nmap = 0; a[na].compose = 0; a[na].empty = 0; a[na].tint = 0; na++; }
                if (!dupt) { a[na].src = ws; a[na].src_key = a[na].key = wt; a[na].type = 300; a[na].map = NULL; a[na].nmap = 0; a[na].compose = 0; a[na].empty = 0; a[na].tint = 0; na++; }
                if (!dupm || !dupt) tt_dbg("[HEROES] co-op: player 2's ranged weapon %08X (texture %08X) added\n", wm, wt);
            }
        }
        if ((sk == SK_TROLL || (s_coop_skin && (s_playas == SK_TROLL || s_playas == SK_TROLL2))) && na < 24 && !getenv("TT_TROLLROCK")) {   /* (1.9.9) his log's bark (troll_log_patch) */
            a[na].src = W("14FANG2.bfa"); a[na].src_key = a[na].key = TROLL_LOG_TEX; a[na].type = 300; a[na].map = NULL; a[na].nmap = 0; a[na].compose = 0; a[na].empty = 0; a[na].tint = 0; na++;
        }
        snprintf(x, sizeof x, "%.*s%s", (int)(strlen(xp) - 4), xp, gv ? s_skins[sk].lvar : p2only ? xp + strlen(xp) - 4 : ".bfa");   /* (co-op: player 1's own variant) */
        s_remap_noshadow = gv;   /* (torn volumes on Gimli's skeleton too) */   /* Aragorn's variant (or Gimli's) */
        base[strlen(base) - 4] = 0;
        strcat(base, p2only ? xp + strlen(xp) - 4 : ends_with(xp, ".bfi") ? ".bfi" : ".bfa");
        /* the cache folder names what went into the file, so a changed
         * skin (or this rebuild's version) never picks up a stale one */
        if (skin_retail(sk))
            snprintf(sub, sizeof sub, "%s%s%s_coop_b%u_%08X", s_skins[sk].name, sk == LVG ? "_p1" : "", p2too ? s_skins[s_playas].name : "", HEROES_BUILD, (s_skins[sk].mesh ^ s_skins[sk].wmesh * 5u ^ (uint32_t)na * 11u ^ (uint32_t)nat * 19u) + HEROES_BUILD);
        else if (s_skins[sk].map)
            /* the version on its own as well (b<n>): with it only in the sum, an
             * older build's folder could come out under the same name, and its
             * stale levels were used (Frodo missing from Caradhras' opening
             * scene, and a crash there and in Amon Hen) */
            snprintf(sub, sizeof sub, "%s%s%s%s_b%u_%08X", s_skins[sk].name, nat ? "_own" : "", p2only ? "_c" : "", p2too ? s_skins[s_playas].name : "", HEROES_BUILD,
                     (s_skins[sk].mesh ^ s_skins[sk].tex * 3u ^ s_skins[sk].wmesh * 5u ^ s_skins[sk].wtex * 7u ^ s_skins[sk].w2mesh * 17u ^ s_skins[sk].xmesh * 23u ^ (uint32_t)na * 11u ^ (uint32_t)gv * 13u ^ (uint32_t)nat * 19u) + HEROES_BUILD + anims_hash(sk));
        else snprintf(sub, sizeof sub, "%s", s_skins[sk].name);
        {
            const char *r2;
            s_build_skin = sk;
            r2 = cached(sub, x, base, a, na);
            s_build_skin = -1;
            if (!s_shv_n && ((sk >= 0 && !strcmp(s_skins[sk].name, "orc")) || (p2too && s_playas >= 0 && !strcmp(s_skins[s_playas].name, "orc")))) shield_sig_fill();   /* (a level built before: the shield's vertices made again, to find them) */
            hero_size_prepare(r2,sk);
            return r2;
        }
    }
}

/* ===================================================================== */
/* the hero's name in the game's text                                    */
/* ===================================================================== */
/* startup.BFF's names, UTF-16: "Aragorn", "Legolas", "Gimli", then 12 bytes
 * on "Isildur" -- the fourth hero's, shown on the character select, the
 * mission map and the upgrade screen. Found once, rewritten as hero 4
 * changes hands. */
static uint32_t s_name_at[8];
static int s_nname = -1;
static char s_name_cur[8] = "Isildur";   /* what the tables say now */
static int u16_is(uint32_t va, const char *t, int term)
{
    int i;
    for (i = 0; t[i]; i++) if (G16(va + 2 * i) != (uint16_t)t[i]) return 0;
    return !term || G16(va + 2 * i) == 0;
}
static void name_long_prep(int k, uint32_t q);
static void name_find(void)
{
    uint32_t va;
    s_nname = 0;
    /* after "Gimli": "Isildur" (the hero lists) or "Play as Isildur" (the
     * level map's line for Isildur's level) -- only the name is changed */
    const volatile uint16_t *m = (const volatile uint16_t *)xbox_GetMemoryOffset();
    for (va = 0x10000u; va < 0x84000000u - 64 && s_nname < 8; va += 2) {
        if (va == 0x03FF0000u) va = 0x80000000u;
        if (m[va / 2] != 'G' || m[va / 2 + 1] != 'i' || !u16_is(va, "Gimli", 1)) continue;
        if (u16_is(va + 12, "Isildur", 1) || u16_is(va + 12, s_name_cur, 1)) s_name_at[s_nname++] = va + 12;
        else if (u16_is(va + 12, "Play as ", 0) && (u16_is(va + 28, "Isildur", 1) || u16_is(va + 28, s_name_cur, 1))) s_name_at[s_nname++] = va + 28;
    }
    tt_dbg("[HEROES] %d name tables\n", s_nname);
    { int k; for (k = 0; k < s_nname; k++) name_long_prep(k, s_name_at[k]); }
}
/* ===================================================================== */
/* the upgrades' names, per guest                                         */
/* ===================================================================== */
/* startup.BFF holds the four heroes' upgrade names (UTF-16, end to end);
 * hero 4 shows Aragorn's set ("Isildur's Swift Terror", "Strength of the
 * Stewards", ...). While a guest holds hero 4 its names (and the
 * descriptions that name them) read as his: written over the originals in
 * memory, each no longer than the original, and put back for Isildur. */
typedef struct { const char *from, *to; } UpgName;
/* (1.8) Each hero's own: whole strings first (his six named moves, which were
 * "Isildur's Swift Terror" and the like -- now names of their own; his ranged
 * weapon's descriptions, which spoke of bows and arrows for stones, rocks
 * and fire), then the words in the rest. Each no longer than the original. */
static const UpgName s_upn_boromir[] = {
    { "Isildur's Swift Terror", "Horn of Gondor" }, { "Isildur's Deliverance", "Captain's Stand" }, { "Isildur's Death Charge", "Minas Tirith Charge" }, { "Isildur's Gambit", "Steward's Gambit" }, { "Isildur's War Rush", "White Tower Rush" }, { "Isildur's Judgment", "Amon Hen Stand" }, { "Isildur's", "Boromir's" }, { "Ranger Fury", "Gondor Fury" }, { "Dunedain Bow", "Steward Bow" }, { "Strength of Isildur", "Might of Denethor" }, { "Strength of Elendil", "Strength of Gondor" }, { "Wilderness Rage", "Wrath of Gondor" }, { NULL, NULL } };
static const UpgName s_upn_gandalf[] = {
    { "Isildur's Swift Terror", "Flame of Anor" }, { "Isildur's Deliverance", "You Shall Not Pass" }, { "Isildur's Death Charge", "Shadowfax Charge" }, { "Isildur's Gambit", "Grey Pilgrim" }, { "Isildur's War Rush", "Mithrandir's Rush" }, { "Isildur's Judgment", "Wrath of the Maia" }, { "Fired like the Dunedain Bow, the barbed arrows of the Rohirrim do serious damage.", "Cast like Wizard Light, the flame of Narya burns foes for serious damage." }, { "The iron arrows of Gondor do terrible damage.  (Requires Rohan Bow)", "The white fire of the Istari burns terribly. (Requires Ring Fire)" }, { "Fire with the A button", "Cast with the A button" }, { "Isildur's", "Gandalf's" }, { "Ranger Fury", "Maia Fury" }, { "Sword Parry", "Staff Parry" }, { "Dunedain Bow", "Wizard Light" }, { "Rohan Bow", "Ring Fire" }, { "Gondor Bow", "White Fire" }, { "Strength of the Stewards", "Strength of the Istari" }, { "Strength of Isildur", "Strength of Olorin" }, { "Strength of Elendil", "Strength of Narya" }, { "Strength of the Argonath", "Strength of the White" }, { "Wilderness Rage", "Wizard's Rage" }, { "Master Swordsman", "Glamdring Master" }, { "Wrath of Numenor", "Wrath of Olorin" }, { NULL, NULL } };
static const UpgName s_upn_frodo[] = {
    { "Isildur's Swift Terror", "Sting's Bite" }, { "Isildur's Deliverance", "Ringbearer's Will" }, { "Isildur's Death Charge", "Baggins Charge" }, { "Isildur's Gambit", "Hobbit's Luck" }, { "Isildur's War Rush", "Tookish Rush" }, { "Isildur's Judgment", "Mithril Resolve" }, { "Fired like the Dunedain Bow, the barbed arrows of the Rohirrim do serious damage.", "Thrown like Shire Stones, sharp Brandywine flints do serious damage." }, { "The iron arrows of Gondor do terrible damage.  (Requires Rohan Bow)", "Elf-blessed stones do terrible damage.  (Requires Flintshot)" }, { "Fire with the A button", "Hurl with the A button" }, { "Isildur's", "Frodo's" }, { "Ranger Fury", "Hobbit Fury" }, { "Dunedain Bow", "Shire Stones" }, { "Rohan Bow", "Flintshot" }, { "Gondor Bow", "Elf Stones" }, { "Strength of the Stewards", "Strength of the Shire" }, { "Strength of Isildur", "Strength of Bilbo" }, { "Strength of Elendil", "Strength of Samwise" }, { "Strength of the Argonath", "Strength of Bag End" }, { "Wilderness Rage", "Shire Rage" }, { "Master Swordsman", "Sting Mastery" }, { "Wrath of Numenor", "Wrath of Sting" }, { NULL, NULL } };
static const UpgName s_upn_lurtz[] = {
    { "Isildur's Swift Terror", "Lurtz's Hunt" }, { "Isildur's Deliverance", "Saruman's Will" }, { "Isildur's Death Charge", "Fighting Uruk Charge" }, { "Isildur's Gambit", "Butcher's Gambit" }, { "Isildur's War Rush", "Isengard Onslaught" }, { "Isildur's Judgment", "First Uruk's Doom" }, { "Fired like the Dunedain Bow, the barbed arrows of the Rohirrim do serious damage.", "Fired like the Isengard Bow, the barbed arrows of Orthanc do serious damage." }, { "The iron arrows of Gondor do terrible damage.  (Requires Rohan Bow)", "Black arrows of Isengard do terrible damage.  (Requires Uruk Bow)" }, { "Isildur's", "Lurtz's" }, { "Ranger Fury", "Uruk Fury" }, { "Sword Parry", "Blade Parry" }, { "Dunedain Bow", "Isengard Bow" }, { "Rohan Bow", "Uruk Bow" }, { "Gondor Bow", "Black Bow" }, { "Strength of the Stewards", "Strength of Isengard" }, { "Strength of Isildur", "Strength of Saruman" }, { "Strength of Elendil", "Strength of Orthanc" }, { "Strength of the Argonath", "Strength of the Uruk-hai" }, { "Wilderness Rage", "Isengard Rage" }, { "Master Swordsman", "Master Butcher" }, { "Wrath of Numenor", "Wrath of Saruman" }, { "Bane of Saruman", "Bane of Rohan" }, { NULL, NULL } };
static const UpgName s_upn_elrond[] = {
    { "Isildur's Swift Terror", "Vilya's Grace" }, { "Isildur's Deliverance", "Last Alliance" }, { "Isildur's Death Charge", "Charge of Lindon" }, { "Isildur's Gambit", "Herald's Gambit" }, { "Isildur's War Rush", "Noldor War Rush" }, { "Isildur's Judgment", "Lord of Imladris" }, { "Fired like the Dunedain Bow, the barbed arrows of the Rohirrim do serious damage.", "Fired like the Elven Bow, the arrows of Rivendell do serious damage." }, { "The iron arrows of Gondor do terrible damage.  (Requires Rohan Bow)", "Silver arrows of the Eldar do terrible damage. (Requires Vanya Bow)" }, { "Isildur's", "Elrond's" }, { "Ranger Fury", "Elven Fury" }, { "Dunedain Bow", "Elven Bow" }, { "Rohan Bow", "Vanya Bow" }, { "Gondor Bow", "Eldar Bow" }, { "Strength of the Stewards", "Strength of Rivendell" }, { "Strength of Isildur", "Might of Gil-galad" }, { "Strength of Elendil", "Strength of Vilya" }, { "Strength of the Argonath", "Strength of the Eldar" }, { "Wilderness Rage", "Noldor Rage" }, { "Wrath of Numenor", "Wrath of Lindon" }, { NULL, NULL } };
static const UpgName s_upn_sauron[] = {
    { "Isildur's Swift Terror", "Sauron's Dance" }, { "Isildur's Deliverance", "The One Ring's Will" }, { "Isildur's Death Charge", "Dark Lord's Charge" }, { "Isildur's Gambit", "Annatar's Gambit" }, { "Isildur's War Rush", "Mordor War Rush" }, { "Isildur's Judgment", "Eye of Sauron" }, { "Fired like the Dunedain Bow, the barbed arrows of the Rohirrim do serious damage.", "Cast like the Dark Bolt, the fire of Barad-dur does serious damage." }, { "The iron arrows of Gondor do terrible damage.  (Requires Rohan Bow)", "The flames of Mount Doom do terrible damage.  (Requires Hellfire)" }, { "Fire with the A button", "Cast with the A button" }, { "Isildur's", "Sauron's" }, { "Ranger Fury", "Dark Fury" }, { "Sword Parry", "Mace Parry" }, { "Dunedain Bow", "Dark Bolt" }, { "Rohan Bow", "Hellfire" }, { "Gondor Bow", "Doom Fire" }, { "Strength of the Stewards", "Strength of Barad-dur" }, { "Strength of Isildur", "Strength of Mordor" }, { "Strength of Elendil", "Might of the Ring" }, { "Strength of the Argonath", "Strength of Mount Doom" }, { "Wilderness Rage", "Mordor Rage" }, { "Master Swordsman", "Master of Maces" }, { "Wrath of Numenor", "Wrath of Mordor" }, { "Bane of Saruman", "Bane of Elves" }, { NULL, NULL } };
static const UpgName s_upn_uruk[] = {
    { "Isildur's Swift Terror", "Berserker Frenzy" }, { "Isildur's Deliverance", "Torch of the Deep" }, { "Isildur's Death Charge", "Deeping Wall Charge" }, { "Isildur's Gambit", "Berserker Gambit" }, { "Isildur's War Rush", "Siege Rush" }, { "Isildur's Judgment", "Wrath of the Pits" }, { "Fired like the Dunedain Bow, the barbed arrows of the Rohirrim do serious damage.", "Thrown like Crag Rocks, these rocks from the pits do serious damage." }, { "The iron arrows of Gondor do terrible damage.  (Requires Rohan Bow)", "Molten rocks do terrible damage.  (Requires War Rocks)" }, { "Fire with the A button", "Hurl with the A button" }, { "Isildur's", "Berserker's" }, { "Ranger Fury", "Berserker Fury" }, { "Sword Parry", "Blade Parry" }, { "Dunedain Bow", "Crag Rocks" }, { "Rohan Bow", "War Rocks" }, { "Gondor Bow", "Lava Rocks" }, { "Strength of the Stewards", "Strength of Isengard" }, { "Strength of Isildur", "Might of the Pits" }, { "Strength of Elendil", "Strength of Saruman" }, { "Strength of the Argonath", "Strength of the Berserker" }, { "Wilderness Rage", "Berserker Rage" }, { "Master Swordsman", "Master Berserker" }, { "Wrath of Numenor", "Wrath of Orthanc" }, { NULL, NULL } };
#include "heroes_ampliacion_textos.inc"
/* (1.9.9) the Nazgul (Versus): his own move names, his thrown darts in place of the bow (each no longer than the original) */
static const UpgName s_upn_nazgul[] = {
    { "Isildur's Swift Terror", "Morgul Blade Fury" }, { "Isildur's Deliverance", "Witch-king's Will" }, { "Isildur's Death Charge", "Black Rider Charge" }, { "Isildur's Gambit", "Wraith's Gambit" }, { "Isildur's War Rush", "Minas Morgul Rush" }, { "Isildur's Judgment", "Cry of the Nazgul" },
    { "Fired like the Dunedain Bow, the barbed arrows of the Rohirrim do serious damage.", "Thrown like Morgul Darts, the blades of Minas Morgul do serious damage." }, { "The iron arrows of Gondor do terrible damage.  (Requires Rohan Bow)", "Poisoned darts of Mordor do terrible damage.  (Requires Doom Dart)" }, { "Fire with the A button", "Hurl with the A button" },
    { "Isildur's", "Nazgul's" }, { "Ranger Fury", "Wraith Fury" }, { "Sword Parry", "Blade Parry" }, { "Dunedain Bow", "Morgul Darts" }, { "Rohan Bow", "Doom Dart" }, { "Gondor Bow", "Grave Dart" }, { "Strength of the Stewards", "Strength of the Nine" }, { "Strength of Isildur", "Strength of Angmar" }, { "Strength of Elendil", "Might of the Nine" }, { "Strength of the Argonath", "Strength of Minas Morgul" }, { "Wilderness Rage", "Ringwraith Rage" }, { "Master Swordsman", "Morgul Swordsman" }, { "Wrath of Numenor", "Wrath of Angmar" }, { "Bane of Saruman", "Bane of Men" }, { NULL, NULL } };
static const UpgName *upn_list(int g)
{
    static const struct { const char *n; const UpgName *l; } L[] = {
        { "boromir", s_upn_boromir }, { "gandalf", s_upn_gandalf }, { "frodo", s_upn_frodo }, { "lurtz", s_upn_lurtz },
        { "elrond", s_upn_elrond }, { "sauron", s_upn_sauron }, { "uruk", s_upn_uruk }, { "saruman", s_upn_saruman }, { "urukcap", s_upn_urukcap_nuevo }, { "troll", s_upn_troll_nuevo }, { "troll2", s_upn_troll_nuevo }, { "ent", s_upn_ent_nuevo }, { "orc", s_upn_orc_nuevo }, { "elvenwarrior", s_upn_elvenwarrior_nuevo }, { "nazgul", s_upn_nazgul } };
    int k;
    if (g < 0 || g >= N_SKINS) return NULL;
    for (k = 0; k < (int)(sizeof L / sizeof L[0]); k++) if (!strcmp(L[k].n, s_skins[g].name)) return L[k].l;
    return NULL;
}
#define UPN_MAX 512
static uint32_t s_upn_at[UPN_MAX];     /* each string's place in memory */
static uint16_t *s_upn_orig[UPN_MAX];  /* and its original text (UTF-16, with its end) */
static uint16_t *s_upn_now[UPN_MAX];   /* and what was last written there */
static int s_upn_len[UPN_MAX], s_upn_n = -1, s_upn_cur = -2;
/* the strings that hold a name of Aragorn's set, near each copy of the set */
static void upn_find(void)
{
    static const char *keys[] = { "Isildur's", "Ranger Fury", "Dunedain Bow", "Rohan Bow", "Gondor Bow", "Strength of the Stewards",
                                  "Strength of Isildur", "Strength of Elendil", "Strength of the Argonath", "Wilderness Rage",
                                  "Master Swordsman", "Wrath of Numenor", "Sword Parry", "Bane of Saruman",
                                  "Fire with the A button" };   /* (the last: the first such in each copy only -- Aragorn's; Legolas's follows) */
    const volatile uint16_t *m = (const volatile uint16_t *)xbox_GetMemoryOffset();
    uint32_t va;
    int i0;
    for (i0 = 0; i0 < UPN_MAX; i0++) { free(s_upn_orig[i0]); free(s_upn_now[i0]); s_upn_orig[i0] = s_upn_now[i0] = NULL; }
    s_upn_n = 0;
    for (va = 0x10000u; va < 0x84000000u - 64 && s_upn_n < UPN_MAX; va += 2) {
        uint32_t s0, e, k;
        if (va == 0x03FF0000u) va = 0x80000000u;
        if (m[va / 2] != 'T' || !u16_is(va, "Triple Strike", 1) || !u16_is(va + 28, "Double Hack", 1) || !u16_is(va + 52, "Dunedain Bow", 1)) continue;
        /* this copy's strings: back to 0x3000 bytes of descriptions, on to the set's end ("Nothing") */
        int fire_seen = 0;
        for (s0 = va - 0x3000u; s0 < va + 0x400u && s_upn_n < UPN_MAX; s0 += 2) {
            uint32_t l = 0;
            if (m[s0 / 2] < 0x20 || m[s0 / 2] > 0x7E || (m[s0 / 2 - 1] != 0)) continue;
            while (l < 200 && m[s0 / 2 + l] >= 0x20 && m[s0 / 2 + l] <= 0x7E) l++;
            if (m[s0 / 2 + l] != 0 || l < 3) continue;
            for (k = 0; k < sizeof keys / sizeof keys[0]; k++) {
                size_t kl = strlen(keys[k]); uint32_t j;
                for (j = 0; j + kl <= l; j++) { size_t q; for (q = 0; q < kl && m[s0 / 2 + j + q] == (uint16_t)keys[k][q]; q++); if (q == kl) break; }
                if (j + kl <= l) break;
            }
            if (l == 7 && m[s0 / 2] == 'N' && m[s0 / 2 + 1] == 'o' && m[s0 / 2 + 6] == 'g') fire_seen = 1;   /* "Nothing": Aragorn's descriptions are over */
            if (k == sizeof keys / sizeof keys[0] - 1) { if (fire_seen) k = sizeof keys / sizeof keys[0]; fire_seen = 1; }
            if (k < sizeof keys / sizeof keys[0]) {
                s_upn_at[s_upn_n] = s0; s_upn_len[s_upn_n] = (int)l;
                s_upn_orig[s_upn_n] = (uint16_t *)malloc(2 * (l + 1));
                s_upn_now[s_upn_n] = (uint16_t *)malloc(2 * (l + 1));
                for (e = 0; e <= l; e++) s_upn_now[s_upn_n][e] = s_upn_orig[s_upn_n][e] = m[s0 / 2 + e];
                s_upn_n++;
            }
            s0 += 2 * l;
        }
        va += 0x400u;
    }
    tt_dbg("[HEROES] %d upgrade name strings\n", s_upn_n);
}
/* ---- (1.9.2) the same in a language (lang.c): the game's tables hold the
 * language's words, so the strings are found by those, and each guest's are
 * the language's own (heroes_lang.inc: the English strings' keys, his, and
 * the keys of his versions -- written whole, with the room lang.c leaves
 * after every string) ---- */
#include "heroes_lang.inc"
extern int tt_lang_on(void);
extern int tt_lang_key16(uint32_t hash, const uint16_t **out);
#define HL_N ((int)(sizeof s_hl / sizeof s_hl[0]))
#define HL_ROOM 47
static struct { uint32_t at, from; int len; uint16_t last[256]; int lastn; } s_hla[UPN_MAX];
static int s_hla_n = -1;
static int s_hla_saved;
static DWORD s_hla_scan;
static int hl_holds(uint32_t at, const uint16_t *u, int n)
{
    int k;
    for (k = 0; k < n; k++) if (G16(at + 2 * k) != u[k]) return 0;
    return G16(at + 2 * k) == 0;
}
/* Un barrido debe conservar los textos que seguimos poseyendo. Buscar
 * solo el original perdia el nombre ya cambiado al recargar otro menu. */
static int hl_retain(void)
{
    int n=0;
    for(int i=0;i<s_hla_saved;i++){
        const uint16_t *u=NULL;int len=tt_lang_key16(s_hla[i].from,&u);
        uint32_t at=s_hla[i].at;if(!at||len<=0)continue;
        if(!hl_holds(at,u,len)&&!(s_hla[i].lastn&&hl_holds(at,s_hla[i].last,s_hla[i].lastn)))continue;
        if(n!=i)s_hla[n]=s_hla[i];n++;
    }return n;
}
static void hl_find(void)
{
    const volatile uint16_t *m = (const volatile uint16_t *)xbox_GetMemoryOffset();
    uint32_t froms[HL_N], va;
    const uint16_t *fu[HL_N];
    int fn[HL_N], nf = 0, i, j;
    for (i = 0; i < HL_N; i++) {
        for (j = 0; j < nf && froms[j] != s_hl[i].from; j++);
        if (j < nf) continue;
        if ((fn[nf] = tt_lang_key16(s_hl[i].from, &fu[nf])) <= 0) continue;
        froms[nf++] = s_hl[i].from;
    }
    s_hla_n = hl_retain();
    s_hla_scan = GetTickCount();
    for (va = 0x10002u; va < 0x84000000u - 600 && s_hla_n < UPN_MAX; va += 2) {
        uint16_t c;
        if (va == 0x03FF0000u) va = 0x80000002u;
        c = m[va / 2];
        if (c == 0 || m[va / 2 - 1] != 0) continue;
        for (j = 0; j < nf; j++)
            if (fu[j][0] == c && hl_holds(va, fu[j], fn[j])) {
                int known=0;for(int k=0;k<s_hla_n;k++)if(s_hla[k].at==va){known=1;break;}
                if(known)break;
                s_hla[s_hla_n].at = va; s_hla[s_hla_n].from = froms[j]; s_hla[s_hla_n].len = fn[j]; s_hla[s_hla_n].lastn = 0;
                s_hla_n++;
                va += 2 * (uint32_t)fn[j];
                break;
            }
    }
    s_hla_saved=s_hla_n;
    tt_dbg("[HEROES] %d strings that name hero 4's things (in the language)\n", s_hla_n);
}
static int hl_set(int g)
{
    const char *who = g >= 0 && g < N_SKINS ? s_skins[g].name : NULL;
    int i, j, ok = 0;
    extern const char *tt_lang_code(void);
    int nuevo=g>=7 && g<N_EXTRA;
    /* (1.9.9) Saruman: Gandalf's spell texts in the languages (theirs are a wizard's, the others' a bow's) */
    if(who&&!strcmp(who,"saruman"))who="gandalf";
    if (s_hla_n < 0) hl_find();
    for (i = 0; i < s_hla_n; i++) {
        const uint16_t *ou, *tu = NULL;
        int on = tt_lang_key16(s_hla[i].from, &ou), tn = -1, k;
        uint32_t at = s_hla[i].at;
        if (!at || on <= 0) continue;
        if (!hl_holds(at, ou, on) && !(s_hla[i].lastn && hl_holds(at, s_hla[i].last, s_hla[i].lastn))) { s_hla[i].at = 0; continue; }
        ok++;
        if (who) for (j = 0; j < HL_N; j++)
            if (s_hl[j].from == s_hla[i].from && !strcmp(s_hl[j].hero, who)) { tn = tt_lang_key16(s_hl[j].to, &tu); break; }
        uint16_t own[256];
        if(nuevo)for(unsigned z=0;z<sizeof s_new_es/sizeof s_new_es[0];z++){
            if(strcmp(s_new_es[z].guest,s_skins[g].name) ||s_new_es[z].hash!=s_hla[i].from)continue;
            const char *text=NULL;
            if(!strcmp(tt_lang_code(),"es"))text=s_new_es[z].text; else if(s_hla[i].from==0xDEEF44C1u)text=s_new_es[z].en;   /* (1.9.9: his title in English outside Spanish) */
            if(text){tn=(int)strlen(text);for(k=0;k<tn;k++)own[k]=(uint16_t)(unsigned char)text[k];tu=own;}break;
        }
        if (tn <= 0 || tn > on + HL_ROOM || tn > 255) { tu = ou; tn = on; }
        for (k = 0; k < tn; k++) G16(at + 2 * k) = tu[k];
        { int end = s_hla[i].lastn > on ? s_hla[i].lastn : on; if (end < tn) end = tn; for (; k <= end; k++) G16(at + 2 * k) = 0; }
        memcpy(s_hla[i].last, tu, 2 * (size_t)tn); s_hla[i].lastn = tn;
    }
    if (ok < s_hla_n && GetTickCount() - s_hla_scan > 2000u) s_hla_n = -1;   /* (some gone: look again next time) */
    return ok;
}

/* hero 4's upgrade names: guest g's (-1 Isildur's, the originals) */
/* (1.9.9) the special move's name the HUD's popup word shows (list 1, element 14: the hero-4 strings, upn_set's) as player
 * 2's own: his guest's names, or a retail hero's name in Isildur's place. A buffer of ours in guest memory, or 0 (not one
 * of those strings, or player 2's names are player 1's) */
uint32_t tt_heroes_p2_popup_text(uint32_t tp)
{
    extern const char *tt_coop_p2_char_now; static uint32_t buf; char t[512], o[512]; const UpgName *L = NULL; int i, k, j, l;
    const char *c = tt_coop_p2_char_now; int p2 = c ? skin_by_name(c) : -1;
    static const UpgName ret_ar[] = { { "Isildur's", "Aragorn's" }, { NULL, NULL } }, ret_le[] = { { "Isildur's", "Legolas'" }, { NULL, NULL } }, ret_gi[] = { { "Isildur's", "Gimli's" }, { NULL, NULL } };
    if (getenv("TT_NOP2UPN") || !c || s_upn_n <= 0) return 0;
    for (i = 0; i < s_upn_n && s_upn_at[i] != tp; i++) ;
    if (i == s_upn_n) return 0;
    if (s_coop_skin && s_playas == SK_TROLL2) p2 = SK_TROLL;
    if (!strcmp(c, "aragorn")) L = ret_ar; else if (!strcmp(c, "legolas")) L = ret_le; else if (!strcmp(c, "gimli")) L = ret_gi;
    else if (!strcmp(c, "isildur")) L = NULL; else if (p2 >= 0) L = upn_list(p2); else return 0;
    if (p2 >= 0 && p2 == LVG && !skin_retail(p2)) return 0;   /* (his names are player 1's) */
    l = s_upn_len[i];
    for (k = 0; k < l && k < 500; k++) t[k] = (char)s_upn_orig[i][k];
    t[k] = 0;
    if (L) for (j = 0; L[j].from; j++) { char *p = t; while ((p = strstr(p, L[j].from)) != NULL) { size_t fl = strlen(L[j].from), nl = strlen(L[j].to);
        if (strlen(t) - fl + nl >= 250) break; snprintf(o, sizeof o, "%.*s%s%s", (int)(p - t), t, L[j].to, p + fl); p = t + (p - t) + nl; strcpy(t, o); } }
    if (!buf) { extern uint32_t xbox_ContiguousAlloc(uint32_t size, uint32_t alignment); buf = xbox_ContiguousAlloc(1024u, 16u); }
    if (!buf) return 0;
    for (k = 0; t[k] && k < 250; k++) G16(buf + 2 * k) = (uint16_t)(unsigned char)t[k];
    G16(buf + 2 * k) = 0;
    return buf;
}
static void upn_set(int g)
{
    const UpgName *L = upn_list(g);
    int i;
    if (getenv("TT_NOUPGNAMES")) return;
    if (tt_lang_on()) { if (hl_set(g) > 0) s_upn_cur = g; if (s_upn_n < 0) s_upn_n = 0; return; }
    if (s_upn_n <= 0) upn_find();   /* (none yet: startup.BFF not loaded -- again later) */
    if (s_upn_cur == g || s_upn_n == 0) return;
    for (i = 0; i < s_upn_n; i++) {
        char t[512], o[512]; int l = s_upn_len[i], k, j; uint32_t q = s_upn_at[i];
        /* still ours? (holding what we left there, or the original) */
        { int same = 1, orig = 1; for (k = 0; k <= l; k++) { if (G16(q + 2 * k) != s_upn_now[i][k]) same = 0; if (G16(q + 2 * k) != s_upn_orig[i][k]) orig = 0; } if (!same && !orig) continue; }
        for (k = 0; k < l; k++) t[k] = (char)s_upn_orig[i][k];
        t[l] = 0;
        if (L) for (j = 0; L[j].from; j++) {
            char *p = t;
            while ((p = strstr(p, L[j].from)) != NULL) {
                size_t fl = strlen(L[j].from), nl = strlen(L[j].to);
                if (strlen(t) - fl + nl > (size_t)l || strlen(t) - fl + nl >= sizeof o) break;   /* (no room: as it was) */
                snprintf(o, sizeof o, "%.*s%s%s", (int)(p - t), t, L[j].to, p + fl);
                p = t + (p - t) + nl;
                strcpy(t, o);
            }
        }
        for (k = 0; k <= l; k++) G16(q + 2 * k) = s_upn_now[i][k] = k < (int)strlen(t) ? (uint16_t)(unsigned char)t[k] : 0;
    }
    s_upn_cur = g;
    if (getenv("TT_UPNLOG")) for (i = 0; i < s_upn_n && i < 90; i++) {   /* testing */
        char t[256]; int k;
        for (k = 0; k < 255 && G16(s_upn_at[i] + 2 * k); k++) t[k] = (char)G16(s_upn_at[i] + 2 * k);
        t[k] = 0; tt_dbg("[UPN] %08X %s\n", s_upn_at[i], t);
    }
}

/* (1.9.9) Names longer than "Isildur" ("Berserker"): the slot holds 7
 * letters, so the string table's entry (OBSS: "OBSS", 1, ?, count, then
 * {id, byte offset from the header}) is pointed at a longer slot -- the room
 * of a string the table holds twice ("Unable to save game. ..."), whose
 * second entry is pointed at the first copy. The 7 letters stay in place. */
static struct { uint32_t base, ent, orig, slot, slotlen, dent, dto; } s_nl[8];
static uint32_t u16_len(uint32_t va) { uint32_t n = 0; while (n < 400 && G16(va + 2 * n)) n++; return n; }
static int u16_same(uint32_t a, uint32_t b) { uint32_t i; for (i = 0; i < 400; i++) { if (G16(a + 2 * i) != G16(b + 2 * i)) return 0; if (!G16(a + 2 * i)) return 1; } return 0; }
static void name_long_prep(int k, uint32_t q)
{
    uint32_t B, n, j, i, e = 0, eo = 0, pre, used;
    memset(&s_nl[k], 0, sizeof s_nl[k]);
    if (getenv("TT_NAMESHORT")) return;
    for (B = q & ~3u; B > q - 0x20000u && B >= 0x10000u; B -= 4) if (G32(B) == 0x5353424Fu) break;
    if (G32(B) != 0x5353424Fu || (n = G32(B + 12)) == 0 || n > 4000) return;
    for (j = 0; j < n; j++) { uint32_t o = G32(B + 20 + 8 * j); if (o <= q - B && o >= eo) { eo = o; e = B + 20 + 8 * j; } }
    if (!e) return;
    pre = (q - B - eo) / 2;
    for (j = 0; j < n; j++) {
        uint32_t oj = G32(B + 20 + 8 * j), L = u16_len(B + oj), m, shared = 0;
        if (L < pre + 12) continue;
        for (m = 0; m < 8; m++) if (s_nl[m].base == B && s_nl[m].slot == B + oj) shared = 1;
        for (m = 0; m < n && !shared; m++) if (m != j && G32(B + 20 + 8 * m) == oj) shared = 1;   /* (another entry reads it) */
        if (shared) continue;
        for (i = 0, used = 0; i < n; i++) {
            uint32_t oi = G32(B + 20 + 8 * i);
            if (i == j || oi == oj || !u16_same(B + oi, B + oj)) continue;
            for (m = 0; m < 8; m++) if (s_nl[m].base == B && (s_nl[m].slot == B + oi || s_nl[m].dto == oj || s_nl[m].dto == oi)) break;
            if (m < 8) continue;   /* (a pair another name took) */
            s_nl[k].base = B; s_nl[k].ent = e; s_nl[k].orig = eo; s_nl[k].slot = B + oj; s_nl[k].slotlen = L;
            s_nl[k].dent = B + 20 + 8 * j; s_nl[k].dto = oi; used = 1; break;
        }
        if (used) break;
    }
    tt_dbg("[HEROES] name %08X: table %08X, entry %08X, prefix %u, long slot %08X (%u)\n", q, B, e, pre, s_nl[k].slot, s_nl[k].slotlen);
}
static void name_long_set(int k, uint32_t q, const char *title)
{
    uint32_t B = s_nl[k].base, pre, i, L = (uint32_t)strlen(title);
    if (!B || G32(B) != 0x5353424Fu) return;
    if (G32(s_nl[k].ent) != s_nl[k].orig && G32(s_nl[k].ent) != s_nl[k].slot - B) return;   /* (not the table it was) */
    if (L <= 7) { G32(s_nl[k].ent) = s_nl[k].orig; return; }
    pre = (q - B - s_nl[k].orig) / 2;
    if (pre + L >= s_nl[k].slotlen) return;
    G32(s_nl[k].dent) = s_nl[k].dto;   /* the twin reads the first copy */
    for (i = 0; i < pre; i++) G16(s_nl[k].slot + 2 * i) = G16(B + s_nl[k].orig + 2 * i);
    for (i = 0; i <= L; i++) G16(s_nl[k].slot + 2 * (pre + i)) = i < L ? (uint16_t)(unsigned char)title[i] : 0;
    G32(s_nl[k].ent) = s_nl[k].slot - B;
    if (getenv("TT_NAMELOG")) { char t[64]; uint32_t a = B + G32(s_nl[k].ent); for (i = 0; i < 63 && G16(a + 2 * i); i++) t[i] = (char)G16(a + 2 * i); t[i] = 0; tt_dbg("[HEROES] name entry %08X now reads \"%s\"\n", s_nl[k].ent, t); }
}

static void name_set(const char *title)
{
    int k, i, end;
    if (tt_lang_on()) { snprintf(s_name_cur, sizeof s_name_cur, "%.7s", title); return; }   /* (a language: hl_set) */
    if (s_nname < 0) name_find();
    for (k = 0; k < s_nname; k++) {
        uint32_t q = s_name_at[k];
        /* still ours? (a menu file reloaded since puts other data there) */
        if (!u16_is(q, "Isildur", 1) && !u16_is(q, s_name_cur, 1)) continue;
        for (i = 0, end = 0; i < 8; i++) {   /* 7 letters at most, then the end */
            if (i == 7 || !title[i]) end = 1;
            G16(q + 2 * i) = end ? 0 : (uint16_t)title[i];
        }
        name_long_set(k, q, title);
    }
    snprintf(s_name_cur, sizeof s_name_cur, "%s", title);
}

/* ===================================================================== */
/* hero 4's records, one set per extra hero                              */
/* ===================================================================== */
/* What the save keeps per hero (0x000D2000 / 0x000D2130), for hero 4: */
typedef struct { uint32_t va, len; } Span;
static const Span s_rec[] = {
    { 0x4AB69Eu + 3 * 15, 15 },            /* levels open / done */
    { 0x4AB6DCu + 3 * 15 * 8, 15 * 8 },    /* per-level scores */
    { 0x4AB8BCu + 4 * 4, 4 },
    { 0x4AB944u + 3 * 0x348, 0x348 },      /* experience, upgrade points, ... */
    { 0x4AC600u + 3 * 0x50, 0x50 },        /* upgrades bought */
};
#define REC_BYTES (15 + 120 + 4 + 0x348 + 0x50)
static unsigned char s_recs[N_EXTRA + 1][REC_BYTES];   /* [0..N_SEL-1] extra heroes, [N_SEL] Isildur (the file's), then the Versus-only guests (not kept) */
#define REC_ISIL N_SEL
static int rec_slot(int g) { return g < 0 ? REC_ISIL : g < N_SEL ? g : g + 1; }
static int s_rec_have[N_EXTRA + 1];
static int s_rec_owner = -1;                           /* whose records hero 4 holds now: -1 Isildur */
static int s_rec_readonly;

static void rec_names(const char **names)
{
    int i; for (i = 0; i < N_EXTRA; i++) names[i] = s_skins[i].name;
}

static void rec_copy(unsigned char *buf, int to_mem)
{
    size_t o = 0, k, i;
    for (k = 0; k < sizeof s_rec / sizeof s_rec[0]; k++)
        for (i = 0; i < s_rec[k].len; i++, o++) {
            if (to_mem) G8(s_rec[k].va + i) = buf[o];
            else buf[o] = G8(s_rec[k].va + i);
        }
}
/* (1.9.7) Versus' progress kept apart: while Versus is in use (from its menu opening until it is left) the extra
 * heroes' records are copied away, nothing is written to records.dat, and on leaving they are put back (menus.c,
 * with the save data's own records) -- Versus' new game and its top level for both players are not the campaign's */
static unsigned char s_vs_recs[N_EXTRA + 1][REC_BYTES];
static int s_vs_rec_have[N_EXTRA + 1], s_vs_rec_owner, s_vs_rec_kept;
static void rec_load_file(void);
void tt_heroes_vs_keep(int keep)
{
    if (keep) { if (s_vs_rec_kept) return; rec_load_file(); memcpy(s_vs_recs, s_recs, sizeof s_recs); memcpy(s_vs_rec_have, s_rec_have, sizeof s_rec_have); s_vs_rec_owner = s_rec_owner; s_vs_rec_kept = 1; return; }
    if (!s_vs_rec_kept) return;
    memcpy(s_recs, s_vs_recs, sizeof s_recs); memcpy(s_rec_have, s_vs_rec_have, sizeof s_rec_have); s_rec_owner = s_vs_rec_owner; s_vs_rec_kept = 0;
}
static void rec_save_file(void)
{
    char p[700], tmp[710]; const char *names[N_EXTRA];
    FILE *f;
    if (s_vs_rec_kept) return;   /* (Versus: nothing of it kept) */
    int ok;
    if (s_rec_readonly || s_test_unlock) return;
    paths();
    rec_names(names);
    snprintf(p, sizeof p, "%s/heroes/records_v3.dat", s_dir);
    snprintf(tmp, sizeof tmp, "%s.tmp", p);
    mkdirs(p);
    if (!(f = fopen(tmp, "wb"))) { tt_dbg("[HEROES] no se pudo abrir el temporal de progreso\n"); return; }
    ok = tth_write(f, names, N_SEL, s_rec_have, &s_recs[0][0], REC_BYTES);
    if (fflush(f)) ok = 0;
    if (fclose(f)) ok = 0;
    if (ok) ok = TT_UNLOCK_REPLACE(tmp, p) != 0;
    if (!ok) tt_dbg("[HEROES] progreso no guardado; se conserva el sidecar anterior\n");
}
static void rec_load_file(void)
{
    char p[700]; const char *names[N_EXTRA];
    FILE *f;
    static int done;
    if (done) return;
    done = 1;
    paths();
    rec_names(names);
    snprintf(p, sizeof p, "%s/heroes/records_v3.dat", s_dir);
    if (GetFileAttributesA(p) == INVALID_FILE_ATTRIBUTES)
        snprintf(p, sizeof p, "%s/heroes/records.dat", s_dir);
    if ((f = fopen(p, "rb"))) {
        if (!tth_read(f, names, N_SEL, s_rec_have, &s_recs[0][0], REC_BYTES)) {
            s_rec_readonly = 1;
            tt_dbg("[HEROES] sidecar invalido: se conserva y no se sobrescribe: %s\n", p);
        }
        fclose(f);
    } else if (GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES) s_rec_readonly = 1;
    s_rec_have[REC_ISIL] = 0;    /* Isildur's are the save's */
}
/* hero 4 becomes guest g (-1 Isildur): the holder's records out, g's in */
static void rec_switch(int g)
{
    int slot = rec_slot(g), old;
    rec_load_file();
    if (s_rec_owner == g) return;
    old = rec_slot(s_rec_owner);
    rec_copy(s_recs[old], 0); s_rec_have[old] = 1;
    if (s_rec_have[slot]) rec_copy(s_recs[slot], 1);
    else {
        /* first time: a new hero -- level 1, nothing bought; the missions
         * open to him are the ones open to anyone */
        unsigned char fresh[REC_BYTES];
        int lv;
        memcpy(fresh, s_recs[old], REC_BYTES);
        memset(fresh + 15, 0, 120);                                  /* no scores */
        memset(fresh + 15 + 120 + 4, 0, 0x348);                      /* no experience */
        memset(fresh + 15 + 120 + 4 + 0x348, 0, 0x50);               /* nothing bought */
        /* 1 means completed, not merely available. Availability is queried
         * through id 14; never inherit another character's completions. */
        for (lv = 0; lv < 15; lv++) fresh[lv] = 0;
        rec_copy(fresh, 1);
    }
    s_rec_owner = g;
    rec_save_file();
    tt_dbg("[HEROES] hero 4 is now %s\n", g < 0 ? "Isildur" : s_skins[g].name);
}
static int s_rec_saving_owner = -2;
/* co-op: an extra hero's own hero record (0x348 bytes: experience, level, the level's running numbers; the save's
 * format, 0x4AB598 + hero * 0x348) for player 2, from heroes\records.dat (or hero 4's live one if he is hero 4 now).
 * 1 = found, 0 = he has none yet (a new hero: the caller starts him fresh, as rec_switch does) */
/* co-op: a retail hero's hero byte (1..4: [0x4AB69D], his runtime record 0x4AB598 + byte * 0x348), 0 for an extra hero */
int tt_heroes_retail_byte(const char *skin)
{
    int g = skin ? skin_by_name(skin) : -1;
    return g >= 0 && skin_retail(g) ? s_retail_hero[g - N_EXTRA] : 0;
}
int tt_heroes_record_of(const char *skin, unsigned char *out)
{
    /* the save's per-hero block (s_rec's 0x4AB944 + h * 0x348) starts 0x64 bytes into the runtime record of hero h + 1
     * (0x000D26F0: 0x4AB598 + index * 0x348): so it gives the record from +0x64 on (experience +0x6C, level +0x78,
     * the kill counts); its first 0x64 bytes (the combat meter, the Perfect time) are left to the caller */
    int g = skin_by_name(skin);
    if (g >= 0 && skin_retail(g)) {   /* a retail hero: his own record in the save (Isildur's: records.dat's while a guest is hero 4) */
        int h = s_retail_hero[g - N_EXTRA] - 1; uint32_t i;
        rec_load_file();
        if (h == 3 && s_rec_owner >= 0) { if (!s_rec_have[REC_ISIL]) return 0; memcpy(out + 0x64, s_recs[REC_ISIL] + 15 + 120 + 4, 0x348 - 0x64); return 1; }
        for (i = 0x64; i < 0x348; i++) out[i] = G8(0x4AB944u + (uint32_t)h * 0x348u + (i - 0x64));
        return 1;
    }
    if (g < 0 || g >= N_EXTRA) return 0;
    rec_load_file();
    if (s_rec_owner == g) { uint32_t i; for (i = 0x64; i < 0x348; i++) out[i] = G8(0x4AB944u + 3u * 0x348u + (i - 0x64)); return 1; }
    if (!s_rec_have[rec_slot(g)]) return 0;
    memcpy(out + 0x64, s_recs[rec_slot(g)] + 15 + 120 + 4, 0x348 - 0x64);
    return 1;
}

/* Thank you Gumpolas and contributors for the port and original extras.
 * D2B90 calls D2A30 with value 1 on victory. The campaign menus only
 * consult heroes 1..3, never the extra's temporary hero-4 record.
 * Share completion flags only; XP, purchases and scores remain private.
 * Only the successfully completed mission is shared (never old sidecar flags).
 */
void tt_heroes_result(uint32_t level, int success)
{
    if (!tt_heroes_on() || s_test_unlock || s_guest < 0 || G8(HERO_BYTE) != 4 ||
        !success || level < 2 || level > 15) return;
    uint32_t lv = level - 1;
    if (G8(0x4AB68Fu + 60 + lv) == 1) G8(0x4AB68Fu + 15 + lv) = 1;
    /* The original result handler marks both Orthanc records at level 14. */
    if (lv == 13 && G8(0x4AB68Fu + 60 + 12) == 1) G8(0x4AB68Fu + 15 + 12) = 1;
}

int tt_heroes_save_blocked(void)
{
    return tt_heroes_on() && s_test_unlock;
}

/* ===================================================================== */
/* the level: point the hero's keys at the skin's                         */
/* ===================================================================== */
static uint32_t s_buf;     /* the level file in guest memory */

static int find_level_buffer(void)
{
    uint32_t va;
    /* the directory's first entry is {400, 8, 0}; the file starts n bytes
     * before its count, with version 9 and n at +4 */
    for (va = 0x80000000u; va < 0x83FFFF00u; va += 4) {
        uint32_t base;
        if (G32(va + 4) != 400u || G32(va + 8) != 8u || G32(va + 12) != 0u) continue;
        for (base = va & ~0xFFFu; base >= 0x80000000u && va - base < 0x2000000u; base -= 0x1000u)
            if (G8(base) == 9 && G32(base + 4) == va - base) { s_buf = base; return 1; }
    }
    return 0;
}
static int cmp_key(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}
static uint32_t key_type(uint32_t (*t)[2], uint32_t n, uint32_t key)
{
    uint32_t lo = 0, hi = n;
    if (!key) return 0;
    while (lo < hi) {
        uint32_t m = (lo + hi) / 2;
        if (t[m][0] < key) lo = m + 1; else if (t[m][0] > key) hi = m; else return t[m][1];
    }
    return 0;
}
/* the hero's placement record -- {mesh 100, anim 150, tex 300} after a
 * name with "Hero" in it -- and every reference to its keys -> the skin's */
/* co-op: player 2's pairs (the level's hero keys -> his skin's, for the twin's record and weapons) and his resources in
 * the player's pack. hero: the keys the level's hero has now (the hero's own, or a guest's as player 1 after his swap) */
static void coop_pairs_apply(const uint32_t *hero, const uint32_t *to, int sk)
{
    uint32_t va, n = G32(s_buf + 4), cnt = G32(s_buf + n);
        int k; const char *ws; uint32_t wm, wt;
        tt_coop_skin_n = 0;
        for (k = 0; k < 3; k++) if (to[k] != hero[k]) { tt_coop_skin_pairs[tt_coop_skin_n][0] = hero[k]; tt_coop_skin_pairs[tt_coop_skin_n][1] = to[k]; tt_coop_skin_n++; }
        if (skin_weapon(sk, 1, &ws, &wm, &wt)) {   /* his sword-hand weapon for Aragorn's sword (model, texture: the weapon record's +0x40/+0x50) */
            tt_coop_skin_pairs[tt_coop_skin_n][0] = 0x4586A62Cu; tt_coop_skin_pairs[tt_coop_skin_n][1] = wm; tt_coop_skin_n++;
            tt_coop_skin_pairs[tt_coop_skin_n][0] = 0xFFFFFFFFu; tt_coop_skin_pairs[tt_coop_skin_n][1] = wt; tt_coop_skin_n++; }   /* (the texture: matched by position, see the co-op code) */
        for (k = 0; k < tt_coop_skin_n; k++) tt_dbg("[HEROES] co-op pair %d: %08X -> %08X\n", k, tt_coop_skin_pairs[k][0], tt_coop_skin_pairs[k][1]);
        {   /* the level's resource lists ({index, key} pairs: ... 14 tex, 13 anim, 12 mesh ...) name the hero's keys; only
             * what they name gets loaded (the skin's texture missing: player 2 came out flat, observed). The first
             * TT_COOP_SKINLISTS (1) of them get the skin's texture (and mesh, TT_COOP_SKINLISTMESH), the others keep the hero's */
            /* (2026-10-19) which list: each sits in a type-500 resource (a pack). The pack keyed 0E01E39C is in every
             * level (Weathertop's, Balin's Tomb's): the player's own; the others are the level's cutscenes'. The first
             * list found was a cutscene's on Weathertop and in Balin's Tomb: there the hero was drawn with player 2's
             * model, moves and texture (a playtest: "Aragorn's animation is weird during the cutscene", "the
             * character selection should not affect characters during cutscenes"). The player's pack now; the first list only
             * where a level has none. TT_COOP_SKINPACK: another pack key (0: the first list, as before) */
            uint32_t want = getenv("TT_COOP_SKINPACK") ? (uint32_t)strtoul(getenv("TT_COOP_SKINPACK"), NULL, 16) : 0x0E01E39Cu, pick = 0, first = 0, e;
            int lists = getenv("TT_COOP_SKINLISTS") ? atoi(getenv("TT_COOP_SKINLISTS")) : 1, done = 0;
            for (va = s_buf + 8; va + 0x10 <= s_buf + n; va += 4) {
                uint32_t best = 0, bkey = 0, rel = va - s_buf;
                if (G32(va) != hero[2] || G32(va - 4) >= 0x100u || G32(va + 4) >= 0x100u || G32(va + 8) != hero[1]) continue;
                for (e = 0; e < cnt; e++) { uint32_t ty2 = G32(s_buf + n + 4 + 12 * e), of = G32(s_buf + n + 4 + 12 * e + 4); if (ty2 == 500 && of <= rel && of >= best) { best = of; bkey = G32(s_buf + n + 4 + 12 * e + 8); } }
                tt_dbg("[HEROES] co-op: the hero's resources listed at +%X, in pack %08X\n", rel, bkey);
                if (!first) first = va;
                if (want && bkey == want && !pick) pick = va;
            }
            if (!pick) pick = first;
            for (va = pick; va && va + 0x10 <= s_buf + n && done < lists; va += 4) {
                if (G32(va) != hero[2] || G32(va - 4) >= 0x100u || G32(va + 4) >= 0x100u || G32(va + 8) != hero[1]) continue;   /* {.., n, tex, n-1, anim, n-2, mesh} */
                G32(va) = to[2];
                if (s_native && to[1] != hero[1]) G32(va + 8) = to[1];   /* his own animation set (native: his skeleton, his composed moves) */
                if ((s_native || getenv("TT_COOP_SKINLISTMESH")) && G32(va + 0x10) == hero[0]) G32(va + 0x10) = to[0];
                tt_dbg("[HEROES] co-op: resource list at +%X: texture %08X -> %08X%s\n", va - s_buf, hero[2], to[2], s_native ? ", animations and model too (native)" : ""); done++;
            }
        }
}
static void skin_apply_one(void);
/* player 1 a guest and player 2 a character of his own: after the guest's swap, player 2's pairs from the guest's keys */
static void skin_apply(void)
{
    skin_apply_one();
    if (s_coop_skin && s_playas >= 0 && G8(HERO_BYTE) == 4 && LVG >= 0 && s_playas != LVG && find_level_buffer()) {
        const Skin *g = &s_skins[LVG], *q = &s_skins[s_playas];
        uint32_t hero[3], to[3];
        hero[0] = PLAYER_KEY(g->mesh); hero[1] = (g->map && !s_native) ? 0 : PLAYER_KEY(g->anim); hero[2] = PLAYER_KEY(g->tex);
        to[0] = PLAYER_KEY(q->mesh); to[1] = PLAYER_KEY(q->anim); to[2] = PLAYER_KEY(q->tex);
        tt_dbg("[HEROES] co-op: player 1 is %s (hero 4), player 2 %s: his pairs from %s's keys\n", g->name, q->name, g->name);
        if (hero[1]) coop_pairs_apply(hero, to, s_playas);
    }
}
static void skin_apply_one(void)
{
    int sk = active_skin();
    uint32_t n, cnt, i, va, hero[3] = { 0, 0, 0 }, to[3];
    static uint32_t keyt[4096][2];
    uint32_t nk;
    if (sk < 0 || !find_level_buffer()) return;
    hero_size_bind(s_buf,sk);
    n = G32(s_buf + 4); cnt = G32(s_buf + n);
    nk = cnt < 4096 ? cnt : 4096;
    for (i = 0; i < nk; i++) { keyt[i][0] = G32(s_buf + n + 4 + 12 * i + 8); keyt[i][1] = G32(s_buf + n + 4 + 12 * i); }
    qsort(keyt, nk, sizeof keyt[0], cmp_key);
    for (va = s_buf + 8; va + 12 < s_buf + n && !hero[0]; va += 4) {
        uint32_t a = G32(va), b, c, j;
        if (key_type(keyt, nk, a) != 100) continue;
        b = G32(va + 4); c = G32(va + 8);
        if (key_type(keyt, nk, b) != 150 || key_type(keyt, nk, c) != 300) continue;
        for (j = 4; j < 0x80 && !hero[0]; j++) {
            char nm[40];
            uint32_t q = va - j, z;
            if (G8(q) < 'A' || G8(q - 1) != 0) continue;
            for (z = 0; z < 39 && G8(q + z) >= 32 && G8(q + z) < 127; z++) nm[z] = (char)G8(q + z);
            nm[z] = 0;
            if (z >= 4 && (strstr(nm, "Hero") || strstr(nm, "hero"))) {
                hero[0] = a; hero[1] = b; hero[2] = c;
                tt_dbg("[HEROES] hero record \"%s\": mesh %08X anim %08X tex %08X\n", nm, a, b, c);
            }
        }
    }
    if (!hero[0]) { tt_dbg("[HEROES] no hero record found\n"); return; }
    to[0] = PLAYER_KEY(s_skins[sk].mesh); to[1] = (s_skins[sk].map && !s_native) ? hero[1] : PLAYER_KEY(s_skins[sk].anim); to[2] = PLAYER_KEY(s_skins[sk].tex);
    if (s_coop_skin && sk == s_playas) { coop_pairs_apply(hero, to, sk); return; }   /* co-op: nothing swapped; the pairs for player 2 */
    {
        int hits = 0, kept = 0;
        for (va = s_buf + 8; va + 4 <= s_buf + n; va += 4) {
            uint32_t v = G32(va), k;
            for (k = 0; k < 3; k++) if (v == hero[k]) {
                /* only where the hero's model stands close by (his records,
                 * the cutscenes' copies of him, the level's list of his
                 * resources): others share the hero's moves (an elf, Sauron
                 * in the prologue) and keep them */
                if (k > 0 && !getenv("TT_SWAPALL")) {
                    uint32_t z, nearm = 0;
                    for (z = 4; z <= 0x20 && !nearm; z += 4)
                        if ((va - z >= s_buf + 8 && (G32(va - z) == hero[0] || G32(va - z) == to[0])) || (va + z + 4 <= s_buf + n && G32(va + z) == hero[0])) nearm = 1;
                    if (!nearm) { kept++; break; }
                }
                if (getenv("TT_REFLOG")) {   /* testing: where the hero's keys stand */
                    char nm[24]; uint32_t z, q = va - 0x40;
                    for (z = 0; z < 0x40; z += 4) if (G32(va - z) == 0x20 || (G8(va - z) >= 'a' && G8(va - z) <= 'z' && G8(va - z + 1) >= 'a')) { q = va - z; }
                    for (z = 0; z < 23 && G8(q + z) >= 32 && G8(q + z) < 127; z++) nm[z] = (char)G8(q + z);
                    nm[z] = 0;
                    tt_dbg("[REF] +%X key %d: %08X %08X [%08X] %08X %08X near \"%s\"\n", va - s_buf, k, G32(va - 8), G32(va - 4), v, G32(va + 4), G32(va + 8), nm);
                }
                G32(va) = to[k]; hits++; break;
            }
        }
        tt_dbg("[HEROES] %s: %d references swapped (%d left to others)\n", s_skins[sk].name, hits, kept);
    }
    /* (1.7 final) The cutscenes' copies of the hero (Fangorn's: records
     * with his model and a set of his own, the hero's skeleton and the
     * cutscene's moves) kept that set: his model, now the guest's, was
     * posed on the hero's skeleton and came out torn and stretched. Beside
     * the guest's model, a set with the hero's bone count becomes the
     * guest's (his skeleton, the hero's moves made his). TT_NOCINESET: as
     * before. */
    if (s_native && to[1] != hero[1] && !getenv("TT_NOCINESET")) {
        int moved = 0;
        uint32_t hb = 0, e;
        for (e = 0; e < cnt; e++) if (G32(s_buf + n + 4 + 12 * e + 8) == hero[1] && G32(s_buf + n + 4 + 12 * e) == 150) hb = G32(s_buf + 0x40 + 8 + G32(s_buf + n + 4 + 12 * e + 4));
        for (va = s_buf + 8; hb && va + 4 <= s_buf + n; va += 4) {
            int d;
            if (G32(va) != to[0]) continue;
            for (d = -3; d <= 3; d++) {
                uint32_t q = va + 4 * (uint32_t)d, v, e2;
                if (!d || q < s_buf + 8 || q + 4 > s_buf + n) continue;
                v = G32(q);
                if (v == hero[1] || v == to[1] || key_type(keyt, nk, v) != 150) continue;
                for (e2 = 0; e2 < cnt; e2++)
                    if (G32(s_buf + n + 4 + 12 * e2 + 8) == v && G32(s_buf + n + 4 + 12 * e2) == 150) {
                        uint32_t so = s_buf + G32(s_buf + n + 4 + 12 * e2 + 4);
                        if (G32(so) == 0x7A696E61u && G32(so + 0x48) == hb) {
                            tt_dbg("[HEROES]   a cutscene's copy of him (+%X): set %08X -> his\n", q - s_buf, v);
                            G32(q) = to[1]; moved++;
                        }
                        break;
                    }
            }
        }
        if (moved) tt_dbg("[HEROES] %s: %d cutscene sets made his\n", s_skins[sk].name, moved);
    }
    /* his own weapon: the hero's weapon records ("weaponObjNN", 0x16 bytes
     * of header, position at +0x28, model key +0x40, texture +0x50) that
     * stand where a hero record does hold Aragorn's bow or sword; the one
     * for his hand becomes his */
    if (s_skins[sk].wmesh || s_skins[sk].w2mesh) {
        static const uint32_t hand_key[2] = { 0x36AC3F58u, 0x4586A62Cu };   /* Aragorn's bow, sword */
        float hx[8], hy[8];
        int nh = 0, swapped = 0;
        for (va = s_buf + 8; va + 0x60 < s_buf + n && nh < 8; va += 4) {
            char nm[40];
            uint32_t z;
            if (G32(va) != 1 || G32(va + 4) != 0x20) continue;
            for (z = 0; z < 39 && G8(va + 8 + z) >= 32 && G8(va + 8 + z) < 127; z++) nm[z] = (char)G8(va + 8 + z);
            nm[z] = 0;
            if (z >= 4 && strstr(nm, "Hero")) { hx[nh] = GF(va + 0x2C); hy[nh] = GF(va + 0x30); nh++; }
        }
        for (va = s_buf + 8; va + 0x60 < s_buf + n; va += 4) {
            int h;
            if (G32(va) != 0x16 || G32(va + 4) != 0x20 || G32(va + 8) != 0x70616577u /* "weap" */) continue;
            const char *ws; uint32_t wm, wt; int sl;
            for (sl = 0; sl < 2 && G32(va + 0x40) != hand_key[sl]; sl++) ;
            if (sl == 2 && G32(va + 0x40) == 0x13F82514u && !getenv("TT_NOSWORD2")) sl = 1;   /* the sword drawn in his hand */
            else if (sl == 1 && !arrow_is_his(sk)) continue;   /* the arrow his draw hand holds as he shoots */
            if (sl == 2 || !skin_weapon(sk, sl, &ws, &wm, &wt)) continue;
            for (h = 0; h < nh; h++)
                if (fabsf(GF(va + 0x28) - hx[h]) < 0.01f && fabsf(GF(va + 0x2C) - hy[h]) < 0.01f) {
                    G32(va + 0x40) = wm; G32(va + 0x50) = wt; swapped++;
                    break;
                }
        }
        tt_dbg("[HEROES] %s: his weapon in %d weapon record(s) (%d hero records)\n", s_skins[sk].name, swapped, nh);
    }
}

/* TT_KEYSWAP="A=B,C=D" (testing): every aligned dword equal to key A in
 * the contiguous window becomes B, but for the resource directories */
static void keyswap_apply(void)
{
    const char *e = getenv("TT_KEYSWAP");
    uint32_t from[16], to[16], va;
    int n = 0, hits = 0, i;
    if (!e) return;
    while (*e && n < 16) {
        char *q;
        from[n] = (uint32_t)strtoul(e, &q, 16);
        if (*q != '=') break;
        to[n] = (uint32_t)strtoul(q + 1, &q, 16);
        n++;
        e = (*q == ',') ? q + 1 : q;
        if (!*q) break;
    }
    for (va = 0x80000000u; va < 0x84000000u; va += 4) {
        uint32_t v = G32(va);
        for (i = 0; i < n; i++)
            if (v == from[i]) {
                if (G32(va + 4) < 1000u && G32(va - 4) < 0x2000000u && (G32(va - 4) & 0x7Fu) == 0) break;
                G32(va) = to[i]; hits++; break;
            }
    }
    tt_dbg("[KEYSWAP] %d keys, %d places\n", n, hits);
}

/* ===================================================================== */
/* the character select                                                   */
/* ===================================================================== */
#define FE_MGR   0x49F440u     /* the menu's character manager: [0] array (0x1E0 each), [8] room (8; 12 with the mod: tt_fe_room) */
#define FE_TEXT  0x49FBE0u
#define N_PLACES (4 + N_SEL)
static int s_fe_extra = 0;               /* extra characters created (actors 4..) */
static uint32_t s_fe_last_args[4];       /* the fourth retail character's resources */
static uint32_t s_fe_anim0;              /* the first one's (Aragorn's) animation set */
static int s_spawning;
static int s_spawn_pending;               /* the menu's fourth character exists, ours not yet */
static int s_cursor = -1;                /* our place, 0..7 (the game's [menu+0x18] is min(this, 3)) */
int tt_select_place(void) { return s_cursor; }   /* (menus.c: the mouse sees the ring turn) */
static float s_ang[N_PLACES];            /* current angle, radians */
static int s_ang_ok;
static uint32_t s_actor[N_PLACES];       /* the characters' records in the manager, by creation */

/* The manager finds a character by the number in its record (+0x1B8), and
 * the game's own code (the "level" labels, the chosen pose, the confirm move)
 * works on character 3 for hero 4.  So the guest who is hero 4 takes number 3
 * and Isildur takes the guest's. */
static void hero4_actor(int g)
{
    int e;
    if (!s_actor[3]) return;
    G32(s_actor[3] + 0x1B8) = g < 0 ? 3u : (uint32_t)(4 + g);
    for (e = 0; e < N_SEL; e++)
        if (s_actor[4 + e]) G32(s_actor[4 + e] + 0x1B8) = e == g ? 3u : (uint32_t)(4 + e);
}
/* the number place p's character answers to now */
/* the carousel's order of the extra heroes (1.8: Sauron last, after the
 * Uruk-hai): place 4 + i holds guest s_place_order[i] */
static const int s_place_order[N_SEL] = { 0, 1, 2, 3, 4, 6, 5 };
static int place_actor(int p) { return p >= 4 && p < 4 + N_SEL ? 4 + s_place_order[p - 4] : p; }
static int guest_place(int g) { int i; for (i = 0; i < N_SEL; i++) if (s_place_order[i] == g) return 4 + i; return 4 + g; }
static uint32_t aid(int p) { int q = place_actor(p); return s_actor[q] ? G32(s_actor[q] + 0x1B8) : (uint32_t)q; }

static int s_h4_pending;   /* hero 4's number to hand to s_guest once the select runs */
static void fe_spawn_extras(void)
{
    int g;
    s_fe_extra = 0;
    s_spawning = 1;
    static int own_anim = -1;   /* testing: TT_FE_OWNANIM, the menu file's own animation sets */
    uint32_t mesh[N_SEL], anim[N_SEL], tex[N_SEL], wmesh = 0, wtex = 0, gwm[N_SEL], gwt[N_SEL];
    const volatile uint32_t *w = (const volatile uint32_t *)(xbox_GetMemoryOffset() + 0x80000000u);
    const uint32_t nw = (0x04000000u - 0x20u) / 4u, KEY0 = FE_KEY(0);
    uint32_t i, nearp = s_fe_last_args[1];
    const int fewlog = getenv("TT_FEWLOG") != NULL;
    /* several copies can be in memory (an earlier menu's, not cleared): the
     * one nearest the fourth retail character's model is this menu's */
#define IS_PROP(d) ((d) >= 0x80000000u && (d) < 0x83FFFFF0u && (w[((d) - 0x80000000u) / 4u] == PROP_MAGIC \
    || (w[((d) - 0x80000000u) / 4u] >= 0x80000000u && w[((d) - 0x80000000u) / 4u] < 0x83FFFFF0u && w[(w[((d) - 0x80000000u) / 4u] - 0x80000000u) / 4u] == PROP_MAGIC)))
#define CLOSER(cur, va) (!(cur) || (((va) > nearp ? (va) - nearp : nearp - (va)) < ((cur) > nearp ? (cur) - nearp : nearp - (cur))))
    if (own_anim < 0) own_anim = getenv("TT_FE_OWNANIM") != NULL;
    memset(mesh, 0, sizeof mesh); memset(anim, 0, sizeof anim); memset(tex, 0, sizeof tex); memset(gwm, 0, sizeof gwm); memset(gwt, 0, sizeof gwt);
    /* the menu file's resource objects, wherever the loader put them in the
     * contiguous window: meshes and animation sets {0, key, 0, data} (data
     * "\x20XBG" / "aniz"), textures {0x1709C4, 0, key, 0, data} */
    for (i = 0; i < nw; i++) {
        uint32_t k = w[i + 1] - KEY0, d;
        /* Aragorn's sword on the character select: model and texture key 2 */
        if (w[i] == 0 && w[i + 1] == MENU_SWORD_KEY && w[i + 2] == 0 && w[i + 3] >= 0x80000000u && w[i + 3] < 0x83FFFFF0u
            && (w[(w[i + 3] - 0x80000000u) / 4u] == 0x47425820u || IS_PROP(w[i + 3]))) { uint32_t va = 0x80000000u + i * 4u; if (CLOSER(wmesh, va)) wmesh = va; }
        /* each guest's own weapon: prop and texture keys FE_WKEY(g) */
        {
            uint32_t kw = w[i + 1] - FE_WKEY(0), kt = w[i + 2] - FE_WKEY(0);
            if (fewlog && w[i] == 0 && (kw < N_SEL || w[i + 1] == MENU_SWORD_KEY) && w[i + 2] == 0 && w[i + 3] >= 0x80000000u && w[i + 3] < 0x83FFFFF0u)
                tt_dbg("[HEROES] fe weapon? key %X at %08X data %08X magic %08X %08X\n", w[i + 1], 0x80000000u + i * 4u, w[i + 3], w[(w[i + 3] - 0x80000000u) / 4u], w[(w[i + 3] - 0x80000000u) / 4u + 1]);
            if (w[i] == 0 && kw < N_SEL && w[i + 2] == 0 && IS_PROP(w[i + 3])) { uint32_t va = 0x80000000u + i * 4u; if (CLOSER(gwm[kw], va)) gwm[kw] = va; }
            if (w[i] == 0x001709C4u && w[i + 1] == 0 && kt < N_SEL && w[i + 4] >= 0x80000000u && w[i + 4] < 0x84000000u)
                { uint32_t va = 0x80000000u + i * 4u; if (CLOSER(gwt[kt], va)) gwt[kt] = va; }
        }
        if (w[i] == 0x001709C4u && w[i + 1] == 0 && w[i + 2] == MENU_SWORD_KEY && w[i + 4] >= 0x80000000u && w[i + 4] < 0x84000000u)
            { uint32_t va = 0x80000000u + i * 4u; if (CLOSER(wtex, va)) wtex = va; }
        if (w[i] == 0 && k < N_SEL && w[i + 2] == 0) {
            d = w[i + 3];
            if (d >= 0x80000000u && d < 0x83FFFFF0u) {
                uint32_t m = w[(d - 0x80000000u) / 4u];
                uint32_t va = 0x80000000u + i * 4u;
                if (m == 0x47425820u) { if (CLOSER(mesh[k], va)) mesh[k] = va; }
                else if (m == 0x7A696E61u) { if (CLOSER(anim[k], va)) anim[k] = va; }
            }
        }
        k = w[i + 2] - KEY0;
        if (w[i] == 0x001709C4u && k < N_SEL && w[i + 1] == 0 && w[i + 4] >= 0x80000000u && w[i + 4] < 0x84000000u)
            { uint32_t va = 0x80000000u + i * 4u; if (CLOSER(tex[k], va)) tex[k] = va; }
    }
    for (g = 0; g < N_SEL; g++) {   /* all four, or none yet */
        uint32_t an = (s_skins[g].map && !own_anim && !s_fe_native) ? s_fe_anim0 : anim[g];
        if (!mesh[g] || !an || !tex[g]) {
            if (s_spawn_pending == 1) tt_dbg("[HEROES] %s: menu model not loaded yet (%08X %08X %08X)\n", s_skins[g].name, mesh[g], an, tex[g]);
            s_spawn_pending = 2;
            s_spawning = 0;
            return;
        }
    }
    for (g = 0; g < N_SEL; g++) {
        uint32_t a[4], r, an = (s_skins[g].map && !own_anim && !s_fe_native) ? s_fe_anim0 : anim[g];   /* re-rigged: Aragorn's moves */
        a[0] = an; a[1] = mesh[g]; a[2] = tex[g]; a[3] = s_fe_last_args[3];   /* the shared second texture */
        r = tt_gcall(0x000CA490u, FE_MGR, 4, a);
        if ((int)r != 4 + g) { tt_dbg("[HEROES] %s: character %d, not %d\n", s_skins[g].name, (int)r, 4 + g); break; }
        tt_dbg("[HEROES] %s: character %d, weapon %08X %08X\n", s_skins[g].name, (int)r, gwm[g], gwt[g]);
        if (getenv("TT_NOFEW")) ;
        else if (gwm[g] && gwt[g]) {   /* his own weapon in hand: the game's own way, as for the retail four */
            uint32_t w3[3] = { r, gwm[g], gwt[g] };
            tt_gcall(0x000CA540u, FE_MGR, 3, w3);
        } else if (s_skins[g].map && wmesh && wtex && getenv("TT_FE_ARASWORD")) {   /* (testing: else Aragorn's sword) */
            /* (1.7: no longer -- the menu's sword object is Aragorn's and
             * Isildur's; given to a guest whose own was not found, it left
             * Isildur empty-handed on the select) */
            uint32_t w3[3] = { r, wmesh, wtex };
            tt_gcall(0x000CA540u, FE_MGR, 3, w3);
        }
        tt_dbg("[HEROES] %s: weapon on the select %08X %08X\n", s_skins[g].name, gwm[g], gwt[g]);
        s_fe_extra++;
    }
    s_spawning = 0;
    s_ang_ok = 0;
    if (s_fe_extra == N_SEL) s_spawn_pending = 0;
    memset(s_actor, 0, sizeof s_actor);
    if (getenv("TT_FE_GUEST")) s_guest = skin_by_name(getenv("TT_FE_GUEST"));   /* testing: the select as after playing him */
    if (s_fe_extra == N_SEL) {
        uint32_t arr = G32(FE_MGR), cnt = G32(FE_MGR + 8), k;
        for (k = 0; k < cnt && k < N_PLACES; k++) {
            uint32_t id = G32(arr + k * 0x1E0u + 0x1B8u);
            if (id < N_PLACES) s_actor[id] = arr + k * 0x1E0u;
        }
        /* (1.7) hero 4's number goes to the guest last played only once the
         * select runs (layout): the menu's script, still running, gives
         * Isildur his sword and moves by his number (3) -- done here, the
         * guest got Isildur's sword and Isildur stood empty-handed in no
         * motion (a T-pose) */
        s_h4_pending = 1;
        if (getenv("TT_FE_H4EARLY")) hero4_actor(s_guest);   /* testing: the old way */
    }
    tt_dbg("[HEROES] %d extra characters on the character select (%s)\n", s_fe_extra,
           unlocked() ? "unlocked" : "locked: finish Hornburg Courtyard and the Tower of Orthanc first");
}

/* the character manager's room (8: the four, Isildur, and three more
 * the game never uses): the seven guests need eleven */
uint32_t tt_fe_room(uint32_t room)
{
    if (tt_heroes_on() && room == 8 && !getenv("TT_FE_ROOM8")) { tt_dbg("[HEROES] the character select's room: %u -> %u\n", room, (uint32_t)(N_PLACES + 1)); return N_PLACES + 1; }
    return room;
}
static int places(void)
{
    return (tt_heroes_on() && s_fe_extra == N_SEL) ? N_PLACES : 0;
}
/* how faint a locked place stands (TT_LOCKALPHA, testing) */
static float lock_alpha(void)
{
    static float a = -1;
    if (a < 0) a = getenv("TT_LOCKALPHA") ? (float)atof(getenv("TT_LOCKALPHA")) : 0.2f;
    return a;
}
/* a place the cursor passes by: Isildur until he is unlocked (the final
 * and the secret mission done), a guest until his own unlock. All of them
 * stand in the ring from the first character select on, as faint shadows
 * while locked */
static int place_locked(int p)
{
    if (p == 3) return !unlocked();
    return p >= 4 && (!unlocked() || !guest_unlocked(place_actor(p) - 4));
}

/* the guest a place stands for: -1 Isildur / not hero 4 */
static int place_guest(int p) { return p >= 4 ? place_actor(p) - 4 : -1; }

static void cursor_changed(uint32_t menu)
{
    G32(menu + 0x18) = (uint32_t)(s_cursor < 4 ? s_cursor : 3);
    if (s_cursor >= 3) {
        int g = place_guest(s_cursor);
        s_guest = g;
        rec_switch(g);
        hero4_actor(g);
        name_set(g < 0 ? "Isildur" : s_skins[g].title);
        upn_set(g);
    }
}

/* the ring: place p's angle, degrees, with c at the front */
static float target_deg(int p, int c, int n) { return 180.0f + 360.0f / n * (float)(((p - c) % n + n) % n); }

/* ---- the "Level" labels of the characters away from the front (1.7) ----
 * 0x00075430 draws them for the characters numbered 0..3 (two elements
 * each, 0xDC bytes: the word at 0x42DAB8 + 0xDC n, the number at
 * 0x42DE28 + 0xDC n), each at its character's place (0x000CA670) plus his
 * label's offset (0x000CAA50), through the view and projection, faded with
 * him. The guests (numbered 4 on) had none, nor Isildur when a guest is
 * hero 4 (he takes the guest's number). After the game's, the same two
 * elements for each unlocked one of them, his level from his records. */
static float s_lab_alpha[N_PLACES];
static uint32_t s_lab_l4; static int s_lab_l4_set;   /* hero 4's level, put back after the game's labels */
static uint32_t s_lab_el;   /* their elements (contiguous memory: the draw may keep pointers to them) */
/* Dos elementos por plaza; ocho pares ya no cubren el roster ampliado. */
#define HERO_LABEL_BYTES ((2u * N_PLACES * 0xDCu + 15u) & ~15u)
static void mat_row(const float *v, uint32_t m, float *o)
{
    int c;
    for (c = 0; c < 4; c++) o[c] = v[0] * GF(m + 4 * c) + v[1] * GF(m + 16 + 4 * c) + v[2] * GF(m + 32 + 4 * c) + v[3] * GF(m + 48 + 4 * c);
}
static int hero_level_of(int g)   /* g a guest, or -1 Isildur; the level shown (his records' +1) */
{
    const unsigned char *r;
    uint32_t v;
    int slot = rec_slot(g);
    if (s_rec_owner == g) return (int)G32(0x4AB60Cu + 4 * 0x348u) + 1;   /* hero 4's now */
    if (g >= 0 && !s_rec_have[slot]) return 1;                          /* never played: level 1 */
    r = s_recs[slot] + 15 + 120 + 4 + 0x10;   /* the level: hero block +0x10 (0x000D2610) */
    v = (uint32_t)r[0] | (uint32_t)r[1] << 8 | (uint32_t)r[2] << 16 | (uint32_t)r[3] << 24;
    return (int)v + 1;
}
static int lab_log(void) { static int v = -1; if (v < 0) v = getenv("TT_LABLOG") != NULL; return v; }
extern int tt_select_unavail; static uint32_t s_unav_mem; static uint32_t unav_text(void);
static void extra_labels(void)
{
    int n = places(), p, k = 0;
    uint32_t scratch = (tt_guest_esp() - 0x3000u) & ~0xFu, view, proj;
    const uint32_t A0 = 0x42DAB8u, B0 = 0x42DE28u;
    if (lab_log()) { static unsigned f2; if ((f2++ & 255) == 0) tt_dbg("[LAB] places %d view/proj ...\n", n); }
    if (!n || getenv("TT_NOLABELS")) return;
    if (!s_lab_el) { extern uint32_t xbox_ContiguousAlloc(uint32_t size, uint32_t alignment); s_lab_el = xbox_ContiguousAlloc(HERO_LABEL_BYTES, 16u); if (!s_lab_el) return; }
    if (!G32(0x49F444u)) return;
    view = tt_gcall(0x0004EAF0u, G32(0x49F444u), 0, NULL);
    proj = tt_gcall(0x0004EB00u, G32(0x49F444u), 0, NULL);
    { static unsigned f3; if (lab_log() && (f3++ & 255) == 0) { int i; tt_dbg("[LAB] view %08X proj %08X\n", view, proj); for (i = 0; i < n; i++) tt_dbg("[LAB]  p %d aid %u locked %d\n", i, aid(i), place_locked(i)); } }
    if (view < 0x10000u || proj < 0x10000u) return;
    for (p = 0; p < n && k < N_PLACES; p++) {
        uint32_t num = aid(p), pp, po, a8, e, q;
        int g, lv;
        float v[4], t[4], c[4], x, y, al;
        if (num < 4 || place_locked(p)) continue;   /* the game's own, or not unlocked */
        g = p >= 4 ? place_guest(p) : -1;
        if (p < 4 && p != 3) continue;
        lv = hero_level_of(g);
        { uint32_t aa[2] = { scratch, num }; pp = tt_gcall(0x000CA670u, FE_MGR, 2, aa); }
        { uint32_t aa[2] = { scratch + 0x40, num }; po = tt_gcall(0x000CAA50u, FE_MGR, 2, aa); }
        if (lab_log()) { static unsigned f4; if ((f4++ & 255) < 8) tt_dbg("[LAB]  p %d pp %08X po %08X\n", p, pp, po); }
        if (pp < 0x10000u || po < 0x10000u) continue;
        v[0] = GF(pp) + GF(po); v[1] = GF(pp + 4) + GF(po + 4); v[2] = GF(pp + 8) + GF(po + 8); v[3] = 1.0f;
        mat_row(v, view, t); mat_row(t, proj, c);
        if (c[3] <= 0.001f) continue;
        x = (c[0] / c[3] + GF(0x18AC38u)) * GF(0x173E5Cu) - GF(0x1705F8u);
        y = GF(0x173E58u) - (c[1] / c[3]) * GF(0x173E58u) + GF(0x173E54u);
        al = s_lab_alpha[p] * GF(0x173E60u);
        if (lab_log()) { static unsigned fr; if ((fr++ & 255) < 12) tt_dbg("[LAB] place %d num %u g %d lv %d pos %.2f %.2f %.2f clip %.2f %.2f %.2f -> %.0f %.0f alpha %.2f\n", p, num, g, lv, v[0], v[1], v[2], c[0], c[1], c[3], x, y, al); }
        a8 = al < 0 ? 0 : al > 255 ? 255 : (uint32_t)al;
        for (q = 0; q < 2; q++) {   /* the word, then the number */
            e = s_lab_el + (uint32_t)(2 * k + q) * 0xDCu;
            { uint32_t i2; for (i2 = 0; i2 < 0xDCu; i2 += 4) G32(e + i2) = G32((q ? B0 : A0) + i2); }
            if (q && p == tt_select_unavail) continue;   /* (co-op: player 1's, while player 2 chooses) */
            if (!q && p == tt_select_unavail && unav_text()) G32(e + 0x34) = s_unav_mem;
            if (q) { uint32_t aa[2] = { e + 0x38, (uint32_t)lv }; tt_gcall(0x000C91A0u, 0, 2, aa); }
            G32(e + 8) = (uint32_t)(int32_t)lroundf(x);
            G32(e + 0xC) = (uint32_t)(int32_t)lroundf(y) + (q ? 0x14u : 0u);
            G32(e + 0x20) = a8 << 24 | 0xFFFFFFu;
            G8(e + 0x30) = a8 > 0xC8u;
            if (a8 > 0xC8u) G32(e + 0xC0) = a8 << 24 | 0x808080u;
            tt_gcall(0x000C8940u, e, 0, NULL);
        }
        k++;
    }
}
static void layout(uint32_t menu)
{
    int n = places(), i;
    uint32_t scratch;
    float center[3], step, pi2 = 6.2831853f, front, slot;
    int mode = (int)G32(menu + 0x20), dir = G8(menu + 0x210);
    /* scratch room for the calls' out-parameters: well below the stack */
    scratch = (tt_guest_esp() - 0x3000u) & ~0xFu;
    if (s_h4_pending) { s_h4_pending = 0; if (!getenv("TT_FE_H4EARLY")) hero4_actor(s_guest); }
    if (s_cursor < 0 || s_cursor >= n || G32(menu + 0x18) != (uint32_t)(s_cursor < 4 ? s_cursor : 3)) {
        s_cursor = (int)G32(menu + 0x18);
        if (s_cursor == 3 && s_guest >= 0) s_cursor = guest_place(s_guest);
    }
    /* the ring's centre: the screen point (320, 340) at depth 17 */
    {
        uint32_t a[4] = { 0x140u, 0x154u, scratch, fbits(GF(0x173DBCu)) };
        tt_gcall(0x000CAEF0u, FE_TEXT, 4, a);
        center[0] = GF(scratch); center[1] = GF(scratch + 4); center[2] = GF(scratch + 8);
    }
    step = GF(0x4B8B98u);
    if (!(step <= 100.0f)) step = 100.0f;
    step *= 0.0035999f;
    front = 180.0f * 0.0174533f;
    slot = pi2 / n;
    for (i = 0; i < n; i++) {
        float tgt = target_deg(i, s_cursor, n) * 0.0174533f, a, alpha, d;
        while (tgt >= pi2) tgt -= pi2;
        if (!s_ang_ok || mode != 2) s_ang[i] = tgt;
        else if (s_ang[i] != tgt) {
            /* turn the way the cursor went, and stop on the target */
            float c = s_ang[i], dist = dir ? c - tgt : tgt - c;
            while (dist < 0) dist += pi2;
            while (dist >= pi2) dist -= pi2;
            if (dist <= step) {
                s_ang[i] = tgt;
                if (i == s_cursor) {   /* arrived in front: the "chosen" pose (and voice) */
                    if (i < 4) tt_gcall(0x00073830u, menu, 0, NULL);
                    else if (s_skins[place_guest(i)].map) { uint32_t aa[4] = { aid(i), 0x25u, 0, 0x64u }; tt_gcall(0x000CA960u, FE_MGR, 4, aa); }   /* (no sound: only when chosen) */
                }
            } else {
                c += dir ? -step : step;
                while (c < 0) c += pi2;
                while (c >= pi2) c -= pi2;
                s_ang[i] = c;
            }
        }
        a = s_ang[i];
        /* on a circle of radius 3 round the centre, in the ground plane */
        {
            /* Lurtz is a head taller than the others: a step further back,
             * so that his name above him stays clear */
            static const float back[N_SEL] = { 0, 0, 0, 1.8f, 0, 2.6f, 1.6f }, down[N_SEL] = { 0, 0, 0, 0.5f, 0, 0.8f, 0.4f };   /* the tall ones further back */
            /* eight round a ring made for four: a wider ring (its front
             * where the four's front is, the ring's centre moved back), so
             * they stand further apart */
            static float wide = -1;
            float rad;
            if (wide < 0) wide = getenv("TT_FE_RING") ? (float)atof(getenv("TT_FE_RING")) : 2.6f;   /* (testing) */
            rad = 3.0f + wide - (i >= 4 ? back[place_guest(i)] : 0.0f);
            GF(scratch + 0x20) = center[0] - rad * sinf(a);
            GF(scratch + 0x24) = center[1] + wide + rad * cosf(a);
            GF(scratch + 0x28) = center[2] - (i >= 4 ? down[place_guest(i)] : 0.0f);
        }
        { uint32_t aa[2] = { aid(i), scratch + 0x20 }; tt_gcall(0x000CAA10u, FE_MGR, 2, aa); }
        /* full colour in front, faded elsewhere */
        d = fabsf(a - front);
        if (d > pi2 / 2) d = pi2 - d;
        if (mode != 2 || !s_ang_ok) alpha = i == s_cursor ? 1.0f : 0.35f;
        else alpha = d >= slot ? 0.35f : 0.35f + (1.0f - 0.35f) * (slot - d) / slot;
        if (place_locked(i)) alpha = lock_alpha();   /* locked: a faint shadow */
        { uint32_t aa[2] = { aid(i), fbits(alpha) }; tt_gcall(0x000CAAD0u, FE_MGR, 2, aa); }
        s_lab_alpha[i] = alpha;
    }
    s_ang_ok = 1;
    /* Aragorn's and Gimli's idles, as the game keeps them going */
    {
        static const uint32_t idle[2][2] = { { 0, 0x22 }, { 2, 0xA0 } };
        int k;
        for (k = 0; k < 2; k++) {
            uint32_t a1[1] = { idle[k][0] };
            if (tt_gcall(0x000CA9A0u, FE_MGR, 1, a1) & 0xFF) {
                if (tt_gcall(0x000CA9E0u, FE_MGR, 1, a1) != idle[k][1]) {
                    uint32_t aa[4] = { idle[k][0], idle[k][1], 1, 0 };
                    tt_gcall(0x000CA960u, FE_MGR, 4, aa);
                }
            }
        }
        /* Isildur (1.7): while a guest held his number (the select opened on
         * the guest last played) the game did not keep him going, and he
         * could stand in no motion at all (T-posed). His own idle, as the
         * game plays it, kept going (0x22 until it is known) */
        if (n > 3 && !place_locked(3)) {
            static uint32_t isi_idle;
            uint32_t a1[1] = { aid(3) }, cur = tt_gcall(0x000CA9E0u, FE_MGR, 1, a1);
            int done = (int)(tt_gcall(0x000CA9A0u, FE_MGR, 1, a1) & 0xFF);
            if (aid(3) == 3 && cur != 0xFFFFFFFFu && cur != 0x25u && cur != 0x28u && !isi_idle) isi_idle = cur;
            if (cur == 0xFFFFFFFFu || (cur != 0x22u && cur != isi_idle && (aid(3) != 3 || (done && (cur == 0x25u || cur == 0x28u))))) {
                uint32_t aa[4] = { aid(3), isi_idle ? isi_idle : 0x22u, 1, 0 };
                tt_gcall(0x000CA960u, FE_MGR, 4, aa);
                if (getenv("TT_FELOG")) tt_dbg("[FE] Isildur (id %u) restarted: was %08X\n", aid(3), cur);
            }
            if (getenv("TT_FELOG")) { static unsigned fr3; if ((++fr3 & 255) < 4) tt_dbg("[FE] Isildur id %u anim %08X done %d idle %08X\n", aid(3), cur, done, isi_idle); }
        }
        /* the guests: Aragorn's stance (0x22) looping, after the chosen pose (0x25), once any other move is done --
         * and from the start: they stood in the set's first motion, frozen, until chosen */
        for (k = 4; k < n; k++) {
            uint32_t a1[1] = { aid(k) };
            uint32_t cur = tt_gcall(0x000CA9E0u, FE_MGR, 1, a1);
            /* back to the stance when nothing plays or a move (the chosen
             * pose 0x25, the game's confirm stab 0x28) has finished */
            if (getenv("TT_FELOG")) {   /* testing */
                static unsigned fr; if ((++fr & 255) < 8) tt_dbg("[FE] place %d id %u anim %08X done %u\n", k, aid(k), cur, tt_gcall(0x000CA9A0u, FE_MGR, 1, a1) & 0xFF);
            }
            if (cur == 0xFFFFFFFFu || (cur != 0x22u && cur != 0x25u && cur != 0x28u) || (cur != 0x22u && (tt_gcall(0x000CA9A0u, FE_MGR, 1, a1) & 0xFF))) {
                uint32_t aa[4] = { aid(k), 0x22u, 1, 0 };
                tt_gcall(0x000CA960u, FE_MGR, 4, aa);
                if (getenv("TT_FELOG")) tt_dbg("[FE] place %d restarted\n", k);
            }
        }
    }
}

/* ===================================================================== */
/* hooks                                                                  */
/* ===================================================================== */
static int s_last_kind = -1;
static int s_in_wop;   /* inside the weapon script op */
static int s_wop_swap; /* ... for the player's slot that the guest's own weapon takes */
static int s_wop_tex;  /* textures swapped so far in this op */
static uint32_t s_wop_mesh, s_wop_wtex;   /* ... to these */
static int s_wop_rec;
static int s_wop_player;   /* 1 the player's bow given now, 2 his sword */
static int s_wop_extra;
static uint32_t s_wop_ins, s_wop_ctx;
/* the script context's actor record the weapon op is for: [ctx+0x178]
 * records of 0xE4 bytes from ctx+0x188, [+4] the actor; record 0 is the
 * player's hero */
static int wop_record(uint32_t ctx)
{
    uint32_t ins = G32(ctx + 0x30), n = G32(ctx + 0x178), i;
    for (i = 0; i < n && i < 256; i++)
        if (G32(ctx + 0x188 + i * 0xE4u + 4) == G32(ins + 4)) return (int)i;
    return -1;
}

/* (1.9) Is the weapon op's character the player or a cutscene's copy of
 * him -- his model (the guest's, under the player's keys) in its record or
 * its object? Others (a companion, an archer) share the hero's sword model
 * 13F82514 in some cutscenes and must keep it. 1 his, 0 not, -1 unknown. */
static int wop_is_him(void)
{
    uint32_t ctx = s_wop_ctx, e, act, q, k1, k2;
    int i = ctx ? wop_record(ctx) : -1;
    if (i < 0 || s_guest < 0) return -1;
    k1 = PLAYER_KEY(s_skins[s_guest].mesh); k2 = s_skins[s_guest].mesh;
    e = ctx + 0x188 + (uint32_t)i * 0xE4u;
    for (q = 0; q < 0xE4u; q += 4) if (G32(e + q) == k1 || G32(e + q) == k2) return 1;
    act = G32(e + 8);
    if (act < 0x80000000u || act >= 0x84000000u - 0x800u) return -1;
    for (q = 0; q < 0x800u; q += 4) {
        uint32_t v = G32(act + q);
        if (v == k1 || v == k2) return 1;
        if (v >= 0x80000000u && v < 0x84000000u - 8u && (G32(v + 4) == k1 || G32(v + 4) == k2)) return 1;
    }
    return 0;
}

static DWORD WINAPI aw_thread(LPVOID u);
void tt_heroes_init(void)
{
    { int q; for (q = 0; q < 256; q++) s_idmap[q] = (uint8_t)q; }
    { int q; for (q = 0; q < N_EXTRA; q++) if (q >= (int)(sizeof s_pools / sizeof s_pools[0]) || strcmp(s_pools[q].name, s_skins[q].name)) tt_dbg("[HEROES] *** s_pools[%d] is not %s's: the tables are out of order ***\n", q, s_skins[q].name); }   /* (1.9.9: per-skin tables by index -- a reorder gave Saruman the troll's moves) */
    {   /* (1.9.2) the files' copies are made in two steps: ours, then the
         * language's (lang.c), which owns xbox_path_redirect */
        extern const char *(*tt_heroes_redirect)(const char *xp);
        extern void tt_lang_init(void);
        tt_heroes_redirect = redirect;   /* (always: co-op resets per level and may build player 2's level variant; with the mod off only level files are looked at) */
        tt_lang_init();
    }
    if (getenv("TT_ANIMWATCH") || getenv("TT_WOBJWATCH") || getenv("TT_POSLOG") || getenv("TT_WOBJSHOW") || getenv("TT_HPFIND") || getenv("TT_HPFREEZE") || getenv("TT_ANIMCYCLE") || getenv("TT_CYCSLOTS")) CreateThread(NULL, 0, aw_thread, NULL, 0, NULL);
    if (getenv("TT_GUEST")) s_guest = skin_by_name(getenv("TT_GUEST"));   /* testing: hero 4 is this one */
    if (getenv("TT_VS_P1SKIN")) s_vsp1 = skin_by_name(getenv("TT_VS_P1SKIN"));   /* testing: Versus on Mount Doom, player 1 a retail hero */
    if (getenv("TT_FIGDUMP")) {   /* testing: the upgrade figures as heroes/fig_<name>.ppm */
        int g, k; paths();
        for (g = 0; g < N_SEL; g++) {
            uint32_t *im = figure_render(g); char pth[700]; FILE *pf;
            if (!im) continue;
            snprintf(pth, sizeof pth, "%s/heroes/fig_%s.ppm", s_dir, s_skins[g].name);
            if ((pf = fopen(pth, "wb")) != NULL) { fprintf(pf, "P6 128 256 255\n"); for (k = 0; k < 128 * 256; k++) { unsigned char c3[3] = { (unsigned char)(im[k] >> 16), (unsigned char)(im[k] >> 8), (unsigned char)im[k] }; fwrite(c3, 1, 3, pf); } fclose(pf); }
            free(im);
        }
    }
    if (getenv("TT_FIGSCAN")) {   /* testing: each guest's figure in each of these slots, heroes/figscan_<name>_<slot>.ppm */
        int g, k; paths();
        const char *q = getenv("TT_FIGSCAN");
        while (*q) {
            int sl = atoi(q); char sv[16];
            snprintf(sv, sizeof sv, "%d", sl);
            for (g = 0; g < N_SEL; g++) {
                uint32_t *im; char pth[700]; FILE *pf;
                _putenv_s("TT_FIGSLOT", sv);
                im = figure_render(g);
                if (!im) continue;
                snprintf(pth, sizeof pth, "%s/heroes/figscan_%s_%d.ppm", s_dir, s_skins[g].name, sl);
                if ((pf = fopen(pth, "wb")) != NULL) { fprintf(pf, "P6 128 256 255\n"); for (k = 0; k < 128 * 256; k++) { unsigned char c3[3] = { (unsigned char)(im[k] >> 16), (unsigned char)(im[k] >> 8), (unsigned char)im[k] }; fwrite(c3, 1, 3, pf); } fclose(pf); }
                free(im);
            }
            q = strchr(q, ','); if (!q) break; q++;
        }
        _putenv_s("TT_FIGSLOT", "");
    }
    if (getenv("TT_GTEX")) s_skins[1].tex = s_skins[0].tex;
    if (getenv("TT_SWAPMESH")) {   /* testing: Boromir's slot with Gandalf's model (or texture with =2) */
        if (atoi(getenv("TT_SWAPMESH")) == 2) s_skins[0].tex = s_skins[1].tex;
        else { s_skins[0].mesh = s_skins[1].mesh; s_skins[0].map = s_skins[1].map; s_skins[0].nmap = s_skins[1].nmap; }
    }   /* testing: Gandalf in Boromir's texture */
    if (getenv("TT_UPG_SELFTEST")) {   /* testing: build the four upgrade pictures now */
        int g, keep = s_guest;
        for (g = 0; g < N_SEL; g++) { s_guest = g; upg_redirect("\\Device\\CdRom0\\UI\\upgbigi.BFF", "upgbigi.BFF"); upg_redirect("\\Device\\CdRom0\\UI\\endlvl.BFF", "endlvl.BFF"); upg_redirect("\\Device\\CdRom0\\UI\\upgradei.BFF", "upgradei.BFF"); }
        s_guest = keep;
    }
}

/* testing: TT_CALLTRACE=level -- each function reached through an indirect
 * call, the first time, with ms since that level's file was opened */
static uint32_t s_ct_t0;
static unsigned char *s_ct_seen;
static FILE *s_ct_f;
static void tt_ct_arm(const char *xp)
{
    const char *e = getenv("TT_CALLTRACE");
    if (e && !s_ct_t0 && strstr(xp, e)) s_ct_t0 = GetTickCount() | 1;
}
void tt_calltrace(uint32_t va)
{
    if (!s_ct_t0 || va >= 0x200000) return;
    if (!s_ct_seen) { s_ct_seen = (unsigned char *)calloc(0x200000, 1); if (!s_ct_seen) { s_ct_t0 = 0; return; } }
    if (s_ct_seen[va]) return;
    s_ct_seen[va] = 1;
    if (!s_ct_f) s_ct_f = fopen("calltrace.txt", "w");
    if (s_ct_f) { fprintf(s_ct_f, "%u %06X\n", (unsigned)(GetTickCount() - s_ct_t0), va); fflush(s_ct_f); }
}


/* testing: TT_ANIMWATCH -- which animation slots the player's moves play.
 * Finds the hero's animation set in memory (by its size), the words that
 * point at its records, and logs their changes as slot numbers. */
#define AW_MAXW 64
static uint32_t s_aw_set, s_aw_ns, s_aw_watch[AW_MAXW], s_aw_last[AW_MAXW];
static int s_aw_n = -1;
static int aw_slot(uint32_t v)
{
    uint32_t i, io = G32(s_aw_set + 0x10);
    for (i = 0; i < s_aw_ns; i++) { uint32_t r = G32(s_aw_set + io + 4 * i); if (r && s_aw_set + r == v) return (int)i; }
    return -1;
}
static void anim_watch(void)
{
    static int fr;
    uint32_t va, k;
    (void)fr;
    if (!s_aw_set) {
        uint32_t want = !strcmp(getenv("TT_ANIMWATCH"), "1") ? s_comp_size : (uint32_t)strtoul(getenv("TT_ANIMWATCH"), NULL, 16);   /* (1: the set composed this run) */
        if (!want) return;
        for (va = 0x80000000u; va < 0x83FFFF00u; va += 0x10)
            if (G32(va) == 0x7A696E61u && G32(va + 0xC) == want) { s_aw_set = va; s_aw_ns = G16(va + 6); break; }
        if (!s_aw_set) return;
        tt_dbg("[AW] set at %08X, %u slots\n", s_aw_set, s_aw_ns);
    }
    if (getenv("TT_AWRESCAN") && s_aw_n >= 0 && ++fr % 150 == 0) {   /* testing: holders made later (the pause screen's figure) */
        uint32_t lo = s_aw_set, hi = s_aw_set + G32(s_aw_set + 0xC);
        for (va = 0x80000000u; va < 0x83FFFFFCu && s_aw_n < AW_MAXW; va += 4) {
            uint32_t v = G32(va); int j, seen = 0;
            if (!((v == lo || (v > lo && v < hi && G32(v) == 0x6D696E61u)) && (va < lo || va >= hi))) continue;
            for (j = 0; j < s_aw_n; j++) if (s_aw_watch[j] == va) seen = 1;
            if (!seen) { s_aw_watch[s_aw_n++] = va; tt_dbg("[AW] new %08X holds %08X (slot %d; +64 %u)\n", va, v, aw_slot(v), G32(va + 64)); }
        }
    }
    if (s_aw_n < 0 && ++fr > 200) {
        uint32_t lo = s_aw_set, hi = s_aw_set + G32(s_aw_set + 0xC);
        s_aw_n = 0;
        for (va = 0x80000000u; va < 0x83FFFFFCu && s_aw_n < AW_MAXW; va += 4) {
            uint32_t v = G32(va);
            if ((v == lo || (v > lo && v < hi && G32(v) == 0x6D696E61u)) && (va < lo || va >= hi)) {
                s_aw_watch[s_aw_n++] = va;
                tt_dbg("[AW] %08X holds %08X (slot %d; +64 %u)\n", va, v, aw_slot(v), G32(va + 64));
            }
        }
        { static uint32_t snap[AW_MAXW][48]; (void)snap; }
    }
    {
        static uint32_t snap[AW_MAXW][48]; static int have;
        for (k = 0; k < (uint32_t)(s_aw_n > 0 ? s_aw_n : 0); k++) {
            int j;
            for (j = 0; j < 48; j++) {
                uint32_t a2 = s_aw_watch[k] - 0x60 + 4 * j, v = G32(a2);
                if (have && 4 * j - 0x60 == 64 && v != snap[k][j] && (v < 260 || (v > s_aw_set && v < s_aw_set + G32(s_aw_set + 0xC))))
                    tt_dbg("[AW] t%u %08X+%d: %u -> %u (slot %d)\n", GetTickCount() / 100 % 100000, s_aw_watch[k], 4 * j - 0x60, snap[k][j], v, aw_slot(v));
                snap[k][j] = v;
            }
        }
        if (s_aw_n > 0) have = 1;
    }
}
static void anim_cycle(void)
{
    static uint32_t set, k, last;
    uint32_t va;
    if (!s_ncyc) return;
    if (!set) {
        for (va = 0x80000000u; va < 0x83FFFF00u; va += 0x10)
            if (G32(va) == 0x7A696E61u && G32(va + 0xC) == s_comp_size) { set = va; break; }
        if (!set) return;
        last = GetTickCount();
        tt_dbg("[CYC] set at %08X\n", set);
    }
    if (GetTickCount() - last < (getenv("TT_CYCMS") ? (DWORD)atoi(getenv("TT_CYCMS")) : 5000u)) return;
    last = GetTickCount();
    {
        uint32_t io = G32(set + 0x10), q;
        static const int idles[] = { 0, 1, 3, 4, 5, 33, 34, 37, 38, 39 };
        for (q = 0; q < sizeof idles / sizeof idles[0]; q++) G32(set + io + 4 * idles[q]) = s_cyc[k % s_ncyc][2];
        { FILETIME ft; ULARGE_INTEGER u; GetSystemTimeAsFileTime(&ft); u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
          tt_dbg("[CYC] unix %llu showing his slot %u\n", (unsigned long long)(u.QuadPart / 10000000ull - 11644473600ull), s_cyc[k % s_ncyc][0]); }
        k++;
    }
}
/* testing: TT_WOBJWATCH -- the player's weapon objects' words that change */
static uint32_t s_sword_res, s_shooter;   /* the guest's sword-hand model as the game holds it */
static int s_in_launch;
static uint32_t s_lk_key, s_lk_ty, s_bow_res, s_beam_obj[8], s_beam_n, s_launching; static uint32_t s_beam_p2obj[8], s_beam_p2n; static uint32_t s_beam_p2obj_log[8];   /* (co-op) player 2's beam shots */   /* the last resource lookup; the guest's bow-hand model */
static uint32_t s_wobj[4];   /* the player's bow and sword objects, his actor */
/* BEC90 -> C7610: arg12 (+0x30) es dano entero; pasa a evento+0x14
 * y sub_000849F0 lo convierte en el dano pendiente actor+0x3F0.
 * El ajuste se hace despues del contacto, una vez por evento, nunca en
 * la DB compartida ni durante las sondas geometricas BA110. */
/* (132) Sin s_wobj: en 131 no se capturaba para el troll y el doble dano
 * nunca se aplico (0 lineas TROLL-POWER en partida). Basta con que el
 * atacante sea el actor del jugador (0x43B15C) y el arma la suya (id 1). */
/* (133) El tipo de golpe (C7610 +0x34 -> evento +0x18) lo lee 849F0:
 * los escudos (tipos 0xB/0x5E, 86BE0/86CA0) solo paran tipos que no sean
 * 3, 4, 0xA o 0xD. El 0xA es el golpe pesado nativo: lanza por el aire a
 * los ligeros (ficha de tipo +0x164 < 2) y derriba a los pesados. Si el
 * tipo de la victima no tiene ficha (6CAB0 = 0), 849F0 saldria sin
 * reaccion con el 0xA: entonces 0xD, derribo sin lanzar. */
static uint32_t troll_type_record(uint32_t type)
{
    uint32_t base=G32(0x424A14u),hdr=G32(0x424A18u);
    if(type==0xFFFFFFFFu)return 0;
    if(base<0x80000000u||base>=0x83FFF000u||hdr<0x80000000u||hdr>=0x83FFF000u)return 0;
    int32_t n=(int32_t)G32(hdr+0xC);
    if(n<=0||n>2048||base+0x178u*(uint32_t)n>=0x83FFF000u)return 0;
    for(int32_t i=0;i<n;i++)if(G32(base+0x178u*(uint32_t)i+0x20)==type)return base+0x178u*(uint32_t)i;
    return 0;
}
/* (136) El troll de la tumba de Balin (tipo 71, el de la maza 6) no pierde
 * vida con el 0xA: en la 135 recibio 24 golpes "aplicados" sin bajar. Con
 * el tipo nativo del ataque si. A el no se le cambia el tipo. */
#define TROLL_BOSS_TYPE 71u
static uint32_t troll_hit_kind(uint32_t victim_type,uint32_t native){
    if(victim_type==TROLL_BOSS_TYPE)return native;
    return troll_type_record(victim_type)?0xAu:0xDu;
}
static int troll_hit_apply(uint32_t sp)
{
    static int seen;
    if(s_guest!=9||G8(HERO_BYTE)!=4||G8(0x43BABCu))return 0;
    if(sp<0x10000u||sp>=0x83FFFFC0u)return 0;
    uint32_t weapon=G32(sp+8),owner=G32(sp+12),victim=G32(sp+4);
    if(G32(sp)!=0xBECFFu||weapon<0x80000000u||weapon>=0x83FFF000u
       ||owner<0x80000000u||owner>=0x83FFF000u
       ||owner!=G32(0x43B15Cu)||victim==owner||victim<0x80000000u
       ||victim>=0x83FFF000u||G32(weapon+0x2F4)!=1){
        /* diagnostico: los primeros rechazos de cada partida */
        if(seen<12&&G32(sp)==0xBECFFu){seen++;tt_dbg("[TROLL-POWER] sin aplicar: arma %08X id %u, atacante %08X, jugador %08X, victima %08X\n",
            weapon,weapon>=0x80000000u&&weapon<0x83FFF000u?G32(weapon+0x2F4):0u,owner,G32(0x43B15Cu),victim);}
        return 0;
    }
    uint32_t damage=G32(sp+0x30);
    if(!damage||damage>0x3FFFFFFFu)return 0;
    G32(sp+0x30)=2*damage;
    uint32_t kind=G32(sp+0x34);
    if(!getenv("TT_TROLLKIND0"))G32(sp+0x34)=troll_hit_kind(G32(victim+0x2F0),kind);
    tt_dbg("[TROLL-POWER] melee %08X -> %08X: %u -> %u (antes de defensa), tipo %u -> %u\n",owner,victim,damage,2*damage,kind,G32(sp+0x34));
    return 1;
}
/* (136) el objeto de ataque de BE0D0/BA110 es una variable local del juego:
 * vive en la pila (memoria baja), no en el monton como actores y armas */
#define TT_ATTACK_OK(a) ((a)>=0x10000u&&(a)<0x83FFF000u)
#include "heroes_troll_log.h"
int tt_troll_hit(uint32_t sp)
{
    int applied=troll_hit_apply(sp);
    if(sp>=0x10000u&&sp<0x83FFFFC0u&&G32(sp)==0xBECFFu)clog_event(sp,applied);
    else if(sp>=0x10000u&&sp<0x83FFFFC0u)clog_impact(sp);   /* (137) rocas y demas danos */
    return applied;
}
static void wobj_watch(void)
{
    static uint32_t snap[4][0x200]; static int have[4];
    int k, j;
    if (getenv("TT_WOBJSHOW") && s_wobj[0]) {   /* testing: flag word +0x110 bits off */
        uint32_t m = (uint32_t)strtoul(getenv("TT_WOBJSHOW"), NULL, 16);
        G32(s_wobj[0] + 0x110) &= ~m;
    }
    if (getenv("TT_POSLOG") && s_wobj[2]) {   /* testing: the player actor's place, twice a second */
        static DWORD last; uint32_t a = s_wobj[2];
        if (GetTickCount() - last > 500) { last = GetTickCount(); tt_dbg("[POS] %.2f %.2f %.2f\n", GF(a + 0x60 + 0x30), GF(a + 0x60 + 0x34), GF(a + 0x60 + 0x38)); }
    }
    if (!getenv("TT_WOBJWATCH") || !strcmp(getenv("TT_WOBJWATCH"), "0")) return;
    for (k = 0; k < 2; k++) {
        uint32_t o = s_wobj[k];
        if (!o) continue;
        for (j = 0; j < 0x200; j++) {
            uint32_t v = G32(o + 4 * j);
            if (have[k] && v != snap[k][j])
                tt_dbg("[WOBJ] t%u w%d %08X+%X: %08X -> %08X\n", GetTickCount() / 10 % 1000000, k, o, 4 * j, snap[k][j], v);
            snap[k][j] = v;
        }
        have[k] = 1;
    }
}
/* testing: TT_HPFIND=<value> -- words holding the hero's starting health
 * (float or int) at 40 s, which of them changed by 80 s; TT_HPFREEZE=<addr>
 * keeps a float there at its value */
static void hp_find(void)
{
    static uint32_t cand[4096]; static int nc = -1, done;
    static DWORD t0;
    float want = (float)atof(getenv("TT_HPFIND"));
    uint32_t va;
    if (!t0) t0 = GetTickCount();
    if (nc < 0 && GetTickCount() - t0 > 40000) {
        nc = 0;
        for (va = 0x80000000u; va < 0x83FFFFFCu && nc < 4096; va += 4)
            if (GF(va) == want || G32(va) == (uint32_t)want) cand[nc++] = va;
        tt_dbg("[HP] %d candidates\n", nc);
    }
    if (nc > 0 && !done && GetTickCount() - t0 > 90000) {
        int i;
        done = 1;
        for (i = 0; i < nc; i++) if (GF(cand[i]) != want && G32(cand[i]) != (uint32_t)want)
            tt_dbg("[HP] %08X now %.2f / %u\n", cand[i], GF(cand[i]), G32(cand[i]));
    }
}
static DWORD WINAPI aw_thread(LPVOID u) { (void)u; for (;;) { Sleep(50);
        if (getenv("TT_HPFIND")) hp_find();
        if (getenv("TT_HPFREEZE")) { static float keep; static int have; uint32_t a = (uint32_t)strtoul(getenv("TT_HPFREEZE"), NULL, 16); if (G32(a)) { if (!have) { keep = GF(a); have = 1; } if (GF(a) < keep) GF(a) = keep; } } if (getenv("TT_ANIMWATCH")) anim_watch(); if (getenv("TT_WOBJWATCH") || getenv("TT_WOBJSHOW") || getenv("TT_POSLOG")) wobj_watch(); anim_cycle(); } return 0; }

static void wdb_dump(void)
{
    if (G32(0x424AA0u) && G32(0x424A9Cu)) {   /* testing: the weapon table */
        static int done; uint32_t n = G32(G32(0x424AA0u) + 0xC), b = G32(0x424A9Cu), i, q;
        if (!done && n && n < 200) {
            done = 1;
            for (i = 0; i < n; i++) {
                uint32_t r = b + i * 0xB0u; char nm[33]; int k;
                for (k = 0; k < 32 && G8(r + k) >= 32 && G8(r + k) < 127; k++) nm[k] = (char)G8(r + k);
                nm[k] = 0;
                tt_dbg("[WDB] %2u %-24s id %u dmg %u ext %.2f..%.2f samp %u trail", i, nm, G32(r + 0x20), G32(r + 0x24), GF(r + 0x44), GF(r + 0x48), G32(r + 0x4C));
                for (q = 0x5C; q < 0x9C; q += 4) tt_dbg(" %u", G32(r + q));
                tt_dbg("\n");
            }
        }
    }
}
/* testing: TT_PARTLOG -- particle spawns (kind 0: id, pos) and LightGlowEffect calls (1) */
static uint32_t s_rock_obj[8]; static int s_rock_burst[8], s_self_burst;   /* (his rocks in flight: below) */
static unsigned s_burst_n;   /* the fire pots' bursts (testing: TT_SHOTLOG) */
void tt_partlog(int kind, uint32_t a, uint32_t b, uint32_t c)
{
    if (kind == 0 && a == 0x73u && G32(tt_guest_esp()) == 0x000D76ADu && !s_self_burst && b >= 0x10000u && b < 0x84000000u) {   /* a fire pot's burst (sub_000D7530): one of his? */
        int i;
        s_burst_n++;
        {   /* (the nearest still unburst: where it lies, or the one it struck, within a few units) */
            int best = -1; float bd = 100.0f;
            for (i = 0; i < 8; i++) if (s_rock_obj[i] && !s_rock_burst[i]) {
                float dx = GF(s_rock_obj[i] + 0x408) - GF(b), dy = GF(s_rock_obj[i] + 0x40C) - GF(b + 4), dz = GF(s_rock_obj[i] + 0x410) - GF(b + 8), d = dx * dx + dy * dy + dz * dz;
                if (d < bd) { bd = d; best = i; }
            }
            if (best >= 0) s_rock_burst[best] = 1;
        }
    }
    static int on = -1, wd = -1; static unsigned n[1024], g;
    if (on < 0) { on = getenv("TT_PARTLOG") ? 1 : 0; wd = getenv("TT_WDBDUMP") ? 1 : 0; }
    if (wd) wdb_dump();
    if (kind == 3 && getenv("TT_FEWLOG")) { tt_dbg("[FEW] CA540 id %u mesh %08X (%08X %08X %08X %08X) tex %08X (%08X %08X %08X %08X %08X)\n", a, b, G32(b), G32(b + 4), G32(b + 8), G32(b + 12), c, G32(c), G32(c + 4), G32(c + 8), G32(c + 12), G32(c + 16)); return; }
    if (kind == 3) return;
    if (!on) return;
    if (kind == 1) { if (g++ < 50) tt_dbg("[GLOW] args %08X %08X %08X\n", a, b, c); return; }
    if (kind == 2) { if (g++ < 50) tt_dbg("[SHIMMER] args %08X %08X %08X\n", a, b, c); return; }
    { static DWORD last; if (GetTickCount() - last > 5000) { int i; last = GetTickCount(); for (i = 0; i < 1024; i++) if (n[i] > 20) tt_dbg("[PARTN] id %X n %u\n", i, n[i]); } }
    if (a < 1024 && n[a]++ < 3) tt_dbg("[PART] id %X from %08X pos %.1f %.1f %.1f\n", a, G32(tt_guest_esp()), b >= 0x10000u && b < 0x84000000u ? GF(b) : 0, b >= 0x10000u && b < 0x84000000u ? GF(b + 4) : 0, b >= 0x10000u && b < 0x84000000u ? GF(b + 8) : 0);
    (void)c;
}
/* testing: TT_SWEEPLOG -- script callbacks counted (22 OnKillingBlow, 24 OnBreakShield) */
void tt_cbev(uint32_t obj, uint32_t ev)
{
    static unsigned n[64];
    static int on = -1;
    (void)obj;
    if (on < 0) on = getenv("TT_SWEEPLOG") ? 1 : 0;
    if (!on || ev >= 64) return;
    n[ev]++;
    if (ev == 22 || ev == 24) tt_dbg("[CB] ev %u count %u\n", ev, n[ev]);
    if (getenv("TT_CBRARE") && n[ev] <= 6) {
        uint32_t pl = G32(0x43B15Cu);
        tt_dbg("[CBR] t %u ev %u obj %08X (#%u)", (unsigned)GetTickCount(), ev, obj, n[ev]);
        if (obj >= 0x80000000u && obj < 0x83FFF000u) tt_dbg(" at %.1f %.1f %.1f / %.1f %.1f %.1f", GF(obj + 0xF0), GF(obj + 0xF4), GF(obj + 0xF8), GF(obj + 0x90), GF(obj + 0x94), GF(obj + 0x98));
        if (pl >= 0x80000000u) tt_dbg(" player %.1f %.1f", GF(pl + 0xF0), GF(pl + 0xF4));
        tt_dbg("\n");
    }
    { static DWORD last; if (GetTickCount() - last > 5000) { char b[512]; int i, l = 0; last = GetTickCount(); for (i = 0; i < 32; i++) l += snprintf(b + l, sizeof b - l, "%u ", n[i]); tt_dbg("[CBH] %s\n", b); } }
}
/* testing: TT_SWEEPLOG -- the melee sweeps' results per weapon id (hits) */
/* testing: TT_HEADLOG -- at each sweep of the player's sword: its owner's
 * heading (a z-up 4x4 matrix found in an object the weapon points to, near
 * the weapon), the victim's bearing and the blade's direction, relative to
 * that heading */
static int find_zup(uint32_t obj, const float *nrp, float lim)
{
    uint32_t o;
    if (obj < 0x80000000u || obj >= 0x83FFF000u) return -1;
    for (o = 0; o < 0x800; o += 4) {
        float m[16]; int i, ok = 1;
        for (i = 0; i < 16; i++) { m[i] = GF(obj + o + 4u * (uint32_t)i); if (!(m[i] > -1e5f && m[i] < 1e5f)) ok = 0; }
        if (!ok) continue;
        if (fabsf(m[2]) > 0.02f || fabsf(m[6]) > 0.02f || fabsf(m[10] - 1) > 0.02f || fabsf(m[8]) > 0.02f || fabsf(m[9]) > 0.02f) continue;
        if (fabsf(m[0] * m[0] + m[1] * m[1] - 1) > 0.03f || fabsf(m[4] * m[4] + m[5] * m[5] - 1) > 0.03f || fabsf(m[0] * m[4] + m[1] * m[5]) > 0.03f) continue;
        if (fabsf(m[0]) > 0.9999f && fabsf(m[12]) < 1e-3f) continue;
        if (nrp && (fabsf(m[12] - nrp[0]) > lim || fabsf(m[13] - nrp[1]) > lim)) continue;
        return (int)o;
    }
    return -1;
}
static uint32_t s_hl_w, s_hl_own; static int s_hl_mo;
/* testing: TT_HEADLOG -- every HUD frame, once the sweep found them: the
 * sword's hand (the weapon's place) and tip in its owner's frame (x along
 * the owner's X row, y along its Y row) */
/* testing: TT_SLOTLOG -- the anim slots the sword's owner is playing: the
 * actor (or an object one pointer away) holding a pointer into his "aniz"
 * set at one of its slots' records; logged as they change */
static void slot_log(uint32_t own)
{
    /* the object holding the pointer to his "aniz" set (the actor, or one
     * pointer away): its small-number words (< 256) logged as they change */
    static uint32_t obj, prev[256]; static int tried;
    uint32_t o, p, k;
    char buf[1024]; int n = 0;
    if (!tried) {
        tried = 1;
        for (o = 0; o < 0x2000 && !obj; o += 4) {
            p = G32(own + o);
            if (p >= 0x80000000u && p < 0x83FF0000u && G32(p) == 0x7A696E61u) { obj = own; tt_dbg("[SLOT] set at actor+%X\n", o); }
            else if (p >= 0x80000000u && p < 0x83FF0000u) {
                uint32_t o2;
                for (o2 = 0; o2 < 0x400 && !obj; o2 += 4) { uint32_t q = G32(p + o2); if (q >= 0x80000000u && q < 0x83FF0000u && G32(q) == 0x7A696E61u) { obj = p; tt_dbg("[SLOT] set at actor+%X -> +%X (obj %08X)\n", o, o2, p); } }
            }
        }
    }
    if (!obj) return;
    for (k = 0; k < 256; k++) {
        uint32_t v = G32(obj + 4 * k);
        if (v != prev[k] && v < 256 && prev[k] < 256 && n < 1000) n += snprintf(buf + n, sizeof buf - n, "+%X:%u>%u ", 4 * k, prev[k], v);
        prev[k] = v;
    }
    if (n) tt_dbg("[SLOT] t %u %s\n", (unsigned)GetTickCount(), buf);
}
/* a projectile's entry in the game's projectile table (built from the
 * level's projectile database at load): 16 words, {id, id, then floats 32,
 * 3, 100, 10, 5, 2, 2.2, then flight, 30, trail, impact, sparks, effect, 0}. Found once per
 * level (the place checked each time) */
static uint32_t s_prec_va, s_rock_rec, s_fire_page, s_rock_obj[8], s_rock_start;
/* (1.7) the ranged weapon's last upgrade (the "Gondor Bow" in the hero's
 * list: Lava Rocks, White Fire, Doom Fire): upgrade 0xF of the player's
 * block (0x4AC600 + 0x50 * hero, the guest's in the fourth hero's) -- the
 * game's own arrow-glow colours (sub_0009B4D0) test 0xF, then 0xE, then
 * 0xD, the three ranged upgrades. TT_RANGEDTOP=0/1 (testing) */
static int ranged_top(void)
{
    if (getenv("TT_RANGEDTOP")) return atoi(getenv("TT_RANGEDTOP")) != 0;
    return G8(0x4AC600u + 3u * 0x50u + 0xFu) != 0;
}
#define POT_REC 0x100u   /* (his fire-pot rock's entry, in the same page after the fire rock's) */
static uint32_t s_rock_pot;
static int s_rock_trail;   /* (his burning rocks carry the fire pots' trail: no effects of ours) */
static int s_rock_has_trail[8];
static unsigned s_urk_throws;
static float s_rock_start_save[3];
/* (137) El troll no tiene clip de lanzamiento: su mano vacia queda donde la
 * deje el clip que suene, a menudo muy por encima de los orcos (la maza
 * llega a +21) o a un lado. Sin objetivo fijado, el juego apunta desde la
 * salida (salida + direccion * alcance), y la roca volaba por encima de
 * todos. Sale delante de su cuerpo, a lo sumo a TROLL_ROCK_Z sobre sus pies
 * (los orcos ocupan de +0 a +4). TT_TROLLROCKHAND: la salida de la 132. */
#define TROLL_ROCK_Z 3.0f
static void troll_rock_start(uint32_t st, float ux, float uy, float ahead)
{
    uint32_t a = G32(0x43B15Cu);
    float top = getenv("TT_TROLLROCKZ") ? (float)atof(getenv("TT_TROLLROCKZ")) : TROLL_ROCK_Z;
    if (a < 0x80000000u || a >= 0x83FFF000u) { GF(st) += ux * ahead; GF(st + 4) += uy * ahead; return; }
    GF(st) = GF(a + 0x90) + ux * ahead; GF(st + 4) = GF(a + 0x94) + uy * ahead;
    if (!(GF(st + 8) <= GF(a + 0x98) + top)) GF(st + 8) = GF(a + 0x98) + top;
}
static int s_rock_age[8], s_rock_pid[8], s_rock_burst[8];
static int s_memdump_arm;
static int prec_at(uint32_t va, uint32_t id)
{
    return G32(va) == id && G32(va + 4) == id && GF(va + 8) == 32.0f && GF(va + 12) == 3.0f && GF(va + 16) == 100.0f && GF(va + 20) == 10.0f;
}
static uint32_t proj_record(uint32_t pid)
{
    uint32_t va, base = s_prec_va;
    if (!(base >= 0x80000000u && base < 0x84000000u && prec_at(base, 0) && prec_at(base + 0x40, 1))) {
        base = 0;
        for (va = 0x80000000u; va < 0x84000000u - 0x1000; va += 4)
            if (prec_at(va, 0) && prec_at(va + 0x40, 1) && G32(va + 0x7C) == 0u) { base = va; break; }
        s_prec_va = base;
        if (getenv("TT_SHOTLOG")) {
            uint32_t q, j;
            tt_dbg("[SHOT] projectile table at %08X\n", base);
            for (q = 0; base && q < 24 && G32(base + 0x40u * q) == q; q++) {
                char ln[300]; int l = 0;
                for (j = 0; j < 16; j++) l += snprintf(ln + l, sizeof ln - l, " %08X", G32(base + 0x40u * q + 4u * j));
                tt_dbg("[SHOT]  proj %2u:%s\n", q, ln);
            }
        }
    }
    if (!base) return 0;
    va = base + 0x40u * pid;
    return G32(va) == pid && G32(va + 4) == pid ? va : 0;
}
/* his rocks in flight (the object's position at +0x408, the last one at
 * +0x418): embers (particle 16) each frame from the last place to this,
 * and a flame (4) on the rock. TT_ROCKPART / TT_ROCKFLAME other particles,
 * -1 none (testing) */
/* (1.8.2) the game keeps at most 64 particle effects at a time
 * (0x0008E470: the count at 0x43AD1C, 0x40 the most) and, when full, ends
 * the one with the least time left to make room -- the effects of ours (a
 * beam's line of glows, the embers behind a rock) ended the archers' fire
 * pots' flames. Ours take only what is free above a reserve. */
#define FX_MAX 64
#define FX_RESERVE 20
static int fx_free(void)
{
    extern int tt_fx_total;   /* (1.8.2: the game's 64 and the ones made after them) */
    int n = (int)G32(0x43AD1Cu), t = tt_fx_total > FX_MAX ? tt_fx_total : FX_MAX;
    if (n < 0 || n > t) return 0;
    return t - FX_RESERVE - n;
}
static void fire_rock_frame(void)
{
    static int pid = -2;
    int i;
    static int fpid = -2;
    if (pid == -2) pid = getenv("TT_ROCKPART") ? (int)strtol(getenv("TT_ROCKPART"), NULL, 0) : 16;
    if (fpid == -2) fpid = getenv("TT_ROCKFLAME") ? (int)strtol(getenv("TT_ROCKFLAME"), NULL, 0) : 4;
    for (i = 0; i < 8; i++) {
        uint32_t o = s_rock_obj[i], scratch;
        float a[3], b[3];
        int c, k, q, n;
        if (!o) continue;
        if (!s_fire_page || (G32(o + 0x3DC) != s_fire_page && G32(o + 0x3DC) != s_fire_page + POT_REC) || G32(o + 0x3D0) != (G32(o + 0x3DC) == s_fire_page ? 10u : 4u) || G8(o + 0x3E4) || ++s_rock_age[i] > 150) {
            /* a fire-pot rock the game put down without bursting it (its
             * landing on the ground often ends it as a plain rock would):
             * burst as the pot does, where it lies */
            if (G32(o + 0x3DC) == s_fire_page + POT_REC && G32(o + 0x3D0) == 4u && G8(o + 0x3E4) && !s_rock_burst[i] && s_rock_age[i] <= 150 && !getenv("TT_NOPOTBURST")) {
                uint32_t args[2] = { 0, 0 };
                s_rock_burst[i] = 1; s_self_burst = 1;
                tt_gcall(0x000D7530u, o, 2, args);
                s_self_burst = 0;
                if (getenv("TT_SHOTLOG")) tt_dbg("[SHOT] fire pot rock %08X burst by us\n", o);
            }
            if (getenv("TT_SHOTLOG")) tt_dbg("[ROCKEND] bursts so far %u, %08X age %d id %u entry %08X dead %u at %.1f %.1f %.1f\n", s_burst_n, o, s_rock_age[i], G32(o + 0x3D0), G32(o + 0x3DC), G8(o + 0x3E4), GF(o + 0x408), GF(o + 0x40C), GF(o + 0x410));
            s_rock_obj[i] = 0; continue;
        }
        for (c = 0; c < 3; c++) { a[c] = GF(o + 0x418 + 4 * c); b[c] = GF(o + 0x408 + 4 * c); }
        if (getenv("TT_PSURVEY")) for (c = 0; c < 3; c++) { a[c] = b[c] = GF(o + 0x3E8 + 4 * c); }   /* testing: where it left */
        if (getenv("TT_ROCKLOG") && s_rock_age[i] < 40) tt_dbg("[ROCK] %d age %d at %.1f %.1f %.1f\n", i, s_rock_age[i], b[0], b[1], b[2]);
        if (s_rock_has_trail[i] || (s_rock_pid[i] != -2 ? s_rock_pid[i] : pid) < 0 || s_rock_age[i] < 1) continue;
        scratch = (tt_guest_esp() - 0x3000u) & ~0xFu;
        for (q = 0; q < 16; q++) GF(scratch + 0x40 + 4 * q) = (q % 5) ? 0.0f : 1.0f;   /* an identity matrix */
        n = 2;
        if (fx_free() < n + 1) continue;   /* (no room above the reserve: none this frame) */
        for (k = 1; k <= n + (fpid >= 0 ? 1 : 0); k++) {   /* embers along the way, then the flame on the rock */
            uint32_t args[4];
            float f = k > n ? 2.0f : (float)k / (float)n;   /* (the flame a frame ahead: it shows the frame after, with the rock) */
            for (c = 0; c < 3; c++) GF(scratch + 4 * c) = a[c] + (b[c] - a[c]) * f;
            GF(scratch + 12) = 1.0f;
            args[0] = (uint32_t)(k > n ? fpid : s_rock_pid[i] != -2 ? s_rock_pid[i] : pid); args[1] = scratch; args[2] = scratch + 0x40; args[3] = fbits(-1.0f);
            tt_gcall(0x0008E470u, 0, 4, args);
        }
    }
}
static int s_shotdump_left, s_shotdump_n;   /* (testing: TT_SHOTDUMP=prefix) */
/* Sauron's bolt red (1.8): his shots are Gandalf's magic bolt (projectile
 * 15), whose glow and his beam are particle effect 0x135 (light blue). In
 * the effects' table ([0x32AF90]: {id, its emitters}) each emitter holds
 * its two colours (ABGR: measured, a colour with red and blue swapped drew the
 * bolt deep blue) just after the words 000C0BB8 00034C70 and a
 * float; for Sauron they are turned red, once per level (a red colour no
 * longer matches, so it is done once whatever the table's address) */
/* (1.7) the colour as asked: 0 the effect's own (light blue), 1 red
 * (Sauron's), 2 white (Gandalf's with his last ranged upgrade). Each colour's
 * own value is kept (and found again when a level brings the table back)
 * so the colours can change back and forth -- Sauron's red and blue shot
 * by shot */
#define EC_MAX 32
static struct { uint32_t va, orig, mine; } s_ec[EC_MAX];
static int s_ec_n;
static void effect_colour(uint32_t id, int mode)
{
    uint32_t db = G32(0x32AF90u), t, n, i, k, q;
    if (db < 0x10000u || db >= 0x84000000u) return;
    t = db + G32(db + 0x1C); n = G32(db + 0xC);
    if (n > 4096) return;
    for (i = 0; i < n; i++) if (G32(t + 8 * i) == id) {
        uint32_t arr = G32(t + 8 * i + 4);
        if (arr < 0x80000000u || arr >= 0x84000000u) return;
        for (k = 0; k < 8; k++) {
            uint32_t e = G32(arr + 4 * k);
            if (e < 0x80000000u || e >= 0x84000000u - 0x200) continue;
            for (q = 0; q < 0x180; q += 4) if (G32(e + q) == 0x000C0BB8u && G32(e + q + 4) == 0x00034C70u) {
                int c, j;
                for (c = 0; c < 2; c++) {
                    uint32_t va = e + q + 12 + 4 * (uint32_t)c, v = G32(va), o, a, b, g, r, m, out;
                    for (j = 0; j < s_ec_n && s_ec[j].va != va; j++) {}
                    if (j == s_ec_n) { if (s_ec_n == EC_MAX) return; s_ec_n++; s_ec[j].va = va; s_ec[j].orig = v; s_ec[j].mine = v; }
                    if (v != s_ec[j].mine) s_ec[j].orig = v;   /* (the level's own again) */
                    o = s_ec[j].orig;
                    a = o >> 24; b = (o >> 16) & 255; g = (o >> 8) & 255; r = o & 255;   /* (ABGR: measured) */
                    m = r > g ? (r > b ? r : b) : (g > b ? g : b);
                    if (mode == 1) out = getenv("TT_BOLTRGB") ? (uint32_t)strtoul(getenv("TT_BOLTRGB"), NULL, 16) : (a << 24 | (r / 8) << 16 | (g / 8) << 8 | m);
                    else if (mode == 2) { uint32_t w = m + (255 - m) / 2; out = getenv("TT_WHITERGB") ? (uint32_t)strtoul(getenv("TT_WHITERGB"), NULL, 16) : (a << 24 | w << 16 | w << 8 | w); }
                    else if (mode == 3) out = a << 24 | (m / 10) << 16 | m << 8 | (m * 3 / 5);         /* (1.9.9, Saruman) green-yellow (ABGR) */
                    else if (mode == 4) out = a << 24 | (m / 10) << 16 | (m * 9 / 10) << 8 | m;       /* (1.9.9, Saruman) yellow */
                    else out = o;
                    if (out != v) { G32(va) = out; if (log_on()) tt_dbg("[HEROES] effect %X emitter %u: colour %08X -> %08X\n", id, k, v, out); }
                    s_ec[j].mine = out;
                }
                break;
            }
        }
        return;
    }
}
/* the bolt's colour for the guest now: Sauron red (with his last ranged
 * upgrade red and blue in turn, s_bolt_blue), Gandalf white with his */
static int s_bolt_blue;
static int bolt_mode(void)
{
    if (s_guest < 0) return 0;
    if (!strcmp(s_skins[s_guest].name, "sauron")) return getenv("TT_NOREDBOLT") ? 0 : s_bolt_blue ? 0 : 1;
    if (!strcmp(s_skins[s_guest].name, "saruman")) return s_bolt_blue ? 4 : 3;   /* (1.9.9) green-yellow, yellow */
    if (!strcmp(s_skins[s_guest].name, "gandalf")) return ranged_top() && !getenv("TT_NOWHITEBOLT") ? 2 : 0;
    return -1;
}
/* ---- a new hero who falls through the ground is put back (1.7.5) ----
 * Sauron fell through the floor at the Gates of Moria (at a high frame
 * rate, 1.7.3 and 1.7.4) and dropped into the void below the level, alive,
 * for good (not seen here at 5-16 fps under Wine, scripted runs as
 * Sauron there with fights, walks and every upgrade). Whatever lets him
 * through, he is stood again where he stood: his place (the player,
 * 0x43B15C: +0xF0, and his matrix's +0x90) every 100 ms, kept once he has
 * stood half a second; a steady drop -- three samples in a row faster than
 * a walk down a slope, 20 below where he stood -- puts him back on the
 * highest place he stood in the five seconds up to a second before it
 * began (three if he fell again soon). A jump of his place in one sample
 * (a scene putting him elsewhere) is not a fall. Only for the new heroes,
 * only in play. TT_NOCATCH turns it off; TT_SINK makes a fall-through
 * (tested: 14 under the ground at the Gates of Moria's start, he dropped
 * and was stood back on the path). */
int tt_heroes_guest(void);
static void fall_catch_frame(void)
{
    /* (1.7.8: a fall in the void at full speed drops more than 15 in 100 ms
     * within a second -- 1.7.5 took that for a scene moving him and forgot
     * where he stood, and never caught him; and at the level's start he may
     * drop before he has stood anywhere. Now only a jump sideways or up is a
     * scene's, and the place he came into play is kept to go back to.) */
    static uint32_t pl; static DWORD last, fall_t0, last_catch;
    static float hx[64], hy[64], hz[64]; static DWORD ht[64]; static int hn, nfall;
    static float pz, px, py, sx, sy, sz; static int steady, have_spawn, said;
    static int off = -1;
    uint32_t p, scr = G32(0x4AD050u);
    DWORD now = GetTickCount();
    float x, y, z;
    int i, k;
    if (off < 0) off = getenv("TT_NOCATCH") != NULL;
    if (off || tt_heroes_guest() < 0 || (scr != 6 && scr != 8)) { pl = 0; return; }
    p = G32(0x43B15Cu);
    if (p < 0x80000000u || p >= 0x84000000u) { pl = 0; return; }
    if (getenv("TT_SINK")) {   /* testing: "frame,count[,depth]" -- he sinks depth (3) a frame for count frames from that frame of play (a fall-through, made) */
        static int sf; int f0 = 0, cnt = 0; float dz = 3.0f;
        sf++;
        if (sscanf(getenv("TT_SINK"), "%d,%d,%f", &f0, &cnt, &dz) >= 2 && sf >= f0 && sf < f0 + cnt) { GF(p + 0xF8) -= dz; GF(p + 0x98) -= dz; }
    }
    if (now - last < 100) return;
    last = now;
    x = GF(p + 0xF0); y = GF(p + 0xF4); z = GF(p + 0xF8);
    if (!(z > -1e5f && z < 1e5f) || !(x > -1e5f && x < 1e5f) || !(y > -1e5f && y < 1e5f)) return;
    if (p != pl) {   /* into play: where he starts is a place to go back to */
        pl = p; hn = 0; nfall = 0; steady = 0; pz = z; px = x; py = y;
        sx = x; sy = y; sz = z; have_spawn = 1; said = 0;
        tt_dbg("[HEROES] in play at %.1f %.1f %.1f\n", x, y, z);
        return;
    }
    {
        float dxy = sqrtf((x - px) * (x - px) + (y - py) * (y - py));
        if (dxy > 30.0f || z - pz > 15.0f) {   /* put elsewhere at once (a scene): start again from there */
            hn = 0; nfall = 0; steady = 0; sx = x; sy = y; sz = z; have_spawn = 1;
            tt_dbg("[HEROES] put at %.1f %.1f %.1f\n", x, y, z);
        }
        else if (z - pz > -1.2f) nfall = 0;                                /* standing, walking (down a slope too) */
        else if (nfall++ == 0) fall_t0 = now;                              /* dropping */
    }
    pz = z; px = x; py = y;
    if (!nfall) {   /* where he stands -- kept once he has stood a third of a second (sunk a little, he can hover a moment before he drops) */
        if (++steady >= 3) { k = hn % 64; hx[k] = x; hy[k] = y; hz[k] = z; ht[k] = now; hn++; }
        said = 0;
        return;
    }
    steady = 0;
    if (nfall < 3) return;
    {
        float top = -1e9f;   /* the place he stood just before (or came into play at) */
        for (i = 0; i < hn && i < 64; i++) if (hz[i] > top && now - ht[i] < 6000) top = hz[i];
        if (top < -1e8f && have_spawn) top = sz;
        if (!said) { said = 1; tt_dbg("[HEROES] dropping at %.1f %.1f %.1f (stood at %.1f)\n", x, y, z, top); }
        if (top < -1e8f || z > top - 20.0f) return;
    }
    {   /* the highest place he stood in the five seconds up to a second before the drop began (three if he fell again soon) */
        DWORD back = (now - last_catch < 6000) ? 3000 : 1000;
        int best = -1, n2 = hn < 64 ? hn : 64, bi = -1;
        float tx, ty, tz;
        for (i = 0; i < n2; i++) {
            k = (hn - 1 - i) % 64;
            if (ht[k] + back > fall_t0) continue;
            if (ht[k] + back + 5000 < fall_t0) break;
            if (best < 0 || hz[k] > hz[best]) { best = k; bi = i; }
        }
        if (best < 0 && n2 > 0) { bi = n2 - 1; best = (hn - 1 - bi) % 64; }   /* the oldest kept */
        static DWORD catches[3];   /* (the last three catches' times) */
        if (best >= 0) { tx = hx[best]; ty = hy[best]; tz = hz[best]; }
        else if (have_spawn) { tx = sx; ty = sy; tz = sz; bi = 0; }   /* never stood: where he came into play */
        else return;
        catches[0] = catches[1]; catches[1] = catches[2]; catches[2] = now;
        if (have_spawn && catches[0] && now - catches[0] < 20000) {
            /* (1.9) a third fall in 20 s: the place he stood is no good (he
             * drops through it again) -- back to where he came into play */
            tx = sx; ty = sy; tz = sz; best = -1;
            catches[0] = catches[1] = catches[2] = 0;
            tt_dbg("[HEROES] fell through again and again: back to where he came into play\n");
        }
        GF(p + 0xF0) = tx; GF(p + 0xF4) = ty; GF(p + 0xF8) = tz + 0.5f;
        GF(p + 0x90) = tx; GF(p + 0x94) = ty; GF(p + 0x98) = tz + 0.5f;
        tt_dbg("[HEROES] fell through the ground at %.1f %.1f %.1f: put back at %.1f %.1f %.1f\n", x, y, z, tx, ty, tz);
        last_catch = now; nfall = 0; steady = 0; pz = tz + 0.5f; px = tx; py = ty; said = 0;
        if (best >= 0) hn -= bi;   /* the places after it forgotten, the ones before kept */
        else hn = 0;
    }
}
static uint32_t s_lad[16]; static int s_nlad, s_lad_fr;   /* (testing: TT_LADFIND) */
/* testing: TT_ANIMPROBE="from,frames" -- the player object's words
 * (0x43B15C, 0x2000 bytes) that change, each frame from that HUD frame */
static void anim_probe(void)
{
    static int from = -2, cnt, fr;
    static uint32_t prev[0x800], obj;
    uint32_t p = G32(0x43B15Cu), q;
    if (from == -2) { const char *e = getenv("TT_ANIMPROBE"); from = -1; if (e) sscanf(e, "%d,%d", &from, &cnt); }
    if (from < 0 || p < 0x80000000u || p >= 0x84000000u - 0x2000u) return;
    ++fr;
    if (p != obj) { obj = p; for (q = 0; q < 0x800; q++) prev[q] = G32(p + 4 * q); return; }
    {   /* (only while he moves: from = frames to skip once moving, cnt = frames logged) */
        static float lx, ly; static int moving;
        float x = GF(p + 0xF0), y = GF(p + 0xF4), d = (x - lx) * (x - lx) + (y - ly) * (y - ly);
        lx = x; ly = y;
        if (d > 0.0004f && d < 4.0f) moving++;
        if (moving <= from || moving > from + cnt) { for (q = 0; q < 0x800; q++) prev[q] = G32(p + 4 * q); return; }
    }
    {
        char buf[4096]; int n = 0;
        n += snprintf(buf + n, sizeof buf - n, "[PROBE] f %d t %u:", fr, (unsigned)GetTickCount());
        for (q = 0; q < 0x800 && n < 4000; q++) {
            uint32_t v = G32(p + 4 * q);
            if (v != prev[q]) { float f; memcpy(&f, &v, 4); n += snprintf(buf + n, sizeof buf - n, " %X=%08X(%g)", 4 * q, v, f); }
            prev[q] = v;
        }
        tt_dbg("%s\n", buf);
    }
}
/* Sauron's sprint (1.9): the left stick's click (Shift on the keys, both
 * rebindable) makes him run faster. It does not touch his animation or
 * what he is doing (the old sprint swapped his run for another record, and
 * a press made him skip and jump before he ran): each frame the way the
 * game moved him is lengthened, easing in and out, only while he runs on
 * the ground unblocked under the player's stick -- a wall, an attack, a
 * scene or a stop and he is the game's own again. sprint= toggle (a press
 * on, a press off; it also ends when he stops), hold (while held), off. */
extern volatile int tt_l3_acc, tt_push_acc, tt_in_polls;
static void sprint_frame(void)
{
    static uint32_t pl; static float lx, ly, lz, k = 1.0f, wd, wspeed, ref;
    static int on, was_l3, still, mode = -1, fr;
    static DWORD stop_t, w0;
    const float SPRINT = 1.6f;
    uint32_t p = G32(0x43B15Cu);
    float x, y, z, dx, dy, d, want;
    static int l3_last, push_last;
    int l3, push, lunge;
    DWORD now = GetTickCount();
    if (tt_in_polls) {   /* (what the pads said since the last frame; none read: as before) */
        l3_last = tt_l3_acc; push_last = tt_push_acc;
        tt_l3_acc = 0; tt_push_acc = 0; tt_in_polls = 0;
    }
    l3 = l3_last; push = push_last;
    fr++;
    if ((fr & 31) == 1 || mode < 0) {
        const char *e = getenv("TT_SPRINT");
        mode = !e || !*e || !_stricmp(e, "toggle") || !strcmp(e, "1") ? 1 : !_stricmp(e, "hold") ? 2 : 0;
    }
    if (!mode || s_guest < 0 || s_guest >= N_EXTRA || strcmp(s_skins[s_guest].name, "sauron")
        || G8(HERO_BYTE) != 4 || G32(0x4AD050u) != 6 || p < 0x80000000u || p >= 0x84000000u - 0x1000u) {
        static int nlog;
        if (l3 && !was_l3 && G32(0x4AD050u) == 6 && nlog++ < 10)
            tt_dbg("[SPRINT] button, not sprinting: mode %d, guest %d (%s), hero %d\n", mode, s_guest,
                   s_guest >= 0 && s_guest < N_EXTRA ? s_skins[s_guest].name : "-", G8(HERO_BYTE));
        pl = 0; on = 0; k = 1.0f; was_l3 = l3; return;
    }
    x = GF(p + 0xF0); y = GF(p + 0xF4); z = GF(p + 0xF8);
    if (!(x > -1e5f && x < 1e5f && y > -1e5f && y < 1e5f && z > -1e5f && z < 1e5f)) { pl = 0; return; }
    if (p != pl) { pl = p; lx = x; ly = y; lz = z; on = 0; k = 1.0f; was_l3 = l3; wd = 0; w0 = now; wspeed = 0; return; }
    if (getenv("TT_POSPUSH")) {   /* testing: his place pushed along x this much each frame (do the writes hold?) */
        float px = (float)atof(getenv("TT_POSPUSH"));
        GF(p + 0xF0) += px; GF(p + 0x90) += px;
        if ((fr % 30) == 0) tt_dbg("[POSPUSH] %.2f %.2f %.2f\n", GF(p + 0xF0), GF(p + 0xF4), GF(p + 0xF8));
        return;
    }
    /* the button: toggle (ends when he stops a moment) or hold */
    if (l3 && !was_l3) {   /* (in the log: is the button seen, and what holds him back) */
        static int nlog;
        if (nlog++ < 40) tt_dbg("[SPRINT] button (%s): %s; stick %d, speed %.2f (his running %.2f)\n", mode == 2 ? "hold" : "toggle",
                                mode == 2 ? "held" : on ? "off" : "on", push, wspeed, ref);
    }
    if (mode == 2) on = l3;
    else if (l3 && !was_l3) on = !on;
    was_l3 = l3;
    if (push < 8000) { if (!still++) stop_t = now; if (mode == 1 && on && now - stop_t > 400) on = 0; }
    else still = 0;
    /* the game's step this frame, and his speed over a tenth of a second */
    dx = x - lx; dy = y - ly; d = sqrtf(dx * dx + dy * dy);
    lunge = d > 3.0f || fabsf(z - lz) > 1.5f + d || (ref > 0 && d > ref * 0.16f);   /* a lunge, a knock, a scene's jump */
    if (!lunge) wd += d;
    if (now - w0 >= 100) {
        wspeed = wd * 1000.0f / (float)(now - w0); wd = 0; w0 = now;
        if (push >= 20000 && wspeed > 2.0f && wspeed < 30.0f) ref = wspeed > ref ? wspeed : ref * 0.97f + wspeed * 0.03f;   /* his running speed */
    }
    /* sprinting while he runs unblocked under the stick (a wall, a stop: the game's speed again) */
    /* (1.9.1: no longer "at his usual running speed" -- that speed, once
     * overestimated, kept the sprint off; up against a wall his own step is
     * next to nothing anyway, and the extra is in proportion to it) */
    want = on && !lunge && push >= 20000 && (wspeed > 1.0f || getenv("TT_SPRINTNOGATE")) ? SPRINT : 1.0f;
    k += (want - k) * (want > k ? 0.08f : 0.2f);   /* easing in, quicker out */
    if (want == 1.0f && k < 1.01f) k = 1.0f;
    if (k > 1.0f && !lunge && d > 0.0001f) {   /* (in proportion to the game's own step: up against a wall it is nothing) */
        float ex = dx * (k - 1.0f), ey = dy * (k - 1.0f);
        x += ex; y += ey;
        GF(p + 0xF0) = x; GF(p + 0xF4) = y;
        GF(p + 0x90) += ex; GF(p + 0x94) += ey;
    }
    if (getenv("TT_SPRINTLOG") && (fr % 8) == 0)   /* testing */
        tt_dbg("[SPRINT] t %u on %d l3 %d push %d d %.3f speed %.2f ref %.2f k %.2f\n", (unsigned)now, on, l3, push, d, wspeed, ref, k);
    lx = x; ly = y; lz = z;
}
/* (1.8.2) mouse steering (menus.c reads the mouse and turns the move keys):
 * the mouse's left / right turns the hero where he stands, as aiming the
 * ranged attack does, and the move keys then go by the way he faces -- W
 * straight ahead of him, S back, A / D to his sides. The game moves him by
 * the stick against the camera: his facing (+0xE8, and +0x158 the facing he
 * turns to) comes out as the camera's way less the stick's angle clockwise
 * from up, so the camera's way is learned from him as he walks, and the keys
 * are turned by (camera - facing) for the game. */
volatile float tt_steer_turn, tt_steer_rot;
volatile int tt_steer_active;
volatile unsigned long tt_steer_move_t;
volatile float tt_steer_sx, tt_steer_sy;
extern int tt_in_scene(void);
static int s_steady;
static float wrap_pi(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }
static void steer_frame(void)
{
    static int have, cam_ok, was_moving;
    static float yaw, cam;
    uint32_t p = G32(0x43B15Cu);
    float turn, now_yaw;
    int moving;
    static int off;
    if (!tt_steer_active || p < 0x80000000u || p >= 0x84000000u - 0x200u || tt_in_scene()) {
        if (++off > 15) { have = 0; tt_steer_rot = 0; }   /* (a moment's lapse keeps the way he was going) */
        tt_steer_turn = 0;
        return;
    }
    off = 0;
    now_yaw = GF(p + 0xE8);
    if (!(now_yaw > -100.0f && now_yaw < 100.0f)) return;
    turn = tt_steer_turn; tt_steer_turn -= turn;
    if (getenv("TT_STEERTURN")) {   /* testing: "a,b" -- a steady turn to the right from a to b s into play */
        static DWORD t0; float a = 0, b = 0; DWORD t;
        if (!t0) t0 = GetTickCount();
        sscanf(getenv("TT_STEERTURN"), "%f,%f", &a, &b);
        t = GetTickCount() - t0;
        if (t >= a * 1000 && t < b * 1000) turn += 0.05f;
    }
    moving = tt_steer_move_t && (int)(GetTickCount() - tt_steer_move_t) < 120;
    if (!have) { have = 1; yaw = now_yaw; if (!cam_ok) { cam = now_yaw; cam_ok = 1; } }
    if (!moving && !turn && !was_moving) yaw = now_yaw;   /* (the game's own turns: kept) */
    {   /* the camera's way, from the way he faces once he has turned to the
         * stick the game had (only then: while he turns it says nothing) */
        static float last_now, last_th;
        float th = atan2f(tt_steer_sx, tt_steer_sy) + tt_steer_rot;
        if (moving && fabsf(wrap_pi(now_yaw - last_now)) < 0.03f && fabsf(wrap_pi(th - last_th)) < 0.03f) s_steady++; else s_steady = 0;
        if (s_steady >= 4) cam = wrap_pi(cam + 0.5f * wrap_pi(now_yaw + th - cam));
        last_now = now_yaw; last_th = th;
    }
    was_moving = moving;
    if (turn) {
        yaw = wrap_pi(yaw - turn);   /* (the mouse to the right: a turn to his right) */
        if (!moving) {   /* where he stands: turned at once */
            static uint32_t ctl, ctl_for;   /* (his controller's record, 0x46D450 on: the facing it holds him to, +0x2C) */
            if (ctl_for != p) {
                uint32_t a;
                ctl = 0; ctl_for = p;
                for (a = 0x46C000u; a < 0x470000u; a += 4)
                    if (G32(a + 8) == p && fabsf(wrap_pi(GF(a + 0x2C) - now_yaw)) < 0.01f) { ctl = a; break; }
                if (getenv("TT_STEERLOG")) tt_dbg("[STEER] his controller's record %08X\n", ctl);
            }
            if (ctl && G32(ctl + 8) == p) GF(ctl + 0x2C) = yaw;
            GF(p + 0xE8) = yaw; GF(p + 0x158) = yaw;
            GF(p + 0x60) = cosf(yaw); GF(p + 0x64) = sinf(yaw);
            GF(p + 0x70) = -sinf(yaw); GF(p + 0x74) = cosf(yaw);
        }
    }
    tt_steer_rot = wrap_pi(cam - yaw);
    if (getenv("TT_STEERLOG")) { static int n; if (n++ % (moving ? 1 : 10) == 0) {
        tt_dbg("[STEER] yaw %.2f now %.2f cam %.2f rot %.2f moving %d steady %d t158 %.2f th %.2f pp %.2f %.2f\n", yaw, now_yaw, cam, tt_steer_rot, moving, s_steady,
            GF(p + 0x158), atan2f(tt_steer_sx, tt_steer_sy) + tt_steer_rot, GF(p + 0xF0), GF(p + 0xF4)); } }
}
static void yaw_find(void)   /* testing: TT_YAWFIND -- the player's words that follow his facing */
{
    static int fr;
    uint32_t p = G32(0x43B15Cu), q;
    float ax, ay, a;
    char ln[1500]; int l = 0;
    if (!getenv("TT_YAWFIND") || p < 0x80000000u || p >= 0x84000000u - 0x1000u || (++fr % 10)) return;
    ax = GF(p + 0x60); ay = GF(p + 0x64);
    a = atan2f(ay, ax);
    l += snprintf(ln + l, sizeof ln - l, "[YAW] mat %.3f %.3f %.3f %.3f a %.3f:", GF(p + 0x60), GF(p + 0x64), GF(p + 0x70), GF(p + 0x74), a);
    for (q = 0; q < 0x1000 && l < 1400; q += 4) {
        float v = GF(p + q);
        if (!(v > -1000.0f && v < 1000.0f) || q == 0x60 || q == 0x64) continue;
        if (fabsf(v - a) < 0.02f || fabsf(remainderf(v - a, 6.2831853f)) < 0.02f || fabsf(remainderf(v - a - 1.5707963f, 6.2831853f)) < 0.02f
            || fabsf(remainderf(v - a + 1.5707963f, 6.2831853f)) < 0.02f || fabsf(remainderf(v - a * 57.29578f, 360.0f)) < 1.0f)
            l += snprintf(ln + l, sizeof ln - l, " %X=%.3f", q, v);
    }
    tt_dbg("%s\n", ln);
}
void tt_framelog(void)
{
    static int on = -1;
    {   /* Se deshabilita en menus, pausa y escenas. La colision sigue siendo la del heroe. */
        uint32_t p=G32(0x43B15Cu),port=G32(0x1CB8B4u);
        int enabled=s_size_base && s_size_guest==s_guest && tt_heroes_guest()>=0
            &&G32(0x4AD050u)==6 &&p>=0x80000000u &&p<0x83FFF000u &&!tt_in_scene();
        InterlockedExchange(&s_size_port,port<4?(LONG)port:0);
        InterlockedExchange(&s_size_enabled,enabled);
        int dir=(int)InterlockedExchange(&s_size_request,0);
        if(enabled&&dir){
            float next=s_size_scale+(dir>0?0.25f:-0.25f);
            if(next<0.5f)next=0.5f;if(next>1.0f)next=1.0f;
            unsigned char *d=(unsigned char *)(xbox_GetMemoryOffset()+s_size_base);
            if(G32(s_size_base)==9 &&G32(s_size_base+4)==s_size_tab
                &&ts_apply(&s_size_plan,d,s_size_len,s_size_scale,next)){
                s_size_scale=next;tt_dbg("[SIZE] %s: %.0f%% (colision original)\n",s_skins[s_guest].name,100.0f*next);
            }else {s_size_base=0;InterlockedExchange(&s_size_enabled,0);tt_dbg("[SIZE] cambio cancelado: datos modificados\n");}
        }
    }
    yaw_find();
    steer_frame();
    if (getenv("TT_FXTEST") && G32(0x43B15Cu) >= 0x80000000u) {   /* testing: many effects at once round the player */
        static int fr; uint32_t p = G32(0x43B15Cu), sc = (tt_guest_esp() - 0x3000u) & ~0xFu, args[4]; int k, q;
        if (++fr % 60 == 0) {
            for (q = 0; q < 16; q++) GF(sc + 0x40 + 4 * q) = (q % 5) ? 0.0f : 1.0f;
            for (k = 0; k < atoi(getenv("TT_FXTEST")); k++) {
                GF(sc) = GF(p + 0xF0) + (float)(k % 20) - 10.0f; GF(sc + 4) = GF(p + 0xF4) + (float)(k / 20) - 5.0f; GF(sc + 8) = GF(p + 0xF8) + 3.0f; GF(sc + 12) = 1.0f;
                args[0] = 0x135u; args[1] = sc; args[2] = sc + 0x40; args[3] = fbits(-1.0f);
                tt_gcall(0x0008E470u, 0, 4, args);
            }
        }
    }
    if (getenv("TT_LVL_DONE")) {   /* testing: the first N levels finished by every hero, in memory */
        int h, l, n = atoi(getenv("TT_LVL_DONE"));
        for (h = 0; h <= 3; h++) for (l = 0; l < n && l < 15; l++) if (G8(0x4AB68Fu + h * 15 + l) == 0) G8(0x4AB68Fu + h * 15 + l) = 1;
    }
    anim_probe();
    /* (1.9.1) Sauron's sprint removed (sprint_frame kept for reference, not run) */
    (void)sprint_frame;
    fire_rock_frame();
    fall_catch_frame();
    if (getenv("TT_POSLOGF")) {   /* testing: the player's place (0x43B15C +0xF0) every 15 frames */
        static unsigned fr; uint32_t p = G32(0x43B15Cu);
        if (++fr % 15 == 0 && p >= 0x80000000u && p < 0x84000000u) tt_dbg("[POSF] %u %.2f %.2f %.2f\n", fr, GF(p + 0xF0), GF(p + 0xF4), GF(p + 0xF8));
    }
    select_gone();   /* the select gone: the bank's own sounds back */
    {   /* Sauron's bolt red, Gandalf's white: once the level's effects are in, now and then (cheap: one effect) */
        static unsigned fr;
        if (tt_heroes_on() && G8(HERO_BYTE) == 4 && s_guest >= 0 && (++fr & 63) == 1 && bolt_mode() >= 0) effect_colour(0x135u, bolt_mode());
    }
    if (getenv("TT_UPNDUMP")) {   /* testing: hero 4's upgrade strings now against the game's own (how many differ, the first few) */
        static DWORD lt; if (GetTickCount() - lt > 4000u) { int i, k, d = 0; lt = GetTickCount();
            if (s_upn_n <= 0 && !tt_lang_on()) upn_find();
            for (i = 0; i < s_upn_n; i++) { int diff = 0; char t[160];
                for (k = 0; k <= s_upn_len[i]; k++) if (G16(s_upn_at[i] + 2 * k) != s_upn_orig[i][k]) diff = 1;
                if (!diff) continue; d++;
                for (k = 0; k < 150 && G16(s_upn_at[i] + 2 * k); k++) t[k] = (char)G16(s_upn_at[i] + 2 * k); t[k] = 0;
                if (d <= 6) tt_dbg("[UPNDUMP]   now '%s'\n", t); }
            tt_dbg("[UPNDUMP] lang %d, hero byte %u, guest %d, cur %d: %d of %d strings changed\n", tt_lang_on(), G8(HERO_BYTE), s_guest, s_upn_cur, d, s_upn_n); } }
    {   /* the upgrade names: the guest's while he plays (startup.BFF's
         * strings, found once they are in memory) */
        static unsigned fr;
        if (tt_heroes_on() && G8(HERO_BYTE) == 4 && s_guest >= 0 && s_upn_cur != s_guest && (++fr & 31) == 1) upn_set(s_guest);
    }
    if (getenv("TT_PARTDUMP") && G32(0x43B15Cu) >= 0x80000000u) {   /* testing: a particle effect's emitters (the effects' table at [0x32AF90]) */
        static int done;
        uint32_t db = G32(0x32AF90u), id = (uint32_t)strtoul(getenv("TT_PARTDUMP"), NULL, 0), t, n, i, k, q;
        if (!done) tt_dbg("[PART] db %08X\n", db);
        if (!done && db >= 0x10000u && db < 0x84000000u) {
            done = 1;
            t = db + G32(db + 0x1C); n = G32(db + 0xC);
            tt_dbg("[PART] db %08X %u effects\n", db, n);
            for (i = 0; i < n; i++) if (G32(t + 8 * i) == id) {
                uint32_t arr = G32(t + 8 * i + 4);
                tt_dbg("[PART] arr %08X\n", arr);
                { uint32_t e0 = G32(arr); for (q = 0; q < 0x500 && e0; q += 16) tt_dbg("[PARTR] +%03X %08X %08X %08X %08X\n", q, G32(e0 + q), G32(e0 + q + 4), G32(e0 + q + 8), G32(e0 + q + 12)); }
                { uint32_t pr = proj_record(15u); if (pr) for (q = 0; q < 0x40; q += 4) tt_dbg("[PARTP] proj15 +%02X %08X\n", q, G32(pr + q)); }
                for (k = 0; k < 8; k++) {
                    uint32_t e = G32(arr + 4 * k);
                    if (!e) continue;
                    tt_dbg("[PART] emitter %u at %08X\n", k, e);
                    for (q = 0; q < 0x40; q += 4) tt_dbg("[PART]   +%02X %08X %.3f\n", q, G32(e + q), GF(e + q));
                    { uint32_t d = G32(e + 0x10); if (d >= 0x10000u && d < 0x84000000u) for (q = 0; q < 0x200; q += 4) tt_dbg("[PART]     d+%03X %08X %.3f\n", q, G32(d + q), GF(d + q)); }
                }
            }
        }
    }
    if (getenv("TT_LADMAT") && s_nlad) {   /* testing: each ladder's matrix (its name's pointer +72), when it changes */
        static float last[16][16]; static DWORD lt;
        if (GetTickCount() - lt > 300) {
            int i, c, ch;
            lt = GetTickCount();
            for (i = 0; i < s_nlad; i++) {
                float m[16];
                for (c = 0; c < 16; c++) m[c] = GF(s_lad[i] + 72 + 4 * c);
                for (ch = 0, c = 0; c < 16; c++) if (fabsf(m[c] - last[i][c]) > 0.02f) ch = 1;
                if (ch) {
                    uint32_t pl = G32(0x43B15Cu);
                    tt_dbg("[LADM] t %u lad %d  x %.2f %.2f %.2f  y %.2f %.2f %.2f  z %.2f %.2f %.2f  at %.2f %.2f %.2f  player %.1f %.1f\n", (unsigned)lt, i,
                           m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10], m[12], m[13], m[14], pl >= 0x80000000u ? GF(pl + 0xF0) : 0, pl >= 0x80000000u ? GF(pl + 0xF4) : 0);
                    memcpy(last[i], m, sizeof m);
                }
            }
        }
    }
    if (getenv("TT_LADFIND") && s_nlad) {   /* their blocks' changes (around the name's pointer), twice a second */
        static DWORD lt; static uint32_t snap[16][0x200];
        if (GetTickCount() - lt > 500) {
            int i; uint32_t k;
            lt = GetTickCount();
            for (i = 0; i < s_nlad; i++) {
                uint32_t b = s_lad[i] - 0x400u;
                for (k = 0; k < 0x200; k++) {
                    uint32_t v = G32(b + 4 * k);
                    if (snap[i][k] != v && snap[i][0x1FF] == 1u + 0) {}
                    if (snap[i][k] != v && getenv("TT_LADDIFF") && lt > 0) {
                        float f; memcpy(&f, &v, 4);
                        if (s_lad_fr > 1) tt_dbg("[LADD] %d +%03X %08X -> %08X (%.3f)\n", i, 4 * k, snap[i][k], v, f);
                    }
                    snap[i][k] = v;
                }
            }
            s_lad_fr++;
        }
    }
    if (getenv("TT_LADFIND") && G32(0x43B15Cu) >= 0x80000000u) {   /* testing: the siege ladders' names in memory, and what points at them */
        static int fr2;
        if (++fr2 == 90) {
            const unsigned char *M = (const unsigned char *)(xbox_GetMemoryOffset() + 0x80000000u);
            uint32_t o, names[64]; int nn = 0, i;
            for (o = 0; o + 16 < 0x4000000u && nn < 64; o++)
                if (M[o] == 's' && !memcmp(M + o, "siegeLadderObj", 14)) { names[nn++] = 0x80000000u + o; tt_dbg("[LAD] name %08X %.20s\n", 0x80000000u + o, (const char *)M + o); }
            for (o = 0; o + 4 < 0x4000000u; o += 4) {
                uint32_t v; memcpy(&v, M + o, 4);
                if (v < 0x80000000u) continue;
                for (i = 0; i < nn; i++) if (v == names[i]) {
                    tt_dbg("[LAD] ref %08X -> %08X\n", 0x80000000u + o, v);
                    if (s_nlad < 16 && v >= 0x81000000u) s_lad[s_nlad++] = 0x80000000u + o;
                    if (v >= 0x81000000u && getenv("TT_LADDUMP")) {
                        uint32_t q, r0 = 0x80000000u + o;
                        for (q = 0; q < 0x4E0; q += 4) { float f = GF(r0 - 0x300 + q); if (fabsf(f) > 0.5f && fabsf(f) < 400.0f) tt_dbg("[LADF] %08X ref%+d: %.3f\n", r0, (int)q - 0x300, f); }
                    }
                }
            }
        }
    }
    if (getenv("TT_PPOS") && G32(0x43B15Cu) >= 0x80000000u) {   /* testing: the player (0x43B15C)'s place (+0xF0) and matrix (+0x60), twice a second */
        static DWORD lt; uint32_t a = G32(0x43B15Cu);
        if (GetTickCount() - lt > (DWORD)(getenv("TT_PPOSMS") ? atoi(getenv("TT_PPOSMS")) : 500)) { lt = GetTickCount(); tt_dbg("[PPOS] t %u pad %.3f %.3f %.3f %.3f st %u ", (unsigned)lt, GF(0x6B2758u + 0x14), GF(0x6B2758u + 0x18), GF(0x6B2758u + 0x1C), GF(0x6B2758u + 0x20), G32(a + 0x408)); tt_dbg("[PPOS] %08X %.2f %.2f %.2f | %.2f %.2f %.2f | x %.2f %.2f y %.2f %.2f\n", a, GF(a + 0xF0), GF(a + 0xF4), GF(a + 0xF8), GF(a + 0x90), GF(a + 0x94), GF(a + 0x98), GF(a + 0x60), GF(a + 0x64), GF(a + 0x70), GF(a + 0x74)); }
    }
    if (getenv("TT_PTP") && G32(0x43B15Cu) >= 0x80000000u) {   /* testing: "x,y,z,deg,frame" -- the player put there (with +0xF0) */
        static int fr; float x, y, z, hd; int at;
        static DWORD t0; int ms = 0;
        if (!t0) t0 = GetTickCount();
        if (sscanf(getenv("TT_PTP"), "%f,%f,%f,%f,%d,%d", &x, &y, &z, &hd, &at, &ms) == 6) { DWORD el = GetTickCount() - t0; fr = (el >= (DWORD)ms && el < (DWORD)ms + 1200) ? at : -100; }
        if (sscanf(getenv("TT_PTP"), "%f,%f,%f,%f,%d", &x, &y, &z, &hd, &at) == 5 && ++fr >= at && fr < at + 4) {
            uint32_t a = G32(0x43B15Cu); float r = hd * 0.0174533f;
            GF(a + 0xF0) = x; GF(a + 0xF4) = y; GF(a + 0xF8) = z;
            GF(a + 0x90) = x; GF(a + 0x94) = y; GF(a + 0x98) = z;
            GF(a + 0x60) = cosf(r); GF(a + 0x64) = sinf(r); GF(a + 0x70) = -sinf(r); GF(a + 0x74) = cosf(r);
            tt_dbg("[PTP] frame %d put at %.1f %.1f %.1f %.0f\n", fr, x, y, z, hd);
        }
    }
    if (getenv("TT_TPHL") && s_hl_own) {   /* testing: "x,y,z,heading_deg" -- the player put there (once, the first frames the owner is known) */
        static int n; float x, y, z, hd;
        if (n < 3 && sscanf(getenv("TT_TPHL"), "%f,%f,%f,%f", &x, &y, &z, &hd) == 4) {
            uint32_t m0 = s_hl_own + (uint32_t)s_hl_mo; float r = hd * 0.0174533f;
            n++;
            GF(m0 + 48) = x; GF(m0 + 52) = y; GF(m0 + 56) = z;
            GF(m0) = cosf(r); GF(m0 + 4) = sinf(r); GF(m0 + 16) = -sinf(r); GF(m0 + 20) = cosf(r);
            tt_dbg("[TP] owner %08X+%X put at %.1f %.1f %.1f h %.0f\n", s_hl_own, s_hl_mo, x, y, z, hd);
        }
    }
    if (getenv("TT_TELEPORT") && !s_wobj[2]) {   /* testing: no weapon op seen -- the player found by his start place */
        static int fr0; static uint32_t cand;
        float sx, sy, sz;
        if (++fr0 == 20 && getenv("TT_TPSTART") && sscanf(getenv("TT_TPSTART"), "%f,%f,%f", &sx, &sy, &sz) == 3) {
            uint32_t va;
            for (va = 0x80000000u; va < 0x84000000u - 0x100; va += 16)
                if (fabsf(GF(va + 0x30) - sx) < 1.5f && fabsf(GF(va + 0x34) - sy) < 1.5f && fabsf(GF(va + 0x38) - sz) < 1.5f && GF(va + 0x3C) == 1.0f
                    && fabsf(GF(va + 0x28) - 1.0f) < 0.01f && fabsf(GF(va + 0x08)) < 0.01f) {
                    static int ci;
                    tt_dbg("[TP] candidate %d matrix at %08X\n", ci, va);
                    if (ci++ == (getenv("TT_TPIDX") ? atoi(getenv("TT_TPIDX")) : 0)) cand = va - 0x60u;
                }
            if (cand) s_wobj[2] = cand;
        }
    }
    if (getenv("TT_TELEPORT") && s_wobj[2]) {   /* testing: "x,y,z,frame[,heading]" -- the player put there at that HUD frame */
        static int fr; float x, y, z, hd = 99; int at = 0;
        if (sscanf(getenv("TT_TELEPORT"), "%f,%f,%f,%d,%f", &x, &y, &z, &at, &hd) >= 4 && ++fr >= at && fr < at + 3) {
            uint32_t a = s_wobj[2];
            GF(a + 0x90) = x; GF(a + 0x94) = y; GF(a + 0x98) = z;
            if (hd != 99) { GF(a + 0x60) = cosf(hd); GF(a + 0x64) = sinf(hd); GF(a + 0x70) = -sinf(hd); GF(a + 0x74) = cosf(hd); }
            tt_dbg("[TP] frame %d put at %.1f %.1f %.1f\n", fr, x, y, z);
        }
    }
    if (s_shotdump_left > 0) {
        extern void xbox_PresentDumpRequest(const char *path);
        char nm[260];
        if ((s_shotdump_left-- & (getenv("TT_SHOTDUMP1") ? 0 : 1)) == 0) { snprintf(nm, sizeof nm, "%s%04d.bmp", getenv("TT_SHOTDUMP"), s_shotdump_n++); xbox_PresentDumpRequest(nm); }
    }
    float m[16], x, y, z, tx, ty, tz;
    uint32_t w = s_hl_w, own = s_hl_own;
    int i;
    if (on < 0) on = getenv("TT_HEADLOG") ? 1 : 0;
    if (!w || !own) return;
    { static int sl = -1; if (sl < 0) sl = getenv("TT_SLOTLOG") ? 1 : 0; if (sl) slot_log(own); }
    if (!on) return;
    for (i = 0; i < 16; i++) m[i] = GF(own + (uint32_t)s_hl_mo + 4u * (uint32_t)i);
    x = GF(w + 0x60 + 48) - m[12]; y = GF(w + 0x60 + 52) - m[13]; z = GF(w + 0x60 + 56) - m[14];
    tx = x + 4 * GF(w + 0x60 + 16); ty = y + 4 * GF(w + 0x60 + 20); tz = z + 4 * GF(w + 0x60 + 24);
    tt_dbg("[HL] %.2f %.2f %.2f  %.2f %.2f %.2f\n", x * m[0] + y * m[1], x * m[4] + y * m[5], z, tx * m[0] + ty * m[1], tx * m[4] + ty * m[5], tz);
}
void tt_sweep2(uint32_t a0, uint32_t a1, double res)
{
    static int on = -1, wo = -1, mo = -1, vo = -1;
    uint32_t w, own;
    float wt[3], h, bx, by, vx, vy;
    if (on < 0) on = getenv("TT_HEADLOG") || getenv("TT_SLOTLOG") ? 1 : 0;
    if (!on || a0 < 0x10000u || a0 >= 0x84000000u || a1 < 0x80000000u || a1 >= 0x83FFF000u) return;
    w = G32(a0 + 0x14); if (w < 0x80000000u || w >= 0x84000000u || G32(w + 0x2F4) != 1) return;
    wt[0] = GF(w + 0x60 + 48); wt[1] = GF(w + 0x60 + 52); wt[2] = GF(w + 0x60 + 56);
    if (wo < 0 && wo != -2) {
        uint32_t o;
        for (o = 0; o < 0x800 && wo < 0; o += 4) {
            uint32_t p = G32(w + o);
            int m = find_zup(p, wt, 8.0f);
            if (m >= 0 && p != a1) { wo = (int)o; mo = m; tt_dbg("[HEAD] owner at weapon +%X (%08X), matrix +%X\n", o, p, m); }
        }
        if (wo < 0) { wo = -2; tt_dbg("[HEAD] no owner found\n"); }
    }
    if (wo < 0) return;
    own = G32(w + (uint32_t)wo);
    s_hl_w = w; s_hl_own = own; s_hl_mo = mo;
    vo = mo;   /* (an actor, as the owner) */
    h = atan2f(GF(own + mo + 4), GF(own + mo)) * 57.29578f;
    bx = GF(w + 0x60 + 16); by = GF(w + 0x60 + 20);
    vx = GF(a1 + vo + 48) - GF(own + mo + 48); vy = GF(a1 + vo + 52) - GF(own + mo + 52);
    {   /* the blade's tip (4 along its Y) from the owner */
        float tx = GF(w + 0x60 + 48) + 4 * bx - GF(own + mo + 48), ty = GF(w + 0x60 + 52) + 4 * by - GF(own + mo + 52);
        if (getenv("TT_HEADZ")) tt_dbg("[HEADZ] hand %.1f tip %.1f victim %.1f\n", GF(w + 0x60 + 56) - GF(own + mo + 56), GF(w + 0x60 + 56) + 4 * GF(w + 0x60 + 24) - GF(own + mo + 56), GF(a1 + vo + 56) - GF(own + mo + 56));
        tt_dbg("[HEAD] tip %.0f %.1f victim %.0f %.1f hit %d\n", remainderf(atan2f(ty, tx) * 57.29578f - h, 360.0f), sqrtf(tx * tx + ty * ty),
               remainderf(atan2f(vy, vx) * 57.29578f - h, 360.0f), sqrtf(vx * vx + vy * vy), res >= 0);
    }
}
void tt_sweep(uint32_t a0, double res)
{
    static unsigned n[64], hit[64], last;
    uint32_t w, id;
    static int on = -1;
    if (on < 0) on = getenv("TT_SWEEPLOG") ? 1 : 0;
    if (!on || a0 < 0x10000u || a0 >= 0x84000000u) return;
    w = G32(a0 + 0x14); if (w < 0x80000000u || w >= 0x84000000u) return;
    id = G32(w + 0x2F4); if (id >= 64) return;
    n[id]++; if (res >= 0) hit[id]++;
    if (getenv("TT_SWEEPT")) {   /* testing: each weapon's sweeps, at most one line per 100 ms, with the weapon's model */
        static DWORD lt[64];
        DWORD now = GetTickCount();
        if (now - lt[id] > 100) { lt[id] = now; tt_dbg("[SWT] %u id %u w %08X model %08X res %.2f\n", (unsigned)now, id, w, G32(w + 0x494), res); }
    }
    if (id == 1 && n[id] == 3) {   /* the player's sword: the models it refers to (a resource's key at +4) */
        uint32_t o2;
        tt_dbg("[SWP] sword res %08X key %08X\n", s_sword_res, s_sword_res ? G32(s_sword_res + 4) : 0);
        for (o2 = 0; o2 < 0x800; o2 += 4) { uint32_t q = G32(w + o2); if (q >= 0x80000000u && q < 0x84000000u && (G32(q + 4) & 0xFF000000u) && G32(q) >= 0x80000000u && G32(q) < 0x84000000u) tt_dbg("[SWP]   w+%03X -> %08X (+4 %08X)\n", o2, q, G32(q + 4)); }
    }
    if (GetTickCount() - last > 5000) { int i; last = GetTickCount(); for (i = 0; i < 64; i++) if (n[i]) tt_dbg("[SWP] id %d sweeps %u hits %u\n", i, n[i], hit[i]); }
}
/* The game's melee hit test (the sweep, sub_000BA110) samples the weapon
 * from WEAPON_MIN_EXTENTS to WEAPON_MAX_EXTENTS along its local +Y (the
 * blade), through its world matrices (object +0x60, last frame's +0xA0),
 * by the vector transform sub_000FF13D (out, v, m) -- the guests' swings
 * pass a little off the hero's (their arms are not his), so their blade's
 * samples reach further: {0, y * reach, 0} for the player's weapon (id 1). */
static const float s_wreach[N_EXTRA] = { 1.35f, 1.35f, 1.0f, 1.65f, 1.35f, 1.4f, 2.25f, 1.35f, 1.5f, 1.35f, 1.35f, 1.6f, 1.35f, 1.5f, 1.35f, 2.0f };   /* Frodo: DB privada ajustada a la geometria de Dardo. */   /* the Uruk's great sword long (sampled 8 along, the hero's 4: his heavy blows found
 * the enemies, who stand further from him, half as often as the hero's -- 9 to 13% of the hit tests at 1.65 and 1.9, Aragorn's 15 to 18%) */
/* and lower: Gandalf's and Sauron's own swings carry their weapon high
 * (Sauron is half again as tall as the heroes), over the heads of the
 * orcs they strike at -- measured at the hit tests, their weapon's tip
 * about 2 and 2.7 higher than Aragorn's. Their samples taken that much
 * lower, so a blow that looks to land does. */
static const float s_wdrop[N_EXTRA] = { 0, 3.2f, 0, 0, 0, 3.6f, 0, 3.2f, 0, 3.0f, 0, 0, 0, 0, 3.0f, 3.0f };
/* 128: primero la consulta nativa; solo si falla, cuatro sondas sobre
 * cinco cortes de la envolvente convexa de la cabeza 830B7594. Mango, reloj,
 * matrices, pose y lista de victimas nativos. Sin estirar/bajar el arma.
 * TT_LEGACYMACE conserva el diagnostico anterior, solo por peticion. */
#include "heroes_troll_mace.h"
static _Thread_local int s_sweep_lane=-1;
void tt_sweep_lane(int lane){s_sweep_lane=lane;}
/* (132) el arma de la consulta en curso, tomada del propio ataque: en 131
 * dependia de s_wobj, que no se capturaba para el troll (volumen inactivo) */
static _Thread_local uint32_t s_sweep_weapon;
int tt_sweep_lanes(uint32_t attack){
    if(s_sweep_lane>=0||s_guest!=9||G8(HERO_BYTE)!=4||G8(0x43BABCu)||getenv("TT_NOMACEVOLUME")||getenv("TT_NOWAXIS"))return 1;
    if(!TT_ATTACK_OK(attack))return 1;
    uint32_t w=G32(attack+0x14),own=G32(attack+8);
    if(w<0x80000000u||w>=0x83FFF000u||G32(w+0x2F4)!=1)return 1;
    if(getenv("TT_LEGACYMACE")){if(w!=s_wobj[1])return 1;}
    else if(!own||own!=G32(0x43B15Cu))return 1;
    s_sweep_weapon=w;return 5;
}
/* (133) BA110(ataque, victima, pos*, normal*, frac*) devuelve en st0 la
 * parte golpeada o -1. Con las cinco sondas fallidas, si la cabeza de la
 * maza pasa junto a la victima: posicion, normal y fraccion como un
 * acierto nativo (parte 0). El juego sigue decidiendo candidatos, aliados,
 * lista de victimas (hasta 8 por golpe) y dano. */
#include "heroes_troll_reach.h"
int tt_sweep_fallback(const uint32_t args[6]){
    if(s_guest!=9||G8(HERO_BYTE)!=4||G8(0x43BABCu)||getenv("TT_NOTROLLREACH")||args[0]!=0xBEAB0u)return 0;
    uint32_t attack=args[1],victim=args[2],pos=args[3],nrm=args[4],fr=args[5];
    if(!TT_ATTACK_OK(attack)||victim<0x80000000u||victim>=0x83FFF000u)return 0;
    uint32_t w=G32(attack+0x14),own=G32(attack+8);
    if(w<0x80000000u||w>=0x83FFF000u||G32(w+0x2F4)!=1||!own||own!=G32(0x43B15Cu)||victim==own)return 0;
    uint32_t outs[3]={pos,nrm,fr};
    for(int i=0;i<3;i++)if(outs[i]<0x10000u||outs[i]>=0x83FFFFF0u)return 0;
    float prev[16],cur[16],v[3],p[3],n[3],t=1.f;
    for(int c=0;c<16;c++){prev[c]=GF(w+0xA0+4u*c);cur[c]=GF(w+0x60+4u*c);}
    float b[3];
    for(int c=0;c<3;c++){v[c]=GF(victim+0x90+4u*c);b[c]=GF(own+0x90+4u*c);}
    /* (134) pegado al troll: tambien el recorrido de su cuerpo a la cabeza */
    if(!troll_reach_hit(prev,cur,v,getenv("TT_NOTROLLBODY")?NULL:b,p,n,&t)){
        static unsigned misses;   /* (134) diagnostico de prueba, acotado */
        if(misses++<300)tt_dbg("[TROLL-REACH] fallo: victima %08X a %.2f %.2f %.2f, troll %.2f %.2f %.2f, cabeza %.2f %.2f %.2f\n",victim,v[0],v[1],v[2],b[0],b[1],b[2],
            5.19682591f*cur[4]+cur[12],5.19682591f*cur[5]+cur[13],5.19682591f*cur[6]+cur[14]);
        return 0;
    }
    for(int c=0;c<3;c++){GF(pos+4u*c)=p[c];GF(nrm+4u*c)=n[c];}
    if(!(GF(fr)<=t))GF(fr)=t;
    tt_dbg("[TROLL-REACH] victima %08X a %.2f %.2f %.2f, troll %.2f %.2f %.2f, maza %.2f %.2f %.2f\n",victim,v[0],v[1],v[2],b[0],b[1],b[2],p[0],p[1],p[2]);
    return 1;
}
int tt_wvec(uint32_t out, uint32_t v, uint32_t m, uint32_t ret)
{
    static int off = -1;
    static float reach = -1.0f;
    uint32_t obj = 0;
    float x, M[16], o[4];
    int c;
    if (ret != 0xBA2AEu && ret != 0xBA314u) return 0;   /* the sweep's two samples */
    if (off < 0) { off = getenv("TT_NOWAXIS") ? 1 : 0; reach = getenv("TT_WREACH") ? (float)atof(getenv("TT_WREACH")) : 0.0f; }
    if (off || s_guest < 0 || s_guest >= N_EXTRA || G8(HERO_BYTE) != 4) return 0;
    if(s_guest==SK_TROLL&&!getenv("TT_LEGACYMACE")){
        float p[3];uint32_t w=s_sweep_weapon;
        if(getenv("TT_NOMACEVOLUME")||G8(0x43BABCu)||s_sweep_lane<1||s_sweep_lane>4
          ||w<0x80000000u||w>=0x83FFF000u||(m!=w+0x60&&m!=w+0xA0)
          ||G32(w+0x2F4)!=1||G32(v)||G32(v+8)
          ||!troll_mace_point(GF(v+4),s_sweep_lane,p))return 0;
        for(c=0;c<16;c++)M[c]=GF(m+4u*c);
        for(c=0;c<4;c++)o[c]=p[0]*M[c]+p[1]*M[4+c]+p[2]*M[8+c]+M[12+c];
        if(o[3]!=0&&o[3]!=1)for(c=0;c<3;c++)o[c]/=o[3];
        for(c=0;c<3;c++)GF(out+4u*c)=o[c];return 1;
    }
    if ((G32(v) & 0x7FFFFFFFu) || (G32(v + 8) & 0x7FFFFFFFu) || !(G32(v + 4) & 0x7FFFFFFFu)) return 0;
    if (m >= 0x80000100u && m < 0x83FFF000u) {   /* the player's sword (weapon id 1): this frame's matrix or the last */
        if(s_wobj[1]&&(m==s_wobj[1]+0x60||m==s_wobj[1]+0xA0)&&G32(s_wobj[1]+0x2F4)==1u)obj=s_wobj[1];
        else if (G32(m - 0x60u + 0x2F4u) == 1u) obj = m - 0x60u;
        else if (G32(m - 0xA0u + 0x2F4u) == 1u) obj = m - 0xA0u;
    }
    if (!obj) return 0;
    for (c = 0; c < 16; c++) M[c] = GF(m + 4u * (uint32_t)c);
    if(s_guest==SK_TROLL&&s_sweep_lane>=0&&s_sweep_lane<5&&obj==s_wobj[1]){
        static const float cross[5][2]={{0,0},{-1.5f,0},{1.5f,0},{0,-1.0f},{0,1.0f}};
        /* Los 5 puntos originales [-.5,5] cubren ahora [-2,5.5]. */
        float y=-2.0f+(GF(v+4)+.5f)*(7.5f/5.5f),sx=cross[s_sweep_lane][0],sz=cross[s_sweep_lane][1];
        for(c=0;c<4;c++)o[c]=sx*M[c]+y*M[4+c]+sz*M[8+c]+M[12+c];
        if(o[3]!=0&&o[3]!=1)for(c=0;c<3;c++)o[c]/=o[3];
        for(c=0;c<3;c++)GF(out+4u*(uint32_t)c)=o[c];return 1;
    }
    x = GF(v + 4) * (reach != 0 ? reach : s_wreach[s_guest]);
    for (c = 0; c < 4; c++) o[c] = x * M[4 + c] + M[12 + c];
    if (o[3] != 0.0f && o[3] != 1.0f) for (c = 0; c < 3; c++) o[c] /= o[3];
    if (!getenv("TT_NOWDROP")) o[2] -= s_wdrop[s_guest] * (s_size_base && s_size_guest==s_guest?s_size_scale:1.0f);
    for (c = 0; c < 3; c++) GF(out + 4u * (uint32_t)c) = o[c];
    return 1;
}
/* a shot's shooter (a weapon object, its model resource at +0x494; the
 * resource's key at +4) is the guest's bow hand: the model the weapon op
 * gave it, or (should that lookup have been missed) its key */
static int shot_res_is(uint32_t shooter, uint32_t res, uint32_t key)
{
    uint32_t m;
    if (shooter < 0x80000000u || shooter >= 0x84000000u) return 0;
    m = G32(shooter + 0x494);
    if (res && m == res) return 1;
    return m >= 0x80000000u && m < 0x84000000u && G32(m + 4) == key;
}
/* the level being loaded is the prologue (Isildur's, whoever hero 4 is) */
static int s_prologue;
int tt_level_is_prologue(void) { return s_prologue; }
uint32_t tt_select_obj; unsigned long tt_select_obj_t;
/* co-op (menus.c): a place player 2 may not take (player 1's hero) while he chooses, -1 none: its label reads
 * "Unavailable" (the game's word element pointed at our text, its number at an empty one, for the draw) */
int tt_select_unavail = -1;
static uint32_t s_unav_mem, s_unav_save[2];
static uint32_t unav_text(void)
{
    extern int tt_tr16(const char *en, uint16_t *out, int max); uint16_t u[24]; int i, n;
    if (!s_unav_mem) { extern uint32_t xbox_ContiguousAlloc(uint32_t size, uint32_t alignment); s_unav_mem = xbox_ContiguousAlloc(64u, 16u); if (!s_unav_mem) return 0;
        n = tt_tr16("Unavailable", u, 24);   /* (1.9.9: in the chosen language) */
        for (i = 0; i < n; i++) G16(s_unav_mem + 2 * i) = u[i];
        G16(s_unav_mem + 2 * i) = 0; G16(s_unav_mem + 0x30) = 0; }
    return s_unav_mem;
}
/* co-op: player 2's beam, its particles spawned once out of his context. Spawned inside it (his launch runs while
 * the player global and the swapped blocks are his), they stayed for good where they were made (a playtest: "a permanent
 * trail"); player 1's, spawned in his own, fade */
static struct { uint32_t pid; int n; float a[3], d[3]; } s_p2beam[4];
static int s_p2beam_n;
static void coop_beam_flush(void)
{
    extern uint32_t coop_p2_actor(void);
    uint32_t p2 = coop_p2_actor(), scratch; int b, k, q;
    if (!s_p2beam_n || (p2 && G32(0x43B15Cu) == p2)) return;
    scratch = (tt_guest_esp() - 0x3000u) & ~0xFu;
    for (q = 0; q < 16; q++) GF(scratch + 0x40 + 4 * q) = (q % 5) ? 0.0f : 1.0f;
    for (b = 0; b < s_p2beam_n; b++)
        for (k = 0; k <= s_p2beam[b].n; k++) {
            float f = (float)k / (float)s_p2beam[b].n; uint32_t args[4]; int c;
            for (c = 0; c < 3; c++) GF(scratch + 4 * c) = s_p2beam[b].a[c] + s_p2beam[b].d[c] * f;
            GF(scratch + 12) = 1.0f;
            args[0] = s_p2beam[b].pid; args[1] = scratch; args[2] = scratch + 0x40; args[3] = fbits(getenv("TT_COOP_BEAMLIFE") ? (float)atof(getenv("TT_COOP_BEAMLIFE")) : 0.6f);   /* (its last argument: -1 kept them for good for player 2, observed) */
            tt_gcall(0x0008E470u, 0, 4, args);
        }
    if (getenv("TT_COOP_BEAMLOG")) tt_dbg("[COOPBEAM] %d beam(s) of player 2 spawned out of his context\n", s_p2beam_n);
    s_p2beam_n = 0;
}
static int s_launch_p2;
int tt_hk(int id, int phase, uint32_t *r)
{
    if (s_p2beam_n) coop_beam_flush();
    if (id == 24 && phase == 0 && getenv("TT_INPLOG")) {   /* testing: actors' inputs (this, input id), the player's marked */
        uint32_t inp = G32(r[3] + 4) & 0xFFFFu, act = r[1];
        static const int want[] = { 1, 2, 3, 6, 15, 16, 30, 28 };
        int k, all = getenv("TT_INPALL") != NULL;
        for (k = 0; k < 8; k++) if ((int)inp == want[k] || (all && k == 0)) {
            uint32_t m = G32(r[3] + 8);
            static uint32_t lastp; if (s_wobj[2] != lastp) { lastp = s_wobj[2]; tt_dbg("[INP] player actor %08X\n", lastp); }
            tt_dbg("[INP] %u %08X inp %u arg %08X ret %08X%s m %08X %08X %08X %08X %08X %08X %08X %08X\n", (unsigned)GetTickCount(), act, inp, m, G32(r[3]), act == s_wobj[2] ? " PLAYER" : "",
                   G32(m), G32(m + 4), G32(m + 8), G32(m + 12), G32(m + 16), G32(m + 20), G32(m + 24), G32(m + 28));
            break;
        }
        return 0;
    }
    uint32_t g_ecx = r[1], g_esp = r[3];
    if (id == 4 && getenv("TT_HEROLOG")) {   /* testing: who asks for the hero, and what he is told */
        static uint32_t from;
        if (phase == 0) from = G32(g_esp);
        else { static uint32_t lastf, lastv; if (from != lastf || r[0] != lastv) { lastf = from; lastv = r[0]; tt_dbg("[HERO?] from %08X -> %u (byte %u, screen %u)\n", from, r[0], G8(HERO_BYTE), G32(0x4AD050u + 4) ? 0 : 0); } }
    }
    if (id == 3 && phase == 0) {   /* the level loader: "worlds\\<level>" */
        uint32_t p = G32(g_esp + 4);
        char nm[96];
        int i;
        for (i = 0; i < 95 && p && G8(p + i) >= 32 && G8(p + i) < 127; i++) nm[i] = (char)G8(p + i);
        nm[i] = 0;
        if (strstr(nm, "orlds\\") || strstr(nm, "ORLDS\\")) s_prologue = strstr(nm, "00ISUL1") || strstr(nm, "00isul1") ? 1 : 0;
    }
    if (id == 21 && phase == 0 && getenv("TT_TABDUMP")) {   /* testing: the table at 0x424A0C (rows 0x4C), count [0x424A10]+0xC */
        static int done;
        uint32_t tab = G32(0x424A0C), db = G32(0x424A10);
        if (!done && tab && db) {
            uint32_t n = G32(db + 0xC), i, q;
            done = 1;
            for (i = 0; i < n && i < 64; i++) {
                char line[700]; int l = 0;
                l += snprintf(line + l, sizeof line - l, "[TAB] %2u:", i);
                for (q = 0; q < 0x4C; q += 4) l += snprintf(line + l, sizeof line - l, " %X", G32(tab + i * 0x4C + q));
                tt_dbg("%s\n", line);
            }
        }
    }
    if (id == 21 && phase == 0 && getenv("TT_PROJDUMP")) {   /* testing: the projectile database (rows 0x40 at [0x424A50], count [[0x424A4C]+0xC]) */
        static int done;
        uint32_t db = G32(0x424A4C), tab = G32(0x424A50);
        if (!done && db && tab) {
            uint32_t n = G32(db + 0xC), i, q;
            done = 1;
            tt_dbg("[PROJ] db %08X tab %08X n %u\n", db, tab, n);
            for (i = 0; i < n && i < 64; i++) {
                char line[700]; int l = 0;
                l += snprintf(line + l, sizeof line - l, "[PROJ] %2u:", i);
                for (q = 0; q < 0x40; q += 4) l += snprintf(line + l, sizeof line - l, " %X", G32(tab + i * 0x40 + q));
                tt_dbg("%s\n", line);
            }
        }
    }
    if (id == 3 && phase == 0) {
        static int once;
        s_last_kind = (int)G32(r[3] + 8);
        if (!once) { once = 1; tt_heroes_init(); }
        if (s_last_kind == 0) {   /* a level: is it Isildur's run at the secret level's end? */
            hero_size_reset();
            char nm[64];
            uint32_t p = G32(g_esp + 4);
            int i;
            for (i = 0; i < 63 && p && G8(p + i) >= 32 && G8(p + i) < 127; i++) nm[i] = (char)G8(p + i);
            nm[i] = 0;
            s_orth_pending = strstr(nm, "21ORTH2") && G8(HERO_BYTE) == 4 && s_guest < 0;
            s_at_watcher = strstr(nm, "02GATE") != NULL;   /* (the Gates of Moria: the Watcher) */
            if (s_orth_saving) tt_dbg("[HEROES] a level began with Isildur's finish not saved: not counted\n");
            s_orth_saving = 0;   /* a finish not saved before the next level is not counted */
        }
    }
    /* his shots: the shooter (the bow-hand weapon's object, its model at
     * +0x494) carries his bow-hand model -- the player's shot. Gandalf's
     * are all the magic bolt (projectile 15, whatever arrows he has) */
    if (id == 22 && phase == 0) { extern uint32_t coop_p2_actor(void); uint32_t p2a = coop_p2_actor(); s_launch_p2 = p2a && G32(g_esp + 0x18) == p2a; }   /* (1.9.9) player 2's shot: none of player 1's guest's ways (player 2 Legolas shot player 1 Saruman's beams) */
    if (id == 22 && phase == 0 && tt_heroes_on() && G8(HERO_BYTE) == 4 && s_guest >= 0 && s_skins[s_guest].beam && !getenv("TT_NOBEAM") && !s_launch_p2
        && (shot_res_is(G32(g_esp + 4), s_bow_res, EMPTY_PROP_KEY))) {
        s_beam_obj[s_beam_n++ & 7] = r[1];
        s_shooter = G32(g_esp + 4);
        if (!getenv("TT_KEEPPROJ")) G32(g_esp + 16) = 15u;
    }
    /* the Uruk-hai's shots: a burning rock thrown from his empty bow hand.
     * The rock is the thrown-rock projectile (10). A projectile reads its
     * entry in the projectile table as it flies (the object's +0x3DC), so his
     * rocks get their own copy of the rock's entry (in a block of the
     * contiguous memory, kept for good) carrying the fire arrow's (7) flame
     * fields; nobody else's rocks change. TT_URUKPROJ picks another
     * projectile, TT_FIREROCK="trail,impact,sparks,effect" other fields
     * (testing) */
    /* Frodo's (1.8): a plain stone, the same projectile without the fire,
     * in place of the throwing axe of the Gimli variant he plays (its
     * projectile 15; nobody else in his levels throws one) */
    /* (1.8.1) not at the Gates of Moria: the Watcher takes no harm from the
     * thrown stone (projectile 10) -- Frodo's and the Uruk-hai's shots stay
     * the game's own there */
    if (id == 22 && phase == 0 && tt_heroes_on() && G8(HERO_BYTE) == 4 && s_guest >= 0 && !s_launch_p2 && !strcmp(s_skins[s_guest].name, "frodo")
        && (G32(g_esp + 16) == 15u || shot_res_is(G32(g_esp + 4), s_bow_res, EMPTY_PROP_KEY)) && !getenv("TT_FRODOAXE")) {   /* (1.9.1: the bow's arrows too, the variant he plays now) */
        uint32_t st = G32(g_esp + 4), dv = G32(g_esp + 12);
        if (getenv("TT_SHOTLOG")) { tt_dbg("[SHOT] frodo: a stone for projectile %u (watcher level %d)\n", G32(g_esp + 16), s_at_watcher); (void)proj_record(10u); }
        if (s_at_watcher && !getenv("TT_NOFRODOSWAP")) s_frodo_pid = G32(g_esp + 16);   /* (handled as this one when it strikes: below) */
        G32(g_esp + 16) = 10u;
        (void)st; (void)dv;
        if (getenv("TT_SHOTDUMP") && s_shotdump_left <= 0 && s_shotdump_n < 400) s_shotdump_left = 16;
    }
    /* (132) el troll lanza las mismas rocas (sin animacion de lanzamiento
     * propia: la cadena de Moria no tiene una en sus clips NPC) */
    if (id == 22 && phase == 0 && tt_heroes_on() && G8(HERO_BYTE) == 4 && s_guest >= 0 && !s_launch_p2
        && (!strcmp(s_skins[s_guest].name, "uruk") || !strcmp(s_skins[s_guest].name, "troll")
            || !strcmp(s_skins[s_guest].name, "nazgul") || !strcmp(s_skins[s_guest].name, "orc") || !strcmp(s_skins[s_guest].name, "eomer") || !strcmp(s_skins[s_guest].name, "ent"))   /* (and Versus' three: they throw as he does; 1.9.9 the Ent a log, as the troll) */
        && shot_res_is(G32(g_esp + 4), s_bow_res, EMPTY_PROP_KEY)) {
        int pj = getenv("TT_URUKPROJ") ? atoi(getenv("TT_URUKPROJ")) : !strcmp(s_skins[s_guest].name, "troll") && !getenv("TT_TROLLROCK") ? TROLL_LOG_PROJ : 10;   /* (1.9.9) the troll: Fangorn's trolls' log */
        /* (1.9.1) at the Gates of Moria too: his rock (and its upgrades), struck
         * as the arrow he had, as Frodo's stone is -- the Watcher takes no
         * harm from a rock (he threw the bow's arrows there) */
        if (s_at_watcher && !getenv("TT_NOFRODOSWAP")) s_frodo_pid = G32(g_esp + 16);
        G32(g_esp + 16) = (uint32_t)pj;
        s_rock_rec = 0;
        /* from in front of him: the throw leaves from his empty bow hand,
         * which in his swing is at his side or behind him, and the rock
         * would strike his own body first. It leaves from the hand's place
         * moved ahead along the throw (TT_ROCKAHEAD units) */
        if (!getenv("TT_ROCKFROMHAND")) {
            uint32_t st = G32(g_esp + 4), dv = G32(g_esp + 12);
            float fx = GF(dv), fy = GF(dv + 4), l = sqrtf(fx * fx + fy * fy);
            float ahead = getenv("TT_ROCKAHEAD") ? (float)atof(getenv("TT_ROCKAHEAD")) : !strcmp(s_skins[s_guest].name, "troll") ? 5.0f : 3.5f;   /* (el troll, mas ancho) */
            if (st >= 0x80000000u && st < 0x84000000u && dv >= 0x10000u && l > 0.01f && l < 1000.0f) {
                s_rock_start = st;
                s_rock_start_save[0] = GF(st); s_rock_start_save[1] = GF(st + 4); s_rock_start_save[2] = GF(st + 8);
                if (!strcmp(s_skins[s_guest].name, "troll") && !getenv("TT_TROLLROCKHAND")) troll_rock_start(st, fx / l, fy / l, ahead);
                else { GF(st) += fx / l * ahead; GF(st + 4) += fy / l * ahead; }
            }
        }
        if (!strcmp(s_skins[s_guest].name, "troll")) clog_rock_arm(G32(g_esp + 4), G32(g_esp + 8), G32(g_esp + 12), s_rock_start ? s_rock_start_save : NULL);
        /* (1.7) a plain stone, as Frodo's, until his ranged weapon's last
         * upgrade (Lava Rocks): then the burning rock, and every third one
         * lands as the Orthanc archers' fire pots do (projectile 4: it
         * bursts and leaves burning ground that hurts whoever stands in it) */
        if (pj == 10 && !getenv("TT_NOFIREROCK") && ranged_top()) {
            int f[4] = { -1, 7, 53, 864 }, k;   /* (+0x38: the sound as it lands -- the rock's own) */
            uint32_t rec = proj_record(10u);
            {   /* (1.8.2) the burning rock's fire: the fire pots' own trail (an effect
                 * the game ties to each shot), not effects of ours made each frame --
                 * those took over the archers' pots' fire (the effect is one per id:
                 * theirs went out while his rock flew) and showed little on his */
                uint32_t pr0 = proj_record(4u);
                if (pr0 && (int32_t)G32(pr0 + 0x2C) >= 0 && !getenv("TT_ROCKOLDFIRE")) { f[0] = (int)G32(pr0 + 0x2C); s_rock_trail = 1; }
                else s_rock_trail = 0;
            }
            if (getenv("TT_FIREROCK")) sscanf(getenv("TT_FIREROCK"), "%d,%d,%d,%d", &f[0], &f[1], &f[2], &f[3]);
            if (!s_fire_page) { extern uint32_t xbox_ContiguousAlloc(uint32_t size, uint32_t alignment); s_fire_page = xbox_ContiguousAlloc(4096u, 4096u); }
            if (rec && s_fire_page) {
                for (k = 0; k < 16; k++) G32(s_fire_page + 4u * (uint32_t)k) = G32(rec + 4u * (uint32_t)k);
                for (k = 0; k < 4; k++) G32(s_fire_page + 0x2C + 4u * (uint32_t)k) = (uint32_t)f[k];
                s_rock_rec = rec;
                s_rock_pot = 0;
                if (++s_urk_throws % (getenv("TT_POTEVERY") ? (unsigned)atoi(getenv("TT_POTEVERY")) : 3u) == 0 && !getenv("TT_NOROCKPOT")) {
                    uint32_t pr = proj_record(4u);
                    if (pr) {   /* the rock's flight (+0x08-+0x20), the pot's landing (+0x24 on), no trail of its own */
                        for (k = 0; k < 16; k++) G32(s_fire_page + POT_REC + 4u * (uint32_t)k) = G32((k >= 9 ? pr : rec) + 4u * (uint32_t)k);
                        G32(s_fire_page + POT_REC + 0x2C) = s_rock_trail ? (uint32_t)f[0] : 0xFFFFFFFFu;
                        s_rock_pot = 1;
                    }
                }
            }
        }
        if (getenv("TT_SHOTLOG")) tt_dbg("[SHOT] uruk proj %u rec %08X copy %08X\n", G32(g_esp + 16), s_rock_rec, s_fire_page);
        if (getenv("TT_SHOTMEM") && !s_memdump_arm) s_memdump_arm = 1;
        if (getenv("TT_SHOTDUMP") && s_shotdump_left <= 0 && s_shotdump_n < 400) s_shotdump_left = 16;
    }
    if (id == 22 && phase == 1 && s_rock_start) {   /* his hand's place back */
        GF(s_rock_start) = s_rock_start_save[0]; GF(s_rock_start + 4) = s_rock_start_save[1]; GF(s_rock_start + 8) = s_rock_start_save[2];
        s_rock_start = 0;
    }
    if (id == 22 && phase == 1) clog_rock_launched(s_launching);   /* (137) registro: la roca del troll ya lanzada */
    if (id == 23) clog_rock_update(phase, r[1]);
    if (id == 22 && phase == 1 && getenv("TT_PRJSCAN")) {   /* (research) the launched object's pointers to models and textures */
        uint32_t o = s_launching, q, p, p2;
        for (q = 0; o >= 0x80000000u && q < 0x600; q += 4) { p = G32(o + q); if (p < 0x80000000u || p >= 0x83FFFF00u) continue;
            p2 = G32(p);
            if (p2 == PROP_MAGIC || p2 == 0x47425820u) tt_dbg("[PRJSCAN] +%X -> %08X model (%08X)\n", q, p, G32(p + 4));
            else if (p2 >= 0x80000000u && p2 < 0x83FFFF00u && (G32(p2) == PROP_MAGIC || G32(p2) == 0x47425820u)) tt_dbg("[PRJSCAN] +%X -> %08X -> %08X model, +4 %08X +8 %08X\n", q, p, p2, G32(p + 4), G32(p + 8)); }
    }
    if (id == 22 && phase == 1 && s_rock_rec) {   /* his rock: on its own entry */
        uint32_t o = s_launching;
        if (o >= 0x80000000u && o < 0x84000000u && G32(o + 0x3DC) == s_rock_rec && G32(o + 0x3D0) == 10u) {
            static int nx;
            G32(o + 0x3DC) = s_fire_page + (s_rock_pot ? POT_REC : 0u);
            /* a fire-pot rock is the pot (4) from here: where it lands, or
             * whoever it strikes, it bursts as one (sub_000DA110's switch;
             * a rock striking someone is handled as a rock, 10, elsewhere) */
            if (s_rock_pot) G32(o + 0x3D0) = 4u;
            if (getenv("TT_SHOTLOG")) tt_dbg("[SHOT] rock %u: %s\n", s_urk_throws, s_rock_pot ? "a fire pot" : "a fire rock");
            s_rock_pot = 0;
            s_rock_obj[nx & 7] = o; s_rock_age[nx & 7] = 0; s_rock_pid[nx & 7] = -2; s_rock_burst[nx & 7] = 0; s_rock_has_trail[nx & 7] = s_rock_trail;
            if (getenv("TT_ROCKPARTS")) {   /* testing: a particle per throw, in turn */
                int v[16], m = sscanf(getenv("TT_ROCKPARTS"), "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9], &v[10], &v[11], &v[12], &v[13], &v[14], &v[15]);
                if (m > 0) { s_rock_pid[nx & 7] = v[nx % m]; tt_dbg("[SHOT] rock particle %d\n", v[nx % m]); }
            }
            nx++;
        }
        if (getenv("TT_SHOTLOG")) tt_dbg("[SHOT] rock %08X entry %08X\n", o, o >= 0x80000000u && o < 0x84000000u ? G32(o + 0x3DC) : 0);
        s_rock_rec = 0;
    }
    if (id == 22 && phase == 1 && s_frodo_pid) {
        /* (1.8.1) Frodo's stone (and the Uruk-hai's rock) at the Gates of Moria: still the stone (its
         * model and flight), but what it strikes is struck as by the shot he
         * had (the object's +0x3D0: a rock, 10, did the Watcher no harm) */
        uint32_t o = s_launching;
        if (o >= 0x80000000u && o < 0x84000000u && G32(o + 0x3D0) == 10u) G32(o + 0x3D0) = s_frodo_pid;
        if (getenv("TT_SHOTLOG")) tt_dbg("[SHOT] frodo's stone %08X: struck as projectile %u\n", o, o >= 0x80000000u && o < 0x84000000u ? G32(o + 0x3D0) : 0);
        s_frodo_pid = 0;
    }
    if (id == 22 && phase == 1 && s_memdump_arm == 1) {   /* testing: guest memory after his first shot */
        FILE *o;
        s_memdump_arm = 2;
        tt_dbg("[SHOT] dump: projectile %08X\n", s_launching);
        if ((o = fopen(getenv("TT_SHOTMEM"), "wb"))) { fwrite((const void *)(xbox_GetMemoryOffset() + 0x80000000u), 1, 0x4000000u, o); fclose(o); }
    }
    /* Gandalf's beam: as his bolt leaves (the launch done: the projectile
     * holds where it left, +0x3E8, and where it is aimed, +0x3F8), a line
     * of glowing particles from his staff's head to that point */
    if (id == 22 && phase == 0 && getenv("TT_SHOTDUMPALL") && getenv("TT_SHOTDUMP") && s_shotdump_left <= 0 && s_shotdump_n < 400) s_shotdump_left = 16;
    if (id == 22 && phase == 0 && getenv("TT_SHOTLOG")) {   /* testing: the shot's start, direction and the player's axes */
        uint32_t st = G32(g_esp + 4), dv = G32(g_esp + 12), a = s_wobj[2] ? s_wobj[2] : 0x80000000u;
        tt_dbg("[AIM] start %.1f %.1f %.1f dir %.2f %.2f %.2f %.2f actor %.1f %.1f %.1f x %.2f %.2f %.2f y %.2f %.2f %.2f\n",
               GF(st), GF(st + 4), GF(st + 8), GF(dv), GF(dv + 4), GF(dv + 8), GF(dv + 12), GF(a + 0x90), GF(a + 0x94), GF(a + 0x98),
               GF(a + 0x60), GF(a + 0x64), GF(a + 0x68), GF(a + 0x70), GF(a + 0x74), GF(a + 0x78));
    }
    if (id == 22 && phase == 0 && getenv("TT_SHOTLOG")) {   /* testing */
        uint32_t sh = G32(g_esp + 4);
        tt_dbg("[SHOT] ret %08X shooter %08X id %u res %08X bow %08X proj %u guest %d hero %u\n", G32(g_esp), sh, sh >= 0x80000000u && sh < 0x84000000u ? G32(sh + 0x2F4) : 0,
               sh >= 0x80000000u && sh < 0x84000000u ? G32(sh + 0x494) : 0, s_bow_res, G32(g_esp + 16), s_guest, G8(HERO_BYTE));
        if (sh >= 0x80000000u && sh < 0x84000000u && G32(sh + 0x494) >= 0x80000000u && G32(sh + 0x494) < 0x84000000u) { uint32_t rr = G32(sh + 0x494), q; for (q = 0; q < 0x40; q += 16) tt_dbg("[SHOT]   res+%02X %08X %08X %08X %08X\n", q, G32(rr + q), G32(rr + q + 4), G32(rr + q + 8), G32(rr + q + 12)); }
    }
    if (id == 22 && phase == 0) s_launching = r[1];
    if (id == 22) s_in_launch = phase == 0;   /* (testing, TT_SHOTLOG: the lookups a launch makes) */
    {   /* (co-op: player 2's own launches too, when his character shoots a beam -- Gandalf, Sauron: his shots are marked
         * at the launch, the beam from the shot's own start, the shooter being player 1's bow) */
        int p2b = 0, q2; for (q2 = 0; q2 < 8; q2++) if (s_launching && s_beam_p2obj[q2] == s_launching) p2b = 1;
    if (id == 22 && phase == 1 && !getenv("TT_NOBEAM") && (p2b || (!s_launch_p2 && tt_heroes_on() && G8(HERO_BYTE) == 4 && s_guest >= 0 && s_skins[s_guest].beam))) {
        int bg = p2b ? s_playas : s_guest;
        if (p2b && getenv("TT_COOP_BEAMLOG")) { extern uint32_t coop_p2_actor(void); uint32_t pa = coop_p2_actor(), pb = G32(0x43B15Cu), ob = s_launching; static int n; if (n++ < 12 && pa && ob >= 0x80000000u && ob < 0x84000000u) tt_dbg("[COOPBEAM] start %.2f %.2f %.2f aim %.2f %.2f %.2f | P2 %.2f %.2f %.2f | player %08X %.2f %.2f %.2f\n", GF(ob + 0x3E8), GF(ob + 0x3EC), GF(ob + 0x3F0), GF(ob + 0x3F8), GF(ob + 0x3FC), GF(ob + 0x400), GF(pa + 0xF0), GF(pa + 0xF4), GF(pa + 0xF8), pb, GF(pb + 0xF0), GF(pb + 0xF4), GF(pb + 0xF8)); }
        if (p2b && getenv("TT_COOP_P2GAMESTART") && !getenv("TT_COOP_BEAMGAMESTART")) {   /* (1.9.8: his shot leaves his hand now, coop_launch_pre; this only with TT_COOP_P2GAMESTART) */   /* player 2's shot starts where player 1's bow is held (5 units over his head, beside him: observed), and the staff's
             * offset below is from a hand: his own place at hand height (TT_COOP_BEAMZ, 1.4) as the start, for the beam and the bolt's sweep */
            extern uint32_t coop_p2_actor(void); uint32_t pa = coop_p2_actor(), ob = s_launching;
            float hz = getenv("TT_COOP_BEAMZ") ? (float)atof(getenv("TT_COOP_BEAMZ")) : 1.4f;
            if (pa >= 0x80000000u && pa < 0x84000000u && ob >= 0x80000000u && ob < 0x84000000u) { GF(ob + 0x3E8) = GF(pa + 0xF0); GF(ob + 0x3EC) = GF(pa + 0xF4); GF(ob + 0x3F0) = GF(pa + 0xF8) + hz;
                /* (1.9.8) ... and its end at his lock: the shot's aim (+0x3F8) is worked out in his context from player 1's
                 * (a playtest: Gandalf's bolts hit nothing; a test, at the enemy's feet 130 units under or 100 units past it).
                 * With his lock on a living enemy: its chest (TT_COOP_BEAMAIMZ, 2.6), the beam going on past it to the length
                 * the game gave (the bolt sweeps it all); without, straight ahead of him at the game's length.
                 * TT_COOP_BEAMGAMEAIM: as before */
                if (!getenv("TT_COOP_BEAMGAMEAIM")) {
                    uint32_t c2 = G32(pa + 0x494), t = c2 >= 0x80000000u && c2 < 0x84000000u ? G32(c2 + 0x98) : 0; float e[3], d[3], l, L0; int c3;
                    for (c3 = 0; c3 < 3; c3++) d[c3] = GF(ob + 0x3F8 + 4 * c3) - GF(ob + 0x3E8 + 4 * c3);
                    L0 = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]); if (L0 < 20.0f || L0 > 200.0f) L0 = 60.0f;
                    if (t >= 0x80000000u && t < 0x84000000u && G32(t) == 0x1741C0u && G32(t + 0x114) == 3 && (int32_t)G32(t + 0x2FC) > 0) {
                        e[0] = GF(t + 0xF0); e[1] = GF(t + 0xF4); e[2] = GF(t + 0xF8) + (getenv("TT_COOP_BEAMAIMZ") ? (float)atof(getenv("TT_COOP_BEAMAIMZ")) : 2.6f);
                    } else { float fx = GF(pa + 0x70), fy = GF(pa + 0x74), fn = sqrtf(fx * fx + fy * fy);   /* (his facing, +0x70 / +0x74, as Versus sets it) */
                        if (fn < 1e-3f) { fx = 1.0f; fy = 0.0f; fn = 1.0f; }
                        e[0] = GF(ob + 0x3E8) + fx / fn * 10.0f; e[1] = GF(ob + 0x3EC) + fy / fn * 10.0f; e[2] = GF(ob + 0x3F0); t = 0; }
                    for (c3 = 0; c3 < 3; c3++) d[c3] = e[c3] - GF(ob + 0x3E8 + 4 * c3);
                    l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                    if (l > 0.5f) { float k = (t ? (l + 6.0f > L0 ? l + 6.0f : L0) : L0) / l; for (c3 = 0; c3 < 3; c3++) GF(ob + 0x3F8 + 4 * c3) = GF(ob + 0x3E8 + 4 * c3) + d[c3] * k; }
                    if (getenv("TT_COOP_BEAMLOG")) tt_dbg("[COOPBEAM] aimed at %s %08X: %.2f %.2f %.2f, the beam %.1f long\n", t ? "his lock" : "(no lock) ahead", t, e[0], e[1], e[2], t ? (l + 6.0f > L0 ? l + 6.0f : L0) : L0);
                } }
        }
        uint32_t o = s_launching, scratch = (tt_guest_esp() - 0x3000u) & ~0xFu, pid;
        float a[3], b[3], d[3], len, step;
        int k, n, c, mine = 0, q;
        for (q = 0; q < 8; q++) if (o && (p2b ? s_beam_p2obj[q] : s_beam_obj[q]) == o) { mine = 1; if (p2b) s_beam_p2obj[q] = 0; else s_beam_obj[q] = 0; }
        if (mine && o >= 0x80000000u && o < 0x84000000u && s_shooter >= 0x80000000u && s_shooter < 0x84000000u) {
            /* (1.7) the bolt flies unseen: only the beam shows. Its model
             * (+0x494, as the weapons') was the arrows' -- the bow hand's
             * empty prop instead, the shooter's own */
            uint32_t m = G32(o + 0x494), e = G32(s_shooter + 0x494);
            if (getenv("TT_SHOTLOG")) {
                uint32_t q;
                (void)proj_record(15u);
                for (q = 0; q < 0x600; q += 4) {
                    uint32_t v = G32(o + q), d1, d2;
                    if (v < 0x80000000u || v >= 0x83FFFF00u) continue;
                    d1 = G32(v); d2 = G32(v + 12);
                    if (d1 == 0x47425820u || d1 == PROP_MAGIC || (d2 >= 0x80000000u && d2 < 0x83FFFF00u && (G32(d2) == 0x47425820u || G32(d2) == PROP_MAGIC))
                        || (d1 >= 0x80000000u && d1 < 0x83FFFF00u && (G32(d1) == 0x47425820u || G32(d1) == PROP_MAGIC)))
                        tt_dbg("[BEAM]   +%03X %08X -> %08X %08X %08X %08X\n", q, v, d1, G32(v + 4), G32(v + 8), d2);
                }
            }
            if (getenv("TT_SHOTLOG")) tt_dbg("[BEAM] bolt %08X model %08X (key %08X), the bow hand's %08X (key %08X)\n", o, m,
                                              m >= 0x80000000u && m < 0x84000000u ? G32(m + 4) : 0, e, e >= 0x80000000u && e < 0x84000000u ? G32(e + 4) : 0);
            if (!getenv("TT_BEAMARROW") && (m == 0 || (m >= 0x80000000u && m < 0x84000000u)) && e >= 0x80000000u && e < 0x84000000u && G32(e + 4) == EMPTY_PROP_KEY)
                G32(o + 0x494) = e;
            /* ... and it is at the end of its flight at once: its last place
             * the start (+0x3E8), its place a step short of where it is aimed
             * (+0x3F8), so its first move sweeps the beam's whole length and
             * strikes what the beam touches -- the bolt the game draws (the
             * level's arrow, for want of a bolt of its own) is never seen
             * flying behind the beam */
            if (!getenv("TT_BEAMFLY")) {
                float a0[3], b0[3], d0[3], l0; int c0;
                for (c0 = 0; c0 < 3; c0++) { a0[c0] = GF(o + 0x3E8 + 4 * c0); b0[c0] = GF(o + 0x3F8 + 4 * c0); d0[c0] = b0[c0] - a0[c0]; }
                l0 = sqrtf(d0[0] * d0[0] + d0[1] * d0[1] + d0[2] * d0[2]);
                if (l0 > 3.0f && l0 < 1000.0f) {
                    for (c0 = 0; c0 < 3; c0++) {
                        GF(o + 0x408 + 4 * c0) = b0[c0] - d0[c0] / l0 * 1.5f;
                        GF(o + 0x418 + 4 * c0) = a0[c0];
                    }
                    if (getenv("TT_SHOTLOG")) tt_dbg("[BEAM] bolt sent to its end (%.1f units)\n", l0);
                }
            }
        }
        if (mine && !strcmp(s_skins[bg].name, "sauron")) {   /* (1.7) his last ranged upgrade: red, then blue, in turn */
            static unsigned nshot;
            s_bolt_blue = ranged_top() && (nshot++ & 1);
        }
        if (mine && !strcmp(s_skins[bg].name, "saruman")) { static unsigned nshot; s_bolt_blue = nshot++ & 1; }   /* (1.9.9) his: green-yellow, then yellow, in turn */
        if (mine && bolt_mode() >= 0) effect_colour(0x135u, bolt_mode());
        if (mine) {
            pid = getenv("TT_BEAMPART") ? (uint32_t)strtoul(getenv("TT_BEAMPART"), NULL, 0) : 0x135u;
            step = getenv("TT_BEAMSTEP") ? (float)atof(getenv("TT_BEAMSTEP")) : 1.0f;
            for (c = 0; c < 3; c++) { a[c] = GF(o + 0x3E8 + 4 * c); b[c] = GF(o + 0x3F8 + 4 * c); d[c] = b[c] - a[c]; }
            {   /* from his staff's head: the staff's object (the sword hand's
                 * weapon, its model at +0x494, near the shooter) holds its
                 * world box at +0xE0 (8 corners of 16 bytes): the second four
                 * the far end of the model's long axis -- the head */
                uint32_t va, ob = 0;
                if (s_shooter && !shield_on(bg) && !p2b) {   /* (his staff in his model: no object of its own) */
                    const char *ws; uint32_t wm = 0, wt;
                    skin_weapon(bg, 1, &ws, &wm, &wt);
                    for (va = s_shooter - 0x4000u; va < s_shooter + 0x4000u && !ob; va += 4)
                        if (va - 0x494u != s_shooter && G32(va) >= 0x80000000u && G32(va) < 0x84000000u
                            && ((s_sword_res && G32(va) == s_sword_res) || (wm && G32(G32(va) + 4) == wm))) ob = va - 0x494u;
                }
                if (ob && !getenv("TT_TIP")) {
                    float h[3] = { 0, 0, 0 };
                    for (q = 0; q < 4; q++) for (c = 0; c < 3; c++) h[c] += GF(ob + 0x120 + 0x10 * (uint32_t)q + 4 * (uint32_t)c) * 0.25f;
                    for (c = 0; c < 3; c++) if (!(fabsf(h[c] - a[c]) < 20.0f)) ob = 0;   /* (nothing sensible: the old way) */
                    if (ob) for (c = 0; c < 3; c++) { a[c] = h[c]; d[c] = b[c] - a[c]; }
                }
                if (!ob) {   /* off the shot's own start: tip = {ahead, right, up} */
                    float tip[3] = { 1.0f, -1.1f, 2.5f }, fw[2], hl = sqrtf(d[0] * d[0] + d[1] * d[1]);
                    if (shield_on(bg)) { tip[0] = 1.2f; tip[1] = 1.4f; tip[2] = 3.6f; }   /* the staff's head, in his left hand */
                    if (getenv("TT_TIP")) sscanf(getenv("TT_TIP"), "%f,%f,%f", &tip[0], &tip[1], &tip[2]);
                    if (hl > 0.01f) {
                        fw[0] = d[0] / hl; fw[1] = d[1] / hl;
                        a[0] += fw[0] * tip[0] + fw[1] * tip[1];
                        a[1] += fw[1] * tip[0] - fw[0] * tip[1];
                        a[2] += tip[2];
                        for (c = 0; c < 3; c++) d[c] = b[c] - a[c];
                    }
                }
            }
            len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (len > 120.0f) { for (c = 0; c < 3; c++) d[c] *= 120.0f / len; len = 120.0f; }
            n = (int)(len / step); if (n > 72) n = 72; if (n < 1) n = 1;
            for (q = 0; q < 16; q++) GF(scratch + 0x40 + 4 * q) = (q % 5) ? 0.0f : 1.0f;   /* an identity matrix */
            if (p2b && !getenv("TT_COOP_BEAMNOW")) {   /* player 2's: spawned outside his context (coop_beam_flush) */
                if (s_p2beam_n < 4) { int c2; s_p2beam[s_p2beam_n].pid = pid; s_p2beam[s_p2beam_n].n = n; for (c2 = 0; c2 < 3; c2++) { s_p2beam[s_p2beam_n].a[c2] = a[c2]; s_p2beam[s_p2beam_n].d[c2] = d[c2]; } s_p2beam_n++; }
                n = -1;
            }
            for (k = 0; k <= n && !getenv("TT_BEAMNOPART"); k++) {   /* (TT_BEAMNOPART testing: the bolt alone) */
                float f = (float)k / (float)n;
                uint32_t args[4];
                for (c = 0; c < 3; c++) GF(scratch + 4 * c) = a[c] + d[c] * f;
                GF(scratch + 12) = 1.0f;
                args[0] = pid; args[1] = scratch; args[2] = scratch + 0x40; args[3] = fbits(getenv("TT_COOP_BEAMLIFE") ? (float)atof(getenv("TT_COOP_BEAMLIFE")) : 0.6f);   /* (-1: the particles stayed for good, his own too: observed 2026-10-19) */
                tt_gcall(0x0008E470u, 0, 4, args);
            }
            if (getenv("TT_PROJLOG")) tt_dbg("[BEAM] %d particles %X over %.1f from %.2f %.2f %.2f\n", n + 1, pid, len, a[0], a[1], a[2]);
        }
    }
    }
    if (id == 23 && phase == 0 && getenv("TT_COOP_BEAMLOG")) {   /* (research) player 2's bolts as they update */
        uint32_t o = r[1]; int q; static uint32_t lt[8], cnt[8], first[8], seen[8];
        for (q = 0; q < 8; q++) {
            if (!seen[q] && s_beam_p2obj_log[q] == o) { seen[q] = o; first[q] = GetTickCount(); cnt[q] = 0; lt[q] = 0; }
            if (seen[q] == o) { cnt[q]++; if (GetTickCount() - lt[q] > 1000) { lt[q] = GetTickCount(); tt_dbg("[COOPBEAM] bolt %08X at %lu ms: %u updates, at %.1f %.1f %.1f, +3F8 %.1f %.1f %.1f, +408 %.1f %.1f %.1f\n", o, (unsigned long)(GetTickCount() - first[q]), cnt[q], GF(o + 0xF0), GF(o + 0xF4), GF(o + 0xF8), GF(o + 0x3F8), GF(o + 0x3FC), GF(o + 0x400), GF(o + 0x408), GF(o + 0x40C), GF(o + 0x410)); } }
        }
    }
    if ((id == 22 || id == 23) && getenv("TT_PROJLOG")) {   /* testing: projectiles */
        static uint32_t track, launching; static int nup;
        uint32_t o = r[1];
        if (id == 22 && phase == 0) launching = o;
        if (id == 22 && phase == 1) o = launching;
        if (id == 22 && phase == 0) {
            uint32_t a1 = G32(g_esp + 4), va;
            tt_dbg("[PRJ] launch obj %08X ret %08X args %08X %08X %08X %08X %08X\n", o, G32(g_esp), a1, G32(g_esp + 8), G32(g_esp + 12), G32(g_esp + 16), G32(g_esp + 20));
            {   /* pointers in the shooter (two levels) to an animation set */
                uint32_t q, q2;
                for (q = 0; q < 0x800; q += 4) {
                    uint32_t p = G32(a1 + q);
                    if (p < 0x80000000u || p >= 0x84000000u) continue;
                    if (G32(p) == 0x7A696E61u) tt_dbg("[PRJ]   +%X -> aniz size %X\n", q, G32(p + 0xC));
                    for (q2 = 0; q2 < 0x200; q2 += 4) { uint32_t p2 = G32(p + q2); if (p2 >= 0x80000000u && p2 < 0x84000000u && G32(p2) == 0x7A696E61u) tt_dbg("[PRJ]   +%X+%X -> aniz size %X\n", q, q2, G32(p2 + 0xC)); }
                }
                tt_dbg("[PRJ]   bow res %08X\n", s_bow_res);
                for (q = 0; q < 0x800; q += 4) {
                    uint32_t p = G32(a1 + q);
                    if (p == s_bow_res) tt_dbg("[PRJ]   +%X = bow res\n", q);
                    if (p >= 0x80000000u && p < 0x84000000u) for (q2 = 0; q2 < 0x100; q2 += 4) if (G32(p + q2) == s_bow_res) tt_dbg("[PRJ]   +%X+%X = bow res\n", q, q2);
                }
            }
        }
        if (id == 22 && phase == 1) {
            char line[900]; int l = 0; uint32_t q;
            track = o; nup = 0;
            l += snprintf(line + l, sizeof line - l, "[PRJ] obj %08X:", o);
            for (q = 0x3D0; q < 0x4E0; q += 4) l += snprintf(line + l, sizeof line - l, " %X", G32(o + q));
            tt_dbg("%s\n", line);
        }
        if (id == 23 && phase == 0) { static int n23; if (n23++ < 20) tt_dbg("[PRJ] upd call obj %08X (track %08X)\n", o, track); }
        if (id == 23 && phase == 0) o = r[1];
        if (id == 23 && phase == 0 && o == track && nup++ < 40) {
            char line[900]; int l = 0; uint32_t q;
            l += snprintf(line + l, sizeof line - l, "[PRJ] t%u upd:", GetTickCount() % 100000);
            for (q = 0; q < 0x40; q += 4) l += snprintf(line + l, sizeof line - l, " %.2f", GF(o + 0x3E8 + q));
            l += snprintf(line + l, sizeof line - l, " |");
            for (q = 0xD0; q < 0x120; q += 4) l += snprintf(line + l, sizeof line - l, " %.2f", GF(o + q));
            tt_dbg("%s\n", line);
        }
    }
    if (id == 10 && phase == 0 && s_last_kind == 0) { keyswap_apply(); skin_apply(); }
    if (id == 10 && phase == 0 && getenv("TT_UPGALL")) {   /* testing: every upgrade bought, every hero */
        uint32_t h, i;
        for (h = 0; h < 4; h++) for (i = 0; i < 0x28; i++) G8(0x4AC600u + h * 0x50u + i) = 1;
    }

    /* the weapon script op: the player's weapon in that hand becomes the
     * guest's own (resource lookups of type 2 = model, 1 = texture) */
    if (id == 21 && phase == 0 && getenv("TT_WOPDUMP")) {
        uint32_t ins = G32(g_ecx + 0x30), q;
        char line[600]; int l = 0;
        l += snprintf(line + l, sizeof line - l, "[WOP] rec %d ins %08X:", wop_record(g_ecx), ins);
        for (q = 0; q < 0x40; q += 4) l += snprintf(line + l, sizeof line - l, " %08X", G32(ins + q));
        tt_dbg("%s\n", line);
    }
    if (id == 21) {
        s_in_wop = phase == 0;
        if (phase == 0) { s_wop_swap = 0; s_wop_tex = 0; s_wop_player = 0; s_wop_ins = G32(g_ecx + 0x30); s_wop_ctx = g_ecx; }
        if (phase == 0) s_wop_rec = wop_record(g_ecx) * 100 + (int)G32(G32(g_ecx + 0x30) + 0x18);
        if (phase == 1 && s_wop_player) {   /* the player's weapon objects (bow +0xBC, sword +0xC0 of his record) */
            uint32_t ctx = s_wop_ctx, n = G32(ctx + 0x178), i;
            for (i = 0; i < n && i < 64; i++) {
                uint32_t e = ctx + 0x188 + i * 0xE4;
                if (G32(e + 4) == G32(s_wop_ins + 4)) {
                    s_wobj[0] = G32(e + 0xBC); s_wobj[1] = G32(e + 0xC0);
                    s_wobj[2] = G32(e + 8);
                    if (getenv("TT_WOBJWATCH")) tt_dbg("[WOBJ] record %u: bow obj %08X sword obj %08X actor %08X\n", i, s_wobj[0], s_wobj[1], G32(e + 8));
                }
            }
        }
        /* his second hand: once the player's sword is given, the op again
         * for another slot, with his shield (the lookups swapped below) */
        if (phase == 1 && s_wop_player == 2 && !s_wop_extra && tt_heroes_on() && G8(HERO_BYTE) == 4 && s_guest >= 0
            && s_skins[s_guest].xmesh && !getenv("TT_NOWEAPON")) {
            uint32_t ctx = s_wop_ctx, ins = s_wop_ins, save_ip = G32(ctx + 0x30), save_slot = G32(ins + 0x18);
            int slot = getenv("TT_XSLOT") ? atoi(getenv("TT_XSLOT")) : s_skins[s_guest].xslot;
            s_wop_extra = 1;
            G32(ctx + 0x30) = ins; G32(ins + 0x18) = (uint32_t)slot;
            {
                extern void *recomp_lookup(uint32_t);
                tt_dbg("[HEROES] extra op: ctx %08X ins %08X fn %p\n", ctx, ins, recomp_lookup(0x0001AE00u));
            }
            tt_gcall(0x0001AE00u, ctx, 0, NULL);
            G32(ins + 0x18) = save_slot; G32(ctx + 0x30) = save_ip;
            s_wop_extra = 0;
            tt_dbg("[HEROES] %s: second hand (slot %d)\n", s_skins[s_guest].name, slot);
        }
    }
    /* the guest's bow-hand weapon's model as the game holds it (the
     * lookup's result): a shot whose shooter carries it is his */
    if (id == 2 && phase == 0) s_lk_key = G32(g_esp + 8), s_lk_ty = G32(g_esp + 4);
    if (id == 2 && phase == 1 && s_lk_ty == 2 && LVG >= 0 && s_lk_key && (int)r[0]) {
        const char *ws; uint32_t wm, wt;
        if (s_in_wop && (s_lk_key == 0x36AC3F58u || (skin_weapon(LVG, 0, &ws, &wm, &wt) && s_lk_key == wm))) s_bow_res = r[0];
        if (s_in_wop && (s_lk_key == 0x4586A62Cu || (skin_weapon(LVG, 1, &ws, &wm, &wt) && s_lk_key == wm))) s_sword_res = r[0];
    }
    if (id == 2 && phase == 0 && s_in_launch && getenv("TT_SHOTLOG"))
        tt_dbg("[SHOTLK] ty %u key %08X\n", G32(g_esp + 4), G32(g_esp + 8));
    if (id == 2 && phase == 0 && getenv("TT_LKLOG"))   /* testing: every resource lookup */
        tt_dbg("[LK] t%u ty %u key %08X from %08X\n", GetTickCount() / 10 % 1000000, G32(g_esp + 4), G32(g_esp + 8), G32(g_esp));
    if (id == 2 && phase == 0 && s_in_wop && getenv("TT_WOPLOG"))
        tt_dbg("[WOPLK] rec %d ty %u key %08X\n", s_wop_rec, G32(g_esp + 4), G32(g_esp + 8));
    /* the weapon op's resource lookups (type 2 model, 1 texture): the
     * hero's bow / sword -> the guest's own; in the extra op, his shield */
    if (id == 2 && phase == 0 && s_in_wop && tt_heroes_on() && G8(HERO_BYTE) == 4 && LVG >= 0 && !getenv("TT_NOWEAPON")) {
        uint32_t ty = G32(g_esp + 4), key = G32(g_esp + 8);
        if (ty == 2) {
            const char *ws; uint32_t wm, wt;
            /* the player's record holds the bow (36AC3F58) and two
             * sword-hand models: 4586A62C and 13F82514 (the one drawn in
             * his hand as he fights) -- both become the guest's */
            int sl = key == 0x36AC3F58u ? 0 : key == 0x4586A62Cu ? (arrow_is_his(LVG) ? (!strcmp(s_skins[LVG].name, "frodo") ? 0 : 1) : -2) : -1;   /* (Frodo's draw hand: empty, the stone is thrown from it) */
            {   /* (1.9.1) in a scene (no HUD) the arrow is nothing for a hero with no
                 * bow: the level's archers draw it (the Plains of Rohan's opening, on
                 * the wall) and drew Sauron's mace back as an arrow */
                extern int tt_in_scene(void);
                if (key == 0x4586A62Cu && sl == 1 && s_skins[LVG].nobow && tt_in_scene() && !getenv("TT_NOSCENEARROW")) sl = 0;
            }
            if (sl < 0 && key == 0x13F82514u && !getenv("TT_NOSWORD2")) sl = 1;
            if (sl >= 0 && !getenv("TT_SWORD2ALL")) {
                /* the player's, and the cutscenes' copies of him -- not another
                 * character's: the hero's bow and swords are the level's archers'
                 * and companions' too (an archer held Sauron's mace for a bow in
                 * the Plains of Rohan's opening; 1.9: the bow and the first sword
                 * as well as 13F82514) */
                int him = wop_is_him();
                if (getenv("TT_WOPLOG")) {
                    uint32_t c = s_wop_ctx; int ri = c ? wop_record(c) : -1;
                    uint32_t act = ri >= 0 ? G32(c + 0x188 + (uint32_t)ri * 0xE4u + 8) : 0;
                    tt_dbg("[WOPLK] rec %d: %08X -- %s (ctx %08X idx %d actor %08X, player %08X)\n", s_wop_rec, key,
                           him > 0 ? "his" : him == 0 ? "another's, kept" : "unknown, his", c, ri, act, G32(0x43B15Cu));
                }
                if (him == 0) sl = -1;
            }
            if (sl < 0 && key) {   /* already the guest's (the level's weapon records were changed) */
                const char *ws2; uint32_t wm2, wt2;
                if (skin_weapon(LVG, 0, &ws2, &wm2, &wt2) && key == wm2) sl = 0;
                else if (skin_weapon(LVG, 1, &ws2, &wm2, &wt2) && key == wm2) sl = 1;
            }
            s_wop_swap = 0;
            if (sl == -2) sl = -1;   /* (the arrow stays an arrow) */
            if (sl >= 0) s_wop_player = sl + 1;
            if (s_wop_extra && sl >= 0) { s_wop_mesh = s_skins[LVG].xmesh; s_wop_wtex = s_skins[LVG].xtex; s_wop_swap = 1; }
            else if (sl >= 0 && skin_weapon(LVG, sl, &ws, &wm, &wt)) { s_wop_mesh = wm; s_wop_wtex = wt; s_wop_swap = 1; }
            if (s_wop_swap) { G32(g_esp + 8) = s_wop_mesh; s_wop_tex = 0; }
        } else if (ty == 1 && s_wop_swap) {
            int k = s_wop_tex++;
            if (k == 0) G32(g_esp + 8) = s_wop_wtex;
            else if (k == 1 && weapon_tex2(s_wop_mesh) && !getenv("TT_NOTEX2")) G32(g_esp + 8) = weapon_tex2(s_wop_mesh);   /* its second layer (Sting's glow) */
        }
    }
    if (id == 18 && phase == 0) { tt_select_obj = g_ecx; tt_select_obj_t = GetTickCount(); }
    /* co-op: player 2's shots are his own character's (the launch's owner, [esp+0x18], is player 2; he shoots with
     * LT held and A or Y, as player 1 does). The projectile ids player 1 fires as each hero (observed, Balin's Tomb):
     * Aragorn 13, Legolas 19, Gimli 16 (his throwing axe), Isildur 15; Frodo and the Uruk-hai a stone (10, as for them
     * as hero 4); Gandalf and Sauron 15 (hero 4's, their bolt); other guests the arrow (13) */
    if (id == 22 && phase == 0) {   /* (1.9.8, co-op) each hero's shot noted with his lock as it leaves (in his own context), and whether it is a beam */
        extern void coop_proj_note(uint32_t proj, uint32_t shooter, int beam); extern uint32_t coop_p2_actor(void);
        uint32_t shr = G32(g_esp + 0x18); int bm = 0;
        if (shr && shr == coop_p2_actor()) bm = s_coop_skin && s_playas >= 0 && s_skins[s_playas].beam && !getenv("TT_NOBEAM");
        else bm = tt_heroes_on() && G8(HERO_BYTE) == 4 && s_guest >= 0 && s_skins[s_guest].beam && !getenv("TT_NOBEAM");
        coop_proj_note(r[1], shr, bm);
    }
    if (id == 22 && phase == 0 && !getenv("TT_COOP_P2KEEPPROJ")) {
        extern uint32_t coop_p2_actor(void); extern const char *tt_coop_p2_char_now;
        uint32_t p2 = coop_p2_actor();
        if (p2 && G32(g_esp + 0x18) == p2) {
            static const struct { const char *who; uint32_t pj; } own[] = { { "aragorn", 13 }, { "legolas", 19 }, { "gimli", 16 }, { "isildur", 15 },
                { "frodo", 10 }, { "uruk", 10 }, { "gandalf", 15 }, { "sauron", 15 }, { "nazgul", 10 }, { "orc", 10 }, { "eomer", 10 },
                { "troll", TROLL_LOG_PROJ }, { "elvenwarrior", 19 }, { "saruman", 15 }, { "urukcap", 10 }, { "ent", 10 }, { "troll2", TROLL_LOG_PROJ } };   /* (1.9.9) */
            const char *c = tt_coop_p2_char_now; uint32_t pj = G32(g_esp + 16), to = pj; int q;
            if (c) { to = 13u; for (q = 0; q < (int)(sizeof own / sizeof own[0]); q++) if (!_stricmp(c, own[q].who)) { to = own[q].pj; break; } }
            if (to != pj) G32(g_esp + 16) = to;
            if (s_coop_skin && s_playas >= 0 && s_skins[s_playas].beam && !getenv("TT_NOBEAM")) { s_beam_p2obj_log[s_beam_p2n & 7] = r[1]; s_beam_p2obj[s_beam_p2n++ & 7] = r[1]; }   /* his bolt: a beam (below) */
            { static int n; uint32_t sh = G32(g_esp + 4), m = sh >= 0x80000000u && sh < 0x84000000u ? G32(sh + 0x494) : 0;
              if (n++ < 20) tt_dbg("[COOPSHOT] player 2 (%s) shoots: projectile %u -> %u, shooter %08X model %08X (key %08X)\n", c ? c : "copy", pj, to, sh, m, m >= 0x80000000u && m < 0x84000000u ? G32(m + 4) : 0); }
        }
    }
    if (id == 22 && phase == 0 && getenv("TT_COOP_SHOTLOG")) { extern uint32_t coop_p2_actor(void); static int n; if (n++ < 60) tt_dbg("[COOPSHOT] t %lu launch: ecx %08X args %08X %08X %08X %08X %08X %08X %08X %08X | player now %08X, P1 %08X P2 %08X\n", GetTickCount(), r[1], G32(g_esp + 4), G32(g_esp + 8), G32(g_esp + 12), G32(g_esp + 16), G32(g_esp + 20), G32(g_esp + 24), G32(g_esp + 28), G32(g_esp + 32), G32(0x43B15Cu), G32(0x43B15Cu), coop_p2_actor()); }
    if (id == 24 && phase == 0) {   /* the game's labels (characters 0..3): player 1's hero's while player 2 chooses */
        int p = tt_select_unavail; uint32_t num = p < 0 ? 99u : places() ? aid(p) : (uint32_t)p;
        s_unav_save[0] = s_unav_save[1] = 0xFFFFFFFFu;
        if (num < 4 && unav_text()) {
            uint32_t w = 0x42DAB8u + num * 0xDCu, nb = 0x42DE28u + num * 0xDCu;
            extern volatile uint32_t tt_unav_word_el, tt_unav_text;
            s_unav_save[0] = 0; s_unav_save[1] = G32(nb + 0x34);
            tt_unav_word_el = w; tt_unav_text = s_unav_mem;   /* the word: swapped in its own draw (menus.c), the routine sets it again first */
            G32(nb + 0x34) = s_unav_mem + 0x30;
        }
    }
    if (id == 24 && phase == 1 && s_unav_save[0] != 0xFFFFFFFFu) {
        int p = tt_select_unavail; uint32_t num = p < 0 ? 99u : places() ? aid(p) : (uint32_t)p;
        extern volatile uint32_t tt_unav_word_el;
        if (num < 4) G32(0x42DE28u + num * 0xDCu + 0x34) = s_unav_save[1];
        tt_unav_word_el = 0;
        s_unav_save[0] = 0xFFFFFFFFu;
    }   /* (menus.c: the character select's object, co-op player 2's turn) */
    if (tt_heroes_on()) {
        /* the menu's characters: after the fourth retail one, ours */
        if (id == 17 && phase == 0 && !s_spawning) {
            int k;
            for (k = 0; k < 4; k++) s_fe_last_args[k] = G32(g_esp + 4 + 4 * k);
        }
        if (id == 17 && phase == 1 && !s_spawning && (int)r[0] == 0) s_fe_anim0 = s_fe_last_args[0];   /* Aragorn's */
        if (id == 17 && phase == 1 && !s_spawning && (int)r[0] == 3) {
            s_fe_extra = 0;
            s_spawn_pending = 1;
            s_nname = -1;   /* this menu's texts: found again on the next name change */
            s_hla_n = -1;
            /* at the first menu of a run the loader may not have reached our
             * models yet (they are at the end of the file): then the
             * character select tries again when it runs */
            if (getenv("TT_FE_LATE")) s_spawn_pending = 2;   /* testing: as if not loaded yet */
            else fe_spawn_extras();
        }
        /* the chosen pose's voice the game gives as the select opens: a guest's own */
        if (id == 19 && phase == 0 && places()) select_entry(s_cursor >= 4 && s_cursor < places() ? place_guest(s_cursor) : -1);
        if (id == 8 && phase == 0) select_gone();   /* (the menus' input, every frame of a menu) */
        /* the select's labels: the others' too (1.7). The game labels its
         * characters 0..3 with heroes 1..4's levels; number 3 is a guest's
         * when Isildur is unlocked (he takes the guest's number), and hero 4's
         * level is whoever's records it holds: for that one call, his own. */
        if (id == 24 && phase == 0 && places()) {
            int p3;
            s_lab_l4_set = 0;
            for (p3 = 0; p3 < places(); p3++) if (aid(p3) == 3) break;
            if (p3 < places()) {
                int lv = hero_level_of(p3 >= 4 ? place_guest(p3) : -1);
                s_lab_l4 = G32(0x4AB60Cu + 4 * 0x348u);
                s_lab_l4_set = 1;
                G32(0x4AB60Cu + 4 * 0x348u) = (uint32_t)(lv - 1);
            }
        }
        if (id == 24 && phase == 1 && s_lab_l4_set) { G32(0x4AB60Cu + 4 * 0x348u) = s_lab_l4; s_lab_l4_set = 0; }
        if (id == 24 && phase == 1 && places()) extra_labels();
        /* never seen while locked */
        if (id == 18 && phase == 1 && !places() && s_fe_extra > 0) {
            int k;
            for (k = 4; k < 4 + s_fe_extra; k++) { uint32_t aa[2] = { (uint32_t)k, fbits(0.0f) }; tt_gcall(0x000CAAD0u, FE_MGR, 2, aa); }
        }
        /* the carousel: ours when it has eight places */
        if (id == 18 && phase == 0 && s_spawn_pending && s_fe_extra == 0) {
            static unsigned t;
            if (++t % 15 == 0) fe_spawn_extras();   /* a scan of the whole window: not every frame */
        }
        if (id == 18 && phase == 0 && places()) {
            static int spin, spin_ok, tick;
            select_entry(s_cursor >= 4 && s_cursor < places() ? place_guest(s_cursor) : -1);   /* his line and confirm while he is in front */
            s_sel_tick = GetTickCount(); s_sel_set = 1;
            if (getenv("TT_SELVTEST")) {   /* testing: each guest's select line in turn */
                static unsigned fr;
                if (++fr % 150 == 0) select_voice(g_ecx, (int)(fr / 150) % N_SEL);
            }
            if (!spin_ok) { spin_ok = 1; spin = getenv("TT_FE_SPIN") ? atoi(getenv("TT_FE_SPIN")) : 0; }   /* testing: step right every N frames */
            static int spin_to = -2;
            if (spin_to == -2) spin_to = getenv("TT_FE_SPINTO") ? atoi(getenv("TT_FE_SPINTO")) : -1;
            if (spin != 0 && s_cursor != spin_to && ++tick % (spin < 0 ? -spin : spin) == 0) {   /* < 0: to the left */
                int n = places();
                if (s_cursor < 0 || s_cursor >= n) s_cursor = (int)G32(g_ecx + 0x18);
                {   /* as the stick steps: past the locked */
                    int step = 0;
                    do s_cursor = (s_cursor + (spin > 0 ? 1 : n - 1)) % n;
                    while (place_locked(s_cursor) && ++step < n);
                }
                G8(g_ecx + 0x210) = spin > 0 ? 1 : 0;
                cursor_changed(g_ecx);
                tt_dbg("[SELECT] spin to place %d\n", s_cursor);
            }
            if (spin_to >= 4 && s_cursor == spin_to && getenv("TT_FE_STAB") && ++tick % 120 == 0) {   /* testing: the confirm move */
                uint32_t aa[4] = { aid(s_cursor), 0x28u, 0, 0x64u };
                tt_gcall(0x000CA960u, FE_MGR, 4, aa);
                tt_dbg("[SELECT] confirm move on place %d\n", s_cursor);
            }
            layout(g_ecx); r[3] += 4; return 1;
        }
        if ((id == 6 || id == 7) && phase == 0 && places()) {
            int n = places();
            if (s_cursor < 0 || s_cursor >= n) s_cursor = (int)G32(g_ecx + 0x18);
            {   /* locked guests stand in the ring, but the cursor passes them by */
                int step = 0;
                do s_cursor = (s_cursor + (id == 6 ? 1 : n - 1)) % n;
                while (place_locked(s_cursor) && ++step < n);
            }
            G8(g_ecx + 0x210) = id == 6 ? 1 : 0;
            cursor_changed(g_ecx);
            if (log_on()) tt_dbg("[SELECT] place %d (game cursor %u)\n", s_cursor, G32(g_ecx + 0x18));
            r[3] += 4; return 1;
        }
        /* the guest may play where the others may */
        /* testing: Hornburg Courtyard (0xB) and the Tower of Orthanc (0xD)
         * count as finished, as far as the game asks; nothing is written */
        if (id == 14 && phase == 0 && s_test_unlock) {
            uint32_t level = G32(r[3] + 4) & 0xFFu, hero = G32(r[3] + 8);
            if ((level == 0xB || level == 0xD) && hero >= 1 && hero <= 3) { r[0] = 1; r[3] += 4; return 1; }
        }
        if (id == 14 && phase == 0 && s_guest >= 0) {
            uint32_t level = G32(r[3] + 4), hero = G32(r[3] + 8), h, best = 0;
            if (hero == 4 && level < 15) {
                for (h = 1; h <= 4; h++) { uint32_t v = G8(0x4AB68Fu + h * 15 + level); if (v > best) best = v; }
                r[0] = best; r[3] += 4; return 1;
            }
        }
        /* the save after Isildur's finish of the Tower of Orthanc: one more */
        /* (1.9.1) counted once the save is written, on the save written: the
         * save screen's list opens every save's game.xsv, and the count went
         * to the last one listed, not the one played */
        if (id == 12 && phase == 0 && s_orth_pending) { s_orth_pending = 0; s_orth_saving = 1; s_xsv_at_save = s_xsv_opens; }
        /* (counted as the save's game.xsv is opened for writing: wins_saved) */
        /* saving: Isildur's records in hero 4 while the save is written */
        if (id == 12 && phase == 0 && s_rec_owner >= 0) { s_rec_saving_owner = s_rec_owner; rec_switch(-1); }
        if (id == 12 && phase == 1 && s_rec_saving_owner >= 0) { int g = s_rec_saving_owner; s_rec_saving_owner = -2; rec_switch(g); }
        /* loading a save: hero 4 is Isildur's again */
        if (id == 11 && phase == 1 && getenv("TT_RELOCK")) {   /* testing: a save that has not finished the game */
            uint32_t h;
            for (h = 1; h <= 4; h++) { G8(0x4AB68Fu + h * 15 + 0xB) = 0; G8(0x4AB68Fu + h * 15 + 0xD) = 0; }
        }
        if (id == 11 && phase == 0) s_orth_pending = s_orth_saving = 0;
        if (id == 11 && phase == 1 && s_read_key[0]) {   /* a save loaded (its game.xsv the last read): its count */
            snprintf(s_loaded_key, sizeof s_loaded_key, "%s", s_read_key);
            snprintf(s_save_key, sizeof s_save_key, "%s", s_read_key);
            s_isildur_wins = wins_read(s_read_key);
            tt_dbg("[HEROES] save %s loaded: Isildur has finished the Tower of Orthanc %d time(s)\n", s_read_key, s_isildur_wins);
        }
        if (id == 11 && phase == 1) { s_rec_owner = -1; s_guest = -1; s_cursor = -1; if (s_nname >= 0) name_set("Isildur"); if (s_upn_n >= 0) upn_set(-1); }
    }

    if (!log_on()) return 0;
    switch (id) {
    case 1:
        if (phase == 0) {
            uint32_t ins = G32(g_ecx + 0x30), k;
            char s[400];
            int n = sprintf(s, "[CREATE] ctx %08X slot %u op %04X:", g_ecx, G32(g_ecx + 0x178), G16(ins));
            for (k = 1; k < 24; k++) n += sprintf(s + n, " %08X", G32(ins + 4 * k));
            tt_dbg("%s\n", s);
        }
        break;
    case 3:
        if (phase == 0) {
            char nm[96];
            int i;
            uint32_t p = G32(g_esp + 4);
            for (i = 0; i < 95 && p && G8(p + i) >= 32 && G8(p + i) < 127; i++) nm[i] = (char)G8(p + i);
            nm[i] = 0;
            tt_dbg("[LOADER] %s kind %u hero %u\n", nm, G32(g_esp + 8), G8(HERO_BYTE));
        }
        break;
    case 5:
        if (phase == 0) tt_dbg("[HERO] set %u\n", G32(g_esp + 4));
        break;
    case 2:
        if (phase == 1 && s_in_wop) tt_dbg("[WEAPON]   -> %08X\n", r[0]);
        if (phase == 0 && s_in_wop) tt_dbg("[WEAPON]   lookup type %u key %08X\n", G32(g_esp + 4), G32(g_esp + 8));
        break;
    case 21:
        if (phase == 1 && wop_record(g_ecx) == 0) {
            uint32_t rec = g_ecx + 0x188, k;
            for (k = 0; k < 4; k++) { uint32_t w = G32(rec + 0xbc + 4 * k); if (w) tt_dbg("[WEAPON]   player slot %u: obj %08X mesh %08X tex %08X\n", k, w, G32(w + 0x584), G32(w + 0x598)); }
        }
        if (phase == 0) {
            uint32_t ins = G32(g_ecx + 0x30), k;
            char t[300];
            int m = sprintf(t, "[WEAPON] ctx %08X rec %d op %04X:", g_ecx, wop_record(g_ecx), G16(ins));
            for (k = 1; k < 12; k++) m += sprintf(t + m, " %08X", G32(ins + 4 * k));
            tt_dbg("%s\n", t);
        }
        break;
    case 17:
        if (phase == 0) tt_dbg("[FEACTOR] create(%08X, %08X, %08X, %08X)\n", G32(g_esp + 4), G32(g_esp + 8), G32(g_esp + 12), G32(g_esp + 16));
        else tt_dbg("[FEACTOR]   -> %d\n", (int)r[0]);
        break;
    default: break;
    }
    return 0;
}

/* for the ECTS HUD mod: who hero 4 is now */
int tt_heroes_guest(void)
{
    return (tt_heroes_on() && G8(HERO_BYTE) == 4 && s_guest >= 0 && s_guest < N_EXTRA) ? s_guest : -1;
}
const char *tt_heroes_guest_name(int g)
{
    return g >= 0 && g < N_EXTRA ? s_skins[g].name : "";
}
/* testing (TT_HITMSG; the wrapper is a test build's): a hit message a
 * character receives -- the player's: its type, where it struck relative to
 * him (along his facing, to his side, up) and the victim's place */
void tt_hitmsg(uint32_t victim, uint32_t msg)
{
    static int on = -1;
    uint32_t pl = G32(0x43B15Cu), att;
    float fx, fy, sx, sy, dx, dy;
    if (on < 0) on = getenv("TT_HITMSG") ? 1 : 0;
    if (!on || msg < 0x10000u || msg >= 0x84000000u || pl < 0x80000000u) return;
    att = G32(msg + 0x1C);
    if (victim == pl || att == pl || (att >= 0x80000000u && att < 0x84000000u && G32(att) == pl)) tt_dbg("[HITMSG0] victim %08X type %u att %08X pl %08X\n", victim, G32(msg + 0x18), att, pl);
    if (att != pl) return;
    fx = GF(pl + 0x70); fy = GF(pl + 0x74); sx = GF(pl + 0x60); sy = GF(pl + 0x64);
    dx = GF(msg + 0x20) - GF(pl + 0xF0); dy = GF(msg + 0x24) - GF(pl + 0xF4);
    {
        float vx = GF(victim + 0xF0) - GF(pl + 0xF0), vy = GF(victim + 0xF4) - GF(pl + 0xF4);
        tt_dbg("[HITMSG] type %u w4 %08X w40 %08X b44 %u | at fwd %.2f side %.2f up %.2f | dir %.2f %.2f %.2f | victim fwd %.2f side %.2f\n",
               G32(msg + 0x18), G32(msg + 4), G32(msg + 0x40), G8(msg + 0x44), dx * fx + dy * fy, dx * sx + dy * sy, GF(msg + 0x28) - GF(pl + 0xF8),
               GF(msg + 0x30), GF(msg + 0x34), GF(msg + 0x38), vx * fx + vy * fy, vx * sx + vy * sy);
    }
}
/* testing (TT_HITMSG): a kick's hit message (types 9, 10): the code
 * addresses on the stack (who sent it) */
void tt_hitbt(uint32_t msg, uint32_t sp)
{
    static int on = -1, n;
    uint32_t k, t;
    char b[400]; int l = 0;
    if (on < 0) on = getenv("TT_HITMSG") ? 1 : 0;
    if (!on || n > 60 || msg < 0x10000u || msg >= 0x84000000u) return;
    t = G32(msg + 0x18);
    for (k = 0; k < 8 && sp > 0x10000u && sp < 0x84000000u && l < 360; k++) { l += snprintf(b + l, sizeof b - l, "%X ", G32(sp + 4)); sp = G32(sp); }
    n++;
    tt_dbg("[HITBT] type %u att %08X: %s\n", t, G32(msg + 0x1C), b);
}
/* testing (TT_SNDLOG; the wrapper is a test build's): the sound ids played */
void tt_sndid(uint32_t mgr, uint32_t id)
{
    static int on = -1;
    if (on < 0) on = getenv("TT_SNDLOG") ? 1 : 0;
    if (on) tt_dbg("[SND] t %u mgr %08X id %u cursor %d\n", (unsigned)GetTickCount(), mgr, id, s_cursor);
    if (on && getenv("TT_SNDMEM")) {   /* testing: the entry the id finds, and its data */
        uint32_t n = G32(mgr + 0x287C), t = G32(mgr + 0x2880), i;
        for (i = 0; i < n && i < 600; i++) if (G32(t + 8 * i) == id) break;
        if (i < n) {
            uint32_t e = mgr + 0x78 + 20 * i, base = G32(mgr + 0x24);
            tt_dbg("[SND]   pos %u fmt %08X off %08X len %u base %08X cnt %u data %08X %08X\n", i, G32(e), G32(e + 4), G32(e + 8), base, G32(mgr + 0x34),
                   G32(e + 4) < 0x10000000u ? G32(base + G32(e + 4)) : G32(G32(e + 4)), G32(e + 4) < 0x10000000u ? G32(base + G32(e + 4) + 4) : G32(G32(e + 4) + 4));
        }
    }
}

/* co-op (menus.c, player 2's turn of the character select): the ring's places, and the skin a place stands for
 * (NULL: a retail hero, 0 Aragorn 1 Legolas 2 Gimli 3 Isildur); its title as the game shows it */
int tt_select_places(void) { return places(); }
int tt_coop_skin_on(void) { return s_coop_skin && s_playas >= 0; }   /* (co-op) player 2's skin went into this level */
const char *tt_select_place_skin(int p)
{
    static const char *const retail[4] = { "aragorn", "legolas", "gimli", "isildur" };
    int g = place_guest(p);
    if (g >= 0 && g < N_EXTRA) return s_skins[g].name;
    return p >= 0 && p < 4 ? retail[p] : NULL;
}
const char *tt_select_place_title(int p)
{
    static const char *const retail[4] = { "Aragorn", "Legolas", "Gimli", "Isildur" };
    int g = place_guest(p);
    if (g >= 0 && g < N_EXTRA) return s_skins[g].title;
    return p >= 0 && p < 4 ? retail[p] : "?";
}
/* (co-op) the guests player 2 can reach in the ring: unlocked ones */
int tt_select_guests_open(void) { int p, n = 0; for (p = 4; p < places(); p++) if (!place_locked(p)) n++; return n; }

/* ---- co-op: the Co-op menu's background (UI\stills3.BFF key 2, Aragorn, Gimli and Legolas on the
 * plains; a 640 x 448 linear 32-bit still) as the Options background's texture takes it: 512 x 256, swizzled 32-bit
 * (options.c, draw_coopmenu), darkened for the menu's text. out: 512 * 256 words, alpha full. */
int tt_coop_menu_bg_pixels(uint32_t *out)
{
    char hp[600]; unsigned char *f; size_t fl; uint32_t off, end, ty, *src; int x, y;
    if (!host_path("\\Device\\CdRom0\\UI\\stills3.BFF", hp, sizeof hp) || !(f = read_all(hp, &fl))) return 0;
    if (!res_find(f, fl, 2, 350, &off, &end, &ty) || rd32(f, off + 4) != 640 || rd32(f, off + 8) != 448 || off + 0x80 + 640 * 448 * 4 > fl) { free(f); return 0; }
    src = (uint32_t *)malloc(640 * 448 * 4);
    if (!src) { free(f); return 0; }
    for (y = 0; y < 448; y++) for (x = 0; x < 640; x++) src[y * 640 + x] = rd32(f, off + 0x80 + (size_t)(y * 640 + x) * 4) & 0xFFFFFFu;
    free(f);
    for (y = 0; y < 256; y++)
        for (x = 0; x < 512; x++)
        {   /* darkened (the menu's light text on the bright sky could not be read): 0.5, a little less at the right */
            uint32_t c = bilerp(src, 640, 448, (x + 0.5f) * 640.0f / 512.0f - 0.5f, (y + 0.5f) * 448.0f / 256.0f - 0.5f);
            float k = 0.45f + 0.15f * (float)x / 511.0f;
            out[swz_index((uint32_t)x, (uint32_t)y, 512, 256)] = 0xFF000000u | (uint32_t)(((c >> 16) & 255) * k) << 16 | (uint32_t)(((c >> 8) & 255) * k) << 8 | (uint32_t)((c & 255) * k);
        }
    free(src);
    return 1;
}

/* (1.9.7, Versus) player 1's hero by name: the hero byte (1-4); an extra hero is hero 4 with that guest (as the
 * character select's ring sets him, cursor_changed), Isildur hero 4 with none */
int tt_heroes_p1_pick(const char *name)
{
    static const char *const retail[4] = { "aragorn", "legolas", "gimli", "isildur" };
    int k, g;
    s_vsp1 = -1;
    for (k = 0; k < 4; k++) if (name && !_stricmp(name, retail[k])) {
        if (s_upn_n > 0 || tt_lang_on()) upn_set(-1);   /* (1.9.9) a retail hero: the game's own upgrade names (Aragorn's are the strings hero 4's guests write over -- a guest
                                                         * picked before left his on them) */
        if (k == 3 || G32(0x1BB2A4u) == 0) { s_guest = -1; rec_switch(-1); name_set("Isildur"); upn_set(-1); }
        if (k < 3 && G32(0x1BB2A4u) == 0 && !getenv("TT_VS_NOP1SKIN")) {   /* Mount Doom: Isildur's level -- hero 4 there, wearing the pick */
            s_vsp1 = skin_by_name(name);
            tt_dbg("[HEROES] Versus on Mount Doom: player 1 is %s (hero 4, his skin built into the level)\n", name);
            return 4;
        }
        if (k >= 1 && G32(0x1BB2A4u) == 1 && tt_heroes_on() && !getenv("TT_VS_NOP1SKIN")) {   /* (1.9.8) Weathertop has only Aragorn: hero 4 there, as an extra hero is, wearing the pick */
            s_guest = -1; rec_switch(-1); name_set(s_skins[skin_by_name(name)].title); upn_set(-1);
            s_vsp1 = skin_by_name(name);
            tt_dbg("[HEROES] Versus on Weathertop: player 1 is %s (hero 4, his skin built into the level)\n", name);
            return 4;
        }
        return k + 1;
    }
    g = skin_by_name(name);
    if (g < 0 || g >= N_EXTRA) return 1;
    s_guest = g; rec_switch(g); name_set(s_skins[g].title); upn_set(g);
    tt_dbg("[HEROES] Versus: player 1 is %s (hero 4's guest)\n", s_skins[g].name);
    return 4;
}

/* (1.9.7, Versus) a picture of the player's own beside the exe (versus\<file>, a 24- or 32-bit BMP) as a 512 x 256
 * swizzled 32-bit texture's pixels, the whole picture stretched over it, darkened by k (1 = as it is) */
int tt_vs_image_pixels(const char *file, uint32_t *out, float k)
{
    char p[700]; int w, h, x, y; uint32_t *src;
    paths();
    snprintf(p, sizeof p, "%s/versus/%s", s_dir, file);
    if (!(src = bmp_load_argb(p, &w, &h, NULL))) { tt_dbg("[VS] picture %s: not found or not a 24/32-bit BMP\n", p); return 0; }
    for (y = 0; y < 256; y++) for (x = 0; x < 512; x++) {
        uint32_t c = bilerp(src, w, h, (x + 0.5f) * (float)w / 512.0f - 0.5f, (y + 0.5f) * (float)h / 256.0f - 0.5f);
        out[swz_index((uint32_t)x, (uint32_t)y, 512, 256)] = 0xFF000000u | (uint32_t)(((c >> 16) & 255) * k) << 16 | (uint32_t)(((c >> 8) & 255) * k) << 8 | (uint32_t)((c & 255) * k);
    }
    free(src);
    tt_dbg("[VS] picture %s: %d x %d\n", p, w, h);
    return 1;
}
int tt_host_path(const char *xbox_path, char *out, size_t n) { return host_path(xbox_path, out, n); }   /* (versus.c) */
/* (1.9.9, Versus) the level whose sound bank a skin's own sounds are in (its resources' level: "00ISUL1", ...), or NULL */
int tt_heroes_skin_bank(const char *skin, char *bank, size_t bn)
{
    int g = skin_by_name(skin); const char *p, *e; size_t n;
    if (g < 0 || !s_skins[g].src || !(p = strrchr(s_skins[g].src, '\\')) || !(e = strchr(++p, '.'))) return 0;
    n = (size_t)(e - p); if (n + 1 > bn) return 0;
    memcpy(bank, p, n); bank[n] = 0;
    return 1;
}
