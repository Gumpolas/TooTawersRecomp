// ultra_build.h -- builds the "Ultra textures" files from the GLOS69 pack.
//
// The GameCube high-resolution texture pack (a Dolphin pack, folder GLOS69)
// is its author's, and is downloaded and installed separately: put the
// GLOS69 folder in the game's folder. What this port adds to it ships with
// the game, in textures_ultra:
//
//   ui/<hash>.dds      the game's own menu, level-select and HUD textures,
//                      upscaled (no pack needed for these)
//   masks/<hash>.png   the game textures' transparency, for pack pictures
//                      that lost it (alphafix)
//   recipe.txt         which pack pictures are remade, and how
//   pairs.txt          pairings checked by eye
//
// From those and the pack, this makes (once; the launcher runs it):
//
//   png/<name>.dds       the pack's PNG pictures as BC7 DDS files
//   bleed/<name>.dds     cut-out pictures (grass, leaves, fences) with their
//                        colour carried under the transparent texels
//   alphafix/<hash>.dds  pack pictures given the game texture's transparency
//   index.bin            every picture's 16 x 16 signature (nv2a_ultra.inc)
//   built.txt            what it was built from (a different pack: rebuilt)
//
// Header-only; the program that includes it also compiles bc7enc.cpp,
// bc7decomp.cpp and lodepng.cpp (beside this file).
#pragma once
#include "bc7enc.h"
#include "bc7decomp.h"
#include "lodepng.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace ultra_build {
namespace fs = std::filesystem;

static const char *kRecipeVersion = "TTU-BUILT 2";
// never used: the green health pickup (the orange pickups mod recolours the game's own)
static const char *kExclude[] = {"tex1_64x64_86315efe98503949_14.dds", "tex1_64x64_86315efe98503949_14.png"};

struct Img { unsigned w = 0, h = 0; std::vector<uint8_t> px; };   // RGBA

using Progress = std::function<void(double frac, const std::string &what)>;

inline bool read_file(const fs::path &p, std::vector<uint8_t> &d)
{
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streamoff n = f.tellg();
    if (n < 0) return false;
    f.seekg(0);
    d.resize((size_t)n);
    return n == 0 || (bool)f.read((char *)d.data(), n);
}

// A BC7 DDS (DX10 header, format 98/99): sizes and levels.
struct Dds { unsigned w = 0, h = 0, levels = 0; size_t off = 148; };
inline bool dds_parse(const std::vector<uint8_t> &b, Dds &d)
{
    if (b.size() < 148 || memcmp(b.data(), "DDS ", 4) || memcmp(b.data() + 84, "DX10", 4)) return false;
    uint32_t h, w, mc, fmt;
    memcpy(&h, &b[12], 4); memcpy(&w, &b[16], 4); memcpy(&mc, &b[28], 4); memcpy(&fmt, &b[128], 4);
    if ((fmt != 98 && fmt != 99) || !w || !h || w > 16384 || h > 16384) return false;
    d.w = w; d.h = h; d.levels = mc ? mc : 1;
    return true;
}
inline size_t bc7_level_size(unsigned w, unsigned h) { return (size_t)((w + 3) / 4) * ((h + 3) / 4) * 16; }

// One level of a BC7 DDS, decoded (cropped to its size).
inline bool dds_level(const std::vector<uint8_t> &b, const Dds &d, unsigned lv, Img &out)
{
    size_t off = d.off;
    unsigned W = d.w, H = d.h;
    for (unsigned i = 0; i < lv; i++) { off += bc7_level_size(W, H); W = std::max(1u, W / 2); H = std::max(1u, H / 2); }
    if (off + bc7_level_size(W, H) > b.size()) return false;
    out.w = W; out.h = H; out.px.assign((size_t)W * H * 4, 0);
    unsigned bw = (W + 3) / 4, bh = (H + 3) / 4;
    for (unsigned by = 0; by < bh; by++)
        for (unsigned bx = 0; bx < bw; bx++) {
            bc7decomp::color_rgba px[16];
            bc7decomp::unpack_bc7(&b[off + ((size_t)by * bw + bx) * 16], px);
            for (unsigned y = 0; y < 4; y++)
                for (unsigned x = 0; x < 4; x++) {
                    unsigned X = bx * 4 + x, Y = by * 4 + y;
                    if (X < W && Y < H) memcpy(&out.px[((size_t)Y * W + X) * 4], px[y * 4 + x].m_comps, 4);
                }
        }
    return true;
}

