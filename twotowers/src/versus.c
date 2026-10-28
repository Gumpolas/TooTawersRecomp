/* Versus: sounds of our own (a WAV file beside the exe, in the folder "versus"), played through the APU's
 * software mixer on top of the game's own sound. Used for the round's start ("FIGHT!", versus/fight.wav). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "../../xboxrecomp-0.12.0/src/apu/apu.h"
#if defined(_WIN32)
#include <windows.h>
#else
extern unsigned long GetModuleFileNameA(void *m, char *p, unsigned long n);
#include <time.h>
static unsigned long vs_ticks(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (unsigned long)t.tv_sec * 1000ul + (unsigned long)(t.tv_nsec / 1000000); }
#endif
#if defined(_WIN32)
static unsigned long vs_ticks(void) { return GetTickCount(); }
#endif
extern void tt_dbg(const char *fmt, ...);
static int strncasecmp_vs(const char *a, const char *b) { while (*b) { int x = *a++, y = *b++; if (x >= 'a' && x <= 'z') x -= 32; if (y >= 'a' && y <= 'z') y -= 32; if (x != y) return 1; } return 0; }

typedef struct { char name[32]; int16_t *pcm; uint32_t bytes, ch, rate; int slot, tried; } VsSnd;
static VsSnd s_snd[16]; static int s_nsnd;

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static int load_wav(VsSnd *s)
{
    char path[600], *sl; FILE *f; long n; uint8_t *b; uint32_t o, fmt = 0, ch = 0, rate = 0, bits = 0; const uint8_t *data = 0; uint32_t dlen = 0;
    GetModuleFileNameA(NULL, path, 500);
    sl = strrchr(path, '\\'); if (!sl) sl = strrchr(path, '/'); if (sl) sl[1] = 0;
    strcat(path, "versus"); strcat(path, sl && *sl == '\\' ? "\\" : "/"); strcat(path, s->name);
    f = fopen(path, "rb"); if (!f) { tt_dbg("[VS] sound %s: not found\n", path); return 0; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = (uint8_t *)malloc((size_t)n); if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return 0; }
    fclose(f);
    if (n < 44 || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) { tt_dbg("[VS] sound %s: not a WAV file\n", path); free(b); return 0; }
    for (o = 12; o + 8 <= (uint32_t)n; ) {
        uint32_t len = rd32(b + o + 4);
        if (!memcmp(b + o, "fmt ", 4) && len >= 16) { fmt = rd16(b + o + 8); ch = rd16(b + o + 10); rate = rd32(b + o + 12); bits = rd16(b + o + 22);
            if (fmt == 0xFFFE && len >= 40) fmt = rd16(b + o + 32); }
        if (!memcmp(b + o, "data", 4)) { data = b + o + 8; dlen = len; if (o + 8 + dlen > (uint32_t)n) dlen = (uint32_t)n - o - 8; }
        o += 8 + len + (len & 1);
    }
    if (!data || !ch || ch > 2 || !rate || !((fmt == 1 && (bits == 16 || bits == 8 || bits == 24)) || (fmt == 3 && bits == 32))) {
        tt_dbg("[VS] sound %s: unsupported (format %u, %u channels, %u bits)\n", path, fmt, ch, bits); free(b); return 0; }
    {   uint32_t ns = dlen / (bits / 8), i; int16_t *p = (int16_t *)malloc(ns * 2 + 2);
        for (i = 0; i < ns; i++) {
            int v;
            if (bits == 16) v = (int16_t)rd16(data + 2 * i);
            else if (bits == 8) v = ((int)data[i] - 128) << 8;
            else if (bits == 24) v = (int16_t)rd16(data + 3 * i + 1);
            else { float fv; memcpy(&fv, data + 4 * i, 4); v = (int)(fv * 32767.0f); if (v > 32767) v = 32767; if (v < -32768) v = -32768; }
            p[i] = (int16_t)v;
        }
        s->pcm = p; s->bytes = ns * 2; s->ch = ch; s->rate = rate;
    }
    free(b);
    tt_dbg("[VS] sound %s: %u Hz, %u channels, %.2f s\n", path, s->rate, s->ch, (double)s->bytes / 2 / s->ch / s->rate);
    return 1;
}

/* (1.9.9) Troll Battle's own sound if there is one (versus/troll_<name>), else the usual (versus/<name>) */
extern int tt_vs_troll_mode(void);
static int wav_present(const char *name)
{
    int i; for (i = 0; i < s_nsnd; i++) if (!strcmp(s_snd[i].name, name)) return s_snd[i].pcm != 0;
    if (s_nsnd >= 16) return 0;
    { VsSnd *s = &s_snd[s_nsnd++]; memset(s, 0, sizeof *s); snprintf(s->name, sizeof s->name, "%s", name); s->slot = -1; s->tried = 1; load_wav(s); return s->pcm != 0; }
}
void tt_vs_play_wav(const char *name, float vol);
void tt_vs_play_mode_wav(const char *name, float vol)
{
    char t[64];
    if (tt_vs_troll_mode()) { snprintf(t, sizeof t, "troll_%s", name); if (wav_present(t)) { tt_vs_play_wav(t, vol); return; } }
    tt_vs_play_wav(name, vol);
}
/* (1.9.9) Troll Battle's records, kept beside the game (troll_records.txt): the biggest smash, the fastest round, the
 * most damage in a match -- each with who set it (P1/P2) and when */
