/* Respuesta de los ataques nativos del troll (desde 126).
 * Hasta 125 el troll reproducia los clips NPC 45/41/43 completos: el arma
 * abria a 633/833/566 ms (el heroe a 0-133 ms) y de cerca le golpeaban o
 * el enemigo ya se habia movido. No se recorta la preparacion (empezaria
 * en una pose alzada, a 12-20 de la postura): se comprime solo donde el
 * cuerpo se mueve despacio. Cada tramo de 10 ms antes de abrir el arma se
 * acelera hasta que su hueso mas rapido alcanza el 80% del movimiento
 * mas rapido del clip NPC hasta cerrar el arma (como mucho x4; nunca se
 * ralentiza), con un filtro de
 * maximos para que el ritmo cambie suave. Desde la apertura el clip es el
 * NPC desplazado: barrido, impacto, cierre y recuperacion intactos.
 * Marcas 0x204/0x1004 y sus descriptores se conservan
 * (troll_native_damage los copia luego). */
#ifndef TT_HEROES_TROLL_ATTACK_H
#define TT_HEROES_TROLL_ATTACK_H
#define TROLL_WIND_MAXN 400
#define TROLL_WIND_KMAX 4.0f
#define TROLL_WIND_SPEED .80f
typedef struct{unsigned open,n,w;float T[TROLL_WIND_MAXN+1];}TrollWind;
static float troll_pose_step(const float (*P)[3],const float (*Q)[3]){
    float m=0;for(int b=1;b<56;b++){float dd=0;for(int c=0;c<3;c++){float d=(P[b][c]-P[0][c])-(Q[b][c]-Q[0][c]);dd+=d*d;}if(dd>m)m=dd;}return sqrtf(m);
}
static int troll_wind_plan(const unsigned char *S,size_t len,const unsigned char *r,size_t rl,unsigned open,unsigned close,TrollWind *m){
    static float P[128][3],Q[128][3];float f[TROLL_WIND_MAXN],g[TROLL_WIND_MAXN],strike=0;
    unsigned n=(open+9)/10;if(!n||n>TROLL_WIND_MAXN)return 0;
    unsigned s0=0; /* referencia: lo mas rapido del propio clip hasta cerrar el arma */
    if(rt_pose(S,len,r,rl,(float)s0,P)!=56)return 0;
    for(unsigned t=s0+10;t<=close;t+=10){if(rt_pose(S,len,r,rl,(float)t,Q)!=56)return 0;float d=troll_pose_step((const float(*)[3])P,(const float(*)[3])Q);if(d>strike)strike=d;memcpy(P,Q,sizeof P);}
    if(strike<=0)return 0;float v=TROLL_WIND_SPEED*strike;
    if(rt_pose(S,len,r,rl,0,P)!=56)return 0;
    for(unsigned i=0;i<n;i++){
        unsigned t0=10*i,t1=10*(i+1)<open?10*(i+1):open;
        if(rt_pose(S,len,r,rl,(float)t1,Q)!=56)return 0;
        float s=troll_pose_step((const float(*)[3])P,(const float(*)[3])Q)*10.0f/(float)(t1-t0),k=s/v;
        if(k<1/TROLL_WIND_KMAX)k=1/TROLL_WIND_KMAX;if(k>1)k=1;f[i]=k;memcpy(P,Q,sizeof P);
    }
    /* filtro de maximos (+-3 tramos): solo ralentiza, nunca supera v */
    for(unsigned i=0;i<n;i++){float k=f[i];for(unsigned j=i>3?i-3:0;j<=i+3&&j<n;j++)if(f[j]>k)k=f[j];g[i]=k;}
    m->T[0]=0;for(unsigned i=0;i<n;i++){unsigned t0=10*i,t1=10*(i+1)<open?10*(i+1):open;m->T[i+1]=m->T[i]+g[i]*(float)(t1-t0);}
    m->open=open;m->n=n;m->w=(unsigned)lroundf(m->T[n]);if(!m->w||m->w>=open)return 0;
    float sc=(float)m->w/m->T[n];for(unsigned i=0;i<=n;i++)m->T[i]*=sc;
    return 1;
}
static float troll_wind_map(float t,const TrollWind *m){
    if(t>=m->open)return t-m->open+m->w;if(t<=0)return 0;
    unsigned i=(unsigned)(t/10);if(i>=m->n)i=m->n-1;
    float t0=10.0f*i,t1=10.0f*(i+1)<m->open?10.0f*(i+1):(float)m->open;
    return m->T[i]+(m->T[i+1]-m->T[i])*(t-t0)/(t1-t0);
}
static size_t troll_attack_wind(const unsigned char *S,size_t len,const unsigned char *r,size_t rl,
 unsigned char *o,size_t cap,unsigned *open_out,unsigned *window_out){
    unsigned pair[2];if(rl<64||len<0x4c)return 0;
    if(rd32(r,0x14)<rl)rl=rd32(r,0x14); /* grlen llega como resto del banco */
    if(rl<64||rl>cap||!troll_damage_pair(r,rl,pair))return 0;
    unsigned dur=rd32(r,8),open=rd16(r,rd32(r,0x34)+4*pair[0]),close=rd16(r,rd32(r,0x34)+4*pair[1]);
    static TrollWind plan;if(dur>32767||!troll_wind_plan(S,len,r,rl,open,close,&plan))return 0;
    unsigned w=plan.w;
    unsigned nb=rd32(S,0x48),bt=rd32(r,0x20),gev=rd32(r,0x1c);
    if(nb>128||bt>rl||nb>(rl-bt)/4||gev>rl)return 0;
    unsigned nm=rd32(r,0x30),mt=rd32(r,0x34),mc=rd32(r,0x38);
    if(nm>64||mt>rl||mc>rl||nm>(rl-mt)/4||nm>(rl-mc)/24)return 0;
    /* Se reconstruye el clip: en las pistas con claves a ambos lados de la
     * apertura se intercala una muestra en ella, para que desde la
     * apertura todas las pistas sean exactamente las del NPC desplazadas. */
    memcpy(o,r,64);size_t pos=64;unsigned offsets[128];memset(offsets,0,sizeof offsets);
    for(unsigned b=0;b<nb;b++){
        unsigned bo=rd32(r,bt+4*b);if(!bo)continue;
        if(bo+4>gev||pos+4>cap)return 0;unsigned from=bo+4;
        offsets[b]=(unsigned)pos;size_t head=pos;memset(o+pos,0,4);pos+=4;
        for(int c=0;c<4;c++){
            unsigned count=r[bo+c],prev=0,n=0;if(from+8u*count>gev)return 0;
            for(unsigned j=0;j<count;j++){
                unsigned at=from+8*j,packed=rd16(r,at),kt=packed&0x7fffu;
                if(c<2&&j&&rt_key_time(r+at-8)<open&&kt>open)
                 /* muestra en la apertura (tiempo w en el clip nuevo) y, en
                  * rotaciones, cada 8 ms hasta la clave siguiente: NLERP no
                  * es asociativa y partir el tramo desviaba la maza */
                 for(unsigned ts=open;ts<kt;ts+=c?kt:8){
                    if(pos+8>cap)return 0;unsigned flag=0,nt=ts-open+w;
                    if(!c){Quat q;if(!rt_qsample(r+from,(int)count,(float)ts,&q))return 0;if(q.w<0){flag=0x8000u;q.x=-q.x;q.y=-q.y;q.z=-q.z;}
                        wr16(o,(unsigned)pos+2,(uint16_t)rt_qpack(q.x));wr16(o,(unsigned)pos+4,(uint16_t)rt_qpack(q.y));wr16(o,(unsigned)pos+6,(uint16_t)rt_qpack(q.z));}
                    else{float v[3];if(!rt_sample(r+from,(int)count,(float)ts,v))return 0;for(int k=0;k<3;k++)wr16(o,(unsigned)pos+2+2*k,(uint16_t)(int16_t)lroundf(v[k]));}
                    if(nt<=prev)return 0;wr16(o,(unsigned)pos,(uint16_t)(flag|nt));prev=nt;pos+=8;n++;
                }
                unsigned t=(unsigned)lroundf(troll_wind_map((float)kt,&plan));
                if(n&&t<=prev)t=prev+1; /* claves distintas y ordenadas */
                if(t>32767||pos+8>cap)return 0;prev=t;
                memcpy(o+pos,r+at,8);wr16(o,(unsigned)pos,(uint16_t)((packed&0x8000u)|t));pos+=8;n++;
            }
            if(n>255)return 0;o[head+c]=(unsigned char)n;from+=8*count;
        }
    }
    unsigned ev=(unsigned)pos;if(pos+4*nb+28*nm>cap)return 0;
    for(unsigned b=0;b<nb;b++){wr32(o,(unsigned)pos,offsets[b]);pos+=4;}
    unsigned marks=(unsigned)pos;
    for(unsigned j=0;j<nm;j++){memcpy(o+pos,r+mt+4*j,4);wr16(o,(unsigned)pos,(uint16_t)lroundf(troll_wind_map((float)rd16(r,mt+4*j),&plan)));pos+=4;}
    unsigned desc=(unsigned)pos;memcpy(o+pos,r+mc,24*nm);pos+=24*nm;
    wr32(o,8,dur-open+w);wr32(o,0x14,(unsigned)pos);wr32(o,0x18,0);wr32(o,0x1c,ev);wr32(o,0x20,ev);
    wr32(o,0x24,0xffff);wr32(o,0x28,marks);wr32(o,0x2c,0);wr32(o,0x30,nm);wr32(o,0x34,marks);wr32(o,0x38,desc);
    if(open_out)*open_out=open;if(window_out)*window_out=w;
    return pos;
}
#endif
