/* achieve.c -- (1.9.7) achievements: earned as you play, noted in the bottom right corner (the F10 panel's toast) and
 * listed on the F10 panel's Achievements page. Kept in achievements.txt beside the game (one id a line, with when);
 * only on this PC, nothing is sent anywhere.
 *
 * What counts (watched from here and from the Versus code):
 *  - a level won: the end-of-level screen (UI\endlvl.BFF) opening after the level's world (Worlds\<code>.bf?) -- the
 *    game shows it only for a level won (heroes.c's file redirect hands us every file the game opens);
 *  - what the saves already hold: each hero's levels done (0x4AB69E + hero * 15, 2 or more: done) and his level
 *    (0x4AB60C + hero byte * 0x348, +1), read now and then;
 *  - Versus (twotowers_manual.c): rounds and matches won, as whom, the shield orc's shield broken. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#else
extern unsigned long GetModuleFileNameA(void *m, char *p, unsigned long n);
extern unsigned long GetTickCount(void);
#endif
#include "../../xboxrecomp-0.12.0/src/video/gfx_menu.h"

extern void tt_dbg(const char *fmt, ...);
extern ptrdiff_t xbox_GetMemoryOffset(void);
#define A8(a)  (*(volatile uint8_t *)(xbox_GetMemoryOffset() + (uint32_t)(a)))
#define A32(a) (*(volatile uint32_t *)(xbox_GetMemoryOffset() + (uint32_t)(a)))

typedef struct { const char *id, *name, *desc; } Ach;
/* the levels, by the game's index (0x1BB2A4), the world file's code and their names */
static const struct { const char *code, *name; } s_lv[14] = {
    { "00isul1", "Mount Doom" }, { "01weat1", "Weathertop" }, { "02gate2", "The Gates of Moria" }, { "04balin1", "Balin's Tomb" },
    { "13amon1", "Amon Hen" }, { "14fang2", "Fangorn Forest" }, { "16rohan2", "The Plains of Rohan" }, { "16rohan1", "The Westfold" },
    { "16rohan3", "The Road to Helm's Deep" }, { "17top1", "The Deeping Wall" }, { "18hole1", "The Breached Wall" },
    { "20yard1", "The Hornburg Courtyard" }, { "21orth1", "Orthanc" }, { "21orth2", "Orthanc" } };