typedef struct { long v; char who[4]; char when[12]; } TrollRec;
static TrollRec s_trec[3]; static int s_trec_loaded;
static void trec_path(char *p, size_t n)
{
    char *sl; GetModuleFileNameA(NULL, p, (unsigned long)n - 32);
    sl = strrchr(p, '\\'); if (!sl) sl = strrchr(p, '/');
    if (sl) sl[1] = 0; else p[0] = 0;
    strcat(p, "troll_records.txt");
}
static void trec_load(void)
{
    char p[700], ln[160]; FILE *f; int k;
    if (s_trec_loaded) return; s_trec_loaded = 1;
    trec_path(p, sizeof p);
    if (!(f = fopen(p, "r"))) return;
    while (fgets(ln, sizeof ln, f)) { long v; char w[4], d[12];
        if (sscanf(ln, "%d %ld %3s %11s", &k, &v, w, d) == 4 && k >= 0 && k < 3) { s_trec[k].v = v; snprintf(s_trec[k].who, 4, "%s", w); snprintf(s_trec[k].when, 12, "%s", d); } }
    fclose(f);
}
static void trec_save(void)
{
    char p[700]; FILE *f; int k;
    trec_path(p, sizeof p);
    if (!(f = fopen(p, "w"))) return;
    for (k = 0; k < 3; k++) if (s_trec[k].v) fprintf(f, "%d %ld %s %s\n", k, s_trec[k].v, s_trec[k].who, s_trec[k].when);
    fclose(f);
}
/* a match's best: kind 0 biggest smash, 1 fastest round (ms; lower wins), 2 most damage; 1 if it is a new record */
int tt_troll_record(int kind, long v, int who)
{
    TrollRec *r; time_t t = time(NULL); struct tm *tm = localtime(&t);
    if (kind < 0 || kind > 2 || v <= 0) return 0;
    trec_load(); r = &s_trec[kind];
    if (r->v && (kind == 1 ? v >= r->v : v <= r->v)) return 0;
    r->v = v; snprintf(r->who, 4, "P%d", who + 1);
    if (tm) snprintf(r->when, 12, "%02d/%02d/%02d", tm->tm_mon + 1, tm->tm_mday, tm->tm_year % 100); else snprintf(r->when, 12, "-");
    trec_save();
    tt_dbg("[VS] Troll Battle record %d: %ld by %s\n", kind, v, r->who);
    return 1;
}
long tt_troll_record_get(int kind, const char **who, const char **when)
{
    trec_load(); if (kind < 0 || kind > 2) return 0;
    if (who) *who = s_trec[kind].who; if (when) *when = s_trec[kind].when;
    return s_trec[kind].v;
}
/* plays versus/<name> once (at volume 0..1); nothing if the file is missing */
void tt_vs_play_wav(const char *name, float vol)
{
    VsSnd *s = 0; APUMixerVoice *v; int i;
    for (i = 0; i < s_nsnd; i++) if (!strcmp(s_snd[i].name, name)) s = &s_snd[i];
    if (!s) { if (s_nsnd >= 16) return; s = &s_snd[s_nsnd++]; memset(s, 0, sizeof *s); snprintf(s->name, sizeof s->name, "%s", name); s->slot = -1; }
    if (!s->tried) { s->tried = 1; load_wav(s); }
    if (!s->pcm) return;
    if (s->slot < 0) s->slot = apu_mixer_alloc_voice();
    if (s->slot < 0) return;
    v = apu_mixer_get_voice(s->slot); if (!v) return;
    apu_mixer_stop(s->slot);
    v->pcm_data = s->pcm; v->pcm_bytes = s->bytes; v->num_channels = s->ch; v->sample_rate = s->rate; v->volume = vol;
    apu_mixer_set_position(s->slot, 0);
    apu_mixer_play(s->slot, 0);
}

