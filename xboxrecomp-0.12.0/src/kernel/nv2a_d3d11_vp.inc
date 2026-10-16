/*
 * NV2A vertex programs on the graphics card (included by nv2a_d3d11.inc).
 *
 * The CPU interpreter (vp_run_fast in nv2a_sw3d.inc) was most of the
 * emulated GPU's cost in gameplay. Here each program the title uploads is
 * translated to an HLSL vertex shader, compiled on a background thread, and
 * once ready the batch's vertices go to the GPU as raw inputs: the CPU only
 * fetches the attributes the program reads (as float4s) and the shader runs
 * the program and produces exactly what sw3d_transform would have -- window
 * position times w, D0/D1 clamped, fog, T0-T3 -- then the same window-to-clip
 * mapping as vs_main. Until a program's shader is ready (or if it cannot be
 * translated: one that writes constants) the CPU path draws it.
 *
 * [Display] vertex_shaders=0 in the ini / RECOMP_HW_VP=0 turns it off.
 */

typedef struct {
    uint32_t hash, len, state;        /* state: 0 compiling, 1 ready, 2 failed */
    uint32_t words[VP_SLOTS][4];
    uint32_t nin;                     /* inputs the program reads */
    uint8_t  inreg[16];               /* their attribute registers, in buffer order */
    ID3D11VertexShader *vs;
    ID3D11InputLayout *il;
    char *src;                        /* HLSL, freed after compile */
    volatile LONG claimed;
} HwVp;

#define HWVP_MAX 512
static HwVp *g_hwvp[HWVP_MAX];
static int g_hwvp_n;
static int g_hwvp_on = -1;
static ID3D11Buffer *g_cb_vpc;        /* b2: float4 c[192] */
static float g_vpc_shadow[VP_CONSTS][4];
static int g_vpc_ok;
static ID3D11Buffer *g_vbx;           /* raw inputs ring */
static UINT g_vbx_cap, g_vbx_pos;
static float *g_xin; static uint32_t g_xin_cap;
static uint32_t g_hwvp_stat_hw, g_hwvp_stat_cpu, g_hwvp_compiled, g_hwvp_failed;

/* background compiler */
static HANDLE g_hwvp_ev;
static volatile LONG g_hwvp_pending;

static uint32_t hwvp_hash(const uint32_t (*w)[4], uint32_t len)
{
    uint32_t h = 2166136261u, i, k;
    for (i = 0; i < len; i++) for (k = 0; k < 4; k++) h = (h ^ w[i][k]) * 16777619u;
    return h ^ len;
}

static void hwvp_cat(char **p, size_t *cap, size_t *len, const char *fmt, ...)
{
    va_list ap;
    int n;
    for (;;) {
        va_start(ap, fmt);
        n = vsnprintf(*p + *len, *cap - *len, fmt, ap);
        va_end(ap);
        if (n >= 0 && (size_t)n < *cap - *len) { *len += n; return; }
        *cap *= 2;
        *p = (char *)realloc(*p, *cap);
        if (!*p) return;
    }
}

static const char *hwvp_src_expr(const VpSrc *d, char *buf, size_t bs)
{
    static const char sw[] = "xyzw";
    char base[64];
    switch (d->mux) {
    case 1: if (d->reg == 12) snprintf(base, sizeof base, "o[0]"); else snprintf(base, sizeof base, "R[%u]", d->reg); break;
    case 2: snprintf(base, sizeof base, "v%u", d->reg); break;
    case 3:
        if (d->rel) snprintf(base, sizeof base, "kc(%d + a0)", d->ci);
        else if (d->ci >= 0 && d->ci < VP_CONSTS) snprintf(base, sizeof base, "c[%d]", d->ci);
        else snprintf(base, sizeof base, "float4(0,0,0,1)");
        break;
    default: snprintf(base, sizeof base, "float4(0,0,0,1)"); break;
    }
    snprintf(buf, bs, "%s(%s).%c%c%c%c", d->neg ? "-" : "", base,
             sw[d->sw[0]], sw[d->sw[1]], sw[d->sw[2]], sw[d->sw[3]]);
    return buf;
}

