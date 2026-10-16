#include <stdlib.h>
/**
 * XAudio2 Audio Output Backend
 *
 * Provides low-latency audio output via XAudio2 (Win7+).
 * Called from the APU monitor frame to submit mixed samples.
 * Falls back gracefully if XAudio2 is unavailable.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "apu_xaudio2.h"

/* The XAudio2 backend is Windows-only. On Linux all xa2_* functions are
 * stubbed to report inactive; real audio output via SDL2 comes later. */
#if defined(_WIN32)

#define COBJMACROS
#include <windows.h>
#include <xaudio2.h>

#pragma comment(lib, "ole32.lib")

#define XA2_SAMPLE_RATE   48000
#define XA2_CHANNELS      2
#define XA2_BUF_SAMPLES   1024   /* ~21ms per submission */
#define XA2_NUM_BUFS      16  /* 256-sample periods: ~85 ms of queue */

static IXAudio2               *g_xa2 = NULL;
static IXAudio2MasteringVoice *g_xa2_master = NULL;
static IXAudio2SourceVoice    *g_xa2_source = NULL;
static int16_t                 g_xa2_bufs[XA2_NUM_BUFS][XA2_BUF_SAMPLES][2];
static int                     g_xa2_next_buf = 0;
static int                     g_xa2_initialized = 0;
static int                     g_xa2_frames_written = 0;

/* Recovering lost output.
 *
 * XAudio2 stops for good when its device goes away -- headphones unplugged,
 * the default device switched, an HDMI screen going to sleep, a driver
 * reset. It reports that once, through OnCriticalError, and from then on the
 * source voice consumes nothing: the queue stays full, every submit is
 * dropped, and the game plays on in silence with its music stream still
 * being read. Nothing here listened, so sound never came back.
 *
 * Now a critical error, a failed submit, or a queue that stays full and does
 * not drain for 750 ms rebuilds the engine on the default device (at most
 * once every two seconds), and says so in the log. */
static volatile LONG g_xa2_critical = 0;
static DWORD         g_xa2_full_since = 0;
static DWORD         g_xa2_last_rebuild = 0;
static int           g_xa2_rebuilds = 0;

static void STDMETHODCALLTYPE xa2_cb_pass_start(IXAudio2EngineCallback *t) { (void)t; }
static void STDMETHODCALLTYPE xa2_cb_pass_end(IXAudio2EngineCallback *t) { (void)t; }
static void STDMETHODCALLTYPE xa2_cb_critical(IXAudio2EngineCallback *t, HRESULT err)
{
    (void)t;
    InterlockedExchange(&g_xa2_critical, (LONG)err ? (LONG)err : 1);
}
static IXAudio2EngineCallbackVtbl g_xa2_cb_vtbl = { xa2_cb_pass_start, xa2_cb_pass_end, xa2_cb_critical };
static IXAudio2EngineCallback     g_xa2_cb = { &g_xa2_cb_vtbl };

