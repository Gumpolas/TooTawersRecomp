/* 28/29 son fases en el suelo, no una segunda caida. Los NPC suelen tener
 * 27 y 30 solamente. Si falta una fase, mantener su propia pose final 27.
 * Los perfiles que ya usan reacciones retargeteadas quedan intactos. */
#ifndef TT_HEROES_GROUND_REACTION_H
#define TT_HEROES_GROUND_REACTION_H
static unsigned char *npc_ground_hold(int g,unsigned slot,const unsigned char *a,size_t al,
 const unsigned char *G,size_t gl,size_t *length){
 if(g<0||g>=N_EXTRA||(slot!=28&&slot!=29)||strchr(s_pools[g].rtcls,'n')||al<64||gl<0x58)return NULL;
 /* Su 27 termina en pie: no tratarlo como una pose tumbada. El perfil
  * de Sauron se conserva hasta disponer de una reaccion apropiada. */
 if(g==5)return NULL;
 unsigned nb=rd32(G,0x48),ns=rd16(G,6),tab=rd32(G,0x10);
 if(!nb||nb>128||ns<=slot||tab>gl||ns>(gl-tab)/4||rd32(G,tab+4*slot))return NULL;
 unsigned off=rd32(G,tab+4*27);if(!off||off>gl||gl-off<64)return NULL;
 const unsigned char *r=G+off;size_t rl=rd32(r,0x14);unsigned du=rd32(r,8),dur=rd32(a,8),bt=rd32(r,0x20);
 if(!du||!dur||dur>32767||rl>gl-off||bt>rl||nb>(rl-bt)/4)return NULL;
 size_t cap=64+24*nb;unsigned char *o=calloc(1,cap);if(!o)return NULL;memcpy(o,r,64);
 unsigned offsets[128];size_t pos=64;
 for(unsigned b=0;b<nb;b++){
  Quat q;float p[3];if(!tr_track(r,rl,b,(float)du,&q,p))goto bad;
  offsets[b]=(unsigned)pos;o[pos]=o[pos+1]=1;pos+=4;
  unsigned bo=rd32(r,bt+4*b),nr=bo?r[bo]:0;
  /* Evitar una segunda cuantizacion de la ultima clave original. */
  if(nr&&rt_key_time(r+bo+4+8*(nr-1))<=du){memcpy(o+pos,r+bo+4+8*(nr-1),8);wr16(o,pos,(uint16_t)(rd16(o,pos)&0x8000));}
  else{if(q.w<0){q.x=-q.x;q.y=-q.y;q.z=-q.z;}wr16(o,pos+2,(uint16_t)rt_qpack(q.x));wr16(o,pos+4,(uint16_t)rt_qpack(q.y));wr16(o,pos+6,(uint16_t)rt_qpack(q.z));}
  pos+=8;
  if(b==0){Quat unused;float hp[3];if(!tr_track(a,al,0,0,&unused,hp))goto bad;p[0]=hp[0];p[1]=hp[1];}
  for(int c=0;c<3;c++){long v=lroundf(p[c]);if(v< -32768||v>32767)goto bad;wr16(o,pos+2+2*c,(uint16_t)(int16_t)v);}pos+=8;
 }
 unsigned newbt=(unsigned)pos;for(unsigned b=0;b<nb;b++){wr32(o,pos,offsets[b]);pos+=4;}
 wr32(o,8,dur);wr32(o,0x14,(unsigned)pos);wr32(o,0x18,0);wr32(o,0x1c,newbt);wr32(o,0x20,newbt);
 wr32(o,0x24,0xffff);wr32(o,0x28,(unsigned)pos);wr32(o,0x2c,0);wr32(o,0x30,0);wr32(o,0x34,0);wr32(o,0x38,0);
 *length=pos;return o;
bad:free(o);return NULL;
}
#endif