static const char *hwvp_mask(uint32_t m, char *b)
{
    int n = 0;
    b[n++] = '.';
    if (m & 8) b[n++] = 'x';
    if (m & 4) b[n++] = 'y';
    if (m & 2) b[n++] = 'z';
    if (m & 1) b[n++] = 'w';
    b[n] = 0;
    return b;
}

/* Translate the decoded program (s_vpd) to HLSL. Returns NULL if it cannot
 * run on the GPU (it writes constants). */
static char *hwvp_translate(HwVp *e)
{
    size_t cap = 16384, len = 0;
    char *p = (char *)malloc(cap);
    uint32_t k, used = 0, i;
    char a[96], b[96], c[96], mk[8];
    if (!p) return NULL;
    p[0] = 0;
    for (k = 0; k < s_vpd_len; k++) {
        const VpIns *I = &s_vpd[k];
        int s;
        if (I->o_mask && !I->orb) { free(p); return NULL; }
        for (s = 0; s < 3; s++) if (I->s[s].mux == 2) used |= 1u << I->s[s].reg;
    }
    e->nin = 0;
    for (i = 0; i < 16; i++) if (used & (1u << i)) e->inreg[e->nin++] = (uint8_t)i;

    hwvp_cat(&p, &cap, &len,
        "cbuffer VSC : register(b0) { float4 vp; };\n"
        "cbuffer VPC : register(b2) { float4 c[192]; };\n"
        "float4 kc(int i) { return (i >= 0 && i < 192) ? c[i] : float4(0,0,0,1); }\n"
        "float4 rcc(float x) { float r = 1.0 / x; float ar = abs(r);\n"
        "  ar = (ar < 5.42101e-20) ? 5.42101e-20 : ((ar > 1.884467e+19) ? 1.884467e+19 : ar);\n"
        "  r = (r < 0 || (r == 0 && asuint(r) == 0x80000000)) ? -ar : ar; return float4(r,r,r,r); }\n"
        "float4 vexp(float x) { float fl = floor(x); return float4(exp2(fl), x - fl, exp2(x), 1); }\n"
        "float4 vlog(float x) { x = abs(x); if (x == 0) return float4(-asfloat(0x7f800000), 1, -asfloat(0x7f800000), 1);\n"
        "  float e = floor(log2(x)); return float4(e, x / exp2(e), log2(x), 1); }\n"
        "float4 vlit(float4 s) { float nl = max(s.x, 0), nh = max(s.y, 0); float pw = clamp(s.w, -127.9961, 127.9961);\n"
        "  return float4(1, nl, (s.x > 0) ? pow(nh, pw) : 0, 1); }\n"
        "struct VI {");
    for (i = 0; i < e->nin; i++) hwvp_cat(&p, &cap, &len, " float4 v%u:TEXCOORD%u;", e->inreg[i], i);
    if (!e->nin) hwvp_cat(&p, &cap, &len, " uint vid:SV_VertexID;");
    hwvp_cat(&p, &cap, &len,
        " };\n"
        "struct VO { float4 pos:SV_Position; float4 d0:COLOR0; float4 d1:COLOR1; float4 fog:TEXCOORD4;\n"
        "            float4 t0:TEXCOORD0; float4 t1:TEXCOORD1; float4 t2:TEXCOORD2; float4 t3:TEXCOORD3; };\n"
        "VO vs_hw(VI vin) {\n"
        "  float4 R[16]; float4 o[13]; int a0 = 0; float4 m, l;\n"
        "  [unroll] for (int q = 0; q < 16; q++) R[q] = float4(0,0,0,0);\n"
        "  [unroll] for (int q2 = 0; q2 < 13; q2++) o[q2] = float4(0,0,0,1);\n"
        "  o[3] = float4(1,1,1,1);\n");
    for (i = 0; i < e->nin; i++) hwvp_cat(&p, &cap, &len, "  float4 v%u = vin.v%u;\n", e->inreg[i], e->inreg[i]);
    for (k = 0; k < s_vpd_len; k++) {
        const VpIns *I = &s_vpd[k];
        hwvp_src_expr(&I->s[0], a, sizeof a);
        hwvp_src_expr(&I->s[1], b, sizeof b);
        hwvp_src_expr(&I->s[2], c, sizeof c);
        hwvp_cat(&p, &cap, &len, "  m = float4(0,0,0,0); l = float4(0,0,0,0);\n");
        switch (I->mac) {
        case MAC_MOV: hwvp_cat(&p, &cap, &len, "  m = %s;\n", a); break;
        case MAC_MUL: hwvp_cat(&p, &cap, &len, "  m = %s * %s;\n", a, b); break;
        case MAC_ADD: hwvp_cat(&p, &cap, &len, "  m = %s + %s;\n", a, c); break;
        case MAC_MAD: hwvp_cat(&p, &cap, &len, "  m = %s * %s + %s;\n", a, b, c); break;
        case MAC_DP3: hwvp_cat(&p, &cap, &len, "  m = dot((%s).xyz, (%s).xyz).xxxx;\n", a, b); break;
        case MAC_DPH: hwvp_cat(&p, &cap, &len, "  { float4 A_ = %s, B_ = %s; m = (dot(A_.xyz, B_.xyz) + B_.w).xxxx; }\n", a, b); break;
        case MAC_DP4: hwvp_cat(&p, &cap, &len, "  m = dot(%s, %s).xxxx;\n", a, b); break;
        case MAC_DST: hwvp_cat(&p, &cap, &len, "  { float4 A_ = %s, B_ = %s; m = float4(1, A_.y * B_.y, A_.z, B_.w); }\n", a, b); break;
        case MAC_MIN: hwvp_cat(&p, &cap, &len, "  m = min(%s, %s);\n", a, b); break;
        case MAC_MAX: hwvp_cat(&p, &cap, &len, "  m = max(%s, %s);\n", a, b); break;
        case MAC_SLT: hwvp_cat(&p, &cap, &len, "  m = (%s < %s) ? 1.0 : 0.0;\n", a, b); break;
        case MAC_SGE: hwvp_cat(&p, &cap, &len, "  m = (%s >= %s) ? 1.0 : 0.0;\n", a, b); break;
        case MAC_ARL: hwvp_cat(&p, &cap, &len, "  a0 = (int)floor((%s).x + 0.001);\n", a); break;
        default: break;
        }
        switch (I->ilu) {
        case ILU_MOV: hwvp_cat(&p, &cap, &len, "  l = %s;\n", c); break;
        case ILU_RCP: hwvp_cat(&p, &cap, &len, "  l = (1.0 / (%s).x).xxxx;\n", c); break;
        case ILU_RCC: hwvp_cat(&p, &cap, &len, "  l = rcc((%s).x);\n", c); break;
        case ILU_RSQ: hwvp_cat(&p, &cap, &len, "  l = (1.0 / sqrt(abs((%s).x))).xxxx;\n", c); break;
        case ILU_EXP: hwvp_cat(&p, &cap, &len, "  l = vexp((%s).x);\n", c); break;
        case ILU_LOG: hwvp_cat(&p, &cap, &len, "  l = vlog((%s).x);\n", c); break;
        case ILU_LIT: hwvp_cat(&p, &cap, &len, "  l = vlit(%s);\n", c); break;
        default: break;
        }
        if (I->mac && I->mac != MAC_ARL && I->mac_mask && I->out_r < 16 && I->out_r != 12)
            hwvp_cat(&p, &cap, &len, "  R[%u]%s = m%s;\n", I->out_r, hwvp_mask(I->mac_mask, mk), mk);
        if (I->ilu && I->ilu_mask) {
            uint32_t d = I->mac ? 1 : I->out_r;
            if (d < 16 && d != 12) hwvp_cat(&p, &cap, &len, "  R[%u]%s = l%s;\n", d, hwvp_mask(I->ilu_mask, mk), mk);
        }
        if (I->o_mask && I->orb && I->oaddr < VP_OUTS)
            hwvp_cat(&p, &cap, &len, "  o[%u]%s = %s%s;\n", I->oaddr, hwvp_mask(I->o_mask, mk), I->omux ? "l" : "m", mk);
        if (I->final) break;
    }
    hwvp_cat(&p, &cap, &len,
        "  VO r;\n"
        "  float w = o[0].w; if (w == 0) w = 1e-30;\n"
        "  float4 P = float4(o[0].xyz * w, w);\n"
        "  r.pos = float4(P.x * vp.x - P.w, P.w - P.y * vp.y, P.z * vp.z, P.w);\n"
        "  r.d0 = saturate(o[3]); r.d1 = saturate(o[4]); r.fog = float4(o[5].x, 0, 0, 0);\n"
        "  r.t0 = o[9]; r.t1 = o[10]; r.t2 = o[11]; r.t3 = o[12];\n"
        "  return r;\n}\n");
    return p;
}

