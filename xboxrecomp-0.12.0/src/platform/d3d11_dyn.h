/* d3d11_dyn.h -- Direct3D 11 loaded at run time, not imported.
 *
 * [Display] renderer=vulkan (RECOMP_RENDERER=vulkan) runs the Direct3D 11
 * renderer on Vulkan through DXVK: its d3d11.dll and dxgi.dll, put in a
 * "dxvk" folder beside the game, are loaded instead of Windows' own. For that
 * the game must not import d3d11.dll or dxgi.dll itself (an imported dxgi.dll
 * would be the one DXVK's d3d11.dll got, and DXVK needs its own), so both are
 * loaded here: DXVK's when asked for and present, else the system's. */
#ifndef D3D11_DYN_H
#define D3D11_DYN_H
#if defined(_WIN32)
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <d3d11.h>

typedef HRESULT (WINAPI *PFN_tt_D3D11CreateDevice)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
typedef HRESULT (WINAPI *PFN_tt_D3D11CreateDeviceAndSwapChain)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL *, UINT, UINT, const DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **, ID3D11Device **,
    D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

/* 1 when the renderer runs on Vulkan (DXVK loaded) */
static int s_d3d11_vulkan;

static HMODULE d3d11_dyn_module(void)
{
    static HMODULE m;
    static int tried;
    if (tried) return m;
    tried = 1;
    {
        const char *r = getenv("RECOMP_RENDERER");
        if (r && (!_stricmp(r, "vulkan") || !_stricmp(r, "dxvk"))) {
            wchar_t dir[MAX_PATH], p[MAX_PATH + 32], *s;
            DWORD n = GetModuleFileNameW(NULL, dir, MAX_PATH);
            if (n && n < MAX_PATH && (s = wcsrchr(dir, L'\\')) != NULL) {
                HMODULE g;
                *s = 0;
                /* dxgi first: DXVK's d3d11.dll then finds it already loaded */
                _snwprintf(p, MAX_PATH + 32, L"%ls\\dxvk\\dxgi.dll", dir); p[MAX_PATH + 31] = 0;
                g = LoadLibraryExW(p, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
                _snwprintf(p, MAX_PATH + 32, L"%ls\\dxvk\\d3d11.dll", dir); p[MAX_PATH + 31] = 0;
                if (g) m = LoadLibraryExW(p, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
                if (m) {
                    s_d3d11_vulkan = 1;
                    fprintf(stderr, "  [D3D11] Vulkan: DXVK's d3d11.dll and dxgi.dll from the dxvk folder\n");
                } else {
                    if (g) FreeLibrary(g);
                    fprintf(stderr, "  [D3D11] renderer=vulkan, but no DXVK in the dxvk folder (dxvk\\d3d11.dll and "
                                    "dxvk\\dxgi.dll, x64): Direct3D 11 as usual\n");
                }
            }
        }
    }
    if (!m) m = LoadLibraryA("d3d11.dll");
    fflush(stderr);
    return m;
}

static HRESULT WINAPI tt_D3D11CreateDevice(IDXGIAdapter *a, D3D_DRIVER_TYPE t, HMODULE sw, UINT fl,
    const D3D_FEATURE_LEVEL *lv, UINT nlv, UINT sdk, ID3D11Device **dev, D3D_FEATURE_LEVEL *got, ID3D11DeviceContext **ctx)
{
    HMODULE m = d3d11_dyn_module();
    PFN_tt_D3D11CreateDevice f = m ? (PFN_tt_D3D11CreateDevice)(void *)GetProcAddress(m, "D3D11CreateDevice") : NULL;
    if (!f) return E_NOTIMPL;
    return f(a, t, sw, fl, lv, nlv, sdk, dev, got, ctx);
}
static HRESULT WINAPI tt_D3D11CreateDeviceAndSwapChain(IDXGIAdapter *a, D3D_DRIVER_TYPE t, HMODULE sw, UINT fl,
    const D3D_FEATURE_LEVEL *lv, UINT nlv, UINT sdk, const DXGI_SWAP_CHAIN_DESC *sd, IDXGISwapChain **sc,
    ID3D11Device **dev, D3D_FEATURE_LEVEL *got, ID3D11DeviceContext **ctx)
{
    HMODULE m = d3d11_dyn_module();
    PFN_tt_D3D11CreateDeviceAndSwapChain f = m ? (PFN_tt_D3D11CreateDeviceAndSwapChain)(void *)GetProcAddress(m, "D3D11CreateDeviceAndSwapChain") : NULL;
    if (!f) return E_NOTIMPL;
    return f(a, t, sw, fl, lv, nlv, sdk, sd, sc, dev, got, ctx);
}
#define D3D11CreateDevice tt_D3D11CreateDevice
#define D3D11CreateDeviceAndSwapChain tt_D3D11CreateDeviceAndSwapChain
#endif
#endif
