/* credits_music.c -- (1.9.9) the credits' second song. The credits play one song (streamed from the interface's
 * streaming wave bank, sound\S_WB_INTRFACE.xwb) and outlast it, so the game started it again from the top until the
 * credits ended. With music\credits2.wav beside the game (16-bit PCM, mono or stereo, any rate), that plays instead
 * (and music\credits3.wav after it, if there is one)
 * once the first song has ended -- on, round again if the credits still go on -- until the credits end.
 *
 * How:
 *  - the credits' scroll (twotowers_manual.c, 0x3F9A0 from 0x710E1) says each frame that the credits are on;
 *  - the game's file reads (xbox_read_observer) show the song streamed from the wave bank: when it was read from its
 *    start, and when its reads go back to the top (the song starting over);
 *  - its end (its start plus its length from the bank's entry) is when ours begins: the finished mix
 *    (xbox_apu_post_mix) is ours from then on, as loud as the game's song came out of the mixer (the music volume) --
 *    until the credits end, when the game's sound is its own again.
 * TT_CREDITS2=<file>: another file. TT_CREDITS2_LOG: what it does, in the log. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#if defined(_WIN32)
#include <windows.h>
#else
extern unsigned long GetModuleFileNameA(void *m, char *p, unsigned long n);
extern unsigned long GetTickCount(void);
#endif

extern void tt_dbg(const char *fmt, ...);
extern int tt_host_path(const char *xbox_path, char *out, size_t n);
extern void (*xbox_read_observer)(uint32_t caller, long long offset, uint32_t length);
extern void (*xbox_apu_post_mix)(int16_t *samples, int n);

#define NENT 16
static struct { uint32_t start, len; unsigned dur_ms; } s_ent[NENT];
static int s_nent = -1;
static volatile unsigned long s_last;          /* the credits' last frame */
static volatile long long s_lastoff = -1;
static volatile unsigned long s_t0;           /* the streaming song's start (read from its top) */
static volatile int s_deep;                    /* read well into it since */
static volatile int s_entry = -1;
static volatile int s_phase;                   /* 0 the first song, 1 it has gone round (ours at s_switch_at), 2 ours */
static volatile unsigned long s_switch_at, s_session_t;
static int16_t *s_pcm;                         /* ours: stereo, 48 kHz */
static size_t s_frames, s_pos;
static int s_load_tried;
static volatile int s_ready;
static volatile unsigned s_nreads, s_nmix;     /* (what was seen while the credits were on: for the log) */                   /* ours loaded (on a thread of its own) */
static double s_sumsq; static unsigned long long s_cnt;
static float s_gain = 1.0f;
static int s_log = -1;

static int lg(void) { if (s_log < 0) s_log = getenv("TT_CREDITS2_LOG") != NULL; return s_log; }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* the streaming bank's songs: where each is in the file, how long it plays (an XACT version 2 wave bank: its segments
 * at 8, the entries' at 16 -- 24 bytes each: flags, format, offset, length --, the data's at 32) */
static void bank_load(void)
{
    char p[900]; FILE *f; uint8_t h[0x800]; uint32_t eo, el, d0, i;
    if (s_nent >= 0) return;
    s_nent = 0;
    if (!tt_host_path("\\Device\\CdRom0\\sound\\S_WB_INTRFACE.xwb", p, sizeof p) || !(f = fopen(p, "rb"))) return;
    if (fread(h, 1, sizeof h, f) != sizeof h) { fclose(f); return; }
    fclose(f);
    if (memcmp(h, "WBND", 4)) return;
    eo = rd32(h + 16); el = rd32(h + 20); d0 = rd32(h + 32);
    for (i = 0; i < el / 24 && i < NENT && eo + 24 * i + 24 <= sizeof h; i++) {
        uint32_t fmt = rd32(h + eo + 24 * i + 4), ch = (fmt >> 2) & 7, rate = (fmt >> 5) & 0x3FFFF, len = rd32(h + eo + 24 * i + 12);
        s_ent[i].start = d0 + rd32(h + eo + 24 * i + 8); s_ent[i].len = len;
        s_ent[i].dur_ms = ((fmt & 3) == 1 && ch && rate) ? (unsigned)((double)(len / (36u * ch)) * 64.0 * 1000.0 / rate) : 0u;
        s_nent = (int)i + 1;
    }
    if (lg()) for (i = 0; i < (uint32_t)s_nent; i++) tt_dbg("[CREDITS2] bank song %u: %08X + %08X, %u ms\n", i, s_ent[i].start, s_ent[i].len, s_ent[i].dur_ms);
}

