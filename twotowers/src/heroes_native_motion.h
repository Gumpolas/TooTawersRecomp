/* Diagnostico/correccion de sincronizacion de movimientos NPC.
 * No cambia tablas de combos ni las cargas de los eventos del heroe. */
#ifndef TT_HEROES_NATIVE_MOTION_H
#define TT_HEROES_NATIVE_MOTION_H
/* Extraer un golpe de una secuencia NPC, conservando sus poses. Los
 * extremos se muestrean con el mismo interpolador que usa el compositor;
 * no se generan rotaciones a partir del esqueleto de otro personaje. */
static size_t npc_clip_range(const unsigned char *S,size_t len,const unsigned char *r,size_t rl,
 unsigned char *o,size_t cap,unsigned begin,unsigned end,unsigned first,unsigned last,unsigned sample_step){
    if(rl<64||len<0x4c||cap<64||begin>=end||end>rd32(r,8))return 0;
    unsigned nb=rd32(S,0x48),bt=rd32(r,0x20),wt=rd32(r,0x34),nw=rd32(r,0x30);
    if(nb>128||bt>rl||nb>(rl-bt)/4||first>last||last>nw||wt>rl||last>(rl-wt)/4)return 0;
    memcpy(o,r,64);size_t pos=64;unsigned offsets[128];memset(offsets,0,sizeof offsets);
    for(unsigned b=0;b<nb;b++){
        unsigned bo=rd32(r,bt+4*b);if(!bo)continue;
        if(bo+4>rl||pos+4>cap)return 0;
        unsigned total=0;for(int c=0;c<4;c++)total+=r[bo+c];if(bo+4+8u*total>rl)return 0;
        offsets[b]=(unsigned)pos;size_t head=pos;memset(o+pos,0,4);pos+=4;unsigned from=bo+4;
        for(int c=0;c<4;c++){
            unsigned count=r[bo+c],newcount=0;if(!count)continue;
            if(c==0){
                /* Mantener las claves originales e intercalar muestras
                 * cercanas a los cortes: NLERP no es asociativa. */
                unsigned times[256],nt=0;times[nt++]=begin;times[nt++]=end;
                for(unsigned j=0;j<count;j++){unsigned t=rt_key_time(r+from+8*j);if(t>begin&&t<end){if(nt==255)return 0;times[nt++]=t;}}
                for(unsigned t=begin+sample_step;sample_step&&t<end;t+=sample_step){if(nt==255)return 0;times[nt++]=t;}
                for(unsigned j=1;j<nt;j++){unsigned v=times[j],k=j;while(k&&times[k-1]>v){times[k]=times[k-1];k--;}times[k]=v;}
                unsigned distinct=0;for(unsigned j=0;j<nt;j++)if(!j||times[j]!=times[j-1])times[distinct++]=times[j];nt=distinct;
                if(pos+8*nt>cap)return 0;
                for(unsigned j=0;j<nt;j++){
                    unsigned t=times[j],found=count;for(unsigned k=0;k<count;k++)if(rt_key_time(r+from+8*k)==t){found=k;break;}
                    unsigned flag=0;
                    if(found<count){memcpy(o+pos,r+from+8*found,8);flag=rd16(r,from+8*found)&0x8000u;}
                    else{Quat q;if(!rt_qsample(r+from,(int)count,(float)t,&q))return 0;if(q.w<0){flag=0x8000u;q.x=-q.x;q.y=-q.y;q.z=-q.z;}
                        wr16(o,(unsigned)pos+2,(uint16_t)rt_qpack(q.x));wr16(o,(unsigned)pos+4,(uint16_t)rt_qpack(q.y));wr16(o,(unsigned)pos+6,(uint16_t)rt_qpack(q.z));}
                    wr16(o,(unsigned)pos,(uint16_t)(flag|(t-begin)));pos+=8;
                }
                o[head]= (unsigned char)nt;from+=8*count;continue;
            }
            unsigned inside=0;for(unsigned j=0;j<count;j++){unsigned t=rt_key_time(r+from+8*j);if(t>begin&&t<end)inside++;}
            if(inside+2>255||pos+8*(inside+2)>cap)return 0;
            for(int edge=0;edge<2;edge++){
                float v[3];unsigned t=edge?end:begin;if(!rt_sample(r+from,(int)count,(float)t,v))return 0;
                wr16(o,(unsigned)pos,(uint16_t)(t-begin));for(int k=0;k<3;k++)wr16(o,(unsigned)pos+2+2*k,(uint16_t)(int16_t)lroundf(v[k]));pos+=8;newcount++;
                if(!edge)for(unsigned j=0;j<count;j++){
                    unsigned kt=rt_key_time(r+from+8*j);if(kt<=begin||kt>=end)continue;
                    memcpy(o+pos,r+from+8*j,8);wr16(o,(unsigned)pos,(uint16_t)((rd16(r,from+8*j)&0x8000u)|(kt-begin)));pos+=8;newcount++;
                }
            }
            o[head+c]=(unsigned char)newcount;from+=8*count;
        }
    }
    unsigned ev=(unsigned)pos;if(pos+4*nb+4*(last-first)>cap)return 0;
    for(unsigned b=0;b<nb;b++){wr32(o,(unsigned)pos,offsets[b]);pos+=4;}
    unsigned tail=(unsigned)pos;for(unsigned j=first;j<last;j++){memcpy(o+pos,r+wt+4*j,4);wr16(o,(unsigned)pos,(uint16_t)(rd16(r,wt+4*j)-begin));pos+=4;}
    wr32(o,8,end-begin);wr32(o,0x14,(unsigned)pos);wr32(o,0x18,0);wr32(o,0x1c,ev);wr32(o,0x20,ev);
    wr32(o,0x24,0xffff);wr32(o,0x28,tail);wr32(o,0x2c,0);wr32(o,0x30,last-first);wr32(o,0x34,tail);wr32(o,0x38,0);
    return pos;
}
static size_t npc_attack_slice(int g,unsigned src,unsigned serial,const unsigned char *S,size_t len,
 const unsigned char *r,size_t rl,unsigned char *o,size_t cap,unsigned *cut_start){
    const char *name=s_pools[g].name;
    if(strcmp(name,"frodo")&&strcmp(name,"uruk")&&strcmp(name,"urukcap")&&strcmp(name,"troll")&&strcmp(name,"lurtz")&&strcmp(name,"boromir")&&strcmp(name,"elrond")&&strcmp(name,"elvenwarrior")&&strcmp(name,"elvenarcher")&&strcmp(name,"rohan"))return 0;
    int allowed=src==41||src==43;
    if(!strcmp(name,"uruk")||!strcmp(name,"lurtz"))allowed=allowed||src==69;
    if(!strcmp(name,"troll")||!strcmp(name,"lurtz"))allowed=allowed||src==45;
    if(!strcmp(name,"troll"))allowed=allowed||src==113;
    if(!strcmp(name,"urukcap"))allowed=allowed||src==172;
    if(!strcmp(name,"lurtz"))allowed=allowed||src==42;
    if(!strcmp(name,"elrond")||!strcmp(name,"elvenwarrior")||!strcmp(name,"elvenarcher"))allowed=src==66;
    if(!allowed)return 0; /* No recortar levantadas, muertes o interacciones. */
    if(rl<64||cap<64||len<0x4c)return 0;
    unsigned nb=rd32(S,0x48),dur=rd32(r,8),bt=rd32(r,0x20),nw=rd32(r,0x30),wt=rd32(r,0x34);
    if(!dur||dur>32767||nb>128||bt>rl||nb>(rl-bt)/4||nw<2||nw>64||wt>rl||nw>(rl-wt)/4)return 0;
    unsigned first=0,last=nw;
    /* Dos golpes de Frodo/capitan y cuatro del berserker: uno por eslabon
     * del combo. No comprimir la secuencia entera en una sola pulsacion. */
    int chain=(src==41&&(!strcmp(name,"frodo")||!strcmp(name,"urukcap")||!strcmp(name,"uruk")||!strcmp(name,"lurtz")))||src==66||(!strcmp(name,"rohan")&&src==43);
    if(chain && !(nw&1)){
        int valid=1;for(unsigned j=0;j<nw;j+=2)if(rd16(r,wt+4*j)>=rd16(r,wt+4*(j+1)) || (j&&rd16(r,wt+4*j)<rd16(r,wt+4*(j-1))))valid=0;
        if(valid){first=2*(serial%(nw/2));last=first+2;}
    }
    unsigned lo=dur,hi=0;for(unsigned j=first;j<last;j++){unsigned t=rd16(r,wt+4*j);if(t>dur)return 0;if(t<lo)lo=t;if(t>hi)hi=t;}
    if(hi<=lo)return 0;
    unsigned begin=lo>180?lo-180:0,end=hi+200<dur?hi+200:dur;
    /* No incluir el impacto vecino al separar una cadena de NPC. */
    if(first){unsigned prev=rd16(r,wt+4*(first-1));if(begin<=prev)begin=(prev+lo)/2;}
    if(last<nw){unsigned next=rd16(r,wt+4*last);if(end>=next)end=(hi+next)/2;}
    /* Lurtz cierra el segundo golpe en el ultimo ms del clip. La marca
     * final inclusiva es valida y no exige inventar una recuperacion. */
    if(begin>=lo||end<hi||begin>=end)return 0;
    if(cut_start)*cut_start=begin;
    return npc_clip_range(S,len,r,rl,o,cap,begin,end,first,last,8);
}
static int npc_weapon_peak(const unsigned char *S,size_t len,const unsigned char *r,size_t rl,int hand,uint32_t dur,float *speed){
    if(rl<0x38||len<0x4c){if(speed)*speed=0;return -1;}
    unsigned count=rd32(r,0x30),tab=rd32(r,0x34),nb=rd32(S,0x48);
    float lo=(float)dur,hi=0;int found=0;
    if(speed)*speed=0;
    if(hand<0||(unsigned)hand>=nb||nb>128||tab>rl||count>(rl-tab)/4)return -1;
    /* La mano del arma, no la mano libre mas rapida. Las marcas nativas
     * permiten excluir gestos de preparacion/recuperacion del NPC. */
    for(unsigned j=0;j<count;j++)if(rd16(r,tab+4*j+2)==(unsigned)hand){
        float t=rd16(r,tab+4*j);if(t<=dur){if(t<lo)lo=t;if(t>hi)hi=t;found=1;}
    }
    if(!found)return swing_peak(S,len,r,rl,hand,dur,speed);
    if(hi<=lo)return lo>0&&lo<dur?(int)lo:-1;
    float P[128][3],prev[3]={0},best=-1;int peak=-1,have=0;
    for(float t=lo;t<=hi+0.01f;t+=16.0f){
        if(rt_pose(S,len,r,rl,t,P)<=hand)return -1;
        float q[3];for(int c=0;c<3;c++)q[c]=P[hand][c]-P[0][c];
        if(have){float d=0;for(int c=0;c<3;c++){float v=q[c]-prev[c];d+=v*v;}if(d>best){best=d;peak=(int)(t-8.0f);}}
        memcpy(prev,q,sizeof prev);have=1;
    }
    if(speed)*speed=best;return peak;
}
#endif
