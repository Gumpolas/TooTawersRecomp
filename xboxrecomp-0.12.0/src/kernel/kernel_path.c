/*
 * kernel_path.c - Xbox device-path translation
 *
 * Translates Xbox device-style paths to host filesystem paths:
 *   \Device\CdRom0\  -> <game_dir>/
 *   D:\               -> <game_dir>/
 *   T:\               -> <save_dir>/TitleData/
 *   U:\               -> <save_dir>/UserData/
 *   Z:\               -> <save_dir>/Cache/
 *
 * The Win32 build emits UTF-16 paths (for CreateFileW); the Linux build
 * emits UTF-8 paths with '/' separators (for open()).
 */

#include "kernel.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/*
 * Helper: check if an ANSI string starts with a prefix (case-insensitive).
 * Returns the number of chars consumed from the prefix, or 0 if no match.
 * Platform-independent.
 */
static int match_prefix(const char* path, const char* prefix)
{
    int i = 0;
    while (prefix[i]) {
        if (tolower((unsigned char)path[i]) != tolower((unsigned char)prefix[i]))
            return 0;
        i++;
    }
    return i;
}

/* A device-path translation rule, shared by both backends. */
typedef struct {
    const char* prefix;     /* Xbox path prefix (backslash form)         */
    int         to_save;    /* 1 = under save_dir, 0 = under game_dir    */
    const char* sub_win;    /* sub-directory, Win32 backslash form       */
    const char* sub_posix;  /* sub-directory, POSIX slash form           */
    int         whole_name; /* 1 = the prefix is a whole folder name: it
                             * matches only before a separator or the end */
} path_rule;

static const path_rule s_rules[] = {
    { "\\Device\\CdRom0\\",                   0, NULL,         NULL, 0 },
    /* Partition1 is E:, where a console keeps TDATA (title data) and UDATA
     * (saved games). Those two folders go under save_dir, so that two
     * installations pointed at the same game_dir do not share -- and
     * overwrite -- each other's saves. Anything else under Partition1 stays
     * in the game dir, as before; listed ahead of the generic rule. */
    { "\\Device\\Harddisk0\\Partition1\\UDATA", 1, "\\UDATA",    "/UDATA",     1 },
    { "\\Device\\Harddisk0\\Partition1\\TDATA", 1, "\\TDATA",    "/TDATA",     1 },
    { "\\Device\\Harddisk0\\Partition1\\",    0, NULL,         NULL, 0 },
    /* The rest of the disk. Partition 0 is the whole raw device, 2 holds
     * system data, and 3-5 are the per-title caches behind X:, Y: and Z:.
     * Without these the path layer reported "Unrecognized Xbox path" and
     * NtOpenFile returned STATUS_OBJECT_PATH_NOT_FOUND; DSTEAL_JP probes
     * partition0 at startup and treats the failure as fatal.
     *
     * No trailing separator: the probe opens the device itself, with nothing
     * after it. Listed after Partition1 so that keeps its own mapping. */
    /* Partition2 is C:, the system partition. On a console that is where the
     * dashboard and its own assets live, and the dashboard opens them by
     * device path as well as through Y:, so a path *under* partition2 is an
     * ordinary asset read and belongs in the game dir -- the same place Y:
     * already goes. Only the bare device keeps the system-data image, which
     * is why this rule carries the trailing separator and is listed first. */
    { "\\Device\\Harddisk0\\Partition2\\",    0, NULL,         NULL, 0 },
    { "C:\\",                                 0, NULL,         NULL, 0 },
    { "\\??\\C:\\",                           0, NULL,         NULL, 0 },
    { "\\Device\\Harddisk0\\Partition2",     1, "\\SystemData", "/SystemData", 0 },
    { "\\Device\\Harddisk0\\Partition3",     1, "\\Cache",   "/Cache", 0 },
    { "\\Device\\Harddisk0\\Partition4",     1, "\\Cache",   "/Cache", 0 },
    { "\\Device\\Harddisk0\\Partition5",     1, "\\Cache",   "/Cache", 0 },
    { "D:\\",                                 0, NULL,         NULL, 0 },
    { "d:\\",                                 0, NULL,         NULL, 0 },
    /* Y: is the Xbox dashboard partition; the dashboard opens its assets
     * (e.g. "Y:\default.xip") from there. Map it to the game dir. */
    { "Y:\\",                                 0, NULL,         NULL, 0 },
    { "y:\\",                                 0, NULL,         NULL, 0 },
    { "T:\\",                                 1, "\\TitleData","/TitleData", 0 },
    { "U:\\",                                 1, "\\UserData", "/UserData", 0 },
    { "Z:\\",                                 1, "\\Cache",    "/Cache", 0 },
    { "\\??\\D:\\",                           0, NULL,         NULL, 0 },
    { "\\??\\Y:\\",                           0, NULL,         NULL, 0 },
    { "\\??\\y:\\",                           0, NULL,         NULL, 0 },
    { "\\??\\T:\\",                           1, "\\TitleData","/TitleData", 0 },
};
#define PATH_RULE_COUNT ((int)(sizeof(s_rules) / sizeof(s_rules[0])))

