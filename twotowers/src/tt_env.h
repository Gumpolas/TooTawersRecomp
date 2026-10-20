/* tt_env.h -- (1.9.3) getenv, looked up once per name.
 *
 * The test switches (TT_...) are read where the game's code is hooked --
 * some on every call of a hooked game function, hundreds of thousands of
 * times a second -- and msvcrt's getenv walks the whole environment each
 * time: a profile showed the game's thread spending a large share of its
 * time there. The environment is set once, before the game starts (the
 * settings file), so each name is read once and kept. TT_FIGSLOT, which
 * heroes.c changes as it goes, is always read afresh. */
#ifndef TT_ENV_H
#define TT_ENV_H
#include <stdlib.h>
const char *tt_getenv(const char *name);
#define getenv(n) tt_getenv(n)
#endif
