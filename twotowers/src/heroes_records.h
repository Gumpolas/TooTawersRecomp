/* Codec del sidecar de progreso. TTH3 identifica heroes por nombre estable;
 * TTH1/TTH2 solo se importan. No contiene ni modifica partidas Xbox. */
#ifndef TT_HEROES_RECORDS_H
#define TT_HEROES_RECORDS_H
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int tth_read(FILE *f, const char *const *names, int count,
                    int *have, unsigned char *records, size_t stride)
{
    static const char *const legacy[] = { "boromir", "gandalf", "frodo", "lurtz", "elrond", "sauron", "uruk" };
    long length; unsigned char *d; size_t pos; uint32_t n; int ok = 0;
    int *flags = NULL; unsigned char *blocks = NULL;
    if (count <= 0 || count > 64 || !stride || stride > 65536 ||
        fseek(f, 0, SEEK_END) || (length = ftell(f)) < 4 || length > 8 * 1024 * 1024 || fseek(f, 0, SEEK_SET)) return 0;
    d = (unsigned char *)malloc((size_t)length);
    flags = (int *)calloc((size_t)count, sizeof(int));
    blocks = (unsigned char *)calloc((size_t)count, stride);
    if (!d || !flags || !blocks || fread(d, 1, (size_t)length, f) != (size_t)length) goto done;
    if (!memcmp(d, "TTH1", 4) || !memcmp(d, "TTH2", 4)) {
        n = d[3] == '1' ? 4 : 7;
        if ((size_t)length != 4 + (n + 1) * (4 + stride)) goto done;
        for (uint32_t i = 0; i < n; ++i) {
            uint32_t flag; memcpy(&flag, d + 4 + i * 4, 4);
            if (flag > 1) goto done;
            for (int j = 0; j < count; ++j) if (!strcmp(names[j], legacy[i])) {
                flags[j] = (int)flag;
                memcpy(blocks + (size_t)j * stride, d + 4 + (n+1)*4 + i*stride, stride);
            }
        }
    } else if (!memcmp(d, "TTH3", 4)) {
        uint32_t size;
        if (length < 12) goto done;
        memcpy(&n, d + 4, 4); memcpy(&size, d + 8, 4);
        if (n > 64 || size != stride || (size_t)length != 12 + n*(36+stride)) goto done;
        for (uint32_t i = 0; i < n; ++i) {
            uint32_t flag; pos = 12 + i * (36+stride);
            if (!memchr(d + pos, 0, 32) || !d[pos]) goto done;
            for (uint32_t k = 0; k < i; ++k)
                if (!strcmp((char *)d + pos, (char *)d + 12 + k*(36+stride))) goto done;
            memcpy(&flag, d + pos + 32, 4); if (flag > 1) goto done;
            for (int j = 0; j < count; ++j) if (!strcmp(names[j], (char *)d + pos)) {
                flags[j] = (int)flag; memcpy(blocks + (size_t)j*stride, d + pos + 36, stride);
            }
        }
    } else goto done;
    memcpy(have, flags, (size_t)count*sizeof(int));
    memcpy(records, blocks, (size_t)count*stride); ok = 1;
done:
    free(d); free(flags); free(blocks); return ok;
}

static int tth_write(FILE *f, const char *const *names, int count,
                     const int *have, const unsigned char *records, size_t stride)
{
    uint32_t n = (uint32_t)count, size = (uint32_t)stride;
    if (count <= 0 || count > 64 || !stride || stride > 65536) return 0;
    if (fwrite("TTH3", 1, 4, f) != 4 || fwrite(&n, 4, 1, f) != 1 || fwrite(&size, 4, 1, f) != 1) return 0;
    for (int i = 0; i < count; ++i) {
        char name[32] = {0}; uint32_t flag = have[i] ? 1 : 0;
        if (!names[i][0] || strlen(names[i]) >= sizeof name) return 0;
        memcpy(name, names[i], strlen(names[i]));
        if (fwrite(name, 1, 32, f) != 32 || fwrite(&flag, 4, 1, f) != 1 ||
            fwrite(records + (size_t)i*stride, 1, stride, f) != stride) return 0;
    }
    return !ferror(f);
}
#endif