/* Rule i against a path: the characters consumed, or 0. A whole-name rule
 * ("...\UDATA") does not match "...\UDATAX", and it consumes the separators
 * after the name too, so the join below does not double them. */
static int match_rule(const char* path, int i)
{
    int n = match_prefix(path, s_rules[i].prefix);
    if (n && s_rules[i].whole_name) {
        if (path[n] && path[n] != '\\' && path[n] != '/')
            return 0;
        while (path[n] == '\\' || path[n] == '/')
            n++;
    }
    return n;
}

/*
 * Rewrite a path through a drive letter the title mapped for itself.
 *
 * Titles do their own mounting. Wreckless calls IoCreateSymbolicLink at guest
 * 0x000E9B1D to link "\??\Z:" to "\Device\Harddisk0\Partition1\", then loads
 * every asset through z:\. kernel_io.c has recorded that link since it was
 * written, but nothing ever read the table back, so the static rule below --
 * Z: is the cache partition, which is true of the console in general and wrong
 * for this title -- sent every asset open to the save directory and it failed
 * with ERROR_FILE_NOT_FOUND.
 *
 * Accepts "Z:\rest" and "\??\Z:\rest"; the table is keyed on the "\??\Z:" form
 * the kernel actually registers.
 *
 * Deliberately conservative: the rewritten path is returned only when it
 * matches a static rule. A link whose target this layer has no rule for would
 * otherwise turn a path that translated adequately into one that does not
 * translate at all.
 *
 * Returns 1 and fills `out` on success, 0 to leave the path alone.
 */
static int resolve_symlink(const char* xbox_path, char* out, size_t out_size)
{
    char        link[8];
    const char* rest;
    const char* target;
    size_t      tlen, rlen;
    int         i;

    if (!xbox_path || !out || out_size == 0)
        return 0;

    if (match_prefix(xbox_path, "\\??\\") && xbox_path[4] && xbox_path[5] == ':')
        xbox_path += 4;
    if (!xbox_path[0] || xbox_path[1] != ':')
        return 0;
    rest = xbox_path + 2;

    link[0] = '\\';
    link[1] = '?';
    link[2] = '?';
    link[3] = '\\';
    link[4] = (char)toupper((unsigned char)xbox_path[0]);
    link[5] = ':';
    link[6] = '\0';

    target = xbox_LookupSymbolicLink(link);
    if (!target || !target[0])
        return 0;

    while (*rest == '\\' || *rest == '/')
        rest++;

    tlen = strlen(target);
    rlen = strlen(rest);
    if (tlen + rlen + 2 > out_size)
        return 0;

    memcpy(out, target, tlen);
    if (tlen && target[tlen - 1] != '\\' && target[tlen - 1] != '/')
        out[tlen++] = '\\';
    memcpy(out + tlen, rest, rlen);
    out[tlen + rlen] = '\0';

    for (i = 0; i < PATH_RULE_COUNT; i++) {
        if (match_prefix(out, s_rules[i].prefix))
            return 1;           /* the target is somewhere we can place */
    }
    return 0;
}

/* ======================================================================== */
/* Shared by both backends, for the same reason as xbox_LastFileError: the
 * bridge calls it unconditionally, and a _WIN32-only definition breaks every
 * POSIX link. The Win32 backend fills this during translation; the POSIX one
 * does not yet, so it reads empty there. */
static XBOX_THREAD_LOCAL wchar_t s_last_host_path_shared[MAX_PATH];

const wchar_t *xbox_LastHostPath(void)
{
    return s_last_host_path_shared;
}

#if defined(_WIN32)
/* ======================================================================== */

#include <shlobj.h>
#include <winioctl.h>

static WCHAR s_game_dir[MAX_PATH];
static WCHAR s_save_dir[MAX_PATH];
static BOOL  s_initialized = FALSE;

