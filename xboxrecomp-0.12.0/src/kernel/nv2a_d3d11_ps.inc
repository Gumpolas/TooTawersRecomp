/*
 * Specialized pixel shaders (included by nv2a_d3d11.inc).
 *
 * ps_main interprets the register-combiner program from the constant
 * buffer: a loop over up to eight stages, every input and output register
 * chosen at run time. GPUs keep such a dynamically indexed register file in
 * memory rather than registers, so every pixel paid for it -- and the cost
 * grows with the square of the internal resolution (render_scale 2 = 4x the
 * pixels). Here each distinct combiner program the title sets is written out
 * as straight-line HLSL with its registers, mappings, operations, texture
 * stage modes, fog mode and alpha test fixed, compiled on the background
 * thread the vertex programs use, and used once ready. Its constants (the
 * combiner colours, fog colour and parameters, texture scales, alpha
 * reference) still come from the same constant buffer, so one shader serves
 * every batch with the same program. Until it is ready ps_main draws.
 *
 * RECOMP_HW_PS=0 turns it off.
 */

/* PsKey (the program) is declared before gpu_bind_stages in nv2a_d3d11.inc. */

typedef struct {
    PsKey key;
    uint32_t hash, state;             /* 0 compiling, 1 ready, 2 failed */
    ID3D11PixelShader *ps;
    char *src;
    volatile LONG claimed;
} HwPs;

#define HWPS_MAX 1024
static HwPs *g_hwps[HWPS_MAX];
static volatile LONG g_hwps_n;
static int g_hwps_on = -1;
static uint32_t g_hwps_stat_hw, g_hwps_stat_sw, g_hwps_compiled, g_hwps_failed;
static ID3D11PixelShader *g_ps_cur;   /* what this batch draws with */

static const char *ps_cmap(int m, const char *x, char *b, size_t n)
{
    switch (m & 7) {
    case 0: snprintf(b, n, "max(%s, 0.0)", x); break;
    case 1: snprintf(b, n, "(1.0 - saturate(%s))", x); break;
    case 2: snprintf(b, n, "(2.0 * max(%s, 0.0) - 1.0)", x); break;
    case 3: snprintf(b, n, "(-2.0 * max(%s, 0.0) + 1.0)", x); break;
    case 4: snprintf(b, n, "(max(%s, 0.0) - 0.5)", x); break;
    case 5: snprintf(b, n, "(-max(%s, 0.0) + 0.5)", x); break;
    case 6: snprintf(b, n, "(%s)", x); break;
    default: snprintf(b, n, "(-(%s))", x); break;
    }
    return b;
}
/* an rgb input: register c & 15, its alpha replicated if bit 4, mapping c >> 8 */
static const char *ps_crgb(int c, char *b, size_t n)
{
    char x[32], t[96];
    if ((c >> 4) & 1) { snprintf(x, sizeof x, "R%d.a", c & 15); snprintf(b, n, "(%s).xxx", ps_cmap(c >> 8, x, t, sizeof t)); }
    else { snprintf(x, sizeof x, "R%d.rgb", c & 15); ps_cmap(c >> 8, x, b, n); }
    return b;
}
static const char *ps_calp(int c, char *b, size_t n)
{
    char x[32];
    snprintf(x, sizeof x, "R%d.%c", c & 15, ((c >> 4) & 1) ? 'a' : 'b');
    return ps_cmap(c >> 8, x, b, n);
}
static const char *ps_cop(int op, const char *x, char *b, size_t n)
{
    switch (op) {
    case 1: snprintf(b, n, "clamp((%s) - 0.5, -1.0, 1.0)", x); break;
    case 2: snprintf(b, n, "clamp((%s) * 2.0, -1.0, 1.0)", x); break;
    case 3: snprintf(b, n, "clamp(((%s) - 0.5) * 2.0, -1.0, 1.0)", x); break;
    case 4: snprintf(b, n, "clamp((%s) * 4.0, -1.0, 1.0)", x); break;
    case 6: snprintf(b, n, "clamp((%s) * 0.5, -1.0, 1.0)", x); break;
    default: snprintf(b, n, "clamp(%s, -1.0, 1.0)", x); break;
    }
    return b;
}