#define N_STORY 12
static const Ach s_ach[] = {
    /* the story: each level, then all of them, then the secret one */
    { "lv_00isul1", "The Last Alliance", "Win the prologue on the slopes of Mount Doom." },
    { "lv_01weat1", "Fire on Weathertop", "Drive the Ringwraiths from Weathertop." },
    { "lv_02gate2", "Speak Friend", "Escape the Watcher at the Gates of Moria." },
    { "lv_04balin1", "Drums in the Deep", "Fight your way out of Balin's Tomb." },
    { "lv_13amon1", "Breaking of the Fellowship", "Stand against the Uruk-hai at Amon Hen." },
    { "lv_14fang2", "Into the Forest", "Get through Fangorn Forest." },
    { "lv_16rohan2", "Riders of Rohan", "Win the battle on the Plains of Rohan." },
    { "lv_16rohan1", "The Burning of the Westfold", "Save the villages of the Westfold." },
    { "lv_16rohan3", "Wolves on the Road", "Reach Helm's Deep along the road." },
    { "lv_17top1", "The Deeping Wall", "Hold the Deeping Wall." },
    { "lv_18hole1", "The Wall Is Breached", "Fight on when the wall is breached." },
    { "lv_20yard1", "The Hornburg Holds", "Defend the Hornburg's courtyard to the dawn." },
    { "story", "The Two Towers", "Win every level of the story." },
    { "secret", "The Tower of Orthanc", "Win the secret mission at Orthanc." },
    /* the heroes */
    { "hero_aragorn", "Ranger of the North", "Win a level as Aragorn." },
    { "hero_legolas", "Prince of Mirkwood", "Win a level as Legolas." },
    { "hero_gimli", "Son of Gloin", "Win a level as Gimli." },
    { "hero_isildur", "Heir of Elendil", "Win a level after the prologue as Isildur." },
    { "three_hunters", "The Three Hunters", "Win levels as Aragorn, Legolas and Gimli." },
    { "extra_hero", "An Unexpected Ally", "Win a level as one of the extra heroes." },
    { "extra_all", "A Fellowship Remade", "Unlock every extra hero." },
    { "level5", "Seasoned Warrior", "Bring a hero to level 5." },
    { "level10", "Legend of the Third Age", "Bring a hero to level 10." },
    /* together */
    { "coop_level", "Side by Side", "Win a level in co-op." },
    { "coop_wall", "Shoulder to Shoulder", "Hold the Deeping Wall in co-op." },
    /* Versus */
    { "vs_round", "First Blood", "Win a round of Versus." },
    { "vs_match", "Champion of the Arena", "Win a Deathmatch." },
    { "vs_flawless", "Untouched", "Win a round of Versus without taking a blow." },
    { "vs_close", "Last Breath", "Win a round of Versus with less than a tenth of your health left." },
    { "vs_shield", "Bearer of the Shield", "Win a match of Find the Shield." },
    { "vs_nazgul", "The Black Breath", "Win a Versus match as the Nazgul." },
    { "vs_orc", "Iron and Leather", "Win a Versus match as the Shield Orc." },
    { "vs_eomer", "Third Marshal of the Riddermark", "Win a Versus match as Eomer." },
    { "vs_shieldbreak", "Shield-breaker", "Break the Shield Orc's shield." },
    /* (1.9.7) Versus, harder */
    { "vs_streak_p1", "Unbroken Line", "As player 1, win 5 Versus rounds in a row." },
    { "vs_streak_p2", "The Challenger Rises", "As player 2, win 5 Versus rounds in a row." },
    { "vs_kicks", "Get Back!", "Knock your opponent back 50 times in Versus." },
    { "vs_arrows", "Eagle Eye", "Land 40 locked-on arrows on your opponent in Versus." },
    { "vs_pickups", "Scavenger", "Take 30 pickups in Versus." },
    { "vs_perfect", "None Shall Pass", "Win a Versus match without losing a round." },
    { "vs_comeback", "Against All Hope", "Win a Versus match after trailing by 3 rounds." },
    { "vs_rounds100", "Veteran of the Arena", "Win 100 Versus rounds in all." },
    { "vs_arenas", "Wanderer", "Win a Versus match on 8 different arenas." },
    { "vs_heroes", "Many Faces", "Win a Versus match as 10 different heroes." },
    /* (1.9.9) Troll Battle */
    { "troll_win1", "Troll Brawler", "Win a Troll Battle match." },
    { "troll_win3", "Terror of Moria", "Win 3 Troll Battle matches." },
    { "troll_win10", "Lord of the Deep", "Win 10 Troll Battle matches." },
    { "troll_flawless", "Thick Hide", "Win a Troll Battle match without losing a round." },
    { "troll_smash", "Bone Crusher", "Land a single smash of 400 damage or more in Troll Battle." },
    { "troll_damage", "Troll Rampage", "Deal 25,000 damage in Troll Battle, across all your matches." },
    { "troll_fast", "Swift Bludgeon", "Win a Troll Battle round in under 20 seconds." },
    { "troll_logs", "Rock and Log", "Land 25 thrown rocks or logs on the other troll in Troll Battle." },
    /* (1.9.9) Versus with the levels' enemies */
    { "vs_foes25", "Orc-Slayer", "Slay 25 orcs and Uruk-hai during Versus matches." },
    { "vs_foes150", "Scourge of Isengard", "Slay 150 orcs and Uruk-hai during Versus matches." },
    { "vs_foe_arenas", "War on Two Fronts", "Win a Deathmatch on all five arenas with enemies (Amon Hen, Fangorn, Plains of Rohan, Breached Wall, Hornburg)." },
    /* (1.9.9) the heroes, 3 matches each */
    { "vs3_saruman", "The White Hand", "Win 3 Versus matches as Saruman." },
    { "vs3_ent", "Shepherd of the Trees", "Win 3 Versus matches as the Ent." },
    { "vs3_urukcap", "Captain of Isengard", "Win 3 Versus matches as the Uruk Captain." },
    { "vs3_elvenwarrior", "The Last Alliance Returns", "Win 3 Versus matches as the Elven Warrior." },
    { "vs3_lurtz", "Hunt Them Down", "Win 3 Versus matches as Lurtz." },
    { "vs3_sauron", "The Dark Lord", "Win 3 Versus matches as Sauron." },
    { "vs3_gandalf", "Servant of the Secret Fire", "Win 3 Versus matches as Gandalf." },
    { "vs3_elrond", "Lord of Rivendell", "Win 3 Versus matches as Elrond." },
    { "vs3_boromir", "Captain of Gondor", "Win 3 Versus matches as Boromir." },
    { "vs3_frodo", "Ring-bearer", "Win 3 Versus matches as Frodo." },
    { "vs3_uruk", "Blood Frenzy", "Win 3 Versus matches as the Berserker." },
    { "vs_ent_rocks", "Hurl the Stones", "As the Ent, land 20 thrown rocks on your opponent in Versus." },
    { "vs_mirror", "Mirror Image", "Win a match of first to 5 or more where both players are the same hero." },
    /* Impossible */
    { "imp_level", "Defiance", "Win a level on Impossible." },
    { "imp_five", "Hardened", "Win 5 different levels on Impossible." },
    { "imp_all", "DIAMOND: The Unbreakable", "Win every level of the story on Impossible. Very few ever will." },
};
#define N_ACH ((int)(sizeof s_ach / sizeof s_ach[0]))
static unsigned char s_got[N_ACH];
static int s_loaded;
/* (1.9.7) running counts kept with them ("count <key> <n>" lines) and sets ("set <key> <item>") */
#define N_CNT 64
static struct { char k[24]; long v; } s_cnt[N_CNT]; static int s_ncnt;
#define N_SET 96
static struct { char k[16], it[24]; } s_set[N_SET]; static int s_nset;

