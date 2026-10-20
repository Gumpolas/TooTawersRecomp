/*
 * nv2a_sw3d.inc -- software 3D for the pushbuffer executor.
 *
 * Included by nv2a_pb_exec.c (it uses that file's state and helpers).
 *
 * The 2D path draws batches whose positions are already in pixels. Anything a
 * title runs through a vertex program (every XDK title's 3D, and many 2D
 * passes too) arrives in object space and used to be skipped. This file:
 *
 *   - captures the transform state: vertex program slots (0x0B00), constants
 *     (0x0B80), execution mode (0x1E94), the composite matrix (0x0680);
 *   - interprets NV2A vertex program microcode per vertex (MAC + ILU pairs,
 *     a0.x relative addressing, r12 = oPos);
 *   - clips against w > 0 in homogeneous screen space, culls, and rasterises
 *     with a private depth buffer and perspective-correct attributes;
 *   - shades each pixel with the NV2A register combiners (up to 8 general
 *     stages plus the final combiner) over four texture stages, then alpha
 *     test, depth write, blend equation/factors and colour write mask.
 *
 * After the XDK's own postamble, oPos.xyz is already in window coordinates
 * (divided by w) and oPos.w is the clip-space w -- the NV2A rasterises window
 * coordinates directly. So (x*w, y*w, z*w, w) is linear in clip space, which
 * is what clipping and perspective-correct interpolation need, without
 * knowing the viewport.
 *
 * ponytail: no stencil buffer (stencil-tested passes are skipped), no
 * mipmapping, dependent/dot-product texture shader modes sample as plain 2D,
 * point sprites and lines are not drawn.
 */

#include <math.h>

static int gpu_on(void);
static void gpu_batch(void);
static inline int ifloor(float x) { int i = (int)x; return i - (x < (float)i); }

#define NV097_SET_TRANSFORM_PROGRAM        0x0B00   /* ..0x0B7C */
#define NV097_SET_TRANSFORM_CONSTANT       0x0B80   /* ..0x0BFC */
#define NV097_SET_TRANSFORM_EXECUTION_MODE 0x1E94
#define NV097_SET_TRANSFORM_PROGRAM_LOAD   0x1E9C
#define NV097_SET_TRANSFORM_PROGRAM_START  0x1EA0
#define NV097_SET_TRANSFORM_CONSTANT_LOAD  0x1EA4
#define NV097_SET_COMPOSITE_MATRIX         0x0680   /* 16 floats */
#define NV097_SET_ALPHA_TEST_ENABLE        0x0300
#define NV097_SET_CULL_FACE_ENABLE         0x0308
#define NV097_SET_DEPTH_TEST_ENABLE        0x030C
#define NV097_SET_ALPHA_FUNC               0x033C
#define NV097_SET_ALPHA_REF                0x0340
#define NV097_SET_DEPTH_FUNC               0x0354
#define NV097_SET_DEPTH_MASK               0x035C
#define NV097_SET_CULL_FACE                0x039C
#define NV097_SET_FRONT_FACE               0x03A0
#define NV097_ARRAY_ELEMENT32              0x1808

#define VP_SLOTS  136
#define VP_CONSTS 192

static struct {
    uint32_t prog[VP_SLOTS][4];
    float    c[VP_CONSTS][4];
    uint32_t load_slot, load_word, start;
    uint32_t c_load, c_comp;
    uint32_t mode;                 /* 0 fixed, 2 program */
    float    composite[16];
    int      alpha_test, cull_enable, depth_test, depth_mask;
    uint32_t alpha_func, alpha_ref, depth_func, cull_face, front_face;
    uint32_t color_mask, blend_eq, blend_color, stencil_test;
    uint32_t st_wmask, st_func, st_ref, st_mask, st_fail, st_zfail, st_zpass;
    /* register combiners */
    uint32_t c_icw[8], c_ocw[8], a_icw[8], a_ocw[8], factor0[8], factor1[8];
    uint32_t comb_ctl, spec_fog0, spec_fog1, shader_prog, fog_color, fog_enable, fog_mode;
    float    fog_params[3];
    int      comb_seen;            /* the title has programmed the combiners */
    /* stats */
    uint32_t batches, verts, tris_in, tris_culled, tris_clipped, tris_out;
    uint64_t pixels;
} s_3d = { .depth_mask = 1, .depth_func = 0x203, .alpha_func = 0x207,
           .cull_face = 0x405, .front_face = 0x901,
           .color_mask = 0x01010101u, .blend_eq = 0x8006,
           .st_wmask = 0xFF, .st_func = 0x207, .st_mask = 0xFF,
           .st_fail = 0x1E00, .st_zfail = 0x1E00, .st_zpass = 0x1E00 };

/* Texture-shader dot-product state (kept out of s_3d: captures). */
static uint32_t s_dot_map, s_other_in;
static float *s_zbuf;
static uint8_t *s_sbuf;
static int s_zclear_forced;
static uint32_t s_zbuf_w, s_zbuf_h;
static float s_clip_min, s_clip_max;   /* SET_CLIP_MIN/MAX: window z range (not in s_3d: captures) */
static uint32_t s_zeta_offset;      /* SET_SURFACE_ZETA_OFFSET */
static uint32_t s_zbuf_zeta;        /* the zeta surface s_zbuf stands for */
static int s_zbuf_dirty;
static uint32_t s_zeta_pitch, s_zbuf_zpitch;

static void sw3d_zbuf_clear(void)
{
    uint32_t w = s_gpu.clip_x + s_gpu.clip_w, h = s_gpu.clip_y + s_gpu.clip_h, i;
    if (!w || !h) return;
    if (!s_zbuf || w > s_zbuf_w || h > s_zbuf_h) {
        free(s_zbuf);
        s_zbuf_w = w < 640 ? 640 : w;
        s_zbuf_h = h < 480 ? 480 : h;
        s_zbuf = (float *)malloc(sizeof(float) * s_zbuf_w * s_zbuf_h);
        free(s_sbuf);
        s_sbuf = (uint8_t *)calloc(s_zbuf_w * s_zbuf_h, 1);
        if (!s_zbuf || !s_sbuf) { s_zbuf_w = s_zbuf_h = 0; return; }
    }
    {
        /* The clear value the title set (SET_ZSTENCIL_CLEAR_VALUE, 0x1D8C),
         * in the units oPos.z arrives in: Z24S8 keeps depth in the top 24
         * bits. A title with an inverted depth range (GEQUAL, cleared to 0)
         * depends on this. */
        uint32_t zv = s_cb_scratch[0];
        float z = (((s_gpu.format >> 4) & 0xF) == 1) ? (float)(zv & 0xFFFF) : (float)(zv >> 8);
        if (s_zclear_forced) z = INFINITY;
        for (i = 0; i < s_zbuf_w * s_zbuf_h; i++)
            s_zbuf[i] = z;
    }
}

/* A vertex program that only copies v0/v1/v2 to oPos/oD0/oT0 is the XDK's
 * screen-space pass (UI, video). Those batches keep the fast 2D path. */
static void sw3d_sbuf_clear(void)
{
    if (!s_sbuf) sw3d_zbuf_clear();
    if (s_sbuf) memset(s_sbuf, s_cb_scratch[0] & 0xFF, (size_t)s_zbuf_w * s_zbuf_h);
}

static int stencil_pass(uint8_t v)
{
    uint32_t a = s_3d.st_ref & s_3d.st_mask, b = v & s_3d.st_mask;
    switch (s_3d.st_func & 0xF) {
    case 0: return 0;
    case 1: return a <  b;
    case 2: return a == b;
    case 3: return a <= b;
    case 4: return a >  b;
    case 5: return a != b;
    case 6: return a >= b;
    default: return 1;
    }
}

static void stencil_op(uint8_t *p, uint32_t op)
{
    uint8_t v = *p, n = v;
    switch (op) {
    case 0x0000: n = 0; break;
    case 0x1E01: n = (uint8_t)s_3d.st_ref; break;
    case 0x1E02: n = v == 0xFF ? 0xFF : v + 1; break;
    case 0x1E03: n = v == 0 ? 0 : v - 1; break;
    case 0x150A: n = (uint8_t)~v; break;
    case 0x8507: n = (uint8_t)(v + 1); break;
    case 0x8508: n = (uint8_t)(v - 1); break;
    default: return;                       /* KEEP */
    }
    *p = (uint8_t)((v & ~s_3d.st_wmask) | (n & s_3d.st_wmask));
}

static int sw3d_program_mode(void)
{
    uint32_t pc, len = 0;
    if (s_3d.mode != 2) return 0;
    /* Every programmed batch goes through the full pipeline (combiners,
     * blend equations, masks, stencil); the 2D path approximated them and
     * The Two Towers' post passes came out black. RECOMP_FAST_2D restores
     * the old split for the pass-through program. */
    { static int all3d = -1; if (all3d < 0) all3d = getenv("RECOMP_ALL_3D") != NULL; if (all3d) return 1; }
    for (pc = s_3d.start; pc < VP_SLOTS && len < 8; pc++) {
        len++;
        if ((s_3d.prog[pc][3] & 1)) break;
    }
    if (len > 3) return 1;
    /* A screen-space pass the 2D path cannot draw as asked: stencil-masked,
     * a non-additive blend equation or a partial colour mask (The Two
     * Towers' shadow pass: a full-screen quad, stencil != 0, reverse
     * subtract, colour from a combiner factor -- drawn by the 2D path it
     * painted the whole screen dark grey). */
    if (s_3d.stencil_test || s_3d.blend_eq != 0x8006 || s_3d.color_mask != 0x01010101u)
        return 1;
    /* The 2D path draws texel-or-vertex-colour with plain alpha blending.
     * Keep it only for that: one textured stage, T0 or T0*V0, and blending
     * off or SRC_ALPHA/INV_SRC_ALPHA (movies, the loading screen, most of
     * the HUD). Fills from combiner constants, depth-as-texture passes and
     * anything else take the full pipeline. */
    {
        const uint32_t *r = &s_tex_reg[0];
        int tex0 = ((r[3] >> 30) & 1) && r[0] && (s_3d.shader_prog & 0x1F) == 1;
        int simple_comb = !s_3d.comb_seen
            || ((s_3d.comb_ctl & 0xFF) == 1 && (s_3d.c_icw[0] == 0x08040000u || s_3d.c_icw[0] == 0x08200000u)
                && (s_3d.spec_fog0 == 0x0000000Cu || (!s_3d.spec_fog0 && !s_3d.spec_fog1)));
        int simple_blend = !s_gpu.blend_enable || (s_gpu.blend_sfactor == 0x302 && s_gpu.blend_dfactor == 0x303);
        /* ...and only for a big texture: a movie frame or a full-screen
         * copy. Small HUD quads are cheap in the full pipeline, and there the
         * vertex colour tints them (the health bar is green over red). */
        uint32_t cf = (r[1] >> 8) & 0xFF;
        uint32_t area = tex_size_from_format(cf) ? (1u << ((r[1] >> 20) & 0xF)) * (1u << ((r[1] >> 24) & 0xF))
                                                 : (r[7] >> 16) * (r[7] & 0xFFFF);
        return !(tex0 && simple_comb && simple_blend && area >= 512u * 256u);
    }
}

/* Returns 1 if `method` belongs to this file. */
static int sw3d_method(uint32_t method, uint32_t param)
{
    float f;
    memcpy(&f, &param, 4);

    if (method >= NV097_SET_TRANSFORM_PROGRAM && method < NV097_SET_TRANSFORM_PROGRAM + 0x80) {
        if (s_3d.load_slot < VP_SLOTS) {
            s_3d.prog[s_3d.load_slot][s_3d.load_word] = param;
            if (++s_3d.load_word == 4) { s_3d.load_word = 0; s_3d.load_slot++; }
        }
        return 1;
    }
    if (method >= NV097_SET_TRANSFORM_CONSTANT && method < NV097_SET_TRANSFORM_CONSTANT + 0x80) {
        if (s_3d.c_load < VP_CONSTS) {
            s_3d.c[s_3d.c_load][s_3d.c_comp] = f;
            if (++s_3d.c_comp == 4) { s_3d.c_comp = 0; s_3d.c_load++; }
        }
        return 1;
    }
    if (method >= NV097_SET_COMPOSITE_MATRIX && method < NV097_SET_COMPOSITE_MATRIX + 0x40) {
        s_3d.composite[(method - NV097_SET_COMPOSITE_MATRIX) / 4] = f;
        return 1;
    }
    if (method >= 0x0260 && method < 0x0280) { s_3d.a_icw[(method - 0x0260) / 4] = param; s_3d.comb_seen = 1; return 1; }
    if (method >= 0x0A60 && method < 0x0A80) { s_3d.factor0[(method - 0x0A60) / 4] = param; return 1; }
    if (method >= 0x0A80 && method < 0x0AA0) { s_3d.factor1[(method - 0x0A80) / 4] = param; return 1; }
    if (method >= 0x0AA0 && method < 0x0AC0) { s_3d.a_ocw[(method - 0x0AA0) / 4] = param; return 1; }
    if (method >= 0x0AC0 && method < 0x0AE0) { s_3d.c_icw[(method - 0x0AC0) / 4] = param; s_3d.comb_seen = 1; return 1; }
    if (method >= 0x1E40 && method < 0x1E60) { s_3d.c_ocw[(method - 0x1E40) / 4] = param; return 1; }
    if (method >= 0x09C0 && method < 0x09CC) { memcpy(&s_3d.fog_params[(method - 0x09C0) / 4], &param, 4); return 1; }
    switch (method) {
    case NV097_SET_TRANSFORM_EXECUTION_MODE: s_3d.mode = param & 3; return 1;
    case NV097_SET_TRANSFORM_PROGRAM_LOAD:   s_3d.load_slot = param; s_3d.load_word = 0; return 1;
    case NV097_SET_TRANSFORM_PROGRAM_START:  s_3d.start = param; return 1;
    case NV097_SET_TRANSFORM_CONSTANT_LOAD:  s_3d.c_load = param; s_3d.c_comp = 0; return 1;
    case 0x1E98: return 1;                       /* PROGRAM_CXT_WRITE_EN */
    case NV097_SET_ALPHA_TEST_ENABLE: s_3d.alpha_test = param != 0; return 1;
    case NV097_SET_CULL_FACE_ENABLE:  s_3d.cull_enable = param != 0; return 1;
    case NV097_SET_DEPTH_TEST_ENABLE: s_3d.depth_test = param != 0; return 1;
    case NV097_SET_ALPHA_FUNC:        s_3d.alpha_func = param; return 1;
    case NV097_SET_ALPHA_REF:         s_3d.alpha_ref = param & 0xFF; return 1;
    case NV097_SET_DEPTH_FUNC:        s_3d.depth_func = param; return 1;
    case NV097_SET_DEPTH_MASK:        s_3d.depth_mask = param != 0; return 1;
    case NV097_SET_CULL_FACE:         s_3d.cull_face = param; return 1;
    case NV097_SET_FRONT_FACE:        s_3d.front_face = param; return 1;
    case 0x0358: s_3d.color_mask = param; return 1;        /* SET_COLOR_MASK */
    case 0x0394: s_clip_min = f; return 1;
    case 0x0398: s_clip_max = f; return 1;
    case 0x0214: s_zeta_offset = param; return 1;          /* SET_SURFACE_ZETA_OFFSET */
    case 0x020C: s_zeta_pitch = param >> 16; return 0;     /* SET_SURFACE_PITCH: the executor wants it too */
    case 0x0350: s_3d.blend_eq = param; return 1;          /* SET_BLEND_EQUATION */
    case 0x034C: s_3d.blend_color = param; return 1;       /* SET_BLEND_COLOR */
    case 0x032C: s_3d.stencil_test = param != 0; return 1; /* SET_STENCIL_TEST_ENABLE */
    case 0x0360: s_3d.st_wmask = param & 0xFF; return 1;
    case 0x0364: s_3d.st_func = param; return 1;
    case 0x0368: s_3d.st_ref = param & 0xFF; return 1;
    case 0x036C: s_3d.st_mask = param & 0xFF; return 1;
    case 0x0370: s_3d.st_fail = param; return 1;
    case 0x0374: s_3d.st_zfail = param; return 1;
    case 0x0378: s_3d.st_zpass = param; return 1;
    case 0x0288: s_3d.spec_fog0 = param; return 1;         /* COMBINER_SPECULAR_FOG_CW0 */
    case 0x028C: s_3d.spec_fog1 = param; return 1;         /* COMBINER_SPECULAR_FOG_CW1 */
    case 0x1E60: s_3d.comb_ctl = param; return 1;          /* COMBINER_CONTROL */
    case 0x1E70: s_3d.shader_prog = param; return 1;       /* SHADER_STAGE_PROGRAM */
    case 0x1E74: s_dot_map = param; return 1;              /* DOT_RGBMAPPING */
    case 0x1E78: s_other_in = param; return 1;             /* SHADER_OTHER_STAGE_INPUT */
    case 0x02A4: s_3d.fog_enable = param != 0; return 1;   /* FOG_ENABLE */
    case 0x029C: s_3d.fog_mode = param; return 1;          /* FOG_MODE */
    case 0x02A8: s_3d.fog_color = param; return 1;         /* FOG_COLOR (ABGR) */
    default: return 0;
    }
}