/* the arena's music: another level's, loaded in place of its streaming sound bank (lang.c's redirect) and started
 * once play begins (tt_vs_music_track) */
extern int tt_vs_on(void);
static const struct { const char *arena, *donor, *track; } s_music[] = {
    { "00ISUL1", "20YARD1", "20Y1ALT1_L" }, { "01WEAT1", "13AMON1", "13AMALT1_L" }, { "02GATE2", "14FANG2", "14F2ALT1_L" },
    { "04BALIN1", "18HOLE1", "18H1ALT2_L" }, { "13AMON1", "04BALIN1", "04B1ALT1_L" }, { "14FANG2", "17TOP1", "17T1BASE_L" },
    { "16ROHAN2", "02GATE2", "02G2GAL1_L" }, { "16ROHAN3", "16ROHAN1", "16R1ALT1_L" }, { "17TOP1", "16ROHAN3", "16R3ALT2_L" },
    { "18HOLE1", "00ISUL1", "00I1BASE_L" }, { "20YARD1", "16ROHAN2", "16R2ALT2_L" }, { "21ORTH1", "01WEAT1", "01WEALT1_L" },
    { "21ORTH2", "16ROHAN2", "16R2ALT5_L" } };
/* (1.9.9) a track picked at random for each match (any level's but the arena's own; TT_VS_FIXEDMUSIC: the arena's pairing
 * above): chosen when the arena's bank is redirected at its load, kept for its streams */
static char s_mpick_arena[16]; static int s_mpick = -1;
static int music_i(const char *level)
{
    int i, n = (int)(sizeof s_music / sizeof s_music[0]), own = -1; if (!level) return -1;
    for (i = 0; i < n; i++) if (!strncasecmp_vs(level, s_music[i].arena)) own = i;
    if (own < 0 || getenv("TT_VS_FIXEDMUSIC")) return own;
    if (own >= 0 && getenv("TT_VS_MUSIDX")) return atoi(getenv("TT_VS_MUSIDX")) % n;   /* (test) */
    if (s_mpick < 0 || strncasecmp_vs(level, s_mpick_arena)) {
        static unsigned seed; int k;
        seed = seed * 1103515245u + 12345u + (unsigned)vs_ticks();
        for (k = 0; k < 16; k++) { i = (int)((seed >> 8) % (unsigned)n);
            if (strncasecmp_vs(s_music[i].donor, level) && strcmp(s_music[i].donor, "14FANG2") && strcmp(s_music[i].donor, "01WEAT1")) break;   /* (1.9.9: not Fangorn's and Weathertop's tracks -- quiet for long stretches,
                                                                                                                                    * Fangorn's 20 s at a time: "make sure music plays on the map", a playtest on Mount Doom) */
            seed = seed * 1103515245u + 12345u; }
        s_mpick = i; snprintf(s_mpick_arena, sizeof s_mpick_arena, "%s", s_music[own].arena);
        { extern void tt_dbg(const char *, ...); tt_dbg("[VS] music for %s this match: %s's \"%s\"\n", level, s_music[i].donor, s_music[i].track); }
    }
    return s_mpick;
}
static int s_music_off(void) { return getenv("TT_VS_NOMUSIC") != NULL; }
/* (1.9.9) an arena with the level's enemies (twotowers_manual.c, vs_keep_enemy) keeps its own sound banks: the orcs'
 * and Uruk-hai's cries are in them, and with another level's music in their place the enemies were silent (a
 * playtest, Amon Hen). Its own music, then */