inline bool png_load(const fs::path &p, Img &out)
{
    std::vector<uint8_t> file;
    if (!read_file(p, file)) return false;
    return lodepng::decode(out.px, out.w, out.h, file) == 0;
}

// Box filter (as png2dds): each target texel the mean of the source texels it covers.
inline Img box_resize(const Img &s, unsigned nw, unsigned nh)
{
    Img d; d.w = nw; d.h = nh; d.px.resize((size_t)nw * nh * 4);
    for (unsigned y = 0; y < nh; y++) {
        unsigned y0 = (unsigned)((uint64_t)y * s.h / nh), y1 = std::max(y0 + 1, (unsigned)((uint64_t)(y + 1) * s.h / nh));
        for (unsigned x = 0; x < nw; x++) {
            unsigned x0 = (unsigned)((uint64_t)x * s.w / nw), x1 = std::max(x0 + 1, (unsigned)((uint64_t)(x + 1) * s.w / nw));
            unsigned acc[4] = {0, 0, 0, 0}, n = 0;
            for (unsigned yy = y0; yy < y1; yy++)
                for (unsigned xx = x0; xx < x1; xx++) {
                    const uint8_t *p = &s.px[((size_t)yy * s.w + xx) * 4];
                    for (int c = 0; c < 4; c++) acc[c] += p[c];
                    n++;
                }
            for (int c = 0; c < 4; c++) d.px[((size_t)y * nw + x) * 4 + c] = (uint8_t)((acc[c] + n / 2) / n);
        }
    }
    return d;
}

// Colour under the transparent texels: the nearest solid texel's (filtering
// and the smaller levels then show no dark rims).
inline void bleed(Img &img)
{
    unsigned w = img.w, h = img.h;
    std::vector<int> d((size_t)w * h, 1 << 30);
    std::vector<uint32_t> src((size_t)w * h);
    bool any = false, all = true;
    for (size_t i = 0; i < (size_t)w * h; i++) {
        if (img.px[i * 4 + 3] >= 128) { d[i] = 0; src[i] = (uint32_t)i; any = true; }
        else all = false;
    }
    if (!any || all) return;
    for (int pass = 0; pass < 2; pass++)
        for (unsigned yy = 0; yy < h; yy++)
            for (unsigned xx = 0; xx < w; xx++) {
                unsigned y = pass ? h - 1 - yy : yy, x = pass ? w - 1 - xx : xx;
                size_t i = (size_t)y * w + x;
                static const int dx[4] = {-1, 0, -1, 1}, dy[4] = {0, -1, -1, -1};
                for (int k = 0; k < 4; k++) {
                    int nx = (int)x + (pass ? -dx[k] : dx[k]), ny = (int)y + (pass ? -dy[k] : dy[k]);
                    if (nx < 0 || ny < 0 || nx >= (int)w || ny >= (int)h) continue;
                    size_t j = (size_t)ny * w + nx;
                    if (d[j] + 1 < d[i]) { d[i] = d[j] + 1; src[i] = src[j]; }
                }
            }
    for (size_t i = 0; i < (size_t)w * h; i++)
        if (d[i]) memcpy(&img.px[i * 4], &img.px[(size_t)src[i] * 4], 3);
}

