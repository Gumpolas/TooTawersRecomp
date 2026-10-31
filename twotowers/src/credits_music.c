/* credits_music.c -- (1.9.9) the credits' second song. The credits play one song (streamed from the interface's
 * streaming wave bank, sound\S_WB_INTRFACE.xwb) and outlast it, so the game started it again from the top until the
 * credits ended. With music\credits2.wav beside the game (16-bit PCM, mono or stereo, any rate), that plays instead
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
#define DELAY_MS 500   /* the credits' sound held back this long */
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

/* ours, from the file: 16-bit PCM WAV, made stereo at 48 kHz */
static void song_load(void)
{
    char p[900], *sl; const char *e = getenv("TT_CREDITS2");
    FILE *f; long n; uint8_t *b; uint32_t o = 12, ch = 0, rate = 0, bits = 0, fmt = 0, dlen = 0; const uint8_t *data = NULL;
    if (s_load_tried) return;
    s_load_tried = 1;
    if (e && *e) snprintf(p, sizeof p, "%s", e);
    else {
        GetModuleFileNameA(NULL, p, sizeof p);
        sl = strrchr(p, '\\'); if (!sl || strrchr(p, '/') > sl) sl = strrchr(p, '/');
        if (sl) *sl = 0;
        strncat(p, "/music/credits2.wav", sizeof p - strlen(p) - 1);
    }
    if (!(f = fopen(p, "rb"))) { if (lg()) tt_dbg("[CREDITS2] no %s: the song goes round as it did\n", p); return; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = (uint8_t *)malloc((size_t)(n > 0 ? n : 1));
    if (!b || n < 44 || fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return; }
    fclose(f);
    if (memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) { tt_dbg("[CREDITS2] %s: not a WAV file\n", p); free(b); return; }
    while (o + 8 <= (uint32_t)n) {
        uint32_t cl = rd32(b + o + 4);
        if (!memcmp(b + o, "fmt ", 4) && cl >= 16) { fmt = b[o + 8] | b[o + 9] << 8; ch = b[o + 10] | b[o + 11] << 8; rate = rd32(b + o + 12); bits = b[o + 22] | b[o + 23] << 8; }
        else if (!memcmp(b + o, "data", 4)) { data = b + o + 8; dlen = cl; if (o + 8 + dlen > (uint32_t)n) dlen = (uint32_t)n - o - 8; break; }
        o += 8 + cl + (cl & 1);
    }
    if (!data || (fmt != 1 && fmt != 0xFFFE) || bits != 16 || (ch != 1 && ch != 2) || rate < 8000 || rate > 192000) {
        tt_dbg("[CREDITS2] %s: not 16-bit PCM, mono or stereo (format %u, %u bits, %u channels)\n", p, fmt, bits, ch); free(b); return;
    }
    {
        size_t in = dlen / (2 * ch), out = (size_t)((double)in * 48000.0 / rate), i;
        const int16_t *s = (const int16_t *)data;
        s_pcm = (int16_t *)malloc(out * 4 + 4);
        if (!s_pcm) { free(b); return; }
        for (i = 0; i < out; i++) {
            double t = (double)i * rate / 48000.0; size_t k = (size_t)t; double fr = t - (double)k; int c;
            if (k + 1 >= in) { k = in ? in - 1 : 0; fr = 0; }
            for (c = 0; c < 2; c++) {
                int cc = ch == 2 ? c : 0;
                double v = s[k * ch + cc] * (1.0 - fr) + (k + 1 < in ? s[(k + 1) * ch + cc] : 0) * fr;
                s_pcm[i * 2 + c] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
            }
        }
        s_frames = out;
    }
    free(b);
    tt_dbg("[CREDITS2] the credits' second song: %s (%.1f s)\n", p, s_frames / 48000.0);
}

static int on_now(unsigned long now) { return s_last && now - s_last < 500u; }

static void observe(uint32_t caller, long long off, uint32_t length)
{
    unsigned long now = GetTickCount();
    int i, top;
    (void)caller; (void)length;
    if (s_nent <= 0) return;
    for (i = 0; i < s_nent; i++) if (off >= s_ent[i].start && off < (long long)s_ent[i].start + s_ent[i].len) break;
    if (i == s_nent) return;
    top = off < (long long)s_ent[i].start + 0x10000;   /* (its first buffers) */
    if (i != s_entry) { s_entry = i; s_t0 = top ? now : 0; s_deep = 0; }   /* (another song: from its top, playing from now) */
    else if (top && s_deep) {
        /* back to its top after reading well into it: the song went round. It ends at its start plus its length (its
         * last buffers still to play) -- ours then */
        if (on_now(now) && s_phase == 0 && s_pcm) {
            /* (its top is heard about when it is read again -- its first buffer was kept from the first time -- and the
             * credits' sound is held back DELAY_MS: ours from a little after now, before the song's top comes out) */
            s_switch_at = now + DELAY_MS / 2;
            s_phase = 1;
            if (lg()) tt_dbg("[CREDITS2] song %d went round (%.1f s after its start): ours in %d ms\n", i, s_t0 ? (now - s_t0) / 1000.0 : -1.0, DELAY_MS / 2);
        }
        s_t0 = now; s_deep = 0;
    }
    else if (top && !s_t0) s_t0 = now;
    if (off > (long long)s_ent[i].start + 0x100000) s_deep = 1;
    s_lastoff = off;
}

/* the credits' sound held back DELAY_MS (a ring of a second): the song's top, read again, is caught before it is heard */
#define RING 48000
static int16_t s_ring[RING * 2];
static size_t s_rw, s_rr; static int s_delay_on;
static void post_mix(int16_t *buf, int n)
{
    unsigned long now = GetTickCount();
    int i;
    if (lg()) { static unsigned long tl; if (now - tl > 20000u) { tl = now; tt_dbg("[CREDITS2] mix: on %d phase %d\n", on_now(now), s_phase); } }
    if (!on_now(now)) {
        if (s_phase || s_delay_on) { s_phase = 0; s_delay_on = 0; if (lg()) tt_dbg("[CREDITS2] the credits ended: the game's sound again\n"); }
        return;
    }
    if (!s_pcm) return;
    if (!s_delay_on) {   /* (the credits opened: their sound from DELAY_MS on) */
        memset(s_ring, 0, sizeof s_ring);
        s_rr = 0; s_rw = (size_t)(48 * DELAY_MS); s_delay_on = 1;
    }
    for (i = 0; i < n; i++) {
        s_ring[s_rw * 2] = buf[i * 2]; s_ring[s_rw * 2 + 1] = buf[i * 2 + 1]; s_rw = (s_rw + 1) % RING;
        buf[i * 2] = s_ring[s_rr * 2]; buf[i * 2 + 1] = s_ring[s_rr * 2 + 1]; s_rr = (s_rr + 1) % RING;
    }
    if (s_phase == 0) {   /* the game's song as it comes out (the music volume): how loud ours is to be */
        if (now - s_session_t > 3000u) for (i = 0; i < 2 * n; i++) { s_sumsq += (double)buf[i] * buf[i]; s_cnt++; }
        return;
    }
    if (s_phase == 1) {
        if ((long)(now - s_switch_at) < 0) return;
        s_phase = 2; s_pos = 0;
        /* (the file made as loud as the game's song is on the disc: 70 dB RMS -- what came out, over that) */
        s_gain = s_cnt > 48000 ? (float)(sqrt(s_sumsq / (double)s_cnt) / 3162.0) : 1.0f;
        if (s_gain > 1.5f) s_gain = 1.5f;
        if (s_gain < 0.0f) s_gain = 0.0f;
        if (lg()) tt_dbg("[CREDITS2] ours now (as loud as the game's song came out: %.2f)\n", s_gain);
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
    unsigned long now = GetTickCount();
    if (getenv("TT_NOCREDITS2")) return;
    if (!s_last || now - s_last > 1500u) {   /* (the credits opened) */
        s_session_t = now; s_phase = 0; s_sumsq = 0; s_cnt = 0;
        bank_load(); song_load();
        if (getenv("TT_CREDITS2_AFTER") && s_pcm) { s_switch_at = now + (unsigned long)atoi(getenv("TT_CREDITS2_AFTER")); s_phase = 1; }   /* (testing: ours that long after the credits open) */
        if (lg()) tt_dbg("[CREDITS2] the credits opened (song %d streaming)\n", s_entry);
    }
    s_last = now;
}
/* from the front end: the game's reads and its mix watched from before the credits open (their song starts with them) */
void tt_credits_music_init(void)
{
    static int done;
    if (done || getenv("TT_NOCREDITS2")) return;
    done = 1;
    bank_load();
    xbox_read_observer = observe;
    xbox_apu_post_mix = post_mix;
}
