#include "recomp_types.h"
typedef struct { uint32_t Xeax,Xecx,Xedx,Xebx,Xesp,Xebp,Xesi,Xedi,fl,pad; uint64_t mm[8]; uint32_t pad2[2]; uint8_t xmm[8][16]; } __attribute__((aligned(16))) Regs;
typedef struct { void (*fix)(Regs*,uint32_t); void (*lift[14])(void); char gap[14]; void (*nat)(void); int ncc; uint32_t absaddr; const char *txt; const char *ccs; } Case;
extern uint32_t g_hebp; extern int g_hcf;
uint32_t rnd(void);
#define LIFT_PROLOGUE uint32_t ebp = g_hebp; int _cf = g_hcf; int _flags = 0; uint32_t _fa=0,_fb=0; int32_t _fas=0,_fbs=0; (void)_flags;(void)_fa;(void)_fb;(void)_fas;(void)_fbs;
#define LIFT_EPILOGUE g_hebp = ebp; g_hcf = _cf;
#define HS ((volatile uint8_t*)(uintptr_t)0x10000F00u)
#define fp_push(v) do { double _fp_value = (v); g_fp_top = (g_fp_top + 7u) & 7u; g_fp_stack[g_fp_top] = _fp_value; } while (0)
#define fp_pop() (g_fp_top = (g_fp_top + 1u) & 7u)
#define fp_top() g_fp_stack[g_fp_top]
#define fp_st(i) g_fp_stack[(g_fp_top + (i)) & 7u]
#define fp_st1() fp_st(1)
