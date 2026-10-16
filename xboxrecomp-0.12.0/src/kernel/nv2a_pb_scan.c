/*
 * Read-only survey of the pushbuffer a title submits.
 *
 * The title builds NV2A commands in guest RAM and advances DMA_PUT; nothing
 * here executes them, so the framebuffer stays black however far the game
 * gets. Before any of that can be made to draw, the question is what it
 * actually asks for -- which methods, on which object classes, how many of
 * them -- because that is the difference between "the existing PGRAPH
 * translator nearly covers this" and "this needs a real one".
 *
 * Purely a reader: it walks the buffer and counts, and never writes to guest
 * memory or to the GPU state. Enabled with RECOMP_PB_SCAN.
 *
 * Pushbuffer encoding (NV20/NV2A), one dword per command header:
 *   (w & 0xE0030003) == 0x00000000  increasing methods
 *   (w & 0xE0030003) == 0x40000000  non-increasing (same method, count params)
 *   (w & 0x00000003) == 0x00000001  jump
 *   (w & 0x00000003) == 0x00000002  call
 *   (w & 0xFFFF0003) == 0x00020000  return
 * For a method header: count = (w >> 18) & 0x7FF, subchannel = (w >> 13) & 7,
 * method = w & 0x1FFC.
 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>   /* ptrdiff_t */
#include <stdlib.h>
#include <string.h>
extern const char *recomp_cgetenv(const char *name);
#define getenv(n) recomp_cgetenv(n)
#ifndef XBOX_CONTIG_BASE
#define XBOX_CONTIG_BASE 0x80000000u  /* kernel.h */
#endif

extern ptrdiff_t xbox_GetMemoryOffset(void);

#define PB_MAX_METHODS 4096

static struct { uint32_t method, subch, count; } s_seen[PB_MAX_METHODS];
static int s_seen_count;

/* Parse health. An inventory is only worth reading if the walk stayed in step
 * with the command stream: a decoder that desynchronises produces plausible
 * looking method numbers out of parameter data, and the counts then describe
 * nothing. Unrecognised words are the tell. */
static uint32_t s_tot_words, s_tot_unknown, s_tot_jumps, s_tot_segments;

/* Executing is opt-in separately from surveying: a survey is read-only, while
 * the executor writes to guest memory. */
extern void nv2a_pb_exec_method(uint32_t subch, uint32_t method, uint32_t param);
extern void nv2a_pb_exec_report(void);
static int s_exec_enabled = -1;

/* Direct index: (subchannel, method) -> slot+1 in s_seen. note() runs for
 * every method word the GPU executes; the linear search it replaced was
 * the hottest loop of the whole renderer on a Windows PC. */
static uint16_t s_seen_idx[8][0x2000 / 4];
static void note(uint32_t subch, uint32_t method)
{
    uint16_t *ix = &s_seen_idx[subch & 7][(method & 0x1FFC) >> 2];
    if (*ix) { s_seen[*ix - 1].count++; return; }
    if (s_seen_count >= PB_MAX_METHODS) {
        /* Silently dropping past the cap is how a truncated inventory reads as
         * "the title never does that" -- exactly the wrong conclusion when the
         * inventory is being used to decide what to implement. */
        static int warned;
        if (!warned) {
            warned = 1;
            fprintf(stderr, "[PB] method table full at %d -- inventory is"
                            " truncated\n", PB_MAX_METHODS);
        }
    }
    if (s_seen_count < PB_MAX_METHODS) {
        s_seen[s_seen_count].method = method;
        s_seen[s_seen_count].subch  = subch;
        s_seen[s_seen_count].count  = 1;
        s_seen_count++;
        *ix = (uint16_t)s_seen_count;
    }
}

/* NV097 (Kelvin 3D class) methods worth naming. The point of the survey is to
 * decide what a translator has to implement, and a bare method number does not
 * answer that -- "0x1808 x412" only means something once it reads
 * INLINE_ARRAY. Unnamed ones still get counted. */