static void hwvp_compile_one(HwVp *e)
{
    ID3DBlob *b = NULL, *err = NULL;
    HRESULT hr = D3DCompile(e->src, strlen(e->src), "nv2a_vp", NULL, NULL, "vs_hw", "vs_5_0",
                            D3DCOMPILE_OPTIMIZATION_LEVEL1, 0, &b, &err);
    if (FAILED(hr)) {
        static int shown;
        if (shown++ < 3)
            fprintf(stderr, "  [HWVP] program %08X did not compile: %s\n", e->hash,
                    err ? (const char *)ID3D10Blob_GetBufferPointer(err) : "?");
        if (err) ID3D10Blob_Release(err);
        InterlockedIncrement((volatile LONG *)&g_hwvp_failed);
        e->state = 2;
        return;
    }
    if (err) ID3D10Blob_Release(err);
    if (FAILED(ID3D11Device_CreateVertexShader(g_dev, ID3D10Blob_GetBufferPointer(b), ID3D10Blob_GetBufferSize(b), NULL, &e->vs))) {
        ID3D10Blob_Release(b); e->state = 2; return;
    }
    if (e->nin) {
        D3D11_INPUT_ELEMENT_DESC il[16];
        uint32_t i;
        for (i = 0; i < e->nin; i++) {
            il[i].SemanticName = "TEXCOORD"; il[i].SemanticIndex = i;
            il[i].Format = DXGI_FORMAT_R32G32B32A32_FLOAT; il[i].InputSlot = 0;
            il[i].AlignedByteOffset = 16 * i; il[i].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
            il[i].InstanceDataStepRate = 0;
        }
        if (FAILED(ID3D11Device_CreateInputLayout(g_dev, il, e->nin, ID3D10Blob_GetBufferPointer(b),
                                                  ID3D10Blob_GetBufferSize(b), &e->il))) {
            ID3D10Blob_Release(b); ID3D11VertexShader_Release(e->vs); e->vs = NULL; e->state = 2; return;
        }
    }
    ID3D10Blob_Release(b);
    InterlockedIncrement((volatile LONG *)&g_hwvp_compiled);
    MemoryBarrier();
    e->state = 1;
}