// Bicubic (a = -0.5, texel centres) of one 8-bit channel, for the masks.
inline std::vector<uint8_t> bicubic_resize(const std::vector<uint8_t> &s, unsigned sw, unsigned sh, unsigned dw, unsigned dh)
{
    auto k = [](double x) {
        x = std::fabs(x);
        const double a = -0.5;
        if (x < 1) return ((a + 2) * x - (a + 3)) * x * x + 1;
        if (x < 2) return ((a * x - 5 * a) * x + 8 * a) * x - 4 * a;
        return 0.0;
    };
    auto weights = [&](unsigned sn, unsigned dn, std::vector<int> &idx, std::vector<double> &wt, int &taps) {
        double scale = (double)sn / dn, support = scale > 1 ? 2 * scale : 2, fs_ = scale > 1 ? scale : 1;
        taps = (int)std::ceil(support) * 2 + 1;
        idx.assign((size_t)dn * taps, 0); wt.assign((size_t)dn * taps, 0);
        for (unsigned i = 0; i < dn; i++) {
            double c = (i + 0.5) * scale;
            int lo = (int)std::floor(c - support);
            double tot = 0;
            for (int t = 0; t < taps; t++) {
                int j = lo + t;
                double wv = k((j + 0.5 - c) / fs_);
                idx[(size_t)i * taps + t] = std::min(std::max(j, 0), (int)sn - 1);
                wt[(size_t)i * taps + t] = wv; tot += wv;
            }
            if (tot != 0) for (int t = 0; t < taps; t++) wt[(size_t)i * taps + t] /= tot;
        }
    };
    std::vector<int> ix, iy; std::vector<double> wx, wy; int tx, ty;
    weights(sw, dw, ix, wx, tx); weights(sh, dh, iy, wy, ty);
    std::vector<double> mid((size_t)dw * sh);
    for (unsigned y = 0; y < sh; y++)
        for (unsigned x = 0; x < dw; x++) {
            double v = 0;
            for (int t = 0; t < tx; t++) v += wx[(size_t)x * tx + t] * s[(size_t)y * sw + ix[(size_t)x * tx + t]];
            mid[(size_t)y * dw + x] = v;
        }
    std::vector<uint8_t> d((size_t)dw * dh);
    for (unsigned y = 0; y < dh; y++)
        for (unsigned x = 0; x < dw; x++) {
            double v = 0;
            for (int t = 0; t < ty; t++) v += wy[(size_t)y * ty + t] * mid[(size_t)iy[(size_t)y * ty + t] * dw + x];
            d[(size_t)y * dw + x] = (uint8_t)std::min(255.0, std::max(0.0, std::floor(v + 0.5)));
        }
    return d;
}