/*
 * The raw disk device, \Device\Harddisk0\Partition0.
 *
 * A title opens it to read the Xbox partition table at sector 4 (offset 0x800)
 * and find the cache partitions behind X:, Y: and Z: -- XAPI's utility-drive
 * mount does exactly that, and DSTEAL_JP calls HalReturnToFirmware when the
 * read fails. A directory cannot answer a 512-byte read at a file offset, so
 * the device is backed by an image file instead.
 *
 * The table is the standard retail geometry. This is emulating a device that
 * has to be there, not fabricating anything the title owns.
 */
#define XBOX_DISK_IMAGE_NAME   L"Partition0.img"
#define XBOX_PART_TABLE_OFFSET 0x800
#define XBOX_PART_IN_USE       0x80000000u

static void xbox_write_partition_table(const WCHAR *path)
{
    /* name[16], flags, lba_start, lba_size, reserved -- 32 bytes each */
    static const struct { const char *name; ULONG start, size; } parts[] = {
        { "XBOX_PART_X",  0x00000400, 0x00177000 },  /* X: cache      */
        { "XBOX_PART_Y",  0x00177400, 0x00177000 },  /* Y: cache      */
        { "XBOX_PART_Z",  0x002EE400, 0x00177000 },  /* Z: cache      */
        { "XBOX_PART_C",  0x00465400, 0x000FA000 },  /* C: system     */
        { "XBOX_PART_E",  0x0055F400, 0x00465400 },  /* E: game/save  */
    };
    unsigned char sector[512];
    HANDLE h;
    DWORD written;
    LARGE_INTEGER off;
    size_t i;

    h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;

    memset(sector, 0, sizeof(sector));
    memcpy(sector, "****PARTINFO****", 16);
    for (i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
        unsigned char *e = sector + 48 + i * 32;   /* 16 magic + 32 reserved */
        size_t n = strlen(parts[i].name);
        memset(e, ' ', 16);
        memcpy(e, parts[i].name, n < 16 ? n : 16);
        *(ULONG *)(e + 16) = XBOX_PART_IN_USE;
        *(ULONG *)(e + 20) = parts[i].start;
        *(ULONG *)(e + 24) = parts[i].size;
        *(ULONG *)(e + 28) = 0;
    }

    off.QuadPart = XBOX_PART_TABLE_OFFSET;
    if (SetFilePointerEx(h, off, NULL, FILE_BEGIN))
        WriteFile(h, sector, sizeof(sector), &written, NULL);
    CloseHandle(h);
}

/*
 * Map a partition device onto an image file.
 *
 * Matches "\Device\Harddisk0\PartitionN" with nothing below it, which is how a
 * device is opened. Anything with a path under it -- Partition1\TDATA and the
 * like -- is an ordinary filesystem access and falls through to the rules
 * table, which puts it in a directory.
 *
 * Partition0 is the whole disk and carries the partition table written above.
 * The rest start empty, which is what an unformatted partition looks like, so
 * a title that wants a filesystem there formats one.
 */
static BOOL xbox_partition_device_path(const char *xbox_path, WCHAR *out, DWORD n)
{
    static const char *prefix = "\\Device\\Harddisk0\\Partition";
    int len = match_prefix(xbox_path, prefix);
    int digit;
    const char *rest;

    if (!len)
        return FALSE;
    digit = xbox_path[len];
    if (digit < '0' || digit > '9')
        return FALSE;

    rest = xbox_path + len + 1;
    /* Nothing below it, allowing for a single trailing separator. */
    if (*rest == '\\' || *rest == '/')
        rest++;
    if (*rest != '\0')
        return FALSE;

    swprintf_s(out, n, L"%s\\Partition%c.img", s_save_dir, (WCHAR)digit);
    return TRUE;
}

/* Copy a folder tree, files and sub-folders; reparse points are skipped, and
 * a path that does not fit MAX_PATH is left out rather than truncated. */
