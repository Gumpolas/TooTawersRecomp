/*
 * win32_compat_host.c - POSIX versions of the Win32 calls a host program
 * (the game's main.c, its settings file) makes: .ini files
 * (GetPrivateProfile* / WritePrivateProfileString), a few file-system
 * helpers, the executable's own path and _putenv_s.
 *
 * The .ini functions follow the Windows behaviour the callers rely on:
 * section and key names match without regard to case, ';' starts a comment
 * line, spaces around '=' and the value are ignored, a missing file reads as
 * empty, and writing keeps every other line of the file as it was.
 */
#if !defined(_WIN32)

#define _GNU_SOURCE
#include "win32_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <pthread.h>

/* ---- .ini files ---------------------------------------------------------- */

static pthread_mutex_t s_ini_lock = PTHREAD_MUTEX_INITIALIZER;

static char *ini_read_all(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    if (n && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    buf[n] = 0;
    fclose(f);
    if (len) *len = (size_t)n;
    return buf;
}

static void trim(const char **b, const char **e)
{
    while (*b < *e && isspace((unsigned char)**b)) (*b)++;
    while (*e > *b && isspace((unsigned char)(*e)[-1])) (*e)--;
}

/* Walk the lines of buf: calls fn(section, key, value, line_start, line_end)
 * for each key line (section/key/value as [begin,end) pairs). */
typedef struct { const char *b, *e; } span;
static int span_eq(span s, const char *z)
{
    size_t n = strlen(z);
    return (size_t)(s.e - s.b) == n && strncasecmp(s.b, z, n) == 0;
}

/* Find, within the text, the given section: returns the offset just after
 * its header line and the offset where it ends (the next header or EOF).
 * -1 if absent. */
static long ini_find_section(const char *t, const char *sec, long *end)
{
    const char *p = t;
    long found = -1;
    while (*p) {
        const char *le = strchr(p, '\n');
        const char *nl = le ? le + 1 : p + strlen(p);
        const char *b = p, *e = le ? le : p + strlen(p);
        trim(&b, &e);
        if (b < e && *b == '[') {
            const char *cb = strchr(b, ']');
            if (cb && cb < e) {
                span s = { b + 1, cb };
                trim(&s.b, &s.e);
                if (found >= 0) { *end = (long)(p - t); return found; }
                if (span_eq(s, sec)) found = (long)(nl - t);
            }
        }
        p = nl;
    }
    if (found >= 0) *end = (long)(p - t);
    return found;
}

/* Within [from, to) of t, the line holding key: its start and end (end
 * includes the newline). Returns 0 if absent. Value span out. */
static int ini_find_key(const char *t, long from, long to, const char *key,
                        long *ls, long *le_out, span *val)
{
    const char *p = t + from, *stop = t + to;
    while (p < stop) {
        const char *le = memchr(p, '\n', (size_t)(stop - p));
        const char *nl = le ? le + 1 : stop;
        const char *b = p, *e = le ? le : stop;
        const char *eq;
        trim(&b, &e);
        if (b < e && *b != ';' && *b != '#' && (eq = memchr(b, '=', (size_t)(e - b))) != NULL) {
            span k = { b, eq };
            trim(&k.b, &k.e);
            if (span_eq(k, key)) {
                span v = { eq + 1, e };
                trim(&v.b, &v.e);
                /* a value in quotes loses them, as on Windows */
                if (v.e - v.b >= 2 && ((*v.b == '"' && v.e[-1] == '"') || (*v.b == '\'' && v.e[-1] == '\''))) {
                    v.b++; v.e--;
                }
                if (val) *val = v;
                if (ls) *ls = (long)(p - t);
                if (le_out) *le_out = (long)(nl - t);
                return 1;
            }
        }
        p = nl;
    }
    return 0;
}

DWORD GetPrivateProfileStringA(LPCSTR sec, LPCSTR key, LPCSTR def, LPSTR out, DWORD size, LPCSTR path)
{
    char *t;
    long s0, s1;
    span v;
    DWORD n = 0;
    if (!out || !size) return 0;
    out[0] = 0;
    pthread_mutex_lock(&s_ini_lock);
    t = path ? ini_read_all(path, NULL) : NULL;
    pthread_mutex_unlock(&s_ini_lock);
    if (t && sec && key && (s0 = ini_find_section(t, sec, &s1)) >= 0 && ini_find_key(t, s0, s1, key, NULL, NULL, &v)) {
        n = (DWORD)(v.e - v.b);
        if (n > size - 1) n = size - 1;
        memcpy(out, v.b, n);
        out[n] = 0;
    } else if (t && sec && !key && (s0 = ini_find_section(t, sec, &s1)) >= 0) {
        /* all key names of the section, each NUL-terminated, then a NUL */
        const char *p = t + s0, *stop = t + s1;
        while (p < stop && n + 2 < size) {
            const char *le = memchr(p, '\n', (size_t)(stop - p));
            const char *nl = le ? le + 1 : stop, *b = p, *e = le ? le : stop, *eq;
            trim(&b, &e);
            if (b < e && *b != ';' && (eq = memchr(b, '=', (size_t)(e - b))) != NULL) {
                span k = { b, eq };
                size_t kl;
                trim(&k.b, &k.e);
                kl = (size_t)(k.e - k.b);
                if (n + kl + 2 <= size) { memcpy(out + n, k.b, kl); n += (DWORD)kl; out[n++] = 0; }
            }
            p = nl;
        }
        out[n] = 0;
    } else if (def) {
        n = (DWORD)strlen(def);
        if (n > size - 1) n = size - 1;
        memcpy(out, def, n);
        out[n] = 0;
        /* trailing spaces of the default go, as on Windows */
        while (n && out[n - 1] == ' ') out[--n] = 0;
    }
    free(t);
    return n;
}

UINT GetPrivateProfileIntA(LPCSTR sec, LPCSTR key, INT def, LPCSTR path)
{
    char v[64];
    if (!GetPrivateProfileStringA(sec, key, "", v, sizeof v, path) || !v[0]) return (UINT)def;
    return (UINT)strtol(v, NULL, 0);
}

DWORD GetPrivateProfileSectionA(LPCSTR sec, LPSTR out, DWORD size, LPCSTR path)
{
    char *t;
    long s0, s1;
    DWORD n = 0;
    if (!out || size < 2) return 0;
    out[0] = out[1] = 0;
    pthread_mutex_lock(&s_ini_lock);
    t = path ? ini_read_all(path, NULL) : NULL;
    pthread_mutex_unlock(&s_ini_lock);
    if (t && sec && (s0 = ini_find_section(t, sec, &s1)) >= 0) {
        const char *p = t + s0, *stop = t + s1;
        while (p < stop) {
            const char *le = memchr(p, '\n', (size_t)(stop - p));
            const char *nl = le ? le + 1 : stop, *b = p, *e = le ? le : stop;
            size_t l;
            trim(&b, &e);
            l = (size_t)(e - b);
            if (l && n + l + 2 <= size) { memcpy(out + n, b, l); n += (DWORD)l; out[n++] = 0; }
            p = nl;
        }
        out[n] = 0;
    }
    free(t);
    return n;
}

BOOL WritePrivateProfileStringA(LPCSTR sec, LPCSTR key, LPCSTR val, LPCSTR path)
{
    char *t, *o;
    size_t len = 0, cap;
    long s0, s1, ls, le;
    FILE *f;
    char tmp[PATH_MAX + 8];
    if (!sec || !path) return FALSE;
    pthread_mutex_lock(&s_ini_lock);
    t = ini_read_all(path, &len);
    if (!t) { t = strdup(""); len = 0; }
    cap = len + strlen(sec) + (key ? strlen(key) : 0) + (val ? strlen(val) : 0) + 16;
    o = (char *)malloc(cap);
    if (!t || !o) { free(t); free(o); pthread_mutex_unlock(&s_ini_lock); return FALSE; }
    s0 = ini_find_section(t, sec, &s1);
    if (s0 < 0) {
        /* new section at the end */
        if (!key || !val) { free(t); free(o); pthread_mutex_unlock(&s_ini_lock); return TRUE; }
        snprintf(o, cap, "%s%s[%s]\n%s=%s\n", t, (len && t[len - 1] != '\n') ? "\n" : "", sec, key, val);
    } else if (!key) {
        /* delete the whole section (header included) */
        long hs = s0;
        while (hs > 0 && t[hs - 1] == '\n') hs--;
        while (hs > 0 && t[hs - 1] != '\n') hs--;
        snprintf(o, cap, "%.*s%s", (int)hs, t, t + s1);
    } else if (ini_find_key(t, s0, s1, key, &ls, &le, NULL)) {
        if (val) snprintf(o, cap, "%.*s%s=%s\n%s", (int)ls, t, key, val, t + le);
        else snprintf(o, cap, "%.*s%s", (int)ls, t, t + le);
    } else if (val) {
        /* after the section's last non-blank line */
        long at = s1;
        while (at > s0 && (t[at - 1] == '\n' || t[at - 1] == '\r' || t[at - 1] == ' ')) at--;
        while (at < s1 && t[at] != '\n') at++;
        if (at < s1) at++;
        snprintf(o, cap, "%.*s%s%s=%s\n%s", (int)at, t, (at && t[at - 1] != '\n') ? "\n" : "", key, val, t + at);
    } else {
        free(t); free(o); pthread_mutex_unlock(&s_ini_lock); return TRUE;
    }
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    f = fopen(tmp, "wb");
    if (f) {
        fwrite(o, 1, strlen(o), f);
        fclose(f);
        if (rename(tmp, path) != 0) { unlink(tmp); f = NULL; }
    }
    free(t); free(o);
    pthread_mutex_unlock(&s_ini_lock);
    return f != NULL;
}

/* ---- files ---------------------------------------------------------------- */

DWORD GetFileAttributesA(LPCSTR path)
{
    struct stat st;
    if (!path || stat(path, &st) != 0) return INVALID_FILE_ATTRIBUTES;
    return S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
}

BOOL CreateDirectoryA(LPCSTR path, LPSECURITY_ATTRIBUTES sa)
{
    (void)sa;
    if (!path) return FALSE;
    if (mkdir(path, 0755) == 0) return TRUE;
    SetLastError(errno == EEXIST ? 183 /* ERROR_ALREADY_EXISTS */ : 3 /* ERROR_PATH_NOT_FOUND */);
    return FALSE;
}

BOOL MoveFileExA(LPCSTR from, LPCSTR to, DWORD flags)
{
    struct stat st;
    if (!from || !to) return FALSE;
    if (!(flags & MOVEFILE_REPLACE_EXISTING) && stat(to, &st) == 0) { SetLastError(183); return FALSE; }
    return rename(from, to) == 0;
}

BOOL DeleteFileA(LPCSTR path) { return path && unlink(path) == 0; }

DWORD GetModuleFileNameA(HMODULE m, LPSTR out, DWORD size)
{
    ssize_t n;
    (void)m;
    if (!out || !size) return 0;
#if defined(__APPLE__)
    {
        extern int _NSGetExecutablePath(char *buf, uint32_t *bufsize);
        uint32_t sz = size;
        char tmp[4096];
        uint32_t ts = sizeof tmp;
        if (_NSGetExecutablePath(tmp, &ts) != 0 || !realpath(tmp, out)) { (void)sz; out[0] = 0; return 0; }
        return (DWORD)strlen(out);
    }
#endif
    n = readlink("/proc/self/exe", out, size - 1);
    if (n < 0) { out[0] = 0; return 0; }
    out[n] = 0;
    return (DWORD)n;
}

DWORD GetCurrentDirectoryA(DWORD size, LPSTR out)
{
    if (!out || !getcwd(out, size)) return 0;
    return (DWORD)strlen(out);
}

int _putenv_s(const char *name, const char *value)
{
    if (!name) return EINVAL;
    if (!value || !value[0]) return unsetenv(name) == 0 ? 0 : errno;
    return setenv(name, value, 1) == 0 ? 0 : errno;
}

#endif /* !_WIN32 */