static const char *vs_own_music(const char *level)
{
    static const struct { const char *lv, *track; } own[] = { { "13AMON1", "13AMALT1_L" }, { "14FANG2", "14F2ALT1_L" }, { "16ROHAN2", "16R2ALT2_L" }, { "18HOLE1", "18H1ALT2_L" }, { "20YARD1", "20Y1ALT1_L" } };
    extern int tt_vs_troll_mode(void); size_t i;
    if (!level || getenv("TT_VS_NOENEMIES") || getenv("TT_VS_SWAPMUSIC") || tt_vs_troll_mode()) return NULL;
    for (i = 0; i < sizeof own / sizeof own[0]; i++) if (!strncasecmp_vs(level, own[i].lv)) return own[i].track;
    return NULL;
}
const char *tt_vs_music_donor(const char *level)
{   /* (called as the arena's banks open at its load: a new pick for each load, WB and SB a moment apart) */
    static unsigned long last; int i; if (!tt_vs_on() || s_music_off()) return NULL;
    if (vs_ticks() - last > 10000u) s_mpick = -1;
    last = vs_ticks(); if (vs_own_music(level)) { extern void tt_dbg(const char *, ...); tt_dbg("[VS] music for %s: its own (its enemies' sounds are in its banks)\n", level); return NULL; }
    i = music_i(level); return i >= 0 ? s_music[i].donor : NULL;
}
const char *tt_vs_music_track(const char *level) { int i; if (vs_own_music(level) && !getenv("TT_VS_TRACK")) return vs_own_music(level); i = music_i(level); if (getenv("TT_VS_TRACK")) return getenv("TT_VS_TRACK"); return i >= 0 ? s_music[i].track : NULL; }

/* the Options' ring (the interface wave bank's entries 11 and 12, the cues 0xA9D / 0xA9E the Options play left and
 * right), read from the player's own game (sound\WB_INTRFACE.xwb: a version 1 XACT wave bank, entries of 20 bytes at
 * 0x50 -- format (rate << 5 | 3: Xbox ADPCM, mono), offset, length -- the data after them) and decoded once into a
 * stereo voice: in a level the interface bank's sounds are not to be counted on */
extern int tt_host_path(const char *xbox_path, char *out, size_t n);
static const int8_t s_adx[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };
static const int16_t s_ads[89] = { 7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767 };
static uint32_t xadpcm(const uint8_t *d, uint32_t len, int16_t *out, uint32_t max)
{
    uint32_t o, n = 0; int k;
    for (o = 0; o + 36 <= len && n + 65 <= max; o += 36) {
        int pred = (int16_t)rd16(d + o), idx = d[o + 2] > 88 ? 88 : d[o + 2];
        out[n++] = (int16_t)pred;
        for (k = 0; k < 64; k++) {
            int nib = (d[o + 4 + k / 2] >> ((k & 1) * 4)) & 15, st = s_ads[idx], df = st >> 3;
            if (nib & 1) df += st >> 2; if (nib & 2) df += st >> 1; if (nib & 4) df += st; if (nib & 8) df = -df;
            pred += df; if (pred > 32767) pred = 32767; if (pred < -32768) pred = -32768;
            idx += s_adx[nib]; if (idx < 0) idx = 0; if (idx > 88) idx = 88;
            out[n++] = (int16_t)pred;
        }
    }
    return n;
}
static int load_ring(VsSnd *s)
{
    char p[900]; FILE *f; long n; uint8_t *b; uint32_t cnt, last = 0, base, i, f11, o11, l11, o12, l12, n1, n2, m;
    int16_t *a, *c;
    if (!tt_host_path("\\Device\\CdRom0\\sound\\WB_INTRFACE.xwb", p, sizeof p) || !(f = fopen(p, "rb"))) return 0;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = (uint8_t *)malloc((size_t)n); if (!b || n < 0x400 || fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return 0; }
    fclose(f);
    cnt = rd32(b + 12);
    if (memcmp(b, "WBND", 4) || cnt < 13 || cnt > 256 || 0x50 + 20 * cnt > (uint32_t)n) { free(b); return 0; }
    for (i = 0; i < cnt; i++) { uint32_t e = rd32(b + 0x50 + 20 * i + 4) + rd32(b + 0x50 + 20 * i + 8); if (e > last) last = e; }
    base = (uint32_t)n - last;
    f11 = rd32(b + 0x50 + 20 * 11); o11 = rd32(b + 0x50 + 20 * 11 + 4); l11 = rd32(b + 0x50 + 20 * 11 + 8);
    o12 = rd32(b + 0x50 + 20 * 12 + 4); l12 = rd32(b + 0x50 + 20 * 12 + 8);
    if ((f11 & 3) != 3 || base + o11 + l11 > (uint32_t)n || base + o12 + l12 > (uint32_t)n) { free(b); return 0; }
    a = (int16_t *)malloc((l11 / 36 + 1) * 65 * 2); c = (int16_t *)malloc((l12 / 36 + 1) * 65 * 2);
    n1 = xadpcm(b + base + o11, l11, a, (l11 / 36 + 1) * 65); n2 = xadpcm(b + base + o12, l12, c, (l12 / 36 + 1) * 65);
    m = n1 > n2 ? n1 : n2;
    s->pcm = (int16_t *)malloc(m * 4);
    for (i = 0; i < m; i++) { s->pcm[2 * i] = i < n1 ? a[i] : 0; s->pcm[2 * i + 1] = i < n2 ? c[i] : 0; }
    s->bytes = m * 4; s->ch = 2; s->rate = f11 >> 5;
    free(a); free(c); free(b);
    tt_dbg("[VS] the Options' ring from %s: %u Hz, %.2f s\n", p, s->rate, (double)m / s->rate);
    return 1;
}
/* the countdown's tick: the Options' ring; 1 if it played */
int tt_vs_play_ring(void)
{
    static VsSnd s; static int tried; APUMixerVoice *v;
    if (!tried) { tried = 1; s.slot = -1; if (!load_ring(&s)) tt_dbg("[VS] the Options' ring could not be read: the game's own cue\n"); }
    if (!s.pcm) return 0;
    if (s.slot < 0) s.slot = apu_mixer_alloc_voice();
    if (s.slot < 0 || !(v = apu_mixer_get_voice(s.slot))) return 0;
    apu_mixer_stop(s.slot);
    v->pcm_data = s.pcm; v->pcm_bytes = s.bytes; v->num_channels = s.ch; v->sample_rate = s.rate; v->volume = 0.9f;
    apu_mixer_set_position(s.slot, 0); apu_mixer_play(s.slot, 0);
    return 1;
}