static void copy_tree(const WCHAR *from, const WCHAR *to, int depth)
{
    WIN32_FIND_DATAW fd;
    WCHAR pat[MAX_PATH], a[MAX_PATH], b[MAX_PATH];
    HANDLE h;
    int n;

    if (depth > 16)
        return;
    if (!CreateDirectoryW(to, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return;
    n = _snwprintf(pat, MAX_PATH, L"%s\\*", from);
    if (n < 0 || n >= MAX_PATH)
        return;
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            continue;
        n = _snwprintf(a, MAX_PATH, L"%s\\%s", from, fd.cFileName);
        if (n < 0 || n >= MAX_PATH)
            continue;
        n = _snwprintf(b, MAX_PATH, L"%s\\%s", to, fd.cFileName);
        if (n < 0 || n >= MAX_PATH)
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            copy_tree(a, b, depth + 1);
        else
            CopyFileW(a, b, FALSE);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* Partition1's UDATA and TDATA used to resolve under game_dir; they now go
 * under save_dir (see s_rules). An installation that already has saves in
 * game_dir would otherwise start with none, so bring them along once: only
 * when save_dir has no such folder yet, by copying (the originals stay where
 * they are, nothing is moved or overwritten). The copy is made under a
 * temporary name and renamed when complete, so an interrupted copy is
 * redone on the next start instead of being taken for the real folder. */
static void migrate_partition1_saves(void)
{
    static const WCHAR *names[] = { L"UDATA", L"TDATA" };
    WCHAR from[MAX_PATH], to[MAX_PATH], tmp[MAX_PATH];
    DWORD attr;
    int i, n1, n2, n3;

    if (!_wcsicmp(s_game_dir, s_save_dir))
        return;
    for (i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++) {
        n1 = _snwprintf(from, MAX_PATH, L"%s\\%s", s_game_dir, names[i]);
        n2 = _snwprintf(to, MAX_PATH, L"%s\\%s", s_save_dir, names[i]);
        n3 = _snwprintf(tmp, MAX_PATH, L"%s\\%s.migrating", s_save_dir, names[i]);
        if (n1 < 0 || n1 >= MAX_PATH || n2 < 0 || n2 >= MAX_PATH || n3 < 0 || n3 >= MAX_PATH)
            continue;
        attr = GetFileAttributesW(from);
        if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        if (GetFileAttributesW(to) != INVALID_FILE_ATTRIBUTES)
            continue;
        copy_tree(from, tmp, 0);
        if (MoveFileW(tmp, to))
            xbox_log(XBOX_LOG_INFO, XBOX_LOG_PATH, "Saves: copied %S to %S (original kept)", from, to);
        else
            xbox_log(XBOX_LOG_WARN, XBOX_LOG_PATH, "Saves: could not finish copying %S to %S", from, to);
    }
}

void xbox_path_init(const char* game_dir, const char* save_dir)
{
    WCHAR save_base[MAX_PATH];

    /* The fallbacks used to name Burnout 3 specifically, so any other title
     * that passed NULL silently pointed its game dir and its saves at another
     * game's folders. Generic now -- a caller that wants a title-specific
     * location should pass one. */
    if (game_dir) {
        MultiByteToWideChar(CP_UTF8, 0, game_dir, -1, s_game_dir, MAX_PATH);
    } else {
        /* Neutral default: the CWD's "game" subdirectory, which is the
         * layout the game projects use (see templates/new-game). */
        GetCurrentDirectoryW(MAX_PATH, s_game_dir);
    }

    if (save_dir) {
        MultiByteToWideChar(CP_UTF8, 0, save_dir, -1, s_save_dir, MAX_PATH);
    } else {
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, save_base))) {
            swprintf_s(s_save_dir, MAX_PATH, L"%s\\xboxrecomp", save_base);
        } else {
            GetCurrentDirectoryW(MAX_PATH, s_save_dir);
            wcscat_s(s_save_dir, MAX_PATH, L"\\SaveData");
        }
    }

    size_t len = wcslen(s_game_dir);
    if (len > 0 && s_game_dir[len - 1] == L'\\')
        s_game_dir[len - 1] = L'\0';

    len = wcslen(s_save_dir);
    if (len > 0 && s_save_dir[len - 1] == L'\\')
        s_save_dir[len - 1] = L'\0';

    /* Create the save-side directories. T:/U:/Z: map into subdirectories of
     * save_dir, and a title that opens a file there with a create disposition
     * fails if the parent does not exist -- which reads as "cannot create save
     * file" and sends the title down its init-failure path. Halo asserts
     * exactly that at saved games/game_state_xbox.c:97 and then unwinds,
     * clearing global_d3d_device on the way out, so a missing directory
     * surfaces as a graphics failure.
     *
     * Cheap and idempotent: SHCreateDirectoryExW builds intermediates and is
     * happy if they already exist. */
    {
        static const WCHAR *subs[] = { L"TitleData", L"UserData", L"Cache",
                                       L"SystemData" };
        WCHAR image[MAX_PATH];
        WCHAR dir[MAX_PATH];
        SHCreateDirectoryExW(NULL, s_save_dir, NULL);
        for (int i = 0; i < (int)(sizeof(subs) / sizeof(subs[0])); i++) {
            swprintf_s(dir, MAX_PATH, L"%s\\%s", s_save_dir, subs[i]);
            SHCreateDirectoryExW(NULL, dir, NULL);
        }
        swprintf_s(image, MAX_PATH, L"%s\\%s", s_save_dir, XBOX_DISK_IMAGE_NAME);
        xbox_write_partition_table(image);
        /* The other partition devices, sized to the same geometry the table
         * above describes, so a title that asks the device how big it is gets
         * an answer consistent with the table it just read. Marked sparse
         * first: the cache partitions are 750 MB each and none of that is
         * touched until something writes to it. */
        {
            static const ULONGLONG part_sectors[6] = {
                0,             /* 0: whole disk, sized below      */
                0x00465400ull, /* 1: E: game and saves            */
                0x000FA000ull, /* 2: C: system                    */
                0x00177000ull, /* 3: X: cache                     */
                0x00177000ull, /* 4: Y: cache                     */
                0x00177000ull, /* 5: Z: cache                     */
            };
            for (int p = 1; p <= 5; p++) {
                HANDLE h;
                DWORD ret;
                LARGE_INTEGER end;

                swprintf_s(image, MAX_PATH, L"%s\\Partition%d.img",
                           s_save_dir, p);
                h = CreateFileW(image, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                if (h == INVALID_HANDLE_VALUE)
                    continue;
                DeviceIoControl(h, FSCTL_SET_SPARSE, NULL, 0, NULL, 0,
                                &ret, NULL);
                end.QuadPart = (LONGLONG)(part_sectors[p] * 512ull);
                if (SetFilePointerEx(h, end, NULL, FILE_BEGIN))
                    SetEndOfFile(h);
                CloseHandle(h);
            }
        }
    }
    migrate_partition1_saves();

    s_initialized = TRUE;
    xbox_log(XBOX_LOG_INFO, XBOX_LOG_PATH, "Path init: game=%S, save=%S", s_game_dir, s_save_dir);
}

/* The host path the last translation produced.
 *
 * A caller that wants to act on the file a title just opened -- playing an FMV
 * the host can decode itself, say -- has the guest path but not the host one,
 * and re-deriving it would duplicate every rule above. */
/* Thread-local: several threads open files at once, and a plain static let one
 * thread's translation overwrite another's between the translate and the read.
 * That showed up as the FMV trigger firing on roughly two runs in three. */

static void xbox_remember_host_path(const wchar_t *p)
{
    if (p) wcsncpy_s(s_last_host_path_shared, MAX_PATH, p, _TRUNCATE);
}




/* A title's own file substitutions: given the Xbox path, return a host path
 * (UTF-8) to open instead, or NULL. Set by the game (twotowers: generated
 * level files for the extra heroes). */
const char *(*xbox_path_redirect)(const char *xbox_path) = NULL;
BOOL xbox_translate_path(const char* xbox_path, xbox_host_char* host_path_buf, DWORD buf_size)
{
    const char*  remainder = NULL;
    const WCHAR* base_dir  = NULL;
    const char*  sub_dir   = NULL;
    int          skip;

    if (!xbox_path || !host_path_buf || buf_size == 0)
        return FALSE;

    if (!s_initialized)
        xbox_path_init(NULL, NULL);
    {
        extern void xbox_PadScriptNotePath(const char *path);
        xbox_PadScriptNotePath(xbox_path);
    }

    {
        char linked[512];
        if (resolve_symlink(xbox_path, linked, sizeof(linked)))
            return xbox_translate_path(linked, host_path_buf, buf_size);
    }

    if (xbox_partition_device_path(xbox_path, host_path_buf, buf_size)) {
        fprintf(stderr, "  [PATH] %s -> partition image\n", xbox_path);
        fflush(stderr);
        return TRUE;
    }

    if (xbox_path_redirect) {
        const char *r = xbox_path_redirect(xbox_path);
        if (r) {
            MultiByteToWideChar(CP_UTF8, 0, r, -1, host_path_buf, buf_size);
            fprintf(stderr, "  [PATH] %s -> %s\n", xbox_path, r);
            fflush(stderr);
            xbox_remember_host_path(host_path_buf);
            return TRUE;
        }
    }

    for (int i = 0; i < PATH_RULE_COUNT; i++) {
        skip = match_rule(xbox_path, i);
        if (skip) {
            remainder = xbox_path + skip;
            base_dir  = s_rules[i].to_save ? s_save_dir : s_game_dir;
            sub_dir   = s_rules[i].sub_win;
            goto translate;
        }
    }

    xbox_log(XBOX_LOG_WARN, XBOX_LOG_PATH, "Unrecognized Xbox path: %s", xbox_path);
    MultiByteToWideChar(CP_ACP, 0, xbox_path, -1, host_path_buf, buf_size);
    return TRUE;

translate:
    fprintf(stderr, "  [PATH] %s\n", xbox_path);
    fflush(stderr);
    {
        WCHAR remainder_wide[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, remainder, -1, remainder_wide, MAX_PATH);

        for (WCHAR* p = remainder_wide; *p; p++) {
            if (*p == L'/') *p = L'\\';
        }

        if (sub_dir) {
            WCHAR sub_wide[MAX_PATH];
            MultiByteToWideChar(CP_ACP, 0, sub_dir, -1, sub_wide, MAX_PATH);
            swprintf_s(host_path_buf, buf_size, L"%s%s\\%s", base_dir, sub_wide, remainder_wide);

            WCHAR dir_path[MAX_PATH];
            swprintf_s(dir_path, MAX_PATH, L"%s%s", base_dir, sub_wide);
            CreateDirectoryW(s_save_dir, NULL);
            CreateDirectoryW(dir_path, NULL);
        } else {
            swprintf_s(host_path_buf, buf_size, L"%s\\%s", base_dir, remainder_wide);
        }

        /* An empty remainder means the title opened the device itself, so
         * the join above leaves a trailing separator -- which CreateFileW
         * rejects even with FILE_FLAG_BACKUP_SEMANTICS, turning an existing
         * directory into STATUS_OBJECT_PATH_NOT_FOUND. */
        {
            size_t n = wcslen(host_path_buf);
            while (n > 1 && (host_path_buf[n - 1] == L'\\'
                             || host_path_buf[n - 1] == L'/'))
                host_path_buf[--n] = L'\0';
        }

        XBOX_TRACE(XBOX_LOG_PATH, "%s -> %S", xbox_path, host_path_buf);
        xbox_remember_host_path(host_path_buf);
        return TRUE;
    }
}

/* ======================================================================== */
#else /* !_WIN32  -- POSIX / Linux */
/* ======================================================================== */

#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

static char s_game_dir[MAX_PATH];
static char s_save_dir[MAX_PATH];
static BOOL s_initialized = FALSE;
const char *(*xbox_path_redirect)(const char *xbox_path) = NULL;

/* Strip a single trailing '/' (but never the root '/'). */
static void strip_trailing_slash(char* s)
{
    size_t len = strlen(s);
    if (len > 1 && s[len - 1] == '/')
        s[len - 1] = '\0';
}

/* Recursively create a directory and all missing parents. */
static void mkdir_p(const char* path)
{
    char tmp[MAX_PATH];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp))
        return;
    memcpy(tmp, path, len + 1);

    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
                xbox_log(XBOX_LOG_WARN, XBOX_LOG_PATH, "mkdir %s: %s", tmp, strerror(errno));
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
        xbox_log(XBOX_LOG_WARN, XBOX_LOG_PATH, "mkdir %s: %s", tmp, strerror(errno));
}