int xa2_init(void)
{
    HRESULT hr;
    int com_initialized;
    WAVEFORMATEX wfx = { 0 };

    if (g_xa2_initialized) return 1;
    {
        /* RECOMP_AUDIO_OUT=0: emulate the APU but play nothing. For hosts
         * with no audio endpoint, where creating the mastering voice is not
         * guaranteed to fail cleanly (Wine without a sound driver faults). */
        const char *out = getenv("RECOMP_AUDIO_OUT");
        if (out && out[0] == '0') {
            fprintf(stderr, "[XA2] audio output disabled (RECOMP_AUDIO_OUT=0)\n");
            return 0;
        }
    }

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        fprintf(stderr, "[XA2] CoInitializeEx failed: 0x%08lX\n", hr);
        return 0;
    }
    com_initialized = SUCCEEDED(hr);

    {
        /* Loaded at run time: XAudio2 2.9 is in Windows 10 and later, 2.8 in
         * Windows 8 / 8.1; Windows 7 has neither (only the DirectX
         * redistributable's COM-based 2.7), and a static import of either
         * stopped the game from starting there at all. Without them the
         * waveOut path below plays the sound instead. */
        typedef HRESULT (WINAPI *PFN_XAudio2Create)(IXAudio2 **, UINT32, XAUDIO2_PROCESSOR);
        static PFN_XAudio2Create create;
        static int tried;
        if (!tried) {
            static const char *const dlls[] = { "XAudio2_9.dll", "XAudio2_8.dll", "XAudio2_9redist.dll" };
            int i;
            tried = 1;
            for (i = 0; i < 3 && !create; i++) {
                HMODULE m = LoadLibraryA(dlls[i]);
                if (m) create = (PFN_XAudio2Create)(void *)GetProcAddress(m, "XAudio2Create");
                if (create) fprintf(stderr, "[XA2] using %s\n", dlls[i]);
            }
        }
        hr = create ? create(&g_xa2, 0, XAUDIO2_DEFAULT_PROCESSOR) : E_NOTIMPL;
    }
    if (FAILED(hr) || !g_xa2) {
        fprintf(stderr, "[XA2] XAudio2Create failed: 0x%08lX\n", hr);
        goto fail;
    }

    IXAudio2_RegisterForCallbacks(g_xa2, &g_xa2_cb);
    InterlockedExchange(&g_xa2_critical, 0);
    g_xa2_full_since = 0;

    hr = IXAudio2_CreateMasteringVoice(g_xa2, &g_xa2_master,
        XA2_CHANNELS, XA2_SAMPLE_RATE, 0, NULL, NULL, 0);
    if (FAILED(hr)) {
        fprintf(stderr, "[XA2] CreateMasteringVoice failed: 0x%08lX\n", hr);
        goto fail;
    }

    wfx.wFormatTag      = WAVE_FORMAT_PCM;
    wfx.nChannels       = XA2_CHANNELS;
    wfx.nSamplesPerSec  = XA2_SAMPLE_RATE;
    wfx.wBitsPerSample  = 16;
    wfx.nBlockAlign     = XA2_CHANNELS * 2;
    wfx.nAvgBytesPerSec = XA2_SAMPLE_RATE * wfx.nBlockAlign;

    hr = IXAudio2_CreateSourceVoice(g_xa2, &g_xa2_source,
        &wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO, NULL, NULL, NULL);
    if (FAILED(hr)) {
        fprintf(stderr, "[XA2] CreateSourceVoice failed: 0x%08lX\n", hr);
        goto fail;
    }

    hr = IXAudio2SourceVoice_Start(g_xa2_source, 0, XAUDIO2_COMMIT_NOW);
    if (FAILED(hr)) {
        fprintf(stderr, "[XA2] Start failed: 0x%08lX\n", hr);
        goto fail;
    }

    g_xa2_next_buf = 0;
    g_xa2_initialized = 1;
    g_xa2_frames_written = 0;

    fprintf(stderr, "[XA2] XAudio2 initialized (%d Hz stereo 16-bit, %d x %d-sample buffers)\n",
            XA2_SAMPLE_RATE, XA2_NUM_BUFS, XA2_BUF_SAMPLES);
    return 1;

fail:
    xa2_shutdown();
    /* Failed initialization still runs on the COM-initializing thread. */
    if (com_initialized) CoUninitialize();
    return 0;
}

void xa2_shutdown(void)
{
    if (g_xa2_source) {
        IXAudio2SourceVoice_Stop(g_xa2_source, 0, XAUDIO2_COMMIT_NOW);
        IXAudio2SourceVoice_FlushSourceBuffers(g_xa2_source);
        g_xa2_source->lpVtbl->DestroyVoice(g_xa2_source);
        g_xa2_source = NULL;
    }
    if (g_xa2_master) {
        g_xa2_master->lpVtbl->DestroyVoice(g_xa2_master);
        g_xa2_master = NULL;
    }
    if (g_xa2) {
        IXAudio2_UnregisterForCallbacks(g_xa2, &g_xa2_cb);
        IXAudio2_Release(g_xa2);
        g_xa2 = NULL;
    }

    if (g_xa2_initialized)
        fprintf(stderr, "[XA2] Shut down (%d frames written)\n", g_xa2_frames_written);
    g_xa2_initialized = 0;
}

int xa2_is_active(void)
{
    return g_xa2_initialized;
}

/* Tear the engine down and bring it up again on the current default device.
 * Rate-limited: a host with no working device would otherwise retry every
 * call. */
static void xa2_rebuild(void)
{
    DWORD now = GetTickCount();
    if (g_xa2_last_rebuild && now - g_xa2_last_rebuild < 2000) return;
    g_xa2_last_rebuild = now ? now : 1;
    g_xa2_rebuilds++;
    xa2_shutdown();
    if (xa2_init())
        fprintf(stderr, "[XA2] sound output restarted (%d so far)\n", g_xa2_rebuilds);
    else
        fprintf(stderr, "[XA2] sound output could not be restarted; will try again\n");
    fflush(stderr);
}