/* ---------------- vertex program interpreter ---------------- */

#define VF(t, dw, pos, len) (((t)[dw] >> (pos)) & ((1u << (len)) - 1u))

enum { MAC_NOP, MAC_MOV, MAC_MUL, MAC_ADD, MAC_MAD, MAC_DP3, MAC_DPH, MAC_DP4,
       MAC_DST, MAC_MIN, MAC_MAX, MAC_SLT, MAC_SGE, MAC_ARL };
enum { ILU_NOP, ILU_MOV, ILU_RCP, ILU_RCC, ILU_RSQ, ILU_EXP, ILU_LOG, ILU_LIT };

#define VP_OUTS 13   /* 0 pos, 3 D0, 4 D1, 5 fog, 6 pts, 7 B0, 8 B1, 9-12 T0-T3 */

static void vp_fetch(const uint32_t *t, uint32_t mux, uint32_t r, int neg,
                     uint32_t sx, uint32_t sy, uint32_t sz, uint32_t sw,
                     float R[16][4], float (*in)[4], float (*out)[4],
                     int a0, float o[4])
{
    const float *s;
    float tmp[4] = {0, 0, 0, 1};
    switch (mux) {
    case 1: s = (r == 12) ? out[0] : R[r]; break;
    case 2: s = in[VF(t, 1, 9, 4)]; break;
    case 3: {
        int ci = (int)VF(t, 1, 13, 8);
        if (VF(t, 3, 1, 1)) ci += a0;
        s = (ci >= 0 && ci < VP_CONSTS) ? s_3d.c[ci] : tmp;
        break;
    }
    default: s = tmp; break;
    }
    o[0] = s[sx]; o[1] = s[sy]; o[2] = s[sz]; o[3] = s[sw];
    if (neg) { o[0] = -o[0]; o[1] = -o[1]; o[2] = -o[2]; o[3] = -o[3]; }
}

static void vp_mask_write(float *d, const float v[4], uint32_t mask)
{
    if (mask & 8) d[0] = v[0];
    if (mask & 4) d[1] = v[1];
    if (mask & 2) d[2] = v[2];
    if (mask & 1) d[3] = v[3];
}

static int vp_run(float (*in)[4], float (*out)[4])
{
    float R[16][4];
    int a0 = 0, n = 0;
    uint32_t pc, i;

    memset(R, 0, sizeof R);
    for (i = 0; i < VP_OUTS; i++) {
        out[i][0] = out[i][1] = out[i][2] = 0.0f;
        out[i][3] = 1.0f;
    }
    out[3][0] = out[3][1] = out[3][2] = 1.0f;     /* unwritten diffuse: white */

    for (pc = s_3d.start; pc < VP_SLOTS && n < VP_SLOTS; pc++, n++) {
        const uint32_t *t = s_3d.prog[pc];
        uint32_t ilu = VF(t, 1, 25, 3), mac = VF(t, 1, 21, 4);
        float A[4], B[4], C[4], m[4] = {0, 0, 0, 0}, l[4] = {0, 0, 0, 0};
        uint32_t mac_mask = VF(t, 3, 24, 4), out_r = VF(t, 3, 20, 4);
        uint32_t ilu_mask = VF(t, 3, 16, 4), o_mask = VF(t, 3, 12, 4);
        uint32_t orb = VF(t, 3, 11, 1), oaddr = VF(t, 3, 3, 8), omux = VF(t, 3, 2, 1);

        vp_fetch(t, VF(t, 2, 26, 2), VF(t, 2, 28, 4), VF(t, 1, 8, 1),
                 VF(t, 1, 6, 2), VF(t, 1, 4, 2), VF(t, 1, 2, 2), VF(t, 1, 0, 2),
                 R, in, out, a0, A);
        vp_fetch(t, VF(t, 2, 11, 2), VF(t, 2, 13, 4), VF(t, 2, 25, 1),
                 VF(t, 2, 23, 2), VF(t, 2, 21, 2), VF(t, 2, 19, 2), VF(t, 2, 17, 2),
                 R, in, out, a0, B);
        vp_fetch(t, VF(t, 3, 28, 2), (VF(t, 2, 0, 2) << 2) | VF(t, 3, 30, 2), VF(t, 2, 10, 1),
                 VF(t, 2, 8, 2), VF(t, 2, 6, 2), VF(t, 2, 4, 2), VF(t, 2, 2, 2),
                 R, in, out, a0, C);

        switch (mac) {
        case MAC_MOV: memcpy(m, A, sizeof m); break;
        case MAC_MUL: for (i = 0; i < 4; i++) m[i] = A[i] * B[i]; break;
        case MAC_ADD: for (i = 0; i < 4; i++) m[i] = A[i] + C[i]; break;
        case MAC_MAD: for (i = 0; i < 4; i++) m[i] = A[i] * B[i] + C[i]; break;
        case MAC_DP3: m[0] = m[1] = m[2] = m[3] = A[0]*B[0] + A[1]*B[1] + A[2]*B[2]; break;
        case MAC_DPH: m[0] = m[1] = m[2] = m[3] = A[0]*B[0] + A[1]*B[1] + A[2]*B[2] + B[3]; break;
        case MAC_DP4: m[0] = m[1] = m[2] = m[3] = A[0]*B[0] + A[1]*B[1] + A[2]*B[2] + A[3]*B[3]; break;
        case MAC_DST: m[0] = 1.0f; m[1] = A[1] * B[1]; m[2] = A[2]; m[3] = B[3]; break;
        case MAC_MIN: for (i = 0; i < 4; i++) m[i] = A[i] < B[i] ? A[i] : B[i]; break;
        case MAC_MAX: for (i = 0; i < 4; i++) m[i] = A[i] > B[i] ? A[i] : B[i]; break;
        case MAC_SLT: for (i = 0; i < 4; i++) m[i] = A[i] <  B[i] ? 1.0f : 0.0f; break;
        case MAC_SGE: for (i = 0; i < 4; i++) m[i] = A[i] >= B[i] ? 1.0f : 0.0f; break;
        case MAC_ARL: a0 = (int)floorf(A[0] + 0.001f); break;
        default: break;
        }

        switch (ilu) {
        case ILU_MOV: memcpy(l, C, sizeof l); break;
        case ILU_RCP: l[0] = l[1] = l[2] = l[3] = 1.0f / C[0]; break;
        case ILU_RCC: {
            float r = 1.0f / C[0];
            float ar = fabsf(r);
            if (ar < 5.42101e-20f) ar = 5.42101e-20f;
            else if (ar > 1.884467e+19f) ar = 1.884467e+19f;
            r = (r < 0 || (r == 0 && signbit(r))) ? -ar : ar;
            l[0] = l[1] = l[2] = l[3] = r;
            break;
        }
        case ILU_RSQ: l[0] = l[1] = l[2] = l[3] = 1.0f / sqrtf(fabsf(C[0])); break;
        case ILU_EXP: {
            float fl = floorf(C[0]);
            l[0] = exp2f(fl); l[1] = C[0] - fl; l[2] = exp2f(C[0]); l[3] = 1.0f;
            break;
        }
        case ILU_LOG: {
            float x = fabsf(C[0]);
            if (x == 0.0f) { l[0] = l[2] = -INFINITY; l[1] = 1.0f; }
            else { float e = floorf(log2f(x)); l[0] = e; l[1] = x / exp2f(e); l[2] = log2f(x); }
            l[3] = 1.0f;
            break;
        }
        case ILU_LIT: {
            float nl = C[0] > 0 ? C[0] : 0, nh = C[1] > 0 ? C[1] : 0;
            float p = C[3]; if (p < -127.9961f) p = -127.9961f; if (p > 127.9961f) p = 127.9961f;
            l[0] = 1.0f; l[1] = nl; l[2] = (C[0] > 0) ? powf(nh, p) : 0.0f; l[3] = 1.0f;
            break;
        }
        default: break;
        }

        if (mac && mac != MAC_ARL && mac_mask && out_r < 16 && out_r != 12)
            vp_mask_write(R[out_r], m, mac_mask);
        if (ilu && ilu_mask) {
            uint32_t d = mac ? 1 : out_r;
            if (d < 16 && d != 12)
                vp_mask_write(R[d], l, ilu_mask);
        }
        if (o_mask) {
            const float *v = omux ? l : m;
            if (orb) {
                if (oaddr < VP_OUTS) vp_mask_write(out[oaddr], v, o_mask);
            } else if (oaddr < VP_CONSTS) {
                vp_mask_write(s_3d.c[oaddr], v, o_mask);
            }
        }
        if (VF(t, 3, 0, 1))
            return 1;
    }
    return 0;
}

/* ---------------- decoded vertex program ----------------
 *
 * vp_run decodes every bit field of every instruction for every vertex.
 * The program only changes between batches, so it is decoded once into
 * VpIns and the per-vertex loop just executes (vp_run_fast). The decode is
 * re-validated per batch by comparing the program words it came from, which
 * also covers capture replays that restore s_3d wholesale.
 * RECOMP_VP_SLOW=1 uses the old interpreter. */
typedef struct {
    uint8_t mux, reg, neg, rel;
    uint8_t sw[4];
    int16_t ci;
} VpSrc;
typedef struct {
    uint8_t mac, ilu, mac_mask, out_r, ilu_mask, o_mask, orb, oaddr, omux, final;
    VpSrc s[3];
} VpIns;
static VpIns s_vpd[VP_SLOTS];
static uint32_t s_vpd_src[VP_SLOTS][4];
static uint32_t s_vpd_start = 0xFFFFFFFFu, s_vpd_len;
static int s_vpd_cwrite;          /* the program writes constants: not thread-safe */

static void vp_dec_src(VpSrc *d, const uint32_t *t, uint32_t mux, uint32_t r, int neg,
                       uint32_t sx, uint32_t sy, uint32_t sz, uint32_t sw)
{
    d->mux = (uint8_t)mux; d->reg = (uint8_t)r; d->neg = (uint8_t)neg;
    d->sw[0] = (uint8_t)sx; d->sw[1] = (uint8_t)sy; d->sw[2] = (uint8_t)sz; d->sw[3] = (uint8_t)sw;
    d->ci = (int16_t)VF(t, 1, 13, 8); d->rel = (uint8_t)VF(t, 3, 1, 1);
    if (mux == 2) d->reg = (uint8_t)VF(t, 1, 9, 4);
}

/* Decode (or re-validate) the program for the current batch. */
static void vp_decode(void)
{
    uint32_t pc, n = 0;
    if (s_vpd_start == s_3d.start && s_vpd_len &&
        s_3d.start + s_vpd_len <= VP_SLOTS &&
        !memcmp(s_vpd_src, s_3d.prog[s_3d.start], s_vpd_len * 16))
        return;
    s_vpd_cwrite = 0;
    for (pc = s_3d.start; pc < VP_SLOTS; pc++, n++) {
        const uint32_t *t = s_3d.prog[pc];
        VpIns *I = &s_vpd[n];
        memcpy(s_vpd_src[n], t, 16);
        I->ilu = (uint8_t)VF(t, 1, 25, 3); I->mac = (uint8_t)VF(t, 1, 21, 4);
        I->mac_mask = (uint8_t)VF(t, 3, 24, 4); I->out_r = (uint8_t)VF(t, 3, 20, 4);
        I->ilu_mask = (uint8_t)VF(t, 3, 16, 4); I->o_mask = (uint8_t)VF(t, 3, 12, 4);
        I->orb = (uint8_t)VF(t, 3, 11, 1); I->oaddr = (uint8_t)VF(t, 3, 3, 8); I->omux = (uint8_t)VF(t, 3, 2, 1);
        I->final = (uint8_t)VF(t, 3, 0, 1);
        vp_dec_src(&I->s[0], t, VF(t, 2, 26, 2), VF(t, 2, 28, 4), VF(t, 1, 8, 1),
                   VF(t, 1, 6, 2), VF(t, 1, 4, 2), VF(t, 1, 2, 2), VF(t, 1, 0, 2));
        vp_dec_src(&I->s[1], t, VF(t, 2, 11, 2), VF(t, 2, 13, 4), VF(t, 2, 25, 1),
                   VF(t, 2, 23, 2), VF(t, 2, 21, 2), VF(t, 2, 19, 2), VF(t, 2, 17, 2));
        vp_dec_src(&I->s[2], t, VF(t, 3, 28, 2), (VF(t, 2, 0, 2) << 2) | VF(t, 3, 30, 2), VF(t, 2, 10, 1),
                   VF(t, 2, 8, 2), VF(t, 2, 6, 2), VF(t, 2, 4, 2), VF(t, 2, 2, 2));
        if (I->o_mask && !I->orb) s_vpd_cwrite = 1;
        if (I->final) { n++; break; }
    }
    s_vpd_start = s_3d.start;
    s_vpd_len = n;
}

static inline void vp_fetch_fast(const VpSrc *d, float R[16][4], float (*in)[4],
                                 float (*out)[4], int a0, float o[4])
{
    static const float zero[4] = {0, 0, 0, 1};
    const float *s;
    switch (d->mux) {
    case 1: s = (d->reg == 12) ? out[0] : R[d->reg]; break;
    case 2: s = in[d->reg]; break;
    case 3: {
        int ci = d->ci + (d->rel ? a0 : 0);
        s = (ci >= 0 && ci < VP_CONSTS) ? s_3d.c[ci] : zero;
        break;
    }
    default: s = zero; break;
    }
    if (d->neg) { o[0] = -s[d->sw[0]]; o[1] = -s[d->sw[1]]; o[2] = -s[d->sw[2]]; o[3] = -s[d->sw[3]]; }
    else        { o[0] =  s[d->sw[0]]; o[1] =  s[d->sw[1]]; o[2] =  s[d->sw[2]]; o[3] =  s[d->sw[3]]; }
}

