/* d3dcompile_dyn.h -- the HLSL compiler (D3DCompile), loaded at run time.
 * Include after <d3dcompiler.h>. */
#ifndef XBOXRECOMP_D3DCOMPILE_DYN_H
#define XBOXRECOMP_D3DCOMPILE_DYN_H
#include <stdio.h>
#include <string.h>
#include <wchar.h>
/* The HLSL compiler, loaded at run time. d3dcompiler_47 is part of Windows
 * 8.1 and later; Windows 7 has it only with KB4019990 or beside a program
 * that ships it, but most have the DirectX redistributable's 43. A static
 * import of 47 stopped the game from starting on a Windows 7 without it;
 * now the newest one present is used, and with none the software renderer
 * draws instead (and says why). */
typedef HRESULT (WINAPI *PFN_tt_D3DCompile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *, ID3DInclude *,
                                            LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);
static PFN_tt_D3DCompile s_d3dcompile;
typedef HRESULT (WINAPI *PFN_tt_D3DCreateBlob)(SIZE_T, ID3DBlob **);
static PFN_tt_D3DCreateBlob s_d3dcreateblob;
static const char *s_d3dcompiler_name = "";
static int d3dcompiler_load(void)
{
    static int tried;
    static const char *const dlls[] = { "d3dcompiler_47.dll", "d3dcompiler_46.dll", "d3dcompiler_45.dll",
                                        "d3dcompiler_44.dll", "d3dcompiler_43.dll" };
    int i;
    if (tried) return s_d3dcompile != NULL;
    tried = 1;
    for (i = 0; i < 5 && !s_d3dcompile; i++) {
        HMODULE m = LoadLibraryA(dlls[i]);
        if (m) {
            s_d3dcompile = (PFN_tt_D3DCompile)(void *)GetProcAddress(m, "D3DCompile");
            s_d3dcreateblob = (PFN_tt_D3DCreateBlob)(void *)GetProcAddress(m, "D3DCreateBlob");
        }
        if (s_d3dcompile) { s_d3dcompiler_name = dlls[i]; fprintf(stderr, "  [D3D11] shader compiler: %s\n", dlls[i]); }
    }
    if (!s_d3dcompile)
        fprintf(stderr, "  [D3D11] no HLSL compiler (d3dcompiler_47.dll or _43.dll); using the software renderer. "
                        "Installing the DirectX End-User Runtime gives one.\n");
    return s_d3dcompile != NULL;
}

/* (1.8) The compiled shaders are kept on disk (shadercache\ beside the game):
 * the game's vertex programs and combiners are turned into HLSL as they are
 * first met, and compiling one took a few milliseconds to tens of them -- a
 * stutter each time something new came on screen. Met again, in this run or
 * the next, the compiled code is read back instead. Keyed by everything that
 * goes into the compile (the source, the entry point, the target, the flags,
 * the macros and the compiler); RECOMP_SHADER_CACHE=0 turns it off. */
static unsigned long long tt_sc_mix(unsigned long long h, const void *p, size_t n)
{
    const unsigned char *b = (const unsigned char *)p;
    size_t i;
    for (i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}
static int tt_sc_path(unsigned long long key, wchar_t *out, size_t cap)
{
    static wchar_t dir[MAX_PATH + 16];
    static int state;   /* 0 not tried, 1 ready, -1 off */
    if (!state) {
        const char *e = getenv("RECOMP_SHADER_CACHE");
        DWORD n;
        wchar_t *sl;
        state = -1;
        if (!(e && e[0] == '0') && (n = GetModuleFileNameW(NULL, dir, MAX_PATH)) > 0 && n < MAX_PATH) {
            sl = wcsrchr(dir, L'\\');
            if (sl) {
                wcscpy(sl + 1, L"shadercache");
                CreateDirectoryW(dir, NULL);
                if (GetFileAttributesW(dir) != INVALID_FILE_ATTRIBUTES) state = 1;
            }
        }
    }
    if (state < 0) return 0;
    _snwprintf(out, cap, L"%ls\\%016llx.dxbc", dir, key);
    out[cap - 1] = 0;
    return 1;
}
static HRESULT tt_D3DCompile(LPCVOID src, SIZE_T n, LPCSTR name, const D3D_SHADER_MACRO *defs, ID3DInclude *inc,
                             LPCSTR entry, LPCSTR target, UINT f1, UINT f2, ID3DBlob **code, ID3DBlob **err)
{
    unsigned long long key = 14695981039346656037ull;
    wchar_t path[MAX_PATH + 64];
    int cached;
    HRESULT hr;
    if (!d3dcompiler_load()) { if (code) *code = NULL; if (err) *err = NULL; return E_NOTIMPL; }
    key = tt_sc_mix(key, "tt-sc1", 6);
    key = tt_sc_mix(key, s_d3dcompiler_name, strlen(s_d3dcompiler_name));
    key = tt_sc_mix(key, src, n);
    key = tt_sc_mix(key, entry ? entry : "", entry ? strlen(entry) + 1 : 1);
    key = tt_sc_mix(key, target ? target : "", target ? strlen(target) + 1 : 1);
    key = tt_sc_mix(key, &f1, sizeof f1);
    key = tt_sc_mix(key, &f2, sizeof f2);
    for (; defs && defs->Name; defs++) {
        key = tt_sc_mix(key, defs->Name, strlen(defs->Name) + 1);
        if (defs->Definition) key = tt_sc_mix(key, defs->Definition, strlen(defs->Definition) + 1);
    }
    cached = !inc && code && s_d3dcreateblob && tt_sc_path(key, path, sizeof path / sizeof path[0]);
    if (cached) {   /* compiled before? */
        HANDLE fh = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (fh != INVALID_HANDLE_VALUE) {
            DWORD sz = GetFileSize(fh, NULL), got = 0;
            ID3DBlob *b = NULL;
            if (sz > 32 && sz < (64u << 20) && SUCCEEDED(s_d3dcreateblob(sz, &b))) {
                if (ReadFile(fh, b->lpVtbl->GetBufferPointer(b), sz, &got, NULL) && got == sz
                    && !memcmp(b->lpVtbl->GetBufferPointer(b), "DXBC", 4)) {
                    CloseHandle(fh);
                    *code = b;
                    if (err) *err = NULL;
                    return S_OK;
                }
                b->lpVtbl->Release(b);
            }
            CloseHandle(fh);
        }
    }
    hr = s_d3dcompile(src, n, name, defs, inc, entry, target, f1, f2, code, err);
    if (cached && SUCCEEDED(hr) && *code) {   /* kept for next time (written aside, then put in place) */
        wchar_t tmp[MAX_PATH + 80];
        HANDLE fh;
        DWORD put = 0, sz = (DWORD)(*code)->lpVtbl->GetBufferSize(*code);
        _snwprintf(tmp, sizeof tmp / sizeof tmp[0], L"%ls.%lu.tmp", path, (unsigned long)GetCurrentThreadId());
        tmp[sizeof tmp / sizeof tmp[0] - 1] = 0;
        fh = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (fh != INVALID_HANDLE_VALUE) {
            BOOL ok = WriteFile(fh, (*code)->lpVtbl->GetBufferPointer(*code), sz, &put, NULL) && put == sz;
            CloseHandle(fh);
            if (!ok || !MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING)) DeleteFileW(tmp);
        }
    }
    return hr;
}
#define D3DCompile tt_D3DCompile
#endif
