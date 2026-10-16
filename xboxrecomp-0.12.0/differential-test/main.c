#define _GNU_SOURCE
#include "harness.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/mman.h>
#include <math.h>
RECOMP_TLS double g_fp_stack[8]; RECOMP_TLS int g_fp_top; RECOMP_TLS uint16_t g_fp_control_word=0x37F; RECOMP_TLS int g_fp_cmp; RECOMP_TLS uint16_t g_fp_cc;
static double rdbl(void){ uint32_t k=rnd()%4; if(k==0) return (double)((int)(rnd()%2001)-1000); if(k==1) return ((int)(rnd()%2000001)-1000000)/1000.0; if(k==2) return ((int)(rnd()%2001)-1000)/7.0; return ((double)rnd()/4294967296.0-0.5)*1e6; }
extern Case CASES[]; extern int NCASES;
RECOMP_TLS uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi;
ptrdiff_t g_xbox_mem_offset = 0;
uint32_t g_hebp; int g_hcf; RECOMP_TLS uint32_t g_ebp, g_seh_ebp; void recomp_unimpl(const char*t,uint32_t v){(void)t;(void)v;}
Regs g_nat; uint64_t g_saved_rsp;
static uint64_t s=88172645463325252ull;
uint32_t rnd(void){ s^=s<<13; s^=s>>7; s^=s<<17; return (uint32_t)s; }
static uint32_t val(void){ static const uint32_t E[]={0,1,2,3,0x1F,0x20,0x21,0x7F,0x80,0x81,0xFF,0x100,0x7FFF,0x8000,0xFFFF,0x10000,0x7FFFFFFF,0x80000000u,0x80000001u,0xFFFFFFFEu,0xFFFFFFFFu};
  uint32_t k=rnd()%4; if(k==0) return E[rnd()%21]; if(k==1) return rnd()&0xFF; if(k==2) return (rnd()&0xFFFF) | ((rnd()&1)?0xFFFF0000u:0); return rnd(); }