static int vp_run_fast(float (*in)[4], float (*out)[4])
{
    float R[16][4];
    int a0 = 0;
    uint32_t k, i;

    memset(R, 0, sizeof R);
    for (i = 0; i < VP_OUTS; i++) {
        out[i][0] = out[i][1] = out[i][2] = 0.0f;
        out[i][3] = 1.0f;
    }
    out[3][0] = out[3][1] = out[3][2] = 1.0f;

    for (k = 0; k < s_vpd_len; k++) {
        const VpIns *I = &s_vpd[k];
        float A[4], B[4], C[4], m[4] = {0, 0, 0, 0}, l[4] = {0, 0, 0, 0};
        vp_fetch_fast(&I->s[0], R, in, out, a0, A);
        vp_fetch_fast(&I->s[1], R, in, out, a0, B);
        vp_fetch_fast(&I->s[2], R, in, out, a0, C);
        switch (I->mac) {
        case MAC_MOV: memcpy(m, A, sizeof m); break;
        case MAC_MUL: for (i = 0; i < 4; i++) m[i] = A[i] * B[i]; break;
        case MAC_ADD: for (i = 0; i < 4; i++) m[i] = A[i] + C[i]; break;
        case MAC_MAD: for (i = 0; i < 4; i++) m[i] = A[i] * B[i] + C[i]; break;
        case MAC_DP3: m[0] = m[1] = m[2] = m[3] = A[0]*B[0] + A[1]*B[1] + A[2]*B[2]; break;
        case MAC_DPH: m[0] = m[1] = m[2] = m[3] = A[0]*B[0] + A[1]*B[1] + A[2]*B[2] + B[3]; break;
        case MAC_DP4: m[0] = m[1] = m[2] = m[3] = A[0]*B[0] + A[1]*B[1] + A[2]*B[2] + A[3]*B[3]; break;
        case MAC_DST: m[0] = 1.0f; m[1] = A[1] * B[1]; m[2] = A[2]; m[3] = B[3]; break;
        case MAC_MIN: for (i = 0; i < 4; i++) m[i] = A[i] < B[i] ? A[i] : B[i]; break;
        case MAC_MAX: for (i = 0; i < 4; i++) m[i] = A[i] > B[i] ? A[i] : B[i]; break;
        case MAC_SLT: for (i = 0; i < 4; i++) m[i] = A[i] <  B[i] ? 1.0f : 0.0f; break;
        case MAC_SGE: for (i = 0; i < 4; i++) m[i] = A[i] >= B[i] ? 1.0f : 0.0f; break;
        case MAC_ARL: a0 = (int)floorf(A[0] + 0.001f); break;
        default: break;
        }
        switch (I->ilu) {
        case ILU_MOV: memcpy(l, C, sizeof l); break;
        case ILU_RCP: l[0] = l[1] = l[2] = l[3] = 1.0f / C[0]; break;
        case ILU_RCC: {
            float r = 1.0f / C[0];
            float ar = fabsf(r);
            if (ar < 5.42101e-20f) ar = 5.42101e-20f;
            else if (ar > 1.884467e+19f) ar = 1.884467e+19f;
            r = (r < 0 || (r == 0 && signbit(r))) ? -ar : ar;
            l[0] = l[1] = l[2] = l[3] = r;
            break;
        }
        case ILU_RSQ: l[0] = l[1] = l[2] = l[3] = 1.0f / sqrtf(fabsf(C[0])); break;
        case ILU_EXP: {
            float fl = floorf(C[0]);
            l[0] = exp2f(fl); l[1] = C[0] - fl; l[2] = exp2f(C[0]); l[3] = 1.0f;
            break;
        }
        case ILU_LOG: {
            float x = fabsf(C[0]);
            if (x == 0.0f) { l[0] = l[2] = -INFINITY; l[1] = 1.0f; }
            else { float e = floorf(log2f(x)); l[0] = e; l[1] = x / exp2f(e); l[2] = log2f(x); }
            l[3] = 1.0f;
            break;
        }
        case ILU_LIT: {
            float nl = C[0] > 0 ? C[0] : 0, nh = C[1] > 0 ? C[1] : 0;
            float p = C[3]; if (p < -127.9961f) p = -127.9961f; if (p > 127.9961f) p = 127.9961f;
            l[0] = 1.0f; l[1] = nl; l[2] = (C[0] > 0) ? powf(nh, p) : 0.0f; l[3] = 1.0f;
            break;
        }
        default: break;
        }
        if (I->mac && I->mac != MAC_ARL && I->mac_mask && I->out_r < 16 && I->out_r != 12)
            vp_mask_write(R[I->out_r], m, I->mac_mask);
        if (I->ilu && I->ilu_mask) {
            uint32_t d = I->mac ? 1 : I->out_r;
            if (d < 16 && d != 12)
                vp_mask_write(R[d], l, I->ilu_mask);
        }
        if (I->o_mask) {
            const float *v = I->omux ? l : m;
            if (I->orb) {
                if (I->oaddr < VP_OUTS) vp_mask_write(out[I->oaddr], v, I->o_mask);
            } else if (I->oaddr < VP_CONSTS) {
                vp_mask_write(s_3d.c[I->oaddr], v, I->o_mask);
            }
        }
        if (I->final)
            return 1;
    }
    return 0;
}

static int s_vp_slow = -1;

/* ---------------- vertex fetch & transform ---------------- */

/* Interpolated attributes: diffuse, specular, fog, four texcoords. */
#define SA_D0   0
#define SA_D1   4
#define SA_FOG  8
#define SA_T0   9
#define SW_NATTR 25

typedef struct {
    float p[4];              /* x*w, y*w, z*w, w : linear in clip space */
    float a[SW_NATTR];
} SwVert;

static int fetch_attr_any(uint32_t a, uint32_t index, float out[4])
{
    const VertexAttr *at = &s_gpu.attr[a];
    if (!at->size || !at->stride) {
        out[0] = out[1] = out[2] = 0.0f; out[3] = 1.0f;
        return 0;
    }
    return fetch_attr(at, index, out);
}

static int sw3d_transform(uint32_t index, SwVert *v)
{
    float in[16][4], out[VP_OUTS][4];
    uint32_t a;
    float w, sx, sy, sz;

    for (a = 0; a < 16; a++)
        fetch_attr_any(a, index, in[a]);

    if (s_3d.mode == 2) {
        if (s_vp_slow) vp_run(in, out); else vp_run_fast(in, out);
        sx = out[0][0]; sy = out[0][1]; sz = out[0][2]; w = out[0][3];
        memcpy(&v->a[SA_D0], out[3], 4 * sizeof(float));
        memcpy(&v->a[SA_D1], out[4], 4 * sizeof(float));
        v->a[SA_FOG] = out[5][0];
        for (a = 0; a < 4; a++)
            memcpy(&v->a[SA_T0 + 4 * a], out[9 + a], 4 * sizeof(float));
    } else {
        const float *M = s_3d.composite;
        const float *P = in[0];
        float c[4];
        int i;
        for (i = 0; i < 4; i++)
            c[i] = P[0]*M[0*4+i] + P[1]*M[1*4+i] + P[2]*M[2*4+i] + P[3]*M[3*4+i];
        w = c[3];
        if (w == 0.0f) w = 1.0f;
        sx = c[0] / w; sy = c[1] / w; sz = c[2] / w;
        if (s_gpu.attr[3].size) memcpy(&v->a[SA_D0], in[3], 4 * sizeof(float));
        else v->a[SA_D0] = v->a[SA_D0 + 1] = v->a[SA_D0 + 2] = v->a[SA_D0 + 3] = 1.0f;
        memcpy(&v->a[SA_D1], in[4], 4 * sizeof(float));
        if (!s_gpu.attr[4].size) v->a[SA_D1] = v->a[SA_D1 + 1] = v->a[SA_D1 + 2] = v->a[SA_D1 + 3] = 0.0f;
        v->a[SA_FOG] = in[5][0];
        for (a = 0; a < 4; a++)
            memcpy(&v->a[SA_T0 + 4 * a], in[9 + a], 4 * sizeof(float));
    }
    for (a = 0; a < 8; a++) {
        if (v->a[a] < 0.0f) v->a[a] = 0.0f;
        if (v->a[a] > 1.0f) v->a[a] = 1.0f;
    }
    if (!isfinite(w) || !isfinite(sx) || !isfinite(sy)) return 0;
    v->p[0] = sx * w; v->p[1] = sy * w; v->p[2] = sz * w; v->p[3] = w;
    return 1;
}

/* ---------------- texture stages ---------------- */

typedef struct {
    Texture t;
    int     enabled;
    int     normalized;      /* swizzled/compressed: coordinates in [0,1] */
    int     bilinear;
    int     mip;             /* minification uses mipmaps (MIN filter >= 3) */
    uint32_t mode;           /* shader stage program */
    const uint32_t *texels;  /* decoded ARGB (texture cache), or NULL */
    int pow2;
} SwStage;

static SwStage s_stage[4];
static int s_diag_px;
static int s_tex_used[4];
static uint8_t s_attr_need[SW_NATTR];
static uint64_t s_st_bbox, s_st_inside, s_st_shaded;

/* Which of T0-T3 the programmed combiners actually read: sampling a stage is
 * most of the per-pixel cost, and most passes read one or two. */
static void sw3d_tex_usage(void)
{
    uint32_t n = s_3d.comb_ctl & 0xFF, i, b;
    int r0 = 0;
    memset(s_tex_used, 0, sizeof s_tex_used);
    if (!s_3d.comb_seen) { s_tex_used[0] = 1; return; }
    if (n > 8) n = 8;
#define MARK(byte) do { uint32_t r_ = (byte) & 0xF; if (r_ >= 8 && r_ <= 11) s_tex_used[r_ - 8] = 1; \
                        if (r_ == 12 || r_ == 14) r0 = 1; } while (0)
    for (i = 0; i < n; i++)
        for (b = 0; b < 32; b += 8) { MARK(s_3d.c_icw[i] >> b); MARK(s_3d.a_icw[i] >> b); }
    for (b = 0; b < 32; b += 8) { MARK(s_3d.spec_fog0 >> b); MARK(s_3d.spec_fog1 >> b); }
#undef MARK
    if (r0) s_tex_used[0] = 1;                /* R0.a starts as T0.a */
}

/* Decoded-texture cache. Sampling through the format decoder costs a block
 * decode per texel for DXT and a swizzle per texel otherwise; decoding a
 * texture once into ARGB and indexing that is what makes per-pixel
 * multi-texture affordable. An entry is re-decoded when a checksum of its
 * first and middle bytes changes (render targets, streamed textures). */
/* (1.9.3: 2048, was 256 -- a busy scene with an extra hero's effects
 * used over 300 textures a frame, and each frame decoded them all again,
 * the least recently used pushing out the next one needed: 20 fps) */
#define TEXC_SLOTS 2048
typedef struct {
    uint32_t off, color, w, h, pitch, sum, gen, ver, hash;
    uint32_t *argb;
    uint64_t last_use;
    uint32_t run;            /* pushbuffer run the checksum was last taken in */
    long trev;               /* xbox_tex_rev when decoded */
    int uidx;                /* its Ultra textures picture, or -1 (nv2a_ultra.inc) */
    int redec;               /* decoded again with new content (a movie's frames, streamed pictures) */
} TexCacheEnt;
extern volatile uint32_t g_pb_runs;
static TexCacheEnt s_texc[TEXC_SLOTS];
static uint64_t s_texc_clock;
/* (1.9.3) Frames presented (the D3D11 renderer counts them). Swizzled
 * textures -- loaded art the CPU does not rewrite as it goes -- have their
 * content checked once a frame rather than once a kick: re-summing a couple
 * of hundred textures at every kick was a large part of the render thread's
 * time. The tick advances with presented frames, but at most every 8 ms:
 * at hundreds of frames a second, once a frame was still most of the
 * render thread's own work. Linear ones (movie frames, the CPU's own pictures) are still checked
 * at every kick. RECOMP_TEX_CHECK_KICK=1: every texture at every kick. */
static uint32_t s_texc_frame;
/* (1.9.3) a slow frame's breakdown for the log ([SLOWFRAME], nv2a_d3d11.inc) */
static long long g_slow_dec_t, g_slow_up_t, g_slow_sync_t, g_slow_sh_t, g_slow_rt_t;
static unsigned g_slow_dec_n, g_slow_up_n, g_slow_sync_n, g_slow_sh_n, g_slow_rb_n, g_slow_new_n;
static uint32_t g_slow_big_w, g_slow_big_h, g_slow_big_fmt, g_slow_big_off, g_slow_big_why, g_slow_big_px;
/* the epoch a texture's content check is good for: a kick for linear ones,
 * a texture-check tick for swizzled ones (see above) */
static uint32_t texc_epoch(const Texture *t)
{
    static int per_kick = -1;
    if (per_kick < 0) per_kick = getenv("RECOMP_TEX_CHECK_KICK") != NULL;
    /* (swizzled or compressed by its format: the pitch register is set for
     * those too, so it does not tell a linear texture) */
    return (tex_size_from_format(t->color) && s_texc_frame && !per_kick) ? (s_texc_frame | 0x80000000u) : g_pb_runs;
}

static uint32_t texc_sum(const Texture *t)
{
    const uint8_t *m = (const uint8_t *)xbox_GetMemoryOffset() + t->offset;
    uint32_t i, h = 2166136261u;
    /* The texture's whole extent in bytes (the old sum only looked at the
     * first width*height BYTES -- a quarter of a 32-bit texture -- and froze
     * the New Line Cinema logo on screen on one PC while the movie played
     * on underneath). Linear textures (movie frames, loading screens: the
     * CPU rewrites them) are hashed completely, every 4th word; swizzled
     * ones at 4096 points spread over all of it. */
    uint32_t bytes = t->pitch ? t->pitch * t->height : t->width * t->height * 4;
    if (bytes > (16u << 20)) bytes = 16u << 20;
    bytes &= ~3u;
    if (bytes < 16) return 0;
    if (t->pitch && !tex_size_from_format(t->color)) {   /* (1.9.3: by the format -- see texc_epoch) */
        for (i = 0; i + 4 <= bytes; i += 64) h = (h ^ *(const uint32_t *)(m + i)) * 16777619u;
    } else {
        uint32_t step = bytes > 16384 ? (bytes / 4096) & ~3u : 4;
        for (i = 0; i + 4 <= bytes; i += step) h = (h ^ *(const uint32_t *)(m + i)) * 16777619u;
    }
    return h;
}

static uint32_t s_texc_last_ver, s_texc_last_hash;
static int s_texc_last_uidx = -1;
static const uint32_t *texc_get(const Texture *t);
/* Every decoded texture passes here once per (re)decode.
 *  - RECOMP_TEX_DUMP=<dir>: each distinct texture (by content hash) is saved
 *    once as <dir>/<hash>_<w>x<h>_f<fmt>.bmp, for finding textures to change.
 *  - xbox_TexRecolorSet(hash, mode): the title's texture mods (recolour).
 * The hash is FNV-1a over the decoded ARGB pixels, so it is the same every
 * run and every load of that texture. */
#define TEXREC_MAX 64
static struct { uint32_t hash; int mode; } s_texrec[TEXREC_MAX];
static int s_texrec_n;
static volatile int s_texrec_on = 1;
void xbox_TexRecolorSet(uint32_t hash, int mode)
{
    int i;
    for (i = 0; i < s_texrec_n; i++) if (s_texrec[i].hash == hash) { s_texrec[i].mode = mode; return; }
    if (s_texrec_n < TEXREC_MAX) { s_texrec[s_texrec_n].hash = hash; s_texrec[s_texrec_n].mode = mode; s_texrec_n++; }
}
void xbox_TexRecolorEnable(int on) { s_texrec_on = on; }
volatile long xbox_tex_rev;          /* bumped to re-decode everything (mods toggled) */
/* The title's texture hook: sees (and may change) every texture as it is
 * decoded, with its content hash -- for texture mods, or to pick art out of
 * the game's own textures. Renderer thread. */
void (*volatile xbox_tex_observer)(uint32_t hash, uint32_t *argb, int w, int h);