static void ach_path(char *p, size_t n)
{
    char *sl;
    GetModuleFileNameA(NULL, p, (unsigned long)n - 32);
    sl = strrchr(p, '\\'); if (!sl) sl = strrchr(p, '/');
    if (sl) sl[1] = 0; else p[0] = 0;
    strcat(p, "achievements.txt");
}
static int find(const char *id) { int i; for (i = 0; i < N_ACH; i++) if (!strcmp(s_ach[i].id, id)) return i; return -1; }
static void load(void)
{
    char p[700], ln[160]; FILE *f;
    if (s_loaded) return;
    s_loaded = 1;
    ach_path(p, sizeof p);
    if ((f = fopen(p, "r"))) {
        while (fgets(ln, sizeof ln, f)) { char id[64], k[32], it[32]; long v; int i;
            if (sscanf(ln, "count %23s %ld", k, &v) == 2) { if (s_ncnt < N_CNT) { snprintf(s_cnt[s_ncnt].k, sizeof s_cnt[0].k, "%s", k); s_cnt[s_ncnt++].v = v; } continue; }
            if (sscanf(ln, "set %15s %23s", k, it) == 2) { if (s_nset < N_SET) { snprintf(s_set[s_nset].k, sizeof s_set[0].k, "%s", k); snprintf(s_set[s_nset].it, sizeof s_set[0].it, "%s", it); s_nset++; } continue; }
            if (sscanf(ln, "%63s", id) == 1 && (i = find(id)) >= 0) s_got[i] = 1; }
        fclose(f);
    }
}
static void save(void)
{
    char p[700]; FILE *f; int i;
    ach_path(p, sizeof p);
    if (!(f = fopen(p, "w"))) return;
    fprintf(f, "# The Two Towers Recompiled: achievements earned on this PC (an id a line)\n");
    for (i = 0; i < N_ACH; i++) if (s_got[i]) fprintf(f, "%s\n", s_ach[i].id);
    for (i = 0; i < s_ncnt; i++) fprintf(f, "count %s %ld\n", s_cnt[i].k, s_cnt[i].v);
    for (i = 0; i < s_nset; i++) fprintf(f, "set %s %s\n", s_set[i].k, s_set[i].it);
    fclose(f);
}
static int s_quiet;   /* (the first look at the saves: earned without a note each) */
/* (1.9.7) F10 > More > Achievements: Off earns none */
static int ach_on(void) { static int it = -2; extern int xbox_GfxFind(const char *key); if (it == -2) it = xbox_GfxFind("achievements"); return it < 0 || xbox_GfxGet(it) != 0; }
static long cnt_add(const char *k, long d)
{
    int i; load();
    for (i = 0; i < s_ncnt && strcmp(s_cnt[i].k, k); i++) ;
    if (i == s_ncnt) { if (s_ncnt >= N_CNT) return 0; snprintf(s_cnt[i].k, sizeof s_cnt[0].k, "%s", k); s_cnt[i].v = 0; s_ncnt++; }
    s_cnt[i].v += d; save();
    return s_cnt[i].v;
}
static int set_add(const char *k, const char *it)   /* how many in set k, it added */
{
    int i, n = 0, have = 0; load();
    for (i = 0; i < s_nset; i++) if (!strcmp(s_set[i].k, k)) { n++; if (!strcmp(s_set[i].it, it)) have = 1; }
    if (!have && s_nset < N_SET) { snprintf(s_set[s_nset].k, sizeof s_set[0].k, "%s", k); snprintf(s_set[s_nset].it, sizeof s_set[0].it, "%s", it); s_nset++; n++; save(); }
    return n;
}
void tt_ach_unlock(const char *id)
{
    int i;
    load();
    if (!ach_on()) return;
    if ((i = find(id)) < 0 || s_got[i]) return;
    s_got[i] = 1;
    save();
    tt_dbg("[ACH] earned: %s (%s)\n", s_ach[i].name, id);
    if (!s_quiet && !getenv("TT_ACH_NONOTE")) xbox_ToastShow3(!strncmp(s_ach[i].name, "DIAMOND", 7) ? "DIAMOND ACHIEVEMENT EARNED" : "Achievement earned", s_ach[i].name, s_ach[i].desc);   /* (1.9.9: what it was for, under its name) */
}
int tt_ach_has(const char *id) { int i; load(); i = find(id); return i >= 0 && s_got[i]; }

