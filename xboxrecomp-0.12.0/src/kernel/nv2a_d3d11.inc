/*
 * nv2a_d3d11.inc -- Direct3D 11 backend for the pushbuffer executor.
 *
 * Included by nv2a_pb_exec.c after nv2a_sw3d.inc, whose decoded state it
 * reuses: vertex programs still run on the CPU (sw3d_vertex), and the
 * register combiners, blending, depth and stencil the title programmed are
 * handed to the GPU. The software rasteriser stays as the fallback
 * ([Display] renderer=software, or when no D3D11 device can be made) and as
 * the reference the replay tool compares against.
 *
 * Surfaces: every colour and zeta surface the title draws into gets a D3D
 * texture, found again by its guest address. A stage whose texture address is
 * one of those binds the GPU copy (render-to-texture, the back-buffer copy for
 * the heat haze, the depth buffer sampled as A8R8G8B8); anything else is
 * decoded from guest memory by the same texture cache the software path uses
 * and uploaded. Guest memory is not written back except on request
 * (gpu_readback: frame dumps, the replay tool, captures).
 *
 * All calls happen on the executor's thread under the pushbuffer lock, so the
 * immediate context needs no further locking.
 */
#define COBJMACROS
#include <d3d11.h>
#include <d3dcompiler.h>
#include "../platform/d3dcompile_dyn.h"
#include "../platform/d3d11_dyn.h"
#include "nv2a_d3d11_shaders.h"

extern HWND xbox_FramebufferHwnd(void);
extern void xbox_FramebufferSetGpuOwned(int on);

static int s_gpu_mode = -1;             /* -1 undecided, 0 software, 1 D3D11 */
static ID3D11Device *g_dev;
static ID3D11DeviceContext *g_ctx;
static IDXGISwapChain *g_sc;
static ID3D11RenderTargetView *g_sc_rtv;
static HWND g_sc_hwnd;
static UINT g_sc_w, g_sc_h;
static ID3D11VertexShader *g_vs, *g_vs_quad, *g_vs_present, *g_vs_blit;
static ID3D11PixelShader *g_ps, *g_ps_clear, *g_ps_present, *g_ps_copy, *g_ps_fxaa, *g_ps_zres;

/* Internal resolution: every render target and depth buffer is allocated at
 * g_rs times the size the title asked for ([Display] render_scale, env
 * RECOMP_RENDER_SCALE, 1-8). The title still sees its own sizes; viewports,
 * scissors and copies are scaled, CPU-written pixels are scaled up on upload
 * and read-backs scaled down. */
static int g_rs = 1;
static float g_rsf = 1.0f;   /* the scale itself (4.5: 2160 lines) */
extern float xbox_GfxScaleF(void);
/* Horizontal / vertical scale actually applied: g_rs, and wider still for a
 * widescreen picture (RECOMP_ASPECT > 4:3) so the stretched frame keeps full
 * horizontal detail. */
static float g_rsx = 1.0f, g_rsy = 1.0f;
static float g_aspect = 4.0f / 3.0f;     /* shape of the picture on screen */
/* (1.9) the frame-sized targets within the largest texture the card takes
 * (16384 wide): 12x at 32:9 would be 20480 -- both scales come down */
static void rs_fit(void)
{
    float mx = 16384.0f, k = 1.0f;
    if (640.0f * g_rsx > mx) k = mx / (640.0f * g_rsx);
    if (480.0f * g_rsy * k > mx) k = mx / (480.0f * g_rsy);
    if (k < 1.0f) {
        g_rsx *= k; g_rsy *= k;
        fprintf(stderr, "  [GPU] internal resolution held to %.0f x %.0f (the largest texture: %.0f)\n", 640.0f * g_rsx, 480.0f * g_rsy, mx);
        fflush(stderr);
    }
}
/* Widescreen bookkeeping. A frame is shown at g_aspect only when it drew 3D
 * (a real vertex program) and the title's camera was widened recently
 * (g_ws_cam_tick, set by the game's widescreen hook); menus, loading screens
 * and movies are 2D and stay 4:3. In a wide frame, the title's 2D overlay
 * (pass-through programs: HUD, text) is drawn at 4:3 proportions in the
 * middle instead of stretched; full-width overlays (fades) still cover all. */
volatile uint64_t g_ws_cam_tick;
static int g_frame_3d, g_wide_prev;
static unsigned g_present_n;
/* Only frame-sized targets are drawn larger: the 128/256-pixel offscreen
 * passes (reflections, shadows, lava, glow) gain little from it and would
 * cost the square of the scale in fill. */
#define GS_SCALED(w, h) ((w) >= 320 && (h) >= 200)
#define RSX_(v, w, h) ((uint32_t)((float)(v) * (GS_SCALED(w, h) ? g_rsx : 1.0f) + 0.5f))
#define RSY_(v, w, h) ((uint32_t)((float)(v) * (GS_SCALED(w, h) ? g_rsy : 1.0f) + 0.5f))
/* a coordinate on surface s */
#define SX(s, v) ((uint32_t)((float)(v) * (float)(s)->pw / (float)(s)->w + 0.5f))
#define SY(s, v) ((uint32_t)((float)(v) * (float)(s)->ph / (float)(s)->h + 0.5f))

/* Texture filtering options: RECOMP_ANISO=2..16 (anisotropic filtering on
 * mipmapped stages), RECOMP_MIPMAPS=0 to keep textures at their top level
 * only (the old behaviour). */
static int g_aniso = -1, g_mips = -1;
/* Options the settings overlay can change while running (src/video/gfx_menu.c). */
#include "../video/gfx_menu.h"
static long g_gfx_seen = 1;
static int g_aa_on = -1, g_vsync_on = -1, g_stretch_on = -1, g_hwvp_user = 1;
/* Menus fill the screen: a frame that drew no 3D (menus, the upgrade and
 * results screens, loading screens) is drawn inside the TV safe area, the
 * full picture scaled by 593/640 about its middle with black round it; it
 * is shown that much larger, so the art reaches the screen's edges and the
 * margin falls outside. Movies and play are left as they are.
 * RECOMP_MENU_FILL=0: as before. */
static float menu_fill_zoom(int menu2d)
{
    /* (the F10 menu's "Menus"; [Display] menu_fill, RECOMP_MENU_FILL) */
    return (xbox_GfxGet(GFX_MENUFILL) == 0 && menu2d) ? 640.0f / 593.0f : 1.0f;
}

/* Movies in widescreen (1.7.8): with a wide picture chosen ([Display]
 * aspect wider than 4:3), a movie fills the window's width at its own shape,
 * the top and bottom cut -- the game's films are letterboxed inside 4:3
 * (about 1.85:1), so at 16:9 the picture fills the screen with nothing of it
 * lost and nothing stretched. Off: the films looked zoomed in; as before
 * (4:3, or stretched with "Stretch to fill") unless RECOMP_MOVIE_FILL=1. */
static int movie_fill(int movie, float ww, float wh, float *pw, float *ph)
{
    /* (the F10 menu's "Movies in widescreen", off by default: the films look
     * a little close; [Display] movie_fill, RECOMP_MOVIE_FILL) */
    if (xbox_GfxGet(GFX_MOVIEFILL) != 1 || !movie || g_aspect <= 1.34f || ww / wh <= 4.0f / 3.0f + 0.01f) return 0;
    *pw = ww; *ph = ww * 0.75f;
    return 1;
}

static void gpu_filter_opts(void)
{
    if (g_aniso < 0) {
        const char *a = getenv("RECOMP_ANISO"), *m = getenv("RECOMP_MIPMAPS");
        g_aniso = a ? atoi(a) : 1;
        if (g_aniso < 1) g_aniso = 1;
        if (g_aniso > 16) g_aniso = 16;
        g_mips = !(m && m[0] == '0');
        fprintf(stderr, "  [D3D11] texture filtering: mipmaps %s, anisotropic x%d\n", g_mips ? "on" : "off", g_aniso);
    }
}

static ID3D11InputLayout *g_il;
static ID3D11Buffer *g_vb, *g_ib, *g_cb_vs, *g_cb_ps, *g_cb_cl, *g_cb_post;
static float g_post[4] = { 1, 0, 1, 0 };   /* brightness (gamma), sharpening, saturation */
static UINT g_vb_cap, g_ib_cap;
static ID3D11SamplerState *g_present_smp;

/* ---------------- surfaces ---------------- */

typedef struct {
    uint32_t base, w, h, is_depth;
    uint32_t pw, ph;                    /* allocated size: w, h times g_rs */
    ID3D11Texture2D *tex;
    ID3D11RenderTargetView *rtv;
    ID3D11DepthStencilView *dsv;
    ID3D11ShaderResourceView *srv;
    uint32_t mem_sum;                   /* guest memory checksum when last in sync */
    uint32_t pitch;                     /* bytes per row of the guest copy */
    uint64_t used;
    uint32_t synced_run;                /* pushbuffer run of the last memory check */
    uint64_t wr;                        /* when last bound as a target */
    uint64_t rw;                        /* when last written: drawn into with writes on, cleared, copied to, re-read from memory */
    /* (1.9) MSAA: drawing goes to a multisampled copy (mtex; rtv / dsv are
     * its views then, rtv1 / dsv1 the plain texture's). ms_new: drawn since
     * the plain texture was made from it (resolved before anything reads
     * it); ss_new: the plain texture written another way (an upload, a copy)
     * since -- drawn into the multisampled one before it is drawn on. */
    ID3D11Texture2D *mtex;
    ID3D11RenderTargetView *rtv1;
    ID3D11DepthStencilView *dsv1;
    ID3D11ShaderResourceView *msrv;
    uint8_t ms_new, ss_new;
} GSurf;
static int g_msaa = 1;   /* samples: 1 (none), 2, 4 or 8 */
static uint64_t g_wr_clock;
extern volatile uint32_t g_pb_runs;
#define GSURF_MAX 48
static GSurf g_surf[GSURF_MAX];
static uint64_t g_clock;

static uint32_t gpu_mem_sum(uint32_t base, uint32_t bytes)
{
    const uint8_t *m = (const uint8_t *)xbox_GetMemoryOffset() + base;
    uint32_t h = 2166136261u, i, step = bytes / 1024;
    if (step < 4) step = 4;
    for (i = 0; i + 4 <= bytes && i < 1024 * step; i += step)
        h = (h ^ *(const uint32_t *)(m + (i & ~3u))) * 16777619u;
    return h;
}

/* Scratch 1x texture for scaled uploads/read-backs. */
static ID3D11Texture2D *g_one_tex; static ID3D11ShaderResourceView *g_one_srv;
static ID3D11RenderTargetView *g_one_rtv; static uint32_t g_one_w, g_one_h;
static ID3D11SamplerState *g_copy_smp;
static void gpu_sh_reset(void);
static int gpu_one(uint32_t w, uint32_t h)
{
    D3D11_TEXTURE2D_DESC td;
    if (g_one_tex && g_one_w >= w && g_one_h >= h) return 1;
    if (g_one_srv) ID3D11ShaderResourceView_Release(g_one_srv);
    if (g_one_rtv) ID3D11RenderTargetView_Release(g_one_rtv);
    if (g_one_tex) ID3D11Texture2D_Release(g_one_tex);
    g_one_srv = NULL; g_one_rtv = NULL; g_one_tex = NULL;
    if (w < g_one_w) w = g_one_w;
    if (h < g_one_h) h = g_one_h;
    memset(&td, 0, sizeof td);
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    if (FAILED(ID3D11Device_CreateTexture2D(g_dev, &td, NULL, &g_one_tex))) { g_one_tex = NULL; return 0; }
    ID3D11Device_CreateShaderResourceView(g_dev, (ID3D11Resource *)g_one_tex, NULL, &g_one_srv);
    ID3D11Device_CreateRenderTargetView(g_dev, (ID3D11Resource *)g_one_tex, NULL, &g_one_rtv);
    g_one_w = w; g_one_h = h;
    return g_one_srv && g_one_rtv;
}

/* Draw srv (its top-left uw x uh of a sw x sh texture) onto rtv's top-left
 * dw x dh, alpha included. */