/* mode 1: green -> E3 amber/gold, keeping brightness and detail */
static uint32_t texrec_amber(uint32_t c)
{
    float a = (float)(c >> 24), r = (float)((c >> 16) & 255) / 255.0f, g = (float)((c >> 8) & 255) / 255.0f,
          b = (float)(c & 255) / 255.0f;
    float mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    float d = mx - mn, h, sat, v = mx, k;
    if (d < 0.02f || mx <= 0.0f) return c;                       /* grey: leave */
    if (mx == r) h = 60.0f * ((g - b) / d); else if (mx == g) h = 60.0f * ((b - r) / d) + 120.0f;
    else h = 60.0f * ((r - g) / d) + 240.0f;
    if (h < 0) h += 360.0f;
    if (h < 60.0f || h > 185.0f) return c;                        /* only the greens */
    sat = d / mx * 1.2f; if (sat > 1.0f) sat = 1.0f;
    h = 44.0f + (h - 120.0f) * 0.12f;                             /* ~44 deg: E3 yellow-amber */
    {
        float hh = h / 60.0f, f, p, q, tt, R, G, B;
        int ii = (int)hh;
        f = hh - (float)ii; p = v * (1 - sat); q = v * (1 - sat * f); tt = v * (1 - sat * (1 - f));
        switch (ii) { case 0: R = v; G = tt; B = p; break; case 1: R = q; G = v; B = p; break;
                      default: R = v; G = p; B = q; break; }
        k = 255.0f;
        return ((uint32_t)a << 24) | ((uint32_t)(R * k + 0.5f) << 16) | ((uint32_t)(G * k + 0.5f) << 8) | (uint32_t)(B * k + 0.5f);
    }
}

static int s_texc_hook_changed;
static uint32_t texc_hook(uint32_t *argb, const Texture *t)
{
    static const char *dump = (const char *)1;
    static int dump_big;     /* RECOMP_TEX_DUMP_ALL: the big ones too (loading screens) */
    uint32_t h = 2166136261u, i, n = t->width * t->height;
    s_texc_hook_changed = 0;
    for (i = 0; i < n; i++) h = (h ^ argb[i]) * 16777619u;
    h ^= (t->width << 16) | t->height;
    if (dump == (const char *)1) { dump = getenv("RECOMP_TEX_DUMP"); dump_big = getenv("RECOMP_TEX_DUMP_ALL") != NULL; }
    if (dump && (n < 512u * 448u || dump_big)) {    /* not movie frames / full-screen surfaces */
        static uint32_t seen[8192]; static int ns;
        int k;
        for (k = 0; k < ns && seen[k] != h; k++) {}
        if (k == ns && ns < 8192) {
            char nm[400]; FILE *o;
            seen[ns++] = h;
            snprintf(nm, sizeof nm, "%s/%08X_%ux%u_f%02X.bmp", dump, h, t->width, t->height, t->color & 0xFF);
            if ((o = fopen(nm, "wb"))) {
                uint8_t hd[54] = { 'B', 'M' }; uint32_t img = n * 4, tot = img + 54; int32_t hh = -(int32_t)t->height;
                memcpy(hd + 2, &tot, 4); hd[10] = 54; hd[14] = 40; memcpy(hd + 18, &t->width, 4); memcpy(hd + 22, &hh, 4);
                hd[26] = 1; hd[28] = 32; memcpy(hd + 34, &img, 4);
                fwrite(hd, 1, 54, o); fwrite(argb, 4, n, o); fclose(o);
            }
            fprintf(stderr, "  [TEXDUMP] %08X %ux%u fmt %02X at %08X\n", h, t->width, t->height, t->color & 0xFF, t->offset);
        }
    }
    if (xbox_tex_observer) {
        /* (a texture the title changes -- a mod's recolouring or picture --
         * keeps the title's version: no Ultra textures picture for it) */
        uint32_t h2 = 2166136261u;
        xbox_tex_observer(h, argb, (int)t->width, (int)t->height);
        for (i = 0; i < n; i++) h2 = (h2 ^ argb[i]) * 16777619u;
        h2 ^= (t->width << 16) | t->height;
        s_texc_hook_changed = h2 != h;
    }
    if (s_texrec_on)
        for (i = 0; i < (uint32_t)s_texrec_n; i++)
            if (s_texrec[i].hash == h && s_texrec[i].mode == 1) {
                uint32_t j;
                for (j = 0; j < n; j++) argb[j] = texrec_amber(argb[j]);
                s_texc_hook_changed = 1;
                break;
            }
    return h;
}

#include "nv2a_ultra.inc"

static const uint32_t *texc_get_ver(const Texture *t, uint32_t *ver)
{
    const uint32_t *r = texc_get(t);
    *ver = s_texc_last_ver;
    return r;
}
static const uint32_t *texc_get(const Texture *t)
{
    TexCacheEnt *e = NULL, *victim = NULL;
    int i;
    uint32_t sum, gen, x, y, ep;
    s_texc_last_uidx = -1;
    if (!t->valid || t->width > 2048 || t->height > 2048) return NULL;
    (void)ultra_on();   /* (turned on or off: everything decoded again) */
    {
        uint32_t bytes = t->pitch ? t->pitch * t->height : t->width * t->height * 4;
        gen = gpu_written_gen(t->offset, bytes);
    }
    s_texc_clock++;
    {   /* (1.9.3) the slot this texture had last time, from a small index:
         * scanning all 256 slots on every bind (several a draw, a thousand
         * draws a frame) was a large part of the render thread's own time.
         * The full scan only on a miss. */
        static uint16_t idx[8192];
        uint32_t k = ((t->offset >> 6) ^ (t->offset >> 17) ^ (t->color * 0x9E37u) ^ (t->width << 3) ^ (t->height << 7)) & 8191u;
        int q = (int)idx[k] - 1;
        if (q >= 0) {
            TexCacheEnt *c = &s_texc[q];
            if (c->argb && c->off == t->offset && c->color == t->color && c->w == t->width
                && c->h == t->height && c->pitch == t->pitch) e = c;
        }
        if (!e) {
            for (i = 0; i < TEXC_SLOTS; i++) {
                TexCacheEnt *c = &s_texc[i];
                if (c->argb && c->off == t->offset && c->color == t->color && c->w == t->width
                    && c->h == t->height && c->pitch == t->pitch) { e = c; break; }
                if (!victim || (!c->argb && victim->argb) || (c->argb && victim->argb && c->last_use < victim->last_use))
                    victim = c;
            }
            if (!e && victim) idx[k] = (uint16_t)(victim - s_texc + 1);
        }
        if (e) idx[k] = (uint16_t)(e - s_texc + 1);
    }
    /* The CPU can only change a texture between pushbuffer runs: within a
     * run, a texture already checked is current (a texture is bound many
     * times a frame, and the checksum was most of the renderer's CPU time). */
    ep = texc_epoch(t);
    if (e && e->run == ep && e->gen == gen && e->trev == xbox_tex_rev) { e->last_use = s_texc_clock; s_texc_last_ver = e->ver; s_texc_last_hash = e->hash; s_texc_last_uidx = e->uidx; return e->argb; }
    sum = texc_sum(t);
    if (e && e->sum == sum && e->gen == gen && e->trev == xbox_tex_rev) { e->run = ep; e->last_use = s_texc_clock; s_texc_last_ver = e->ver; s_texc_last_hash = e->hash; s_texc_last_uidx = e->uidx; return e->argb; }
    {   /* (for [SLOWFRAME]: why this decode, and the biggest of the frame) */
        uint32_t why = !e ? 0 : e->gen != gen ? 2 : e->trev != xbox_tex_rev ? 3 : 1;   /* new / changed / GPU wrote / settings */
        uint32_t px = t->width * t->height;
        g_slow_dec_n++;
        if (px > g_slow_big_px) { g_slow_big_px = px; g_slow_big_w = t->width; g_slow_big_h = t->height; g_slow_big_fmt = t->color; g_slow_big_off = t->offset; g_slow_big_why = why; }
    }
    if (e && e->trev == xbox_tex_rev) e->redec++;
    if (!e) {
        e = victim;
        e->redec = 0;
        free(e->argb);
        e->argb = (uint32_t *)malloc((size_t)t->width * t->height * 4);
        if (!e->argb) return NULL;
        e->off = t->offset; e->color = t->color; e->w = t->width; e->h = t->height; e->pitch = t->pitch;
    }
    e->sum = sum;
    e->gen = gen;
    e->trev = xbox_tex_rev;
    e->run = ep;
    { static uint32_t vc; e->ver = ++vc; s_texc_last_ver = e->ver; }
    e->last_use = s_texc_clock;
    s_cur_tex = t;
    LARGE_INTEGER dq0, dq1;
    QueryPerformanceCounter(&dq0);
    for (y = 0; y < t->height; y++)
        for (x = 0; x < t->width; x++) {
            uint32_t v;
            if (!sample_texture(x, y, &v)) {
                static uint32_t bad[256];
                if (bad[t->color & 0xFF]++ == 0)
                    fprintf(stderr, "  [GPU3D] texture format 0x%02X not sampled (%ux%u at %08X)\n",
                            t->color, t->width, t->height, t->offset);
                v = 0xFFFF00FFu;           /* magenta: visibly "unsupported" */
            }
            e->argb[y * t->width + x] = v;
        }
    s_cur_tex = &s_gpu.tex;
    e->hash = texc_hook(e->argb, t);
    QueryPerformanceCounter(&dq1);
    g_slow_dec_t += dq1.QuadPart - dq0.QuadPart;
    s_texc_last_hash = e->hash;
    e->uidx = -1;
    if (!gen && !s_texc_hook_changed && e->redec < 2 && g_ultra_on == 1 && ultra_on()) {   /* (not a picture the GPU drew, nor one that keeps changing) */
        e->uidx = ultra_lookup(e->hash, e->argb, (int)t->width, (int)t->height);
        if (e->uidx >= 0) ultra_request(e->uidx);
    }
    s_texc_last_uidx = e->uidx;
    return e->argb;
}

static void sw3d_setup_stages(void)
{
    int st;
    for (st = 0; st < 4; st++) {
        const uint32_t *r = &s_tex_reg[st * 0x10];
        SwStage *S = &s_stage[st];
        uint32_t fmt = r[1];
        memset(S, 0, sizeof *S);
        S->mode = (s_3d.shader_prog >> (5 * st)) & 0x1F;
        S->t.color = (fmt >> 8) & 0xFF;
        S->t.offset = r[0] ? dma_resolve(r[0]) : 0;
        S->normalized = tex_size_from_format(S->t.color);
        if (S->normalized) {
            S->t.width  = 1u << ((fmt >> 20) & 0xF);
            S->t.height = 1u << ((fmt >> 24) & 0xF);
        } else {
            S->t.width  = r[7] >> 16;
            S->t.height = r[7] & 0xFFFF;
        }
        S->t.pitch  = r[4] >> 16;
        S->t.addr_u = r[2] & 0xF;
        S->t.addr_v = (r[2] >> 8) & 0xF;
        S->t.valid  = S->t.offset && S->t.width && S->t.height && (S->normalized || S->t.pitch);
        S->enabled  = ((r[3] >> 30) & 1) && S->t.valid;
        S->bilinear = ((r[5] >> 24) & 0xF) >= 2;
        S->mip = ((r[5] >> 16) & 0xFF) >= 3 && ((r[5] >> 16) & 0xFF) <= 6;
        if (st == 0 && !s_3d.shader_prog && !s_3d.comb_seen)
            S->mode = 1;        /* combiners never programmed: plain 2D on stage 0 */
    }
}

static void sw3d_bind_texels(void)
{
    int st;
    for (st = 0; st < 4; st++)
        s_stage[st].texels = (s_stage[st].enabled && s_tex_used[st]) ? texc_get(&s_stage[st].t) : NULL;
        s_stage[st].pow2 = s_stage[st].t.width && !(s_stage[st].t.width & (s_stage[st].t.width - 1))
                        && s_stage[st].t.height && !(s_stage[st].t.height & (s_stage[st].t.height - 1));
}

static inline int tex_addr(int c, int n, uint32_t mode)
{
    switch (mode) {
    case 1: if (!(n & (n - 1))) return c & (n - 1);                    /* wrap */
            c %= n; if (c < 0) c += n; return c;
    case 2: { int p = 2 * n; c %= p; if (c < 0) c += p;                /* mirror */
              return c < n ? c : p - 1 - c; }
    default: return c < 0 ? 0 : (c >= n ? n - 1 : c);                  /* clamp/border */
    }
}

static uint32_t tex_fetch(const SwStage *S, int iu, int iv)
{
    if (S->texels) {
        int x = tex_addr(iu, (int)S->t.width, S->t.addr_u);
        int y = tex_addr(iv, (int)S->t.height, S->t.addr_v);
        return S->texels[(size_t)y * S->t.width + x];
    }
    uint32_t texel = 0xFFFFFFFFu;
    if (S->t.addr_u != 1) { if (iu < 0) iu = 0; }
    if (S->t.addr_v != 1) { if (iv < 0) iv = 0; }
    s_cur_tex = &S->t;
    if (!sample_texture((uint32_t)iu, (uint32_t)iv, &texel)) {
        static uint32_t bad[256];
        if (bad[S->t.color & 0xFF]++ == 0)
            fprintf(stderr, "  [GPU3D] texture format 0x%02X not sampled (%ux%u at %08X)\n",
                    S->t.color, S->t.width, S->t.height, S->t.offset);
        texel = 0;
    }
    s_cur_tex = &s_gpu.tex;
    return texel;
}

static __attribute__((noinline)) void sw3d_sample(int st, const float *tc, float out[4])
{
    const SwStage *S = &s_stage[st];
    float u, v, q = tc[3];
    uint32_t t;

    out[0] = out[1] = out[2] = 0.0f; out[3] = 1.0f;
    if (S->mode == 0) { out[3] = 0.0f; return; }            /* PROGRAM_NONE */
    if (S->mode == 4) {                                      /* PASS_THROUGH */
        int i;
        for (i = 0; i < 4; i++) out[i] = fminf(fmaxf(tc[i], 0.0f), 1.0f);
        return;
    }
    if (!S->enabled) return;
    u = tc[0]; v = tc[1];
    if (S->mode == 1 && q != 0.0f && q != 1.0f) { u /= q; v /= q; }   /* 2D projective */
    if (S->normalized) { u *= (float)S->t.width; v *= (float)S->t.height; }
    if (S->bilinear) {
        float fu = u - 0.5f, fv = v - 0.5f;
        int iu = ifloor(fu), iv = ifloor(fv);
        uint32_t wu = (uint32_t)((fu - (float)iu) * 256.0f), wv = (uint32_t)((fv - (float)iv) * 256.0f);
        uint32_t t00, t10, t01, t11, rb, ag, rb2, ag2;
        if (S->texels && S->pow2) {
            /* power-of-two wrap / clamp, straight off the decoded texels */
            int x0, x1, y0, y1, w = (int)S->t.width, h = (int)S->t.height;
            if (S->t.addr_u == 1) { x0 = iu & (w - 1); x1 = (iu + 1) & (w - 1); }
            else { x0 = tex_addr(iu, w, S->t.addr_u); x1 = tex_addr(iu + 1, w, S->t.addr_u); }
            if (S->t.addr_v == 1) { y0 = iv & (h - 1); y1 = (iv + 1) & (h - 1); }
            else { y0 = tex_addr(iv, h, S->t.addr_v); y1 = tex_addr(iv + 1, h, S->t.addr_v); }
            t00 = S->texels[y0 * w + x0]; t10 = S->texels[y0 * w + x1];
            t01 = S->texels[y1 * w + x0]; t11 = S->texels[y1 * w + x1];
        } else {
            t00 = tex_fetch(S, iu, iv); t10 = tex_fetch(S, iu + 1, iv);
            t01 = tex_fetch(S, iu, iv + 1); t11 = tex_fetch(S, iu + 1, iv + 1);
        }
        if (wu > 256) wu = 256;
        if (wv > 256) wv = 256;
        /* two channels at a time, 8.8 fixed point */
        rb  = (((t00 & 0x00FF00FFu) * (256 - wu) + (t10 & 0x00FF00FFu) * wu) >> 8) & 0x00FF00FFu;
        ag  = ((((t00 >> 8) & 0x00FF00FFu) * (256 - wu) + ((t10 >> 8) & 0x00FF00FFu) * wu) >> 8) & 0x00FF00FFu;
        rb2 = (((t01 & 0x00FF00FFu) * (256 - wu) + (t11 & 0x00FF00FFu) * wu) >> 8) & 0x00FF00FFu;
        ag2 = ((((t01 >> 8) & 0x00FF00FFu) * (256 - wu) + ((t11 >> 8) & 0x00FF00FFu) * wu) >> 8) & 0x00FF00FFu;
        rb  = ((rb * (256 - wv) + rb2 * wv) >> 8) & 0x00FF00FFu;
        ag  = ((ag * (256 - wv) + ag2 * wv) >> 8) & 0x00FF00FFu;
        out[0] = (float)((rb >> 16) & 0xFF) * (1.0f / 255.0f);
        out[1] = (float)(ag & 0xFF) * (1.0f / 255.0f);
        out[2] = (float)(rb & 0xFF) * (1.0f / 255.0f);
        out[3] = (float)((ag >> 16) & 0xFF) * (1.0f / 255.0f);
        return;
    }
    t = tex_fetch(S, ifloor(u), ifloor(v));
    out[0] = ((t >> 16) & 0xFF) * (1.0f / 255.0f);
    out[1] = ((t >>  8) & 0xFF) * (1.0f / 255.0f);
    out[2] = ( t        & 0xFF) * (1.0f / 255.0f);
    out[3] = ( t >> 24        ) * (1.0f / 255.0f);
}