static void story_check(void)
{
    int i, n = 0;
    for (i = 0; i < N_STORY; i++) { char id[32]; snprintf(id, sizeof id, "lv_%s", s_lv[i].code); n += tt_ach_has(id); }
    if (n == N_STORY) tt_ach_unlock("story");
    if (tt_ach_has("hero_aragorn") && tt_ach_has("hero_legolas") && tt_ach_has("hero_gimli")) tt_ach_unlock("three_hunters");
}

/* the panel's page */
static int a_n(void) { load(); return N_ACH; }
static const char *a_name(int i) { return i >= 0 && i < N_ACH ? s_ach[i].name : ""; }
static const char *a_desc(int i) { return i >= 0 && i < N_ACH ? s_ach[i].desc : ""; }
static int a_got(int i) { return i >= 0 && i < N_ACH && s_got[i]; }
static void reg(void) { static int done; if (!done) { done = 1; xbox_GfxSetAchievements(a_n, a_name, a_desc, a_got); } }
#if defined(__GNUC__)
__attribute__((constructor)) static void ach_ctor(void) { reg(); }
#endif

/* a level won: the level's index (0..13), the hero byte (1 Aragorn, 2 Legolas, 3 Gimli, 4 Isildur or an extra hero),
 * the extra hero playing (-1 none), co-op or not */
