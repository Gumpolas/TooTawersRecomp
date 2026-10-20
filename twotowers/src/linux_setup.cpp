// The Two Towers Recompiled - Linux setup tool (twotowers-setup).
//
// The Linux counterpart of the Windows launcher's setup half: it checks and
// extracts the user's retail disc and edits twotowers_recomp.ini. The
// TwoTowers.sh script beside it runs it (with zenity dialogs when zenity is
// installed) and then starts the game. Settings that the game's own F10 menu
// covers (resolution, renderer, full screen, ...) are not repeated here.
//
//   twotowers-setup status              is the game installed? (exit 0 = yes)
//   twotowers-setup install <file.iso>  extract the disc into game_files/
//   twotowers-setup folder <dir>        use an extracted disc folder instead
//   twotowers-setup set <section> <key> <value>
//   twotowers-setup get <section> <key>
//   twotowers-setup desktop             add the game to the applications menu
//   twotowers-setup                     a small menu in the terminal
//
// "install" prints "PROGRESS <0-100>" lines while it extracts (for zenity).

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <string>
#include <vector>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>   /* _NSGetExecutablePath */
#endif

namespace fs = std::filesystem;
#include "../tools/ultra_textures/ultra_build.h"

static const char *kXbeSha256 = "ae2d1b5304b3e9572339b693a6a45ad0869bdaeec89aae374b5c14feebbb8d30";

// ───────────────────────────── SHA-256 ─────────────────────────────

struct Sha256 {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t buf[64];
    uint64_t len = 0;
    size_t n = 0;
    static uint32_t ror(uint32_t x, int r) { return (x >> r) | (x << (32 - r)); }
    void block(const uint8_t *p)
    {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64], a[8];
        for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        memcpy(a, h, sizeof a);
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = ror(a[4], 6) ^ ror(a[4], 11) ^ ror(a[4], 25);
            uint32_t ch = (a[4] & a[5]) ^ (~a[4] & a[6]);
            uint32_t t1 = a[7] + S1 + ch + k[i] + w[i];
            uint32_t S0 = ror(a[0], 2) ^ ror(a[0], 13) ^ ror(a[0], 22);
            uint32_t mj = (a[0] & a[1]) ^ (a[0] & a[2]) ^ (a[1] & a[2]);
            uint32_t t2 = S0 + mj;
            a[7] = a[6]; a[6] = a[5]; a[5] = a[4]; a[4] = a[3] + t1;
            a[3] = a[2]; a[2] = a[1]; a[1] = a[0]; a[0] = t1 + t2;
        }
        for (int i = 0; i < 8; i++) h[i] += a[i];
    }
    void add(const uint8_t *p, size_t len_)
    {
        len += len_;
        while (len_) {
            size_t c = std::min(len_, 64 - n);
            memcpy(buf + n, p, c);
            n += c; p += c; len_ -= c;
            if (n == 64) { block(buf); n = 0; }
        }
    }
    std::string hex()
    {
        uint64_t bits = len * 8;
        uint8_t pad = 0x80, z = 0;
        add(&pad, 1);
        while (n != 56) add(&z, 1);
        uint8_t L[8];
        for (int i = 0; i < 8; i++) L[i] = (uint8_t)(bits >> (56 - 8 * i));
        add(L, 8);
        std::string s;
        char b[9];
        for (uint32_t v : h) { snprintf(b, sizeof b, "%08x", v); s += b; }
        return s;
    }
};

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

// ───────────────────────────── ini ─────────────────────────────

static fs::path exeDir()
{
    std::error_code ec;
#if defined(__APPLE__)
    char buf[4096];
    uint32_t n = sizeof buf;
    if (_NSGetExecutablePath(buf, &n) == 0) return fs::weakly_canonical(fs::path(buf), ec).parent_path();
    return fs::current_path();
#else
    fs::path p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::current_path() : p.parent_path();
#endif
}
static fs::path iniPath() { return exeDir() / "twotowers_recomp.ini"; }

static std::vector<std::string> iniLines()
{
    std::vector<std::string> v;
    std::ifstream in(iniPath());
    std::string l;
    while (std::getline(in, l)) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        v.push_back(l);
    }
    return v;
}

static std::string trim(std::string s)
{
    while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
    size_t i = 0;
    while (i < s.size() && isspace((unsigned char)s[i])) i++;
    return s.substr(i);
}

