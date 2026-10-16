// png2dds: a PNG to a BC7 DDS with a full mip chain (Ultra textures pack converter)
#include "bc7enc.h"
#include "lodepng.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include <algorithm>
static std::vector<uint8_t> resize(const std::vector<uint8_t>& s, int w, int h, int nw, int nh) {
    std::vector<uint8_t> d((size_t)nw * nh * 4);
    for (int y = 0; y < nh; y++) {
        int y0 = (int)((long long)y * h / nh), y1 = std::max(y0 + 1, (int)((long long)(y + 1) * h / nh));
        for (int x = 0; x < nw; x++) {
            int x0 = (int)((long long)x * w / nw), x1 = std::max(x0 + 1, (int)((long long)(x + 1) * w / nw));
            unsigned acc[4] = {0, 0, 0, 0}, n = 0;
            for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++) {
                const uint8_t* p = &s[((size_t)yy * w + xx) * 4];
                for (int c = 0; c < 4; c++) acc[c] += p[c];
                n++;
            }
            for (int c = 0; c < 4; c++) d[((size_t)y * nw + x) * 4 + c] = (uint8_t)((acc[c] + n / 2) / n);
        }
    }
    return d;
}
int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "png2dds in.png out.dds\n"); return 1; }
    std::vector<uint8_t> img; unsigned w, h;
    if (lodepng::decode(img, w, h, argv[1])) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    unsigned nw = (w + 3) & ~3u, nh = (h + 3) & ~3u;
    if (nw != w || nh != h) { img = resize(img, w, h, nw, nh); w = nw; h = nh; }
    {   // colour under the transparent texels: the nearest solid texel's (no dark rims in the levels)
        std::vector<int> d((size_t)w*h, 1<<30); std::vector<uint32_t> src((size_t)w*h);
        bool any=false, all=true;
        for (size_t i=0;i<(size_t)w*h;i++){ if (img[i*4+3]>=128){d[i]=0;src[i]=(uint32_t)i;any=true;} else all=false; }
        if (any && !all) {
            for (int pass=0; pass<2; pass++) for (unsigned yy=0; yy<h; yy++) for (unsigned xx=0; xx<w; xx++) {
                unsigned y = pass ? h-1-yy : yy, x = pass ? w-1-xx : xx; size_t i=(size_t)y*w+x;
                int dx[4]={-1,0,-1,1}, dy[4]={0,-1,-1,-1};
                for (int k=0;k<4;k++){ int nx=(int)x+(pass?-dx[k]:dx[k]), ny=(int)y+(pass?-dy[k]:dy[k]);
                    if (nx<0||ny<0||nx>=(int)w||ny>=(int)h) continue; size_t j=(size_t)ny*w+nx;
                    if (d[j]+1<d[i]){ d[i]=d[j]+1; src[i]=src[j]; } }
            }
            for (size_t i=0;i<(size_t)w*h;i++) if (d[i]) memcpy(&img[i*4], &img[src[i]*4], 3);
        }
    }
    bc7enc_compress_block_init();
    bc7enc_compress_block_params p; bc7enc_compress_block_params_init(&p);
    bc7enc_compress_block_params_init_linear_weights(&p);
    p.m_uber_level = 0; p.m_max_partitions = 16;
    std::vector<uint8_t> out; int levels = 0;
    std::vector<uint8_t> cur = img; unsigned cw = w, ch = h;
    for (;;) {
        unsigned bw = (cw + 3) / 4, bh = (ch + 3) / 4;
        for (unsigned by = 0; by < bh; by++) for (unsigned bx = 0; bx < bw; bx++) {
            uint8_t px[64]; uint8_t blk[16];
            for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
                unsigned sx = std::min(bx * 4 + x, cw - 1), sy = std::min(by * 4 + y, ch - 1);
                memcpy(px + (y * 4 + x) * 4, &cur[((size_t)sy * cw + sx) * 4], 4);
            }
            bc7enc_compress_block(blk, px, &p);
            out.insert(out.end(), blk, blk + 16);
        }
        levels++;
        if (cw == 1 && ch == 1) break;
        unsigned nw2 = std::max(1u, cw / 2), nh2 = std::max(1u, ch / 2);
        cur = resize(cur, cw, ch, nw2, nh2); cw = nw2; ch = nh2;
    }
    uint32_t hd[37] = {0};
    hd[0] = 0x20534444; hd[1] = 124; hd[2] = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000; hd[3] = h; hd[4] = w;
    hd[5] = ((w + 3) / 4) * ((h + 3) / 4) * 16; hd[7] = levels;
    hd[19] = 32; hd[20] = 0x4; hd[21] = 0x30315844; /* DX10 */
    hd[27] = 0x1000 | 0x400000 | 0x8;
    hd[32] = 98; hd[33] = 3; hd[34] = 0; hd[35] = 1; hd[36] = 0;
    FILE* f = fopen(argv[2], "wb"); if (!f) return 1;
    fwrite(hd, 4, 37, f); fwrite(out.data(), 1, out.size(), f); fclose(f);
    printf("%s %ux%u %d levels\n", argv[2], w, h, levels);
    return 0;
}
