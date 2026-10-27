/* Reacciones del troll a partir de SUS poses: caida 25 y levantada creada.
 * No existe levantada NPC: 177 es caer a un foso (el cuerpo acaba 12 bajo
 * el suelo) y 179 trepar para salir; hasta 124 la ranura 30 usaba 177 y
 * terminaba estirada bajo el suelo, a 27,25 de la postura 34.
 * Desde 125 la levantada es la propia caida 25 al reves y mas lenta
 * (tumbado 2200 -> incorporado 850) y se funde con 34@0: empieza donde
 * acaba 29 y termina exactamente en la postura que siguen ataque/marcha.
 * No modifica el dano ni la invulnerabilidad. */
#ifndef TT_HEROES_TROLL_REACTION_H
#define TT_HEROES_TROLL_REACTION_H
static int tr_track(const unsigned char *r,size_t len,unsigned b,float time,Quat *q,float p[3]){
 unsigned bt=rd32(r,0x20);if(bt>len||b>=(len-bt)/4)return 0;unsigned bo=rd32(r,bt+4*b);
 *q=(Quat){1,0,0,0};p[0]=p[1]=p[2]=0;if(!bo)return 1;
 if(bo>len||len-bo<4||r[bo+2]||r[bo+3])return 0;unsigned nr=r[bo],np=r[bo+1],total=nr+np;if(total>(len-bo-4)/8)return 0;
 if(nr&&!rt_qsample(r+bo+4,nr,time,q))return 0;
 if(np&&!rt_sample(r+bo+4+8*nr,np,time,p))return 0;return 1;
}
/* Levantada 30: tiempo de NPC25 y peso de la postura 34@0 por fraccion.
 * 0-10%: asentarse tumbado (mezcla de poses 25@2200 -> 25@1450, sin recorrer
 * el respingo de pie de 25@2100); 10-66%: caida al
 * reves 1450->850, sin superar la velocidad de la caida nativa
 * (incorporarse); 66-100%: mezcla
 * suavizada hasta 34. Los brazos (26-49) bajan a la postura desde el 30%
 * para no levantarse con el retroceso de brazos alzados de 25@400-1000. */
