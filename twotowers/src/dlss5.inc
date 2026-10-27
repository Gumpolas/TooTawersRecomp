/* Integracion opcional, sin modificar el codigo recompilado del XBE.
 * El paquete local usa ReShade + Feeder x64 + un consumidor neuronal.
 * Cargar ReShade no demuestra que la funcion neuronal se haya evaluado. */
static int s_dlss5_ready;
void tt_dlss5_notify_movie(int movie)
{
#if defined(_WIN32)
    typedef void (*Notify)(int);
    static Notify notify;
    if (!s_dlss5_ready) return;
    if (!notify) {
        HMODULE guard = GetModuleHandleW(L"tt_dlss5_guard.addon64");
        if (guard) notify = (Notify)(void *)GetProcAddress(guard, "TT_DLSS5_SetSceneState");
    }
    if (notify) notify(movie);
#else
    (void)movie;
#endif
}
static void tt_dlss5_init(void)
{
#if defined(_WIN32)
    const char *override = getenv("TT_DLSS5");
    char renderer[32];
    wchar_t base[MAX_PATH], path[MAX_PATH + 80], *slash;
    static const wchar_t *required[] = {
        L"dxgi.dll", L"dlss5-feed.addon64", L"renodx-dlss5.addon64",
        L"nvngx_dlss.dll", L"nvngx_dlssnr.dll", L"ReShade.ini", L"dlss5-feed.cfg", L"tt_dlss5_guard.addon64"
    };
    DWORD n;
    size_t i;
    if (!(override ? atoi(override) : GetPrivateProfileIntA("DLSS5", "enabled", 0, g_ini_path))) return;
    GetPrivateProfileStringA("Display", "renderer", "d3d11", renderer, sizeof(renderer), g_ini_path);
    if (getenv("RECOMP_RENDERER")) {
        strncpy(renderer, getenv("RECOMP_RENDERER"), sizeof(renderer) - 1);
        renderer[sizeof(renderer) - 1] = 0;
    }
    if (_stricmp(renderer, "d3d11")) {
        fprintf(stderr, "[DLSS5] Desactivado: esta integracion requiere renderer=d3d11.\n");
        return;
    }
    n = GetModuleFileNameW(NULL, base, MAX_PATH);
    if (!n || n >= MAX_PATH || !(slash = wcsrchr(base, L'\\'))) return;
    *slash = 0;
    for (i = 0; i < sizeof(required) / sizeof(required[0]); ++i) {
        _snwprintf(path, MAX_PATH + 80, L"%ls\\dlss5\\%ls", base, required[i]);
        path[MAX_PATH + 79] = 0;
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
            fprintf(stderr, "[DLSS5] Paquete incompleto (archivo %u): se continua sin DLSS.\n", (unsigned)i);
            return;
        }
    }
    _snwprintf(path, MAX_PATH + 80, L"%ls\\dlss5\\dxgi.dll", base);
    path[MAX_PATH + 79] = 0;
    if (LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH)) {
        s_dlss5_ready = 1;
        fprintf(stderr, "[DLSS5] ReShade x64 precargado. La evaluacion neuronal se comprueba en dlss5\\ReShade.log.\n");
    } else
        fprintf(stderr, "[DLSS5] No se pudo cargar ReShade (Windows %lu); se continua sin DLSS.\n", GetLastError());
#endif
}