static void level_won(int lv, int hb, int guest, int coop)
{
    char id[32];
    if (lv < 0 || lv > 13) return;
    if (lv >= 12) tt_ach_unlock("secret");
    else { snprintf(id, sizeof id, "lv_%s", s_lv[lv].code); tt_ach_unlock(id); }
    if (guest >= 0) tt_ach_unlock("extra_hero");
    else if (hb == 1) tt_ach_unlock("hero_aragorn");
    else if (hb == 2) tt_ach_unlock("hero_legolas");
    else if (hb == 3) tt_ach_unlock("hero_gimli");
    else if (hb == 4 && lv != 0) tt_ach_unlock("hero_isildur");
    if (coop) { tt_ach_unlock("coop_level"); if (lv == 9) tt_ach_unlock("coop_wall"); }
    {   /* (1.9.7) Impossible: each level won on it counted once */
        extern int tt_impossible(void);
        if (tt_impossible() && lv < N_STORY && ach_on()) {
            int n = set_add("imp", s_lv[lv].code);
            tt_ach_unlock("imp_level");
            if (n >= 5) tt_ach_unlock("imp_five");
            if (n >= N_STORY) tt_ach_unlock("imp_all");
            tt_dbg("[ACH] level %d won on Impossible (%d of %d)\n", lv, n, N_STORY);
        }
    }
    story_check();
}

/* every file the game opens (heroes.c redirect) */
static int s_world = -1;
void tt_ach_file(const char *xp)
{
    const char *w, *b; size_t n; int i;
    reg();
    if (!xp) return;
    n = strlen(xp);
    w = strstr(xp, "orlds\\"); if (!w) w = strstr(xp, "ORLDS\\");
    if (w && n > 4 && xp[n - 4] == '.' && (xp[n - 3] == 'b' || xp[n - 3] == 'B')) {
        char code[16]; size_t k; b = w + 6;
        for (k = 0; k < 15 && b[k] && b[k] != '.'; k++) code[k] = (char)(b[k] >= 'A' && b[k] <= 'Z' ? b[k] + 32 : b[k]);
        code[k] = 0;
        s_world = -1;
        for (i = 0; i < 14; i++) if (!strcmp(code, s_lv[i].code)) s_world = i;
        return;
    }
    if (n >= 10 && (!strcmp(xp + n - 10, "endlvl.BFF") || !strcmp(xp + n - 10, "endlvl.bff") || !strcmp(xp + n - 10, "ENDLVL.BFF"))) {
        extern int tt_heroes_guest(void); extern int tt_coop_active(void); extern int tt_vs_on(void);
        static int last_world = -2; static unsigned long last_t;
        unsigned long now = GetTickCount();
        if (s_world < 0 || tt_vs_on()) return;
        if (s_world == last_world && now - last_t < 20000u) return;   /* (the game opens it more than once) */
        last_world = s_world; last_t = now;
        tt_dbg("[ACH] level %d won (hero %u, guest %d, co-op %d)\n", s_world, A8(0x4AB69Du), tt_heroes_guest(), tt_coop_active());
        level_won(s_world, A8(0x4AB69Du), tt_heroes_guest(), tt_coop_active());
    }
}

/* now and then (the main menu's frame): what the saves hold -- each hero's levels done and his level; the extra
 * heroes unlocked */