/* Submit a buffer of mixed samples to XAudio2.
 * Called from APU frame thread. Returns 1 if buffer was submitted. */
int xa2_submit_samples(const int16_t *samples, int num_samples)
{
    XAUDIO2_VOICE_STATE state;
    XAUDIO2_BUFFER xbuf;
    int idx;
    int copy_samples;
    HRESULT hr;

    if (!g_xa2_initialized || !g_xa2_source) return 0;

    {
        LONG crit = InterlockedExchange(&g_xa2_critical, 0);
        if (crit) {
            fprintf(stderr, "[XA2] audio device error 0x%08lX: restarting the sound output\n", (unsigned long)crit);
            fflush(stderr);
            xa2_rebuild();
            if (!g_xa2_initialized || !g_xa2_source) return 0;
        }
    }

    IXAudio2SourceVoice_GetState(g_xa2_source, &state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    if ((int)state.BuffersQueued >= XA2_NUM_BUFS) {
        /* Full is normal for a moment (we run ahead); full for 750 ms means
         * nothing is consuming it any more. */
        DWORD now = GetTickCount();
        if (!g_xa2_full_since) g_xa2_full_since = now ? now : 1;
        else if (now - g_xa2_full_since > 750) {
            fprintf(stderr, "[XA2] sound output stopped consuming (queue full for %lu ms): restarting it\n",
                    (unsigned long)(now - g_xa2_full_since));
            fflush(stderr);
            xa2_rebuild();
        }
        return 0;
    }
    g_xa2_full_since = 0;

    idx = g_xa2_next_buf;
    copy_samples = (num_samples > XA2_BUF_SAMPLES) ? XA2_BUF_SAMPLES : num_samples;
    memcpy(g_xa2_bufs[idx], samples, copy_samples * XA2_CHANNELS * sizeof(int16_t));

    memset(&xbuf, 0, sizeof(xbuf));
    xbuf.AudioBytes = copy_samples * XA2_CHANNELS * sizeof(int16_t);
    xbuf.pAudioData = (const BYTE *)g_xa2_bufs[idx];

    hr = IXAudio2SourceVoice_SubmitSourceBuffer(g_xa2_source, &xbuf, NULL);
    if (FAILED(hr)) {
        fprintf(stderr, "[XA2] submit failed 0x%08lX: restarting the sound output\n", (unsigned long)hr);
        fflush(stderr);
        xa2_rebuild();
        return 0;
    }

    g_xa2_next_buf = (idx + 1) % XA2_NUM_BUFS;
    g_xa2_frames_written++;
    return 1;
}

int xa2_queued_periods(void)
{
    XAUDIO2_VOICE_STATE state;
    if (!g_xa2_initialized || !g_xa2_source) return -1;
    IXAudio2SourceVoice_GetState(g_xa2_source, &state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    return (int)state.BuffersQueued;
}

int xa2_get_buffer_size(void)
{
    return XA2_BUF_SAMPLES;
}

#else /* !_WIN32 -- SDL2 audio output */

/* The same contract as the XAudio2 path above: 48 kHz stereo 16-bit, pushed
 * from the APU's frame thread, with about 85 ms of queue -- a submit that
 * would go past that is refused (the caller runs ahead of the device and
 * simply drops that period, as with a full XAudio2 queue). SDL's queue is
 * the buffer: SDL_QueueAudio copies, the device thread drains it.
 *
 * Recovery, as for XAudio2: a device that stops playing (unplugged, a sound
 * server restart) or a queue that stays full and does not drain for 750 ms
 * reopens the default device, at most once every two seconds. */
#include <SDL.h>
#include <time.h>

#define SDL_A_RATE      48000
#define SDL_A_CHANNELS  2
#define SDL_A_BUF       1024            /* largest single submit, frames */
#define SDL_A_QUEUE     (16 * 256 * 4)  /* bytes: 16 periods of 256 frames */

extern void xbox_SDLInitLock(int lock);

static SDL_AudioDeviceID g_sdl_dev;
static int      g_sdl_init, g_sdl_frames, g_sdl_rebuilds, g_sdl_sub;
static uint32_t g_sdl_full_since, g_sdl_last_rebuild;

static uint32_t sdl_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

int xa2_init(void)
{
    SDL_AudioSpec want, have;
    const char *out = getenv("RECOMP_AUDIO_OUT");
    if (g_sdl_init) return 1;
    if (out && out[0] == '0') {
        fprintf(stderr, "[AUDIO] audio output disabled (RECOMP_AUDIO_OUT=0)\n");
        return 0;
    }
    if (!g_sdl_sub) {
        int r;
        xbox_SDLInitLock(1);
        r = SDL_InitSubSystem(SDL_INIT_AUDIO);
        xbox_SDLInitLock(0);
        if (r != 0) {
            fprintf(stderr, "[AUDIO] SDL audio unavailable: %s\n", SDL_GetError());
            return 0;
        }
        g_sdl_sub = 1;
    }
    SDL_zero(want);
    want.freq = SDL_A_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = SDL_A_CHANNELS;
    want.samples = 512;
    want.callback = NULL;                 /* queued: SDL_QueueAudio */
    /* SDL converts if the device wants something else */
    g_sdl_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!g_sdl_dev) {
        fprintf(stderr, "[AUDIO] could not open the sound device: %s\n", SDL_GetError());
        return 0;
    }
    SDL_PauseAudioDevice(g_sdl_dev, 0);
    g_sdl_init = 1;
    g_sdl_frames = 0;
    g_sdl_full_since = 0;
    fprintf(stderr, "[AUDIO] SDL audio on \"%s\" (%d Hz stereo 16-bit; driver %s)\n",
            "default device", SDL_A_RATE, SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "?");
    return 1;
}

void xa2_shutdown(void)
{
    if (g_sdl_dev) {
        SDL_CloseAudioDevice(g_sdl_dev);
        g_sdl_dev = 0;
    }
    if (g_sdl_init)
        fprintf(stderr, "[AUDIO] Shut down (%d periods written)\n", g_sdl_frames);
    g_sdl_init = 0;
}

int xa2_is_active(void)
{
    return g_sdl_init;
}

static void sdl_rebuild(void)
{
    uint32_t now = sdl_ms();
    if (g_sdl_last_rebuild && now - g_sdl_last_rebuild < 2000) return;
    g_sdl_last_rebuild = now ? now : 1;
    g_sdl_rebuilds++;
    xa2_shutdown();
    if (xa2_init())
        fprintf(stderr, "[AUDIO] sound output restarted (%d so far)\n", g_sdl_rebuilds);
    else
        fprintf(stderr, "[AUDIO] sound output could not be restarted; will try again\n");
    fflush(stderr);
}

int xa2_submit_samples(const int16_t *samples, int num_samples)
{
    uint32_t bytes;
    if (!g_sdl_init || !g_sdl_dev) {
        /* a device lost earlier: keep trying (rate-limited) */
        if (g_sdl_rebuilds) sdl_rebuild();
        return 0;
    }
    if (SDL_GetAudioDeviceStatus(g_sdl_dev) == SDL_AUDIO_STOPPED) {
        fprintf(stderr, "[AUDIO] sound device stopped: restarting the sound output\n");
        fflush(stderr);
        sdl_rebuild();
        return 0;
    }
    if (num_samples > SDL_A_BUF) num_samples = SDL_A_BUF;
    bytes = (uint32_t)num_samples * SDL_A_CHANNELS * sizeof(int16_t);
    if (SDL_GetQueuedAudioSize(g_sdl_dev) + bytes > SDL_A_QUEUE) {
        uint32_t now = sdl_ms();
        if (!g_sdl_full_since) g_sdl_full_since = now ? now : 1;
        else if (now - g_sdl_full_since > 750) {
            fprintf(stderr, "[AUDIO] sound output stopped consuming (queue full for %u ms): restarting it\n",
                    (unsigned)(now - g_sdl_full_since));
            fflush(stderr);
            sdl_rebuild();
        }
        return 0;
    }
    g_sdl_full_since = 0;
    if (SDL_QueueAudio(g_sdl_dev, samples, bytes) != 0) {
        fprintf(stderr, "[AUDIO] queue failed (%s): restarting the sound output\n", SDL_GetError());
        fflush(stderr);
        sdl_rebuild();
        return 0;
    }
    g_sdl_frames++;
    return 1;
}

int xa2_queued_periods(void)
{
    if (!g_sdl_init || !g_sdl_dev) return -1;
    return (int)(SDL_GetQueuedAudioSize(g_sdl_dev) / (256u * SDL_A_CHANNELS * sizeof(int16_t)));
}

int xa2_get_buffer_size(void)
{
    return SDL_A_BUF;
}

#endif /* _WIN32 */
