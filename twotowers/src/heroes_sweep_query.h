/* Solo repite la consulta geometrica BA110, nunca el despacho de dano.
 * Una devolucion y una entrada FPU, con independencia del numero de sondas. */
#ifndef TT_HEROES_SWEEP_QUERY_H
#define TT_HEROES_SWEEP_QUERY_H
typedef struct {uint32_t r[9];double fp[8];int top,cmp;uint16_t control,cc;RecompXmm xmm[8];} TTSweepCPU;
static void tt_sweep_cpu_save(TTSweepCPU *s){
 uint32_t r[]={g_eax,g_ebx,g_ecx,g_edx,g_esi,g_edi,g_esp,g_ebp,g_seh_ebp};memcpy(s->r,r,sizeof r);
 memcpy(s->fp,g_fp_stack,sizeof s->fp);s->top=g_fp_top;s->cmp=g_fp_cmp;s->control=g_fp_control_word;s->cc=g_fp_cc;
 RecompXmm x[]={g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7};memcpy(s->xmm,x,sizeof x);
}
static void tt_sweep_cpu_restore(const TTSweepCPU *s){
 g_eax=s->r[0];g_ebx=s->r[1];g_ecx=s->r[2];g_edx=s->r[3];g_esi=s->r[4];g_edi=s->r[5];g_esp=s->r[6];g_ebp=s->r[7];g_seh_ebp=s->r[8];
 memcpy(g_fp_stack,s->fp,sizeof s->fp);g_fp_top=s->top;g_fp_cmp=s->cmp;g_fp_control_word=s->control;g_fp_cc=s->cc;
 g_xmm0=s->xmm[0];g_xmm1=s->xmm[1];g_xmm2=s->xmm[2];g_xmm3=s->xmm[3];g_xmm4=s->xmm[4];g_xmm5=s->xmm[5];g_xmm6=s->xmm[6];g_xmm7=s->xmm[7];
}
extern int tt_sweep_lanes(uint32_t);
extern void tt_sweep_lane(int);
extern void sub_000BA110_gen(void);
extern int tt_sweep_fallback(const uint32_t args[6]);
extern void tt_sweep_report(const uint32_t args[6],int lane,int fallback); /* (135) registro de combate */
static void tt_sweep_query(uint32_t attack){
 int count=tt_sweep_lanes(attack);if(count<=1){sub_000BA110_gen();return;}
 if(count>5)count=5;
 TTSweepCPU entry;tt_sweep_cpu_save(&entry);uint32_t args[6],seh=MEM32(XBOX_FS_BASE);
 for(int i=0;i<6;i++)args[i]=MEM32(g_esp+4*i);
 int hit=-1;
 for(int lane=0;lane<count;lane++){
  if(lane){tt_sweep_cpu_restore(&entry);MEM32(XBOX_FS_BASE)=seh;for(int i=0;i<6;i++)MEM32(g_esp+4*i)=args[i];}
  tt_sweep_lane(lane);sub_000BA110_gen();
  if(!(g_fp_stack[g_fp_top]<0.0)){if(g_fp_stack[g_fp_top]>=0.0)hit=lane;break;} /* NaN tampoco provoca reintentos. */
 }
 tt_sweep_lane(-1);
 /* (133) todas las sondas fallan: la cabeza de la maza pasa junto al
  * cuerpo de la victima (ya candidata del juego) -> parte 0. Solo escribe
  * en los parametros de salida de BA110 y en st0, nunca dano ni lista. */
 int fb=hit<0&&g_fp_stack[g_fp_top]<0.0&&tt_sweep_fallback(args);
 if(fb)g_fp_stack[g_fp_top]=0.0;
 tt_sweep_report(args,hit,fb);
}
#endif