/* ---------------- register combiners ---------------- */

static void argb_to_f(uint32_t c, float o[4])
{
    o[0] = ((c >> 16) & 0xFF) / 255.0f; o[1] = ((c >> 8) & 0xFF) / 255.0f;
    o[2] = (c & 0xFF) / 255.0f;         o[3] = (c >> 24) / 255.0f;
}
/* Per-batch constants of the combiners, converted once (sw3d_comb_prepare). */
static float s_cf0[8][4], s_cf1[8][4], s_ff0[4], s_ff1[4], s_fogc[4];
static void sw3d_comb_prepare(void)
{
    uint32_t i, ctl = s_3d.comb_ctl, c = s_3d.fog_color;
    for (i = 0; i < 8; i++) {
        argb_to_f(s_3d.factor0[(ctl >> 12) & 1 ? i : 0], s_cf0[i]);
        argb_to_f(s_3d.factor1[(ctl >> 16) & 1 ? i : 0], s_cf1[i]);
    }
    argb_to_f(s_3d.factor0[0], s_ff0);
    argb_to_f(s_3d.factor1[0], s_ff1);
    s_fogc[0] = (c & 0xFF) / 255.0f; s_fogc[1] = ((c >> 8) & 0xFF) / 255.0f;
    s_fogc[2] = ((c >> 16) & 0xFF) / 255.0f; s_fogc[3] = 1.0f;
}

static float comb_map(float x, uint32_t map)
{
    switch (map) {
    case 0: return fmaxf(x, 0.0f);                       /* unsigned identity */
    case 1: return 1.0f - fminf(fmaxf(x, 0.0f), 1.0f);   /* unsigned invert   */
    case 2: return 2.0f * fmaxf(x, 0.0f) - 1.0f;         /* expand normal     */
    case 3: return -2.0f * fmaxf(x, 0.0f) + 1.0f;        /* expand negate     */
    case 4: return fmaxf(x, 0.0f) - 0.5f;                /* half-bias normal  */
    case 5: return -fmaxf(x, 0.0f) + 0.5f;               /* half-bias negate  */
    case 6: return x;                                    /* signed identity   */
    default: return -x;                                  /* signed negate     */
    }
}

/* One input byte: [3:0] register, [4] alpha channel, [7:5] mapping. */
static void comb_input(uint32_t in, float R[16][4], int for_alpha, float o[4])
{
    uint32_t reg = in & 0xF, map = (in >> 5) & 7;
    int alpha = (in >> 4) & 1;
    const float *r = R[reg];
    if (for_alpha) {
        float v = comb_map(alpha ? r[3] : r[2], map);
        o[0] = o[1] = o[2] = o[3] = v;
    } else if (alpha) {
        float v = comb_map(r[3], map);
        o[0] = o[1] = o[2] = v; o[3] = v;
    } else {
        o[0] = comb_map(r[0], map); o[1] = comb_map(r[1], map); o[2] = comb_map(r[2], map); o[3] = 0;
    }
}

static float comb_op(float x, uint32_t op)
{
    switch (op) {
    case 1: x -= 0.5f; break;                 /* bias */
    case 2: x *= 2.0f; break;                 /* shift left 1 */
    case 3: x = (x - 0.5f) * 2.0f; break;     /* shift left 1, bias */
    case 4: x *= 4.0f; break;                 /* shift left 2 */
    case 6: x *= 0.5f; break;                 /* shift right 1 */
    default: break;
    }
    return fminf(fmaxf(x, -1.0f), 1.0f);
}

/* The combiner program decoded once per batch (sw3d_comb_compile), so the
 * per-pixel loop does no bit-field work. */
typedef struct { uint8_t reg, alpha, map; } CIn;
typedef struct {
    CIn c[4], a[4];
    uint8_t abdot, cddot, mux, op, amux, aop;
    uint8_t dab, dcd, dsum, aab, acd, asum, ab_b2a, cd_b2a;
} CStage;
static struct {
    int n, muxmsb, has_final, need_ef, clamp_sum, fast;
    CStage st[8];
    CIn fA, fB, fC, fD, fE, fF, fG;
} s_cp;
static CIn cin_decode(uint32_t b) { CIn c; c.reg = b & 0xF; c.alpha = (b >> 4) & 1; c.map = (b >> 5) & 7; return c; }
static void sw3d_comb_compile(void)
{
    uint32_t i, ctl = s_3d.comb_ctl, n = ctl & 0xFF;
    if (n > 8) n = 8;
    s_cp.n = (int)n;
    s_cp.muxmsb = (ctl >> 8) & 1;
    for (i = 0; i < n; i++) {
        CStage *S = &s_cp.st[i];
        uint32_t icw = s_3d.c_icw[i], ocw = s_3d.c_ocw[i], aicw = s_3d.a_icw[i], aocw = s_3d.a_ocw[i];
        S->c[0] = cin_decode(icw >> 24); S->c[1] = cin_decode(icw >> 16);
        S->c[2] = cin_decode(icw >> 8);  S->c[3] = cin_decode(icw);
        S->a[0] = cin_decode(aicw >> 24); S->a[1] = cin_decode(aicw >> 16);
        S->a[2] = cin_decode(aicw >> 8);  S->a[3] = cin_decode(aicw);
        S->abdot = (ocw >> 13) & 1; S->cddot = (ocw >> 12) & 1; S->mux = (ocw >> 14) & 1; S->op = (ocw >> 15) & 7;
        S->amux = (aocw >> 14) & 1; S->aop = (aocw >> 15) & 7;
        S->dab = (ocw >> 4) & 0xF; S->dcd = ocw & 0xF; S->dsum = (ocw >> 8) & 0xF;
        S->aab = (aocw >> 4) & 0xF; S->acd = aocw & 0xF; S->asum = (aocw >> 8) & 0xF;
        S->ab_b2a = (ocw >> 19) & 1; S->cd_b2a = (ocw >> 18) & 1;
    }
    {
        uint32_t cw0 = s_3d.spec_fog0, cw1 = s_3d.spec_fog1;
        s_cp.has_final = cw0 || cw1;
        s_cp.fA = cin_decode(cw0 >> 24); s_cp.fB = cin_decode(cw0 >> 16);
        s_cp.fC = cin_decode(cw0 >> 8);  s_cp.fD = cin_decode(cw0);
        s_cp.fE = cin_decode(cw1 >> 24); s_cp.fF = cin_decode(cw1 >> 16);
        s_cp.fG = cin_decode(cw1 >> 8);
        s_cp.clamp_sum = (cw1 & 0x80) != 0;
        s_cp.need_ef = s_cp.fA.reg == 15 || s_cp.fB.reg == 15 || s_cp.fC.reg == 15 || s_cp.fD.reg == 15 || s_cp.fG.reg == 15;
    }
    /* Fast path: one stage R0 = A*B (colour and alpha), and a final
     * combiner that is either "R0" or the fog blend. Most of a frame. */
    s_cp.fast = 0;
    if (n == 1) {
        const CStage *S = &s_cp.st[0];
#define ZERO_IN(c) ((c).reg == 0 && ((c).map == 0 || (c).map == 6))
        int stage_ok = ZERO_IN(S->c[2]) && ZERO_IN(S->c[3]) && ZERO_IN(S->a[2]) && ZERO_IN(S->a[3])
            && !S->abdot && !S->cddot && !S->mux && !S->amux && (S->op == 0 || S->op == 2 || S->op == 4)
            && (S->aop == 0 || S->aop == 2 || S->aop == 4)
            && S->dab == 12 && !S->dcd && !S->dsum && S->aab == 12 && !S->acd && !S->asum && !S->ab_b2a;
        if (stage_ok) {
            int g_r0a = s_cp.fG.reg == 12 && s_cp.fG.alpha && s_cp.fG.map == 0;
            if (!s_cp.has_final)
                s_cp.fast = 1;
            else if (ZERO_IN(s_cp.fA) && ZERO_IN(s_cp.fB) && ZERO_IN(s_cp.fC) && s_cp.fD.reg == 12 && !s_cp.fD.alpha
                     && s_cp.fD.map == 0 && g_r0a)
                s_cp.fast = 1;
            else if (s_cp.fA.reg == 3 && s_cp.fA.alpha && s_cp.fA.map == 0 && s_cp.fB.reg == 12 && !s_cp.fB.alpha
                     && s_cp.fB.map == 0 && s_cp.fC.reg == 3 && !s_cp.fC.alpha && s_cp.fC.map == 0 && ZERO_IN(s_cp.fD) && g_r0a)
                s_cp.fast = 2;
        }
#undef ZERO_IN
    }
}
static inline float cmap(float x, int map)
{
    switch (map) {
    case 0: return x > 0.0f ? x : 0.0f;
    case 1: return 1.0f - fminf(fmaxf(x, 0.0f), 1.0f);
    case 2: return 2.0f * fmaxf(x, 0.0f) - 1.0f;
    case 3: return -2.0f * fmaxf(x, 0.0f) + 1.0f;
    case 4: return fmaxf(x, 0.0f) - 0.5f;
    case 5: return -fmaxf(x, 0.0f) + 0.5f;
    case 6: return x;
    default: return -x;
    }
}
static inline void cin_rgb(const CIn *c, float R[16][4], float o[4])
{
    const float *r = R[c->reg];
    if (c->alpha) { float v = cmap(r[3], c->map); o[0] = o[1] = o[2] = v; }
    else if (c->map == 0) { o[0] = r[0] > 0.0f ? r[0] : 0.0f; o[1] = r[1] > 0.0f ? r[1] : 0.0f; o[2] = r[2] > 0.0f ? r[2] : 0.0f; }
    else { o[0] = cmap(r[0], c->map); o[1] = cmap(r[1], c->map); o[2] = cmap(r[2], c->map); }
}
static inline float cin_a(const CIn *c, float R[16][4])
{
    return cmap(c->alpha ? R[c->reg][3] : R[c->reg][2], c->map);
}

static __attribute__((noinline)) void sw3d_combine(const float *att, float out[4])
{
    float R[16][4];
    uint32_t n, i, k, ctl = s_3d.comb_ctl;
    int st;

    memset(R[0], 0, sizeof R[0]);
    memset(R[6], 0, 2 * sizeof R[0]);
    memset(R[12], 0, 4 * sizeof R[0]);
    for (st = 0; st < 4; st++)
        if (s_tex_used[st])
            sw3d_sample(st, &att[SA_T0 + 4 * st], R[8 + st]);
        else
            memset(R[8 + st], 0, sizeof R[0]);
    memcpy(R[4], &att[SA_D0], 4 * sizeof(float));
    memcpy(R[5], &att[SA_D1], 4 * sizeof(float));
    {
        float fc[4];
        memcpy(fc, s_fogc, sizeof fc);           /* ABGR, converted per batch */
        if (s_3d.fog_enable) {
            /* NV2A fog factor from the fog coordinate (oFog.x), as xemu
             * derives it: linear p0 + d*p1 - 1, exp p0 + 2^(16*d*p1) - 1.5,
             * exp2 p0 + 2^(-32*(d*p1)^2) - 1.5. The XDK sets p0=p1=1 for a
             * shader that writes the factor itself, which comes out as d. */
            float dd = att[SA_FOG], f;
            switch (s_3d.fog_mode) {
            case 0x800: case 0x802:
                if (s_3d.fog_mode == 0x802) dd = fabsf(dd);
                f = s_3d.fog_params[0] + exp2f(dd * s_3d.fog_params[1] * 16.0f) - 1.5f; break;
            case 0x801: case 0x803:
                if (s_3d.fog_mode == 0x803) dd = fabsf(dd);
                f = s_3d.fog_params[0] + exp2f(-dd * dd * s_3d.fog_params[1] * s_3d.fog_params[1] * 32.0f) - 1.5f; break;
            case 0x804:
                dd = fabsf(dd); /* fall through */
            default:
                f = s_3d.fog_params[0] + dd * s_3d.fog_params[1] - 1.0f; break;
            }
            fc[3] = fminf(fmaxf(f, 0.0f), 1.0f);
        }
        memcpy(R[3], fc, sizeof fc);
    }
    R[12][3] = R[8][3];                          /* R0.a starts as T0.a */

    if (!s_3d.comb_seen) {                       /* never programmed: modulate */
        for (k = 0; k < 4; k++)
            out[k] = (s_stage[0].enabled ? R[8][k] : 1.0f) * R[4][k];
        return;
    }

    (void)n; (void)i; (void)ctl;
    if (s_cp.fast) {
        const CStage *S = &s_cp.st[0];
        float A[4], B[4], a, sc = S->op == 2 ? 2.0f : S->op == 4 ? 4.0f : 1.0f;
        float asc = S->aop == 2 ? 2.0f : S->aop == 4 ? 4.0f : 1.0f;
        memcpy(R[1], s_cf0[0], sizeof R[1]);
        memcpy(R[2], s_cf1[0], sizeof R[2]);
        cin_rgb(&S->c[0], R, A); cin_rgb(&S->c[1], R, B);
        a = cin_a(&S->a[0], R) * cin_a(&S->a[1], R) * asc;
        a = fminf(fmaxf(a, 0.0f), 1.0f);       /* [-1,1] clamp, then the final clamp */
        for (k = 0; k < 3; k++) {
            float v = fminf(fmaxf(A[k] * B[k] * sc, 0.0f), 1.0f);
            if (s_cp.fast == 2) {
                float f = fminf(fmaxf(R[3][3], 0.0f), 1.0f);
                v = fminf(fmaxf(f * v + (1.0f - f) * fmaxf(R[3][k], 0.0f), 0.0f), 1.0f);
            }
            out[k] = v;
        }
        out[3] = a;
        return;
    }
    for (i = 0; i < (uint32_t)s_cp.n; i++) {
        const CStage *S = &s_cp.st[i];
        float A[4], B[4], C[4], D[4], ab[3], cd[3], sum[3];
        float res_rgb[3][3], res_a[3];
        memcpy(R[1], s_cf0[i], sizeof R[1]);
        memcpy(R[2], s_cf1[i], sizeof R[2]);
        cin_rgb(&S->c[0], R, A); cin_rgb(&S->c[1], R, B);
        cin_rgb(&S->c[2], R, C); cin_rgb(&S->c[3], R, D);
        if (S->abdot) { float d = A[0]*B[0] + A[1]*B[1] + A[2]*B[2]; ab[0] = ab[1] = ab[2] = d; }
        else { ab[0] = A[0]*B[0]; ab[1] = A[1]*B[1]; ab[2] = A[2]*B[2]; }
        if (S->cddot) { float d = C[0]*D[0] + C[1]*D[1] + C[2]*D[2]; cd[0] = cd[1] = cd[2] = d; }
        else { cd[0] = C[0]*D[0]; cd[1] = C[1]*D[1]; cd[2] = C[2]*D[2]; }
        if (S->mux) {
            int sel = s_cp.muxmsb ? (R[12][3] >= 0.5f) : (((int)(R[12][3] * 255.0f + 0.5f)) & 1);
            for (k = 0; k < 3; k++) sum[k] = sel ? cd[k] : ab[k];
        } else for (k = 0; k < 3; k++) sum[k] = ab[k] + cd[k];
        for (k = 0; k < 3; k++) {
            res_rgb[0][k] = comb_op(ab[k], S->op);
            res_rgb[1][k] = comb_op(cd[k], S->op);
            res_rgb[2][k] = comb_op(sum[k], S->op);
        }
        {
            float a0 = cin_a(&S->a[0], R), a1 = cin_a(&S->a[1], R), a2 = cin_a(&S->a[2], R), a3 = cin_a(&S->a[3], R);
            float abs_ = a0 * a1, cds = a2 * a3, sm;
            if (S->amux) {
                int sel = s_cp.muxmsb ? (R[12][3] >= 0.5f) : (((int)(R[12][3] * 255.0f + 0.5f)) & 1);
                sm = sel ? cds : abs_;
            } else sm = abs_ + cds;
            res_a[0] = comb_op(abs_, S->aop); res_a[1] = comb_op(cds, S->aop); res_a[2] = comb_op(sm, S->aop);
        }
        if (S->dab) { memcpy(R[S->dab], res_rgb[0], 3 * sizeof(float)); if (S->ab_b2a) R[S->dab][3] = res_rgb[0][2]; }
        if (S->dcd) { memcpy(R[S->dcd], res_rgb[1], 3 * sizeof(float)); if (S->cd_b2a) R[S->dcd][3] = res_rgb[1][2]; }
        if (S->dsum) memcpy(R[S->dsum], res_rgb[2], 3 * sizeof(float));
        if (S->aab)  R[S->aab][3]  = res_a[0];
        if (S->acd)  R[S->acd][3]  = res_a[1];
        if (S->asum) R[S->asum][3] = res_a[2];
    }

    if (s_diag_px > 0) {
        s_diag_px--;
        fprintf(stderr, "  [PXDIAG] V0 %.3f %.3f %.3f %.3f V1 %.3f %.3f %.3f %.3f T0 %.3f %.3f %.3f %.3f T1 %.3f %.3f %.3f %.3f R0 %.3f %.3f %.3f %.3f R1 %.3f %.3f %.3f %.3f uv0 %.3f %.3f\n",
                R[4][0],R[4][1],R[4][2],R[4][3], R[5][0],R[5][1],R[5][2],R[5][3], R[8][0],R[8][1],R[8][2],R[8][3],
                R[9][0],R[9][1],R[9][2],R[9][3], R[12][0],R[12][1],R[12][2],R[12][3], R[13][0],R[13][1],R[13][2],R[13][3],
                att[SA_T0], att[SA_T0+1]);
    }
    /* final combiner: rgb = A*B + (1-A)*C + D, alpha = G */
    if (!s_cp.has_final) {
        for (k = 0; k < 4; k++) out[k] = fminf(fmaxf(R[12][k], 0.0f), 1.0f);
        return;
    }
    {
        float A[4], B[4], C[4], D[4], E[4], F[4];
        memcpy(R[1], s_ff0, sizeof R[1]);
        memcpy(R[2], s_ff1, sizeof R[2]);
        if (s_cp.need_ef) {
            cin_rgb(&s_cp.fE, R, E); cin_rgb(&s_cp.fF, R, F);
            for (k = 0; k < 3; k++) R[15][k] = E[k] * F[k];                         /* E*F */
        }
        for (k = 0; k < 3; k++) {
            R[14][k] = R[5][k] + R[12][k];                  /* V1 + R0 */
            if (s_cp.clamp_sum) R[14][k] = fminf(R[14][k], 1.0f);
        }
        cin_rgb(&s_cp.fA, R, A); cin_rgb(&s_cp.fB, R, B);
        cin_rgb(&s_cp.fC, R, C); cin_rgb(&s_cp.fD, R, D);
        for (k = 0; k < 3; k++) {
            float a = fminf(fmaxf(A[k], 0.0f), 1.0f);
            out[k] = a * B[k] + (1.0f - a) * C[k] + D[k];
            out[k] = fminf(fmaxf(out[k], 0.0f), 1.0f);
        }
        out[3] = fminf(fmaxf(cin_a(&s_cp.fG, R), 0.0f), 1.0f);
    }
}