static void gpu_copy_scaled(ID3D11ShaderResourceView *srv, uint32_t uw, uint32_t uh, uint32_t sw, uint32_t sh,
                            ID3D11RenderTargetView *rtv, uint32_t dw, uint32_t dh)
{
    D3D11_VIEWPORT vp;
    float uvs[4];
    ID3D11ShaderResourceView *none = NULL;
    vp.TopLeftX = 0; vp.TopLeftY = 0; vp.Width = (float)dw; vp.Height = (float)dh; vp.MinDepth = 0; vp.MaxDepth = 1;
    ID3D11DeviceContext_OMSetRenderTargets(g_ctx, 1, &rtv, NULL);
    ID3D11DeviceContext_RSSetViewports(g_ctx, 1, &vp);
    ID3D11DeviceContext_RSSetState(g_ctx, NULL);
    ID3D11DeviceContext_OMSetBlendState(g_ctx, NULL, NULL, 0xFFFFFFFFu);
    ID3D11DeviceContext_OMSetDepthStencilState(g_ctx, NULL, 0);
    uvs[0] = (float)uw / (float)sw; uvs[1] = (float)uh / (float)sh; uvs[2] = uvs[3] = 0;
    ID3D11DeviceContext_UpdateSubresource(g_ctx, (ID3D11Resource *)g_cb_cl, 0, NULL, uvs, 0, 0);
    ID3D11DeviceContext_IASetInputLayout(g_ctx, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(g_ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(g_ctx, g_vs_present, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(g_ctx, 2, 1, &g_cb_cl);
    ID3D11DeviceContext_PSSetShader(g_ctx, g_ps_copy, NULL, 0);
    ID3D11DeviceContext_PSSetShaderResources(g_ctx, 0, 1, &srv);
    ID3D11DeviceContext_PSSetSamplers(g_ctx, 0, 1, &g_copy_smp);
    ID3D11DeviceContext_Draw(g_ctx, 3, 0);
    ID3D11DeviceContext_PSSetShaderResources(g_ctx, 0, 1, &none);
    gpu_sh_reset();
}

/* Guest pixels (w x h at pitch) into a colour surface, scaled if need be. */


static void gsurf_free(GSurf *s)
{
    if (s->rtv) ID3D11RenderTargetView_Release(s->rtv);
    if (s->dsv) ID3D11DepthStencilView_Release(s->dsv);
    if (s->mtex) {
        if (s->rtv1) ID3D11RenderTargetView_Release(s->rtv1);
        if (s->dsv1) ID3D11DepthStencilView_Release(s->dsv1);
        if (s->msrv) ID3D11ShaderResourceView_Release(s->msrv);
        ID3D11Texture2D_Release(s->mtex);
    }
    if (s->srv) ID3D11ShaderResourceView_Release(s->srv);
    if (s->tex) ID3D11Texture2D_Release(s->tex);
    memset(s, 0, sizeof *s);
}

static void gsurf_upload(GSurf *s, const uint8_t *m, uint32_t pitch)
{
    D3D11_BOX b = { 0, 0, 0, s->w, s->h, 1 };
    if (s->mtex) { s->ss_new = 1; s->ms_new = 0; }   /* (the whole surface: what was drawn is replaced) */
    if (s->pw == s->w && s->ph == s->h) {
        ID3D11DeviceContext_UpdateSubresource(g_ctx, (ID3D11Resource *)s->tex, 0, &b, m, pitch, 0);
        return;
    }
    if (!s->rtv1 || !gpu_one(s->w, s->h)) return;
    ID3D11DeviceContext_UpdateSubresource(g_ctx, (ID3D11Resource *)g_one_tex, 0, &b, m, pitch, 0);
    gpu_copy_scaled(g_one_srv, s->w, s->h, g_one_w, g_one_h, s->rtv1, s->pw, s->ph);
}

/* ---- (1.9) MSAA ---- */
static int g_msaa_bad;   /* a multisampled surface could not be made: off at the next frame */
/* the samples for a surface this size: fewer when the picture is huge (a
 * 12x internal resolution at 8x would want gigabytes) */
static int msaa_samples(uint32_t pw, uint32_t ph)
{
    int n = g_msaa;
    while (n > 1 && (uint64_t)pw * ph * (uint64_t)n * 8u > (1536ull << 20)) n >>= 1;   /* (colour + zeta, 1.5 GB) */
    while (n > 1) {
        UINT q1 = 0, q2 = 0;
        ID3D11Device_CheckMultisampleQualityLevels(g_dev, DXGI_FORMAT_B8G8R8A8_UNORM, (UINT)n, &q1);
        ID3D11Device_CheckMultisampleQualityLevels(g_dev, DXGI_FORMAT_R24G8_TYPELESS, (UINT)n, &q2);
        if (q1 && q2) break;
        n >>= 1;
    }
    return n;
}
static void gsurf_ms_make(GSurf *s, const D3D11_TEXTURE2D_DESC *plain)
{
    D3D11_TEXTURE2D_DESC td = *plain;
    int n = msaa_samples(td.Width, td.Height);
    if (n <= 1) return;
    td.SampleDesc.Count = (UINT)n; td.SampleDesc.Quality = 0;
    td.BindFlags = s->is_depth ? (D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE) : D3D11_BIND_RENDER_TARGET;
    if (FAILED(ID3D11Device_CreateTexture2D(g_dev, &td, NULL, &s->mtex))) { s->mtex = NULL; g_msaa_bad = 1; return; }
    if (s->is_depth) {
        D3D11_DEPTH_STENCIL_VIEW_DESC dd;
        D3D11_SHADER_RESOURCE_VIEW_DESC sd;
        ID3D11DepthStencilView *v = NULL;
        memset(&dd, 0, sizeof dd);
        dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
        memset(&sd, 0, sizeof sd);
        sd.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
        if (FAILED(ID3D11Device_CreateDepthStencilView(g_dev, (ID3D11Resource *)s->mtex, &dd, &v))) v = NULL;
        ID3D11Device_CreateShaderResourceView(g_dev, (ID3D11Resource *)s->mtex, &sd, &s->msrv);
        if (!v) { if (s->msrv) ID3D11ShaderResourceView_Release(s->msrv); s->msrv = NULL; ID3D11Texture2D_Release(s->mtex); s->mtex = NULL; g_msaa_bad = 1; return; }
        s->dsv1 = s->dsv; s->dsv = v;
        {
            uint32_t zv = s_cb_scratch[0];
            ID3D11DeviceContext_ClearDepthStencilView(g_ctx, s->dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                                                      (float)(zv >> 8) / 16777215.0f, (UINT8)(zv & 0xFF));
        }
    } else {
        ID3D11RenderTargetView *v = NULL;
        float z[4] = { 0, 0, 0, 0 };
        if (FAILED(ID3D11Device_CreateRenderTargetView(g_dev, (ID3D11Resource *)s->mtex, NULL, &v))) {
            ID3D11Texture2D_Release(s->mtex); s->mtex = NULL; g_msaa_bad = 1; return;
        }
        s->rtv1 = s->rtv; s->rtv = v;
        ID3D11DeviceContext_ClearRenderTargetView(g_ctx, s->rtv, z);
        s->ss_new = 1;
    }
}
/* the plain texture made from what was drawn (before anything reads it) */
static void gsurf_resolve(GSurf *s)
{
    if (!s || !s->mtex || !s->ms_new) return;
    s->ms_new = 0;
    if (!s->is_depth) {
        ID3D11DeviceContext_ResolveSubresource(g_ctx, (ID3D11Resource *)s->tex, 0, (ID3D11Resource *)s->mtex, 0, DXGI_FORMAT_B8G8R8A8_UNORM);
        return;
    }
    if (!g_ps_zres || !s->msrv || !s->dsv1) return;
    {   /* depth: one sample each, written as depth (a depth surface cannot be resolved) */
        static ID3D11DepthStencilState *dss;
        D3D11_VIEWPORT vp;
        float uvs[4] = { 1, 1, 0, 0 };
        ID3D11ShaderResourceView *none = NULL;
        if (!dss) {
            D3D11_DEPTH_STENCIL_DESC d;
            memset(&d, 0, sizeof d);
            d.DepthEnable = TRUE; d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; d.DepthFunc = D3D11_COMPARISON_ALWAYS;
            if (FAILED(ID3D11Device_CreateDepthStencilState(g_dev, &d, &dss))) return;
        }
        vp.TopLeftX = 0; vp.TopLeftY = 0; vp.Width = (float)s->pw; vp.Height = (float)s->ph; vp.MinDepth = 0; vp.MaxDepth = 1;
        ID3D11DeviceContext_OMSetRenderTargets(g_ctx, 0, NULL, s->dsv1);
        ID3D11DeviceContext_RSSetViewports(g_ctx, 1, &vp);
        ID3D11DeviceContext_RSSetState(g_ctx, NULL);
        ID3D11DeviceContext_OMSetBlendState(g_ctx, NULL, NULL, 0xFFFFFFFFu);
        ID3D11DeviceContext_OMSetDepthStencilState(g_ctx, dss, 0);
        ID3D11DeviceContext_UpdateSubresource(g_ctx, (ID3D11Resource *)g_cb_cl, 0, NULL, uvs, 0, 0);
        ID3D11DeviceContext_IASetInputLayout(g_ctx, NULL);
        ID3D11DeviceContext_IASetPrimitiveTopology(g_ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11DeviceContext_VSSetShader(g_ctx, g_vs_present, NULL, 0);
        ID3D11DeviceContext_VSSetConstantBuffers(g_ctx, 2, 1, &g_cb_cl);
        ID3D11DeviceContext_PSSetShader(g_ctx, g_ps_zres, NULL, 0);
        ID3D11DeviceContext_PSSetShaderResources(g_ctx, 7, 1, &s->msrv);
        ID3D11DeviceContext_Draw(g_ctx, 3, 0);
        ID3D11DeviceContext_PSSetShaderResources(g_ctx, 7, 1, &none);
        gpu_sh_reset();
    }
}
/* the multisampled copy made from the plain texture (written another way) */
static void gsurf_push(GSurf *s)
{
    if (!s || !s->mtex || !s->ss_new || s->is_depth) return;
    s->ss_new = 0;
    gpu_copy_scaled(s->srv, s->pw, s->ph, s->pw, s->ph, s->rtv, s->pw, s->ph);
}
/* a surface drawn without its multisampled copy from now on (its pair
 * has none) */
static void gsurf_ms_drop(GSurf *s)
{
    if (!s || !s->mtex) return;
    gsurf_resolve(s);
    if (s->is_depth) { if (s->dsv) ID3D11DepthStencilView_Release(s->dsv); s->dsv = s->dsv1; s->dsv1 = NULL; }
    else { if (s->rtv) ID3D11RenderTargetView_Release(s->rtv); s->rtv = s->rtv1; }
    if (s->msrv) ID3D11ShaderResourceView_Release(s->msrv);
    s->msrv = NULL;
    ID3D11Texture2D_Release(s->mtex);
    s->mtex = NULL; s->ms_new = s->ss_new = 0;
}

static GSurf *gsurf_find(uint32_t base, int depth)
{
    int i;
    for (i = 0; i < GSURF_MAX; i++)
        if (g_surf[i].tex && g_surf[i].base == base && (depth < 0 || (int)g_surf[i].is_depth == depth))
            return &g_surf[i];
    return NULL;
}

/* testing (RECOMP_SURF_LOG): does a texture lie inside a render target */
static int gsurf_near(uint32_t off)
{
    int i;
    for (i = 0; i < GSURF_MAX; i++)
        if (g_surf[i].tex && off >= g_surf[i].base && off < g_surf[i].base + g_surf[i].w * g_surf[i].h * 4)
            return 1;
    return 0;
}

static GSurf *gsurf_get(uint32_t base, uint32_t w, uint32_t h, int depth)
{
    GSurf *s = NULL, *victim = NULL;
    int i;
    D3D11_TEXTURE2D_DESC td;
    for (i = 0; i < GSURF_MAX; i++) {
        GSurf *c = &g_surf[i];
        /* zeta surfaces are also keyed by size: D3D needs the depth view the
         * size of the colour target, and the title pairs one zeta offset with
         * targets of different sizes (the 128x128 lava passes). */
        if (c->tex && c->base == base && (int)c->is_depth == depth && (!depth || (c->w == w && c->h == h))) { s = c; break; }
        if (!victim || (!c->tex && victim->tex) || (c->tex && victim->tex && c->used < victim->used)) victim = c;
    }
    ID3D11Texture2D *keep = NULL;     /* a colour surface that grows keeps what it holds */
    uint32_t keep_pw = 0, keep_ph = 0, keep_w = 0, keep_h = 0;
    if (s && s->w >= w && s->h >= h) { s->used = ++g_clock; return s; }
    if (s) {
        if (w < s->w) w = s->w;
        if (h < s->h) h = s->h;
        if (!depth && s->tex) {
            keep = s->tex; ID3D11Texture2D_AddRef(keep);
            keep_pw = s->pw; keep_ph = s->ph; keep_w = s->w; keep_h = s->h;
        }
        gsurf_free(s);
    }
    else { s = victim; gsurf_free(s); }

    memset(&td, 0, sizeof td);
    td.Width = RSX_(w, w, h); td.Height = RSY_(h, w, h); td.MipLevels = 1; td.ArraySize = 1;
    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
    if (depth) {
        td.Format = DXGI_FORMAT_R24G8_TYPELESS;
        td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    } else {
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    }
    if (FAILED(ID3D11Device_CreateTexture2D(g_dev, &td, NULL, &s->tex))) { memset(s, 0, sizeof *s); return NULL; }
    if (depth) {
        D3D11_DEPTH_STENCIL_VIEW_DESC dd;
        D3D11_SHADER_RESOURCE_VIEW_DESC sd;
        memset(&dd, 0, sizeof dd);
        dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        ID3D11Device_CreateDepthStencilView(g_dev, (ID3D11Resource *)s->tex, &dd, &s->dsv);
        memset(&sd, 0, sizeof sd);
        sd.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        ID3D11Device_CreateShaderResourceView(g_dev, (ID3D11Resource *)s->tex, &sd, &s->srv);
        {
            /* A zeta surface starts at the title's clear value, as the
             * software z-buffer does: titles with an inverted depth range
             * never clear depth and rely on it. */
            uint32_t zv = s_cb_scratch[0];
            ID3D11DeviceContext_ClearDepthStencilView(g_ctx, s->dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                                                      (float)(zv >> 8) / 16777215.0f, (UINT8)(zv & 0xFF));
        }
    } else {
        ID3D11Device_CreateRenderTargetView(g_dev, (ID3D11Resource *)s->tex, NULL, &s->rtv);
        ID3D11Device_CreateShaderResourceView(g_dev, (ID3D11Resource *)s->tex, NULL, &s->srv);
        {
            float z[4] = {0, 0, 0, 0};
            ID3D11DeviceContext_ClearRenderTargetView(g_ctx, s->rtv, z);
        }
        if (keep) {
            /* Grown (a later copy reaching further down, as the heat haze's
             * second patch does): the old contents carry over. Re-creating
             * it cleared the first patch to black -- the black boxes over
             * the lava. Only when both are at the same scale. */
            if (keep_pw * w == td.Width * keep_w && keep_ph * h == td.Height * keep_h) {
                D3D11_BOX b = { 0, 0, 0, keep_pw, keep_ph, 1 };
                ID3D11DeviceContext_CopySubresourceRegion(g_ctx, (ID3D11Resource *)s->tex, 0, 0, 0, 0,
                                                          (ID3D11Resource *)keep, 0, &b);
            }
            ID3D11Texture2D_Release(keep);
        }
    }
    s->base = base; s->w = w; s->h = h; s->is_depth = (uint32_t)depth; s->used = ++g_clock;
    s->pw = RSX_(w, w, h); s->ph = RSY_(h, w, h);
    if (!depth) s->rtv1 = s->rtv;   /* (the plain texture's view; another when multisampled) */
    if (g_msaa > 1) gsurf_ms_make(s, &td);
    if (getenv("RECOMP_SURF_LOG")) {
        static int n;
        if (n++ < 200)
            fprintf(stderr, "  [SURF] new %s %08X %ux%u (fmt %08X pitch %08X, clip %u,%u %ux%u)\n", depth ? "zeta" : "colour",
                    base, w, h, s_gpu.format, s_gpu.pitch, s_gpu.clip_x, s_gpu.clip_y, s_gpu.clip_w, s_gpu.clip_h);
    }
    s->mem_sum = 0;
    if (!depth) {
        /* Start from what the guest has there: a surface the CPU filled (or
         * the software path drew before the switch) keeps its picture. */
        const uint8_t *m = (const uint8_t *)xbox_GetMemoryOffset() + base;
        uint32_t pitch = s_gpu.pitch && (s_gpu.pitch & 0xFFFF) >= w * 4 ? (s_gpu.pitch & 0xFFFF) : w * 4;
        if (dma_resolve(s_gpu.color_offset) != base) pitch = w * 4;
        gsurf_upload(s, m, pitch);
        s->pitch = pitch;
        s->mem_sum = gpu_mem_sum(base, pitch * h);
        s->rw = ++g_wr_clock;
    }
    return s;
}

/* The GPU copy of a surface lives on the card; the guest's copy in memory
 * only changes when the CPU writes it -- Bink drawing a movie frame, a
 * loading screen. If memory has changed since the two were last in sync,
 * the CPU drew: take its picture. (v22/v23 kept showing the first frame of
 * the New Line Cinema logo while the movie played on.) */
static void gsurf_sync_from_mem(GSurf *s)
{
    uint32_t sum;
    if (!s || s->is_depth || !s->pitch) return;
    /* The CPU can only have written the surface between pushbuffer runs:
     * check once per run, not once per draw (a thousand draws a frame, each
     * reading a thousand scattered words of a 1 MB surface). */
    if (s->synced_run == g_pb_runs) return;
    s->synced_run = g_pb_runs;
    sum = gpu_mem_sum(s->base, s->pitch * s->h);
    if (sum == s->mem_sum) return;
    gsurf_upload(s, (const uint8_t *)xbox_GetMemoryOffset() + s->base, s->pitch);
    s->mem_sum = sum;
    s->rw = ++g_wr_clock;
}

/* ---------------- textures from guest memory ---------------- */

typedef struct {
    uint32_t off, color, w, h, pitch, ver, hash;
    ID3D11Texture2D *tex;
    ID3D11ShaderResourceView *srv;
    uint64_t used;
} GTex;
/* Content hashes of the textures drawn this frame and last frame (for the
 * title's overlays: "is the game's HUD on screen"). */
#define FHASH_MAX 512
static uint32_t g_fhash[2][FHASH_MAX];
static int g_fhash_n[2], g_fhash_cur;
static void fhash_note(uint32_t h)
{
    int c = g_fhash_cur, i, n = g_fhash_n[c];
    for (i = 0; i < n; i++) if (g_fhash[c][i] == h) return;
    if (n < FHASH_MAX) { g_fhash[c][n] = h; g_fhash_n[c] = n + 1; }
}
/* All of last frame's texture hashes (for finding what a screen is made of). */
int xbox_FrameTextureList(uint32_t *out, int max)
{
    int p = g_fhash_cur ^ 1, i, n = g_fhash_n[p] < max ? g_fhash_n[p] : max;
    for (i = 0; i < n; i++) out[i] = g_fhash[p][i];
    return n;
}
int xbox_FrameUsedTexture(uint32_t hash)
{
    int p = g_fhash_cur ^ 1, i;
    for (i = 0; i < g_fhash_n[p]; i++) if (g_fhash[p][i] == hash) return 1;
    return 0;
}
#define GTEX_MAX 512
static GTex g_tex[GTEX_MAX];

/* Ultra textures (nv2a_ultra.inc): the pack's pictures as BC7 textures,
 * all their levels, made when their file has been read */
static ID3D11ShaderResourceView **g_usrv;
static ID3D11Texture2D **g_utex;
static int s_ultra_used;     /* the last gtex_get gave a pack picture */
static void ultra_gpu_drop(int i)
{
    if (g_usrv[i]) ID3D11ShaderResourceView_Release(g_usrv[i]);
    if (g_utex[i]) ID3D11Texture2D_Release(g_utex[i]);
    g_usrv[i] = NULL; g_utex[i] = NULL;
    g_ultra_bytes -= g_uf[i].size;
    InterlockedExchange(&g_uf[i].state, 0);
}
static ID3D11ShaderResourceView *ultra_gpu_get(int idx)
{
    UltraFile *u = &g_uf[idx];
    if (!g_usrv) {
        g_usrv = (ID3D11ShaderResourceView **)calloc((size_t)g_uicap, sizeof *g_usrv);
        g_utex = (ID3D11Texture2D **)calloc((size_t)g_uicap, sizeof *g_utex);
        if (!g_usrv || !g_utex) return NULL;
    }
    if (u->state == 3 && g_usrv[idx]) { u->used = ++g_ultra_clock; return g_usrv[idx]; }
    if (u->state == 2) {
        D3D11_TEXTURE2D_DESC td;
        D3D11_SUBRESOURCE_DATA sd[16];
        uint32_t l, W = u->w, H = u->h, off = 0;
        memset(&td, 0, sizeof td);
        td.Width = u->w; td.Height = u->h; td.MipLevels = u->levels > 16 ? 16 : u->levels; td.ArraySize = 1;
        td.Format = (DXGI_FORMAT)98;   /* BC7_UNORM */
        td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        for (l = 0; l < td.MipLevels; l++) {
            sd[l].pSysMem = u->data + off; sd[l].SysMemPitch = ((W + 3) / 4) * 16; sd[l].SysMemSlicePitch = 0;
            off += ((W + 3) / 4) * ((H + 3) / 4) * 16;
            W = W > 1 ? W / 2 : 1; H = H > 1 ? H / 2 : 1;
        }
        if (FAILED(ID3D11Device_CreateTexture2D(g_dev, &td, sd, &g_utex[idx]))
            || FAILED(ID3D11Device_CreateShaderResourceView(g_dev, (ID3D11Resource *)g_utex[idx], NULL, &g_usrv[idx]))) {
            static int said;
            if (!said++) fprintf(stderr, "  [ULTRA] the graphics card took no BC7 texture: Ultra textures off\n");
            if (g_utex[idx]) ID3D11Texture2D_Release(g_utex[idx]);
            g_utex[idx] = NULL; g_usrv[idx] = NULL;
            free(u->data); u->data = NULL;
            InterlockedExchange(&u->state, -1);
            return NULL;
        }
        free(u->data); u->data = NULL;
        g_ultra_bytes += u->size;
        u->used = ++g_ultra_clock;
        InterlockedExchange(&u->state, 3);
        while (g_ultra_bytes > ultra_budget()) {   /* over the budget: the least used go */
            int i, old = -1;
            for (i = 0; i < g_uin; i++)
                if (i != idx && g_uf[i].state == 3 && g_usrv[i] && (old < 0 || g_uf[i].used < g_uf[old].used)) old = i;
            if (old < 0) break;
            ultra_gpu_drop(old);
        }
        return g_usrv[idx];
    }
    if (u->state == 0) ultra_request(idx);
    return NULL;
}

static ID3D11ShaderResourceView *gtex_get(const Texture *t)
{
    uint32_t ver = 0;
    const uint32_t *argb;
    GTex *e = NULL, *victim = NULL;
    int i;
    s_ultra_used = 0;
    argb = texc_get_ver(t, &ver);
    if (!argb) return NULL;
    fhash_note(s_texc_last_hash);
    if (s_texc_last_uidx >= 0 && g_ultra_on == 1 && ultra_sky_check(s_texc_last_uidx, t, s_3d.depth_test, s_3d.depth_mask)) {   /* Ultra textures: the pack's picture, once it is read */
        ID3D11ShaderResourceView *hd = ultra_gpu_get(s_texc_last_uidx);
        if (getenv("RECOMP_ULTRA_STATELOG")) {   /* testing: the draw state of each pack picture's first uses */
                static uint32_t seen[2048]; static int ns; int q; uint32_t key = s_texc_last_hash ^ (t->addr_u << 4) ^ (t->addr_v << 8) ^ ((uint32_t)s_3d.depth_test << 12) ^ ((uint32_t)s_3d.depth_mask << 13);
                for (q = 0; q < ns && seen[q] != key; q++) {}
                if (q == ns && ns < 2048) { seen[ns++] = key; fprintf(stderr, "  [USTATE] %08X %ux%u addr %u %u ztest %d zwrite %d %s\n", s_texc_last_hash, t->width, t->height, t->addr_u, t->addr_v, s_3d.depth_test, s_3d.depth_mask, g_ui[s_texc_last_uidx].path); }
            }
        if (hd) {
            s_ultra_used = 1;
            return hd;
        }
    }
    for (i = 0; i < GTEX_MAX; i++) {
        GTex *c = &g_tex[i];
        if (c->tex && c->off == t->offset && c->color == t->color && c->w == t->width && c->h == t->height
            && c->pitch == t->pitch) { e = c; break; }
        if (!victim || (!c->tex && victim->tex) || (c->tex && victim->tex && c->used < victim->used)) victim = c;
    }
    if (!e) {
        D3D11_TEXTURE2D_DESC td;
        e = victim;
        if (e->srv) ID3D11ShaderResourceView_Release(e->srv);
        if (e->tex) ID3D11Texture2D_Release(e->tex);
        memset(e, 0, sizeof *e);
        memset(&td, 0, sizeof td);
        gpu_filter_opts();
        td.Width = t->width; td.Height = t->height; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        /* A full mip chain the GPU builds from the top level (the title's
         * own mip levels are not decoded), for stages that filter with
         * mipmaps -- less shimmer at a distance, and what anisotropic
         * filtering works from. */
        if (g_mips && t->width > 1 && t->height > 1 && tex_size_from_format(t->color)) {
            td.MipLevels = 0;
            td.BindFlags |= D3D11_BIND_RENDER_TARGET;
            td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        }
        if (FAILED(ID3D11Device_CreateTexture2D(g_dev, &td, NULL, &e->tex))) { memset(e, 0, sizeof *e); return NULL; }
        ID3D11Device_CreateShaderResourceView(g_dev, (ID3D11Resource *)e->tex, NULL, &e->srv);
        e->off = t->offset; e->color = t->color; e->w = t->width; e->h = t->height; e->pitch = t->pitch;
        e->ver = ver - 1;
    }
    if (e->ver != ver) {
        D3D11_TEXTURE2D_DESC cd;
        ID3D11Texture2D_GetDesc(e->tex, &cd);
        ID3D11DeviceContext_UpdateSubresource(g_ctx, (ID3D11Resource *)e->tex, 0, NULL, argb, t->width * 4, 0);
        if (cd.MipLevels > 1) ID3D11DeviceContext_GenerateMips(g_ctx, e->srv);
        e->ver = ver;
    }
    e->used = ++g_clock;
    return e->srv;
}

/* ---------------- state objects (small caches) ---------------- */

typedef struct { uint64_t key[3]; void *obj; } GState;
#define GSTATE_MAX 128
static GState g_blend_c[GSTATE_MAX], g_ds_c[GSTATE_MAX], g_rs_c[GSTATE_MAX], g_smp_c[GSTATE_MAX];
static void *gstate_find(GState *c, const uint64_t k[3])
{
    int i;
    for (i = 0; i < GSTATE_MAX && c[i].obj; i++)
        if (!memcmp(c[i].key, k, sizeof c[i].key)) return c[i].obj;
    return NULL;
}
static void gstate_put(GState *c, const uint64_t k[3], void *o)
{
    int i;
    for (i = 0; i < GSTATE_MAX; i++)
        if (!c[i].obj) { memcpy(c[i].key, k, sizeof c[i].key); c[i].obj = o; return; }
    /* full: leak-free fallback, just reuse the last slot's key (object stays referenced) */
}

static D3D11_BLEND gl_blend(uint32_t f, int alpha)
{
    switch (f) {
    case 0x0000: return D3D11_BLEND_ZERO;
    case 0x0001: return D3D11_BLEND_ONE;
    case 0x0300: return alpha ? D3D11_BLEND_SRC_ALPHA : D3D11_BLEND_SRC_COLOR;
    case 0x0301: return alpha ? D3D11_BLEND_INV_SRC_ALPHA : D3D11_BLEND_INV_SRC_COLOR;
    case 0x0302: return D3D11_BLEND_SRC_ALPHA;
    case 0x0303: return D3D11_BLEND_INV_SRC_ALPHA;
    case 0x0304: return D3D11_BLEND_DEST_ALPHA;
    case 0x0305: return D3D11_BLEND_INV_DEST_ALPHA;
    case 0x0306: return alpha ? D3D11_BLEND_DEST_ALPHA : D3D11_BLEND_DEST_COLOR;
    case 0x0307: return alpha ? D3D11_BLEND_INV_DEST_ALPHA : D3D11_BLEND_INV_DEST_COLOR;
    case 0x0308: return D3D11_BLEND_SRC_ALPHA_SAT;
    case 0x8001: case 0x8003: return D3D11_BLEND_BLEND_FACTOR;
    case 0x8002: case 0x8004: return D3D11_BLEND_INV_BLEND_FACTOR;
    default:     return D3D11_BLEND_ONE;
    }
}
static D3D11_BLEND_OP gl_blend_op(uint32_t e)
{
    switch (e) {
    case 0x800A: return D3D11_BLEND_OP_SUBTRACT;
    case 0x800B: return D3D11_BLEND_OP_REV_SUBTRACT;
    case 0x8007: return D3D11_BLEND_OP_MIN;
    case 0x8008: return D3D11_BLEND_OP_MAX;
    default:     return D3D11_BLEND_OP_ADD;
    }
}
static D3D11_COMPARISON_FUNC gl_cmp(uint32_t f)
{
    return (D3D11_COMPARISON_FUNC)((f & 0xF) + 1);   /* GL 0x200+n -> D3D n+1 */
}
static D3D11_STENCIL_OP gl_sop(uint32_t op)
{
    switch (op) {
    case 0x0000: return D3D11_STENCIL_OP_ZERO;
    case 0x1E01: return D3D11_STENCIL_OP_REPLACE;
    case 0x1E02: return D3D11_STENCIL_OP_INCR_SAT;
    case 0x1E03: return D3D11_STENCIL_OP_DECR_SAT;
    case 0x150A: return D3D11_STENCIL_OP_INVERT;
    case 0x8507: return D3D11_STENCIL_OP_INCR;
    case 0x8508: return D3D11_STENCIL_OP_DECR;
    default:     return D3D11_STENCIL_OP_KEEP;
    }
}

/* Shadow of what is bound on the context, so a draw only sends the state
 * that changed: a frame is a thousand draws, and re-sending every target,
 * viewport, constant buffer and state object each time is most of the CPU
 * cost of the renderer. Anything that binds behind the draw path's back
 * (clear, present, debug marks) calls gpu_sh_reset. */
static struct {
    ID3D11RenderTargetView *rtv; ID3D11DepthStencilView *dsv; int tg;
    float vw, vh; D3D11_RECT sc; int sc_ok;
    ID3D11BlendState *bs; float bf[4]; int bs_ok;
    ID3D11DepthStencilState *ds; UINT sref; int ds_ok;
    ID3D11RasterizerState *rs;
    ID3D11ShaderResourceView *srv[4]; ID3D11SamplerState *smp[4]; int tex_ok;
    float cbvs[4]; int cbvs_ok;
    unsigned char cbps[1152]; int cbps_ok;     /* the PS constants: 8 stages x 7 + 16 float4 */
    int pipe;
} g_sh;
static void gpu_sh_reset(void) { memset(&g_sh, 0, sizeof g_sh); }

static void gpu_set_blend(int write_mask_only_clear)
{
    uint64_t k[3];
    ID3D11BlendState *bs;
    uint32_t m = s_3d.color_mask;
    UINT8 wm = (UINT8)(((m & 0x00010000u) ? 1 : 0) | ((m & 0x00000100u) ? 2 : 0)
                     | ((m & 0x00000001u) ? 4 : 0) | ((m & 0x01000000u) ? 8 : 0));
    int en = s_gpu.blend_enable && !write_mask_only_clear;
    k[0] = ((uint64_t)en << 63) | ((uint64_t)wm << 48) | ((uint64_t)s_3d.blend_eq << 32)
         | ((uint64_t)(s_gpu.blend_sfactor & 0xFFFF) << 16) | (s_gpu.blend_dfactor & 0xFFFF);
    k[1] = 0; k[2] = 0;
    bs = (ID3D11BlendState *)gstate_find(g_blend_c, k);
    if (!bs) {
        D3D11_BLEND_DESC d;
        memset(&d, 0, sizeof d);
        d.RenderTarget[0].BlendEnable = en;
        d.RenderTarget[0].SrcBlend = gl_blend(s_gpu.blend_sfactor, 0);
        d.RenderTarget[0].DestBlend = gl_blend(s_gpu.blend_dfactor, 0);
        d.RenderTarget[0].BlendOp = gl_blend_op(s_3d.blend_eq);
        d.RenderTarget[0].SrcBlendAlpha = gl_blend(s_gpu.blend_sfactor, 1);
        d.RenderTarget[0].DestBlendAlpha = gl_blend(s_gpu.blend_dfactor, 1);
        d.RenderTarget[0].BlendOpAlpha = gl_blend_op(s_3d.blend_eq);
        d.RenderTarget[0].RenderTargetWriteMask = wm;
        if (FAILED(ID3D11Device_CreateBlendState(g_dev, &d, &bs))) return;
        gstate_put(g_blend_c, k, bs);
    }
    {
        float bf[4];
        uint32_t c = s_3d.blend_color;
        int alpha_const = s_gpu.blend_sfactor == 0x8003 || s_gpu.blend_sfactor == 0x8004
                       || s_gpu.blend_dfactor == 0x8003 || s_gpu.blend_dfactor == 0x8004;
        bf[0] = ((c >> 16) & 0xFF) / 255.0f; bf[1] = ((c >> 8) & 0xFF) / 255.0f;
        bf[2] = (c & 0xFF) / 255.0f; bf[3] = (c >> 24) / 255.0f;
        if (alpha_const) bf[0] = bf[1] = bf[2] = bf[3];
        if (g_sh.bs_ok && g_sh.bs == bs && !memcmp(g_sh.bf, bf, sizeof bf)) return;
        g_sh.bs = bs; memcpy(g_sh.bf, bf, sizeof bf); g_sh.bs_ok = 1;
        ID3D11DeviceContext_OMSetBlendState(g_ctx, bs, bf, 0xFFFFFFFFu);
    }
}

static void gpu_set_depth(int have_dsv)
{
    uint64_t k[3];
    ID3D11DepthStencilState *ds;
    int dt = have_dsv && s_3d.depth_test, st = have_dsv && s_3d.stencil_test;
    k[0] = ((uint64_t)dt << 63) | ((uint64_t)(dt && s_3d.depth_mask) << 62) | ((uint64_t)st << 61)
         | ((uint64_t)(s_3d.depth_func & 0xF) << 48) | ((uint64_t)(s_3d.st_func & 0xF) << 40)
         | ((uint64_t)(s_3d.st_mask & 0xFF) << 32) | ((uint64_t)(s_3d.st_wmask & 0xFF) << 24);
    k[1] = ((uint64_t)s_3d.st_fail << 32) | s_3d.st_zfail;
    k[2] = s_3d.st_zpass;
    ds = (ID3D11DepthStencilState *)gstate_find(g_ds_c, k);
    if (!ds) {
        D3D11_DEPTH_STENCIL_DESC d;
        memset(&d, 0, sizeof d);
        d.DepthEnable = dt;
        d.DepthWriteMask = (dt && s_3d.depth_mask) ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
        d.DepthFunc = gl_cmp(s_3d.depth_func);
        d.StencilEnable = st;
        d.StencilReadMask = (UINT8)s_3d.st_mask;
        d.StencilWriteMask = (UINT8)s_3d.st_wmask;
        d.FrontFace.StencilFailOp = gl_sop(s_3d.st_fail);
        d.FrontFace.StencilDepthFailOp = gl_sop(s_3d.st_zfail);
        d.FrontFace.StencilPassOp = gl_sop(s_3d.st_zpass);
        d.FrontFace.StencilFunc = gl_cmp(s_3d.st_func);
        d.BackFace = d.FrontFace;
        if (FAILED(ID3D11Device_CreateDepthStencilState(g_dev, &d, &ds))) return;
        gstate_put(g_ds_c, k, ds);
    }
    if (g_sh.ds_ok && g_sh.ds == ds && g_sh.sref == (s_3d.st_ref & 0xFF)) return;
    g_sh.ds = ds; g_sh.sref = s_3d.st_ref & 0xFF; g_sh.ds_ok = 1;
    ID3D11DeviceContext_OMSetDepthStencilState(g_ctx, ds, s_3d.st_ref & 0xFF);
}

static int gpu_set_raster(void)
{
    uint64_t k[3];
    ID3D11RasterizerState *rs;
    D3D11_CULL_MODE cm = D3D11_CULL_NONE;
    if (s_3d.cull_enable && !getenv("RECOMP_GPU_NOCULL")) {
        if (s_3d.cull_face == 0x408) return 0;        /* front and back: nothing drawn */
        cm = s_3d.cull_face == 0x404 ? D3D11_CULL_FRONT : D3D11_CULL_BACK;
    }
    k[0] = ((uint64_t)cm << 8) | (s_3d.front_face == 0x900 ? 0 : 1);
    k[1] = k[2] = 0;
    rs = (ID3D11RasterizerState *)gstate_find(g_rs_c, k);
    if (!rs) {
        D3D11_RASTERIZER_DESC d;
        memset(&d, 0, sizeof d);
        d.FillMode = D3D11_FILL_SOLID;
        d.CullMode = cm;
        /* Window y grows down in both, so "clockwise" means the same thing
         * to the NV2A and to D3D's rasteriser. */
        d.FrontCounterClockwise = (s_3d.front_face != 0x900) ^ (getenv("RECOMP_GPU_CULLFLIP") != NULL);
        d.DepthClipEnable = getenv("RECOMP_GPU_NOZCLIP") ? FALSE : TRUE;   /* NV2A clips z at SET_CLIP_MIN/MAX */
        d.ScissorEnable = TRUE;
        if (FAILED(ID3D11Device_CreateRasterizerState(g_dev, &d, &rs))) return 0;
        gstate_put(g_rs_c, k, rs);
    }
    if (g_sh.rs != rs) { g_sh.rs = rs; ID3D11DeviceContext_RSSetState(g_ctx, rs); }
    return 1;
}

static ID3D11SamplerState *gpu_sampler_ex(uint32_t au, uint32_t av, int linear, int mip)
{
    uint64_t k[3];
    ID3D11SamplerState *s;
    gpu_filter_opts();
    if (!g_mips) mip = 0;
    k[0] = ((uint64_t)au << 16) | ((uint64_t)av << 8) | (uint64_t)linear | ((uint64_t)mip << 32);
    k[1] = k[2] = 0;
    s = (ID3D11SamplerState *)gstate_find(g_smp_c, k);
    if (!s) {
        D3D11_SAMPLER_DESC d;
        D3D11_TEXTURE_ADDRESS_MODE m[2];
        uint32_t a[2] = { au, av };
        int i;
        for (i = 0; i < 2; i++)
            m[i] = a[i] == 1 ? D3D11_TEXTURE_ADDRESS_WRAP : a[i] == 2 ? D3D11_TEXTURE_ADDRESS_MIRROR
                 : a[i] == 4 ? D3D11_TEXTURE_ADDRESS_BORDER : D3D11_TEXTURE_ADDRESS_CLAMP;
        memset(&d, 0, sizeof d);
        d.Filter = linear ? D3D11_FILTER_MIN_MAG_MIP_LINEAR : D3D11_FILTER_MIN_MAG_MIP_POINT;
        if (linear && mip && g_aniso > 1) { d.Filter = D3D11_FILTER_ANISOTROPIC; d.MaxAnisotropy = g_aniso; }
        d.AddressU = m[0]; d.AddressV = m[1]; d.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        d.MaxLOD = mip ? D3D11_FLOAT32_MAX : 0.0f;
        d.ComparisonFunc = D3D11_COMPARISON_NEVER;
        if (FAILED(ID3D11Device_CreateSamplerState(g_dev, &d, &s))) return NULL;
        gstate_put(g_smp_c, k, s);
    }
    return s;
}

static ID3D11SamplerState *gpu_sampler(uint32_t au, uint32_t av, int linear)
{
    return gpu_sampler_ex(au, av, linear, 0);
}

/* ---------------- init ---------------- */

static ID3DBlob *gpu_compile(const char *entry, const char *target)
{
    ID3DBlob *b = NULL, *err = NULL;
    HRESULT hr = D3DCompile(s_hlsl_src, sizeof s_hlsl_src - 1, "nv2a", NULL, NULL, entry, target,
                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &b, &err);
    if (FAILED(hr)) {
        fprintf(stderr, "  [D3D11] shader %s failed: %s\n", entry,
                err ? (const char *)ID3D10Blob_GetBufferPointer(err) : "?");
        if (err) ID3D10Blob_Release(err);
        return NULL;
    }
    if (err) ID3D10Blob_Release(err);
    return b;
}

static ID3D11Buffer *gpu_cbuf(UINT size)
{
    D3D11_BUFFER_DESC d;
    ID3D11Buffer *b = NULL;
    memset(&d, 0, sizeof d);
    d.ByteWidth = (size + 15) & ~15u; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ID3D11Device_CreateBuffer(g_dev, &d, NULL, &b);
    return b;
}

static int gpu_init(void)
{
    D3D_FEATURE_LEVEL fl;
    ID3DBlob *b;
    const char *e = getenv("RECOMP_RENDERER");
    if (e && !_stricmp(e, "software")) return 0;
    if (!d3dcompiler_load()) return 0;
    {
        /* Feature level 11_0 exactly: the shaders are shader model 5. Asking
         * for 11_1 fails outright on a Windows 7 without the platform update,
         * and a 10_x card (older Intel graphics) would make the device and
         * then fail every shader. */
        static const D3D_FEATURE_LEVEL want[1] = { D3D_FEATURE_LEVEL_11_0 };
        HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, want, 1, D3D11_SDK_VERSION,
                                       &g_dev, &fl, &g_ctx);
        if (FAILED(hr)) {
            fprintf(stderr, "  [D3D11] no Direct3D 11 (feature level 11_0) graphics card or driver (0x%08lX); "
                            "using the software renderer\n", (unsigned long)hr);
            g_dev = NULL; g_ctx = NULL;
            return 0;
        }
    }
    {
        /* which card, for reports */
        IDXGIDevice *dd = NULL; IDXGIAdapter *ad = NULL; DXGI_ADAPTER_DESC ds;
        if (SUCCEEDED(ID3D11Device_QueryInterface(g_dev, &IID_IDXGIDevice, (void **)&dd)) && dd) {
            if (SUCCEEDED(IDXGIDevice_GetAdapter(dd, &ad)) && ad) {
                if (SUCCEEDED(IDXGIAdapter_GetDesc(ad, &ds)))
                    fprintf(stderr, "  [D3D11] graphics: %ls (vendor %04X device %04X, %lu MB)\n", ds.Description,
                            ds.VendorId, ds.DeviceId, (unsigned long)(ds.DedicatedVideoMemory >> 20));
                IDXGIAdapter_Release(ad);
            }
            IDXGIDevice_Release(dd);
        }
    }
    if (!(b = gpu_compile("vs_main", "vs_5_0"))) return 0;
    ID3D11Device_CreateVertexShader(g_dev, ID3D10Blob_GetBufferPointer(b), ID3D10Blob_GetBufferSize(b), NULL, &g_vs);
    {
        D3D11_INPUT_ELEMENT_DESC il[8] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,   D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 64,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 80,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 96,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 112, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        if (FAILED(ID3D11Device_CreateInputLayout(g_dev, il, 8, ID3D10Blob_GetBufferPointer(b),
                                                  ID3D10Blob_GetBufferSize(b), &g_il))) return 0;
    }
    ID3D10Blob_Release(b);
    if (!(b = gpu_compile("ps_main", "ps_5_0"))) return 0;
    ID3D11Device_CreatePixelShader(g_dev, ID3D10Blob_GetBufferPointer(b), ID3D10Blob_GetBufferSize(b), NULL, &g_ps);
    ID3D10Blob_Release(b);
    if (!(b = gpu_compile("vs_quad", "vs_5_0"))) return 0;
    ID3D11Device_CreateVertexShader(g_dev, ID3D10Blob_GetBufferPointer(b), ID3D10Blob_GetBufferSize(b), NULL, &g_vs_quad);
    ID3D10Blob_Release(b);
    if (!(b = gpu_compile("ps_clear", "ps_5_0"))) return 0;
    ID3D11Device_CreatePixelShader(g_dev, ID3D10Blob_GetBufferPointer(b), ID3D10Blob_GetBufferSize(b), NULL, &g_ps_clear);
    ID3D10Blob_Release(b);
    if (!(b = gpu_compile("vs_present", "vs_5_0"))) return 0;
    ID3D11Device_CreateVertexShader(g_dev, ID3D10Blob_GetBufferPointer(b), ID3D10Blob_GetBufferSize(b), NULL, &g_vs_present);
    ID3D10Blob_Release(b);
    if (!(b = gpu_compile("ps_present", "ps_5_0"))) return 0;
    ID3D11Device_CreatePixelShader(g_dev, ID3D10Blob_GetBufferPointer(b), ID3D10Blob_GetBufferSize(b), NULL, &g_ps_present);
    ID3D10Blob_Release(b);
    if ((b = gpu_compile("ps_fxaa", "ps_5_0"))) {
        ID3D11Device_CreatePixelShader(g_dev, ID3D10Blob_GetBufferPointer(b), ID3D10Blob_GetBufferSize(b), NULL, &g_ps_fxaa);
        ID3D10Blob_Release(b);
    }
    if ((b = gpu_compile("vs_blit", "vs_5_0"))) {
        ID3D11Device_CreateVertexShader(g_dev, ID3D10Blob_GetBufferPointer(b), ID3D10Blob_GetBufferSize(b), NULL, &g_vs_blit);
        ID3D10Blob_Release(b);
    }
    if (!(b = gpu_compile("ps_copy", "ps_5_0"))) return 0;
    ID3D11Device_CreatePixelShader(g_dev, ID3D10Blob_GetBufferPointer(b), ID3D10Blob_GetBufferSize(b), NULL, &g_ps_copy);
    ID3D10Blob_Release(b);
    if ((b = gpu_compile("ps_zres", "ps_5_0"))) {
        ID3D11Device_CreatePixelShader(g_dev, ID3D10Blob_GetBufferPointer(b), ID3D10Blob_GetBufferSize(b), NULL, &g_ps_zres);
        ID3D10Blob_Release(b);
    }
    {
        const char *rs = getenv("RECOMP_RENDER_SCALE");
        g_msaa = xbox_GfxMsaa();
        if (g_msaa > 1) fprintf(stderr, "  [D3D11] MSAA %dx\n", g_msaa);
        g_rsf = rs ? (float)atof(rs) : 1.0f;
        if (!(g_rsf >= 1.0f)) g_rsf = 1.0f;
        if (g_rsf > 12.0f) g_rsf = 12.0f;
        g_rs = (int)g_rsf;
        {
            const char *as = getenv("RECOMP_ASPECT");
            float a = as ? (float)atof(as) : 0.0f;
            if (a > 1.34f && a < 6.0f) g_aspect = a;
        }
        g_rsx = g_rsf * (g_aspect / (4.0f / 3.0f));
        g_rsy = g_rsf;
        rs_fit();
        if (g_rsf > 1.0f || g_aspect > 1.34f)
            fprintf(stderr, "  [D3D11] rendering at %.2f x %.2f the original resolution, picture %.3f:1\n",
                    g_rsx, g_rsy, g_aspect);
    }
    {
        const char *e = getenv("RECOMP_HW_VP");
        g_hwvp_user = !(e && e[0] == '0');
    }
    g_cb_vs = gpu_cbuf(16);
    g_cb_ps = gpu_cbuf(sizeof(float) * 4 * (8 * 7 + 3 + 4 + 2 + 4 + 1 + 2));
    g_cb_cl = gpu_cbuf(16);
    g_cb_post = gpu_cbuf(16);
    g_post[0] = xbox_GfxGetF(GFX_BRIGHT); g_post[1] = xbox_GfxGetF(GFX_SHARP); g_post[2] = xbox_GfxGetF(GFX_COLOR);
    if (g_post[0] < 0.5f) g_post[0] = 1.0f;
    if (g_post[2] <= 0.0f) g_post[2] = 1.0f;
    if (g_cb_post) ID3D11DeviceContext_UpdateSubresource(g_ctx, (ID3D11Resource *)g_cb_post, 0, NULL, g_post, 0, 0);
    g_present_smp = gpu_sampler(3, 3, 1);
    g_copy_smp = g_present_smp;
    fprintf(stderr, "  [D3D11] renderer up (feature level %x)\n", (unsigned)fl);
    fflush(stderr);
    return 1;
}

static int gpu_on(void)
{
    if (s_gpu_mode < 0) {
        s_gpu_mode = gpu_init();
        if (s_gpu_mode) xbox_FramebufferSetGpuOwned(1);
    }
    return s_gpu_mode;
}

/* ---------------- drawing ---------------- */

typedef struct { float p[4], d0[4], d1[4], fog[4], t[4][4]; } GVert;
static GVert *g_verts;
static uint32_t *g_idx, g_verts_cap, g_idx_cap;
static uint32_t *g_slot, *g_slot_gen;
static uint32_t g_gen = 1;

/* Dynamic vertex/index data goes into one large ring per buffer: append
 * with MAP_NO_OVERWRITE, and only when the ring is full start over with
 * MAP_DISCARD. A DISCARD per draw (hundreds a frame) makes the driver rename
 * the buffer every time. Returns the byte offset the data landed at, or
 * 0xFFFFFFFF on failure. */
static UINT g_vb_pos, g_ib_pos;
static UINT gpu_upload(ID3D11Buffer **buf, UINT *cap, UINT *pos, UINT bind, const void *data, UINT bytes, UINT align)
{
    D3D11_MAPPED_SUBRESOURCE ms;
    D3D11_MAP mode = D3D11_MAP_WRITE_NO_OVERWRITE;
    UINT at;
    if (!*buf || *cap < bytes) {
        D3D11_BUFFER_DESC d;
        if (*buf) ID3D11Buffer_Release(*buf);
        *cap = bytes < (8u << 20) ? (8u << 20) : bytes * 2;
        memset(&d, 0, sizeof d);
        d.ByteWidth = *cap; d.Usage = D3D11_USAGE_DYNAMIC; d.BindFlags = bind;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        *buf = NULL;
        if (FAILED(ID3D11Device_CreateBuffer(g_dev, &d, NULL, buf))) { *cap = 0; return 0xFFFFFFFFu; }
        g_sh.pipe = 0;                     /* a new buffer must be bound */
        *pos = *cap;                       /* force a DISCARD on first use */
    }
    static int per_draw = -1;
    if (per_draw < 0) per_draw = getenv("RECOMP_GPU_DISCARD") != NULL;   /* wined3d maps NO_OVERWRITE slowly */
    at = (*pos + align - 1) / align * align;
    if (at + bytes > *cap || per_draw) { at = 0; mode = D3D11_MAP_WRITE_DISCARD; }
    if (FAILED(ID3D11DeviceContext_Map(g_ctx, (ID3D11Resource *)*buf, 0, mode, 0, &ms)))
        return 0xFFFFFFFFu;
    memcpy((char *)ms.pData + at, data, bytes);
    ID3D11DeviceContext_Unmap(g_ctx, (ID3D11Resource *)*buf, 0);
    *pos = at + bytes;
    return at;
}

/* The colour and zeta targets the title has bound, made current. */
static GSurf *g_cur_rt, *g_cur_ds;
static int gpu_bind_targets(int need_depth)
{
    uint32_t base = dma_resolve(s_gpu.color_offset);
    uint32_t bpp = surface_bpp();
    uint32_t w, h;
    D3D11_VIEWPORT vp;
    D3D11_RECT sc;
    if (!s_gpu.color_offset || !s_gpu.clip_w || !s_gpu.clip_h) return 0;
    w = (bpp && (s_gpu.pitch & 0xFFFF) / (bpp ? bpp : 4) >= s_gpu.clip_x + s_gpu.clip_w)
        ? (s_gpu.pitch & 0xFFFF) / bpp : s_gpu.clip_x + s_gpu.clip_w;
    if (((s_gpu.format >> 8) & 0xF) == 2) w = s_gpu.clip_x + s_gpu.clip_w;   /* swizzled: no pitch */
    h = s_gpu.clip_y + s_gpu.clip_h;
    g_cur_rt = gsurf_get(base, w, h, 0);
    if (!g_cur_rt) return 0;
    g_cur_rt->wr = ++g_wr_clock;
    gsurf_sync_from_mem(g_cur_rt);
    g_cur_ds = NULL;
    if (need_depth) {
        /* No zeta offset seen yet (a replayed capture starts mid-stream):
         * a private zeta for this colour target. */
        uint32_t zb = s_zeta_offset ? dma_resolve(s_zeta_offset) : (base | 1u);
        g_cur_ds = gsurf_get(zb, g_cur_rt->w, g_cur_rt->h, 1);
        if (g_cur_ds) g_cur_ds->wr = ++g_wr_clock;
    }
    if (g_cur_ds && !g_cur_rt->mtex != !g_cur_ds->mtex) gsurf_ms_drop(g_cur_rt->mtex ? g_cur_rt : g_cur_ds);   /* (MSAA: both or neither) */
    if (g_cur_rt->mtex) {
        gsurf_push(g_cur_rt);
        g_cur_rt->ms_new = 1;
        if (g_cur_ds) g_cur_ds->ms_new = 1;
    }
    if (!g_sh.tg || g_sh.rtv != g_cur_rt->rtv || g_sh.dsv != (g_cur_ds ? g_cur_ds->dsv : NULL)) {
        g_sh.rtv = g_cur_rt->rtv; g_sh.dsv = g_cur_ds ? g_cur_ds->dsv : NULL; g_sh.tg = 1;
        ID3D11DeviceContext_OMSetRenderTargets(g_ctx, 1, &g_cur_rt->rtv, g_sh.dsv);
        g_sh.tex_ok = 0;   /* D3D unbinds any texture that is now a target */
    }
    if (g_sh.vw != (float)g_cur_rt->pw || g_sh.vh != (float)g_cur_rt->ph) {
        vp.TopLeftX = 0; vp.TopLeftY = 0; vp.Width = (float)g_cur_rt->pw; vp.Height = (float)g_cur_rt->ph;
        vp.MinDepth = 0; vp.MaxDepth = 1;
        ID3D11DeviceContext_RSSetViewports(g_ctx, 1, &vp);
        g_sh.vw = vp.Width; g_sh.vh = vp.Height;
    }
    sc.left = (LONG)SX(g_cur_rt, s_gpu.clip_x); sc.top = (LONG)SY(g_cur_rt, s_gpu.clip_y);
    sc.right = (LONG)SX(g_cur_rt, s_gpu.clip_x + s_gpu.clip_w); sc.bottom = (LONG)SY(g_cur_rt, s_gpu.clip_y + s_gpu.clip_h);
    if (!g_sh.sc_ok || memcmp(&g_sh.sc, &sc, sizeof sc)) {
        ID3D11DeviceContext_RSSetScissorRects(g_ctx, 1, &sc);
        g_sh.sc = sc; g_sh.sc_ok = 1;
    }
    {
        float c[4];
        /* Window z goes to D3D depth as z / zmax, and the D3D rasteriser
         * clips 0 <= z <= w exactly where the NV2A clips at SET_CLIP_MIN/MAX
         * (the title sets 0 and the depth format's maximum). */
        float zmax = (((s_gpu.format >> 4) & 0xF) == 1) ? 65535.0f : 16777215.0f;
        if (s_clip_max > 0.0f && !getenv("RECOMP_GPU_ZFORMAT")) zmax = s_clip_max;
        c[0] = 2.0f / (float)g_cur_rt->w; c[1] = 2.0f / (float)g_cur_rt->h; c[2] = 1.0f / zmax; c[3] = 0;
        if (!g_sh.cbvs_ok || memcmp(g_sh.cbvs, c, sizeof c)) {
            ID3D11DeviceContext_UpdateSubresource(g_ctx, (ID3D11Resource *)g_cb_vs, 0, NULL, c, 0, 0);
            memcpy(g_sh.cbvs, c, sizeof c); g_sh.cbvs_ok = 1;
        }
    }
    return 1;
}

/* A surface the GPU did not draw but the CPU did (a movie frame written into
 * guest memory, a loading screen) is re-adopted from memory. */
static GSurf *gpu_surface_for_texture(uint32_t off, uint32_t tw, uint32_t th, int *is_depth)
{
    /* One piece of memory can be both: the shadow pass draws a 256x256
     * colour target, then uses the same memory as the zeta of the next pass
     * and samples it as a texture -- reading the depth it just wrote (as
     * A8R8G8B8: the shadow's silhouette). The one drawn into last is what
     * memory holds. */
    GSurf *s = NULL;
    int i;
    /* Which was last WRITTEN, not last bound: a pass that binds this memory
     * as its zeta with depth writes off (or as a colour target it then only
     * clears the depth of) leaves what the memory holds alone. Choosing by
     * binding sampled the wrong one of the two: the character shadows came
     * out as broken wedges and stripes much of the time. */
    static int old = -1;
    if (old < 0) old = getenv("RECOMP_SURF_OLDWR") ? 1 : 0;
    for (i = 0; i < GSURF_MAX; i++)
        if (g_surf[i].tex && g_surf[i].base == off) {
            GSurf *c = &g_surf[i];
            /* the main depth buffer (640x480) and the shadow pass's zeta
             * (256x256) share one address: a texture of the one's size reads
             * that one, whichever was written last */
            int cm = tw && c->w == tw && c->h == th, sm = s && tw && s->w == tw && s->h == th;
            if (!s) s = c;
            else if (old ? c->wr > s->wr : (cm != sm ? cm : (c->rw > s->rw || (c->rw == s->rw && c->wr > s->wr)))) s = c;
        }
    if (!s) return NULL;
    if (getenv("RECOMP_SURF_LOG2")) {   /* testing: where the old choice (by binding) differs */
        GSurf *o = NULL;
        for (i = 0; i < GSURF_MAX; i++)
            if (g_surf[i].tex && g_surf[i].base == off && (!o || g_surf[i].wr > o->wr)) o = &g_surf[i];
        if (o != s) {
            static uint32_t bases[16], cnt[16]; int q;
            for (q = 0; q < 16 && bases[q] && bases[q] != off; q++) {}
            if (q < 16 && !bases[q]) bases[q] = off;
            if (q < 16 && (cnt[q]++ % 500) == 0) fprintf(stderr, "  [SURF2] %08X: sampled %s %ux%u (written %llu), by binding it was %s %ux%u (written %llu)\n", off,
                                   s->is_depth ? "zeta" : "colour", s->w, s->h, (unsigned long long)s->rw,
                                   o->is_depth ? "zeta" : "colour", o->w, o->h, (unsigned long long)o->rw);
        }
    }
    *is_depth = (int)s->is_depth;
    return s;
}

/* The combiner program of the batch, for the specialized pixel shaders
 * (nv2a_d3d11_ps.inc): every integer that decides the shader's code. */
typedef struct {
    int st[8][20];                    /* cin ain f1 f2 f3 per stage (first misc[0]) */
    int fin0[4], fin1[4], misc[4];
    int fogmode, fogen;
    int smode[4], sflags[4];
    int at_en, at_func, at_s0;
    int sdot[4], sinp[4];
} PsKey;
static PsKey g_pskey;
static ID3D11PixelShader *g_ps_bound;

static void gpu_bind_stages(void)
{
    struct {
        int cin[4], ain[4], f1[4], f2[4], f3[4]; float c0[4], c1[4];
    } st[8];
    struct {
        int fin0[4], fin1[4], misc[4];
        float ff0[4], ff1[4], fogc[4], fogp[4];
        int smode[4], sflags[4];
        float sscale[4][4];
        int atest[4];
        int sdot[4];      /* per stage: dot-product normal mapping (SET_DOT_RGBMAPPING) */
        int sinp[4];      /* per stage: which earlier stage supplies the normal */
    } tail;
    char cb[sizeof st + sizeof tail];
    ID3D11ShaderResourceView *srv[4] = { 0 };
    ID3D11SamplerState *smp[4] = { 0 };
    int k, i;

    memset(st, 0, sizeof st);
    memset(&tail, 0, sizeof tail);
    for (i = 0; i < s_cp.n; i++) {
        const CStage *S = &s_cp.st[i];
        for (k = 0; k < 4; k++) {
            st[i].cin[k] = S->c[k].reg | (S->c[k].alpha << 4) | (S->c[k].map << 8);
            st[i].ain[k] = S->a[k].reg | (S->a[k].alpha << 4) | (S->a[k].map << 8);
        }
        st[i].f1[1] = (S->abdot << 1) | S->cddot | (S->mux << 2) | (S->amux << 3);
        st[i].f1[2] = S->op; st[i].f1[3] = S->aop;
        st[i].f2[0] = S->dab; st[i].f2[1] = S->dcd; st[i].f2[2] = S->dsum;
        st[i].f3[0] = S->aab; st[i].f3[1] = S->acd; st[i].f3[2] = S->asum;
        st[i].f3[3] = (S->ab_b2a << 1) | S->cd_b2a;
        memcpy(st[i].c0, s_cf0[i], 16); memcpy(st[i].c1, s_cf1[i], 16);
    }
#define PACK(c) ((c).reg | ((c).alpha << 4) | ((c).map << 8))
    tail.fin0[0] = PACK(s_cp.fA); tail.fin0[1] = PACK(s_cp.fB); tail.fin0[2] = PACK(s_cp.fC); tail.fin0[3] = PACK(s_cp.fD);
    tail.fin1[0] = PACK(s_cp.fE); tail.fin1[1] = PACK(s_cp.fF); tail.fin1[2] = PACK(s_cp.fG); tail.fin1[3] = s_cp.has_final;
#undef PACK
    tail.misc[0] = s_cp.n; tail.misc[1] = s_cp.muxmsb; tail.misc[2] = s_cp.clamp_sum; tail.misc[3] = s_3d.comb_seen;
    memcpy(tail.ff0, s_ff0, 16); memcpy(tail.ff1, s_ff1, 16);
    memcpy(tail.fogc, s_fogc, 16);
    tail.fogp[0] = s_3d.fog_params[0]; tail.fogp[1] = s_3d.fog_params[1];
    tail.fogp[2] = (float)s_3d.fog_mode; tail.fogp[3] = s_3d.fog_enable ? 1.0f : 0.0f;
    tail.atest[0] = s_3d.alpha_test; tail.atest[1] = (int)s_3d.alpha_func; tail.atest[2] = (int)s_3d.alpha_ref;
    tail.atest[3] = s_stage[0].enabled;

    for (k = 0; k < 4; k++) {
        const SwStage *S = &s_stage[k];
        int fl = 0, isd = 0;
        GSurf *gs;
        tail.smode[k] = (int)S->mode;
        tail.sdot[k] = k ? (int)((s_dot_map >> (4 * (k - 1))) & 0xF) : 0;
        tail.sinp[k] = k == 2 ? (int)((s_other_in >> 20) & 1) : k == 3 ? (int)((s_other_in >> 21) & 3) : 0;
        if (getenv("RECOMP_STAGE_MODES") && S->enabled) {
            /* testing: each distinct stage program / texture format once */
            static uint32_t seen[256]; static int ns;
            uint32_t key = (S->mode << 24) | (k << 20) | (s_tex_reg[k * 0x10 + 1] & 0xFFFFF), q;
            for (q = 0; q < (uint32_t)ns && seen[q] != key; q++) {}
            if (q == (uint32_t)ns && ns < 256) {
                seen[ns++] = key;
                fprintf(stderr, "  [STAGE] stage %d mode %u fmt %08X (cube %u) %ux%u batch %u\n", k, S->mode,
                        s_tex_reg[k * 0x10 + 1], (s_tex_reg[k * 0x10 + 1] >> 2) & 1, S->t.width, S->t.height, s_3d.batches);
            }
        }
        if (S->enabled) fl |= 1;
        if (s_tex_used[k] || (S->mode && S->mode != 1 && S->mode != 2)) fl |= 2;
        if (S->normalized) fl |= 4;
        tail.sscale[k][0] = S->t.width ? 1.0f / (float)S->t.width : 1.0f;
        tail.sscale[k][1] = S->t.height ? 1.0f / (float)S->t.height : 1.0f;
        if ((fl & 1) && (fl & 2)) {
            gs = gpu_surface_for_texture(S->t.offset, S->t.width, S->t.height, &isd);
            if (getenv("RECOMP_SURF_LOG") && (gs || (S->t.width <= 256 && S->t.height <= 256 && S->t.offset < 0x04000000u
                                                     && gsurf_near(S->t.offset)))) {
                static uint32_t seen[64]; static int ns; int q;
                uint32_t key = S->t.offset ^ (s_tex_reg[k * 0x10 + 1] << 4);
                for (q = 0; q < ns && seen[q] != key; q++) {}
                if (q == ns && ns < 64) {
                    seen[ns++] = key;
                    fprintf(stderr, "  [SURF] stage %d samples %08X %ux%u fmt %08X mode %u -> %s\n", k, S->t.offset,
                            S->t.width, S->t.height, s_tex_reg[k * 0x10 + 1], S->mode,
                            gs ? (isd ? "zeta surface" : "colour surface") : "memory (near a surface!)");
                }
            }
            if (gs && !isd) gsurf_sync_from_mem(gs);
            if (gs) {
                gsurf_resolve(gs);
                srv[k] = gs->srv;
                if (isd) fl |= 8;
                if (!S->normalized) { tail.sscale[k][0] = 1.0f / (float)gs->w; tail.sscale[k][1] = 1.0f / (float)gs->h; }
            } else {
                srv[k] = gtex_get(&S->t);
            }
            if (!srv[k]) fl &= ~1;
            /* (a pack picture: always through its levels -- it is many times the size it is drawn at) */
            smp[k] = gpu_sampler_ex(S->t.addr_u, S->t.addr_v, S->bilinear || (!gs && s_ultra_used),
                                    (S->mip || (!gs && s_ultra_used)) && !gs && S->normalized);
        }
        tail.sflags[k] = fl;
    }
    {
        PsKey *K = &g_pskey;
        memset(K, 0, sizeof *K);
        K->misc[0] = tail.misc[0]; K->misc[1] = tail.misc[1]; K->misc[2] = tail.misc[2]; K->misc[3] = tail.misc[3];
        if (K->misc[3]) {
            for (i = 0; i < s_cp.n && i < 8; i++) {
                memcpy(&K->st[i][0], st[i].cin, 16); memcpy(&K->st[i][4], st[i].ain, 16);
                memcpy(&K->st[i][8], st[i].f1, 16); memcpy(&K->st[i][12], st[i].f2, 16);
                memcpy(&K->st[i][16], st[i].f3, 16);
            }
            memcpy(K->fin0, tail.fin0, 16); memcpy(K->fin1, tail.fin1, 16);
        } else {
            K->misc[0] = K->misc[1] = K->misc[2] = 0;
            K->at_s0 = tail.atest[3] != 0;
        }
        K->fogen = s_3d.fog_enable ? 1 : 0;
        K->fogmode = K->fogen ? (int)s_3d.fog_mode : 0;
        memcpy(K->smode, tail.smode, 16); memcpy(K->sflags, tail.sflags, 16);
        K->at_en = tail.atest[0] ? 1 : 0;
        K->at_func = K->at_en ? tail.atest[1] : 0;
        memcpy(K->sdot, tail.sdot, 16); memcpy(K->sinp, tail.sinp, 16);
    }
    memcpy(cb, st, sizeof st);
    memcpy(cb + sizeof st, &tail, sizeof tail);
    if (!g_sh.cbps_ok || memcmp(g_sh.cbps, cb, sizeof cb)) {
        ID3D11DeviceContext_UpdateSubresource(g_ctx, (ID3D11Resource *)g_cb_ps, 0, NULL, cb, 0, 0);
        memcpy(g_sh.cbps, cb, sizeof cb); g_sh.cbps_ok = 1;
    }
    if (!g_sh.tex_ok || memcmp(g_sh.srv, srv, sizeof srv) || memcmp(g_sh.smp, smp, sizeof smp)) {
        ID3D11DeviceContext_PSSetShaderResources(g_ctx, 0, 4, srv);
        ID3D11DeviceContext_PSSetSamplers(g_ctx, 0, 4, smp);
        memcpy(g_sh.srv, srv, sizeof srv); memcpy(g_sh.smp, smp, sizeof smp); g_sh.tex_ok = 1;
    }
}

/* Vertices of a batch: assembled first (index -> slot), then transformed
 * all together -- on several cores when the batch is big, since running the
 * title's vertex programs on the CPU is most of the emulated GPU's cost. */
static uint32_t *g_vidx, g_vidx_cap;      /* guest index of each slot */
static uint8_t  *g_vok;                   /* slot transformed to a usable vertex */

static uint32_t gpu_vertex_slot(uint32_t index, uint32_t *nverts)
{
    uint32_t slot = index & (SW3D_CACHE - 1);
    if (g_slot_gen[slot] == g_gen && g_slot[slot * 2] == index) return g_slot[slot * 2 + 1];
    if (*nverts == g_vidx_cap) {
        uint32_t nc = g_vidx_cap ? g_vidx_cap * 2 : 4096;
        uint32_t *ni = (uint32_t *)realloc(g_vidx, nc * 4);
        uint8_t *no = (uint8_t *)realloc(g_vok, nc);
        if (ni) g_vidx = ni;
        if (no) g_vok = no;
        if (!ni || !no) return 0xFFFFFFFFu;
        g_vidx_cap = nc;
    }
    g_vidx[*nverts] = index;
    g_slot_gen[slot] = g_gen;
    g_slot[slot * 2] = index;
    g_slot[slot * 2 + 1] = *nverts;
    return (*nverts)++;
}

static void gpu_xform_range(uint32_t lo, uint32_t hi)
{
    uint32_t k;
    for (k = lo; k < hi; k++) {
        SwVert v;
        GVert *g = &g_verts[k];
        if (!sw3d_transform(g_vidx[k], &v)) { g_vok[k] = 0; memset(g, 0, sizeof *g); continue; }
        if (v.p[3] == 0.0f) v.p[3] = 1e-30f;
        g_vok[k] = 1;
        memcpy(g->p, v.p, 16);
        memcpy(g->d0, &v.a[SA_D0], 16);
        memcpy(g->d1, &v.a[SA_D1], 16);
        g->fog[0] = v.a[SA_FOG]; g->fog[1] = g->fog[2] = g->fog[3] = 0;
        memcpy(g->t, &v.a[SA_T0], 64);
    }
}

/* Transform workers. Each claims 64-vertex chunks of the current job until
 * none are left; the batch's own thread works too. They sit on the cores
 * other than CPU 0 (the title's threads live there). RECOMP_VTX_THREADS=n
 * (0 = off) overrides the count. */
#define VX_MAX 8
#define VX_CHUNK 64
static int g_vx_n = -1;
static volatile LONG g_vx_gen, g_vx_next, g_vx_busy;
static volatile uint32_t g_vx_total;
static HANDLE g_vx_wake[VX_MAX];
static volatile LONG g_vx_sleeping[VX_MAX];
static void gpu_vx_work(void)
{
    for (;;) {
        LONG c = InterlockedIncrement(&g_vx_next) - 1;
        uint32_t lo = (uint32_t)c * VX_CHUNK, hi;
        if (lo >= g_vx_total) break;
        hi = lo + VX_CHUNK; if (hi > g_vx_total) hi = g_vx_total;
        gpu_xform_range(lo, hi);
    }
}
static DWORD WINAPI gpu_vx_thread(LPVOID arg)
{
    int idx = (int)(intptr_t)arg;
    LONG seen = 0;
    DWORD_PTR pm = 0, sm = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &pm, &sm) && (pm & ~(DWORD_PTR)1))
        SetThreadAffinityMask(GetCurrentThread(), pm & ~(DWORD_PTR)1);
    for (;;) {
        int spins = 0;
        while (g_vx_gen == seen) {
            if (++spins < 20000) { YieldProcessor(); continue; }
            InterlockedExchange(&g_vx_sleeping[idx], 1);
            if (g_vx_gen == seen) WaitForSingleObject(g_vx_wake[idx], 20);
            InterlockedExchange(&g_vx_sleeping[idx], 0);
            spins = 0;
        }
        seen = g_vx_gen;
        gpu_vx_work();
        InterlockedDecrement(&g_vx_busy);
    }
    return 0;
}
static void gpu_vx_init(void)
{
    DWORD_PTR pm = 0, sm = 0;
    int i, cpus = 0;
    const char *e = getenv("RECOMP_VTX_THREADS");
    g_vx_n = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &pm, &sm))
        for (i = 1; i < (int)(8 * sizeof(DWORD_PTR)); i++) if (pm & ((DWORD_PTR)1 << i)) cpus++;
    g_vx_n = cpus > 4 ? 4 : cpus;           /* leave room for the title's other threads */
    if (cpus >= 2 && g_vx_n > cpus - 1) g_vx_n = cpus - 1;
    if (e) { g_vx_n = atoi(e); if (g_vx_n < 0) g_vx_n = 0; if (g_vx_n > VX_MAX) g_vx_n = VX_MAX; }
    for (i = 0; i < g_vx_n; i++) {
        HANDLE h;
        g_vx_wake[i] = CreateEventA(NULL, FALSE, FALSE, NULL);
        h = CreateThread(NULL, 0, gpu_vx_thread, (LPVOID)(intptr_t)i, 0, NULL);
        if (!h) { g_vx_n = i; break; }
        SetThreadPriority(h, THREAD_PRIORITY_ABOVE_NORMAL);
        CloseHandle(h);
    }
    fprintf(stderr, "  [GPU3D] %d vertex transform thread(s) besides the caller\n", g_vx_n);
}