void tt_ach_tick(void)
{
    static unsigned long last; static int first = 1;
    unsigned long now = GetTickCount();
    int h, lv;
    reg();
    if (now - last < 3000u) return;
    last = now;
    load();
    {   /* (Versus: its new game and the top level it gives both players are not progress) */
        extern int tt_vs_on(void); extern volatile int tt_vs_menu_on;
        if (tt_vs_on() || tt_vs_menu_on) return;
    }
    s_quiet = first;   /* (the first look: what an old save holds, without a note for each) */
    for (h = 0; h < 4; h++) for (lv = 0; lv < 14; lv++) {
        uint8_t v = A8(0x4AB69Eu + (uint32_t)h * 15u + (uint32_t)lv);
        if (v >= 2 && v < 16) {
            char id[32];
            if (lv >= 12) tt_ach_unlock("secret"); else { snprintf(id, sizeof id, "lv_%s", s_lv[lv].code); tt_ach_unlock(id); }
            if (h == 0) tt_ach_unlock("hero_aragorn"); else if (h == 1) tt_ach_unlock("hero_legolas"); else if (h == 2) tt_ach_unlock("hero_gimli"); else if (lv != 0) tt_ach_unlock("hero_isildur");
        }
    }
    for (h = 1; h <= 4; h++) {
        uint32_t l = A32(0x4AB60Cu + (uint32_t)h * 0x348u) + 1u;
        if (getenv("TT_ACHLOG")) tt_dbg("[ACH] hero %d: level %u\n", h, l);
        if (l >= 5 && l < 100) tt_ach_unlock("level5");
        if (l >= 10 && l < 100) tt_ach_unlock("level10");
    }
    { extern int tt_heroes_unlocked_count(void); if (tt_heroes_unlocked_count() >= 7) tt_ach_unlock("extra_all"); }
    story_check();
    s_quiet = 0; first = 0;
}

/* Versus: a round won (winner 0/1, his health left and most, the damage he took in the round), a match won (as whom,
 * the mode) */
void tt_ach_vs_round(int win, int hp, int maxhp, int taken)
{
    if (win < 0) return;
    tt_ach_unlock("vs_round");
    if (taken <= 0) tt_ach_unlock("vs_flawless");
    if (maxhp > 0 && hp > 0 && hp * 10 < maxhp) tt_ach_unlock("vs_close");
}
void tt_ach_vs_match(const char *skin, int shield_mode)
{
    tt_ach_unlock(shield_mode ? "vs_shield" : "vs_match");
    if (skin && !strcmp(skin, "nazgul")) tt_ach_unlock("vs_nazgul");
    if (skin && !strcmp(skin, "orc")) tt_ach_unlock("vs_orc");
    if (skin && !strcmp(skin, "eomer")) tt_ach_unlock("vs_eomer");
}

/* (1.9.7) Versus' events: 6 a match begins; 1 a round won by who (0/1, the scores after it in a, b); 2 a knock back
 * landed by who; 3 a locked arrow landed; 4 a pickup taken; 5 a match won by who as skin (a: arena level) */
static int s_vs_streak[2], s_vs_deficit[2];
void tt_ach_vs_event(int kind, int who, int a, int b, const char *skin)
{
    if (!ach_on() || who < 0 || who > 1) { if (kind == 6) { s_vs_streak[0] = s_vs_streak[1] = 0; s_vs_deficit[0] = s_vs_deficit[1] = 0; } return; }
    switch (kind) {
    case 6: s_vs_streak[0] = s_vs_streak[1] = 0; s_vs_deficit[0] = s_vs_deficit[1] = 0; break;
    case 1: {
        int me = who ? b : a, them = who ? a : b;   /* (a: player 1's score, b: player 2's) */
        s_vs_streak[who]++; s_vs_streak[who ^ 1] = 0;
        if (s_vs_streak[who] >= 5) tt_ach_unlock(who ? "vs_streak_p2" : "vs_streak_p1");
        if (them - me > s_vs_deficit[who]) s_vs_deficit[who] = them - me;
        if (me - them > s_vs_deficit[who ^ 1] && 0) ;
        if (cnt_add("vs_rounds", 1) >= 100) tt_ach_unlock("vs_rounds100");
        break; }
    case 2: if (cnt_add("vs_kicks", 1) >= 50) tt_ach_unlock("vs_kicks"); break;
    case 3: if (cnt_add("vs_arrows", 1) >= 40) tt_ach_unlock("vs_arrows"); break;
    case 4: if (cnt_add("vs_pickups", 1) >= 30) tt_ach_unlock("vs_pickups"); break;
    case 5: {
        char ar[8];
        if (b == 0) tt_ach_unlock("vs_perfect");
        if (s_vs_deficit[who] >= 3) tt_ach_unlock("vs_comeback");
        snprintf(ar, sizeof ar, "%d", a);
        if (set_add("vsarena", ar) >= 8) tt_ach_unlock("vs_arenas");
        if (skin && set_add("vshero", skin) >= 10) tt_ach_unlock("vs_heroes");
        break; }
    }
}
/* (1.9.9) Versus' newer events (twotowers_manual.c): a match won with more about it, a round won with its time, a hit
 * landed, an enemy slain */