static const struct { uint32_t m; const char *name; } NV097_NAMES[] = {
    { 0x0000, "SET_OBJECT" },
    { 0x0100, "NO_OPERATION" },
    { 0x0104, "SET_WARNING_ENABLE" },
    { 0x0130, "SET_FLIP_READ" },
    { 0x0200, "SET_SURFACE_CLIP_HORIZONTAL" },
    { 0x0204, "SET_SURFACE_CLIP_VERTICAL" },
    { 0x0208, "SET_SURFACE_FORMAT" },
    { 0x020C, "SET_SURFACE_PITCH" },
    { 0x0210, "SET_SURFACE_COLOR_OFFSET" },
    { 0x0214, "SET_SURFACE_ZETA_OFFSET" },
    { 0x0300, "SET_ALPHA_TEST_ENABLE" },
    { 0x0304, "SET_BLEND_ENABLE" },
    { 0x030C, "SET_DEPTH_TEST_ENABLE" },
    { 0x0310, "SET_DITHER_ENABLE" },
    { 0x0314, "SET_LIGHTING_ENABLE" },
    { 0x033C, "SET_CULL_FACE_ENABLE" },
    { 0x0340, "SET_DEPTH_MASK" },
    { 0x0350, "SET_CLEAR_DEPTH_VALUE" },
    { 0x1D8C, "SET_CLEAR_DEPTH" },
    { 0x1D90, "SET_COLOR_CLEAR_VALUE" },
    { 0x1D94, "CLEAR_SURFACE" },
    { 0x1D6C, "SET_ZSTENCIL_CLEAR" },
    { 0x0B80, "SET_TRANSFORM_PROGRAM" },
    { 0x0B00, "SET_TRANSFORM_CONSTANT" },
    { 0x1720, "SET_VERTEX_DATA_ARRAY_OFFSET" },
    { 0x1760, "SET_VERTEX_DATA_ARRAY_FORMAT" },
    { 0x17FC, "SET_BEGIN_END" },
    { 0x1800, "ARRAY_ELEMENT16" },
    { 0x1808, "INLINE_ARRAY" },
    { 0x1810, "DRAW_ARRAYS" },
    { 0x1B00, "SET_TEXTURE_OFFSET" },
    { 0x1B04, "SET_TEXTURE_FORMAT" },
    { 0x1B08, "SET_TEXTURE_ADDRESS" },
    { 0x1B0C, "SET_TEXTURE_CONTROL0" },
    { 0x1B14, "SET_TEXTURE_IMAGE_RECT" },
    { 0x0FD8, "SET_COMBINER_*" },
    { 0x0000, NULL },
};

static const char *nv097_name(uint32_t m)
{
    int i;
    for (i = 0; NV097_NAMES[i].name; i++)
        if (NV097_NAMES[i].m == m)
            return NV097_NAMES[i].name;
    return "";
}

void nv2a_pb_scan_report(void)
{
    int i;

    if (s_exec_enabled > 0)
        nv2a_pb_exec_report();
    if (!s_seen_count || !getenv("RECOMP_PB_SCAN"))
        return;
    fprintf(stderr, "[PB] %u segments, %u words, %u jumps, %u unrecognised"
                    " -- %d distinct (subchannel, method) pairs\n",
            s_tot_segments, s_tot_words, s_tot_jumps, s_tot_unknown,
            s_seen_count);
    for (i = 0; i < s_seen_count; i++)
        fprintf(stderr, "  [PB]   subch %u  method 0x%04X  x%-6u %s\n",
                s_seen[i].subch, s_seen[i].method, s_seen[i].count,
                nv097_name(s_seen[i].method));
    fflush(stderr);
}

