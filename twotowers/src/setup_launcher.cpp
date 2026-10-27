// The Two Towers Recompiled - first-launch setup and launcher.
//
// Like the Mercenaries launcher, this stays outside the recompiled game process:
// it validates/extracts the user's retail disc (ISO or an extracted folder),
// stores display settings in twotowers_recomp.ini, and starts twotowers_recomp.exe.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <bcrypt.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
#include "../tools/ultra_textures/ultra_build.h"

static const char *kXbeSha256 = "ae2d1b5304b3e9572339b693a6a45ad0869bdaeec89aae374b5c14feebbb8d30";
static const wchar_t *kTitle = L"The Two Towers Recompiled";

// ───────────────────────────── XISO (XDVDFS) reader ─────────────────────────────

struct IsoFile {
    std::string path;
    uint64_t size = 0, offset = 0;
    bool dir = false;
};

static bool readAt(std::ifstream &in, uint64_t off, void *buf, size_t n)
{
    in.clear();
    in.seekg((std::streamoff)off);
    if (!in) return false;
    in.read((char *)buf, (std::streamsize)n);
    return (size_t)in.gcount() == n;
}

static bool scanIso(const fs::path &iso, std::vector<IsoFile> &files, std::string &err)
{
    std::ifstream in(iso, std::ios::binary);
    if (!in) { err = "Cannot open the ISO."; return false; }
    const uint64_t kSector = 2048, parts[] = {0, 0x18300000ULL, 0x0FD90000ULL, 0x02080000ULL};
    uint64_t part = 0;
    char magic[20];
    bool found = false;
    for (auto p : parts)
        if (readAt(in, p + 32 * kSector, magic, 20) && !memcmp(magic, "MICROSOFT*XBOX*MEDIA", 20)) { part = p; found = true; break; }
    if (!found) { err = "This is not an original Xbox disc image."; return false; }
    uint32_t rootSector = 0, rootSize = 0;
    readAt(in, part + 32 * kSector + 20, &rootSector, 4);
    readAt(in, part + 32 * kSector + 24, &rootSize, 4);
    std::function<void(uint32_t, uint32_t, const std::string &, int)> walkDir = [&](uint32_t sector, uint32_t size, const std::string &prefix, int depth) {
        if (!size || depth > 32 || size > 64u * 1024 * 1024) return;
        std::vector<uint8_t> t(size);
        if (!readAt(in, part + sector * kSector, t.data(), size)) return;
        std::set<uint32_t> seen;
        std::function<void(uint32_t)> node = [&](uint32_t off) {
            if (off + 14 > t.size() || !seen.insert(off).second) return;
            uint16_t l, r;
            uint32_t start, fsize;
            memcpy(&l, &t[off], 2);
            memcpy(&r, &t[off + 2], 2);
            memcpy(&start, &t[off + 4], 4);
            memcpy(&fsize, &t[off + 8], 4);
            uint8_t attr = t[off + 12], nlen = t[off + 13];
            if (l == 0xFFFF && r == 0xFFFF) return;
            if (off + 14 + nlen > t.size()) return;
            if (l) node(l * 4u);
            std::string name((const char *)&t[off + 14], nlen);
            if (!name.empty() && name != "." && name != ".." && name.find_first_of("/\\:") == std::string::npos) {
                IsoFile f;
                f.path = prefix.empty() ? name : prefix + "/" + name;
                f.dir = (attr & 0x10) != 0;
                f.size = f.dir ? 0 : fsize;
                f.offset = part + (uint64_t)start * kSector;
                files.push_back(f);
                if (f.dir) walkDir(start, fsize, f.path, depth + 1);
            }
            if (r) node(r * 4u);
        };
        node(0);
    };
    walkDir(rootSector, rootSize, "", 0);
    if (files.empty()) { err = "The disc image has no files."; return false; }
    return true;
}

static std::string lower(std::string s)
{
    for (auto &c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// ───────────────────────────── helpers ─────────────────────────────

static std::wstring W(const std::string &s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static std::string sha256Hex(const std::vector<uint8_t> &d)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    unsigned char dig[32] = {};
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
        if (BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0) {
            BCryptHashData(h, (PUCHAR)d.data(), (ULONG)d.size(), 0);
            BCryptFinishHash(h, dig, 32, 0);
            BCryptDestroyHash(h);
        }
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    std::string s;
    char b[3];
    for (unsigned char c : dig) { snprintf(b, 3, "%02x", c); s += b; }
    return s;
}

static fs::path exeDir()
{
    wchar_t b[MAX_PATH];
    GetModuleFileNameW(nullptr, b, MAX_PATH);
    return fs::path(b).parent_path();
}
static fs::path iniPath() { return exeDir() / L"twotowers_recomp.ini"; }

static std::wstring iniGet(const wchar_t *sec, const wchar_t *key, const wchar_t *def)
{
    wchar_t b[1024];
    GetPrivateProfileStringW(sec, key, def, b, 1024, iniPath().c_str());
    return b;
}
static void iniSet(const wchar_t *sec, const wchar_t *key, const std::wstring &v)
{
    WritePrivateProfileStringW(sec, key, v.c_str(), iniPath().c_str());
}

// ───────────────────────────── Language ─────────────────────────────
// [Language] language = <code>: the pack in lang/<code>/ (made by
// tools/lang/build_lang.py). text.txt holds "L:<English>\t<translation>"
// for the launcher's own words; anything missing stays English.

#include <map>
static std::map<std::wstring, std::wstring> g_tr;
static bool g_rtl;
static std::wstring unesc(const std::wstring &v)
{
    std::wstring o;
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i] == L'\\' && i + 1 < v.size()) {
            wchar_t c = v[++i];
            o += c == L'n' ? L'\n' : c == L't' ? L'\t' : c == L'r' ? L'\r' : c;
        } else o += v[i];
    }
    return o;
}
static std::wstring langCode() { return iniGet(L"Language", L"language", L"en"); }
static void langLoad()
{
    std::wstring code = iniGet(L"Setup", L"language", langCode().c_str());
    if (code.rfind(L"pal-", 0) == 0) code = code.substr(4);
    if (code.empty() || code == L"en") return;
    fs::path d = exeDir() / L"launcher-languages" / code;
    if (!fs::exists(d / L"text.txt")) d = exeDir() / L"lang" / code;
    std::ifstream in(d / L"text.txt", std::ios::binary);
    std::string ln;
    while (std::getline(in, ln)) {
        if (!ln.empty() && ln.back() == '\r') ln.pop_back();
        if (ln.compare(0, 2, "L:") != 0) continue;
        size_t t = ln.find('\t');
        if (t == std::string::npos) continue;
        g_tr[unesc(W(ln.substr(2, t - 2)))] = unesc(W(ln.substr(t + 1)));
    }
    std::ifstream inf(d / L"info.txt", std::ios::binary);
    while (std::getline(inf, ln)) if (ln.compare(0, 5, "rtl=1") == 0) g_rtl = true;
    std::ifstream overlay(exeDir() / L"launcher_ui.tsv", std::ios::binary);
    while (std::getline(overlay, ln)) {
        if (!ln.empty() && ln.back() == '\r') ln.pop_back();
        auto a = ln.find('\t'), b = a == std::string::npos ? a : ln.find('\t', a + 1);
        if (b != std::string::npos && W(ln.substr(0,a)) == code)
            g_tr[unesc(W(ln.substr(a+1,b-a-1)))] = unesc(W(ln.substr(b+1)));
    }
}
static const wchar_t *T(const wchar_t *en)
{
    if (!en || g_tr.empty()) return en;
    auto it = g_tr.find(en);
    return it == g_tr.end() ? en : it->second.c_str();
}
static std::string T8(const std::string &en)
{
    if (g_tr.empty()) return en;
    std::wstring w = W(en);
    auto it = g_tr.find(w);
    if (it == g_tr.end()) return en;
    int n = WideCharToMultiByte(CP_UTF8, 0, it->second.c_str(), (int)it->second.size(), nullptr, 0, nullptr, nullptr);
    std::string o(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, it->second.c_str(), (int)it->second.size(), &o[0], n, nullptr, nullptr);
    return o;
}
struct LangItem { std::wstring code, name; };
static std::vector<LangItem> langList()
{
    std::vector<LangItem> v{{L"", T(L"(as the text)")}, {L"en", L"English (USA)"}};
    std::error_code ec;
    std::vector<LangItem> more;
    for (auto &e : fs::directory_iterator(exeDir() / L"lang", ec)) {
        if (!e.is_directory() || !fs::is_regular_file(e.path() / L"game.txt")) continue;
        std::ifstream inf(e.path() / L"info.txt", std::ios::binary);
        std::string ln, name;
        while (std::getline(inf, ln)) {
            if (!ln.empty() && ln.back() == '\r') ln.pop_back();
            if (ln.compare(0, 5, "name=") == 0) name = ln.substr(5);
        }
        if (!name.empty()) {
            auto code = e.path().filename().wstring();
            more.push_back({code, W(name) + L" (" + T(code.rfind(L"pal-", 0) == 0 ? L"Official game text (PAL)" : L"Port translation (community)") + L")"});
        }
    }
    std::sort(more.begin(), more.end(), [](auto &a, auto &b) { return a.code < b.code; });
    v.insert(v.end(), more.begin(), more.end());
    return v;
}
// [Language] fmv= / voice=: the films' and the voices' languages, apart from
// the text's (lang.c). "" = the text's; "en" = the disc's own. Films:
// lang/fmv/<code>/ (made by pal_lang_pack.py, ps2_idioma.py); voices: lang/voz/<code>/.
static std::wstring mediaName(const std::wstring &c)
{
    static const struct { const wchar_t *code, *name; } N[] = {
        {L"es", L"Español"}, {L"it", L"Italiano"}, {L"fr", L"Français"}, {L"de", L"Deutsch"},
        {L"uk", L"English (UK)"}, {L"nl", L"Nederlands"}, {L"sv", L"Svenska"}, {L"pl", L"Polski"},
        {L"pt", L"Português"}, {L"ru", L"Русский"}, {L"ja", L"日本語"}};
    for (auto &n : N) if (!_wcsicmp(c.c_str(), n.code)) return n.name;
    return c;
}
static bool mediaFiles(const fs::path &dir, bool films)
{
    std::error_code ec;
    for (auto &e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        std::string n = lower(e.path().filename().string());
        if (films ? e.path().extension() != L".bik" : n.rfind("s_sb_", 0) != 0 || e.path().extension() != L".xsb") continue;
        std::ifstream f(e.path(), std::ios::binary); char h[6]{}; f.read(h, 6);
        if (f && (films ? !memcmp(h, "BIKi", 4) : !memcmp(h, "SDBK\5\0", 6))) return true;
    }
    return false;
}
static std::vector<LangItem> mediaList(bool films, bool cine = false)
{
    std::vector<LangItem> v{{L"", T(L"(as the text)")}, {L"en", L"English (USA)"}};
    std::set<std::wstring> codes;
    std::error_code ec;
    for (auto &e : fs::directory_iterator(exeDir() / L"lang" / (films ? L"fmv" : L"voz"), ec)) {
        if (!e.is_directory() || !mediaFiles(e.path(), films)) continue;
        std::wstring c = e.path().filename().wstring();
        if (!films) {
            size_t plus = c.find(L'+');
            if (plus != std::wstring::npos) c = cine ? c.substr(plus + 1) : c.substr(0, plus);
        }
        if (!c.empty() && c != L"en" && c != L"sv" && c != L"nl") codes.insert(c);
    }
    if (films) for (auto &e : fs::directory_iterator(exeDir() / L"lang", ec)) {
        std::wstring c = e.path().filename().wstring();
        if (c.rfind(L"pal-",0)==0 && mediaFiles(e.path()/L"fmv",true)) {
            c=c.substr(4); if (c!=L"sv" && c!=L"nl" && c!=L"en") codes.insert(c);
        }
    }
    for (auto &c : codes) v.push_back({c, mediaName(c)});
    return v;
}

