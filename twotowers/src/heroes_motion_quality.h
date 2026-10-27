/* Calidad de transiciones sin modificar eventos, ventanas ni raiz.
 * El tramo de impacto permanece identico; solo entradas/salidas de melee.
 */
#ifndef TT_HEROES_MOTION_QUALITY_H
#define TT_HEROES_MOTION_QUALITY_H
static Quat mq_key(const unsigned char *k){
    return rt_qkey(k);
}
static Quat mq_mix(Quat a,Quat b,float t){
    if(a.w*b.w+a.x*b.x+a.y*b.y+a.z*b.z<0){b.w=-b.w;b.x=-b.x;b.y=-b.y;b.z=-b.z;}
    Quat q={a.w+(b.w-a.w)*t,a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};return qnormq(q);
}
static unsigned mq_ease(unsigned char *r,size_t len,const unsigned char *G,size_t glen,int cls){
    if(cls!=AC_SPEED &&cls!=AC_FIERCE &&cls!=AC_PARRY)return 0;
    if(len<64||glen<0x58||rd16(G,6)<=34)return 0;
    unsigned tab=rd32(G,0x10),stance=tab+4*34+4<=glen?rd32(G,tab+4*34):0;
    if(!stance||stance+64>glen)return 0;
    unsigned sb=stance+rd32(G,stance+0x20),nb=rd32(G,0x48),bt=rd32(r,0x20),dur=rd32(r,8);
    if(!dur||nb>128||sb+4*nb>glen||bt+4*nb>len)return 0;
    float first=(float)dur,last=0;
    unsigned n=rd32(r,0x30),tail=rd32(r,0x34);
    if(tail+4*n>len)return 0;
    for(unsigned i=0;i<n;i++){float t=rd16(r,tail+4*i);if(t<first)first=t;if(t>last)last=t;}
    unsigned ev=rd32(r,0x1c),nev=rd32(r,0x18);
    if(ev+4*nev>len)return 0;
    for(unsigned i=0;i<nev;i++){
        unsigned e=ev+rd32(r,ev+4*i);if(e+16>len)return 0;
        if(rd32(r,e+12)==5){float t=rd32(r,e);if(t<first)first=t;if(t>last)last=t;}
    }
    float edge=fminf(75.0f,dur*.12f),in=fminf(edge,fmaxf(0,first-20)),out=fminf(edge,fmaxf(0,dur-last-20));
    unsigned changed=0;
    for(unsigned b=2;b<nb;b++){
        unsigned bo=rd32(r,bt+4*b),so=rd32(G,sb+4*b);
        if(!bo||bo+4>len||!so||stance+so+12>glen||!G[stance+so])continue;
        unsigned nr=r[bo];if(nr<2||bo+4+8*nr>len)continue;
        Quat rest=mq_key(G+stance+so+4);
        for(unsigned k=0;k<nr;k++){
            unsigned at=bo+4+8*k;float t=rt_key_time(r+at),w=1;
            /* No tocar una clave cuyo segmento interpolado alcance un impacto. */
            if(in>0&&t<in && k+1<nr && rt_key_time(r+at+8)<=first-20)w=t/in;
            if(out>0&&t>dur-out && k>0 && rt_key_time(r+at-8)>=last+20)w=fminf(w,(dur-t)/out);
            if(w>=1)continue;if(w<0)w=0;w=w*w*(3-2*w);
            Quat q=mq_mix(rest,mq_key(r+at),w);if(q.w<0){q.w=-q.w;q.x=-q.x;q.y=-q.y;q.z=-q.z;}
            wr16(r,at+2,(uint16_t)rt_qpack(q.x));
            wr16(r,at+4,(uint16_t)rt_qpack(q.y));
            wr16(r,at+6,(uint16_t)rt_qpack(q.z));changed++;
        }
    }return changed;
}
/* Ritmo por perfil de ataques nativos. El juego y sus FPS no se alteran. */
static uint32_t mq_attack_limit(int g,uint32_t duration){
    static const unsigned pct[N_EXTRA]={115,130,115,120,115,150,135,175,140,130,115,115,115,115};   /* (1.9.9: the troll, the Uruk captain, Saruman, then Versus' four) */
    static const unsigned extra[N_EXTRA]={50,65,50,55,50,85,70,100,75,65,50,50,50,50};
    return g>=0 &&g<N_EXTRA?duration*pct[g]/100+extra[g]:duration*115/100+50;
}
#endif