void nv2a_pb_scan(uint32_t start_va, uint32_t end_va)
{
    const uint8_t *mem = (const uint8_t *)xbox_GetMemoryOffset();
    uint32_t va = start_va;
    int wrap_pending = 0;
    uint32_t words = 0, jumps = 0, unknown = 0;

    if (s_exec_enabled < 0)
        s_exec_enabled = getenv("RECOMP_PB_EXEC") != NULL;
    if (!(getenv("RECOMP_PB_SCAN") || s_exec_enabled) || end_va == start_va)
        return;
    /* end < start: the ring wrapped. Walk from start until the JUMP back to
     * the ring's beginning, follow it, and stop at end. (Bounding the walk
     * by the highest PUT ever seen missed whatever D3D wrote between that
     * PUT and its wrap jump -- a semaphore release among it, and D3D then
     * waited forever for a GPU time that was never written.) */
    if (getenv("RECOMP_PB_FLOW"))
        fprintf(stderr, "  [PBFLOW] scan %08X..%08X\n", start_va, end_va);
    if (end_va < start_va)
        wrap_pending = 1;
    else if (end_va - start_va > 0x400000u)   /* a sane single-frame bound */
        end_va = start_va + 0x400000u;

    /* Jumps, calls and returns are followed, not treated as the end of the
     * segment. D3D's RunPushBuffer / recorded-pushbuffer path writes a jump
     * OUT of the ring to a static buffer that jumps back to the very next
     * ring word (The Two Towers: 0x00699001 at 0x80389DC4, back to DC8).
     * Stopping at the jump lost everything after it -- including the
     * InsertCallback that the title's render thread then waited on forever.
     *
     * A jump whose continuation is still inside [start, end) is followed out
     * and the walk resumes when control lands back in range. A jump that is
     * the last thing in the segment (the ring wrapping to its start) still
     * ends the segment: the poller scans the wrapped part itself. */
    {
        uint32_t ret_va = 0;
        int outside = wrap_pending, hops = 0;

        while (words < 0x100000u) {
            uint32_t w;

            if (!outside && va >= end_va)
                break;
            w = *(const uint32_t *)(mem + va);
            va += 4;
            words++;

            if (((w & 3u) != 0u || (w & 0xE0000003u) == 0x20000000u
                 || (w & 0xFFFF0003u) == 0x00020000u) && getenv("RECOMP_PB_FLOW"))
                fprintf(stderr, "  [PBFLOW]   @%08X %08X (outside=%d ret=%08X)\n",
                        va - 4, w, outside, ret_va);
            if ((w & 3u) == 1u || (w & 0xE0000003u) == 0x20000000u) {
                uint32_t tgt = XBOX_CONTIG_BASE |
                    (((w & 3u) == 1u ? w : (w & 0x1FFFFFFFu)) & 0x0FFFFFFCu);
                jumps++;
                if (wrap_pending && tgt <= end_va) {
                    wrap_pending = 0;
                    start_va = tgt;
                    va = tgt;
                    outside = (tgt == end_va) ? 0 : 0;
                    if (tgt == end_va)
                        break;
                    continue;
                }
                if (tgt >= start_va && tgt <= end_va) {
                    if (tgt == end_va)
                        break;
                    va = tgt;
                    outside = 0;
                    continue;
                }
                if (!outside && va >= end_va)
                    break;                    /* ring wrap: segment ends */
                if (++hops > 256)
                    break;
                va = tgt;
                outside = 1;
                continue;
            }
            if ((w & 3u) == 2u) {             /* call */
                if (ret_va || ++hops > 256)
                    break;
                ret_va = va;
                va = XBOX_CONTIG_BASE | (w & 0x0FFFFFFCu);
                outside = 1;
                continue;
            }
            if ((w & 0xFFFF0003u) == 0x00020000u) {   /* return */
                if (!ret_va)
                    break;
                va = ret_va;
                ret_va = 0;
                outside = !(va >= start_va && va < end_va);
                continue;
            }
            if ((w & 0x00030003u) == 0u) {
                uint32_t count  = (w >> 18) & 0x7FFu;
                uint32_t subch  = (w >> 13) & 7u;
                uint32_t method =  w & 0x1FFCu;
                int noninc = (w & 0xE0000000u) == 0x40000000u;

                for (uint32_t i = 0; i < count && (outside || va < end_va); i++) {
                    uint32_t m = noninc ? method : method + i * 4;
                    note(subch, m);
                    /* Same walk, two consumers: the survey counts, the
                     * executor acts. Keeping them on one decode means they
                     * can never disagree about what the stream said. */
                    if (s_exec_enabled)
                        nv2a_pb_exec_method(subch, m,
                                            *(const uint32_t *)(mem + va));
                    va += 4;
                    words++;
                }
                continue;
            }
            if (getenv("RECOMP_PB_FLOW"))
                fprintf(stderr, "  [PBFLOW]   ?? @%08X %08X\n", va - 4, w);
            unknown++;
        }
    }

    s_tot_words += words;
    s_tot_unknown += unknown;
    s_tot_jumps += jumps;
    s_tot_segments++;
}