/* a stream of the arena's own (its name starts with the arena level's code, "04B1ALT1" in Balin's Tomb) while its bank
 * is the donor's: the donor's track (without its "_L", which the game adds) */
const char *tt_vs_music_track_for_stream(const char *stream)
{
    static char out[32]; extern uint32_t tt_vs_level_index(void); static const char *codes[14] = { "00I", "01WE", "02G", "04B", "13AM", "14F", "16R2", "16R1", "16R3", "17T", "18H", "20Y", "21O", "21O" };
    static const char *lvl[14] = { "00ISUL1", "01WEAT1", "02GATE2", "04BALIN1", "13AMON1", "14FANG2", "16ROHAN2", "16ROHAN1", "16ROHAN3", "17TOP1", "18HOLE1", "20YARD1", "21ORTH1", "21ORTH2" };
    uint32_t li = tt_vs_level_index(); int i; const char *t; size_t n;
    if (!tt_vs_on() || s_music_off() || li > 13 || !stream) return NULL;
    (void)codes;
    if (stream[0] < '0' || stream[0] > '9') return NULL;   /* (a level's own streams start with its number; "EYE_..." is the game's) */
    {   static const char *kinds[] = { "ALT", "BASE", "PAL", "GAL", "GEX", "PEX", "EXP", "CAV", "HOLE", "TAG", "PST", "END" }; size_t k; int m = 0;
        for (k = 0; k < sizeof kinds / sizeof kinds[0] && !m; k++) if (strstr(stream, kinds[k])) m = 1;
        if (!m) return NULL; }   /* (music only: the ambience, "AMB", is not the track) */
    if (vs_own_music(lvl[li])) {   /* (its own banks: its own streams; the match's start, which asks with "0ALT", its own track) */
        if (strcmp(stream, "0ALT")) return NULL;
        t = vs_own_music(lvl[li]); n = strlen(t); if (n > 2 && !strcmp(t + n - 2, "_L")) n -= 2; if (n >= sizeof out) n = sizeof out - 1;
        memcpy(out, t, n); out[n] = 0; return out; }
    i = music_i(lvl[li]); if (i < 0) return NULL;
    t = s_music[i].track; n = strlen(t); if (n > 2 && !strcmp(t + n - 2, "_L")) n -= 2;
    if (n >= sizeof out) n = sizeof out - 1;
    memcpy(out, t, n); out[n] = 0;
    return out;
}

/* (Versus) the heroes' voices, of our own: a blow taken, a fall. The game's own were seldom heard in a match (its few
 * voices for the characters' sounds went to the swings and steps). From the game's level banks (sound\<level>.tbl
 * names each wave's id by its place in sound\WB_<level>.xwb), decoded once each, through the software mixer */
