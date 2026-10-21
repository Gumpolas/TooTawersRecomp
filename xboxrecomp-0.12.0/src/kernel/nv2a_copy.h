/* nv2a_copy.h -- (1.9.5) word copies for the pushbuffer path */
#ifndef NV2A_COPY_H
#define NV2A_COPY_H
#include <stdint.h>
#include <string.h>
#include <emmintrin.h>
/* Copies on the pushbuffer path (the game thread recording, the render
 * thread drawing). Into the graphics card's mapped
 * buffers (write-combined memory: written, never read) with streaming
 * stores, 16 bytes at a time; elsewhere unaligned SSE2. msvcrt's memcpy was
 * the render thread's largest single cost. Lengths are whole words. */
static inline void rt_copy_wc(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst; const uint8_t *s = (const uint8_t *)src;
    while (((uintptr_t)d & 15) && n >= 4) { uint32_t v; memcpy(&v, s, 4); memcpy(d, &v, 4); d += 4; s += 4; n -= 4; }
    while (n >= 64) {
        __m128i a = _mm_loadu_si128((const __m128i *)s), b = _mm_loadu_si128((const __m128i *)(s + 16));
        __m128i c = _mm_loadu_si128((const __m128i *)(s + 32)), e = _mm_loadu_si128((const __m128i *)(s + 48));
        _mm_stream_si128((__m128i *)d, a); _mm_stream_si128((__m128i *)(d + 16), b);
        _mm_stream_si128((__m128i *)(d + 32), c); _mm_stream_si128((__m128i *)(d + 48), e);
        d += 64; s += 64; n -= 64;
    }
    while (n >= 16) { _mm_stream_si128((__m128i *)d, _mm_loadu_si128((const __m128i *)s)); d += 16; s += 16; n -= 16; }
    while (n >= 4) { uint32_t v; memcpy(&v, s, 4); memcpy(d, &v, 4); d += 4; s += 4; n -= 4; }
    while (n) { *d++ = *s++; n--; }
}
static inline void rt_copy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst; const uint8_t *s = (const uint8_t *)src;
    while (n >= 64) {
        __m128i a = _mm_loadu_si128((const __m128i *)s), b = _mm_loadu_si128((const __m128i *)(s + 16));
        __m128i c = _mm_loadu_si128((const __m128i *)(s + 32)), e = _mm_loadu_si128((const __m128i *)(s + 48));
        _mm_storeu_si128((__m128i *)d, a); _mm_storeu_si128((__m128i *)(d + 16), b);
        _mm_storeu_si128((__m128i *)(d + 32), c); _mm_storeu_si128((__m128i *)(d + 48), e);
        d += 64; s += 64; n -= 64;
    }
    while (n >= 16) { _mm_storeu_si128((__m128i *)d, _mm_loadu_si128((const __m128i *)s)); d += 16; s += 16; n -= 16; }
    while (n >= 4) { uint32_t v; memcpy(&v, s, 4); memcpy(d, &v, 4); d += 4; s += 4; n -= 4; }
    while (n) { *d++ = *s++; n--; }
}
#endif