/* The raw disk and its partitions as image files under the save directory,
 * as the Win32 backend does (see the comments there): Partition0.img carries
 * the retail partition table that XAPI reads to mount the utility drives. */
#define XBOX_PART_TABLE_OFFSET 0x800
#define XBOX_PART_IN_USE       0x80000000u
static void xbox_write_partition_table(const char *path)
{
    static const struct { const char *name; uint32_t start, size; } parts[] = {
        { "XBOX_PART_X",  0x00000400, 0x00177000 },
        { "XBOX_PART_Y",  0x00177400, 0x00177000 },
        { "XBOX_PART_Z",  0x002EE400, 0x00177000 },
        { "XBOX_PART_C",  0x00465400, 0x000FA000 },
        { "XBOX_PART_E",  0x0055F400, 0x00465400 },
    };
    unsigned char sector[512];
    FILE *f;
    size_t i;
    memset(sector, 0, sizeof(sector));
    memcpy(sector, "****PARTINFO****", 16);
    for (i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
        unsigned char *e = sector + 48 + i * 32;
        size_t n = strlen(parts[i].name);
        uint32_t v;
        memset(e, ' ', 16);
        memcpy(e, parts[i].name, n < 16 ? n : 16);
        v = XBOX_PART_IN_USE; memcpy(e + 16, &v, 4);
        memcpy(e + 20, &parts[i].start, 4);
        memcpy(e + 24, &parts[i].size, 4);
        v = 0; memcpy(e + 28, &v, 4);
    }
    f = fopen(path, "r+b");
    if (!f) f = fopen(path, "w+b");
    if (!f) return;
    if (fseek(f, XBOX_PART_TABLE_OFFSET, SEEK_SET) == 0) fwrite(sector, 1, sizeof(sector), f);
    fclose(f);
}