static float troll_getup_time(float f,float *w,float *settle){
 *w=0;*settle=1;if(f<.10f){float x=f/.10f;*settle=x*x*(3-2*x);return 1450;}
 if(f<.66f){float x=(f-.10f)/.56f;x=.5f*x+.5f*x*x*(3-2*x);return 1450-600*x;}
 float x=(f-.66f)/.34f;*w=x*x*(3-2*x);return 850;
}
static unsigned char *npc_troll_reaction(int g,unsigned slot,const unsigned char *a,size_t al,
 const unsigned char *G,size_t gl,size_t *length){
 if(g!=9||slot<27||slot>30||al<64||gl<0x58)return NULL;
 unsigned nb=rd32(G,0x48),tab=rd32(G,0x10),gns=rd16(G,6),dur=rd32(a,8);
 if(nb!=56||gns<=34||tab>gl||gns>(gl-tab)/4||!dur||dur>2000)return NULL;
 unsigned f=rd32(G,tab+4*25),u=rd32(G,tab+4*34);if(!f||!u||f>gl||u>gl||gl-f<64||gl-u<64)return NULL;
 const unsigned char *fall=G+f,*up=G+u;size_t fl=rd32(fall,0x14),ul=rd32(up,0x14);
 if(fl>gl-f||ul>gl-u||rd32(fall,8)<2200||rd32(up,8)!=1166)return NULL;
 if(slot==27){
  /* Mantener las claves originales, en especial las cercanas a W=0.
   * Volver a cuantizarlas causaba errores visibles en brazos/piernas. */
  unsigned char *o=malloc(1024*1024);if(!o)return NULL;
  size_t n=npc_clip_range(G,gl,fall,fl,o,1024*1024,0,2200,0,0,0);if(!n){free(o);return NULL;}
  unsigned bt=rd32(o,0x20);
  for(unsigned b=0;b<nb;b++){
   unsigned bo=rd32(o,bt+4*b);if(!bo)continue;unsigned nr=o[bo],np=o[bo+1],total=nr+np+o[bo+2]+o[bo+3];
   for(unsigned k=0;k<total;k++){
    unsigned at=bo+4+8*k,packed=rd16(o,at),time=(unsigned)lroundf((packed&0x7fff)*dur/2200.0f);
    wr16(o,at,(uint16_t)((packed&0x8000)|time));
    if(b==0&&k>=nr&&k<nr+np){Quat unused;float p[3];if(!tr_track(a,al,0,time,&unused,p)){free(o);return NULL;}wr16(o,at+2,(uint16_t)(int16_t)lroundf(p[0]));wr16(o,at+4,(uint16_t)(int16_t)lroundf(p[1]));}
   }
  }
  wr32(o,8,dur);*length=n;return o;
 }
 unsigned nt=(dur+7)/8+1;if(nt>255)return NULL;
 size_t cap=64+(size_t)nb*(4+16*nt)+4*nb;unsigned char *o=calloc(1,cap);if(!o)return NULL;
 memcpy(o,fall,64);unsigned offsets[128];size_t pos=64;
 for(unsigned b=0;b<nb;b++){
  offsets[b]=(unsigned)pos;o[pos]=(unsigned char)nt;o[pos+1]=(unsigned char)nt;pos+=4;
  for(int component=0;component<2;component++)for(unsigned j=0;j<nt;j++){
   float t=(float)dur*j/(nt-1),fraction=t/dur,ft=2200,ut=0,w=0;
   float settle=1;if(slot==30)ft=troll_getup_time(fraction,&w,&settle);
   Quat q1,q2;float p1[3],p2[3];if(!tr_track(fall,fl,b,ft,&q1,p1)||!tr_track(up,ul,b,ut,&q2,p2))goto bad;
   if(settle<1){Quat q0;float p0[3];if(!tr_track(fall,fl,b,2200,&q0,p0))goto bad;q1=mq_mix(q0,q1,settle);for(int c=0;c<3;c++)p1[c]=p0[c]*(1-settle)+p1[c]*settle;}
   /* brazos (26-49) hacia la postura desde el 30%: la parte erguida de 25
    * es su retroceso con ambos brazos alzados */
   if(slot==30&&b>=26&&b<=49&&fraction>.30f){float x=(fraction-.30f)/.70f;x=x*x*(3-2*x);if(x>w)w=x;}
   Quat q=mq_mix(q1,q2,w);unsigned time=(unsigned)lroundf(t);
   wr16(o,pos,(uint16_t)time);
   if(!component){if(q.w<0){q.x=-q.x;q.y=-q.y;q.z=-q.z;}wr16(o,pos+2,(uint16_t)rt_qpack(q.x));wr16(o,pos+4,(uint16_t)rt_qpack(q.y));wr16(o,pos+6,(uint16_t)rt_qpack(q.z));}
   else{
    float p[3];for(int c=0;c<3;c++)p[c]=p1[c]*(1-w)+p2[c]*w;
    if(b==0){Quat unused;float hp[3];if(!tr_track(a,al,0,t,&unused,hp))goto bad;p[0]=hp[0];p[1]=hp[1];}
    for(int c=0;c<3;c++){long v=lroundf(p[c]);if(v< -32768||v>32767)goto bad;wr16(o,pos+2+2*c,(uint16_t)(int16_t)v);}
   }pos+=8;
  }
 }
 unsigned bt=(unsigned)pos;for(unsigned b=0;b<nb;b++){wr32(o,pos,offsets[b]);pos+=4;}
 wr32(o,8,dur);wr32(o,0x14,(unsigned)pos);wr32(o,0x18,0);wr32(o,0x1c,bt);wr32(o,0x20,bt);
 wr32(o,0x24,0xffff);wr32(o,0x28,(unsigned)pos);wr32(o,0x2c,0);wr32(o,0x30,0);wr32(o,0x34,0);wr32(o,0x38,0);
 *length=pos;return o;
bad:free(o);return NULL;
}
#endif