// A picture to a BC7 DDS with a full mip chain (as png2dds), the blocks
// shared out among threads.
inline bool bc7_write(const fs::path &out, Img img, int threads, std::atomic<uint64_t> *done_px = nullptr)
{
    static std::once_flag once;
    std::call_once(once, [] { bc7enc_compress_block_init(); });
    unsigned nw = (img.w + 3) & ~3u, nh = (img.h + 3) & ~3u;
    if (nw != img.w || nh != img.h) img = box_resize(img, nw, nh);
    unsigned w = img.w, h = img.h;
    bc7enc_compress_block_params p;
    bc7enc_compress_block_params_init(&p);
    bc7enc_compress_block_params_init_linear_weights(&p);
    p.m_uber_level = 0; p.m_max_partitions = 16;
    std::vector<uint8_t> data;
    int levels = 0;
    Img cur = std::move(img);
    for (;;) {
        unsigned bw = (cur.w + 3) / 4, bh = (cur.h + 3) / 4;
        size_t base = data.size();
        data.resize(base + (size_t)bw * bh * 16);
        std::atomic<unsigned> next{0};
        auto work = [&] {
            for (;;) {
                unsigned by = next++;
                if (by >= bh) break;
                for (unsigned bx = 0; bx < bw; bx++) {
                    uint8_t px[64];
                    for (unsigned y = 0; y < 4; y++)
                        for (unsigned x = 0; x < 4; x++) {
                            unsigned sx = std::min(bx * 4 + x, cur.w - 1), sy = std::min(by * 4 + y, cur.h - 1);
                            memcpy(px + (y * 4 + x) * 4, &cur.px[((size_t)sy * cur.w + sx) * 4], 4);
                        }
                    bc7enc_compress_block(&data[base + ((size_t)by * bw + bx) * 16], px, &p);
                }
                if (done_px) *done_px += (uint64_t)cur.w * 4;
            }
        };
        int nt = (int)std::min<unsigned>((unsigned)std::max(1, threads), bh);
        std::vector<std::thread> pool;
        for (int i = 1; i < nt; i++) pool.emplace_back(work);
        work();
        for (auto &t : pool) t.join();
        levels++;
        if (cur.w == 1 && cur.h == 1) break;
        cur = box_resize(cur, std::max(1u, cur.w / 2), std::max(1u, cur.h / 2));
    }
    uint32_t hd[37] = {0};
    hd[0] = 0x20534444; hd[1] = 124; hd[2] = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000; hd[3] = h; hd[4] = w;
    hd[5] = (uint32_t)bc7_level_size(w, h); hd[7] = (uint32_t)levels;
    hd[19] = 32; hd[20] = 0x4; hd[21] = 0x30315844; /* DX10 */
    hd[27] = 0x1000 | 0x400000 | 0x8;
    hd[32] = 98; hd[33] = 3; hd[34] = 0; hd[35] = 1; hd[36] = 0;
    fs::path tmp = out; tmp += ".part";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write((const char *)hd, sizeof hd);
        f.write((const char *)data.data(), (std::streamsize)data.size());
        if (!f) return false;
    }
    std::error_code ec;
    fs::rename(tmp, out, ec);
    return !ec;
}

// The 16 x 16 signature (as make_index.py): R G B A means.
inline void img_sig(const Img &im, uint8_t sig[1024])
{
    for (unsigned y = 0; y < 16; y++) {
        unsigned y0 = y * im.h / 16, y1 = std::max(y0 + 1, (y + 1) * im.h / 16);
        for (unsigned x = 0; x < 16; x++) {
            unsigned x0 = x * im.w / 16, x1 = std::max(x0 + 1, (x + 1) * im.w / 16);
            double acc[4] = {0, 0, 0, 0}; unsigned n = 0;
            for (unsigned yy = y0; yy < y1 && yy < im.h; yy++)
                for (unsigned xx = x0; xx < x1 && xx < im.w; xx++) {
                    for (int c = 0; c < 4; c++) acc[c] += im.px[((size_t)yy * im.w + xx) * 4 + c];
                    n++;
                }
            for (int c = 0; c < 4; c++) sig[(y * 16 + x) * 4 + c] = (uint8_t)std::min(255.0, std::max(0.0, std::nearbyint(n ? acc[c] / n : 0)));
        }
    }
}
// ... of the smallest level still 64 x 64 or more
inline bool dds_sig(const std::vector<uint8_t> &b, Dds &d, uint8_t sig[1024])
{
    if (!dds_parse(b, d)) return false;
    unsigned lv = 0, W = d.w, H = d.h;
    for (unsigned i = 0; i < d.levels; i++) {
        if (W >= 64 && H >= 64) lv = i;
        W = std::max(1u, W / 2); H = std::max(1u, H / 2);
    }
    Img im;
    if (!dds_level(b, d, lv, im)) return false;
    img_sig(im, sig);
    return true;
}
// ... of a picture before it was converted (a PNG's: its colours as they
// were under the transparent texels, which the game's textures share --
// the converted file has them bled)
inline void src_sig(Img im, uint8_t sig[1024])
{
    unsigned nw = (im.w + 3) & ~3u, nh = (im.h + 3) & ~3u;
    if (nw != im.w || nh != im.h) im = box_resize(im, nw, nh);
    while (im.w / 2 >= 64 && im.h / 2 >= 64) im = box_resize(im, im.w / 2, im.h / 2);
    img_sig(im, sig);
}