static std::string iniGet(const std::string &sec, const std::string &key, const std::string &def)
{
    std::string cur;
    for (auto &l : iniLines()) {
        std::string t = trim(l);
        if (t.empty() || t[0] == ';' || t[0] == '#') continue;
        if (t[0] == '[') { cur = lower(t.substr(1, t.find(']') - 1)); continue; }
        size_t eq = t.find('=');
        if (eq != std::string::npos && cur == lower(sec) && lower(trim(t.substr(0, eq))) == lower(key))
            return trim(t.substr(eq + 1));
    }
    return def;
}

// Keeps comments and order; changes the key in place, or adds it (and the
// section) if it is not there.
static bool iniSet(const std::string &sec, const std::string &key, const std::string &val)
{
    auto v = iniLines();
    std::string cur;
    int secEnd = -1, secAt = -1;
    for (int i = 0; i < (int)v.size(); i++) {
        std::string t = trim(v[i]);
        if (!t.empty() && t[0] == '[') {
            cur = lower(t.substr(1, t.find(']') - 1));
            if (cur == lower(sec)) secAt = i, secEnd = i + 1;
            continue;
        }
        if (cur != lower(sec)) continue;
        if (!t.empty()) secEnd = i + 1;
        if (t.empty() || t[0] == ';' || t[0] == '#') continue;
        size_t eq = t.find('=');
        if (eq != std::string::npos && lower(trim(t.substr(0, eq))) == lower(key)) {
            v[i] = key + "=" + val;
            goto write;
        }
    }
    if (secAt < 0) {
        if (!v.empty() && !trim(v.back()).empty()) v.push_back("");
        v.push_back("[" + sec + "]");
        v.push_back(key + "=" + val);
    } else {
        v.insert(v.begin() + secEnd, key + "=" + val);
    }
write:
    std::ofstream o(iniPath(), std::ios::trunc);
    for (auto &l : v) o << l << "\n";
    return (bool)o;
}

// ───────────────────────────── disc ─────────────────────────────

static fs::path findXbe(const fs::path &dir)
{
    std::error_code ec;
    for (auto &e : fs::directory_iterator(dir, ec))
        if (lower(e.path().filename().string()) == "default.xbe") return e.path();
    return {};
}

static bool xbeOk(const fs::path &dir, std::string *why)
{
    fs::path x = findXbe(dir);
    if (x.empty()) { if (why) *why = "default.xbe was not found in that folder."; return false; }
    std::ifstream in(x, std::ios::binary);
    std::vector<char> b(1 << 20);
    Sha256 s;
    while (in) {
        in.read(b.data(), (std::streamsize)b.size());
        s.add((const uint8_t *)b.data(), (size_t)in.gcount());
    }
    if (s.hex() != kXbeSha256) {
        if (why) *why = "That default.xbe is not the USA retail release this port was built from.";
        return false;
    }
    return true;
}

static fs::path currentGameDir()
{
    std::string g = iniGet("Paths", "game_dir", "");
    return g.empty() ? exeDir() / "game_files" : fs::path(g);
}

static int cmdStatus(bool quiet)
{
    std::string why;
    fs::path g = currentGameDir();
    if (fs::exists(g) && xbeOk(g, &why)) {
        if (!quiet) std::cout << "Game files ready: " << g.string() << "\n";
        return 0;
    }
    if (!quiet) std::cout << "Game files not installed yet (" << g.string() << ").\n";
    return 1;
}

static int cmdInstall(const fs::path &iso)
{
    std::vector<IsoFile> files;
    std::string err;
    fs::path out = exeDir() / "game_files";
    if (!scanIso(iso, files, err)) { std::cerr << "Error: " << err << "\n"; return 2; }
    auto xbe = std::find_if(files.begin(), files.end(), [](auto &f) { return lower(f.path) == "default.xbe"; });
    if (xbe == files.end()) { std::cerr << "Error: the disc has no default.xbe.\n"; return 2; }
    std::ifstream in(iso, std::ios::binary);
    {
        std::vector<uint8_t> xd(xbe->size);
        readAt(in, xbe->offset, xd.data(), xd.size());
        Sha256 s;
        s.add(xd.data(), xd.size());
        if (s.hex() != kXbeSha256) {
            std::cerr << "Error: this ISO is not the USA retail The Two Towers disc this port supports.\n";
            return 2;
        }
    }
    uint64_t total = 0, done = 0;
    for (auto &f : files) total += f.size;
    std::vector<char> buf(4 << 20);
    int lastPct = -1;
    try {
        fs::create_directories(out);
        for (auto &f : files) {
            fs::path dst = out / fs::u8path(f.path);
            if (f.dir) { fs::create_directories(dst); continue; }
            fs::create_directories(dst.parent_path());
            std::ofstream o(dst, std::ios::binary | std::ios::trunc);
            if (!o) throw std::runtime_error("cannot write " + dst.string());
            uint64_t left = f.size, pos = f.offset;
            while (left) {
                size_t n = (size_t)std::min<uint64_t>(left, buf.size());
                if (!readAt(in, pos, buf.data(), n)) throw std::runtime_error("read failed: " + f.path);
                o.write(buf.data(), (std::streamsize)n);
                if (!o) throw std::runtime_error("write failed (disk full?): " + dst.string());
                left -= n; pos += n; done += n;
                int pct = total ? (int)(done * 100 / total) : 100;
                if (pct != lastPct) {
                    lastPct = pct;
                    std::cout << "PROGRESS " << pct << "\n# Extracting " << f.path << "\n" << std::flush;
                }
            }
        }
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 3;
    }
    iniSet("Paths", "game_dir", "");
    std::cout << "PROGRESS 100\nInstalled into " << out.string() << "\n";
    return 0;
}

