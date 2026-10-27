/* Validacion tambien al abrir el exe directamente: el codigo generado es USA.
 * El lanzador ya validaba el disco, pero se podia evitar ejecutando este exe. */
#if defined(_WIN32)
#include <bcrypt.h>
#endif
static int tt_xbe_supported(const void *data, size_t size)
{
#if defined(_WIN32)
    static const unsigned char wanted[32] = {
        0xae,0x2d,0x1b,0x53,0x04,0xb3,0xe9,0x57,0x23,0x39,0xb6,0x93,0xa6,0xa4,0x5a,0xd0,
        0x86,0x9b,0xda,0xee,0xc8,0x9a,0xae,0x37,0x4b,0x5c,0x14,0xfe,0xeb,0xbb,0x8d,0x30
    };
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    unsigned char digest[32];
    int ok = 0;
    if (size <= 0xffffffffu && BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) >= 0 &&
        BCryptCreateHash(algorithm, &hash, NULL, 0, NULL, 0, 0) >= 0 &&
        BCryptHashData(hash, (unsigned char *)data, (ULONG)size, 0) >= 0 &&
        BCryptFinishHash(hash, digest, 32, 0) >= 0) ok = !memcmp(digest, wanted, 32);
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) fprintf(stderr, "[XBE] Se requiere default.xbe USA ae2d1b53...ebbb8d30. PAL es fuente de idiomas, no un ejecutable compatible.\n");
    return ok;
#else
    (void)data; (void)size;
    return 1; /* Este parche se distribuye como candidato para Windows x64. */
#endif
}