/* stage k's texel into R(8 + k), as stex() does, with mode and flags fixed */
static void ps_gen_stex(char **p, size_t *cap, size_t *len, const PsKey *K, int k)
{
    int mode = K->smode[k], fl = K->sflags[k];
    if (mode == 0) { hwvp_cat(p, cap, len, "  R%d = float4(0, 0, 0, 0);\n", 8 + k); return; }
    if (mode == 4) { hwvp_cat(p, cap, len, "  R%d = saturate(i.t%d);\n", 8 + k, k); return; }
    if (!(fl & 1)) { hwvp_cat(p, cap, len, "  R%d = float4(0, 0, 0, 1);\n", 8 + k); return; }
    hwvp_cat(p, cap, len, "  { float2 uv = i.t%d.xy;\n", k);
    if (mode == 1) hwvp_cat(p, cap, len, "    if (i.t%d.w != 0.0 && i.t%d.w != 1.0) uv /= i.t%d.w;\n", k, k, k);
    if (!(fl & 4)) hwvp_cat(p, cap, len, "    uv *= sscale[%d].xy;\n", k);
    if (fl & 8)
        hwvp_cat(p, cap, len,
                 "    uint w, h; tx%d.GetDimensions(w, h);\n"
                 "    int2 ip = int2(clamp(floor(uv * float2(w, h)), float2(0, 0), float2(w - 1, h - 1)));\n"
                 "    uint z = (uint)(tx%d.Load(int3(ip, 0)).r * 16777215.0 + 0.5);\n"
                 "    R%d = float4(((z >> 8) & 255) / 255.0, (z & 255) / 255.0, 0.0, ((z >> 16) & 255) / 255.0); }\n",
                 k, k, 8 + k);
    else hwvp_cat(p, cap, len, "    R%d = tx%d.Sample(sm%d, uv); }\n", 8 + k, k, k);
}

static const char *ps_dnorm(int m, const char *c, char *b, size_t n)
{
    if (m == 0) snprintf(b, n, "%s.rgb", c);
    else if (m == 3) snprintf(b, n, "dnorm(%s, 3)", c);
    else snprintf(b, n, "(%s.rgb * 2.0 - 1.0)", c);
    return b;
}