static const char *const s_vs3[] = { "saruman", "ent", "urukcap", "elvenwarrior", "lurtz", "sauron", "gandalf", "elrond", "boromir", "frodo", "uruk" };
void tt_ach_vs_match_ex(int who, const char *skin, const char *other, int goal, int lost, int troll, int arena)
{
    size_t i; char k[24];
    if (!ach_on() || who < 0 || who > 1 || !skin) return;
    if (troll) {
        long n = cnt_add("troll_wins", 1);
        tt_ach_unlock("troll_win1"); if (n >= 3) tt_ach_unlock("troll_win3"); if (n >= 10) tt_ach_unlock("troll_win10");
        if (lost == 0) tt_ach_unlock("troll_flawless");
        return;
    }
    for (i = 0; i < sizeof s_vs3 / sizeof s_vs3[0]; i++) if (!strcmp(skin, s_vs3[i])) {
        snprintf(k, sizeof k, "vsw_%s", skin);
        if (cnt_add(k, 1) >= 3) { snprintf(k, sizeof k, "vs3_%s", skin); tt_ach_unlock(k); } }
    if (other && !strcmp(skin, other) && goal >= 5) tt_ach_unlock("vs_mirror");
    if (arena == 4 || arena == 5 || arena == 6 || arena == 10 || arena == 11) { char ar[8]; snprintf(ar, sizeof ar, "%d", arena); if (set_add("vsfoear", ar) >= 5) tt_ach_unlock("vs_foe_arenas"); }
}
void tt_ach_vs_round_ex(int who, unsigned ms, int troll) { if (ach_on() && who >= 0 && troll && ms > 0 && ms < 20000u) tt_ach_unlock("troll_fast"); }
void tt_ach_vs_hit(int who, int dmg, int ranged, const char *skin, int troll)
{
    if (!ach_on() || who < 0 || who > 1 || dmg <= 0) return;
    if (troll) {
        if (dmg >= 400) tt_ach_unlock("troll_smash");
        if (cnt_add("troll_dmg", dmg) >= 25000) tt_ach_unlock("troll_damage");
        if (ranged && cnt_add("troll_logs", 1) >= 25) tt_ach_unlock("troll_logs");
    }
    if (ranged && skin && !strcmp(skin, "ent") && cnt_add("ent_rocks", 1) >= 20) tt_ach_unlock("vs_ent_rocks");
}
void tt_ach_vs_foe_kill(int who) { long n; if (!ach_on() || who < 0 || who > 1) return; n = cnt_add("vs_foes", 1); if (n >= 25) tt_ach_unlock("vs_foes25"); if (n >= 150) tt_ach_unlock("vs_foes150"); }
/* (1.9.7) the deficit before a round: the trailing player's (called with the scores before each round) */
void tt_ach_vs_scores(int s1, int s2) { if (s2 - s1 > s_vs_deficit[0]) s_vs_deficit[0] = s2 - s1; if (s1 - s2 > s_vs_deficit[1]) s_vs_deficit[1] = s1 - s2; }