static int hwps_compile_pending(void);
static DWORD WINAPI hwvp_thread(LPVOID unused)
{
    (void)unused;
    {
        DWORD_PTR pm = 0, sm = 0;
        if (GetProcessAffinityMask(GetCurrentProcess(), &pm, &sm) && (pm & ~(DWORD_PTR)1))
            SetThreadAffinityMask(GetCurrentThread(), pm & ~(DWORD_PTR)1);
    }
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    for (;;) {
        int i, did = 0;
        WaitForSingleObject(g_hwvp_ev, 100);
        for (i = 0; i < g_hwvp_n; i++) {
            HwVp *e = g_hwvp[i];
            if (e && e->state == 0 && e->src && !InterlockedExchange(&e->claimed, 1)) {
                hwvp_compile_one(e);
                free(e->src); e->src = NULL;
                did = 1;
            }
        }
        if (hwps_compile_pending()) did = 1;
        (void)did;
    }
    return 0;
}

static int hwvp_enabled(void)
{
    if (g_hwvp_on < 0) {
        /* Set up even when the ini says cpu: the settings overlay can switch
         * to the GPU later (g_hwvp_user decides per batch). */
        g_hwvp_on = 1;
        if (g_hwvp_on) {
            g_cb_vpc = gpu_cbuf(sizeof(float) * 4 * VP_CONSTS);
            g_hwvp_ev = CreateEventA(NULL, FALSE, FALSE, NULL);
            if (!g_cb_vpc || !g_hwvp_ev || !CreateThread(NULL, 0, hwvp_thread, NULL, 0, NULL)) g_hwvp_on = 0;
            fprintf(stderr, "  [HWVP] vertex programs on the GPU: %s\n", g_hwvp_on && g_hwvp_user ? "on" : "off");
        }
    }
    return g_hwvp_on;
}