static BOOL xbox_partition_device_path(const char *xbox_path, char *out, DWORD n)
{
    static const char *prefix = "\\Device\\Harddisk0\\Partition";
    int len = match_prefix(xbox_path, prefix);
    int digit;
    const char *rest;
    if (!len) return FALSE;
    digit = xbox_path[len];
    if (digit < '0' || digit > '9') return FALSE;
    rest = xbox_path + len + 1;
    if (*rest == '\\' || *rest == '/') rest++;
    if (*rest != '\0') return FALSE;
    snprintf(out, n, "%s/Partition%c.img", s_save_dir, (char)digit);
    return TRUE;
}

void xbox_path_init(const char* game_dir, const char* save_dir)
{
    if (game_dir) {
        snprintf(s_game_dir, sizeof(s_game_dir), "%s", game_dir);
    } else {
        /* Neutral default: CWD/"game", the layout game projects use. */
        char cwd[MAX_PATH];
        if (!getcwd(cwd, sizeof(cwd)))
            snprintf(cwd, sizeof(cwd), ".");
        snprintf(s_game_dir, sizeof(s_game_dir), "%s/game", cwd);
    }

    if (save_dir) {
        snprintf(s_save_dir, sizeof(s_save_dir), "%s", save_dir);
    } else {
        /* XDG base-directory spec: $XDG_DATA_HOME or ~/.local/share */
        const char* xdg = getenv("XDG_DATA_HOME");
        if (xdg && xdg[0]) {
            snprintf(s_save_dir, sizeof(s_save_dir), "%s/xboxrecomp", xdg);
        } else {
            const char* home = getenv("HOME");
            snprintf(s_save_dir, sizeof(s_save_dir), "%s/.local/share/xboxrecomp",
                     (home && home[0]) ? home : ".");
        }
    }

    strip_trailing_slash(s_game_dir);
    strip_trailing_slash(s_save_dir);

    {
        static const char *subs[] = { "TitleData", "UserData", "Cache", "SystemData" };
        static const unsigned long long part_sectors[6] = {
            0, 0x00465400ull, 0x000FA000ull, 0x00177000ull, 0x00177000ull, 0x00177000ull };
        char p[MAX_PATH + 32];
        int i;
        mkdir_p(s_save_dir);
        for (i = 0; i < 4; i++) { snprintf(p, sizeof p, "%s/%s", s_save_dir, subs[i]); mkdir_p(p); }
        snprintf(p, sizeof p, "%s/Partition0.img", s_save_dir);
        xbox_write_partition_table(p);
        /* the other partitions at the sizes the table gives; sparse files */
        for (i = 1; i <= 5; i++) {
            struct stat st;
            snprintf(p, sizeof p, "%s/Partition%d.img", s_save_dir, i);
            if (stat(p, &st) != 0 || (unsigned long long)st.st_size != part_sectors[i] * 512ull) {
                FILE *f = fopen(p, "ab");
                if (f) fclose(f);
                if (truncate(p, (off_t)(part_sectors[i] * 512ull)) != 0) { /* not fatal */ }
            }
        }
    }

    s_initialized = TRUE;
    xbox_log(XBOX_LOG_INFO, XBOX_LOG_PATH, "Path init: game=%s, save=%s",
             s_game_dir, s_save_dir);
}