extern int tt_heroes_voice_src(const char *skin, int slot, char *bank, size_t bn, uint32_t *id, float *pitch);
typedef struct { char bank[16]; uint32_t id; int16_t *pcm; uint32_t n, rate; } VsVox;
static VsVox s_vox[160]; static int s_nvox;
static VsVox *vox_get(const char *bank, uint32_t id)
{
    char p[900], xp[128]; FILE *f; long n; uint8_t *b = 0, *t = 0; uint32_t cnt, last = 0, base, i, k, idx = ~0u, tn; VsVox *v; int j;
    for (j = 0; j < s_nvox; j++) if (s_vox[j].id == id && !strcmp(s_vox[j].bank, bank)) return s_vox[j].pcm ? &s_vox[j] : 0;
    if (s_nvox >= 160) return 0;
    v = &s_vox[s_nvox++]; memset(v, 0, sizeof *v); snprintf(v->bank, sizeof v->bank, "%s", bank); v->id = id;
    snprintf(xp, sizeof xp, "\\Device\\CdRom0\\sound\\%s.tbl", bank);
    if (!tt_host_path(xp, p, sizeof p) || !(f = fopen(p, "rb"))) return 0;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET); t = (uint8_t *)malloc((size_t)n + 1);
    if (!t || fread(t, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(t); return 0; } fclose(f);
    tn = n >= 8 ? rd32(t + 4) : 0;
    for (k = 0; k < tn && 8 + 8 * k + 4 <= (uint32_t)n; k++) if (rd32(t + 8 + 8 * k) == id) { idx = k; break; }
    free(t);
    if (idx == ~0u) return 0;
    snprintf(xp, sizeof xp, "\\Device\\CdRom0\\sound\\WB_%s.xwb", bank);
    if (!tt_host_path(xp, p, sizeof p) || !(f = fopen(p, "rb"))) return 0;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET); b = (uint8_t *)malloc((size_t)n);
    if (!b || n < 0x100 || fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return 0; } fclose(f);
    cnt = rd32(b + 12);
    if (memcmp(b, "WBND", 4) || idx >= cnt || 0x50 + 20 * cnt > (uint32_t)n) { free(b); return 0; }
    for (i = 0; i < cnt; i++) { uint32_t e = rd32(b + 0x50 + 20 * i + 4) + rd32(b + 0x50 + 20 * i + 8); if (e > last) last = e; }
    base = (uint32_t)n - last;
    {   uint32_t fm = rd32(b + 0x50 + 20 * idx), off = rd32(b + 0x50 + 20 * idx + 4), len = rd32(b + 0x50 + 20 * idx + 8);
        if ((fm & 3) != 3 || ((fm >> 2) & 7) != 0 || base + off + len > (uint32_t)n) { free(b); return 0; }   /* (Xbox ADPCM, mono) */
        v->pcm = (int16_t *)malloc((len / 36 + 1) * 65 * 2);
        v->n = xadpcm(b + base + off, len, v->pcm, (len / 36 + 1) * 65); v->rate = (fm >> 5) * (getenv("TT_VS_VOXRATE1") ? 1u : 2u);   /* (1.9.7: the bank's rate field is half the rate the game plays them at -- measured, the same voice an octave apart: every hero sounded like an orc) */
        {   uint32_t q; int pk = 0; double e = 0; for (q = 0; q < v->n; q++) { int a = abs(v->pcm[q]); if (a > pk) pk = a; e += (double)v->pcm[q] * v->pcm[q]; }
            tt_dbg("[VS] voice %s %u decoded: %u samples at %u Hz, peak %d, rms %.0f\n", bank, id, v->n, v->rate, pk, v->n ? sqrt(e / v->n) : 0.0); }
    }
    free(b);
    return v;
}
int tt_vs_voice(const char *who, int slot, float vol);
/* (1.9.7) a sound of a hero's own motions in a match (0xC3B90 from the motions' events, 0x85B90): his voice played as
 * his own (each hero's from his own level's bank -- the motions of the extra heroes and of player 2, a copy of player 1,
 * asked for the retail heroes' voices, so everyone grunted alike), his blows and swings from the arena's bank through
 * the port's mixer at full strength (the game's, placed at him with the camera far off, were barely heard). 0: not
 * ours (the game plays it). TT_VS_GAMESOUNDS: the game's as before */