static char *hwps_translate(const PsKey *K)
{
    size_t cap = 8192, len = 0;
    char *p = (char *)malloc(cap), a[160], b2[160], c[160], d[160], t[256];
    int k;
    if (!p) return NULL;
    p[0] = 0;
    /* the shared declarations: everything in s_hlsl_src before ps_main */
    {
        const char *end = strstr(s_hlsl_src, "float4 ps_main(VO i)");
        if (!end) { free(p); return NULL; }
        hwvp_cat(&p, &cap, &len, "%.*s", (int)(end - s_hlsl_src), s_hlsl_src);
    }
    hwvp_cat(&p, &cap, &len, "float4 ps_spec(VO i) : SV_Target {\n"
                             "  float4 R0 = 0, R1 = 0, R2 = 0, R3 = 0, R4 = 0, R5 = 0, R6 = 0, R7 = 0;\n"
                             "  float4 R8 = 0, R9 = 0, R10 = 0, R11 = 0, R12 = 0, R13 = 0, R14 = 0, R15 = 0;\n"
                             "  float dp1 = 0, dp2 = 0;\n");
    /* texture stages, as ps_main orders them */
    if (K->sflags[0] & 2) ps_gen_stex(&p, &cap, &len, K, 0);
    if (K->smode[1] == 17) hwvp_cat(&p, &cap, &len, "  dp1 = dot(i.t1.xyz, %s); R9 = 0;\n", ps_dnorm(K->sdot[1], "R8", a, sizeof a));
    else if (K->sflags[1] & 2) ps_gen_stex(&p, &cap, &len, K, 1);
    if (K->smode[2] == 17)
        hwvp_cat(&p, &cap, &len, "  dp2 = dot(i.t2.xyz, %s); R10 = 0;\n", ps_dnorm(K->sdot[2], K->sinp[2] ? "R9" : "R8", a, sizeof a));
    else if (K->smode[2] == 9) {
        if (K->sflags[2] & 1)
            hwvp_cat(&p, &cap, &len, "  R10 = tx2.Sample(sm2, float2(dp1, dot(i.t2.xyz, %s)));\n",
                     ps_dnorm(K->sdot[2], K->sinp[2] ? "R9" : "R8", a, sizeof a));
        else hwvp_cat(&p, &cap, &len, "  R10 = float4(0, 0, 0, 1);\n");
    } else if (K->sflags[2] & 2) ps_gen_stex(&p, &cap, &len, K, 2);
    if (K->smode[3] == 9) {
        const char *nn = K->sinp[3] == 2 ? "R10" : K->sinp[3] == 1 ? "R9" : "R8";
        if (K->sflags[3] & 1)
            hwvp_cat(&p, &cap, &len, "  R11 = tx3.Sample(sm3, float2(dp2, dot(i.t3.xyz, %s)));\n", ps_dnorm(K->sdot[3], nn, a, sizeof a));
        else hwvp_cat(&p, &cap, &len, "  R11 = float4(0, 0, 0, 1);\n");
    } else if (K->sflags[3] & 2) ps_gen_stex(&p, &cap, &len, K, 3);
    hwvp_cat(&p, &cap, &len, "  R4 = i.d0; R5 = i.d1;\n  R3 = float4(fogc.rgb, 1.0);\n");
    if (K->fogen) {
        int fm = K->fogmode;
        hwvp_cat(&p, &cap, &len, "  { float dd = i.fog.x, f;\n");
        if (fm == 0x800 || fm == 0x802)
            hwvp_cat(&p, &cap, &len, "%s    f = fogp.x + exp2(dd * fogp.y * 16.0) - 1.5;\n", fm == 0x802 ? "    dd = abs(dd);\n" : "");
        else if (fm == 0x801 || fm == 0x803)
            hwvp_cat(&p, &cap, &len, "%s    f = fogp.x + exp2(-dd * dd * fogp.y * fogp.y * 32.0) - 1.5;\n", fm == 0x803 ? "    dd = abs(dd);\n" : "");
        else hwvp_cat(&p, &cap, &len, "%s    f = fogp.x + dd * fogp.y - 1.0;\n", fm == 0x804 ? "    dd = abs(dd);\n" : "");
        hwvp_cat(&p, &cap, &len, "    R3.a = saturate(f); }\n");
    }
    hwvp_cat(&p, &cap, &len, "  R12.a = R8.a;\n  float4 o;\n");
    if (K->misc[3] == 0) {
        hwvp_cat(&p, &cap, &len, "  o = %s * R4;\n", K->at_s0 ? "R8" : "float4(1, 1, 1, 1)");
    } else {
        for (k = 0; k < K->misc[0] && k < 8; k++) {
            const int *S = K->st[k];
            /* S: cin[0..3] ain[4..7] f1[8..11] f2[12..15] f3[16..19] */
            int f1y = S[9], op = S[10], aop = S[11];
            hwvp_cat(&p, &cap, &len, "  { R1 = st[%d].c0; R2 = st[%d].c1;\n", k, k);
            hwvp_cat(&p, &cap, &len, "    float3 A = %s, B = %s, C = %s, D = %s;\n",
                     ps_crgb(S[0], a, sizeof a), ps_crgb(S[1], b2, sizeof b2), ps_crgb(S[2], c, sizeof c), ps_crgb(S[3], d, sizeof d));
            hwvp_cat(&p, &cap, &len, "    float3 ab = %s, cd = %s;\n", (f1y & 2) ? "dot(A, B).xxx" : "A * B", (f1y & 1) ? "dot(C, D).xxx" : "C * D");
            if ((f1y & 4) || (f1y & 8))
                hwvp_cat(&p, &cap, &len, "    bool sel = %s;\n", K->misc[1] ? "R12.a >= 0.5" : "(((int)(R12.a * 255.0 + 0.5)) & 1) != 0");
            hwvp_cat(&p, &cap, &len, "    float3 sm = %s;\n", (f1y & 4) ? "sel ? cd : ab" : "ab + cd");
            hwvp_cat(&p, &cap, &len, "    float3 rab = %s;\n", ps_cop(op, "ab", t, sizeof t));
            hwvp_cat(&p, &cap, &len, "    float3 rcd = %s;\n", ps_cop(op, "cd", t, sizeof t));
            hwvp_cat(&p, &cap, &len, "    float3 rsm = %s;\n", ps_cop(op, "sm", t, sizeof t));
            hwvp_cat(&p, &cap, &len, "    float a0 = %s, a1 = %s, a2 = %s, a3 = %s;\n",
                     ps_calp(S[4], a, sizeof a), ps_calp(S[5], b2, sizeof b2), ps_calp(S[6], c, sizeof c), ps_calp(S[7], d, sizeof d));
            hwvp_cat(&p, &cap, &len, "    float aab = a0 * a1, acd = a2 * a3, asm_ = %s;\n", (f1y & 8) ? "sel ? acd : aab" : "aab + acd");
            hwvp_cat(&p, &cap, &len, "    float ra0 = %s;\n", ps_cop(aop, "aab", t, sizeof t));
            hwvp_cat(&p, &cap, &len, "    float ra1 = %s;\n", ps_cop(aop, "acd", t, sizeof t));
            hwvp_cat(&p, &cap, &len, "    float ra2 = %s;\n", ps_cop(aop, "asm_", t, sizeof t));
            if (S[12]) { hwvp_cat(&p, &cap, &len, "    R%d.rgb = rab;\n", S[12]); if (S[19] & 2) hwvp_cat(&p, &cap, &len, "    R%d.a = rab.b;\n", S[12]); }
            if (S[13]) { hwvp_cat(&p, &cap, &len, "    R%d.rgb = rcd;\n", S[13]); if (S[19] & 1) hwvp_cat(&p, &cap, &len, "    R%d.a = rcd.b;\n", S[13]); }
            if (S[14]) hwvp_cat(&p, &cap, &len, "    R%d.rgb = rsm;\n", S[14]);
            if (S[16]) hwvp_cat(&p, &cap, &len, "    R%d.a = ra0;\n", S[16]);
            if (S[17]) hwvp_cat(&p, &cap, &len, "    R%d.a = ra1;\n", S[17]);
            if (S[18]) hwvp_cat(&p, &cap, &len, "    R%d.a = ra2;\n", S[18]);
            hwvp_cat(&p, &cap, &len, "  }\n");
        }
        if (K->fin1[3] == 0) {
            hwvp_cat(&p, &cap, &len, "  o = saturate(R12);\n");
        } else {
            hwvp_cat(&p, &cap, &len, "  R1 = ff0; R2 = ff1;\n");
            hwvp_cat(&p, &cap, &len, "  { float3 E = %s, F = %s;\n    R15.rgb = E * F; }\n",
                     ps_crgb(K->fin1[0], a, sizeof a), ps_crgb(K->fin1[1], b2, sizeof b2));
            hwvp_cat(&p, &cap, &len, "  R14.rgb = R5.rgb + R12.rgb;\n");
            if (K->misc[2]) hwvp_cat(&p, &cap, &len, "  R14.rgb = min(R14.rgb, 1.0);\n");
            hwvp_cat(&p, &cap, &len, "  { float3 A = %s, B = %s, C = %s, D = %s;\n",
                     ps_crgb(K->fin0[0], a, sizeof a), ps_crgb(K->fin0[1], b2, sizeof b2), ps_crgb(K->fin0[2], c, sizeof c), ps_crgb(K->fin0[3], d, sizeof d));
            hwvp_cat(&p, &cap, &len, "    float3 sa = saturate(A);\n    o.rgb = saturate(sa * B + (1.0 - sa) * C + D);\n");
            hwvp_cat(&p, &cap, &len, "    o.a = saturate(%s); }\n", ps_calp(K->fin1[2], a, sizeof a));
        }
    }
    if (K->at_en) {
        static const char *cmp[7] = { "false", "a8 < r", "a8 == r", "a8 <= r", "a8 > r", "a8 != r", "a8 >= r" };
        int f = K->at_func & 15;
        if (f < 7)
            hwvp_cat(&p, &cap, &len, "  { int a8 = (int)(saturate(o.a) * 255.0 + 0.5), r = atest.z;\n"
                                     "    if (!(%s)) discard; }\n", cmp[f]);
    }
    hwvp_cat(&p, &cap, &len, "  return o;\n}\n");
    return p;
}