/* ---------------- rasteriser ---------------- */

static int depth_pass(float z, float d)
{
    switch (s_3d.depth_func & 0xF) {
    case 0: return 0;
    case 1: return z <  d;
    case 2: return z == d;
    case 3: return z <= d;
    case 4: return z >  d;
    case 5: return z != d;
    case 6: return z >= d;
    default: return 1;
    }
}

static int alpha_pass(uint32_t a)
{
    uint32_t r = s_3d.alpha_ref;
    switch (s_3d.alpha_func & 0xF) {
    case 0: return 0;
    case 1: return a <  r;
    case 2: return a == r;
    case 3: return a <= r;
    case 4: return a >  r;
    case 5: return a != r;
    case 6: return a >= r;
    default: return 1;
    }
}

/* Blend factor per channel, all in [0,1]. */
static float blend_factor(uint32_t f, int ch, const float s[4], const float d[4], const float k[4])
{
    switch (f) {
    case 0x0000: return 0.0f;
    case 0x0001: return 1.0f;
    case 0x0300: return s[ch];
    case 0x0301: return 1.0f - s[ch];
    case 0x0302: return s[3];
    case 0x0303: return 1.0f - s[3];
    case 0x0304: return d[3];
    case 0x0305: return 1.0f - d[3];
    case 0x0306: return d[ch];
    case 0x0307: return 1.0f - d[ch];
    case 0x0308: return ch == 3 ? 1.0f : fminf(s[3], 1.0f - d[3]);
    case 0x8001: return k[ch];
    case 0x8002: return 1.0f - k[ch];
    case 0x8003: return k[3];
    case 0x8004: return 1.0f - k[3];
    default:     return 1.0f;
    }
}

/* Colour write with the title's blend, equation and write mask. */
static uint64_t s_bl_px; static double s_bl_sum[4];
static __attribute__((noinline)) void put_pixel3d(uint32_t bpp, int x, int y, const float src[4], int s_rt_stats)
{
    if (s_rt_stats) { s_bl_px++; s_bl_sum[0] += src[0]; s_bl_sum[1] += src[1]; s_bl_sum[2] += src[2]; s_bl_sum[3] += src[3]; }
    uint8_t *row = s_surface + (size_t)y * s_gpu.pitch;
    float d[4] = {0, 0, 0, 1}, o[4], k[4];
    uint32_t dst = 0, out, m = s_3d.color_mask;
    int i;

    {
        static int wx = -2, wy, cnt;
        if (wx == -2) {
            const char *e = getenv("RECOMP_PIXWATCH");
            wx = -1;
            if (e) sscanf(e, "%d,%d", &wx, &wy);
        }
        if (x == wx && y == wy && ((cnt++ % 3000) < 60)) {
            fprintf(stderr, "  [PIX] batch %u src %.3f %.3f %.3f %.3f blend %u %X/%X cmask %08X st %u comb %08X t0 %08X/%02X t1 %08X/%02X prim %u n %u\n",
                    s_3d.batches, src[0], src[1], src[2], src[3], s_gpu.blend_enable, s_gpu.blend_sfactor,
                    s_gpu.blend_dfactor, s_3d.color_mask, s_3d.stencil_test, s_3d.comb_ctl,
                    s_stage[0].t.offset, s_stage[0].t.color, s_stage[1].t.offset, s_stage[1].t.color,
                    s_gpu.prim, s_gpu.idx_count);
        }
    }
    if (bpp == 4 && m == 0x01010101u && !s_gpu.blend_enable) {
        /* Opaque full write: the common case, no destination read. */
        float r = fminf(fmaxf(src[0], 0.0f), 1.0f), g = fminf(fmaxf(src[1], 0.0f), 1.0f);
        float b = fminf(fmaxf(src[2], 0.0f), 1.0f), a = fminf(fmaxf(src[3], 0.0f), 1.0f);
        ((uint32_t *)row)[x] = ((uint32_t)(a * 255.0f + 0.5f) << 24) | ((uint32_t)(r * 255.0f + 0.5f) << 16)
                             | ((uint32_t)(g * 255.0f + 0.5f) << 8) | (uint32_t)(b * 255.0f + 0.5f);
        if (s_rt_stats) s_gpu.pixels++;
        return;
    }
    if (bpp == 4) dst = ((const uint32_t *)row)[x];
    else {
        uint32_t t = ((const uint16_t *)row)[x];
        dst = 0xFF000000u | ((t & 0xF800u) << 8) | ((t & 0x07E0u) << 5) | ((t & 0x001Fu) << 3);
    }
    d[0] = ((dst >> 16) & 0xFF) / 255.0f; d[1] = ((dst >> 8) & 0xFF) / 255.0f;
    d[2] = (dst & 0xFF) / 255.0f;         d[3] = (dst >> 24) / 255.0f;

    if (s_gpu.blend_enable) {
        k[0] = ((s_3d.blend_color >> 16) & 0xFF) / 255.0f; k[1] = ((s_3d.blend_color >> 8) & 0xFF) / 255.0f;
        k[2] = (s_3d.blend_color & 0xFF) / 255.0f;         k[3] = (s_3d.blend_color >> 24) / 255.0f;
        for (i = 0; i < 4; i++) {
            float fs = blend_factor(s_gpu.blend_sfactor, i, src, d, k);
            float fd = blend_factor(s_gpu.blend_dfactor, i, src, d, k);
            float a = src[i] * fs, b = d[i] * fd;
            switch (s_3d.blend_eq) {
            case 0x800A: o[i] = a - b; break;               /* subtract */
            case 0x800B: o[i] = b - a; break;               /* reverse subtract */
            case 0x8007: o[i] = fminf(src[i], d[i]); break;
            case 0x8008: o[i] = fmaxf(src[i], d[i]); break;
            default:     o[i] = a + b; break;
            }
            if (o[i] < 0.0f) o[i] = 0.0f;
            if (o[i] > 1.0f) o[i] = 1.0f;
        }
    } else {
        memcpy(o, src, sizeof o);
    }
    if (!(m & 0x00010000u)) o[0] = d[0];
    if (!(m & 0x00000100u)) o[1] = d[1];
    if (!(m & 0x00000001u)) o[2] = d[2];
    if (!(m & 0x01000000u)) o[3] = d[3];
    out = ((uint32_t)(o[3] * 255.0f + 0.5f) << 24) | ((uint32_t)(o[0] * 255.0f + 0.5f) << 16)
        | ((uint32_t)(o[1] * 255.0f + 0.5f) << 8) | (uint32_t)(o[2] * 255.0f + 0.5f);
    if (s_rt_stats) s_gpu.pixels++;
    if (bpp == 4) ((uint32_t *)row)[x] = out;
    else ((uint16_t *)row)[x] = (uint16_t)(((out >> 8) & 0xF800) | ((out >> 5) & 0x07E0) | ((out >> 3) & 0x001F));
}

