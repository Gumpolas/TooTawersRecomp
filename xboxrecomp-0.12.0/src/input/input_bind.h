/* input_bind.h -- remappable controls (see input_bind.c). */
#ifndef XBOXRECOMP_INPUT_BIND_H
#define XBOXRECOMP_INPUT_BIND_H

enum {
    BIND_A, BIND_B, BIND_X, BIND_Y, BIND_BLACK, BIND_WHITE, BIND_LT, BIND_RT,
    BIND_START, BIND_BACK, BIND_DUP, BIND_DDOWN, BIND_DLEFT, BIND_DRIGHT,
    BIND_LTHUMB, BIND_RTHUMB,
    BIND_MOVE_UP, BIND_MOVE_DOWN, BIND_MOVE_LEFT, BIND_MOVE_RIGHT,
    BIND_LOOK_UP, BIND_LOOK_DOWN, BIND_LOOK_LEFT, BIND_LOOK_RIGHT,
    BIND_WALK,   /* keyboard only: held, the keys' left stick is pushed part way (on a pad: the stick itself) */
    BIND_N
};
#define BIND_KEYS 3
#define BIND_PAD_N 17   /* None, A B X Y LB RB LT RT Start Back Up Down Left Right LS RS */

extern volatile long xbox_bind_rev;
void        xbox_BindDefaults(int which);       /* -1 = layout from RECOMP_KB_LAYOUT, 0 wasd, 1 classic */
int         xbox_BindKey(int action, int i);    /* virtual-key code or 0 */
int         xbox_BindPad(int action);           /* pad source 0..16 */
const char *xbox_BindActionName(int action);
const char *xbox_BindActionKey(int action);
int         xbox_BindIsButton(int action);
const char *xbox_BindPadName(int src);
const char *xbox_BindKeyName(int vk);
void        xbox_BindFormatKeys(int action, char *out, int n);
void        xbox_BindSetSaveHandler(void (*fn)(const char *key, const char *value));
void        xbox_BindAssignKey(int action, int vk);
void        xbox_BindClearKeys(int action);
void        xbox_BindAssignPad(int action, int src);
void        xbox_BindResetAll(void);
int         xbox_BindSet(const char *key, const char *value);
#endif