static void hwps_compile_one(HwPs *e)
{
    ID3DBlob *b = NULL, *err = NULL;
    HRESULT hr = D3DCompile(e->src, strlen(e->src), "nv2a_ps", NULL, NULL, "ps_spec", "ps_5_0",
                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &b, &err);
    if (FAILED(hr)) {
        static int shown;
        if (shown++ < 3)
            fprintf(stderr, "  [HWPS] combiner program %08X did not compile: %s\n", e->hash,
                    err ? (const char *)ID3D10Blob_GetBufferPointer(err) : "?");
        if (err) ID3D10Blob_Release(err);
        InterlockedIncrement((volatile LONG *)&g_hwps_failed);
        e->state = 2;
        return;
    }
    if (err) ID3D10Blob_Release(err);
    if (FAILED(ID3D11Device_CreatePixelShader(g_dev, ID3D10Blob_GetBufferPointer(b), ID3D10Blob_GetBufferSize(b), NULL, &e->ps))) {
        ID3D10Blob_Release(b); e->state = 2; return;
    }
    ID3D10Blob_Release(b);
    InterlockedIncrement((volatile LONG *)&g_hwps_compiled);
    MemoryBarrier();
    e->state = 1;
}

/* from the vertex-program compile thread */
static int hwps_compile_pending(void)
{
    int i, n = (int)g_hwps_n, did = 0;
    for (i = 0; i < n; i++) {
        HwPs *e = g_hwps[i];
        if (e && e->state == 0 && e->src && !InterlockedExchange(&e->claimed, 1)) {
            hwps_compile_one(e);
            free(e->src); e->src = NULL;
            did = 1;
        }
    }
    return did;
}

