/* mf_dyn.h -- Media Foundation, loaded at run time.
 *
 * The N editions of Windows (and Windows 7 N/KN without the Media Feature
 * Pack) have no Media Foundation; a static import of mfplat.dll stopped the
 * game from starting there at all. Include after <mfapi.h> and
 * <mfreadwrite.h>: the five functions used go through these, which fail
 * with E_NOTIMPL when it is not installed (the callers treat that like any
 * other failure -- no movie, no music from that file). */
#ifndef XBOXRECOMP_MF_DYN_H
#define XBOXRECOMP_MF_DYN_H
#include <windows.h>
#include <stdio.h>

typedef HRESULT (WINAPI *mfdyn_Startup_t)(ULONG, DWORD);
typedef HRESULT (WINAPI *mfdyn_Shutdown_t)(void);
typedef HRESULT (WINAPI *mfdyn_CreateAttributes_t)(IMFAttributes **, UINT32);
typedef HRESULT (WINAPI *mfdyn_CreateMediaType_t)(IMFMediaType **);
typedef HRESULT (WINAPI *mfdyn_CreateSourceReaderFromURL_t)(LPCWSTR, IMFAttributes *, IMFSourceReader **);

static struct {
    int tried;
    mfdyn_Startup_t startup;
    mfdyn_Shutdown_t shutdown;
    mfdyn_CreateAttributes_t attrs;
    mfdyn_CreateMediaType_t mtype;
    mfdyn_CreateSourceReaderFromURL_t reader;
} s_mfdyn;

static int mfdyn_load(void)
{
    if (!s_mfdyn.tried) {
        HMODULE p = LoadLibraryA("mfplat.dll"), r = LoadLibraryA("mfreadwrite.dll");
        s_mfdyn.tried = 1;
        if (p) {
            s_mfdyn.startup = (mfdyn_Startup_t)(void *)GetProcAddress(p, "MFStartup");
            s_mfdyn.shutdown = (mfdyn_Shutdown_t)(void *)GetProcAddress(p, "MFShutdown");
            s_mfdyn.attrs = (mfdyn_CreateAttributes_t)(void *)GetProcAddress(p, "MFCreateAttributes");
            s_mfdyn.mtype = (mfdyn_CreateMediaType_t)(void *)GetProcAddress(p, "MFCreateMediaType");
        }
        if (r) s_mfdyn.reader = (mfdyn_CreateSourceReaderFromURL_t)(void *)GetProcAddress(r, "MFCreateSourceReaderFromURL");
        if (!s_mfdyn.startup || !s_mfdyn.reader) {
            fprintf(stderr, "  [MF] Media Foundation is not installed (Windows N edition?)\n");
            fflush(stderr);
        }
    }
    return s_mfdyn.startup && s_mfdyn.shutdown && s_mfdyn.attrs && s_mfdyn.mtype && s_mfdyn.reader;
}
static HRESULT mfdyn_MFStartup(ULONG v, DWORD f) { return mfdyn_load() ? s_mfdyn.startup(v, f) : E_NOTIMPL; }
static HRESULT mfdyn_MFShutdown(void) { return mfdyn_load() ? s_mfdyn.shutdown() : E_NOTIMPL; }
static HRESULT mfdyn_MFCreateAttributes(IMFAttributes **a, UINT32 n) { return mfdyn_load() ? s_mfdyn.attrs(a, n) : E_NOTIMPL; }
static HRESULT mfdyn_MFCreateMediaType(IMFMediaType **t) { return mfdyn_load() ? s_mfdyn.mtype(t) : E_NOTIMPL; }
static HRESULT mfdyn_MFCreateSourceReaderFromURL(LPCWSTR u, IMFAttributes *a, IMFSourceReader **r)
{
    return mfdyn_load() ? s_mfdyn.reader(u, a, r) : E_NOTIMPL;
}
#define MFStartup mfdyn_MFStartup
#define MFShutdown mfdyn_MFShutdown
#define MFCreateAttributes mfdyn_MFCreateAttributes
#define MFCreateMediaType mfdyn_MFCreateMediaType
#define MFCreateSourceReaderFromURL mfdyn_MFCreateSourceReaderFromURL
#endif