static int s_surface_ok;
static void sw3d_tri_raster(const SwVert *A, const SwVert *B, const SwVert *C, int nth, int idx)
{
    const int s_rt_stats = nth == 1;   /* shared counters: never while threads share the batch */
    uint32_t bpp = surface_bpp();
    float ax, ay, bx, by, cx, cy, area, iwa, iwb, iwc, za, zb, zc;
    int minx, maxx, miny, maxy, x, y;

    if (bpp != 4 && bpp != 2) return;
    /* No stencil buffer: a stencil-tested pass (shadow-volume darkening,
     * masked decals) would cover everything, so it is not drawn. */

    iwa = 1.0f / A->p[3]; iwb = 1.0f / B->p[3]; iwc = 1.0f / C->p[3];
    ax = A->p[0] * iwa; ay = A->p[1] * iwa; za = A->p[2] * iwa;
    bx = B->p[0] * iwb; by = B->p[1] * iwb; zb = B->p[2] * iwb;
    cx = C->p[0] * iwc; cy = C->p[1] * iwc; zc = C->p[2] * iwc;

    area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    if (area == 0.0f || !isfinite(area)) return;
    if (s_3d.cull_enable) {
        /* Window y grows downward, so area > 0 is clockwise on screen. */
        int cw = area > 0.0f;
        int front = (s_3d.front_face == 0x900) ? cw : !cw;
        static int flip = -1;
        if (flip < 0) flip = getenv("RECOMP_CULL_FLIP") != NULL;
        if (flip) front = !front;
        if ((s_3d.cull_face == 0x405 && !front) || (s_3d.cull_face == 0x404 && front)
            || s_3d.cull_face == 0x408) {
            if (s_rt_stats) s_3d.tris_culled++;
            return;
        }
    }
    if (!s_surface_ok) return;

    minx = (int)floorf(fminf(ax, fminf(bx, cx)));
    maxx = (int)ceilf (fmaxf(ax, fmaxf(bx, cx)));
    miny = (int)floorf(fminf(ay, fminf(by, cy)));
    maxy = (int)ceilf (fmaxf(ay, fmaxf(by, cy)));
    if (minx < (int)s_gpu.clip_x) minx = (int)s_gpu.clip_x;
    if (miny < (int)s_gpu.clip_y) miny = (int)s_gpu.clip_y;
    if (maxx > (int)(s_gpu.clip_x + s_gpu.clip_w)) maxx = (int)(s_gpu.clip_x + s_gpu.clip_w);
    if (maxy > (int)(s_gpu.clip_y + s_gpu.clip_h)) maxy = (int)(s_gpu.clip_y + s_gpu.clip_h);
    if (minx >= maxx || miny >= maxy) { if (s_rt_stats) s_gpu.tris_skipped_offscreen++; return; }
    if (getenv("RECOMP_BIGTRI") && (maxx - minx) * (maxy - miny) > 120000 && s_3d.batches > 20000) {
        static int nbig;
        if ((nbig++ % 4000) < 12)
            fprintf(stderr, "  [BIGTRI] batch %u prim %u n %u (%.0f,%.0f z%.0f w%.3f)(%.0f,%.0f z%.0f w%.3f)(%.0f,%.0f z%.0f w%.3f) blend %u %X/%X eq %X cmask %08X z %u/%X/%u st %u comb %08X cw0 %08X t0 %08X fmt %02X en %d D0 %.2f %.2f %.2f %.2f stf %X ref %X m %X wm %X ops %X/%X/%X at %u %X/%u\n",
                    s_3d.batches, s_gpu.prim, s_gpu.idx_count, ax, ay, za, A->p[3], bx, by, zb, B->p[3], cx, cy, zc, C->p[3],
                    s_gpu.blend_enable, s_gpu.blend_sfactor, s_gpu.blend_dfactor, s_3d.blend_eq, s_3d.color_mask,
                    s_3d.depth_test, s_3d.depth_func, s_3d.depth_mask, s_3d.stencil_test, s_3d.comb_ctl, s_3d.spec_fog0,
                    s_stage[0].t.offset, s_stage[0].t.color, s_stage[0].enabled,
                    A->a[0], A->a[1], A->a[2], A->a[3], s_3d.st_func, s_3d.st_ref, s_3d.st_mask, s_3d.st_wmask,
                    s_3d.st_fail, s_3d.st_zfail, s_3d.st_zpass, s_3d.alpha_test, s_3d.alpha_func, s_3d.alpha_ref);
    }
    if ((s_3d.depth_test || s_3d.stencil_test) && (!s_zbuf || (uint32_t)maxx > s_zbuf_w || (uint32_t)maxy > s_zbuf_h))
        return;              /* sw3d_batch sized the buffer for the surface */

    {
        float inv_area = 1.0f / area;
        float aw[SW_NATTR], bw[SW_NATTR], cw_[SW_NATTR];
        float att[SW_NATTR], col[4];
        int k, j, nlist = 0, list[SW_NATTR];
        /* Only the attributes this batch's combiners read (sw3d_batch fills
         * s_attr_need): most passes use diffuse and one or two texcoords. */
        for (k = 0; k < SW_NATTR; k++) {
            att[k] = 0.0f;
            if (s_attr_need[k]) list[nlist++] = k;
        }
        /* attributes pre-divided by w, for perspective-correct interpolation */
        for (j = 0; j < nlist; j++) {
            k = list[j];
            aw[k] = A->a[k] * iwa; bw[k] = B->a[k] * iwb; cw_[k] = C->a[k] * iwc;
        }
        {
        /* Edge functions as e = ex*px + ey*py + e0, stepped per pixel. */
        const float e0x = -(cy - by) * inv_area, e0y = (cx - bx) * inv_area;
        const float e0c = ((cx - bx) * (-by) - (cy - by) * (-bx)) * inv_area;
        const float e1x = -(ay - cy) * inv_area, e1y = (ax - cx) * inv_area;
        const float e1c = ((ax - cx) * (-cy) - (ay - cy) * (-cx)) * inv_area;
        const int need_z = s_3d.depth_test || s_3d.stencil_test;
        for (y = miny; y < maxy; y++) {
            float py = (float)y + 0.5f;
            float px0;
            if (nth > 1 && ((y >> 3) % nth) != idx) continue;   /* another thread's band */
            float w0, w1;
            size_t zrow = (size_t)y * s_zbuf_w;
            int xs = minx, xe = maxx;
            {
                /* The span of this row inside all three edges, solved rather
                 * than searched: a long thin or guard-band triangle has a
                 * bounding box many times its area. */
                const float ex[3] = { e0x, e1x, -e0x - e1x };
                const float ec[3] = { e0y * py + e0c, e1y * py + e1c, 1.0f - (e0y * py + e0c) - (e1y * py + e1c) };
                float lo = (float)minx, hi = (float)maxx;
                int i3;
                for (i3 = 0; i3 < 3; i3++) {
                    /* ex*(x+0.5) + ec >= -eps */
                    if (ex[i3] > 1e-12f) { float b = (-1e-6f - ec[i3]) / ex[i3] - 0.5f; if (b > lo) lo = b; }
                    else if (ex[i3] < -1e-12f) { float b = (-1e-6f - ec[i3]) / ex[i3] - 0.5f; if (b < hi) hi = b; }
                    else if (ec[i3] < -1e-6f) { lo = 1.0f; hi = 0.0f; }
                }
                if (lo > hi) continue;
                xs = (int)ceilf(lo) - 1; if (xs < minx) xs = minx;
                xe = (int)floorf(hi) + 2; if (xe > maxx) xe = maxx;
                if (xs >= xe) continue;
            }
            px0 = (float)xs + 0.5f;
            w0 = e0x * px0 + e0y * py + e0c;
            w1 = e1x * px0 + e1y * py + e1c;
            if (s_rt_stats) s_st_bbox += (uint64_t)(xe - xs);
            for (x = xs; x < xe; x++, w0 += e0x, w1 += e1x) {
                float w2 = 1.0f - w0 - w1;                                            /* weight of C */
                float z = 0.0f, q;
                uint32_t a8;
                if (w0 < -1e-6f || w1 < -1e-6f || w2 < -1e-6f) continue;
                if (s_rt_stats) s_st_inside++;
                if (need_z) z = w0 * za + w1 * zb + w2 * zc;
                if (s_3d.stencil_test && s_sbuf) {
                    uint8_t *sp = &s_sbuf[zrow + x];
                    if (!stencil_pass(*sp)) { stencil_op(sp, s_3d.st_fail); continue; }
                    if (s_3d.depth_test && s_zbuf && !depth_pass(z, s_zbuf[zrow + x])) {
                        stencil_op(sp, s_3d.st_zfail); continue;
                    }
                    stencil_op(sp, s_3d.st_zpass);
                } else if (s_3d.depth_test && s_zbuf) {
                    if (!depth_pass(z, s_zbuf[zrow + x])) continue;
                }
                q = w0 * iwa + w1 * iwb + w2 * iwc;
                if (q == 0.0f) continue;
                q = 1.0f / q;
                for (j = 0; j < nlist; j++) {
                    k = list[j];
                    att[k] = (w0 * aw[k] + w1 * bw[k] + w2 * cw_[k]) * q;
                }
                sw3d_combine(att, col);
                if (s_rt_stats) s_st_shaded++;
                a8 = (uint32_t)(col[3] * 255.0f + 0.5f);
                if (s_3d.alpha_test && !alpha_pass(a8)) continue;
                if (s_3d.depth_test && s_3d.depth_mask && s_zbuf)
                    s_zbuf[zrow + x] = z;
                if (s_3d.color_mask)
                    put_pixel3d(bpp, x, y, col, s_rt_stats);
                if (s_rt_stats) s_3d.pixels++;
            }
        }
        }
    }
    if (s_rt_stats) {
        s_3d.tris_out++;
        s_gpu.tris_drawn++;
        s_gpu.drawn_offset = s_gpu.color_offset;
    }
}

/* Triangles of the current batch, collected for the raster threads. */
typedef struct { SwVert v[3]; } SwTriJob;
static SwTriJob *s_jobs;
static uint32_t s_jobs_n, s_jobs_cap;
static int s_rt_collect;
static double s_jobs_area;
static void sw3d_tri(const SwVert *A, const SwVert *B, const SwVert *C)
{
    if (!s_rt_collect) { sw3d_tri_raster(A, B, C, 1, 0); return; }
    if (s_jobs_n == s_jobs_cap) {
        uint32_t nc = s_jobs_cap ? s_jobs_cap * 2 : 1024;
        SwTriJob *nj = (SwTriJob *)realloc(s_jobs, sizeof(SwTriJob) * nc);
        if (!nj) { sw3d_tri_raster(A, B, C, 1, 0); return; }
        s_jobs = nj; s_jobs_cap = nc;
    }
    s_jobs[s_jobs_n].v[0] = *A; s_jobs[s_jobs_n].v[1] = *B; s_jobs[s_jobs_n].v[2] = *C;
    s_jobs_n++;
    {
        float ax = A->p[0] / A->p[3], ay = A->p[1] / A->p[3], bx = B->p[0] / B->p[3], by = B->p[1] / B->p[3];
        float cx = C->p[0] / C->p[3], cy = C->p[1] / C->p[3];
        float ar = fabsf((bx - ax) * (cy - ay) - (by - ay) * (cx - ax)) * 0.5f;
        if (isfinite(ar)) s_jobs_area += ar < 400000.0f ? ar : 400000.0f;
    }
}

/* Raster threads: each takes every eighth-row band modulo the thread count,
 * so a pixel belongs to exactly one thread and sees the batch's triangles in
 * order. The game's own threads stay on CPU 0 (see twotowers_host.h). */
#define SW3D_MAX_RT 8
static unsigned s_rt_stat_batches, s_rt_stat_dispatch;
static long long s_rt_t_own, s_rt_t_wait;
static double s_rt_min_area = 500.0;
static int s_rt_n = -1;                 /* worker threads (not counting the caller) */
static volatile LONG s_rt_gen, s_rt_done;
static volatile LONG s_rt_sleeping[SW3D_MAX_RT];
static HANDLE s_rt_wake[SW3D_MAX_RT];
static DWORD_PTR s_rt_mask;
static void sw3d_run_jobs(int nth, int idx)
{
    uint32_t i;
    for (i = 0; i < s_jobs_n; i++)
        sw3d_tri_raster(&s_jobs[i].v[0], &s_jobs[i].v[1], &s_jobs[i].v[2], nth, idx);
}
static DWORD WINAPI sw3d_rt_thread(LPVOID arg)
{
    int idx = (int)(intptr_t)arg;
    LONG seen = 0;
    if (s_rt_mask) SetThreadAffinityMask(GetCurrentThread(), s_rt_mask);
    for (;;) {
        int spins = 0;
        while (s_rt_gen == seen) {
            if (++spins < 50000) { YieldProcessor(); continue; }
            InterlockedExchange(&s_rt_sleeping[idx], 1);
            if (s_rt_gen == seen) WaitForSingleObject(s_rt_wake[idx], 50);
            InterlockedExchange(&s_rt_sleeping[idx], 0);
            spins = 0;
        }
        seen = s_rt_gen;
        sw3d_run_jobs(s_rt_n + 1, idx + 1);
        InterlockedIncrement(&s_rt_done);
    }
    return 0;
}
static void sw3d_rt_init(void)
{
    DWORD_PTR pm = 0, sm = 0, others;
    int i, cpus = 0;
    const char *e = getenv("RECOMP_RASTER_THREADS");
    s_rt_n = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &pm, &sm)) return;
    others = pm & ~(DWORD_PTR)1;
    s_rt_mask = others;
    for (i = 0; i < (int)(8 * sizeof(DWORD_PTR)); i++) if (others & ((DWORD_PTR)1 << i)) cpus++;
    s_rt_n = cpus < SW3D_MAX_RT ? cpus : SW3D_MAX_RT;
    if (s_rt_n > 3) s_rt_n = 3;
    if (e) { s_rt_n = atoi(e); if (s_rt_n < 0) s_rt_n = 0; if (s_rt_n > SW3D_MAX_RT) s_rt_n = SW3D_MAX_RT; }
    for (i = 0; i < s_rt_n; i++) {
        HANDLE h;
        s_rt_wake[i] = CreateEventA(NULL, FALSE, FALSE, NULL);
        h = CreateThread(NULL, 0, sw3d_rt_thread, (LPVOID)(intptr_t)i, 0, NULL);
        if (!h) { s_rt_n = i; break; }
        (void)h;
        SetThreadPriority(h, THREAD_PRIORITY_ABOVE_NORMAL);
        CloseHandle(h);
    }
    if (getenv("RECOMP_RASTER_MIN")) s_rt_min_area = atof(getenv("RECOMP_RASTER_MIN"));
    fprintf(stderr, "  [GPU3D] %d raster thread(s) besides the caller\n", s_rt_n);
}
static void sw3d_flush_jobs(void)
{
    int i;
    if (!s_jobs_n) return;
    s_rt_stat_batches++;
    if (s_rt_n <= 0 || s_jobs_area < s_rt_min_area) {
        sw3d_run_jobs(1, 0);
    } else {
        s_rt_stat_dispatch++;
        s_rt_done = 0;
        MemoryBarrier();
        InterlockedIncrement(&s_rt_gen);
        for (i = 0; i < s_rt_n; i++)
            if (s_rt_sleeping[i]) SetEvent(s_rt_wake[i]);
        {
        LARGE_INTEGER t0, t1, t2;
        QueryPerformanceCounter(&t0);
        sw3d_run_jobs(s_rt_n + 1, 0);
        QueryPerformanceCounter(&t1);
        while (s_rt_done < s_rt_n) YieldProcessor();
        QueryPerformanceCounter(&t2);
        s_rt_t_own += t1.QuadPart - t0.QuadPart; s_rt_t_wait += t2.QuadPart - t1.QuadPart;
        }
        MemoryBarrier();
        s_3d.tris_out += s_jobs_n;
        s_gpu.tris_drawn += s_jobs_n;
        s_gpu.drawn_offset = s_gpu.color_offset;
    }
    s_jobs_n = 0;
    s_jobs_area = 0;
}

static void sw3d_lerp(SwVert *o, const SwVert *a, const SwVert *b, float t)
{
    int i;
    for (i = 0; i < 4; i++) o->p[i] = a->p[i] + (b->p[i] - a->p[i]) * t;
    for (i = 0; i < SW_NATTR; i++) o->a[i] = a->a[i] + (b->a[i] - a->a[i]) * t;
}

#define SW3D_NEAR_W 1e-5f

/* Clip against w > 0 (everything in front of the eye) and draw the fan. */
static void sw3d_clip_tri(const SwVert *a, const SwVert *b, const SwVert *c)
{
    SwVert in[3], out[8];
    int n = 0, i;

    s_3d.tris_in++;
    if (a->p[3] > SW3D_NEAR_W && b->p[3] > SW3D_NEAR_W && c->p[3] > SW3D_NEAR_W) {
        sw3d_tri(a, b, c);
        return;
    }
    if (a->p[3] <= SW3D_NEAR_W && b->p[3] <= SW3D_NEAR_W && c->p[3] <= SW3D_NEAR_W)
        return;
    s_3d.tris_clipped++;
    in[0] = *a; in[1] = *b; in[2] = *c;
    for (i = 0; i < 3; i++) {
        const SwVert *p = &in[i], *q = &in[(i + 1) % 3];
        int pin = p->p[3] > SW3D_NEAR_W, qin = q->p[3] > SW3D_NEAR_W;
        if (pin) out[n++] = *p;
        if (pin != qin) {
            float t = (SW3D_NEAR_W - p->p[3]) / (q->p[3] - p->p[3]);
            sw3d_lerp(&out[n++], p, q, t);
        }
    }
    for (i = 1; i + 1 < n; i++)
        sw3d_tri(&out[0], &out[i], &out[i + 1]);
}

/* Per-batch vertex cache: an index is transformed once. */
#define SW3D_CACHE 65536
static SwVert   *s_vcache;
static uint32_t *s_vtag;
static uint8_t  *s_vok;
static uint32_t  s_vgen = 1;

static const SwVert *sw3d_vertex(uint32_t index)
{
    uint32_t slot = index & (SW3D_CACHE - 1);
    if (!s_vcache) {
        s_vcache = (SwVert *)calloc(SW3D_CACHE, sizeof(SwVert));
        s_vtag = (uint32_t *)calloc(SW3D_CACHE, sizeof(uint32_t) * 2);
        s_vok = (uint8_t *)calloc(SW3D_CACHE, 1);
        if (!s_vcache || !s_vtag || !s_vok) return NULL;
    }
    if (s_vtag[slot * 2] != s_vgen || s_vtag[slot * 2 + 1] != index) {
        s_vtag[slot * 2] = s_vgen;
        s_vtag[slot * 2 + 1] = index;
        s_3d.verts++;
        s_vok[slot] = (uint8_t)sw3d_transform(index, &s_vcache[slot]);
        if (s_vok[slot] && s_vcache[slot].p[3] == 0.0f) s_vcache[slot].p[3] = 1e-30f;
    }
    return s_vok[slot] ? &s_vcache[slot] : NULL;
}

static void sw3d_emit(uint32_t i0, uint32_t i1, uint32_t i2)
{
    const SwVert *a, *b, *c;
    SwVert va, vb;
    if (!(a = sw3d_vertex(i0))) return;
    va = *a;
    if (!(b = sw3d_vertex(i1))) return;
    vb = *b;
    if (!(c = sw3d_vertex(i2))) return;
    sw3d_clip_tri(&va, &vb, c);
}