static uint32_t hwps_hash(const PsKey *k)
{
    const uint32_t *w = (const uint32_t *)k;
    uint32_t h = 2166136261u, i;
    for (i = 0; i < sizeof *k / 4; i++) h = (h ^ w[i]) * 16777619u;
    return h;
}

/* The shader for the program in this key (ps_main until it is ready). */
static ID3D11PixelShader *hwps_lookup(const PsKey *k)
{
    uint32_t h, i, n;
    HwPs *e;
    static HwPs *last;
    if (g_hwps_on < 0) {
        const char *v = getenv("RECOMP_HW_PS");
        g_hwps_on = !(v && v[0] == '0');
        fprintf(stderr, "  [HWPS] specialized pixel shaders: %s\n", g_hwps_on ? "on" : "off");
    }
    if (!g_hwps_on || !hwvp_enabled()) return g_ps;
    if (last && !memcmp(&last->key, k, sizeof *k)) {
        if (last->state == 1) { g_hwps_stat_hw++; return last->ps; }
        g_hwps_stat_sw++;
        return g_ps;
    }
    h = hwps_hash(k);
    n = (uint32_t)g_hwps_n;
    for (i = 0; i < n; i++) {
        e = g_hwps[i];
        if (e->hash == h && !memcmp(&e->key, k, sizeof *k)) {
            last = e;
            if (e->state == 1) { g_hwps_stat_hw++; return e->ps; }
            g_hwps_stat_sw++;
            return g_ps;
        }
    }
    g_hwps_stat_sw++;
    if (n >= HWPS_MAX) return g_ps;
    e = (HwPs *)calloc(1, sizeof *e);
    if (!e) return g_ps;
    e->key = *k; e->hash = h;
    e->src = hwps_translate(k);
    if (!e->src) e->state = 2;
    {
        static int sync = -1;
        if (sync < 0) sync = getenv("RECOMP_HW_PS_SYNC") != NULL;
        if (sync && e->src) {          /* testing: compile now, use it now */
            e->claimed = 1;
            hwps_compile_one(e);
            free(e->src); e->src = NULL;
        }
    }
    g_hwps[n] = e;
    MemoryBarrier();
    InterlockedIncrement(&g_hwps_n);
    last = e;
    if (e->src) SetEvent(g_hwvp_ev);
    return e->state == 1 ? e->ps : g_ps;
}