static int cmdFolder(const fs::path &dir)
{
    std::string why;
    if (!xbeOk(dir, &why)) { std::cerr << "Error: " << why << "\n"; return 2; }
    iniSet("Paths", "game_dir", fs::absolute(dir).string());
    std::cout << "Using " << fs::absolute(dir).string() << "\n";
    return 0;
}

// A menu entry (and its icon) for this folder, in the user's applications.
static int cmdDesktop()
{
    const char *home = getenv("HOME");
    const char *xdg = getenv("XDG_DATA_HOME");
    if (!home && !xdg) { std::cerr << "Error: no home folder.\n"; return 2; }
    fs::path apps = (xdg && *xdg ? fs::path(xdg) : fs::path(home) / ".local" / "share") / "applications";
    std::error_code ec;
    fs::create_directories(apps, ec);
    fs::path dir = exeDir();
    std::ofstream o(apps / "twotowers-recompiled.desktop", std::ios::trunc);
    o << "[Desktop Entry]\nType=Application\nName=The Two Towers Recompiled\n"
         "Comment=The Lord of the Rings: The Two Towers (Xbox), recompiled for PC\n"
         "Exec=\"" << (dir / "TwoTowers.sh").string() << "\"\n"
         "Path=" << dir.string() << "\n"
         "Icon=" << (dir / "twotowers.png").string() << "\n"
         "Terminal=false\nCategories=Game;ActionGame;\n";
    if (!o) { std::cerr << "Error: cannot write " << (apps / "twotowers-recompiled.desktop").string() << "\n"; return 2; }
    std::cout << "Added to your applications menu: " << (apps / "twotowers-recompiled.desktop").string() << "\n";
    return 0;
}

static std::string ask(const std::string &q)
{
    std::cout << q << std::flush;
    std::string a;
    std::getline(std::cin, a);
    return trim(a);
}