static sigjmp_buf jb; static volatile int faulted;
static void onsig(int sig){ (void)sig; faulted=1; siglongjmp(jb,1); }
#define BUF 0x10000000u
_Static_assert(__builtin_offsetof(Regs,mm)==40,"mm"); _Static_assert(__builtin_offsetof(Regs,xmm)==112,"xmm");
static void rvec(Regs *r){ for(int i=0;i<8;i++){ r->mm[i]=((uint64_t)rnd()<<32)|rnd(); if(rnd()&1){ for(int j=0;j<4;j++){ float f=((int)(rnd()%200000)-100000)/100.0f; memcpy(&r->xmm[i][j*4],&f,4);} } else for(int j=0;j<16;j++) r->xmm[i][j]=rnd(); } }
#define SETV(r) do{ RecompMmx*M[8]={&g_mm0,&g_mm1,&g_mm2,&g_mm3,&g_mm4,&g_mm5,&g_mm6,&g_mm7}; RecompXmm*X[8]={&g_xmm0,&g_xmm1,&g_xmm2,&g_xmm3,&g_xmm4,&g_xmm5,&g_xmm6,&g_xmm7}; for(int q=0;q<8;q++){ M[q]->q=(r).mm[q]; memcpy(X[q],(r).xmm[q],16);} }while(0)
#define GETV(r) do{ RecompMmx*M[8]={&g_mm0,&g_mm1,&g_mm2,&g_mm3,&g_mm4,&g_mm5,&g_mm6,&g_mm7}; RecompXmm*X[8]={&g_xmm0,&g_xmm1,&g_xmm2,&g_xmm3,&g_xmm4,&g_xmm5,&g_xmm6,&g_xmm7}; for(int q=0;q<8;q++){ (r).mm[q]=M[q]->q; memcpy((r).xmm[q],X[q],16);} }while(0)
RECOMP_TLS RecompMmx g_mm0,g_mm1,g_mm2,g_mm3,g_mm4,g_mm5,g_mm6,g_mm7; RECOMP_TLS RecompXmm g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7;
static void ensure(uint32_t a){ void *p=(void*)(uintptr_t)(a&~0xFFFu); mmap(p,0x2000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0); }
int main(int argc,char**argv){
  int iters = argc>1?atoi(argv[1]):300;
  stack_t ss; ss.ss_sp=malloc(1<<16); ss.ss_size=1<<16; ss.ss_flags=0; sigaltstack(&ss,0);
  struct sigaction sa; memset(&sa,0,sizeof sa); sa.sa_handler=onsig; sa.sa_flags=SA_ONSTACK|SA_NODEFER; sigaction(SIGFPE,&sa,0); sigaction(SIGSEGV,&sa,0); sigaction(SIGBUS,&sa,0);
  if(mmap((void*)(uintptr_t)BUF,0x10000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==MAP_FAILED){perror("mmap");return 1;}
  int nbad=0;
  for(int ci=0;ci<NCASES;ci++){ Case *c=&CASES[ci]; int reported=0;
    if(c->absaddr) ensure(c->absaddr);
    for(int it=0;it<iters && !reported;it++){
      Regs r; memset(&r,0,sizeof r); r.Xeax=val();r.Xecx=val();r.Xedx=val();r.Xebx=val();r.Xesp=val();r.Xebp=val();r.Xesi=val();r.Xedi=val(); rvec(&r);
      if(rnd()&1){ r.Xecx = (r.Xecx&~0xFFu) | (rnd()%40); }
      int cf=rnd()&1; r.fl=0x2|cf;
      uint32_t T=BUF+0x8000+(rnd()%256);
      c->fix(&r,T);
      uint8_t win[512], awin[64], wn[512], an[64], sn[16], sl[16];
      for(int i=0;i<512;i++) win[i]=rnd(); for(int i=0;i<64;i++) awin[i]=rnd();
      uint8_t *W=(uint8_t*)(uintptr_t)(BUF+0x8000-128); uint8_t *A=(uint8_t*)(uintptr_t)(c->absaddr?c->absaddr-16:BUF+0x100);
      uint8_t *S=(uint8_t*)(uintptr_t)(BUF+0xF00);
      memcpy(W,win,512); memcpy(A,awin,64); memset(S,0xCC,16);
      int x87 = !strncmp(c->ccs,"x87:",4); int nout=0, fsw=0; double fin[8]; uint8_t *F=(uint8_t*)(uintptr_t)(BUF+0xE00); double fon[8], fol[8];
      if(x87){ sscanf(c->ccs+4,"%d:%d",&nout,&fsw); for(int i=0;i<8;i++) fin[i]=rdbl(); memcpy(F,fin,64); memset(F+0x80,0,64);
        /* plausible float/double data in the operand window too */
        for(int i=0;i<512;i+=8){ double d=rdbl(); float f=(float)d; if(rnd()&1) memcpy(win+i,&d,8); else { memcpy(win+i,&f,4); f=(float)rdbl(); memcpy(win+i+4,&f,4);} if(rnd()%4==0){ int32_t q=(int32_t)(rnd()%20001)-10000; memcpy(win+i,&q,4);} }
        memcpy(W,win,512); }
      Regs rn; int fn;
      g_nat=r; faulted=0; if(!sigsetjmp(jb,1)) c->nat(); fn=faulted; rn=g_nat;
      memcpy(wn,W,512); memcpy(an,A,64); memcpy(sn,S,16); if(x87) memcpy(fon,F+0x80,64);
      memcpy(W,win,512); memcpy(A,awin,64); memset(S,0xCC,16);
      int nv = c->ncc ? c->ncc : 1; Regs rl; int fl=0;
      for(int k=0;k<nv;k++){
        memcpy(W,win,512); memcpy(A,awin,64); if(k==0) memset(S,0xCC,16); if(x87){ memcpy(F,fin,64); memset(F+0x80,0,64); g_fp_top=0; g_fp_control_word=0x37F; g_fp_cmp=0; g_fp_cc=0; }
        g_eax=r.Xeax; g_ecx=r.Xecx; g_edx=r.Xedx; g_ebx=r.Xebx; g_esp=r.Xesp; g_hebp=r.Xebp; g_esi=r.Xesi; g_edi=r.Xedi; g_hcf=cf; SETV(r);
        faulted=0; if(!sigsetjmp(jb,1)) c->lift[k](); fl|=faulted;
        if(k==0){ memset(&rl,0,sizeof rl); rl.Xeax=g_eax;rl.Xecx=g_ecx;rl.Xedx=g_edx;rl.Xebx=g_ebx;rl.Xesp=g_esp;rl.Xebp=g_hebp;rl.Xesi=g_esi;rl.Xedi=g_edi; GETV(rl);}
      }
      memcpy(sl,S,16); if(x87) memcpy(fol,F+0x80,64);
      if(fn){ continue; }
      char why[512]; why[0]=0;
      if(fl) strcat(why,"lifted-fault ");
      const char *rn_[]={"eax","ecx","edx","ebx","esp","ebp","esi","edi"};
      if(x87&&fsw){ rn.Xeax&=0xFFFF4500u; rl.Xeax&=0xFFFF4500u; }
      for(int k=0;k<8;k++){ uint32_t a=((uint32_t*)&rn)[k], b=((uint32_t*)&rl)[k]; if(a!=b) sprintf(why+strlen(why),"%s nat=%08X lift=%08X ",rn_[k],a,b); }
      if(!x87) for(int k=0;k<8;k++) if(rn.mm[k]!=rl.mm[k]) sprintf(why+strlen(why),"mm%d nat=%016llX lift=%016llX ",k,(unsigned long long)rn.mm[k],(unsigned long long)rl.mm[k]);
      for(int k=0;k<8;k++) if(memcmp(rn.xmm[k],rl.xmm[k],16)){ uint32_t *a=(uint32_t*)rn.xmm[k],*b=(uint32_t*)rl.xmm[k]; sprintf(why+strlen(why),"xmm%d nat=%08X_%08X_%08X_%08X lift=%08X_%08X_%08X_%08X ",k,a[3],a[2],a[1],a[0],b[3],b[2],b[1],b[0]); }
      if(x87) for(int k=0;k<nout&&k<8;k++){ double a=fon[k], b=fol[k]; int ok = (isnan(a)&&isnan(b)) || a==b || fabs(a-b) <= 1e-6*fmax(fabs(a),fabs(b)); if(!ok) sprintf(why+strlen(why),"st%d nat=%.9g lift=%.9g ",k,a,b); }
      if(x87){ for(int i=0;i<8;i++) sprintf(why[0]?why+strlen(why):why,"%s", ""); }
      if(memcmp(wn,W,512)){ for(int i=0;i<512;i++) if(wn[i]!=W[i]){ sprintf(why+strlen(why),"mem[T%+d] nat=%02X lift=%02X ",i-128-(int)(T-BUF-0x8000),wn[i],W[i]); break; } }
      if(c->absaddr && memcmp(an,A,64)) sprintf(why+strlen(why),"absmem differs ");
      int gap=0; for(int i=0;i<c->ncc;i++) if(sn[i]!=sl[i]){ if(c->gap[i]) gap=1; sprintf(why+strlen(why),"cc#%d%s nat=%d lift=%d ",i,c->gap[i]?"(gap)":"",sn[i],sl[i]); }
      if(why[0]){ reported=1; nbad++;
        if(x87){ printf("   fin:"); for(int i=0;i<8;i++) printf(" %.6g",fin[i]); printf("\n"); }
        printf("%s [%s] {%s} in: eax=%08X ecx=%08X edx=%08X ebx=%08X esp=%08X ebp=%08X esi=%08X edi=%08X cf=%d :: %s\n",gap?"GAP":"BAD",c->txt,c->ccs,r.Xeax,r.Xecx,r.Xedx,r.Xebx,r.Xesp,r.Xebp,r.Xesi,r.Xedi,cf,why); }
    }
  }
  printf("cases=%d bad=%d\n",NCASES,nbad); return 0;
}