/* Case-insensitive lookup of the part of path below base: each component that
 * does not exist as spelled is replaced by a directory entry that matches it
 * ignoring case. Components that match nothing are left as they are (a file
 * about to be created). */
#include <dirent.h>
#include <strings.h>
static void posix_ci_resolve(char *path, const char *base)
{
    struct stat st;
    size_t blen = base ? strlen(base) : 0;
    char *p;
    if (stat(path, &st) == 0) return;
    if (!base || strncmp(path, base, blen) != 0) return;
    p = path + blen;
    while (*p == '/') p++;
    while (*p) {
        char *end = strchr(p, '/');
        char save = end ? *end : 0;
        if (end) *end = 0;
        if (lstat(path, &st) != 0) {
            /* look for it in the parent directory */
            DIR *d;
            char *slash = strrchr(path, '/');
            char parent[MAX_PATH];
            size_t plen = slash ? (size_t)(slash - path) : 0;
            if (!slash || plen >= sizeof parent) { if (end) *end = save; return; }
            memcpy(parent, path, plen); parent[plen] = 0;
            if (!plen) { parent[0] = '/'; parent[1] = 0; }
            d = opendir(parent);
            if (d) {
                struct dirent *de;
                while ((de = readdir(d)) != NULL)
                    if (strlen(de->d_name) == strlen(p) && strcasecmp(de->d_name, p) == 0) {
                        memcpy(p, de->d_name, strlen(p));
                        break;
                    }
                closedir(d);
            }
        }
        if (!end) break;
        *end = save;
        p = end + 1;
        while (*p == '/') p++;
    }
}