static void gpu_transform_all(uint32_t nverts)
{
    int i;
    if (nverts > g_verts_cap) {
        uint32_t nc = g_verts_cap ? g_verts_cap : 4096;
        GVert *nv;
        while (nc < nverts) nc *= 2;
        nv = (GVert *)realloc(g_verts, nc * sizeof(GVert));
        if (!nv) { nverts = g_verts_cap; } else { g_verts = nv; g_verts_cap = nc; }
    }
    s_3d.verts += nverts;
    if (g_vx_n < 0) gpu_vx_init();
    /* Serial when small, or when the program writes constants (the
     * vertices would then depend on each other's order). */
    if (g_vx_n <= 0 || nverts < 2 * VX_CHUNK || (s_3d.mode == 2 && (s_vp_slow || s_vpd_cwrite))) {
        gpu_xform_range(0, nverts);
        return;
    }
    g_vx_total = nverts;
    g_vx_next = 0;
    g_vx_busy = g_vx_n;
    MemoryBarrier();
    InterlockedIncrement(&g_vx_gen);
    for (i = 0; i < g_vx_n; i++)
        if (g_vx_sleeping[i]) SetEvent(g_vx_wake[i]);
    gpu_vx_work();
    while (g_vx_busy > 0) YieldProcessor();
}