extern int tt_heroes_voice_slot(uint32_t id);
int tt_vs_hero_sound(const char *skin, uint32_t id, float vol)
{
    static int slots[8] = { -1, -1, -1, -1, -1, -1, -1, -1 }, next;
    static const char *lvl[14] = { "00ISUL1", "01WEAT1", "02GATE2", "04BALIN1", "13AMON1", "14FANG2", "16ROHAN2", "16ROHAN1", "16ROHAN3", "17TOP1", "18HOLE1", "20YARD1", "21ORTH1", "21ORTH2" };
    extern uint32_t tt_vs_level_index(void); uint32_t li = tt_vs_level_index(); int slot, s; VsVox *x; APUMixerVoice *v;
    if (getenv("TT_VS_GAMESOUNDS") || !skin) return 0;
    if (vol < 0.05f) vol = 0.05f; if (vol > 1.0f) vol = 1.0f;
    slot = tt_heroes_voice_slot(id);
    if (slot >= 0) return tt_vs_voice(skin, slot, 0.55f + 0.45f * vol);
    {   /* (1.9.9) the cave troll's own: his motions are the hero's (their sounds the sword's whoosh, 183-190); his club's
         * blow is his Balin's Tomb sounds' -- the slam (926) and the crash (930) his own attacks play there */
        static unsigned n; uint32_t tid = 0;
        if (!strcmp(skin, "troll") && id >= 183u && id <= 190u && getenv("TT_VS_TROLLWHOOSH"))   /* (1.9.9: the whoosh his own; the slam after it, tt_troll_slam_tick) */ { tid = 930u; (void)n; }
        if (tid && (x = vox_get("04BALIN1", tid))) { li = 3; goto play; }
    }
    if (li > 13 || !(x = vox_get(lvl[li], id))) {   /* (1.9.9) not in the arena's bank: his home level's (the orc's prologue, ...) */
        extern int tt_heroes_skin_bank(const char *skin, char *bank, size_t bn); static char hb[16];
        if (!tt_heroes_skin_bank(skin, hb, sizeof hb) || !(x = vox_get(hb, id))) return 0;
    }
play:
    s = next++ & 7;
    if (slots[s] < 0) slots[s] = apu_mixer_alloc_voice();
    if (slots[s] < 0 || !(v = apu_mixer_get_voice(slots[s]))) return 0;
    apu_mixer_stop(slots[s]);
    v->pcm_data = x->pcm; v->pcm_bytes = x->n * 2; v->num_channels = 1; v->sample_rate = x->rate;
    v->volume = (getenv("TT_VS_HEROSFX") ? (float)atof(getenv("TT_VS_HEROSFX")) : 1.0f) * vol * (id == 3000u ? 0.75f : 1.0f);   /* (3000: his steps, a little softer; 1.9.9: 0.75, was 0.4 -- "way too quiet") */
    apu_mixer_set_position(slots[s], 0); apu_mixer_play(slots[s], 0);
    { static int n; if (n++ < 30) tt_dbg("[VS] %s's sound %u from %s (%.2f s)\n", skin, id, lvl[li], (double)x->n / x->rate); }
    return 1;
}
/* (1.9.9) a wave of a streaming bank by its name (sound\S_WB_<level>.xwb, XWB v2: the bank's segment at +0x08, entries at
 * +0x10 of 24 bytes -- flags, format (2 bits tag, 1 Xbox ADPCM; 3 bits channels; 26 bits rate), offset, length --, names at
 * +0x18, 64 bytes each, the waves at +0x20), mono Xbox ADPCM only; decoded once */