// The pack: GLOS69 in the game's folder (or one folder down, as some
// archives unpack it). Returns the path relative to the game's folder,
// '/'-separated, or "" when it is not there.
inline bool is_pack_dir(const fs::path &p)
{
    std::error_code ec;
    if (!fs::is_directory(p, ec)) return false;
    int n = 0;
    for (auto &e : fs::directory_iterator(p, ec)) {
        std::string s = e.path().filename().string();
        if (s.rfind("tex1_", 0) == 0 && ++n >= 50) return true;
    }
    return false;
}
inline std::string find_pack(const fs::path &game)
{
    static const char *cand[] = {"GLOS69", "GLOS69/GLOS69", "textures_ultra/GLOS69", "Load/Textures/GLOS69", "User/Load/Textures/GLOS69"};
    for (const char *c : cand)
        if (is_pack_dir(game / fs::u8path(c))) return c;
    return "";
}

struct PackFile { std::string name; unsigned gw, gh; bool png; uintmax_t size; };
inline std::vector<PackFile> pack_files(const fs::path &dir)
{
    static const std::regex re(R"(tex1_(\d+)x(\d+)_(m_)?([0-9a-f]{16})_(\d+)(.*)\.(dds|png)$)");
    std::vector<PackFile> v;
    std::error_code ec;
    for (auto &e : fs::directory_iterator(dir, ec)) {
        std::string n = e.path().filename().string();
        std::smatch m;
        if (!std::regex_match(n, m, re) || m[6].length()) continue;
        bool skip = false;
        for (const char *x : kExclude) if (n == x) skip = true;
        if (skip) continue;
        v.push_back({n, (unsigned)std::stoul(m[1]), (unsigned)std::stoul(m[2]), m[7] == "png", e.file_size(ec)});
    }
    std::sort(v.begin(), v.end(), [](const PackFile &a, const PackFile &b) { return a.name < b.name; });
    return v;
}

inline std::string stamp_for(const std::vector<PackFile> &files, const std::string &packrel)
{
    uintmax_t tot = 0;
    for (auto &f : files) tot += f.size;
    return std::string(kRecipeVersion) + "\n" + packrel + "\n" + std::to_string(files.size()) + " " + std::to_string(tot) + "\n";
}
inline std::string read_text(const fs::path &p)
{
    std::vector<uint8_t> d;
    return read_file(p, d) ? std::string(d.begin(), d.end()) : std::string();
}

// Is there a pack whose files are not built yet (or were built from another)?
inline bool needed(const fs::path &game)
{
    std::string rel = find_pack(game);
    if (rel.empty()) return false;
    std::string have = read_text(game / "textures_ultra" / "built.txt");
    have.erase(std::remove(have.begin(), have.end(), '\r'), have.end());   // (1.9.2: written in text mode on Windows, CRLF: never equal, built again and again)
    return have != stamp_for(pack_files(game / fs::u8path(rel)), rel);
}

struct Entry { std::string path; unsigned w, h, gw, gh; uint8_t sig[1024]; };

inline bool write_index(const fs::path &game, const std::vector<Entry> &ents)
{
    fs::path p = game / "textures_ultra" / "index.bin", tmp = p;
    tmp += ".part";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        uint32_t n = (uint32_t)ents.size();
        f.write("TTU1", 4); f.write((const char *)&n, 4);
        for (auto &e : ents) {
            char path[120] = {0};
            memcpy(path, e.path.c_str(), std::min<size_t>(119, e.path.size()));
            uint16_t s[4] = {(uint16_t)e.w, (uint16_t)e.h, (uint16_t)e.gw, (uint16_t)e.gh};
            f.write(path, 120); f.write((const char *)s, 8); f.write((const char *)e.sig, 1024);
        }
        if (!f) return false;
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
    return !ec;
}