/* The shader for the current program, or NULL to use the CPU path. */
static HwVp *hwvp_lookup(void)
{
    uint32_t h, i;
    HwVp *e;
    if (!g_hwvp_user || !hwvp_enabled() || s_3d.mode != 2 || s_vp_slow || !s_vpd_len) return NULL;
    h = hwvp_hash((const uint32_t (*)[4])s_vpd_src, s_vpd_len);
    {   /* testing: RECOMP_HWVP_DENY=hash,hash,... keeps those programs on the CPU */
        static char deny[512]; static int dl = -1;
        char hx[16];
        if (dl < 0) { const char *e = getenv("RECOMP_HWVP_DENY"); dl = e ? (int)strlen(e) : 0; if (e) snprintf(deny, sizeof deny, "%s", e); }
        if (dl) { snprintf(hx, sizeof hx, "%08X", h); if (strstr(deny, hx)) return NULL; }
    }
    for (i = 0; i < (uint32_t)g_hwvp_n; i++) {
        e = g_hwvp[i];
        if (e->hash == h && e->len == s_vpd_len && !memcmp(e->words, s_vpd_src, s_vpd_len * 16))
            return e->state == 1 ? e : NULL;
    }
    if (g_hwvp_n >= HWVP_MAX) return NULL;
    e = (HwVp *)calloc(1, sizeof *e);
    if (!e) return NULL;
    e->hash = h; e->len = s_vpd_len;
    fprintf(stderr, "  [HWVP] program %08X (%u instructions)\n", h, s_vpd_len);
    memcpy(e->words, s_vpd_src, s_vpd_len * 16);
    e->src = hwvp_translate(e);
    if (!e->src) e->state = 2;
    {
        static int sync = -1;
        if (sync < 0) sync = getenv("RECOMP_HW_VP_SYNC") != NULL;
        if (sync && e->src) {          /* testing: compile now, use it now */
            e->claimed = 1;
            hwvp_compile_one(e);
            free(e->src); e->src = NULL;
        }
    }
    g_hwvp[g_hwvp_n] = e;
    MemoryBarrier();
    g_hwvp_n++;
    if (e->src) SetEvent(g_hwvp_ev);
    return e->state == 1 ? e : NULL;
}

/* Fill the raw-input buffer for nverts slots. */
static void hwvp_pack_range(const HwVp *e, uint32_t lo, uint32_t hi)
{
    uint32_t k, i;
    for (k = lo; k < hi; k++) {
        float *d = g_xin + (size_t)k * 4 * e->nin;
        for (i = 0; i < e->nin; i++) fetch_attr_any(e->inreg[i], g_vidx[k], d + 4 * i);
    }
}
