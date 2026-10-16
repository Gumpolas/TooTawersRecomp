// lang_ultra.cpp -- for lang.c (1.9.2): the Ultra textures' upscaled fonts
// with a language's letters. The DDS (BC7) reading and writing and the
// inflating are the launcher's (tools/ultra_textures/ultra_build.h).
#include "../tools/ultra_textures/ultra_build.h"
#include <cstdlib>

extern "C" {

// zlib data -> malloc'd bytes
int tt_ul_inflate(const unsigned char *in, size_t n, unsigned char **out, size_t *outn)
{
    unsigned char *o = nullptr;
    size_t on = 0;
    if (lodepng_zlib_decompress(&o, &on, in, n, &lodepng_default_decompress_settings) != 0) { free(o); return 0; }
    *out = o; *outn = on;
    return 1;
}

// a DDS's top level, RGBA (malloc'd)
int tt_ul_read_dds(const char *path, unsigned char **rgba, int *w, int *h)
{
    std::vector<uint8_t> b;
    ultra_build::Dds d;
    ultra_build::Img im;
    if (!ultra_build::read_file(ultra_build::fs::u8path(path), b) || !ultra_build::dds_parse(b, d) || !ultra_build::dds_level(b, d, 0, im))
        return 0;
    *rgba = (unsigned char *)malloc(im.px.size());
    if (!*rgba) return 0;
    memcpy(*rgba, im.px.data(), im.px.size());
    *w = (int)im.w; *h = (int)im.h;
    return 1;
}

// RGBA -> a BC7 DDS with its mip levels
int tt_ul_write_dds(const char *path, const unsigned char *rgba, int w, int h)
{
    ultra_build::Img im;
    im.w = (unsigned)w; im.h = (unsigned)h;
    im.px.assign(rgba, rgba + (size_t)w * h * 4);
    unsigned hc = std::thread::hardware_concurrency();
    std::error_code ec;
    ultra_build::fs::create_directories(ultra_build::fs::u8path(path).parent_path(), ec);
    return ultra_build::bc7_write(ultra_build::fs::u8path(path), std::move(im), hc > 1 ? (int)hc - 1 : 1) ? 1 : 0;
}

}