/* a song from its file: 16-bit PCM WAV, made stereo at 48 kHz and put after what is there */
static int wav_add(const char *p)
{
    FILE *f; long n; uint8_t *b; uint32_t o = 12, ch = 0, rate = 0, bits = 0, fmt = 0, dlen = 0; const uint8_t *data = NULL;
    if (!(f = fopen(p, "rb"))) { tt_dbg("[CREDITS2] no %s\n", p); return 0; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = (uint8_t *)malloc((size_t)(n > 0 ? n : 1));
    if (!b || n < 44 || fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return 0; }
    fclose(f);
    if (memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) { tt_dbg("[CREDITS2] %s: not a WAV file\n", p); free(b); return 0; }
    while (o + 8 <= (uint32_t)n) {
        uint32_t cl = rd32(b + o + 4);
        if (!memcmp(b + o, "fmt ", 4) && cl >= 16) { fmt = b[o + 8] | b[o + 9] << 8; ch = b[o + 10] | b[o + 11] << 8; rate = rd32(b + o + 12); bits = b[o + 22] | b[o + 23] << 8; }
        else if (!memcmp(b + o, "data", 4)) { data = b + o + 8; dlen = cl; if (o + 8 + dlen > (uint32_t)n) dlen = (uint32_t)n - o - 8; break; }
        o += 8 + cl + (cl & 1);
    }
    if (!data || (fmt != 1 && fmt != 0xFFFE) || bits != 16 || (ch != 1 && ch != 2) || rate < 8000 || rate > 192000) {
        tt_dbg("[CREDITS2] %s: not 16-bit PCM, mono or stereo (format %u, %u bits, %u channels)\n", p, fmt, bits, ch); free(b); return 0;
    }
    {
        size_t in = dlen / (2 * ch), out = (size_t)((double)in * 48000.0 / rate), i;
        const int16_t *s = (const int16_t *)data;
        int16_t *np = (int16_t *)realloc(s_pcm, (s_frames + out) * 4 + 4), *d;
        if (!np) { free(b); return 0; }
        s_pcm = np; d = s_pcm + s_frames * 2;
        if (rate == 48000 && ch == 2) memcpy(d, s, out * 4);   /* (as it is played: copied) */
        else for (i = 0; i < out; i++) {
            double t = (double)i * rate / 48000.0; size_t k = (size_t)t; double fr = t - (double)k; int c;
            if (k + 1 >= in) { k = in ? in - 1 : 0; fr = 0; }
            for (c = 0; c < 2; c++) {
                int cc = ch == 2 ? c : 0;
                double v = s[k * ch + cc] * (1.0 - fr) + (k + 1 < in ? s[(k + 1) * ch + cc] : 0) * fr;
                d[i * 2 + c] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
            }
        }
        s_frames += out;
        tt_dbg("[CREDITS2] a song for the credits: %s (%.1f s)\n", p, out / 48000.0);
    }
    free(b);
    return 1;
}
/* ours: music\credits2.wav, then music\credits3.wav after it (the credits still on when the second ends), the two round
 * again if the credits outlast them both. TT_CREDITS2 / TT_CREDITS3: other files */
static void song_load(void)
{
    char dir[900], p[1000], *sl; const char *e;
    int k;
    if (s_load_tried) return;
    s_load_tried = 1;
    GetModuleFileNameA(NULL, dir, sizeof dir);
    sl = strrchr(dir, '\\'); if (!sl || strrchr(dir, '/') > sl) sl = strrchr(dir, '/');
    if (sl) *sl = 0;
    for (k = 2; k <= 3; k++) {
        e = getenv(k == 2 ? "TT_CREDITS2" : "TT_CREDITS3");
        if (e && *e) snprintf(p, sizeof p, "%s", e); else snprintf(p, sizeof p, "%s/music/credits%d.wav", dir, k);
        wav_add(p);
    }
    if (!s_frames) { free(s_pcm); s_pcm = NULL; tt_dbg("[CREDITS2] no songs of ours (%s/music/credits2.wav, credits3.wav): the credits' song goes round as it did\n", dir); }
    else { s_ready = 1; tt_dbg("[CREDITS2] ready: %.1f s of our songs\n", s_frames / 48000.0); }
}
#if defined(_WIN32)
static DWORD WINAPI loader(LPVOID a) { (void)a; song_load(); return 0; }
#else
#include <pthread.h>
static void *loader(void *a) { (void)a; song_load(); return NULL; }
#endif

/* (times compared signed: the game's thread and the sound's read the clock apart -- on Windows it moves in 15.6 ms steps --
 * so "now" can be a step behind the credits' last frame. Unsigned, that was a huge age: the credits taken for ended many
 * times a second, the switch to ours undone each time it was made, and the song went round as before) */
static long since(unsigned long now, unsigned long t) { return (long)(now - t); }
/* (TT_CREDITS2_COARSE: the clock in Windows' 15.6 ms steps, to test with) */
static unsigned long tick_now(void)
{
    static int c = -1; unsigned long t = GetTickCount();
    if (c < 0) c = getenv("TT_CREDITS2_COARSE") != NULL;
    return c ? t - t % 16u : t;
}
static int on_now(unsigned long now) { return s_last && since(now, s_last) < 500; }

static void observe(uint32_t caller, long long off, uint32_t length)
{
    unsigned long now = tick_now();
    int i, top;
    (void)caller; (void)length;
    if (s_nent <= 0) return;
    for (i = 0; i < s_nent; i++) if (off >= s_ent[i].start && off < (long long)s_ent[i].start + s_ent[i].len) break;
    if (i == s_nent) return;
    if (on_now(now)) s_nreads++;
    top = off < (long long)s_ent[i].start + 0x10000;   /* (its first buffers) */
    if (i != s_entry) { s_entry = i; s_t0 = top ? now : 0; s_deep = 0; }   /* (another song: from its top, playing from now) */
    else if (top && s_deep) {
        /* back to its top after reading well into it: the song went round (its end not seen coming) -- ours now */
        if (on_now(now) && s_phase == 0 && s_ready) {
            s_switch_at = now; s_phase = 1;
            tt_dbg("[CREDITS2] song %d went round (%.1f s after its start): ours now\n", i, s_t0 ? since(now, s_t0) / 1000.0 : -1.0);
        }
        s_t0 = now; s_deep = 0;
    }
    else if (top && !s_t0) s_t0 = now;
    else if (s_deep && on_now(now) && s_phase == 0 && s_ready && off + (long long)length >= (long long)s_ent[i].start + s_ent[i].len - 18432) {
        /* its last buffers read (the reads run a buffer or two ahead of what is heard): the song is in its quiet last
         * moments, about to start over -- ours from here, before its top is heard again. Until now the song played
         * just as the game plays it */
        s_switch_at = now + 600u; s_phase = 1;   /* (its top is heard again about a second after this read) */
        tt_dbg("[CREDITS2] song %d: its last buffers read (%.1f s after its start): ours now\n", i, s_t0 ? since(now, s_t0) / 1000.0 : -1.0);
    }
    if (off > (long long)s_ent[i].start + 0x100000) s_deep = 1;
    s_lastoff = off;
}

static void post_mix(int16_t *buf, int n)
{
    unsigned long now = tick_now();
    int i;
    if (lg()) { static unsigned long tl; if (since(now, tl) > 20000) { tl = now; tt_dbg("[CREDITS2] mix: on %d phase %d\n", on_now(now), s_phase); } }
    if (!on_now(now)) {
        if (s_phase) { s_phase = 0; tt_dbg("[CREDITS2] the credits ended: the game's sound again\n"); }
        return;
    }
    s_nmix++;
    if (!s_ready) return;
    if (s_phase == 0) {   /* the game's song as it comes out (the music volume): how loud ours is to be */
        if (since(now, s_session_t) > 3000) for (i = 0; i < 2 * n; i++) { s_sumsq += (double)buf[i] * buf[i]; s_cnt++; }
        /* (a backstop: the song seen neither ending nor going round -- it goes round about 161 s after its start) */
        if (s_t0 && since(now, s_t0) > 163500 && since(now, s_session_t) > 163500) {
            s_phase = 1; s_switch_at = now;
            tt_dbg("[CREDITS2] backstop: %.1f s since the song's start, no end seen (%u reads seen in the credits): ours now\n", since(now, s_t0) / 1000.0, s_nreads);
        } else return;
    }
    if (s_phase == 1) {
        if ((long)(now - s_switch_at) < 0) return;
        s_phase = 2; s_pos = 0;
        /* (the file made as loud as the game's song is on the disc: 70 dB RMS -- what came out, over that) */
        s_gain = s_cnt > 48000 ? (float)(sqrt(s_sumsq / (double)s_cnt) / 3162.0) : 1.0f;
        if (s_gain > 1.5f) s_gain = 1.5f;
        if (s_gain < 0.0f) s_gain = 0.0f;
        tt_dbg("[CREDITS2] ours now (as loud as the game's song came out: %.2f)\n", s_gain);
    }
    for (i = 0; i < n; i++) {
        float f = s_pos < 2400 ? (float)s_pos / 2400.0f : 1.0f;   /* (a 50 ms fade over the game's last) */
        int c;
        for (c = 0; c < 2; c++) {
            float v = s_pcm[s_pos * 2 + c] * s_gain;
            float g = buf[i * 2 + c] * (1.0f - f);
            float m = v * f + g;
            buf[i * 2 + c] = (int16_t)(m > 32767.0f ? 32767.0f : m < -32768.0f ? -32768.0f : m);
        }
        if (++s_pos >= s_frames) s_pos = 0;   /* (the credits still on: round again) */
    }
}

/* the credits' scroll, each frame */
void tt_credits_music_tick(void)
{
    unsigned long now = tick_now();
    if (getenv("TT_NOCREDITS2")) return;
    if (!s_last || since(now, s_last) > 1500) {   /* (the credits opened) */
        s_session_t = now; s_phase = 0; s_sumsq = 0; s_cnt = 0; s_nreads = 0; s_nmix = 0;
        if (!s_ready && !s_load_tried) song_load();   /* (the front end's load not started: now) */
        tt_dbg("[CREDITS2] the credits opened: our songs %s, song %d streaming, watching %s\n", s_ready ? "ready" : s_load_tried ? "loading or missing" : "not loaded", s_entry, xbox_read_observer == observe && xbox_apu_post_mix == post_mix ? "yes" : "NO");
        if (getenv("TT_CREDITS2_AFTER") && s_ready) { s_switch_at = now + (unsigned long)atoi(getenv("TT_CREDITS2_AFTER")); s_phase = 1; }   /* (testing: ours that long after the credits open) */
    }
    if (s_last && since(now, s_session_t) > 30000) { static unsigned long tl; if (since(now, tl) > 30000) { tl = now; tt_dbg("[CREDITS2] %.0f s into the credits: phase %d, %u reads of the bank, %u mixes, our songs %s\n", since(now, s_session_t) / 1000.0, s_phase, s_nreads, s_nmix, s_ready ? "ready" : "not ready"); } }
    s_last = now;
}
/* from the front end: the game's reads and its mix watched from before the credits open (their song starts with them) */
void tt_credits_music_init(void)
{
    static int done;
    if (done || getenv("TT_NOCREDITS2")) return;
    done = 1;
    bank_load();
    {   /* (ours read now, away from the game: a few seconds of disk and work that stalled the credits' start) */
#if defined(_WIN32)
        HANDLE h = CreateThread(NULL, 0, loader, NULL, 0, NULL); if (h) CloseHandle(h); else song_load();
#else
        pthread_t t; if (pthread_create(&t, NULL, loader, NULL) == 0) pthread_detach(t); else song_load();
#endif
    }
    xbox_read_observer = observe;
    xbox_apu_post_mix = post_mix;
}