static uint32_t g_nidx;
static void gpu_tri(uint32_t a, uint32_t b, uint32_t c, uint32_t *nverts)
{
    uint32_t sa = gpu_vertex_slot(a, nverts), sb, sc;
    if (sa == 0xFFFFFFFFu) return;
    sb = gpu_vertex_slot(b, nverts); if (sb == 0xFFFFFFFFu) return;
    sc = gpu_vertex_slot(c, nverts); if (sc == 0xFFFFFFFFu) return;
    if (g_nidx + 3 > g_idx_cap) {
        uint32_t nc = g_idx_cap ? g_idx_cap * 2 : 16384;
        uint32_t *ni = (uint32_t *)realloc(g_idx, nc * 4);
        if (!ni) return;
        g_idx = ni; g_idx_cap = nc;
    }
    g_idx[g_nidx++] = sa; g_idx[g_nidx++] = sb; g_idx[g_nidx++] = sc;
}

/* Drop the triangles that use a vertex the transform rejected (non-finite). */
static uint32_t g_badtri_stat;
volatile uint32_t xbox_gpu_cpu_lo, xbox_gpu_cpu_hi;   /* see gpu_batch */   /* triangles dropped for a non-finite vertex (diagnostics) */
static void gpu_drop_bad_tris(uint32_t nverts)
{
    uint32_t k, o = 0, bad = 0;
    for (k = 0; k < nverts; k++) if (!g_vok[k]) { bad = 1; break; }
    if (!bad) return;
    for (k = 0; k + 2 < g_nidx; k += 3) {
        if (!g_vok[g_idx[k]] || !g_vok[g_idx[k + 1]] || !g_vok[g_idx[k + 2]]) { g_badtri_stat++; continue; }
        g_idx[o++] = g_idx[k]; g_idx[o++] = g_idx[k + 1]; g_idx[o++] = g_idx[k + 2];
    }
    g_nidx = o;
}