// Pictures paired by hash only (pairs.txt), never by look: shape 1 x 0.
inline void hash_only(const fs::path &game, const char *sub, std::vector<Entry> &ents)
{
    std::vector<std::string> names;
    std::error_code ec;
    for (auto &e : fs::directory_iterator(game / "textures_ultra" / sub, ec)) {
        std::string n = e.path().filename().string();
        if (n.size() > 4 && n.substr(n.size() - 4) == ".dds") names.push_back(n);
    }
    std::sort(names.begin(), names.end());
    for (auto &n : names) {
        std::vector<uint8_t> b;
        Dds d;
        std::ifstream f(game / "textures_ultra" / sub / n, std::ios::binary);
        b.resize(148);
        if (!f.read((char *)b.data(), 148) || !dds_parse(b, d)) continue;
        Entry e{std::string("textures_ultra/") + sub + "/" + n, d.w, d.h, 1, 0, {0}};
        ents.push_back(e);
    }
}

// The index of the textures that ship with the game only (no pack).
inline bool write_ui_index(const fs::path &game)
{
    std::vector<Entry> ents;
    hash_only(game, "ui", ents);
    return write_index(game, ents);
}

// Newer than its source: already made (an earlier, interrupted run).
inline bool up_to_date(const fs::path &out, const fs::path &src)
{
    std::error_code e1, e2;
    auto a = fs::last_write_time(out, e1), b = fs::last_write_time(src, e2);
    return !e1 && !e2 && a >= b && fs::file_size(out, e1) > 148;
}