static void sw3d_diag(void)
{
    static int shown;
    int st;
    uint32_t pc, len = 0;
    const char *db = getenv("RECOMP_DIAG_BATCH");
    static int vpd = -1;
    if (vpd < 0) vpd = getenv("RECOMP_VP_DUMP") != NULL;
    if (!db && !vpd) return;
    for (pc = s_3d.start; pc < VP_SLOTS; pc++) { len++; if (VF(s_3d.prog[pc], 3, 0, 1)) break; }
    if (db) {
        char *e = (char *)db; int hit = 0;
        while (*e) { uint32_t v = (uint32_t)strtoul(e, &e, 0); if (v == s_3d.batches + 1) hit = 1; if (*e == ',') e++; else break; }
        if (!hit) return;
        shown = 1;
    } else {
    if (!getenv("RECOMP_VP_DUMP") || shown >= 60) return;
    if (s_3d.mode == 2 && len <= 3) return;   /* the 2D pass-through */
    if (shown >= 6 && (s_3d.batches % 3001) != 0) return;   /* then a sample now and then */
    }
    shown++;
    s_diag_px = 3;
    if (s_3d.mode == 2 && (shown < 4 || shown % 8 == 0)) {
        fprintf(stderr, "  [3DDIAG] program (%u instr):\n", len);
        for (pc = s_3d.start; pc < s_3d.start + len; pc++) {
            const uint32_t *t = s_3d.prog[pc];
            fprintf(stderr, "  [3DDIAG]  %3u mac%u ilu%u v%u c%u a(%u,%u%s) b(%u,%u) c(%u,%u) R%u m%X im%X -> %s%u om%X %s\n",
                    pc, VF(t,1,21,4), VF(t,1,25,3), VF(t,1,9,4), VF(t,1,13,8),
                    VF(t,2,26,2), VF(t,2,28,4), VF(t,1,8,1) ? "-" : "",
                    VF(t,2,11,2), VF(t,2,13,4), VF(t,3,28,2), (VF(t,2,0,2) << 2) | VF(t,3,30,2),
                    VF(t,3,20,4), VF(t,3,24,4), VF(t,3,16,4), VF(t,3,11,1) ? "o" : "c", VF(t,3,3,8),
                    VF(t,3,12,4), VF(t,3,2,1) ? "ilu" : "mac");
        }
    }
    fprintf(stderr, "  [3DDIAG] mode %u prim %u n %u blend %u %X/%X eq %X cmask %08X ztest %u zfunc %X zmask %u atest %u %X/%u cull %u %X/%X stencil %u\n",
            s_3d.mode, s_gpu.prim, s_gpu.idx_count, s_gpu.blend_enable, s_gpu.blend_sfactor, s_gpu.blend_dfactor,
            s_3d.blend_eq, s_3d.color_mask, s_3d.depth_test, s_3d.depth_func, s_3d.depth_mask,
            s_3d.alpha_test, s_3d.alpha_func, s_3d.alpha_ref, s_3d.cull_enable, s_3d.cull_face,
            s_3d.front_face, s_3d.stencil_test);
    fprintf(stderr, "  [3DDIAG]   comb ctl %08X shprog %08X fog %u/%X col %08X par %g %g  cw0 %08X cw1 %08X\n",
            s_3d.comb_ctl, s_3d.shader_prog, s_3d.fog_enable, s_3d.fog_mode, s_3d.fog_color, s_3d.fog_params[0], s_3d.fog_params[1],
            s_3d.spec_fog0, s_3d.spec_fog1);
    for (st = 0; st < (int)(s_3d.comb_ctl & 0xFF) && st < 8; st++)
        fprintf(stderr, "  [3DDIAG]   stage%d cICW %08X cOCW %08X aICW %08X aOCW %08X f0 %08X f1 %08X\n", st,
                s_3d.c_icw[st], s_3d.c_ocw[st], s_3d.a_icw[st], s_3d.a_ocw[st], s_3d.factor0[st], s_3d.factor1[st]);
    sw3d_setup_stages();
    for (st = 0; st < 4; st++)
        fprintf(stderr, "  [3DDIAG]   stage%d mode %u en %d norm %d %ux%u off %08X fmt %02X valid %d\n", st, s_stage[st].mode,
                s_stage[st].enabled, s_stage[st].normalized, s_stage[st].t.width, s_stage[st].t.height,
                s_stage[st].t.offset, s_stage[st].t.color, s_stage[st].t.valid);
    {
        uint32_t k;
        fprintf(stderr, "  [3DDIAG]   last tex writes:");
        for (k = 0; k < 32 && k < s_texlog_n; k++) {
            uint32_t i = (s_texlog_n - 1 - k) & 31;
            fprintf(stderr, " %04X=%08X", s_texlog[i][0], s_texlog[i][1]);
        }
        fprintf(stderr, "\n");
    }
    for (st = 0; st < 4; st++) {
        const uint32_t *r = &s_tex_reg[st * 0x10];
        fprintf(stderr, "  [3DDIAG]   tex%d off %08X fmt %08X addr %08X ctl0 %08X ctl1 %08X filt %08X rect %08X\n",
                st, r[0], r[1], r[2], r[3], r[4], r[5], r[7]);
    }
    {
        uint32_t a;
        for (a = 0; a < 16; a++)
            if (s_gpu.attr[a].size)
                fprintf(stderr, "  [3DDIAG]   v%u type %u size %u stride %u off %08X\n", a,
                        s_gpu.attr[a].type, s_gpu.attr[a].size, s_gpu.attr[a].stride, s_gpu.attr[a].offset);
    }
    {
        float in[16][4], out[VP_OUTS][4];
        uint32_t a, k;
        {
            uint32_t q;
            for (q = 0; q < s_gpu.idx_count && q < 8; q++) {
                SwVert vv;
                if (sw3d_transform(s_gpu.idx[q], &vv))
                    fprintf(stderr, "  [3DDIAG]   vtx %u: idx %u win %.1f %.1f z %.0f w %.3f\n", q, s_gpu.idx[q],
                            vv.p[0] / vv.p[3], vv.p[1] / vv.p[3], vv.p[2] / vv.p[3], vv.p[3]);
            }
        }
        for (a = 0; a < 16; a++) fetch_attr_any(a, s_gpu.idx[0], in[a]);
        for (a = 0; a < 16; a++)
            if (s_gpu.attr[a].size)
                fprintf(stderr, "  [3DDIAG]   in v%u = %g %g %g %g\n", a, in[a][0], in[a][1], in[a][2], in[a][3]);
        if (s_3d.mode == 2) {
            vp_run(in, out);
            for (k = 0; k < VP_OUTS; k++)
                if (k == 0 || k == 3 || k == 4 || k == 5 || (k >= 9))
                    fprintf(stderr, "  [3DDIAG]   o%u = %g %g %g %g\n", k, out[k][0], out[k][1], out[k][2], out[k][3]);
        }
    }
}

static void sw3d_blend_stats(void)
{
    static struct { uint32_t key, n; } t[64];
    static uint32_t total;
    uint32_t key, i;
    static int on = -1;
    if (on < 0) on = getenv("RECOMP_BLEND_STATS") != NULL;
    if (!on) return;
    key = (s_gpu.blend_enable ? 0x80000000u : 0) | ((s_gpu.blend_sfactor & 0xFFF) << 16)
        | ((s_gpu.blend_dfactor & 0xFFF) << 4) | ((s_3d.color_mask >> 24) & 1) << 3
        | (s_3d.stencil_test << 2) | (s_3d.alpha_test << 1) | (s_3d.color_mask & 1);
    for (i = 0; i < 64; i++)
        if (!t[i].n || t[i].key == key) { t[i].key = key; t[i].n++; break; }
    if (++total % 20000 == 0) {
        fprintf(stderr, "  [BLEND] blend sf/df amask st at rmask:");
        for (i = 0; i < 64 && t[i].n; i++)
            fprintf(stderr, " %u:%03X/%03X:%u%u%u%u x%u", t[i].key >> 31, (t[i].key >> 16) & 0xFFF, (t[i].key >> 4) & 0xFFF,
                    (t[i].key >> 3) & 1, (t[i].key >> 2) & 1, (t[i].key >> 1) & 1, t[i].key & 1, t[i].n);
        fprintf(stderr, "\n");
    }
}

static void sw3d_batch(void)
{
    uint32_t i, n = s_gpu.idx_count;

    sw3d_blend_stats();

    sw3d_diag();
    s_3d.batches++;
    s_vgen++;
    if (!s_vgen) s_vgen = 1;
    if (s_vp_slow < 0) s_vp_slow = getenv("RECOMP_VP_SLOW") != NULL;
    if (s_3d.mode == 2 && !s_vp_slow) vp_decode();
    sw3d_setup_stages();
    sw3d_tex_usage();
    if (!gpu_on()) sw3d_bind_texels();
    sw3d_comb_prepare();
    sw3d_comb_compile();
    {
        int st, k;
        memset(s_attr_need, 0, sizeof s_attr_need);
        for (k = 0; k < 8; k++) s_attr_need[SA_D0 + k] = 1;    /* D0, D1 */
        if (s_3d.fog_enable) s_attr_need[SA_FOG] = 1;
        for (st = 0; st < 4; st++)
            if (s_tex_used[st] || (s_stage[st].mode && s_stage[st].mode != 1 && s_stage[st].mode != 2))
                for (k = 0; k < 4; k++) s_attr_need[SA_T0 + 4 * st + k] = 1;
    }
    if (s_stage[0].enabled) note_texture_use();

    if (gpu_on()) { gpu_batch(); return; }
    s_surface_ok = surface_begin_batch((uint8_t *)xbox_GetMemoryOffset());
    if (!s_surface_ok) return;
    if ((s_3d.depth_test || s_3d.stencil_test)
        && (!s_zbuf || s_gpu.clip_x + s_gpu.clip_w > s_zbuf_w || s_gpu.clip_y + s_gpu.clip_h > s_zbuf_h))
        sw3d_zbuf_clear();   /* never cleared yet: use the title's clear value too */
    if ((s_3d.depth_test && s_3d.depth_mask) || (s_3d.stencil_test && s_3d.st_wmask)) {
        s_zbuf_zeta = s_zeta_offset; s_zbuf_zpitch = s_zeta_pitch; s_zbuf_dirty = 1;
    }
    if (s_rt_n < 0) sw3d_rt_init();
    s_rt_collect = s_rt_n > 0;
    s_jobs_n = 0; s_jobs_area = 0;

    switch (s_gpu.prim) {
    case NV_PRIM_TRIANGLES:
        for (i = 0; i + 2 < n; i += 3) sw3d_emit(s_gpu.idx[i], s_gpu.idx[i+1], s_gpu.idx[i+2]);
        break;
    case NV_PRIM_TRIANGLE_STRIP:
        for (i = 0; i + 2 < n; i++) {
            if (i & 1) sw3d_emit(s_gpu.idx[i+1], s_gpu.idx[i], s_gpu.idx[i+2]);
            else       sw3d_emit(s_gpu.idx[i], s_gpu.idx[i+1], s_gpu.idx[i+2]);
        }
        break;
    case NV_PRIM_TRIANGLE_FAN:
    case NV_PRIM_POLYGON:
        for (i = 1; i + 1 < n; i++) sw3d_emit(s_gpu.idx[0], s_gpu.idx[i], s_gpu.idx[i+1]);
        break;
    case NV_PRIM_QUADS:
        for (i = 0; i + 3 < n; i += 4) {
            sw3d_emit(s_gpu.idx[i], s_gpu.idx[i+1], s_gpu.idx[i+2]);
            sw3d_emit(s_gpu.idx[i], s_gpu.idx[i+2], s_gpu.idx[i+3]);
        }
        break;
    case NV_PRIM_QUAD_STRIP:
        for (i = 0; i + 3 < n; i += 2) {
            sw3d_emit(s_gpu.idx[i], s_gpu.idx[i+1], s_gpu.idx[i+3]);
            sw3d_emit(s_gpu.idx[i], s_gpu.idx[i+3], s_gpu.idx[i+2]);
        }
        break;
    default:
        break;
    }
    s_rt_collect = 0;
    sw3d_flush_jobs();
    {
        static int bl = -1;
        if (bl < 0) bl = getenv("RECOMP_BATCHLOG") != NULL;
        if (bl) {
            int k, plen = 0;
            for (k = 0; k < VP_SLOTS; k++) { plen++; if (s_3d.prog[s_3d.start + k < VP_SLOTS ? s_3d.start + k : 0][3] & 1) break; }
            fprintf(stderr, "B %u prim %u n %u px %llu avg %.3f %.3f %.3f %.3f | md %u prog@%u len %d bl %u %X/%X eq %X cm %08X z %u/%X/%u st %u at %u %X/%X | sp %08X ctl %08X fc %08X %08X",
                    s_3d.batches, s_gpu.prim, n, (unsigned long long)s_bl_px,
                    s_bl_px ? s_bl_sum[0] / s_bl_px : 0, s_bl_px ? s_bl_sum[1] / s_bl_px : 0,
                    s_bl_px ? s_bl_sum[2] / s_bl_px : 0, s_bl_px ? s_bl_sum[3] / s_bl_px : 0,
                    s_3d.mode, s_3d.start, plen, s_gpu.blend_enable, s_gpu.blend_sfactor, s_gpu.blend_dfactor,
                    s_3d.blend_eq, s_3d.color_mask, s_3d.depth_test, s_3d.depth_func, s_3d.depth_mask,
                    s_3d.stencil_test, s_3d.alpha_test, s_3d.alpha_func, s_3d.alpha_ref,
                    s_3d.shader_prog, s_3d.comb_ctl, s_3d.spec_fog0, s_3d.spec_fog1);
            for (k = 0; k < 4; k++)
                if (s_stage[k].enabled) fprintf(stderr, " t%d %08X %ux%u f%02X", k, s_stage[k].t.offset, s_stage[k].t.width, s_stage[k].t.height, s_stage[k].t.color);
            fprintf(stderr, "\n");
        }
        s_bl_px = 0; s_bl_sum[0] = s_bl_sum[1] = s_bl_sum[2] = s_bl_sum[3] = 0;
    }
    if ((s_3d.batches % 2000) == 0)
        fprintf(stderr, "  [GPU3D] %u batches, %u verts, tris in %u out %u culled %u clipped %u, %llu px\n",
                s_3d.batches, s_3d.verts, s_3d.tris_in, s_3d.tris_out, s_3d.tris_culled,
                s_3d.tris_clipped, (unsigned long long)s_3d.pixels);
}

static void sw3d_dump_program(void) { }


/* The title samples its depth buffer as a texture (The Two Towers: a
 * 640x480 A8R8G8B8 "image" at the zeta offset, for a depth-based post pass).
 * Depth lives in s_zbuf here, not in guest memory, so write it out as Z24S8
 * when a stage is about to read the zeta surface. */
static void sw3d_zeta_write(void);
static void sw3d_zeta_sync(void)
{
    uint32_t zbase, st;
    int hit = 0;
    if (!s_zbuf || !s_zbuf_dirty || !s_zbuf_zeta) return;
    zbase = dma_resolve(s_zbuf_zeta);
    for (st = 0; st < 4; st++) {
        const uint32_t *r = &s_tex_reg[st * 0x10];
        if (((r[3] >> 30) & 1) && r[0] && dma_resolve(r[0]) == zbase) hit = 1;
    }
    if (hit) sw3d_zeta_write();
}
static void sw3d_zeta_sync_base(uint32_t base)
{
    if (s_zbuf && s_zbuf_dirty && s_zbuf_zeta && dma_resolve(s_zbuf_zeta) == base) sw3d_zeta_write();
}
static void sw3d_zeta_write(void)
{
    uint8_t *mem = (uint8_t *)xbox_GetMemoryOffset();
    uint32_t zbase = dma_resolve(s_zbuf_zeta), zpitch, x, y;
    zpitch = s_zbuf_zpitch ? s_zbuf_zpitch : s_zbuf_w * 4;
    if (surface_write_refused(zbase, zpitch * s_zbuf_h, "zeta sync")) return;
    for (y = 0; y < s_zbuf_h; y++) {
        uint32_t *row = (uint32_t *)(mem + zbase + (size_t)y * zpitch);
        for (x = 0; x < s_zbuf_w && x * 4 < zpitch; x++) {
            float z = s_zbuf[(size_t)y * s_zbuf_w + x];
            uint32_t zi = z <= 0.0f ? 0u : (z >= 16777215.0f ? 0xFFFFFFu : (uint32_t)z);
            row[x] = (zi << 8) | s_sbuf[(size_t)y * s_zbuf_w + x];
        }
    }
    gpu_note_write(zbase, zpitch * s_zbuf_h);
    s_zbuf_dirty = 0;
}