static VsVox *vox_get_named(const char *xwb, const char *name)
{
    char p[900], xp[128], key[16]; FILE *f; uint8_t hd[0x30], *nm = 0, *en = 0, *w = 0; uint32_t cnt, esz, nsz, i, idx = ~0u; VsVox *v; int j;
    snprintf(key, sizeof key, "%.15s", name);
    for (j = 0; j < s_nvox; j++) if (s_vox[j].id == 0xFFFFFFFEu && !strcmp(s_vox[j].bank, key)) return s_vox[j].pcm ? &s_vox[j] : 0;
    if (s_nvox >= 160) return 0;
    v = &s_vox[s_nvox++]; memset(v, 0, sizeof *v); snprintf(v->bank, sizeof v->bank, "%s", key); v->id = 0xFFFFFFFEu;
    snprintf(xp, sizeof xp, "\\Device\\CdRom0\\sound\\%s.xwb", xwb);
    if (!tt_host_path(xp, p, sizeof p) || !(f = fopen(p, "rb"))) return 0;
    if (fread(hd, 1, sizeof hd, f) != sizeof hd || memcmp(hd, "WBND", 4) || rd32(hd + 4) != 2u) { fclose(f); return 0; }
    {   uint8_t bd[0x24]; fseek(f, (long)rd32(hd + 8), SEEK_SET); if (fread(bd, 1, sizeof bd, f) != sizeof bd) { fclose(f); return 0; }
        cnt = rd32(bd + 4); esz = rd32(bd + 0x18); nsz = rd32(bd + 0x1C); }
    if (!cnt || cnt > 4096 || esz < 16 || nsz < 8 || nsz > 256) { fclose(f); return 0; }
    nm = (uint8_t *)malloc(cnt * nsz); en = (uint8_t *)malloc(cnt * esz);
    fseek(f, (long)rd32(hd + 0x18), SEEK_SET); if (!nm || fread(nm, 1, cnt * nsz, f) != cnt * nsz) goto fail;
    fseek(f, (long)rd32(hd + 0x10), SEEK_SET); if (!en || fread(en, 1, cnt * esz, f) != cnt * esz) goto fail;
    for (i = 0; i < cnt; i++) if (!strncmp((const char *)nm + i * nsz, name, nsz)) { idx = i; break; }
    if (idx == ~0u) goto fail;
    {   uint32_t fm = rd32(en + idx * esz + 4), off = rd32(en + idx * esz + 8), len = rd32(en + idx * esz + 12);
        if ((fm & 3) != 1 || ((fm >> 2) & 7) != 1 || len > 8u * 1024 * 1024) goto fail;   /* (Xbox ADPCM, mono) */
        w = (uint8_t *)malloc(len); fseek(f, (long)(rd32(hd + 0x20) + off), SEEK_SET); if (!w || fread(w, 1, len, f) != len) goto fail;
        v->pcm = (int16_t *)malloc((len / 36 + 1) * 65 * 2);
        v->n = xadpcm(w, len, v->pcm, (len / 36 + 1) * 65); v->rate = (fm >> 5) & 0x3FFFFFFu;
        {   /* at most 4.5 s of it (the groans run 7 to 14 s), its last 0.8 s faded out */
            uint32_t mx = v->rate * 9u / 2u, fd = v->rate * 4u / 5u, q;
            if (v->n > mx && fd < mx) { v->n = mx; for (q = 0; q < fd; q++) v->pcm[mx - fd + q] = (int16_t)((int32_t)v->pcm[mx - fd + q] * (int32_t)(fd - q) / (int32_t)fd); } }
        tt_dbg("[VS] voice %s's %s decoded: %u samples at %u Hz\n", xwb, name, v->n, v->rate); }
    free(nm); free(en); free(w); fclose(f); return v;
fail:
    free(nm); free(en); free(w); fclose(f); tt_dbg("[VS] voice %s's %s: not found or not mono Xbox ADPCM\n", xwb, name); return 0;
}
/* who: a skin name ("aragorn", "nazgul", ...); slot as tt_heroes_voice_src; 1 if it played */
int tt_vs_voice(const char *who, int slot, float vol)
{
    static int slots[4] = { -1, -1, -1, -1 }, next; char bank[16]; uint32_t id; float pitch; VsVox *x; APUMixerVoice *v; int s;
    if (getenv("TT_VS_NOVOICES")) return 0;
    if (who && !strcmp(who, "ent")) {   /* (1.9.9) the Ent: Fangorn Forest's own Ent groans (its streaming bank's Ent_01-03), hurt (slots 4-6) and
        * falling (7); long, so one at a time */
        static uint32_t until; uint32_t now = (uint32_t)vs_ticks(); static const char *nm[3] = { "Ent_01", "Ent_02", "Ent_03" };
        if (slot < 4 || now < until) return 0;
        if (!(x = vox_get_named("S_WB_14FANG2", nm[slot == 7 ? 2 : (slot - 4) % 2]))) return 0;
        until = now + (uint32_t)(1000.0 * x->n / (x->rate ? x->rate : 44100)) - 600u; pitch = 1.0f; snprintf(bank, sizeof bank, "%s", nm[slot == 7 ? 2 : (slot - 4) % 2]); id = 0;
        goto have;
    }
    if (!tt_heroes_voice_src(who, slot, bank, sizeof bank, &id, &pitch) || !(x = vox_get(bank, id))) return 0;
have:
    s = next++ & 3;
    if (slots[s] < 0) slots[s] = apu_mixer_alloc_voice();
    if (slots[s] < 0 || !(v = apu_mixer_get_voice(slots[s]))) return 0;
    apu_mixer_stop(slots[s]);
    v->pcm_data = x->pcm; v->pcm_bytes = x->n * 2; v->num_channels = 1; v->sample_rate = (uint32_t)((float)x->rate * pitch); v->volume = vol;
    apu_mixer_set_position(slots[s], 0); apu_mixer_play(slots[s], 0);
    { static int n; if (n++ < 20) tt_dbg("[VS] voice: %s slot %d (%s %u, %.2f s)\n", who, slot, bank, id, (double)x->n / x->rate); }
    return 1;
}
