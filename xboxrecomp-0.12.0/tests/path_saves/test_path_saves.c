/* Partition1's UDATA/TDATA under save_dir, and the one-time copy of the saves
 * an installation already had under game_dir. Windows only; builds against
 * kernel_path.c alone, with the three kernel functions it calls stubbed.
 *
 *   clang -I../../src -I../../src/kernel test_path_saves.c ../../src/kernel/kernel_path.c -lshell32
 *   test_path_saves.exe <empty scratch folder>
 */
#include "kernel.h"
#include <stdio.h>
#include <string.h>
#include <shlobj.h>

const char *xbox_LookupSymbolicLink(const char *link) { (void)link; return NULL; }
void xbox_PadScriptNotePath(const char *p) { (void)p; }
void xbox_log(int level, const char *subsystem, const char *fmt, ...) { (void)level; (void)subsystem; (void)fmt; }

static int fails;
#define CHECK(c, what) do { if (c) printf("ok   %s\n", what); else { printf("FAIL %s\n", what); fails++; } } while (0)

static void put(const char *path, const char *text)
{
    char dir[MAX_PATH], *s;
    FILE *f;
    snprintf(dir, sizeof dir, "%s", path);
    s = strrchr(dir, '\\');
    if (s) { *s = 0; SHCreateDirectoryExA(NULL, dir, NULL); }
    f = fopen(path, "wb");
    if (f) { fputs(text, f); fclose(f); }
}

static int has(const char *path, const char *text)
{
    char buf[256] = {0};
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f) return 0;
    n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    return !strcmp(buf, text);
}

static int exists(const char *path) { return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES; }

static void translate(const char *xp, char *out, size_t n)
{
    WCHAR w[MAX_PATH];
    xbox_translate_path(xp, w, MAX_PATH);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)n, NULL, NULL);
}

int main(int argc, char **argv)
{
    char root[MAX_PATH], game[MAX_PATH], save[MAX_PATH], p[MAX_PATH], q[MAX_PATH];
    if (argc < 2) { printf("usage: test_path_saves <empty scratch folder>\n"); return 2; }
    snprintf(root, sizeof root, "%s", argv[1]);

    /* 1: an existing installation: saves under game_dir, nothing in save_dir */
    snprintf(game, sizeof game, "%s\\a\\game", root);
    snprintf(save, sizeof save, "%s\\a\\save", root);
    snprintf(p, sizeof p, "%s\\UDATA\\45410010\\SLOT\\game.xsv", game); put(p, "dummy save");
    snprintf(p, sizeof p, "%s\\TDATA\\45410010\\settings.bin", game); put(p, "dummy title data");
    xbox_path_init(game, save);
    snprintf(p, sizeof p, "%s\\UDATA\\45410010\\SLOT\\game.xsv", save);
    CHECK(has(p, "dummy save"), "existing UDATA copied into save_dir");
    snprintf(p, sizeof p, "%s\\TDATA\\45410010\\settings.bin", save);
    CHECK(has(p, "dummy title data"), "existing TDATA copied into save_dir");
    snprintf(p, sizeof p, "%s\\UDATA\\45410010\\SLOT\\game.xsv", game);
    CHECK(has(p, "dummy save"), "original left in game_dir");
    snprintf(p, sizeof p, "%s\\UDATA.migrating", save);
    CHECK(!exists(p), "no temporary folder left behind");

    /* the game now reads and writes the copy */
    translate("\\Device\\Harddisk0\\Partition1\\UDATA\\45410010\\SLOT\\game.xsv", q, sizeof q);
    snprintf(p, sizeof p, "%s\\UDATA\\45410010\\SLOT\\game.xsv", save);
    CHECK(!_stricmp(q, p), "Partition1\\UDATA resolves under save_dir");
    translate("\\Device\\Harddisk0\\Partition1\\udata", q, sizeof q);
    snprintf(p, sizeof p, "%s\\UDATA", save);   /* the join's trailing separator is dropped */
    CHECK(!_stricmp(q, p), "case-insensitive, bare folder");
    translate("\\Device\\Harddisk0\\Partition1\\UDATAX\\f", q, sizeof q);
    snprintf(p, sizeof p, "%s\\UDATAX\\f", game);
    CHECK(!_stricmp(q, p), "UDATAX is not UDATA (stays in game_dir)");

    /* 2: save_dir already has saves: never overwritten, even if game_dir differs */
    snprintf(p, sizeof p, "%s\\UDATA\\45410010\\SLOT\\game.xsv", game); put(p, "older save in game_dir");
    xbox_path_init(game, save);
    snprintf(p, sizeof p, "%s\\UDATA\\45410010\\SLOT\\game.xsv", save);
    CHECK(has(p, "dummy save"), "second start: save_dir copy not overwritten");

    /* 3: game_dir and save_dir the same folder: nothing to do */
    snprintf(game, sizeof game, "%s\\b", root);
    snprintf(p, sizeof p, "%s\\UDATA\\x.xsv", game); put(p, "same");
    xbox_path_init(game, game);
    snprintf(p, sizeof p, "%s\\UDATA.migrating", game);
    CHECK(!exists(p), "same folder: no copy attempted");
    snprintf(p, sizeof p, "%s\\UDATA\\x.xsv", game);
    CHECK(has(p, "same"), "same folder: save untouched");

    /* 4: a fresh installation: no saves anywhere, nothing created */
    snprintf(game, sizeof game, "%s\\c\\game", root);
    snprintf(save, sizeof save, "%s\\c\\save", root);
    SHCreateDirectoryExA(NULL, game, NULL);
    xbox_path_init(game, save);
    snprintf(p, sizeof p, "%s\\UDATA", save);
    CHECK(!exists(p), "fresh install: no empty UDATA invented");

    /* 5: a copy interrupted on a previous start is completed, not taken as done */
    snprintf(game, sizeof game, "%s\\d\\game", root);
    snprintf(save, sizeof save, "%s\\d\\save", root);
    snprintf(p, sizeof p, "%s\\UDATA\\45410010\\SLOT\\game.xsv", game); put(p, "full save");
    snprintf(p, sizeof p, "%s\\UDATA.migrating\\45410010\\SLOT\\game.xsv", save); put(p, "trunc");
    xbox_path_init(game, save);
    snprintf(p, sizeof p, "%s\\UDATA\\45410010\\SLOT\\game.xsv", save);
    CHECK(has(p, "full save"), "interrupted copy redone in full");
    snprintf(p, sizeof p, "%s\\UDATA.migrating", save);
    CHECK(!exists(p), "interrupted copy: temporary folder gone");

    printf(fails ?"%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