#include <stdarg.h>
#include "nv2a_d3d11_vp.inc"
#include "nv2a_d3d11_ps.inc"

/* Draw the assembled batch with the program's GPU vertex shader. */
static void gpu_batch_hw(HwVp *hv, uint32_t nverts)
{
    UINT stride = 16 * hv->nin, off = 0, vb_at = 0, ib_at;
    uint32_t k;
    if (hv->nin) {
        size_t need = (size_t)nverts * 4 * hv->nin;
        if (need > g_xin_cap) {
            float *n = (float *)realloc(g_xin, need * sizeof(float) * 2);
            if (!n) return;
            g_xin = n; g_xin_cap = (uint32_t)(need * 2);
        }
        hwvp_pack_range(hv, 0, nverts);
        vb_at = gpu_upload(&g_vbx, &g_vbx_cap, &g_vbx_pos, D3D11_BIND_VERTEX_BUFFER, g_xin, (UINT)(need * sizeof(float)), stride);
        if (vb_at == 0xFFFFFFFFu) return;
        /* Base the indices on the ring offset: a stride that does not
         * divide the ring position cannot use BaseVertexLocation reliably
         * across drivers, so the indices carry it. */
        vb_at /= stride;
        for (k = 0; k < g_nidx; k++) g_idx[k] += vb_at;
    }
    ib_at = gpu_upload(&g_ib, &g_ib_cap, &g_ib_pos, D3D11_BIND_INDEX_BUFFER, g_idx, g_nidx * 4, 4);
    if (!g_ib || ib_at == 0xFFFFFFFFu) return;
    if (!g_vpc_ok || memcmp(g_vpc_shadow, s_3d.c, sizeof g_vpc_shadow)) {
        memcpy(g_vpc_shadow, s_3d.c, sizeof g_vpc_shadow);
        ID3D11DeviceContext_UpdateSubresource(g_ctx, (ID3D11Resource *)g_cb_vpc, 0, NULL, g_vpc_shadow, 0, 0);
        g_vpc_ok = 1;
    }
    gpu_bind_stages();
    gpu_set_blend(0);
    gpu_set_depth(g_cur_ds != NULL);
    ID3D11DeviceContext_IASetInputLayout(g_ctx, hv->il);
    if (hv->nin) ID3D11DeviceContext_IASetVertexBuffers(g_ctx, 0, 1, &g_vbx, &stride, &off);
    ID3D11DeviceContext_IASetIndexBuffer(g_ctx, g_ib, DXGI_FORMAT_R32_UINT, 0);
    ID3D11DeviceContext_IASetPrimitiveTopology(g_ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(g_ctx, hv->vs, NULL, 0);
    {
        ID3D11Buffer *cbs[3] = { g_cb_vs, NULL, g_cb_vpc };
        ID3D11DeviceContext_VSSetConstantBuffers(g_ctx, 0, 3, cbs);
    }
    g_ps_cur = hwps_lookup(&g_pskey);
    ID3D11DeviceContext_PSSetShader(g_ctx, g_ps_cur, NULL, 0);
    ID3D11DeviceContext_PSSetConstantBuffers(g_ctx, 1, 1, &g_cb_ps);
    g_sh.pipe = 0;                         /* the CPU path rebinds its own */
    ID3D11DeviceContext_DrawIndexed(g_ctx, g_nidx, ib_at / 4, 0);
    s_gpu.tris_drawn += g_nidx / 3;
    s_gpu.drawn_offset = s_gpu.color_offset;
    s_3d.verts += nverts;
    g_hwvp_stat_hw++;
}

static void gpu_batch(void)
{
    uint32_t i, n = s_gpu.idx_count, nverts = 0;
    UINT stride = sizeof(GVert), off = 0, vb_at, ib_at;
    int need_depth = s_3d.depth_test || s_3d.stencil_test;

    {
        static int lo = -2, hi;
        if (lo == -2) { const char *e = getenv("RECOMP_GPU_SKIP"); lo = -1; if (e) sscanf(e, "%d,%d", &lo, &hi); }
        if (lo >= 0 && (int)s_3d.batches >= lo && (int)s_3d.batches <= hi) return;
    }
    if (!g_slot) {
        g_slot = (uint32_t *)calloc(SW3D_CACHE * 2, 4);
        g_slot_gen = (uint32_t *)calloc(SW3D_CACHE, 4);
        if (!g_slot || !g_slot_gen) return;
    }
    if (!gpu_bind_targets(need_depth)) return;
    if (!gpu_set_raster()) return;
    g_gen++; if (!g_gen) g_gen = 1;
    g_nidx = 0;
    switch (s_gpu.prim) {
    case NV_PRIM_TRIANGLES:
        for (i = 0; i + 2 < n; i += 3) gpu_tri(s_gpu.idx[i], s_gpu.idx[i+1], s_gpu.idx[i+2], &nverts);
        break;
    case NV_PRIM_TRIANGLE_STRIP:
        for (i = 0; i + 2 < n; i++) {
            if (i & 1) gpu_tri(s_gpu.idx[i+1], s_gpu.idx[i], s_gpu.idx[i+2], &nverts);
            else       gpu_tri(s_gpu.idx[i], s_gpu.idx[i+1], s_gpu.idx[i+2], &nverts);
        }
        break;
    case NV_PRIM_TRIANGLE_FAN:
    case NV_PRIM_POLYGON:
        for (i = 1; i + 1 < n; i++) gpu_tri(s_gpu.idx[0], s_gpu.idx[i], s_gpu.idx[i+1], &nverts);
        break;
    case NV_PRIM_QUADS:
        for (i = 0; i + 3 < n; i += 4) {
            gpu_tri(s_gpu.idx[i], s_gpu.idx[i+1], s_gpu.idx[i+2], &nverts);
            gpu_tri(s_gpu.idx[i], s_gpu.idx[i+2], s_gpu.idx[i+3], &nverts);
        }
        break;
    case NV_PRIM_QUAD_STRIP:
        for (i = 0; i + 3 < n; i += 2) {
            gpu_tri(s_gpu.idx[i], s_gpu.idx[i+1], s_gpu.idx[i+3], &nverts);
            gpu_tri(s_gpu.idx[i], s_gpu.idx[i+3], s_gpu.idx[i+2], &nverts);
        }
        break;
    default:
        return;
    }
    if (!g_nidx) return;
    if (s_3d.mode == 2 && s_vpd_len > 3) g_frame_3d = 1;
    if (g_wide_prev && s_3d.mode == 2 && s_vpd_len <= 3 && g_aspect > 1.34f) {
        static int hud = -1;
        if (hud < 0) hud = getenv("RECOMP_HUD_STRETCH") == NULL;
        if (hud) {
            uint32_t k; float mnx = 1e9f, mxx = -1e9f, mny = 1e9f, mxy = -1e9f, kk = g_aspect / (4.0f / 3.0f);
            gpu_transform_all(nverts);
            gpu_drop_bad_tris(nverts);
            if (!g_nidx) return;
            for (k = 0; k < nverts; k++) if (g_vok[k]) {
                float x = g_verts[k].p[0] / g_verts[k].p[3], y = g_verts[k].p[1] / g_verts[k].p[3];
                if (x < mnx) mnx = x; if (x > mxx) mxx = x;
                if (y < mny) mny = y; if (y > mxy) mxy = y;
            }
            /* HUD-sized pieces only: big screen-space quads are effects
             * (fog, blur, fades, often drawn in strips) and must keep
             * covering the frame they were made for. */
            int samples_frame = 0, st;
            /* ...and not screen-space effects that sample a rendered frame
             * at their own position (lava heat haze): moving those would
             * show the wrong part of the picture. */
            for (st = 0; st < 4; st++) {
                int isd;
                if (s_stage[st].enabled && gpu_surface_for_texture(s_stage[st].t.offset, s_stage[st].t.width, s_stage[st].t.height, &isd)) samples_frame = 1;
            }
            if (!samples_frame && (mxx - mnx) < 400.0f && (mxx - mnx) * (mxy - mny) < 0.2f * 640.0f * 480.0f
                && g_cur_rt && g_cur_rt->w >= 640)
            {
                hud_note(mnx, mxx, mny, mxy);
                for (k = 0; k < nverts; k++) if (g_vok[k]) {
                    float w = g_verts[k].p[3], x = g_verts[k].p[0] / w;
                    g_verts[k].p[0] = hud_wide_x(x, (mnx + mxx) * 0.5f, (mny + mxy) * 0.5f, kk) * w;
                }
            }
            goto cpu_verts_ready;
        }
    }
    if (getenv("RECOMP_BATCH_SHAPES")) {
        /* testing: program length and window-space bounds of each batch */
        static int shown;
        uint32_t k; float mnx = 1e9f, mxx = -1e9f, mny = 1e9f, mxy = -1e9f;
        gpu_transform_all(nverts);
        for (k = 0; k < nverts; k++) if (g_vok[k]) {
            float x = g_verts[k].p[0] / g_verts[k].p[3], y = g_verts[k].p[1] / g_verts[k].p[3];
            if (x < mnx) mnx = x; if (x > mxx) mxx = x; if (y < mny) mny = y; if (y > mxy) mxy = y;
        }
        if (g_present_n % 300 == 150 && shown++ < 20000)
            fprintf(stderr, "  [SHAPE] surf %ux%u clip %u,%u %ux%u ", g_cur_rt ? g_cur_rt->w : 0, g_cur_rt ? g_cur_rt->h : 0,
                    s_gpu.clip_x, s_gpu.clip_y, s_gpu.clip_w, s_gpu.clip_h),
            fprintf(stderr, "  [SHAPE] batch %u mode %u len %u verts %u x %.0f..%.0f y %.0f..%.0f w0 %.3f\n", s_3d.batches, s_3d.mode,
                    s_vpd_len, nverts, mnx, mxx, mny, mxy, nverts ? g_verts[0].p[3] : 0.0f);
    }
    {
        HwVp *hv;
        static int nul = -1;
        /* the title may ask for some models to be transformed on the CPU
         * (xbox_gpu_cpu_lo/hi: guest addresses of their vertex data) */
        uint32_t va0 = s_gpu.attr[0].offset | 0x80000000u;
        hv = (xbox_gpu_cpu_hi && va0 >= xbox_gpu_cpu_lo && va0 < xbox_gpu_cpu_hi) ? NULL : hwvp_lookup();
        if (hv) {
            if (nul < 0) nul = getenv("RECOMP_GPU_NULL") != NULL;
            if (!nul) gpu_batch_hw(hv, nverts);
            else if (hv->nin) {            /* CPU side only: the input fetch */
                size_t need = (size_t)nverts * 4 * hv->nin;
                if (need > g_xin_cap) { float *n2 = (float *)realloc(g_xin, need * 8); if (!n2) return; g_xin = n2; g_xin_cap = (uint32_t)(need * 2); }
                hwvp_pack_range(hv, 0, nverts);
            }
            return;
        }
        if (s_3d.mode == 2) g_hwvp_stat_cpu++;
    }
    gpu_transform_all(nverts);
    gpu_drop_bad_tris(nverts);
    if (!g_nidx) return;
    {   /* testing: RECOMP_BIGTRI=1 logs batches with screen-spanning triangles, 2 also skips them */
        static int bt = -1; static uint32_t seen[256]; static int nseen;
        if (bt < 0) { const char *e = getenv("RECOMP_BIGTRI"); bt = e ? atoi(e) : 0; }
        if (bt == 4 && s_vpd_len == 13 && !s_gpu.attr[0].offset) return;
        if (bt == 5 && s_vpd_len == 13 && !s_gpu.attr[0].offset) {
            static int shown;
            uint32_t k; float mn[3] = {1e30f,1e30f,1e30f}, mx[3] = {-1e30f,-1e30f,-1e30f}, f[4]; int a2;
            for (k = 0; k < nverts; k++) { fetch_attr_any(0, g_vidx[k], f); for (a2 = 0; a2 < 3; a2++) { if (f[a2] < mn[a2]) mn[a2] = f[a2]; if (f[a2] > mx[a2]) mx[a2] = f[a2]; } }
            if (shown++ < 400 && (shown % 4) == 0) fprintf(stderr, "  [SHADOWIN] n %u x %.1f..%.1f y %.1f..%.1f z %.1f..%.1f\n", nverts, mn[0], mx[0], mn[1], mx[1], mn[2], mx[2]);
        }
        if (bt == 3 && s_vpd_len == 13 && !s_gpu.attr[0].offset) {
            static uint32_t mx, n;
            if (nverts > mx) mx = nverts;
            if ((++n & 255) == 0) { fprintf(stderr, "  [VOL] %u draws, max verts %u, now %u idx %u\n", n, mx, nverts, g_nidx); mx = 0; }
        }
        if (bt && bt < 3 && s_3d.mode == 2) {
            uint32_t k, big = 0;
            for (k = 0; k + 2 < g_nidx && !big; k += 3) {
                float mn = 1e30f, mx = -1e30f, mny = 1e30f, mxy = -1e30f; int j;
                for (j = 0; j < 3; j++) {
                    const float *P = g_verts[g_idx[k + j]].p; float x, y;
                    if (P[3] <= 0) { mn = 0; mx = 0; break; }
                    x = P[0] / P[3]; y = P[1] / P[3];
                    if (x < mn) mn = x; if (x > mx) mx = x; if (y < mny) mny = y; if (y > mxy) mxy = y;
                }
                if (mx - mn > 1200.0f || mxy - mny > 1200.0f) big = 1;
            }
            if (big) {
                uint32_t va = s_gpu.attr[0].offset | 0x80000000u, key = va ^ (nverts << 20) ^ s_vpd_len, i;
                for (i = 0; i < (uint32_t)nseen && seen[i] != key; i++) ;
                if (i == (uint32_t)nseen && nseen < 256) {
                    seen[nseen++] = key;
                    fprintf(stderr, "  [BIGTRI] attr0 %08X stride %u verts %u idx %u vplen %u tex0 %08X frame %u\n", va,
                            s_gpu.attr[0].stride, nverts, g_nidx, s_vpd_len, s_stage[0].enabled ? s_stage[0].t.offset : 0, g_present_n);
                }
                if (bt == 2) return;
            }
        }
    }
cpu_verts_ready:
    {
        static int nul = -1;   /* RECOMP_GPU_NULL: measure the CPU side alone */
        if (nul < 0) nul = getenv("RECOMP_GPU_NULL") != NULL;
        if (nul) { return; }
    }
    vb_at = gpu_upload(&g_vb, &g_vb_cap, &g_vb_pos, D3D11_BIND_VERTEX_BUFFER, g_verts, nverts * sizeof(GVert), sizeof(GVert));
    ib_at = gpu_upload(&g_ib, &g_ib_cap, &g_ib_pos, D3D11_BIND_INDEX_BUFFER, g_idx, g_nidx * 4, 4);
    if (!g_vb || !g_ib || vb_at == 0xFFFFFFFFu || ib_at == 0xFFFFFFFFu) return;
    (void)off;
    gpu_bind_stages();
    gpu_set_blend(0);
    gpu_set_depth(g_cur_ds != NULL);
    if (!g_sh.pipe) {
        off = 0;
        ID3D11DeviceContext_IASetInputLayout(g_ctx, g_il);
        ID3D11DeviceContext_IASetVertexBuffers(g_ctx, 0, 1, &g_vb, &stride, &off);
        ID3D11DeviceContext_IASetIndexBuffer(g_ctx, g_ib, DXGI_FORMAT_R32_UINT, 0);
        ID3D11DeviceContext_IASetPrimitiveTopology(g_ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11DeviceContext_VSSetShader(g_ctx, g_vs, NULL, 0);
        ID3D11DeviceContext_VSSetConstantBuffers(g_ctx, 0, 1, &g_cb_vs);
        g_ps_cur = hwps_lookup(&g_pskey);
        ID3D11DeviceContext_PSSetShader(g_ctx, g_ps_cur, NULL, 0);
        ID3D11DeviceContext_PSSetConstantBuffers(g_ctx, 1, 1, &g_cb_ps);
        g_ps_bound = g_ps_cur;
        g_sh.pipe = 1;
    } else {
        g_ps_cur = hwps_lookup(&g_pskey);
        if (g_ps_cur != g_ps_bound) { ID3D11DeviceContext_PSSetShader(g_ctx, g_ps_cur, NULL, 0); g_ps_bound = g_ps_cur; }
    }
    {
        static int lo = -2, hi, mode;
        if (lo == -2) { const char *e = getenv("RECOMP_GPU_MARK"); lo = -1; if (e) sscanf(e, "%d,%d,%d", &lo, &hi, &mode); }
        if (lo >= 0 && (int)s_3d.batches >= lo && (int)s_3d.batches <= hi) {
            float mag[4] = { 1, 0, 1, 1 };
            ID3D11DeviceContext_UpdateSubresource(g_ctx, (ID3D11Resource *)g_cb_cl, 0, NULL, mag, 0, 0);
            ID3D11DeviceContext_PSSetShader(g_ctx, g_ps_clear, NULL, 0);
            ID3D11DeviceContext_PSSetConstantBuffers(g_ctx, 2, 1, &g_cb_cl);
            ID3D11DeviceContext_OMSetBlendState(g_ctx, NULL, NULL, 0xFFFFFFFFu);
            if (mode & 1) ID3D11DeviceContext_OMSetDepthStencilState(g_ctx, NULL, 0);
            if (mode & 1) ID3D11DeviceContext_OMSetRenderTargets(g_ctx, 1, &g_cur_rt->rtv, NULL);
            ID3D11DeviceContext_DrawIndexed(g_ctx, g_nidx, ib_at / 4, (INT)(vb_at / sizeof(GVert)));
            gpu_sh_reset();
            return;
        }
    }
    ID3D11DeviceContext_DrawIndexed(g_ctx, g_nidx, ib_at / 4, (INT)(vb_at / sizeof(GVert)));
    s_gpu.tris_drawn += g_nidx / 3;
    s_gpu.drawn_offset = s_gpu.color_offset;
    if (s_3d.color_mask) g_cur_rt->rw = ++g_wr_clock;
    if (g_cur_ds && ((s_3d.depth_test && s_3d.depth_mask) || (s_3d.stencil_test && (s_3d.st_wmask & 0xFF)))) g_cur_ds->rw = ++g_wr_clock;
}

/* CLEAR_SURFACE. Colour with a per-channel mask is a full-target quad with
 * that write mask; depth and stencil clear the view. */
static void gpu_clear(uint32_t param)
{
    int need_depth = (param & 3) != 0;
    if (!gpu_bind_targets(need_depth)) return;
    if (param & 3) {
        if (g_cur_ds) {
            uint32_t zv = s_cb_scratch[0];
            UINT fl = ((param & 1) ? D3D11_CLEAR_DEPTH : 0) | ((param & 2) ? D3D11_CLEAR_STENCIL : 0);
            float z = (((s_gpu.format >> 4) & 0xF) == 1) ? (float)(zv & 0xFFFF) / 65535.0f : (float)(zv >> 8) / 16777215.0f;
            ID3D11DeviceContext_ClearDepthStencilView(g_ctx, g_cur_ds->dsv, fl, z, (UINT8)(zv & 0xFF));
            g_cur_ds->rw = ++g_wr_clock;
        }
    }
    if (param & 0xF0) {
        uint32_t saved_mask = s_3d.color_mask, c = s_gpu.clear_color;
        gpu_sh_reset();
        float col[4];
        col[0] = ((c >> 16) & 0xFF) / 255.0f; col[1] = ((c >> 8) & 0xFF) / 255.0f;
        col[2] = (c & 0xFF) / 255.0f; col[3] = (c >> 24) / 255.0f;
        s_3d.color_mask = ((param & 0x10) ? 0x00010000u : 0) | ((param & 0x20) ? 0x00000100u : 0)
                        | ((param & 0x40) ? 0x00000001u : 0) | ((param & 0x80) ? 0x01000000u : 0);
        ID3D11DeviceContext_OMSetRenderTargets(g_ctx, 1, &g_cur_rt->rtv, NULL);
        gpu_set_blend(1);
        {
            /* no depth/stencil test for the clear quad */
            uint64_t k[3] = { 0xFFFFFFFFFFFFFFFFull, 0, 0 };
            ID3D11DepthStencilState *ds = (ID3D11DepthStencilState *)gstate_find(g_ds_c, k);
            if (!ds) {
                D3D11_DEPTH_STENCIL_DESC d;
                memset(&d, 0, sizeof d);
                d.DepthFunc = D3D11_COMPARISON_ALWAYS;
                if (SUCCEEDED(ID3D11Device_CreateDepthStencilState(g_dev, &d, &ds))) gstate_put(g_ds_c, k, ds);
            }
            ID3D11DeviceContext_OMSetDepthStencilState(g_ctx, ds, 0);
        }
        {
            uint64_t k[3] = { 0xFFFFFFFFFFFFFFFFull, 1, 0 };
            ID3D11RasterizerState *rs = (ID3D11RasterizerState *)gstate_find(g_rs_c, k);
            if (!rs) {
                D3D11_RASTERIZER_DESC d;
                memset(&d, 0, sizeof d);
                d.FillMode = D3D11_FILL_SOLID; d.CullMode = D3D11_CULL_NONE; d.ScissorEnable = TRUE;
                if (SUCCEEDED(ID3D11Device_CreateRasterizerState(g_dev, &d, &rs))) gstate_put(g_rs_c, k, rs);
            }
            ID3D11DeviceContext_RSSetState(g_ctx, rs);
        }
        ID3D11DeviceContext_UpdateSubresource(g_ctx, (ID3D11Resource *)g_cb_cl, 0, NULL, col, 0, 0);
        ID3D11DeviceContext_IASetInputLayout(g_ctx, NULL);
        ID3D11DeviceContext_IASetPrimitiveTopology(g_ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11DeviceContext_VSSetShader(g_ctx, g_vs_quad, NULL, 0);
        ID3D11DeviceContext_PSSetShader(g_ctx, g_ps_clear, NULL, 0);
        ID3D11DeviceContext_PSSetConstantBuffers(g_ctx, 2, 1, &g_cb_cl);
        ID3D11DeviceContext_Draw(g_ctx, 3, 0);
        g_cur_rt->rw = ++g_wr_clock;
        s_3d.color_mask = saved_mask;
        gpu_sh_reset();
    }
}

/* CopyRects between surfaces the GPU owns (the heat-haze copy of the back
 * buffer). Returns 0 when the source is not a GPU surface, so the caller does
 * the copy in guest memory as before. */
static int gpu_blit(uint32_t sb, uint32_t db, uint32_t ix, uint32_t iy, uint32_t ox, uint32_t oy,
                    uint32_t w, uint32_t h, uint32_t dp, uint32_t bpp)
{
    GSurf *src = gsurf_find(sb, 0), *dst;
    D3D11_BOX box;
    uint32_t dw;
    if (!src || bpp != 4) return 0;
    dw = dp / bpp;
    if (dw < ox + w) dw = ox + w;
    {
        /* A copy of part of a frame into a buffer of the frame's own pitch
         * (the heat haze's back-buffer copy): the buffer is frame sized, so
         * make it so at once -- one surface at the frame's scale -- rather
         * than as tall as the first patch. */
        uint32_t dh = oy + h;
        if (dw == src->w && dh < src->h) dh = src->h;
        dst = gsurf_get(db, dw, dh, 0);
    }
    if (!dst) return 0;
    if (ix + w > src->w) w = src->w > ix ? src->w - ix : 0;
    if (iy + h > src->h) h = src->h > iy ? src->h - iy : 0;
    if (!w || !h) return 1;
    gsurf_resolve(src); gsurf_resolve(dst);   /* (MSAA: copies are between the plain textures) */
    if (dst->mtex) dst->ss_new = 1;
    if (src->pw * dst->w == dst->pw * src->w && src->ph * dst->h == dst->ph * src->h) {
        box.left = SX(src, ix); box.top = SY(src, iy); box.front = 0; box.right = SX(src, ix + w); box.bottom = SY(src, iy + h); box.back = 1;
        if (box.right > src->pw) box.right = src->pw;
        if (box.bottom > src->ph) box.bottom = src->ph;
        if (SX(dst, ox) + (box.right - box.left) > dst->pw || SY(dst, oy) + (box.bottom - box.top) > dst->ph) return 1;
        ID3D11DeviceContext_CopySubresourceRegion(g_ctx, (ID3D11Resource *)dst->tex, 0, SX(dst, ox), SY(dst, oy), 0,
                                                  (ID3D11Resource *)src->tex, 0, &box);
    } else if (dst->rtv1) {
        /* Different scales (a frame-sized source into a small target): draw
         * the region instead of copying texels. */
        D3D11_VIEWPORT vp;
        float c[4];
        ID3D11ShaderResourceView *none = NULL;
        vp.TopLeftX = (float)SX(dst, ox); vp.TopLeftY = (float)SY(dst, oy);
        vp.Width = (float)SX(dst, ox + w) - vp.TopLeftX; vp.Height = (float)SY(dst, oy + h) - vp.TopLeftY;
        vp.MinDepth = 0; vp.MaxDepth = 1;
        c[0] = (float)w / (float)src->w; c[1] = (float)h / (float)src->h;
        c[2] = (float)ix / (float)src->w; c[3] = (float)iy / (float)src->h;
        ID3D11DeviceContext_OMSetRenderTargets(g_ctx, 1, &dst->rtv1, NULL);
        ID3D11DeviceContext_RSSetViewports(g_ctx, 1, &vp);
        ID3D11DeviceContext_RSSetState(g_ctx, NULL);
        ID3D11DeviceContext_OMSetBlendState(g_ctx, NULL, NULL, 0xFFFFFFFFu);
        ID3D11DeviceContext_OMSetDepthStencilState(g_ctx, NULL, 0);
        ID3D11DeviceContext_UpdateSubresource(g_ctx, (ID3D11Resource *)g_cb_cl, 0, NULL, c, 0, 0);
        ID3D11DeviceContext_IASetInputLayout(g_ctx, NULL);
        ID3D11DeviceContext_IASetPrimitiveTopology(g_ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11DeviceContext_VSSetShader(g_ctx, g_vs_blit, NULL, 0);
        ID3D11DeviceContext_VSSetConstantBuffers(g_ctx, 2, 1, &g_cb_cl);
        ID3D11DeviceContext_PSSetShader(g_ctx, g_ps_copy, NULL, 0);
        ID3D11DeviceContext_PSSetShaderResources(g_ctx, 0, 1, &src->srv);
        ID3D11DeviceContext_PSSetSamplers(g_ctx, 0, 1, &g_copy_smp);
        ID3D11DeviceContext_Draw(g_ctx, 3, 0);
        ID3D11DeviceContext_PSSetShaderResources(g_ctx, 0, 1, &none);
        gpu_sh_reset();
    }
    dst->rw = ++g_wr_clock;
    return 1;
}

/* Copy a GPU surface back into guest memory (frame dumps, replay, captures). */
static void gpu_readback(uint32_t base, uint32_t pitch)
{
    GSurf *s = gsurf_find(base, 0);
    D3D11_TEXTURE2D_DESC td;
    ID3D11Texture2D *st = NULL;
    D3D11_MAPPED_SUBRESOURCE ms;
    uint32_t y, w;
    if (!s) return;
    gsurf_resolve(s);
    memset(&td, 0, sizeof td);
    td.Width = s->w; td.Height = s->h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_STAGING; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(ID3D11Device_CreateTexture2D(g_dev, &td, NULL, &st))) return;
    if (s->pw == s->w && s->ph == s->h) ID3D11DeviceContext_CopyResource(g_ctx, (ID3D11Resource *)st, (ID3D11Resource *)s->tex);
    else {
        /* Down to the title's size first. */
        D3D11_BOX b = { 0, 0, 0, s->w, s->h, 1 };
        if (!gpu_one(s->w, s->h)) { ID3D11Texture2D_Release(st); return; }
        gpu_copy_scaled(s->srv, s->pw, s->ph, s->pw, s->ph, g_one_rtv, s->w, s->h);
        ID3D11DeviceContext_CopySubresourceRegion(g_ctx, (ID3D11Resource *)st, 0, 0, 0, 0, (ID3D11Resource *)g_one_tex, 0, &b);
    }
    if (SUCCEEDED(ID3D11DeviceContext_Map(g_ctx, (ID3D11Resource *)st, 0, D3D11_MAP_READ, 0, &ms))) {
        uint8_t *m = (uint8_t *)xbox_GetMemoryOffset() + base;
        if (!pitch) pitch = s->w * 4;
        w = s->w * 4 < pitch ? s->w * 4 : pitch;
        for (y = 0; y < s->h; y++)
            memcpy(m + (size_t)y * pitch, (uint8_t *)ms.pData + (size_t)y * ms.RowPitch, w);
        ID3D11DeviceContext_Unmap(g_ctx, (ID3D11Resource *)st, 0);
        s->mem_sum = gpu_mem_sum(base, (s->pitch ? s->pitch : s->w * 4) * s->h);
    }
    ID3D11Texture2D_Release(st);
}

/* ---------------- graphics settings overlay ---------------- */

/* The panel (src/video/gfx_menu.c draws it with GDI) over the finished frame,
 * centred, at one texel per screen pixel. */
static ID3D11Texture2D *g_menu_tex;
static ID3D11ShaderResourceView *g_menu_srv;
static ID3D11BlendState *g_menu_bs;
static int g_menu_w, g_menu_h;
static unsigned g_menu_ver;
static void gpu_draw_menu(const uint32_t *px, int w, int h, unsigned ver)
{
    D3D11_VIEWPORT vp;
    float cb[4] = { 1, 1, 0, 0 };
    if (!g_menu_tex || g_menu_w != w || g_menu_h != h) {
        D3D11_TEXTURE2D_DESC td;
        if (g_menu_srv) { ID3D11ShaderResourceView_Release(g_menu_srv); g_menu_srv = NULL; }
        if (g_menu_tex) { ID3D11Texture2D_Release(g_menu_tex); g_menu_tex = NULL; }
        memset(&td, 0, sizeof td);
        td.Width = (UINT)w; td.Height = (UINT)h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(ID3D11Device_CreateTexture2D(g_dev, &td, NULL, &g_menu_tex))) { g_menu_tex = NULL; return; }
        ID3D11Device_CreateShaderResourceView(g_dev, (ID3D11Resource *)g_menu_tex, NULL, &g_menu_srv);
        g_menu_w = w; g_menu_h = h; g_menu_ver = 0;
    }
    if (!g_menu_bs) {
        D3D11_BLEND_DESC bd;
        memset(&bd, 0, sizeof bd);
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(ID3D11Device_CreateBlendState(g_dev, &bd, &g_menu_bs))) return;
    }
    if (g_menu_ver != ver) {
        ID3D11DeviceContext_UpdateSubresource(g_ctx, (ID3D11Resource *)g_menu_tex, 0, NULL, px, (UINT)w * 4, 0);
        g_menu_ver = ver;
    }
    vp.TopLeftX = (float)(((int)g_sc_w - w) / 2); vp.TopLeftY = (float)(((int)g_sc_h - h) / 2);
    vp.Width = (float)w; vp.Height = (float)h; vp.MinDepth = 0; vp.MaxDepth = 1;
    ID3D11DeviceContext_RSSetViewports(g_ctx, 1, &vp);
    ID3D11DeviceContext_UpdateSubresource(g_ctx, (ID3D11Resource *)g_cb_cl, 0, NULL, cb, 0, 0);
    ID3D11DeviceContext_OMSetBlendState(g_ctx, g_menu_bs, NULL, 0xFFFFFFFFu);
    ID3D11DeviceContext_VSSetShader(g_ctx, g_vs_present, NULL, 0);
    ID3D11DeviceContext_PSSetShader(g_ctx, g_ps_copy, NULL, 0);
    ID3D11DeviceContext_PSSetShaderResources(g_ctx, 0, 1, &g_menu_srv);
    ID3D11DeviceContext_PSSetSamplers(g_ctx, 0, 1, &g_present_smp);
    ID3D11DeviceContext_Draw(g_ctx, 3, 0);
    {
        ID3D11ShaderResourceView *none = NULL;
        ID3D11DeviceContext_PSSetShaderResources(g_ctx, 0, 1, &none);
    }
    ID3D11DeviceContext_OMSetBlendState(g_ctx, NULL, NULL, 0xFFFFFFFFu);
    gpu_sh_reset();
}

/* A setting changed in the overlay: take the new values. Runs after a
 * Present, between frames, so dropping every surface (a new internal
 * resolution) loses nothing the next frame does not redraw. */
static void gpu_apply_settings(void)
{
    int i, rs, aniso, mips;
    float asp, rsf;
    g_gfx_seen = xbox_gfx_rev;
    g_aa_on = xbox_GfxFxaa();
    g_vsync_on = xbox_GfxGet(GFX_VSYNC);
    g_stretch_on = xbox_GfxGet(GFX_STRETCH);
    g_hwvp_user = xbox_GfxGet(GFX_VP);
    g_post[0] = xbox_GfxGetF(GFX_BRIGHT); g_post[1] = xbox_GfxGetF(GFX_SHARP); g_post[2] = xbox_GfxGetF(GFX_COLOR);
    if (g_post[0] < 0.5f) g_post[0] = 1.0f;
    if (g_post[2] <= 0.0f) g_post[2] = 1.0f;
    if (g_cb_post) ID3D11DeviceContext_UpdateSubresource(g_ctx, (ID3D11Resource *)g_cb_post, 0, NULL, g_post, 0, 0);
    gpu_filter_opts();
    aniso = xbox_GfxGet(GFX_ANISO);
    mips = xbox_GfxGet(GFX_MIPS);
    if (aniso != g_aniso || mips != g_mips) {
        /* samplers are rebuilt on demand; textures too when mipmaps changed */
        for (i = 0; i < GSTATE_MAX; i++)
            if (g_smp_c[i].obj) ID3D11SamplerState_Release((ID3D11SamplerState *)g_smp_c[i].obj);
        memset(g_smp_c, 0, sizeof g_smp_c);
        if (mips != g_mips)
            for (i = 0; i < GTEX_MAX; i++) {
                if (g_tex[i].srv) ID3D11ShaderResourceView_Release(g_tex[i].srv);
                if (g_tex[i].tex) ID3D11Texture2D_Release(g_tex[i].tex);
                memset(&g_tex[i], 0, sizeof g_tex[i]);
            }
        g_aniso = aniso; g_mips = mips;
        g_present_smp = gpu_sampler(3, 3, 1);
        g_copy_smp = g_present_smp;
    }
    rs = xbox_GfxGet(GFX_SCALE);
    rsf = xbox_GfxScaleF();
    asp = xbox_GfxAspect();
    if (asp < 1.34f) asp = 4.0f / 3.0f;
    {
        int ms = g_msaa_bad ? 1 : xbox_GfxMsaa();
        if (ms != g_msaa) {   /* (MSAA on, off or other samples: every surface made again) */
            g_msaa = ms; g_rsf = -1.0f;
        }
    }
    if (rsf != g_rsf || asp != g_aspect) {
        ID3D11DeviceContext_OMSetRenderTargets(g_ctx, 0, NULL, NULL);
        for (i = 0; i < GSURF_MAX; i++)
            if (g_surf[i].tex) gsurf_free(&g_surf[i]);
        g_cur_rt = g_cur_ds = NULL;
        g_rs = rs; g_rsf = rsf; g_aspect = asp;
        g_rsx = g_rsf * (g_aspect / (4.0f / 3.0f));
        g_rsy = g_rsf;
        rs_fit();
    }
    gpu_sh_reset();
    fprintf(stderr, "  [D3D11] settings: scale %g (%.2f x %.2f), picture %.3f:1, aniso x%d, mipmaps %s, %s, vsync %s, %s, vertex programs on the %s\n",
            (double)g_rsf, g_rsx, g_rsy, g_aspect, g_aniso, g_mips ? "on" : "off", g_msaa > 1 ? (g_aa_on ? "MSAA + FXAA" : "MSAA") : g_aa_on ? "FXAA" : "no AA",
            g_vsync_on ? "on" : "off", g_stretch_on ? "stretched" : "shape kept", g_hwvp_user ? "GPU" : "CPU");
    fflush(stderr);
}

/* ---------------- title overlays (HUD mods) ---------------- */

/* The title can draw images over its own picture (src/twotowers e3hud.c):
 * each frame the callback fills up to OVL_MAX quads in the game's 640x480
 * screen space; they are drawn onto the finished frame before it is shown,
 * so they scale, are widescreen-adjusted and anti-aliased like the HUD. */
typedef struct {
    const uint32_t *px;          /* BGRA, straight alpha, w*h; NULL = slot unused */
    int w, h;
    unsigned ver;                /* change when px changes */
    float x, y, dw, dh;          /* 640x480 game coordinates */
} XboxOverlayQuad;
#define OVL_MAX 8
int (*volatile xbox_overlay_cb)(XboxOverlayQuad *q, int max);
static struct { ID3D11Texture2D *tex; ID3D11ShaderResourceView *srv; int w, h; unsigned ver; } g_ovl[OVL_MAX];

static void gpu_draw_overlays(GSurf *s, int wide)
{
    XboxOverlayQuad q[OVL_MAX];
    int n, i;
    float k = wide ? g_aspect / (4.0f / 3.0f) : 1.0f;
    if (!xbox_overlay_cb || !s || !s->rtv1) return;
    memset(q, 0, sizeof q);
    n = xbox_overlay_cb(q, OVL_MAX);
    if (n <= 0) return;
    if (!g_menu_bs) {
        D3D11_BLEND_DESC bd;
        memset(&bd, 0, sizeof bd);
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(ID3D11Device_CreateBlendState(g_dev, &bd, &g_menu_bs))) return;
    }
    gpu_sh_reset();
    ID3D11DeviceContext_OMSetRenderTargets(g_ctx, 1, &s->rtv1, NULL);
    {
        D3D11_RECT sc = { 0, 0, (LONG)s->pw, (LONG)s->ph };
        ID3D11DeviceContext_RSSetScissorRects(g_ctx, 1, &sc);
        ID3D11DeviceContext_RSSetState(g_ctx, NULL);
    }
    ID3D11DeviceContext_OMSetBlendState(g_ctx, g_menu_bs, NULL, 0xFFFFFFFFu);
    ID3D11DeviceContext_OMSetDepthStencilState(g_ctx, NULL, 0);
    ID3D11DeviceContext_IASetInputLayout(g_ctx, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(g_ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(g_ctx, g_vs_present, NULL, 0);
    ID3D11DeviceContext_PSSetShader(g_ctx, g_ps_copy, NULL, 0);
    ID3D11DeviceContext_PSSetSamplers(g_ctx, 0, 1, &g_present_smp);
    for (i = 0; i < n && i < OVL_MAX; i++) {
        D3D11_VIEWPORT vp;
        float cb[4] = { 1, 1, 0, 0 }, x0, x1;
        if (!q[i].px || q[i].w <= 0 || q[i].h <= 0) continue;
        if (!g_ovl[i].tex || g_ovl[i].w != q[i].w || g_ovl[i].h != q[i].h) {
            D3D11_TEXTURE2D_DESC td;
            if (g_ovl[i].srv) ID3D11ShaderResourceView_Release(g_ovl[i].srv);
            if (g_ovl[i].tex) ID3D11Texture2D_Release(g_ovl[i].tex);
            memset(&g_ovl[i], 0, sizeof g_ovl[i]);
            memset(&td, 0, sizeof td);
            td.Width = (UINT)q[i].w; td.Height = (UINT)q[i].h; td.MipLevels = 1; td.ArraySize = 1;
            td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            if (FAILED(ID3D11Device_CreateTexture2D(g_dev, &td, NULL, &g_ovl[i].tex))) { g_ovl[i].tex = NULL; continue; }
            ID3D11Device_CreateShaderResourceView(g_dev, (ID3D11Resource *)g_ovl[i].tex, NULL, &g_ovl[i].srv);
            g_ovl[i].w = q[i].w; g_ovl[i].h = q[i].h; g_ovl[i].ver = q[i].ver - 1;
        }
        if (g_ovl[i].ver != q[i].ver) {
            ID3D11DeviceContext_UpdateSubresource(g_ctx, (ID3D11Resource *)g_ovl[i].tex, 0, NULL, q[i].px, (UINT)q[i].w * 4, 0);
            g_ovl[i].ver = q[i].ver;
        }
        /* HUD art keeps its 4:3 shape in a widened frame (as the game's own) */
        x0 = hud_wide_x(q[i].x, q[i].x + q[i].dw * 0.5f, q[i].y + q[i].dh * 0.5f, k);
        x1 = hud_wide_x(q[i].x + q[i].dw, q[i].x + q[i].dw * 0.5f, q[i].y + q[i].dh * 0.5f, k);
        vp.TopLeftX = x0 * (float)s->pw / (float)s->w;
        vp.TopLeftY = q[i].y * (float)s->ph / (float)s->h;
        vp.Width = (x1 - x0) * (float)s->pw / (float)s->w;
        vp.Height = q[i].dh * (float)s->ph / (float)s->h;
        vp.MinDepth = 0; vp.MaxDepth = 1;
        ID3D11DeviceContext_RSSetViewports(g_ctx, 1, &vp);
        ID3D11DeviceContext_UpdateSubresource(g_ctx, (ID3D11Resource *)g_cb_cl, 0, NULL, cb, 0, 0);
        ID3D11DeviceContext_VSSetConstantBuffers(g_ctx, 2, 1, &g_cb_cl);
        ID3D11DeviceContext_PSSetShaderResources(g_ctx, 0, 1, &g_ovl[i].srv);
        ID3D11DeviceContext_Draw(g_ctx, 3, 0);
    }
    {
        ID3D11ShaderResourceView *none = NULL;
        ID3D11DeviceContext_PSSetShaderResources(g_ctx, 0, 1, &none);
    }
    ID3D11DeviceContext_OMSetBlendState(g_ctx, NULL, NULL, 0xFFFFFFFFu);
    gpu_sh_reset();
}

/* A one-off back-buffer dump requested by the title (testing). */
static volatile char g_dump_req[260];
void xbox_PresentDumpRequest(const char *path)
{
    if (path && strlen(path) < sizeof g_dump_req - 1) { memcpy((char *)g_dump_req, path, strlen(path) + 1); }
}

/* The flip: put the finished surface on the window. */
static void gpu_present(uint32_t base, uint32_t pitch)
{
    hud_frame_end();   /* this frame's HUD pieces, grouped (hud_wide_x) */
    HWND hwnd = xbox_FramebufferHwnd();
    GSurf *s = gsurf_find(base, 0);
    D3D11_VIEWPORT vp;
    RECT rc;
    /* Frame dumps read the GPU's picture back into guest memory; only on
     * the frames the dumper will look at (a readback every frame halves the
     * frame rate). */
    if (getenv("RECOMP_FB_DUMP")) {
        static unsigned every, k;
        if (!every) {
            const char *e = getenv("RECOMP_FB_WINDOW_DUMP_EVERY");
            every = (e && atoi(e) > 0) ? (unsigned)atoi(e) / 4 : 15;
            if (!every) every = 1;
        }
        if (k++ % every == 0) gpu_readback(base, pitch);
    }
    if (!hwnd || !s) return;
    gsurf_sync_from_mem(s);
    gsurf_resolve(s);   /* (MSAA: the picture from its samples) */
    {
        extern int xbox_MoviePlaying(void);
        int wide = g_aspect > 1.34f && g_frame_3d && !xbox_MoviePlaying() && g_ws_cam_tick != 0;
        gpu_draw_overlays(s, wide);
    }
    /* this frame's texture set becomes "last frame" */
    g_fhash_cur ^= 1; g_fhash_n[g_fhash_cur] = 0;
    GetClientRect(hwnd, &rc);
    if (rc.right - rc.left < 8 || rc.bottom - rc.top < 8) return;   /* minimised */
    if (!g_sc || g_sc_hwnd != hwnd) {
        IDXGIDevice *dd = NULL; IDXGIAdapter *ad = NULL; IDXGIFactory *fa = NULL;
        DXGI_SWAP_CHAIN_DESC d;
        if (g_sc_rtv) { ID3D11RenderTargetView_Release(g_sc_rtv); g_sc_rtv = NULL; }
        if (g_sc) { IDXGISwapChain_Release(g_sc); g_sc = NULL; }
        ID3D11Device_QueryInterface(g_dev, &IID_IDXGIDevice, (void **)&dd);
        if (!dd) return;
        IDXGIDevice_GetAdapter(dd, &ad);
        if (ad) IDXGIAdapter_GetParent(ad, &IID_IDXGIFactory, (void **)&fa);
        memset(&d, 0, sizeof d);
        d.BufferDesc.Width = rc.right - rc.left; d.BufferDesc.Height = rc.bottom - rc.top;
        d.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d.SampleDesc.Count = 1; d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; d.BufferCount = 2;
        d.OutputWindow = hwnd; d.Windowed = TRUE;
        if (fa) {
            HRESULT hr;
            /* Flip model first (Windows 10+): lower latency, and a borderless
             * full-screen window gets the display to itself (independent
             * flip) the way exclusive full screen would. */
            d.SwapEffect = (DXGI_SWAP_EFFECT)4 /* DXGI_SWAP_EFFECT_FLIP_DISCARD */;
            hr = getenv("RECOMP_BLT_MODEL") ? E_FAIL : IDXGIFactory_CreateSwapChain(fa, (IUnknown *)g_dev, &d, &g_sc);
            if (FAILED(hr)) {
                d.SwapEffect = DXGI_SWAP_EFFECT_DISCARD; d.BufferCount = 1;
                hr = IDXGIFactory_CreateSwapChain(fa, (IUnknown *)g_dev, &d, &g_sc);
            }
            if (FAILED(hr)) { fprintf(stderr, "  [D3D11] CreateSwapChain failed: 0x%08lX\n", (unsigned long)hr); fflush(stderr); }
            else fprintf(stderr, "  [D3D11] presenting to the game window (%s model)\n",
                         d.SwapEffect == DXGI_SWAP_EFFECT_DISCARD ? "blt" : "flip");
            /* The window thread handles Alt+Enter itself (borderless full
             * screen), so DXGI must not switch modes behind its back. */
            IDXGIFactory_MakeWindowAssociation(fa, hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
        }
        if (fa) IDXGIFactory_Release(fa);
        if (ad) IDXGIAdapter_Release(ad);
        IDXGIDevice_Release(dd);
        if (!g_sc) return;
        g_sc_w = 0; g_sc_h = 0;
        g_sc_hwnd = hwnd;
        fflush(stderr);
    }
    if (!g_sc_rtv || g_sc_w != (UINT)(rc.right - rc.left) || g_sc_h != (UINT)(rc.bottom - rc.top)) {
        ID3D11Texture2D *bb = NULL;
        if (g_sc_rtv) { ID3D11RenderTargetView_Release(g_sc_rtv); g_sc_rtv = NULL; }
        ID3D11DeviceContext_OMSetRenderTargets(g_ctx, 0, NULL, NULL);
        g_sc_w = rc.right - rc.left; g_sc_h = rc.bottom - rc.top;
        if (FAILED(IDXGISwapChain_ResizeBuffers(g_sc, 0, g_sc_w, g_sc_h, DXGI_FORMAT_UNKNOWN, 0))) {
            fprintf(stderr, "  [D3D11] ResizeBuffers %ux%u failed\n", g_sc_w, g_sc_h); fflush(stderr);
        }
        IDXGISwapChain_GetBuffer(g_sc, 0, &IID_ID3D11Texture2D, (void **)&bb);
        if (bb) { ID3D11Device_CreateRenderTargetView(g_dev, (ID3D11Resource *)bb, NULL, &g_sc_rtv); ID3D11Texture2D_Release(bb); }
    }
    if (!g_sc_rtv) return;
    gpu_sh_reset();
    ID3D11DeviceContext_OMSetRenderTargets(g_ctx, 1, &g_sc_rtv, NULL);
    {
        /* Fit the picture: its shape is g_aspect (4:3 unless widescreen),
         * movies stay 4:3; black bars fill the rest. RECOMP_PRESENT_STRETCH
         * fills the window instead. */
        extern int xbox_MoviePlaying(void);
        float black[4] = { 0, 0, 0, 1 };
        int wide = g_aspect > 1.34f && g_frame_3d && !xbox_MoviePlaying()
                   && g_ws_cam_tick != 0;
        float a = wide ? g_aspect : 4.0f / 3.0f, ww = (float)g_sc_w, wh = (float)g_sc_h, pw, ph;
        extern volatile int xbox_frontend_hint;
        float mz = menu_fill_zoom((!g_frame_3d || xbox_frontend_hint == 1) && !xbox_MoviePlaying());
        g_wide_prev = wide; g_frame_3d = 0; g_present_n++;
        if (g_stretch_on < 0) g_stretch_on = getenv("RECOMP_PRESENT_STRETCH") != NULL;
        ID3D11DeviceContext_ClearRenderTargetView(g_ctx, g_sc_rtv, black);
        if (g_stretch_on) { pw = ww; ph = wh; }
        else if (ww / wh > a) { ph = wh; pw = wh * a; }
        else { pw = ww; ph = ww / a; }
        pw *= mz; ph *= mz;
        movie_fill(xbox_MoviePlaying(), ww, wh, &pw, &ph);
        vp.TopLeftX = (float)(int)((ww - pw) * 0.5f); vp.TopLeftY = (float)(int)((wh - ph) * 0.5f);
        vp.Width = pw; vp.Height = ph; vp.MinDepth = 0; vp.MaxDepth = 1;
        present_map_set(vp.TopLeftX / ww, vp.TopLeftY / wh, pw / ww, ph / wh, wide ? g_aspect / (4.0f / 3.0f) : 1.0f);
    }
    ID3D11DeviceContext_RSSetViewports(g_ctx, 1, &vp);
    {
        D3D11_RECT sc = { 0, 0, (LONG)g_sc_w, (LONG)g_sc_h };
        uint64_t k[3] = { 0xFFFFFFFFFFFFFFFFull, 1, 0 };
        ID3D11RasterizerState *rs = (ID3D11RasterizerState *)gstate_find(g_rs_c, k);
        float uvs[4];
        ID3D11DeviceContext_RSSetScissorRects(g_ctx, 1, &sc);
        if (rs) ID3D11DeviceContext_RSSetState(g_ctx, rs);
        else ID3D11DeviceContext_RSSetState(g_ctx, NULL);
        ID3D11DeviceContext_OMSetBlendState(g_ctx, NULL, NULL, 0xFFFFFFFFu);
        ID3D11DeviceContext_OMSetDepthStencilState(g_ctx, NULL, 0);
        /* uv scale: the visible 640x480 of a possibly larger surface */
        uvs[0] = 640.0f / (float)s->w; uvs[1] = 480.0f / (float)s->h;
        uvs[2] = 1.0f / (float)s->pw; uvs[3] = 1.0f / (float)s->ph;
        ID3D11DeviceContext_UpdateSubresource(g_ctx, (ID3D11Resource *)g_cb_cl, 0, NULL, uvs, 0, 0);
    }
    ID3D11DeviceContext_IASetInputLayout(g_ctx, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(g_ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(g_ctx, g_vs_present, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(g_ctx, 2, 1, &g_cb_cl);
    {
        if (g_aa_on < 0) g_aa_on = xbox_GfxFxaa();
        ID3D11DeviceContext_PSSetShader(g_ctx, (g_aa_on && g_ps_fxaa) ? g_ps_fxaa : g_ps_present, NULL, 0);
        ID3D11DeviceContext_PSSetConstantBuffers(g_ctx, 2, 1, &g_cb_cl);
        ID3D11DeviceContext_PSSetConstantBuffers(g_ctx, 3, 1, &g_cb_post);
    }
    ID3D11DeviceContext_PSSetShaderResources(g_ctx, 0, 1, &s->srv);
    ID3D11DeviceContext_PSSetSamplers(g_ctx, 0, 1, &g_present_smp);
    ID3D11DeviceContext_Draw(g_ctx, 3, 0);
    {
        ID3D11ShaderResourceView *none = NULL;
        ID3D11DeviceContext_PSSetShaderResources(g_ctx, 0, 1, &none);
    }
    {
        const uint32_t *mpx; int mw, mh; unsigned mver;
        xbox_GfxMenuSetAvailable(1);
        if (xbox_GfxMenuRender((int)g_sc_w, (int)g_sc_h, &mpx, &mw, &mh, &mver)) gpu_draw_menu(mpx, mw, mh, mver);
    }
    {
        HRESULT hr;
        {
            /* RECOMP_PRESENT_DUMP=prefix: every 300th presented frame as a
             * BMP of the window's back buffer (testing the final picture). */
            static const char *pd = (const char *)1; static unsigned pk, seq;
            if (pd == (const char *)1) pd = getenv("RECOMP_PRESENT_DUMP");
            static int pevery;
            int req = g_dump_req[0] != 0;
            if (!pevery) { const char *e = getenv("RECOMP_PRESENT_DUMP_EVERY"); pevery = e && atoi(e) > 0 ? atoi(e) : 300; }
            if (req || (pd && pk++ % pevery == (unsigned)pevery / 2)) {
                ID3D11Texture2D *bb = NULL, *st = NULL;
                IDXGISwapChain_GetBuffer(g_sc, 0, &IID_ID3D11Texture2D, (void **)&bb);
                if (bb) {
                    D3D11_TEXTURE2D_DESC td; D3D11_MAPPED_SUBRESOURCE ms;
                    ID3D11Texture2D_GetDesc(bb, &td);
                    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; td.MiscFlags = 0;
                    if (SUCCEEDED(ID3D11Device_CreateTexture2D(g_dev, &td, NULL, &st))) {
                        ID3D11DeviceContext_CopyResource(g_ctx, (ID3D11Resource *)st, (ID3D11Resource *)bb);
                        if (SUCCEEDED(ID3D11DeviceContext_Map(g_ctx, (ID3D11Resource *)st, 0, D3D11_MAP_READ, 0, &ms))) {
                            char nm[260]; FILE *o; uint32_t y;
                            if (req) { snprintf(nm, sizeof nm, "%s", (const char *)g_dump_req); g_dump_req[0] = 0; }
                            else snprintf(nm, sizeof nm, "%s%03u.bmp", pd, seq++);
                            if ((o = fopen(nm, "wb"))) {
                                uint8_t h[54] = { 'B', 'M' }; uint32_t row = td.Width * 4, img = row * td.Height, tot = img + 54;
                                int32_t hh = -(int32_t)td.Height;
                                memcpy(h + 2, &tot, 4); h[10] = 54; h[14] = 40; memcpy(h + 18, &td.Width, 4); memcpy(h + 22, &hh, 4);
                                h[26] = 1; h[28] = 32; memcpy(h + 34, &img, 4);
                                fwrite(h, 1, 54, o);
                                for (y = 0; y < td.Height; y++) fwrite((uint8_t *)ms.pData + (size_t)y * ms.RowPitch, 1, row, o);
                                fclose(o);
                            }
                            ID3D11DeviceContext_Unmap(g_ctx, (ID3D11Resource *)st, 0);
                        }
                        ID3D11Texture2D_Release(st);
                    }
                    ID3D11Texture2D_Release(bb);
                }
            }
        }
        if (g_vsync_on < 0) { const char *v = getenv("RECOMP_VSYNC"); g_vsync_on = v && v[0] == '1'; }
        {
            /* (1.8) On Vulkan (DXVK) the picture goes out on the display's
             * refresh even with vsync off: presenting at once (immediate mode)
             * left the window black on some laptops (a GeForce drawing for a
             * display on the other graphics chip). RECOMP_VULKAN_IMMEDIATE=1:
             * at once, as Direct3D 11. */
            static int vk_sync = -1;
            if (vk_sync < 0) {
                vk_sync = s_d3d11_vulkan && !getenv("RECOMP_VULKAN_IMMEDIATE");
                if (vk_sync && !g_vsync_on) { fprintf(stderr, "  [D3D11] Vulkan: presenting on the display's refresh\n"); fflush(stderr); }
            }
            hr = IDXGISwapChain_Present(g_sc, (g_vsync_on || vk_sync) ? 1 : 0, 0);
        }
        static int said;
        if (FAILED(hr)) {
            HRESULT why = ID3D11Device_GetDeviceRemovedReason(g_dev);
            if (said++ < 5)
                fprintf(stderr, "  [D3D11] Present failed: 0x%08lX (device state 0x%08lX)\n",
                        (unsigned long)hr, (unsigned long)why);
            if (FAILED(why)) {
                /* The graphics card dropped the device (driver reset, crash
                 * in the driver): carry on with the CPU renderer. */
                fprintf(stderr, "  [D3D11] graphics device lost; switching to the software renderer\n");
                s_gpu_mode = 0;
                xbox_FramebufferSetGpuOwned(0);
                xbox_GfxMenuSetAvailable(0);
            }
            fflush(stderr);
        }
    }
    if (g_msaa_bad && g_msaa > 1) {
        fprintf(stderr, "  [D3D11] MSAA: a multisampled surface could not be made (video memory?): MSAA off\n"); fflush(stderr);
        gpu_apply_settings();
    }
    if (xbox_gfx_rev != g_gfx_seen) gpu_apply_settings();
}