// Builds everything. Returns 0, or 1 with err set.
inline int build(const fs::path &game, const Progress &cb, std::string &err, int threads = 0)
{
    if (threads <= 0) threads = (int)std::max(1u, std::thread::hardware_concurrency() > 2 ? std::thread::hardware_concurrency() - 1 : 1u);
    fs::path tu = game / "textures_ultra";
    std::string rel = find_pack(game);
    if (rel.empty()) { err = "The GLOS69 texture pack is not in the game's folder."; return 1; }
    fs::path pack = game / fs::u8path(rel);
    std::vector<PackFile> files = pack_files(pack);
    std::error_code ec;
    for (const char *d : {"png", "bleed", "alphafix"}) fs::create_directories(tu / d, ec);
    fs::remove(tu / "built.txt", ec);

    // the jobs, weighed by the pixels to encode
    struct Job { int kind; std::string a, b; };   // 0 png, 1 bleed, 2 alpha
    std::vector<Job> jobs;
    std::set<std::string> have;
    for (auto &f : files) {
        have.insert(f.name);
        if (f.png) jobs.push_back({0, f.name, ""});
    }
    {
        std::ifstream r(tu / "recipe.txt");
        std::string ln;
        while (std::getline(r, ln)) {
            if (!ln.empty() && ln.back() == '\r') ln.pop_back();
            char k[16] = {0}, a[200] = {0}, b[200] = {0};
            int n = sscanf(ln.c_str(), "%15s %199s %199s", k, a, b);
            if (n >= 2 && !strcmp(k, "bleed") && have.count(a)) jobs.push_back({1, a, ""});
            if (n >= 3 && !strcmp(k, "alpha") && have.count(b)) jobs.push_back({2, a, b});
        }
    }
    std::atomic<uint64_t> done_px{0};
    uint64_t total_px = 0;
    std::vector<uint64_t> job_px(jobs.size());
    for (size_t i = 0; i < jobs.size(); i++) {
        // the size, from the header (a PNG's IHDR, a DDS's header)
        std::ifstream f(pack / (jobs[i].kind == 2 ? jobs[i].b : jobs[i].a), std::ios::binary);
        uint8_t h[32] = {0};
        f.read((char *)h, 32);
        uint64_t w = 512, hh = 512;
        if (!memcmp(h + 1, "PNG", 3)) { w = (uint64_t)h[16] << 24 | h[17] << 16 | h[18] << 8 | h[19]; hh = (uint64_t)h[20] << 24 | h[21] << 16 | h[22] << 8 | h[23]; }
        else if (!memcmp(h, "DDS ", 4)) { uint32_t a, b; memcpy(&a, h + 12, 4); memcpy(&b, h + 16, 4); hh = a; w = b; }
        job_px[i] = w * hh * 4 / 3 + 1;
        total_px += job_px[i];
    }
    uint64_t index_px = std::max<uint64_t>(total_px / 50, 1);   // the index: a small part of the work
    auto report = [&](const std::string &what) {
        if (cb) cb(std::min(0.999, (double)done_px / (double)(total_px + index_px)), what);
    };

    int made = 0, failed = 0;
    for (size_t i = 0; i < jobs.size(); i++) {
        const Job &j = jobs[i];
        uint64_t before = done_px;
        fs::path src, out;
        std::string what;
        if (j.kind == 0) {
            src = pack / j.a; out = tu / "png" / (j.a.substr(0, j.a.size() - 4) + ".dds");
            what = "Converting " + j.a;
        } else if (j.kind == 1) {
            src = pack / j.a; out = tu / "bleed" / j.a;
            what = "Softening the edges of " + j.a;
        } else {
            src = pack / j.b; out = tu / "alphafix" / (j.a + ".dds");
            what = "Fixing the transparency of " + j.b;
        }
        report(what);
        if (up_to_date(out, src)) { done_px = before + job_px[i]; continue; }
        Img img;
        bool ok;
        std::string sn = src.filename().string();
        if (sn.size() > 4 && sn.substr(sn.size() - 4) == ".png") ok = png_load(src, img);
        else {
            std::vector<uint8_t> b; Dds d;
            ok = read_file(src, b) && dds_parse(b, d) && dds_level(b, d, 0, img);
        }
        if (ok && j.kind == 2) {
            Img m;
            std::vector<uint8_t> file, gray;
            unsigned mw, mh;
            ok = read_file(tu / "masks" / (j.a + ".png"), file) && lodepng::decode(gray, mw, mh, file, LCT_GREY, 8) == 0;
            if (ok) {
                std::vector<uint8_t> a = bicubic_resize(gray, mw, mh, img.w, img.h);
                for (size_t k = 0; k < a.size(); k++) img.px[k * 4 + 3] = std::min(img.px[k * 4 + 3], a[k]);
            }
        }
        if (ok) { bleed(img); ok = bc7_write(out, std::move(img), threads, &done_px); }
        if (ok) made++; else failed++;
        done_px = before + job_px[i];
    }

    // the index: the textures that ship with the game first (their places
    // stay the same, for a game that is running), then the pack's
    report("Writing the index");
    std::vector<Entry> ents;
    hash_only(game, "ui", ents);
    hash_only(game, "alphafix", ents);
    for (auto &f : files) {
        std::string p = f.png ? "textures_ultra/png/" + f.name.substr(0, f.name.size() - 4) + ".dds" : rel + "/" + f.name;
        std::vector<uint8_t> b;
        Entry e;
        Dds d;
        if (!read_file(game / fs::u8path(p), b) || !dds_sig(b, d, e.sig) || (d.w % 4) || (d.h % 4)) continue;
        if (f.png) {
            Img src;
            if (png_load(pack / f.name, src)) src_sig(std::move(src), e.sig);
        }
        e.path = p; e.w = d.w; e.h = d.h; e.gw = f.gw; e.gh = f.gh;
        ents.push_back(e);
    }
    if (!write_index(game, ents)) { err = "Could not write textures_ultra/index.bin."; return 1; }
    {
        std::ofstream s(tu / "built.txt", std::ios::trunc | std::ios::binary);
        s << stamp_for(files, rel);
    }
    if (cb) cb(1.0, "Ultra textures ready: " + std::to_string(ents.size()) + " pictures" +
                        (failed ? " (" + std::to_string(failed) + " could not be made)" : ""));
    return 0;
}

}  // namespace ultra_build