static int cmdUltra();
static int menu()
{
    for (;;) {
        std::cout << "\nThe Two Towers Recompiled -- setup\n";
        cmdStatus(false);
        std::cout << "  1) Install from an ISO (The Two Towers, USA)\n"
                     "  2) Use an extracted disc folder\n"
                     "  3) Full screen at start: " << iniGet("Display", "fullscreen", "0") << "\n"
                     "  4) Frame rate: " << iniGet("Display", "fps", "30") << "  (30 / 60 / 75 / 90 / 100 / 120 / 144 / 165 / 200 / 240 / 300 / unlocked; above 30 is experimental)\n"
                     "  5) ECTS 2002 HUD mod: " << iniGet("Mods", "e3_hud", "0") << "\n"
                     "  6) Button prompts: " << iniGet("Input", "prompts", "auto") << "  (auto / xbox / xboxone / playstation / keyboard)\n"
                     "  7) Extra heroes mod: Boromir, Gandalf, Frodo, Lurtz, Elrond, Uruk-hai, Sauron\n     (0 off / 1 on, unlock with Isildur / 2 on, unlocked for testing): " << iniGet("Mods", "extra_heroes", "0") << "\n"
                     "  l) Language: " << iniGet("Language", "language", "en") << "  (en, or a folder in lang/)\n"
                     "  8) Add to the applications menu\n"
                     "  9) Play\n"
                     "  q) Quit\n";
        std::string c = ask("> ");
        if (c == "1") { std::string p = ask("Path to the .iso: "); if (!p.empty()) cmdInstall(p); }
        else if (c == "2") { std::string p = ask("Folder with default.xbe: "); if (!p.empty()) cmdFolder(p); }
        else if (c == "3") iniSet("Display", "fullscreen", iniGet("Display", "fullscreen", "0") == "1" ? "0" : "1");
        else if (c == "4") {
            std::string f = iniGet("Display", "fps", "30");
            {
                static const char *seq[] = { "30", "60", "75", "90", "100", "120", "144", "165", "200", "240", "300", "unlocked" };
                size_t k = 0;
                while (k < 12 && f != seq[k]) k++;
                iniSet("Display", "fps", seq[k >= 11 ? 0 : k + 1]);
            }
        }
        else if (c == "5") iniSet("Mods", "e3_hud", iniGet("Mods", "e3_hud", "0") == "1" ? "0" : "1");
        else if (c == "6") {
            std::string p = lower(iniGet("Input", "prompts", "auto"));
            iniSet("Input", "prompts", p == "auto" ? "xbox" : p == "xbox" ? "xboxone" : p == "xboxone" ? "playstation" : p == "playstation" ? "keyboard" : "auto");
        }
        else if (c == "7") {
            std::string v = iniGet("Mods", "extra_heroes", "0");
            iniSet("Mods", "extra_heroes", v == "0" ? "1" : v == "1" ? "2" : "0");   // 2: unlocked for testing
        }
        else if (c == "l" || c == "L") {
            std::vector<std::string> codes{"en"};
            std::error_code ec;
            for (auto &e : fs::directory_iterator(exeDir() / "lang", ec))
                if (fs::exists(e.path() / "info.txt")) codes.push_back(e.path().filename().string());
            std::sort(codes.begin() + 1, codes.end());
            for (size_t i = 0; i < codes.size(); i++) {
                std::string name = codes[i] == "en" ? "English" : "";
                std::ifstream in(exeDir() / "lang" / codes[i] / "info.txt");
                std::string ln;
                while (std::getline(in, ln)) if (ln.compare(0, 5, "name=") == 0) name = ln.substr(5);
                std::cout << "   " << codes[i] << "  " << name << "\n";
            }
            std::string v = ask("Language code: ");
            if (std::find(codes.begin(), codes.end(), v) != codes.end()) iniSet("Language", "language", v);
            else if (!v.empty()) std::cout << "No such language.\n";
        }
        else if (c == "8") cmdDesktop();
        else if (c == "9") {
            if (cmdStatus(true) != 0) { std::cout << "Install the game files first.\n"; continue; }
            if (iniGet("Display", "ultra_textures", "0") == "1" && ultra_build::needed(exeDir())) cmdUltra();
            fs::path exe = exeDir() / "twotowers_recomp";
            if (chdir(exeDir().c_str()) == 0) execl(exe.c_str(), exe.c_str(), (char *)nullptr);
            std::cerr << "Could not start " << exe.string() << "\n";
            return 1;
        }
        else if (c == "q" || c == "Q" || std::cin.eof()) return 0;
    }
}

// Ultra textures: the GLOS69 pack (downloaded separately) made into the
// files the game reads, once (tools/ultra_textures/ultra_build.h).
// "PROGRESS n" and "# text" lines, as install's, for a progress window.
static int cmdUltra()
{
    std::string err;
    int last = -1;
    int r = ultra_build::build(exeDir(), [&](double f, const std::string &what) {
        int p = (int)(f * 100);
        if (p != last) { last = p; std::cout << "PROGRESS " << p << "\n# Ultra textures (once): " << what << std::endl; }
    }, err);
    if (r) std::cerr << "Ultra textures: " << err << "\n";
    return r;
}

int main(int argc, char **argv)
{
    std::string c = argc > 1 ? argv[1] : "";
    if (c == "status") return cmdStatus(argc > 2 && std::string(argv[2]) == "-q");
    if (c == "install" && argc > 2) return cmdInstall(argv[2]);
    if (c == "folder" && argc > 2) return cmdFolder(argv[2]);
    if (c == "desktop") return cmdDesktop();
    if (c == "ultra") return cmdUltra();
    if (c == "ultra-needed") return ultra_build::needed(exeDir()) ? 0 : 1;   // 0: the pack is there and not built yet
    if (c == "set" && argc > 4) return iniSet(argv[2], argv[3], argv[4]) ? 0 : 1;
    if (c == "get" && argc > 3) { std::cout << iniGet(argv[2], argv[3], "") << "\n"; return 0; }
    if (c.empty()) return menu();
    std::cerr << "usage: twotowers-setup [status | install <iso> | folder <dir> | desktop | ultra | ultra-needed | set <sec> <key> <val> | get <sec> <key>]\n";
    return 2;
}