/* ── The GPU's own fetch loop ────────────────────────────────────────────
 *
 * Range scans between successive PUT values were a model of the ring, not of
 * the hardware, and every pushbuffer layout broke it a different way: D3D's
 * kickoff entries CALL out of the 64 KB ring into a 3 MB command buffer that
 * JUMPs back; a wrap's jump sits past the last PUT seen; a range that starts
 * on stale data replays old semaphore releases. The DMA engine itself has no
 * ranges -- it has GET, follows jump/call/return from there, and stops when
 * GET equals PUT. This is that, with GET kept between polls. */
static uint32_t s_get, s_subr;
static int s_get_valid, s_in_call;
static uint32_t s_flow[16][2];
static unsigned s_flow_n;
#define FLOW_NOTE(at, w) do { s_flow[s_flow_n & 15][0] = (at); s_flow[s_flow_n & 15][1] = (w); s_flow_n++; } while (0)

uint32_t nv2a_pb_get(void)  { return s_get & 0x0FFFFFFFu; }
uint32_t nv2a_pb_subr(void) { return s_in_call ? ((s_subr & 0x0FFFFFFFu) | 1u) : 0u; }

volatile uint32_t g_pb_runs;   /* pushbuffer runs; CPU writes happen only between them */
void nv2a_pb_run(uint32_t put_phys, uint32_t init_get_phys)
{
    g_pb_runs++;
    const uint8_t *mem = (const uint8_t *)xbox_GetMemoryOffset();
    uint32_t put = XBOX_CONTIG_BASE | (put_phys & 0x0FFFFFFFu);
    uint32_t words = 0;
    static int trace = -1;

    if (s_exec_enabled < 0)
        s_exec_enabled = getenv("RECOMP_PB_EXEC") != NULL;
    if (trace < 0)
        trace = getenv("RECOMP_PB_FLOW") != NULL;
    if (!(getenv("RECOMP_PB_SCAN") || s_exec_enabled))
        return;
    if (!s_get_valid) {
        s_get = init_get_phys ? (XBOX_CONTIG_BASE | (init_get_phys & 0x0FFFFFFFu)) : put;
        s_get_valid = 1;
    }
    /* A kick that publishes a pointer a little behind where the GPU already
     * is (two threads kicking) asks for nothing new. */
    if (s_get > put && s_get - put < 0x8000u && (s_get >> 16) == (put >> 16))
        return;

    /* Where pushbuffer commands can live, if the title said
     * (RECOMP_PB_RANGE=lo-hi, physical). A jump or call anywhere else is
     * the fetch having walked into data -- stop there instead of
     * executing it (it once rewrote the semaphore DMA object). */
    static uint32_t rng_lo = 1, rng_hi;
    if (rng_lo == 1) {
        const char *v = getenv("RECOMP_PB_RANGE");
        char *e;
        rng_lo = 0; rng_hi = 0xFFFFFFFFu;
        if (v && *v) {
            rng_lo = XBOX_CONTIG_BASE | (uint32_t)strtoul(v, &e, 0);
            if (*e == '-') rng_hi = XBOX_CONTIG_BASE | (uint32_t)strtoul(e + 1, NULL, 0);
        }
    }
#define PB_TARGET_OK(t) ((t) >= rng_lo && (t) < rng_hi)
    if (trace) fprintf(stderr, "  [PBFLOW] run GET %08X -> PUT %08X\n", s_get, put);
    while (s_get != put && words < 0x200000u) {
        uint32_t w;
        if (s_get < XBOX_CONTIG_BASE || s_get >= XBOX_CONTIG_BASE + 0x04000000u - 4u) {
            words = 0x200000u;               /* off the end of RAM: resync */
            break;
        }
        w = *(const uint32_t *)(mem + s_get);
        words++;

        if ((w & 3u) == 1u) {                               /* jump */
            FLOW_NOTE(s_get, w);
            if (trace) fprintf(stderr, "  [PBFLOW] %08X jump %08X\n", s_get, w & ~3u);
            if (!PB_TARGET_OK(XBOX_CONTIG_BASE | (w & 0x0FFFFFFCu))) { words = 0x200000u; break; }
            s_get = XBOX_CONTIG_BASE | (w & 0x0FFFFFFCu);
            continue;
        }
        if ((w & 0xE0000003u) == 0x20000000u) {             /* old jump */
            FLOW_NOTE(s_get, w);
            if (trace) fprintf(stderr, "  [PBFLOW] %08X oldjump %08X\n", s_get, w & 0x1FFFFFFCu);
            if (!PB_TARGET_OK(XBOX_CONTIG_BASE | (w & 0x0FFFFFFCu))) { words = 0x200000u; break; }
            s_get = XBOX_CONTIG_BASE | (w & 0x0FFFFFFCu);
            continue;
        }
        if ((w & 3u) == 2u) {                               /* call */
            FLOW_NOTE(s_get, w);
            if (trace) fprintf(stderr, "  [PBFLOW] %08X call %08X\n", s_get, w & ~3u);
            if (!PB_TARGET_OK(XBOX_CONTIG_BASE | (w & 0x0FFFFFFCu))) { words = 0x200000u; break; }
            s_subr = s_get + 4;
            s_in_call = 1;
            s_get = XBOX_CONTIG_BASE | (w & 0x0FFFFFFCu);
            continue;
        }
        if ((w & 0xFFFF0003u) == 0x00020000u) {             /* return */
            if (trace) fprintf(stderr, "  [PBFLOW] %08X return (in call %d -> %08X)\n", s_get, s_in_call, s_subr);
            if (s_in_call) {
                s_get = s_subr;
                s_in_call = 0;
            } else {
                s_get += 4;
            }
            continue;
        }
        s_get += 4;
        if ((w & 0x00030003u) == 0u) {                      /* methods */
            uint32_t count  = (w >> 18) & 0x7FFu;
            uint32_t subch  = (w >> 13) & 7u;
            uint32_t method =  w & 0x1FFCu;
            int noninc = (w & 0xE0000000u) == 0x40000000u;
            uint32_t i;
            for (i = 0; i < count; i++) {
                uint32_t m = noninc ? method : method + i * 4;
                if (s_get == put || s_get >= XBOX_CONTIG_BASE + 0x04000000u - 4u)
                    break;
                note(subch, m);
                if (s_exec_enabled)
                    nv2a_pb_exec_method(subch, m, *(const uint32_t *)(mem + s_get));
                s_get += 4;
                words++;
            }
        }
    }
    if (s_get != put) {
        unsigned k;
        fprintf(stderr, "  [PB] GPU fetch lost its place (GET %08X, PUT %08X); resyncing\n",
                s_get, put);
        for (k = 0; k < 16 && k < s_flow_n; k++) {
            unsigned idx = (s_flow_n - 1 - k) & 15;
            fprintf(stderr, "  [PB]   flow -%u: @%08X %08X\n", k, s_flow[idx][0], s_flow[idx][1]);
        }
        {
            uint32_t a = (put & ~3u) - 0x20u;
            uint32_t q;
            for (q = 0; q < 40; q++)
                fprintf(stderr, "  [PB]   seg+%03X: %08X\n", q * 4,
                        *(const uint32_t *)(mem + a + q * 4));
        }
        s_get = put;
        s_in_call = 0;
    }
}