/* For host code reading game files itself: fix the case of path below base. */
void xbox_host_path_fix_case(char *path, const char *base)
{
    posix_ci_resolve(path, base);
}

BOOL xbox_translate_path(const char* xbox_path, xbox_host_char* host_path_buf, DWORD buf_size)
{
    const char* remainder = NULL;
    const char* base_dir  = NULL;
    const char* sub_dir   = NULL;
    int         skip;

    if (!xbox_path || !host_path_buf || buf_size == 0)
        return FALSE;

    if (!s_initialized)
        xbox_path_init(NULL, NULL);
    {
        extern void xbox_PadScriptNotePath(const char *path);
        xbox_PadScriptNotePath(xbox_path);
    }

    {
        char linked[512];
        if (resolve_symlink(xbox_path, linked, sizeof(linked)))
            return xbox_translate_path(linked, host_path_buf, buf_size);
    }

    if (xbox_partition_device_path(xbox_path, host_path_buf, buf_size)) {
        fprintf(stderr, "  [PATH] %s -> partition image\n", xbox_path);
        fflush(stderr);
        return TRUE;
    }

    if (xbox_path_redirect) {
        const char *r = xbox_path_redirect(xbox_path);
        if (r) {
            snprintf(host_path_buf, buf_size, "%s", r);
            fprintf(stderr, "  [PATH] %s -> %s\n", xbox_path, r);
            fflush(stderr);
            return TRUE;
        }
    }

    for (int i = 0; i < PATH_RULE_COUNT; i++) {
        skip = match_rule(xbox_path, i);
        if (skip) {
            remainder = xbox_path + skip;
            base_dir  = s_rules[i].to_save ? s_save_dir : s_game_dir;
            sub_dir   = s_rules[i].sub_posix;
            goto translate;
        }
    }

    /* Unrecognized path: pass through, just normalize separators. */
    xbox_log(XBOX_LOG_WARN, XBOX_LOG_PATH, "Unrecognized Xbox path: %s", xbox_path);
    snprintf(host_path_buf, buf_size, "%s", xbox_path);
    for (char* p = host_path_buf; *p; p++)
        if (*p == '\\') *p = '/';
    return TRUE;

translate:
    fprintf(stderr, "  [PATH] %s\n", xbox_path);
    fflush(stderr);
    {
        char remainder_posix[MAX_PATH];
        snprintf(remainder_posix, sizeof(remainder_posix), "%s", remainder);

        /* Xbox paths use backslashes -> POSIX slashes. */
        for (char* p = remainder_posix; *p; p++)
            if (*p == '\\') *p = '/';

        if (sub_dir) {
            snprintf(host_path_buf, buf_size, "%s%s/%s",
                     base_dir, sub_dir, remainder_posix);

            /* Ensure the save directory tree exists. */
            char dir_path[MAX_PATH];
            snprintf(dir_path, sizeof(dir_path), "%s%s", base_dir, sub_dir);
            mkdir_p(dir_path);
        } else {
            snprintf(host_path_buf, buf_size, "%s/%s", base_dir, remainder_posix);
        }

        {
            size_t n = strlen(host_path_buf);
            while (n > 1 && host_path_buf[n - 1] == '/')
                host_path_buf[--n] = '\0';
        }
        /* The disc's file names are upper case and the title asks for them
         * in whatever case its code spells them; Windows does not care, a
         * Linux file system does. */
        posix_ci_resolve(host_path_buf, base_dir);

        XBOX_TRACE(XBOX_LOG_PATH, "%s -> %s", xbox_path, host_path_buf);
        {
            size_t i;
            for (i = 0; i + 1 < MAX_PATH && host_path_buf[i]; i++)
                s_last_host_path_shared[i] = (wchar_t)(unsigned char)host_path_buf[i];
            s_last_host_path_shared[i] = 0;
        }
        return TRUE;
    }
}

#endif /* _WIN32 */