static bool xbeOk(const fs::path &dir, std::string *why)
{
    fs::path x;
    std::error_code ec;
    for (auto &e : fs::directory_iterator(dir, ec))
        if (lower(e.path().filename().string()) == "default.xbe") x = e.path();
    if (x.empty()) { if (why) *why = T8("default.xbe was not found in that folder."); return false; }
    std::ifstream in(x, std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (sha256Hex(d) != kXbeSha256) {
        if (why) *why = T8("That default.xbe is not the USA retail release this port was built from.");
        return false;
    }
    return true;
}

static fs::path currentGameDir()
{
    std::wstring g = iniGet(L"Paths", L"game_dir", L"");
    return g.empty() ? exeDir() / L"game_files" : fs::path(g);
}

// ───────────────────────────── UI ─────────────────────────────

enum { ID_ISO = 100, ID_DIR, ID_RES, ID_MODE, ID_INTERNAL, ID_VSYNC, ID_WIDE, ID_LOG, ID_PLAY, ID_OPEN, ID_PROGRESS, ID_STATUS, ID_GFX, ID_CTL, ID_LANG, ID_FMV, ID_VOICE, ID_SUBS, ID_CINE, ID_DLSS5, ID_CREDITS };
static const UINT WM_STATUS = WM_APP + 1, WM_PROGRESS = WM_APP + 2, WM_DONE = WM_APP + 3;

static HWND g_hwnd, g_status, g_progress, g_res, g_mode, g_internal, g_vsync, g_wide, g_log, g_play;
static HFONT g_font;
static int g_fontH = 16;
static std::atomic<bool> g_busy{false};

struct Res { int w, h; };
static std::vector<Res> g_resList;
static const Res kInternal[] = {{640, 480}, {1280, 960}, {1920, 1440}, {2560, 1920}};

static void setStatus(const std::wstring &s) { SetWindowTextW(g_status, s.c_str()); }
static void postStatus(const std::string &s) { PostMessageW(g_hwnd, WM_STATUS, 0, (LPARAM) new std::wstring(W(s))); }

static void refreshStatus()
{
    fs::path g = currentGameDir();
    std::string why;
    if (fs::exists(g) && xbeOk(g, &why)) {
        setStatus(std::wstring(T(L"Game files ready:  ")) + g.wstring());
        EnableWindow(g_play, TRUE);
    } else {
        setStatus(T(L"Game files not installed yet. Select your The Two Towers (USA) Xbox ISO or extracted disc folder."));
        EnableWindow(g_play, FALSE);
    }
}

static void extractIso(fs::path iso)
{
    std::vector<IsoFile> files;
    std::string err;
    fs::path out = exeDir() / L"game_files";
    if (!scanIso(iso, files, err)) { postStatus(T8("Error: ") + err); PostMessageW(g_hwnd, WM_DONE, 0, 0); return; }
    auto xbe = std::find_if(files.begin(), files.end(), [](auto &f) { return lower(f.path) == "default.xbe"; });
    if (xbe == files.end()) { postStatus(T8("Error: the disc has no default.xbe.")); PostMessageW(g_hwnd, WM_DONE, 0, 0); return; }
    std::ifstream in(iso, std::ios::binary);
    std::vector<uint8_t> xd(xbe->size);
    readAt(in, xbe->offset, xd.data(), xd.size());
    if (sha256Hex(xd) != kXbeSha256) {
        postStatus(T8("Error: this ISO is not the USA retail The Two Towers disc this port supports."));
        PostMessageW(g_hwnd, WM_DONE, 0, 0);
        return;
    }
    uint64_t total = 0, done = 0;
    for (auto &f : files) total += f.size;
    std::vector<char> buf(4 << 20);
    try {
        fs::create_directories(out);
        for (auto &f : files) {
            fs::path dst = out / fs::u8path(f.path);
            if (f.dir) { fs::create_directories(dst); continue; }
            fs::create_directories(dst.parent_path());
            std::ofstream o(dst, std::ios::binary | std::ios::trunc);
            uint64_t left = f.size, pos = f.offset;
            while (left) {
                size_t n = (size_t)std::min<uint64_t>(left, buf.size());
                if (!readAt(in, pos, buf.data(), n)) throw std::runtime_error("read failed: " + f.path);
                o.write(buf.data(), (std::streamsize)n);
                left -= n; pos += n; done += n;
                PostMessageW(g_hwnd, WM_PROGRESS, (WPARAM)(total ? done * 1000 / total : 0), 0);
            }
            postStatus(T8("Extracting ") + f.path);
        }
    } catch (const std::exception &e) {
        postStatus(T8("Error: ") + e.what());
        PostMessageW(g_hwnd, WM_DONE, 0, 0);
        return;
    }
    iniSet(L"Paths", L"game_dir", L"");
    PostMessageW(g_hwnd, WM_DONE, 1, 0);
}

/* Ultra textures: the GLOS69 pack (downloaded separately) made into the
 * files the game reads (tools/ultra_textures/ultra_build.h), once. */
static bool g_ultraOnly;   // --build-ultra: started by the game; closes when done
/* the builder's words (ultra_build.h): its phrases, each translated */
static std::string trWhat(std::string w)
{
    static const char *pre[] = {"Converting ", "Softening the edges of ", "Fixing the transparency of ", "Ultra textures ready: "};
    static const char *whole[] = {"Writing the index", "The GLOS69 texture pack is not in the game's folder.",
                                  "Could not write textures_ultra/index.bin."};
    for (const char *x : whole) if (w == x) return T8(x);
    for (const char *x : pre)
        if (w.compare(0, strlen(x), x) == 0) { w = T8(x) + w.substr(strlen(x)); break; }
    for (const char *x : {" pictures", " could not be made"}) {
        size_t at = w.find(x);
        if (at != std::string::npos) w = w.substr(0, at) + T8(x) + w.substr(at + strlen(x));
    }
    return w;
}

static void ultraBuild(bool thenPlay)
{
    if (g_busy) return;
    g_busy = true;
    EnableWindow(g_play, FALSE);
    setStatus(T(L"Building the Ultra textures from the GLOS69 pack (once; a few minutes)..."));
    std::thread([thenPlay] {
        std::string err;
        int last = -1;
        int r = ultra_build::build(exeDir(), [&](double f, const std::string &what) {
            int p = (int)(f * 1000);
            if (p != last) { last = p; PostMessageW(g_hwnd, WM_PROGRESS, (WPARAM)p, 0); }
            static int n;
            if ((n++ & 7) == 0 || f >= 1.0) postStatus(T8("Ultra textures (once): ") + trWhat(what));
        }, err);
        if (r) postStatus(T8("Ultra textures: ") + trWhat(err));
        PostMessageW(g_hwnd, WM_DONE, r == 0, thenPlay ? 2 : 1);
    }).detach();
}

static void pickIso()
{
    if (g_busy) return;
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"Xbox disc image (*.iso;*.xiso)\0*.iso;*.xiso\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    g_busy = true;
    EnableWindow(g_play, FALSE);
    setStatus(T(L"Checking disc..."));
    std::thread(extractIso, fs::path(file)).detach();
}

static void pickFolder()
{
    BROWSEINFOW bi{};
    bi.hwndOwner = g_hwnd;
    bi.lpszTitle = T(L"Select the extracted The Two Towers disc folder (contains default.xbe)");
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST p = SHBrowseForFolderW(&bi);
    if (!p) return;
    wchar_t path[MAX_PATH];
    bool ok = SHGetPathFromIDListW(p, path);
    CoTaskMemFree(p);
    if (!ok) return;
    std::string why;
    if (!xbeOk(path, &why)) { MessageBoxW(g_hwnd, W(why).c_str(), kTitle, MB_ICONWARNING); return; }
    iniSet(L"Paths", L"game_dir", path);
    refreshStatus();
}

static void saveSettings()
{
    iniSet(L"Display", L"window", SendMessageW(g_wide, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0");
    iniSet(L"Developer", L"logging", SendMessageW(g_log, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0");
}

static void loadSettings()
{
    SendMessageW(g_wide, BM_SETCHECK, iniGet(L"Display", L"window", L"1") == L"1" ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_log, BM_SETCHECK, iniGet(L"Developer", L"logging", L"1") == L"1" ? BST_CHECKED : BST_UNCHECKED, 0);
}


// ───────────────────────────── Graphics settings ─────────────────────────────
// A small modal window with one drop-down per [Display] option. The same
// options are available in game (F10, or hold Back + press Start).

struct GfxChoice { const wchar_t *label, *value; };
struct GfxOpt {
    const wchar_t *label, *key, *def;
    std::vector<GfxChoice> choices;
    HWND lbl = nullptr, combo = nullptr;
    std::wstring extra;   // a custom ini value not in the list, kept as an extra item
    const wchar_t *sec = L"Display";
};

static std::vector<GfxOpt> g_gfx;
static HWND g_gfxWnd;
static bool g_gfxDone;

static void gfxBuildOptions()
{
    g_gfx.clear();
    g_gfx.push_back({L"Renderer", L"renderer", L"d3d11",
        {{L"Direct3D 11", L"d3d11"}, {L"Vulkan (DXVK, put in the dxvk folder)", L"vulkan"},
         {L"Software (no graphics card)", L"software"}}});
    g_gfx.push_back({L"Frame rate", L"fps", L"30",
        {{L"30 fps (as on the Xbox)", L"30"}, {L"60 fps (experimental)", L"60"}, {L"75 fps", L"75"}, {L"90 fps", L"90"},
         {L"100 fps", L"100"}, {L"120 fps", L"120"}, {L"144 fps", L"144"}, {L"165 fps", L"165"}, {L"200 fps", L"200"},
         {L"240 fps", L"240"}, {L"300 fps", L"300"}, {L"Unlocked (experimental)", L"unlocked"}}});
    g_gfx.push_back({L"Display mode", L"fullscreen", L"0",
        {{L"Windowed", L"0"}, {L"Full screen (borderless)", L"1"}, {L"Exclusive full screen", L"2"}}});
    {   // exclusive full screen's resolutions: the monitor's own, largest first
        static std::vector<std::wstring> names, vals;
        static bool listed;
        if (!listed) {
            DEVMODEW dm{}; dm.dmSize = sizeof dm;
            std::vector<std::pair<int, int>> m;
            for (DWORD i = 0; EnumDisplaySettingsW(nullptr, i, &dm) && i < 4000; i++) {
                if (dm.dmBitsPerPel != 32 || dm.dmPelsWidth < 640 || dm.dmPelsHeight < 480) continue;
                std::pair<int, int> r((int)dm.dmPelsWidth, (int)dm.dmPelsHeight);
                if (std::find(m.begin(), m.end(), r) == m.end()) m.push_back(r);
            }
            std::sort(m.begin(), m.end(), [](const std::pair<int, int> &a, const std::pair<int, int> &b) {
                return (long)a.first * a.second != (long)b.first * b.second ? (long)a.first * a.second > (long)b.first * b.second : a.first > b.first; });
            {   // (1.9.3) as the game's list (gfx_menu.c): the screen's own, the common ones it offers (720p and below), the largest of the rest
                static const int pref[][2] = { {3840,2160}, {2560,1600}, {2560,1440}, {1920,1200}, {1920,1080}, {1680,1050}, {1600,900},
                                               {1440,900}, {1366,768}, {1280,1024}, {1280,800}, {1280,720}, {1024,768}, {960,540}, {800,600}, {640,480} };
                std::vector<char> keep(m.size(), 0);
                size_t nk = 0;
                if (!m.empty()) { keep[0] = 1; nk = 1; }
                for (auto &pr : pref)
                    for (size_t k = 0; k < m.size(); k++) if (!keep[k] && m[k].first == pr[0] && m[k].second == pr[1] && nk < 23) { keep[k] = 1; nk++; }
                for (size_t k = 0; k < m.size() && nk < 23; k++) if (!keep[k]) { keep[k] = 1; nk++; }
                std::vector<std::pair<int, int>> k2;
                for (size_t k = 0; k < m.size(); k++) if (keep[k]) k2.push_back(m[k]);
                m.swap(k2);
                std::reverse(m.begin(), m.end());   // smallest first, as the window sizes: right is higher
            }
            names.reserve(24); vals.reserve(24);
            for (auto &r : m) { names.push_back(std::to_wstring(r.first) + L" x " + std::to_wstring(r.second)); vals.push_back(std::to_wstring(r.first) + L"x" + std::to_wstring(r.second)); }
            listed = true;
        }
        GfxOpt o{L"Full-screen resolution (exclusive)", L"fs_resolution", L"", {{L"The desktop's", L""}}};
        for (size_t i = 0; i < names.size(); i++) o.choices.push_back({names[i].c_str(), vals[i].c_str()});
        g_gfx.push_back(o);
    }
    g_gfx.push_back({L"Window size", L"window_size", L"",
        {{L"Automatic", L""}, {L"1280 x 720", L"1280x720"}, {L"1280 x 960", L"1280x960"}, {L"1600 x 900", L"1600x900"},
         {L"1920 x 1080", L"1920x1080"}, {L"1920 x 1200", L"1920x1200"}, {L"2560 x 1080", L"2560x1080"},
         {L"2560 x 1440", L"2560x1440"}, {L"3440 x 1440", L"3440x1440"}, {L"3840 x 2160", L"3840x2160"},
         {L"5120 x 1440", L"5120x1440"}}});
    g_gfx.push_back({L"Aspect ratio", L"aspect", L"4:3",
        {{L"4:3 (original)", L"4:3"}, {L"16:9 widescreen", L"16:9"}, {L"16:10", L"16:10"},
         {L"21:9 ultrawide", L"21:9"}, {L"32:9 super ultrawide", L"32:9"}}});
    g_gfx.push_back({L"HUD in widescreen", L"hud", L"edges",
        {{L"At the screen's edges", L"edges"}, {L"Centred (4:3)", L"centre"}}});
    g_gfx.push_back({L"Menus", L"menu_fill", L"1",
        {{L"Fill the screen (past the old TV margin)", L"1"}, {L"Original (with a margin)", L"0"}}});
    g_gfx.push_back({L"Movies in widescreen", L"movie_fill", L"0",
        {{L"As made (4:3)", L"0"}, {L"Fill the width (black bands cut off)", L"1"}}});
    g_gfx.push_back({L"Widescreen cutscenes", L"wide_scenes", L"0",
        {{L"Off (4:3, side bars)", L"0"}, {L"On (the sky's edges can show)", L"1"}}});
    g_gfx.push_back({L"Ultra textures", L"ultra_textures", L"0",
        {{L"Off", L"0"}, {L"On (sharper menus and HUD; everything with the GLOS69 pack)", L"1"}}});
    g_gfx.push_back({L"Internal resolution", L"render_scale", L"1",
        {{L"1x  (640 x 480, original)", L"1"}, {L"2x  (1280 x 960)", L"2"}, {L"3x  (1920 x 1440)", L"3"}, {L"4x  (2560 x 1920)", L"4"}, {L"4.5x (2160 lines: 3840 x 2160 at 16:9)", L"4.5"}, {L"5x  (3200 x 2400)", L"5"}, {L"6x  (3840 x 2880)", L"6"}, {L"7x  (4480 x 3360)", L"7"}, {L"8x  (5120 x 3840)", L"8"},
         {L"9x  (5760 x 4320)", L"9"}, {L"10x (6400 x 4800)", L"10"}, {L"11x (7040 x 5280)", L"11"}, {L"12x (7680 x 5760)", L"12"}}});
    g_gfx.push_back({L"Anisotropic filtering", L"anisotropic", L"1",
        {{L"Off", L"1"}, {L"2x", L"2"}, {L"4x", L"4"}, {L"8x", L"8"}, {L"16x", L"16"}}});
    g_gfx.push_back({L"Mipmaps", L"mipmaps", L"1", {{L"On (less shimmer)", L"1"}, {L"Off", L"0"}}});
    g_gfx.push_back({L"Anti-aliasing", L"antialiasing", L"off", {{L"Off", L"off"}, {L"FXAA (the finished picture)", L"fxaa"},
        {L"MSAA 2x", L"msaa2"}, {L"MSAA 4x", L"msaa4"}, {L"MSAA 8x", L"msaa8"}, {L"MSAA 4x + FXAA", L"msaa4+fxaa"}, {L"MSAA 8x + FXAA", L"msaa8+fxaa"}}});
    g_gfx.push_back({L"VSync", L"vsync", L"0", {{L"Off", L"0"}, {L"On (no tearing)", L"1"}}});
    g_gfx.push_back({L"Picture shape", L"stretch", L"0", {{L"Keep aspect (black bars)", L"0"}, {L"Stretch to fill", L"1"}}});
    g_gfx.push_back({L"Vertex programs", L"vertex_programs", L"gpu",
        {{L"Graphics card (faster)", L"gpu"}, {L"CPU (old method)", L"cpu"}}});
    g_gfx.push_back({L"Brightness", L"brightness", L"1",
        {{L"Darker", L"0.85"}, {L"Original", L"1"}, {L"Brighter", L"1.15"}, {L"Brighter +", L"1.3"}, {L"Brightest", L"1.45"}}});
    g_gfx.push_back({L"Sharpening", L"sharpening", L"0",
        {{L"Off", L"0"}, {L"Low", L"0.4"}, {L"Medium", L"0.7"}, {L"High", L"1"}}});
    g_gfx.push_back({L"Colour", L"colour", L"1", {{L"Original", L"1"}, {L"Vivid", L"1.25"}, {L"Muted", L"0.8"}}});
    g_gfx.push_back({L"Button prompts", L"prompts", L"auto",
        {{L"Automatic (follows the pad or keys you use)", L"auto"}, {L"Xbox", L"xbox"}, {L"Xbox One", L"xboxone"}, {L"PlayStation", L"playstation"},
         {L"Keyboard / mouse (your bound keys)", L"keyboard"}}});
    g_gfx.back().sec = L"Input";
    g_gfx.push_back({L"Mouse pointer in menus", L"menu_mouse", L"1",
        {{L"On (point and click in the menus)", L"1"}, {L"Off (mouse buttons as bound)", L"0"}}});
    g_gfx.back().sec = L"Input";
    g_gfx.push_back({L"Mouse sensitivity", L"mouse_sensitivity", L"1",
        {{L"Low", L"0.5"}, {L"Medium", L"0.75"}, {L"Normal", L"1"}, {L"High", L"1.5"}, {L"Very high", L"2"}}});
    g_gfx.back().sec = L"Input";
    g_gfx.push_back({L"Pointer size", L"cursor_size", L"normal",
        {{L"Small", L"small"}, {L"Normal", L"normal"}, {L"Large", L"large"}, {L"Extra large", L"xlarge"}}});
    g_gfx.back().sec = L"Input";
    g_gfx.push_back({L"Mouse aiming (ranged)", L"mouse_aim", L"1",
        {{L"On (hold the ranged button, move the mouse)", L"1"}, {L"Off", L"0"}}});
    g_gfx.back().sec = L"Input";
    g_gfx.push_back({L"Mouse aiming: invert", L"mouse_invert", L"0",
        {{L"Off", L"0"}, {L"Up / down", L"y"}, {L"Left / right", L"x"}, {L"Both", L"xy"}}});
    g_gfx.back().sec = L"Input";
    g_gfx.push_back({L"Mouse steering (movement)", L"mouse_steer", L"0",
        {{L"Off", L"0"}, {L"On (hold W: he runs the way the mouse pushes; Walk walks)", L"1"}}});
    g_gfx.back().sec = L"Input";
    g_gfx.push_back({L"Mod: ECTS 2002 HUD (experimental)", L"e3_hud", L"0",
        {{L"Off (retail HUD)", L"0"}, {L"On (ECTS 2002 demo HUD)", L"1"}}});
    g_gfx.back().sec = L"Mods";
    g_gfx.push_back({L"Mod: Orange health pickups (shown in pre-release & making-of videos)", L"e3_pickups", L"0",
        {{L"Off (green, as retail)", L"0"}, {L"On (orange health pickups)", L"1"}}});
    g_gfx.back().sec = L"Mods";
    g_gfx.push_back({L"Mod: 13 extra heroes", L"extra_heroes", L"3",
        {{L"Off (four heroes, as retail)", L"0"}, {L"Unlock through Isildur and Orthanc victories (progress counts)", L"1"},
         {L"On, unlocked for testing (save not changed)", L"2"},
         {L"Extras unlocked from the start (progress counts)", L"3"}}});
    g_gfx.back().sec = L"Mods";
    for (auto &o : g_gfx) {
        o.label = T(o.label);
        for (auto &c : o.choices) c.label = T(c.label);
    }
}

static void gfxLoad(bool defaults)
{
    for (auto &o : g_gfx) {
        std::wstring v = defaults ? std::wstring(o.def) : iniGet(o.sec, o.key, o.def);
        SendMessageW(o.combo, CB_RESETCONTENT, 0, 0);
        int sel = -1;
        for (size_t i = 0; i < o.choices.size(); i++) {
            SendMessageW(o.combo, CB_ADDSTRING, 0, (LPARAM)o.choices[i].label);
            if (_wcsicmp(o.choices[i].value, v.c_str()) == 0) sel = (int)i;
        }
        o.extra.clear();
        if (sel < 0 && !v.empty()) {      // a custom value typed into the ini
            o.extra = v;
            sel = (int)SendMessageW(o.combo, CB_ADDSTRING, 0, (LPARAM)(std::wstring(T(L"Custom: ")) + v).c_str());
        }
        SendMessageW(o.combo, CB_SETCURSEL, sel < 0 ? 0 : sel, 0);
    }
}

static void gfxSave()
{
    for (auto &o : g_gfx) {
        int i = (int)SendMessageW(o.combo, CB_GETCURSEL, 0, 0);
        std::wstring v;
        if (i >= 0 && i < (int)o.choices.size()) v = o.choices[i].value;
        else if (i >= (int)o.choices.size()) v = o.extra;
        else v = o.def;
        iniSet(o.sec, o.key, v);
    }
}

enum { ID_GFX_OK = 300, ID_GFX_CANCEL, ID_GFX_DEFAULTS };

static LRESULT CALLBACK gfxProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_GFX_OK: {
            std::wstring was = iniGet(L"Display", L"ultra_textures", L"0");
            gfxSave();
            DestroyWindow(h);
            if (was != L"1" && iniGet(L"Display", L"ultra_textures", L"0") == L"1" && ultra_build::find_pack(exeDir()).empty())
                MessageBoxW(g_hwnd, (std::wstring(T(L"Ultra textures are on: the menus, level select and HUD are sharper.\n\n")) +
                                     T(L"For every texture, download the GameCube high-resolution texture pack (GLOS69) ") +
                                     T(L"from its author and put its GLOS69 folder here:\n\n") + exeDir().wstring() +
                                     T(L"\n\nThe launcher makes it ready the next time you press Play (once; a few minutes).")).c_str(),
                            kTitle, MB_ICONINFORMATION);
            return 0;
        }
        case IDCANCEL:
        case ID_GFX_CANCEL: DestroyWindow(h); return 0;
        case ID_GFX_DEFAULTS: gfxLoad(true); return 0;
        }
        break;
    case WM_CTLCOLORSTATIC: SetBkMode((HDC)wp, TRANSPARENT); return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: g_gfxDone = true; return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static void graphicsSettings()
{
    static bool reg = false;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!reg) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = gfxProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        wc.lpszClassName = L"TwoTowersGfx";
        wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
        RegisterClassExW(&wc);
        reg = true;
    }
    gfxBuildOptions();
    int fh = g_fontH, M = std::max(14, fh), gap = std::max(6, fh / 2), ch = fh + fh / 2 + 8;
    int labW = fh * 11, comboW = fh * 17;
    /* two columns side by side (one column grew taller than many screens) */
    int colW = labW + gap + comboW, colGap = M * 2;
    int cw = M + colW + colGap + colW + M;
    int rows = (int)g_gfx.size();
    int half = (rows + 1) / 2;
    int noteH = fh * 3;
    /* a label too long for its column wraps: that row gets the text's height */
    std::vector<int> rowH(rows, ch);
    {
        HDC dc = GetDC(nullptr);
        HGDIOBJ of = SelectObject(dc, g_font);
        for (int i = 0; i < rows; i++) {
            RECT r{0, 0, labW, 0};
            DrawTextW(dc, g_gfx[i].label, -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            if (r.bottom + 4 > ch) rowH[i] = r.bottom + 4;
        }
        SelectObject(dc, of);
        ReleaseDC(nullptr, dc);
    }
    int colH[2] = {0, 0};
    for (int i = 0; i < rows; i++) colH[i < half ? 0 : 1] += rowH[i] + gap;
    int chH = M + std::max(colH[0], colH[1]) + noteH + gap + ch + M;
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    RECT wr{0, 0, cw, chH};
    AdjustWindowRect(&wr, style, FALSE);
    RECT pr;
    GetWindowRect(g_hwnd, &pr);
    int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
    int wx = pr.left + ((pr.right - pr.left) - ww) / 2, wy = pr.top + ((pr.bottom - pr.top) - wh) / 2;
    {
        RECT wa;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        wx = std::max<int>(wa.left, std::min<int>(wx, wa.right - ww));
        wy = std::max<int>(wa.top, std::min<int>(wy, wa.bottom - wh));
    }
    g_gfxWnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"TwoTowersGfx", T(L"Graphics settings"), style,
                               wx, wy, ww, wh, g_hwnd, nullptr, inst, nullptr);
    auto child = [&](const wchar_t *cls, const wchar_t *t, DWORD st, int id, int x, int y, int w, int hh) {
        HWND c = CreateWindowExW(g_rtl ? WS_EX_LAYOUTRTL : 0, cls, T(t), WS_CHILD | WS_VISIBLE | st, x, y, w, hh, g_gfxWnd, (HMENU)(INT_PTR)id, inst, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
        return c;
    };
    int y = M, x0 = M;
    for (size_t i = 0; i < g_gfx.size(); i++) {
        auto &o = g_gfx[i];
        int rh = rowH[i];
        if ((int)i == half) { y = M; x0 = M + colW + colGap; }
        if (rh == ch)
            o.lbl = child(L"STATIC", o.label, SS_LEFT | SS_NOPREFIX, -1, x0, y + (ch - fh) / 2, labW, fh + 4);
        else
            o.lbl = child(L"STATIC", o.label, SS_LEFT | SS_NOPREFIX, -1, x0, y, labW, rh);
        o.combo = child(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 400 + (int)i, x0 + labW + gap,
                        y + (rh - ch) / 2, comboW, ch + fh * 14);
        y += rh + gap;
    }
    y = M + std::max(colH[0], colH[1]);
    {   /* (two sentences, each translated: lang) */
        static std::wstring note;
        note = std::wstring(T(L"All but the mods can also be changed while playing: press F10 (or hold Back and press Start). ")) +
               T(L"The mods are set here and take effect when the game starts.");
        child(L"STATIC", note.c_str(), SS_LEFT, -1, M, y, cw - 2 * M, noteH);
    }
    y += noteH + gap;
    int bw = fh * 6;
    child(L"BUTTON", L"Defaults", WS_TABSTOP, ID_GFX_DEFAULTS, M, y, bw, ch);
    child(L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, ID_GFX_OK, cw - M - 2 * bw - gap, y, bw, ch);
    child(L"BUTTON", L"Cancel", WS_TABSTOP, ID_GFX_CANCEL, cw - M - bw, y, bw, ch);
    gfxLoad(false);

    EnableWindow(g_hwnd, FALSE);
    ShowWindow(g_gfxWnd, SW_SHOW);
    g_gfxDone = false;
    MSG msg;
    while (!g_gfxDone && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(g_gfxWnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(g_hwnd, TRUE);
    SetForegroundWindow(g_hwnd);
    g_gfxWnd = nullptr;
}

// ───────────────────────────── Controls ─────────────────────────────
// Same actions, names and rules as the game (xboxrecomp src/input/input_bind.c):
// [Controls] kb_<action>=K, Mouse Left   pad_<action>=A

struct CtlAct { const char *key; const wchar_t *name; bool button; };
static const CtlAct kCtl[] = {
    {"a", L"A button", true}, {"b", L"B button", true}, {"x", L"X button", true}, {"y", L"Y button", true},
    {"black", L"Black button", true}, {"white", L"White button", true},
    {"lt", L"Left trigger", true}, {"rt", L"Right trigger", true},
    {"start", L"Start", true}, {"back", L"Back", true},
    {"dpad_up", L"D-pad up", true}, {"dpad_down", L"D-pad down", true},
    {"dpad_left", L"D-pad left", true}, {"dpad_right", L"D-pad right", true},
    {"ls_click", L"Left stick click", true}, {"rs_click", L"Right stick click", true},
    {"move_up", L"Move forward", false}, {"move_down", L"Move back", false},
    {"move_left", L"Move left", false}, {"move_right", L"Move right", false},
    {"look_up", L"Right stick up", false}, {"look_down", L"Right stick down", false},
    {"look_left", L"Right stick left", false}, {"look_right", L"Right stick right", false},
    {"walk", L"Walk (hold)", false},
};
static const int kCtlN = (int)(sizeof kCtl / sizeof kCtl[0]);
static const wchar_t *kPadSrc[] = {L"None", L"A", L"B", L"X", L"Y", L"LB", L"RB", L"LT", L"RT", L"Start", L"Back",
                                   L"D-pad up", L"D-pad down", L"D-pad left", L"D-pad right", L"LS click", L"RS click"};
static const int kPadSrcN = 17;
static const struct { int vk; const wchar_t *name; } kKeyNames[] = {
    {VK_LBUTTON, L"Mouse Left"}, {VK_RBUTTON, L"Mouse Right"}, {VK_MBUTTON, L"Mouse Middle"},
    {VK_XBUTTON1, L"Mouse 4"}, {VK_XBUTTON2, L"Mouse 5"},
    {VK_SPACE, L"Space"}, {VK_RETURN, L"Enter"}, {VK_ESCAPE, L"Esc"}, {VK_TAB, L"Tab"},
    {VK_BACK, L"Backspace"}, {VK_SHIFT, L"Shift"}, {VK_CONTROL, L"Ctrl"}, {VK_MENU, L"Alt"},
    {VK_CAPITAL, L"Caps Lock"}, {VK_UP, L"Up"}, {VK_DOWN, L"Down"}, {VK_LEFT, L"Left"},
    {VK_RIGHT, L"Right"}, {VK_INSERT, L"Insert"}, {VK_DELETE, L"Delete"}, {VK_HOME, L"Home"},
    {VK_END, L"End"}, {VK_PRIOR, L"Page Up"}, {VK_NEXT, L"Page Down"},
    {VK_NUMPAD0, L"Num 0"}, {VK_NUMPAD1, L"Num 1"}, {VK_NUMPAD2, L"Num 2"}, {VK_NUMPAD3, L"Num 3"},
    {VK_NUMPAD4, L"Num 4"}, {VK_NUMPAD5, L"Num 5"}, {VK_NUMPAD6, L"Num 6"}, {VK_NUMPAD7, L"Num 7"},
    {VK_NUMPAD8, L"Num 8"}, {VK_NUMPAD9, L"Num 9"}, {VK_MULTIPLY, L"Num *"}, {VK_ADD, L"Num +"},
    {VK_SUBTRACT, L"Num -"}, {VK_DECIMAL, L"Num ."}, {VK_DIVIDE, L"Num /"},
    {VK_OEM_1, L"Semicolon"}, {VK_OEM_PLUS, L"Equals"}, {VK_OEM_COMMA, L"Comma"}, {VK_OEM_MINUS, L"Minus"},
    {VK_OEM_PERIOD, L"Period"}, {VK_OEM_2, L"Slash"}, {VK_OEM_3, L"Backquote"}, {VK_OEM_4, L"["},
    {VK_OEM_5, L"Backslash"}, {VK_OEM_6, L"]"}, {VK_OEM_7, L"Quote"},
};

static std::wstring keyName(int vk)
{
    for (auto &k : kKeyNames) if (k.vk == vk) return k.name;
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::wstring(1, (wchar_t)vk);
    if (vk >= VK_F1 && vk <= VK_F24) return L"F" + std::to_wstring(vk - VK_F1 + 1);
    wchar_t b[16]; swprintf(b, 16, L"Key %02X", vk & 0xFF); return b;
}
static int keyFromName(std::wstring s)
{
    while (!s.empty() && s.front() == L' ') s.erase(s.begin());
    while (!s.empty() && s.back() == L' ') s.pop_back();
    if (s.empty() || s == L"-") return 0;
    for (auto &k : kKeyNames) if (!_wcsicmp(k.name, s.c_str())) return k.vk;
    if (s.size() == 1 && iswalnum(s[0])) return towupper(s[0]);
    if ((s[0] == L'F' || s[0] == L'f') && _wtoi(s.c_str() + 1) >= 1 && _wtoi(s.c_str() + 1) <= 24) return VK_F1 + _wtoi(s.c_str() + 1) - 1;
    if (!_wcsnicmp(s.c_str(), L"Key ", 4)) return (int)wcstol(s.c_str() + 4, nullptr, 16);
    return 0;
}

struct CtlState { int kb[25][3]; int pad[25]; };
static CtlState ctlDefaults(bool classic)
{
    CtlState c{};
    auto set = [&](int a, int k0, int k1 = 0, int k2 = 0) { c.kb[a][0] = k0; c.kb[a][1] = k1; c.kb[a][2] = k2; };
    for (int a = 0; a < 16; a++) c.pad[a] = a + 1;      // A B X Y LB(Black) RB(White) LT RT Start Back pad LS RS
    set(10, VK_UP); set(11, VK_DOWN); set(12, VK_LEFT); set(13, VK_RIGHT);
    set(24, 'B');                                       // Walk: B in both layouts
    if (classic) {
        set(0, 'Z'); set(1, 'X'); set(2, 'A'); set(3, 'S'); set(5, 'Q'); set(4, 'E'); set(6, '1'); set(7, '3');
        set(8, VK_RETURN); set(9, VK_BACK); set(14, VK_SHIFT); set(15, VK_CONTROL);
        set(16, VK_NUMPAD8); set(17, VK_NUMPAD2); set(18, VK_NUMPAD4); set(19, VK_NUMPAD6);
        set(20, 'I'); set(21, 'K'); set(22, 'J'); set(23, 'L');
    } else {
        set(0, 'K', VK_LBUTTON, VK_SPACE); set(1, 'L'); set(2, 'J', VK_RBUTTON); set(3, 'I');
        set(5, 'Z'); set(4, 'C'); set(6, 'F'); set(7, 'R');
        set(8, VK_RETURN, VK_ESCAPE); set(9, VK_BACK, VK_TAB); set(14, VK_SHIFT); set(15, VK_MBUTTON);
        set(16, 'W'); set(17, 'S'); set(18, 'A'); set(19, 'D');
        set(20, VK_NUMPAD8); set(21, VK_NUMPAD2); set(22, VK_NUMPAD4); set(23, VK_NUMPAD6);
    }
    return c;
}
static bool layoutClassic() { std::wstring l = iniGet(L"Input", L"layout", L"wasd"); return !l.empty() && (l[0] == L'c' || l[0] == L'C'); }
static CtlState ctlLoad()
{
    CtlState c = ctlDefaults(layoutClassic());
    for (int a = 0; a < kCtlN; a++) {
        std::wstring k = L"kb_" + W(kCtl[a].key), v = iniGet(L"Controls", k.c_str(), L"\x01");
        if (v != L"\x01") {
            int i = 0;
            memset(c.kb[a], 0, sizeof c.kb[a]);
            size_t p = 0;
            while (p <= v.size() && i < 3) {
                size_t q = v.find(L',', p);
                int vk = keyFromName(v.substr(p, q == std::wstring::npos ? std::wstring::npos : q - p));
                if (vk) c.kb[a][i++] = vk;
                if (q == std::wstring::npos) break;
                p = q + 1;
            }
        }
        if (kCtl[a].button) {
            std::wstring pk = L"pad_" + W(kCtl[a].key), pv = iniGet(L"Controls", pk.c_str(), L"");
            for (int s2 = 0; s2 < kPadSrcN && !pv.empty(); s2++) if (!_wcsicmp(kPadSrc[s2], pv.c_str())) c.pad[a] = s2;
        }
    }
    return c;
}
static std::wstring keysText(const CtlState &c, int a)
{
    std::wstring t;
    for (int i = 0; i < 3; i++) if (c.kb[a][i]) { if (!t.empty()) t += L", "; t += keyName(c.kb[a][i]); }
    return t.empty() ? L"-" : t;
}
static void ctlSave(const CtlState &c)
{
    for (int a = 0; a < kCtlN; a++) {
        iniSet(L"Controls", (L"kb_" + W(kCtl[a].key)).c_str(), keysText(c, a));
        if (kCtl[a].button) iniSet(L"Controls", (L"pad_" + W(kCtl[a].key)).c_str(), kPadSrc[c.pad[a]]);
    }
}

enum { ID_CTL_OK = 600, ID_CTL_CANCEL, ID_CTL_DEFAULTS, ID_CTL_KEY = 700, ID_CTL_PAD = 800, ID_CTL_CLR = 900 };
static HWND g_ctlWnd, g_ctlKey[25], g_ctlPad[25], g_ctlHint;
static CtlState g_ctl;
static int g_ctlCapture = -1;
static bool g_ctlDone;

static void ctlRefresh()
{
    for (int a = 0; a < kCtlN; a++) {
        SetWindowTextW(g_ctlKey[a], a == g_ctlCapture ? T(L"press a key or mouse button…") : keysText(g_ctl, a).c_str());
        if (g_ctlPad[a]) SendMessageW(g_ctlPad[a], CB_SETCURSEL, g_ctl.pad[a], 0);
    }
    SetWindowTextW(g_ctlHint, g_ctlCapture >= 0
        ? T(L"Press the key or mouse button for this action (Esc cancels). It replaces the action's first key; other actions lose it.")
        : T(L"Click a key box, then press a key or mouse button. The drop-downs choose which button of your controller does each action. These can also be changed in game: F10, Controls page."));
}

static void ctlAssignKey(int a, int vk)
{
    for (int b = 0; b < kCtlN; b++)
        for (int i = 0; i < 3; i++)
            if (g_ctl.kb[b][i] == vk) { for (int j = i; j < 2; j++) g_ctl.kb[b][j] = g_ctl.kb[b][j + 1]; g_ctl.kb[b][2] = 0; i--; }
    g_ctl.kb[a][2] = g_ctl.kb[a][1]; g_ctl.kb[a][1] = g_ctl.kb[a][0]; g_ctl.kb[a][0] = vk;
}

static LRESULT CALLBACK ctlProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == ID_CTL_OK) { ctlSave(g_ctl); DestroyWindow(h); return 0; }
        if (id == ID_CTL_CANCEL || id == IDCANCEL) { DestroyWindow(h); return 0; }
        if (id == ID_CTL_DEFAULTS) { g_ctl = ctlDefaults(layoutClassic()); g_ctlCapture = -1; ctlRefresh(); return 0; }
        if (id >= ID_CTL_KEY && id < ID_CTL_KEY + kCtlN && HIWORD(wp) == BN_CLICKED) {
            g_ctlCapture = id - ID_CTL_KEY; ctlRefresh(); SetFocus(h); return 0;
        }
        if (id >= ID_CTL_CLR && id < ID_CTL_CLR + kCtlN) {
            memset(g_ctl.kb[id - ID_CTL_CLR], 0, sizeof g_ctl.kb[0]); g_ctlCapture = -1; ctlRefresh(); return 0;
        }
        if (id >= ID_CTL_PAD && id < ID_CTL_PAD + kCtlN && HIWORD(wp) == CBN_SELCHANGE) {
            int a = id - ID_CTL_PAD, src = (int)SendMessageW(g_ctlPad[a], CB_GETCURSEL, 0, 0), old = g_ctl.pad[a];
            if (src > 0) for (int b = 0; b < kCtlN; b++) if (b != a && kCtl[b].button && g_ctl.pad[b] == src) g_ctl.pad[b] = old;
            g_ctl.pad[a] = src; ctlRefresh(); return 0;
        }
        break;
    }
    case WM_CTLCOLORSTATIC: SetBkMode((HDC)wp, TRANSPARENT); return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: g_ctlDone = true; return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static void controlsSettings()
{
    static bool reg = false;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!reg) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = ctlProc; wc.hInstance = inst; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW); wc.lpszClassName = L"TwoTowersCtl";
        wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
        RegisterClassExW(&wc); reg = true;
    }
    g_ctl = ctlLoad(); g_ctlCapture = -1;
    int fh = g_fontH, M = std::max(14, fh), gap = std::max(4, fh / 3), rh = fh + 10;
    int labW = fh * 8, keyW = fh * 11, clrW = fh * 2, padW = fh * 7;
    int colW = labW + gap + keyW + gap / 2 + clrW + gap + padW;
    int half = (kCtlN + 1) / 2;
    int cw = M + colW + M * 2 + colW + M;
    int hintH = fh * 3, ch = fh + fh / 2 + 8;
    int chH = M + rh + half * (rh + gap / 2) + gap + hintH + gap + ch + M;
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    RECT wr{0, 0, cw, chH};
    AdjustWindowRect(&wr, style, FALSE);
    int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
    RECT pr; GetWindowRect(g_hwnd, &pr);
    int wx = pr.left + ((pr.right - pr.left) - ww) / 2, wy = pr.top + ((pr.bottom - pr.top) - wh) / 2;
    { RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
      wx = std::max<int>(wa.left, std::min<int>(wx, wa.right - ww)); wy = std::max<int>(wa.top, std::min<int>(wy, wa.bottom - wh)); }
    g_ctlWnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"TwoTowersCtl", T(L"Controls"), style, wx, wy, ww, wh, g_hwnd, nullptr, inst, nullptr);
    auto child = [&](const wchar_t *cls, const wchar_t *t, DWORD st, int id, int x, int y, int w, int hh) {
        HWND c = CreateWindowExW(g_rtl ? WS_EX_LAYOUTRTL : 0, cls, T(t), WS_CHILD | WS_VISIBLE | st, x, y, w, hh, g_ctlWnd, (HMENU)(INT_PTR)id, inst, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
        return c;
    };
    for (int col = 0; col < 2; col++) {
        int x0 = M + col * (colW + M * 2), y = M;
        child(L"STATIC", L"Action", 0, -1, x0, y + 4, labW, fh + 4);
        child(L"STATIC", L"Keyboard / mouse", 0, -1, x0 + labW + gap, y + 4, keyW, fh + 4);
        child(L"STATIC", L"Controller", 0, -1, x0 + labW + gap + keyW + gap / 2 + clrW + gap, y + 4, padW, fh + 4);
        y += rh;
        for (int r = 0; r < half; r++) {
            int a = col * half + r;
            if (a >= kCtlN) break;
            int x = x0;
            child(L"STATIC", kCtl[a].name, 0, -1, x, y + (rh - fh) / 2, labW, fh + 4); x += labW + gap;
            g_ctlKey[a] = child(L"BUTTON", L"", WS_TABSTOP | BS_LEFT, ID_CTL_KEY + a, x, y, keyW, rh); x += keyW + gap / 2;
            child(L"BUTTON", L"×", WS_TABSTOP, ID_CTL_CLR + a, x, y, clrW, rh); x += clrW + gap;
            if (kCtl[a].button) {
                g_ctlPad[a] = child(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_CTL_PAD + a, x, y, padW, rh + fh * 12);
                for (int s2 = 0; s2 < kPadSrcN; s2++) SendMessageW(g_ctlPad[a], CB_ADDSTRING, 0, (LPARAM)T(kPadSrc[s2]));
            } else {
                g_ctlPad[a] = nullptr;
                child(L"STATIC", a == 24 ? L"stick part way" : a >= 20 ? L"right stick" : L"left stick", 0, -1, x, y + (rh - fh) / 2, padW, fh + 4);
            }
            y += rh + gap / 2;
        }
    }
    int y = M + rh + half * (rh + gap / 2) + gap;
    g_ctlHint = child(L"STATIC", L"", SS_LEFT, -1, M, y, cw - 2 * M, hintH);
    y += hintH + gap;
    int bw = fh * 7;
    child(L"BUTTON", L"Defaults", WS_TABSTOP, ID_CTL_DEFAULTS, M, y, bw, ch);
    child(L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, ID_CTL_OK, cw - M - 2 * bw - gap, y, bw, ch);
    child(L"BUTTON", L"Cancel", WS_TABSTOP, ID_CTL_CANCEL, cw - M - bw, y, bw, ch);
    ctlRefresh();

    EnableWindow(g_hwnd, FALSE);
    ShowWindow(g_ctlWnd, SW_SHOW);
    g_ctlDone = false;
    MSG msg;
    while (!g_ctlDone && GetMessageW(&msg, nullptr, 0, 0)) {
        if (g_ctlCapture >= 0) {
            int vk = 0;
            switch (msg.message) {
            case WM_KEYDOWN: case WM_SYSKEYDOWN: vk = (int)msg.wParam; break;
            case WM_LBUTTONDOWN: vk = VK_LBUTTON; break;
            case WM_RBUTTONDOWN: vk = VK_RBUTTON; break;
            case WM_MBUTTONDOWN: vk = VK_MBUTTON; break;
            case WM_XBUTTONDOWN: vk = HIWORD(msg.wParam) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2; break;
            }
            if (vk) {
                if (vk == VK_LSHIFT || vk == VK_RSHIFT) vk = VK_SHIFT;
                if (vk == VK_LCONTROL || vk == VK_RCONTROL) vk = VK_CONTROL;
                if (vk == VK_LMENU || vk == VK_RMENU) vk = VK_MENU;
                if (vk != VK_ESCAPE) ctlAssignKey(g_ctlCapture, vk);
                g_ctlCapture = -1;
                ctlRefresh();
                continue;   // the key / click is used up
            }
        }
        if (!IsDialogMessageW(g_ctlWnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(g_hwnd, TRUE);
    SetForegroundWindow(g_hwnd);
    g_ctlWnd = nullptr;
}

static void play()
{
    saveSettings();
    static bool built;   /* (built once this run: play, never build again -- 1.9.2) */
    if (!g_busy && !built && iniGet(L"Display", L"ultra_textures", L"0") == L"1" && ultra_build::needed(exeDir())) {
        built = true;
        ultraBuild(true);
        return;
    }
    fs::path exe = exeDir() / L"twotowers_recomp.exe";
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + exe.wstring() + L"\"";
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, exeDir().c_str(), &si, &pi)) {
        MessageBoxW(g_hwnd, T(L"Could not start twotowers_recomp.exe."), kTitle, MB_ICONERROR);
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    DestroyWindow(g_hwnd);
}

static HWND mk(const wchar_t *cls, const wchar_t *t, DWORD style, int id, DWORD ex = 0)
{
    HWND c = CreateWindowExW(ex | (g_rtl ? WS_EX_LAYOUTRTL : 0), cls, T(t), WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, g_hwnd, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

static int textW(HWND c)
{
    int n = GetWindowTextLengthW(c);
    std::wstring t(n, 0);
    GetWindowTextW(c, &t[0], n + 1);
    HDC dc = GetDC(c);
    HGDIOBJ o = SelectObject(dc, g_font);
    SIZE sz{};
    GetTextExtentPoint32W(dc, t.c_str(), n, &sz);
    SelectObject(dc, o);
    ReleaseDC(c, dc);
    return sz.cx;
}

static HWND g_cover;
static HWND g_btnCredits;
static HWND g_uiLabel, g_uiCombo;
static constexpr int ID_UI_LANG = ID_CREDITS + 1;
static bool g_hasCover;   // box art: built in (resources/local/cover.bmp) or cover.bmp beside the exe
static HWND g_lblTitle, g_lblRes, g_lblMode, g_lblInt, g_btnIso, g_btnDir, g_btnOpen, g_btnGfx, g_btnCtl, g_btnDlss5, g_lblLang, g_lang, g_lblFmv, g_fmv, g_lblVoice, g_voice, g_lblSubs, g_subs, g_lblCine, g_cine;
static std::vector<LangItem> g_langs, g_fmvs, g_voices, g_subtitles, g_cines;

static int layout(bool apply)
{
    RECT r;
    GetClientRect(g_hwnd, &r);
    int W_ = r.right, fh = g_fontH, M = std::max(14, fh), gap = std::max(6, fh / 2);
    int lh = fh + 6, ch = fh + fh / 2 + 8, y = M;
    // Cover art column on the left, settings column on the right.
    int picW = g_hasCover ? fh * 15 : 0, picH = picW * 385 / 369;
    int L = g_hasCover ? M + picW + M : M;   // left edge of the settings column
    auto mv = [&](HWND h, int x, int yy, int w, int hh) { if (apply) MoveWindow(h, x - M + L, yy, w, hh, TRUE); };
    if (apply) MoveWindow(g_cover, M, M, picW, picH, TRUE);
    W_ -= L - M;                        // settings column width budget
    mv(g_lblTitle, M, y, W_ - 2 * M, lh); y += lh + gap;
    mv(g_status, M, y, W_ - 2 * M, lh * 2); y += lh * 2 + gap;
    mv(g_progress, M, y, W_ - 2 * M, fh); y += fh + gap * 2;
    int bw1 = textW(g_btnIso) + 2 * fh, bw2 = textW(g_btnDir) + 2 * fh;
    mv(g_btnIso, M, y, bw1, ch); mv(g_btnDir, M + bw1 + gap, y, bw2, ch); y += ch + gap * 2;
    for (HWND c : {g_wide, g_log}) { mv(c, M, y, textW(c) + 3 * fh, lh); y += lh + gap / 2; }
    y += gap;
    { int lw = textW(g_lblLang) + fh; mv(g_lblLang, M, y + (ch - fh) / 2, lw, fh + 4); mv(g_lang, M + lw + gap, y, W_ - 2*M - lw - gap, ch + fh * 16); }
    y += ch + gap;
    {   // Un eje por fila; etiquetas medidas en el idioma del lanzador.
        HWND labels[] = {g_lblSubs, g_lblVoice, g_lblCine, g_lblFmv};
        HWND combos[] = {g_subs, g_voice, g_cine, g_fmv};
        int lw = 0;
        for (auto label : labels) lw = std::max(lw, textW(label) + fh);
        for (int k=0; k<4; k++) {
            mv(labels[k], M, y+(ch-fh)/2, lw, fh+4);
            mv(combos[k], M+lw+gap, y, std::max(fh*9, W_-2*M-lw-gap), ch+fh*16);
            y += ch+gap;
        }
    }
    { int w1 = textW(g_btnGfx) + 2 * fh; mv(g_btnGfx, M, y, w1, ch); mv(g_btnCtl, M + w1 + gap, y, textW(g_btnCtl) + 2 * fh, ch); }
    y += ch + gap * 2;
    mv(g_btnDlss5, M, y, textW(g_btnDlss5) + 2 * fh, ch);
    y += ch + gap;
    mv(g_btnCredits, M, y, textW(g_btnCredits) + 2 * fh, ch);
    y += ch + gap;
    mv(g_uiLabel, M, y, textW(g_uiLabel) + fh, ch);
    mv(g_uiCombo, M + textW(g_uiLabel) + 2 * fh, y, fh * 10, ch + fh * 8);
    y += ch + gap;
    int pw = textW(g_play) + 4 * fh, ow = textW(g_btnOpen) + 2 * fh;
    mv(g_play, M, y, pw, ch + 4); mv(g_btnOpen, M + pw + gap, y, ow, ch + 4);
    y += ch + 4 + M;
    return std::max(y, M + picH + M);
}

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_ISO: pickIso(); return 0;
        case ID_DIR: pickFolder(); return 0;
        case ID_PLAY: play(); return 0;
        case ID_GFX: graphicsSettings(); return 0;
        case ID_CTL: controlsSettings(); return 0;
        case ID_UI_LANG:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                const wchar_t *codes[] = {L"en",L"es",L"fr",L"de",L"it"};
                int i = (int)SendMessageW(g_uiCombo, CB_GETCURSEL, 0, 0);
                if (i >= 0 && i < 5) {
                    iniSet(L"Setup", L"language", codes[i]);
                    MessageBoxW(h, T(L"Reopen the configurator to apply its interface language. Game language settings are preserved."), T(L"Interface language"), MB_ICONINFORMATION);
                }
            }
            return 0;
        case ID_CREDITS: {
            auto credits = exeDir() / (L"creditos." + iniGet(L"Setup",L"language",L"en") + L".html");
            if (!fs::exists(credits)) credits = exeDir() / L"creditos.html";
            if (fs::exists(credits)) ShellExecuteW(h, L"open", credits.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            else MessageBoxW(h, L"The128ReBits: Definitive Mod. Gumpolas and contributors: original port (MIT).\nCredits file missing: creditos.html\nhttps://github.com/Gumpolas/TooTawersRecomp", L"Credits / Creditos", MB_ICONINFORMATION);
            return 0;
        }
        case ID_DLSS5: {
            auto panel = exeDir() / L"dlss5" / L"Configurar_DLSS5.ps1";
            if (!fs::exists(panel)) {
                MessageBoxW(h, L"Falta el panel local dlss5\\Configurar_DLSS5.ps1.", L"DLSS 5", MB_ICONINFORMATION);
                return 0;
            }
            wchar_t system[MAX_PATH];
            GetSystemDirectoryW(system, MAX_PATH);
            auto powershell = fs::path(system) / L"WindowsPowerShell" / L"v1.0" / L"powershell.exe";
            auto args = L"-NoProfile -STA -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + panel.wstring() + L"\"";
            ShellExecuteW(h, L"open", powershell.c_str(), args.c_str(), exeDir().c_str(), SW_HIDE);
            return 0;
        }
        case ID_LANG:
        case ID_SUBS:
        case ID_VOICE:
        case ID_CINE:
        case ID_FMV:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                int id = LOWORD(wp);
                auto &v = id == ID_LANG ? g_langs : id == ID_SUBS ? g_subtitles : id == ID_VOICE ? g_voices : id == ID_CINE ? g_cines : g_fmvs;
                HWND cb = id == ID_LANG ? g_lang : id == ID_SUBS ? g_subs : id == ID_VOICE ? g_voice : id == ID_CINE ? g_cine : g_fmv;
                const wchar_t *key = id == ID_LANG ? L"language" : id == ID_SUBS ? L"subtitles" : id == ID_VOICE ? L"voice" : id == ID_CINE ? L"voice_cine" : L"fmv";
                int i = (int)SendMessageW(cb, CB_GETCURSEL, 0, 0);
                if (i >= 0 && i < (int)v.size()) iniSet(L"Language", key, v[i].code);
            }
            return 0;
        case ID_OPEN: ShellExecuteW(nullptr, L"open", exeDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL); return 0;
        }
        break;
    case WM_STATUS: { auto s = (std::wstring *)lp; setStatus(*s); delete s; return 0; }
    case WM_PROGRESS: SendMessageW(g_progress, PBM_SETPOS, wp, 0); return 0;
    case WM_DONE:
        g_busy = false;
        if (lp) {   // the Ultra textures built (1), then the game started (2)
            EnableWindow(g_play, TRUE);
            if (wp) SendMessageW(g_progress, PBM_SETPOS, 1000, 0);
            if (wp && lp == 2) play();
            else if (wp && g_ultraOnly) DestroyWindow(h);
            return 0;
        }
        if (wp) { SendMessageW(g_progress, PBM_SETPOS, 1000, 0); refreshStatus(); }
        else EnableWindow(g_play, FALSE);
        return 0;
    case WM_SIZE: layout(true); return 0;
    case WM_CTLCOLORSTATIC: SetBkMode((HDC)wp, TRANSPARENT); return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_CLOSE:
        if (g_busy && MessageBoxW(h, T(L"Still working (installing, or building the Ultra textures). Quit anyway?"), kTitle, MB_YESNO) != IDYES) return 0;
        saveSettings();
        DestroyWindow(h);
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int show)
{
    SetProcessDPIAware();
    langLoad();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&icc);
    NONCLIENTMETRICSW ncm{sizeof(ncm)};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    {   /* a face with the chosen language's letters (as the game's F10 panel: lang.c) */
        static const struct { const wchar_t *code, *face; } F[] = {
            {L"ja", L"Meiryo UI"}, {L"ko", L"Malgun Gothic"}, {L"zh-Hans", L"Microsoft YaHei UI"},
            {L"zh-Hant", L"Microsoft JhengHei UI"}, {L"th", L"Leelawadee UI"}, {L"hi", L"Nirmala UI"}};
        std::wstring code = langCode();
        for (auto &f : F)
            if (code == f.code) { wcsncpy(ncm.lfMessageFont.lfFaceName, f.face, LF_FACESIZE - 1); ncm.lfMessageFont.lfFaceName[LF_FACESIZE - 1] = 0; }
    }
    g_font = CreateFontIndirectW(&ncm.lfMessageFont);
    {
        HDC dc = GetDC(nullptr);
        HGDIOBJ o = SelectObject(dc, g_font);
        TEXTMETRICW tm{};
        GetTextMetricsW(dc, &tm);
        SelectObject(dc, o);
        ReleaseDC(nullptr, dc);
        g_fontH = std::max<int>(14, tm.tmHeight + tm.tmExternalLeading);
    }
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.lpszClassName = L"TwoTowersLauncher";
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, kTitle, style, CW_USEDEFAULT, CW_USEDEFAULT, g_fontH * 40, g_fontH * 40, nullptr, nullptr, inst, nullptr);

    g_lblTitle = mk(L"STATIC", L"The Lord of the Rings: The Two Towers  -  native PC recompilation", 0, -1);
    g_status = mk(L"STATIC", L"", 0, ID_STATUS);
    g_progress = mk(PROGRESS_CLASSW, L"", 0, ID_PROGRESS);
    SendMessageW(g_progress, PBM_SETRANGE32, 0, 1000);
    g_btnIso = mk(L"BUTTON", L"Install from ISO…", WS_TABSTOP, ID_ISO);
    g_btnDir = mk(L"BUTTON", L"Use extracted folder…", WS_TABSTOP, ID_DIR);
    g_wide = mk(L"BUTTON", L"Show the game window", BS_AUTOCHECKBOX | WS_TABSTOP, ID_WIDE);
    g_log = mk(L"BUTTON", L"Write a log file (for bug reports)", BS_AUTOCHECKBOX | WS_TABSTOP, ID_LOG);
    g_btnGfx = mk(L"BUTTON", L"Graphics settings…", WS_TABSTOP, ID_GFX);
    g_btnCtl = mk(L"BUTTON", L"Controls…", WS_TABSTOP, ID_CTL);
    g_btnDlss5 = mk(L"BUTTON", L"DLSS 5 - Personalizar...", WS_TABSTOP, ID_DLSS5);
    g_btnCredits = mk(L"BUTTON", L"Credits / Creditos", WS_TABSTOP, ID_CREDITS);
    g_uiLabel = mk(L"STATIC", L"Interface language", 0, -1);
    g_uiCombo = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_UI_LANG);
    {
        const wchar_t *codes[] = {L"en",L"es",L"fr",L"de",L"it"};
        const wchar_t *labels[] = {L"English",L"Español",L"Français",L"Deutsch",L"Italiano"};
        std::wstring current = iniGet(L"Setup",L"language",L"en");
        int selected = 0;
        for (int i=0;i<5;i++) { SendMessageW(g_uiCombo, CB_ADDSTRING, 0, (LPARAM)labels[i]); if(current==codes[i]) selected=i; }
        SendMessageW(g_uiCombo, CB_SETCURSEL, selected, 0);
    }
    g_lblLang = mk(L"STATIC", L"Text:", 0, -1);
    g_lang = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_LANG);
    g_langs = langList();
    {
        std::wstring cur = langCode();
        int sel = 0;
        for (size_t i = 0; i < g_langs.size(); i++) {
            SendMessageW(g_lang, CB_ADDSTRING, 0, (LPARAM)g_langs[i].name.c_str());
            if (!_wcsicmp(g_langs[i].code.c_str(), cur.c_str())) sel = (int)i;
        }
        SendMessageW(g_lang, CB_SETCURSEL, sel, 0);
    }
    g_lblFmv = mk(L"STATIC", L"Film audio:", 0, -1);
    g_fmv = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_FMV);
    g_lblVoice = mk(L"STATIC", L"Gameplay voices:", 0, -1);
    g_voice = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_VOICE);
    g_lblSubs = mk(L"STATIC", L"Subtitles:", 0, -1);
    g_subs = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_SUBS);
    g_lblCine = mk(L"STATIC", L"Cinematic voices:", 0, -1);
    g_cine = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_CINE);
    g_fmvs = mediaList(true); g_voices = mediaList(false); g_cines = mediaList(false, true); g_subtitles = langList();
    for (int k = 0; k < 4; k++) {
        HWND cb = k == 0 ? g_subs : k == 1 ? g_voice : k == 2 ? g_cine : g_fmv;
        auto &v = k == 0 ? g_subtitles : k == 1 ? g_voices : k == 2 ? g_cines : g_fmvs;
        const wchar_t *key = k == 0 ? L"subtitles" : k == 1 ? L"voice" : k == 2 ? L"voice_cine" : L"fmv";
        std::wstring cur = iniGet(L"Language", key, L""); int sel = 0;
        for (size_t i = 0; i < v.size(); i++) {
            SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)v[i].name.c_str());
            if (!_wcsicmp(v[i].code.c_str(), cur.c_str())) sel = (int)i;
        }
        SendMessageW(cb, CB_SETCURSEL, sel, 0);
    }
    g_play = mk(L"BUTTON", L"Play", BS_DEFPUSHBUTTON | WS_TABSTOP, ID_PLAY);
    g_btnOpen = mk(L"BUTTON", L"Open folder", WS_TABSTOP, ID_OPEN);
    // Box art (your own disc's cover), scaled into the picture control.
    g_cover = mk(L"STATIC", L"", SS_BITMAP | SS_REALSIZECONTROL, -1);
    {
        HBITMAP cover = (HBITMAP)LoadImageW(inst, MAKEINTRESOURCEW(2), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
        if (!cover)
            cover = (HBITMAP)LoadImageW(nullptr, (exeDir() / L"cover.bmp").c_str(), IMAGE_BITMAP, 0, 0,
                                        LR_LOADFROMFILE | LR_CREATEDIBSECTION);
        if (cover) { SendMessageW(g_cover, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)cover); g_hasCover = true; }
        else ShowWindow(g_cover, SW_HIDE);
    }

    loadSettings();

    // Size the window to fit the measured layout.
    RECT wr{0, 0, g_fontH * 34 + (g_hasCover ? g_fontH * 15 + std::max(14, g_fontH) : 0), 0};
    int contentH = layout(false);
    wr.bottom = contentH;
    AdjustWindowRect(&wr, style, FALSE);
    SetWindowPos(g_hwnd, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top, SWP_NOMOVE | SWP_NOZORDER);
    layout(true);
    refreshStatus();
    ShowWindow(g_hwnd, show);
    /* "The Two Towers Recompiled.exe" --graphics opens the settings directly */
    if (cmdLine && wcsstr(cmdLine, L"--graphics")) { UpdateWindow(g_hwnd); graphicsSettings(); }
    if (cmdLine && wcsstr(cmdLine, L"--controls")) { UpdateWindow(g_hwnd); controlsSettings(); }
    /* --build-ultra: the game asks for the Ultra textures to be built (they
     * were turned on in its menus); the window closes when they are */
    if (cmdLine && wcsstr(cmdLine, L"--build-ultra")) {
        UpdateWindow(g_hwnd);
        g_ultraOnly = true;
        if (ultra_build::needed(exeDir())) ultraBuild(false);
        else DestroyWindow(g_hwnd);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(g_hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    return 0;
}
