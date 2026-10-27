/* Escalado de datos privados del jugador: plan validado antes de reubicar.
 * Nunca cambia tiempos, eventos, indices, rotaciones ni desplazamiento XY raiz.
 * Los valores se calculan desde la copia original para evitar deriva al alternar.
 */
#ifndef TT_HEROES_SIZE_H
#define TT_HEROES_SIZE_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
typedef struct { uint32_t offset; uint8_t kind; float value; } TTSizePoint;
typedef struct { TTSizePoint *points; size_t count,capacity; } TTSizePlan;
/* Ambos sticks pulsados y cruceta: arriba crece, abajo reduce. */
static int ts_size_chord(uint16_t b){return (b&0xc0)==0xc0 && (b&3)==1?1:(b&0xc0)==0xc0 && (b&3)==2?-1:0;}
static uint32_t ts_u32(const unsigned char *d,size_t o){uint32_t v;memcpy(&v,d+o,4);return v;}
static uint16_t ts_u16(const unsigned char *d,size_t o){uint16_t v;memcpy(&v,d+o,2);return v;}
static void ts_free(TTSizePlan *p){free(p->points);memset(p,0,sizeof *p);}
static int ts_add(TTSizePlan *p,const unsigned char *d,size_t len,size_t off,int kind){
    if(off+(kind==4?4:2)>len || off>UINT32_MAX)return 0;
    float value;if(kind==4){memcpy(&value,d+off,4);if(!isfinite(value)||fabsf(value)>100000)return 0;}
    else value=(float)(int16_t)ts_u16(d,off);
    if(p->count==p->capacity){size_t cap=p->capacity?p->capacity*2:4096;if(cap>2000000)return 0;
        void *q=realloc(p->points,cap*sizeof(TTSizePoint));if(!q)return 0;p->points=q;p->capacity=cap;}
    p->points[p->count++]=(TTSizePoint){(uint32_t)off,(uint8_t)kind,value};return 1;
}
static int ts_mesh(TTSizePlan *p,const unsigned char *d,size_t len,size_t base,size_t end){
    if(end>len||base+0x54>end||memcmp(d+base," XBG",4))return 0;
    uint32_t count=ts_u32(d,base+0x2c);size_t part=base+0x30;
    if(count>64)return 0;
    for(uint32_t i=0;i<count;i++){
        if(part+0x24>end)return 0;
        /* El desplazamiento se interpreta desde XBG, igual que figure_render;
         * las submallas comparten el stream, no se suma la cabecera de parte. */
        uint32_t n=ts_u32(d,part),pal=ts_u32(d,part+0x20);size_t vertices=base+ts_u32(d,part+8)+0x34;
        if(n>100000||vertices+(size_t)n*48>end)return 0;
        for(uint32_t v=0;v<n;v++)for(int c=0;c<3;c++)if(!ts_add(p,d,len,vertices+48*v+4*c,4))return 0;
        part=(part+0x24+2*pal+3)&~(size_t)3;
    }return 1;
}
static int ts_aniz(TTSizePlan *p,const unsigned char *d,size_t len,size_t base,size_t end){
    if(end>len||base+0x58>end||memcmp(d+base,"aniz",4)||memcmp(d+base+0x40,"hier",4))return 0;
    uint32_t n=ts_u32(d,base+0x48);size_t matrices=base+0x40+ts_u32(d,base+0x4c);
    if(!n||n>128||matrices+128*n>end)return 0;
    for(uint32_t b=0;b<n;b++)for(int m=0;m<2;m++)for(int c=0;c<3;c++)
        if(!ts_add(p,d,len,matrices+128*b+64*m+48+4*c,4))return 0;
    uint32_t ns=ts_u16(d,base+6);size_t table=base+ts_u32(d,base+0x10);
    if(ns>1024||table+4*ns>end)return 0;
    for(uint32_t s=0;s<ns;s++){
        uint32_t rel=ts_u32(d,table+4*s);if(!rel)continue;size_t a=base+rel;
        if(a+64>end||memcmp(d+a,"anim",4))return 0;
        size_t ae=a+ts_u32(d,a+20),bt=a+ts_u32(d,a+32);if(ae>end||bt+4*n>ae)return 0;
        for(uint32_t b=0;b<n;b++){
            uint32_t off=ts_u32(d,bt+4*b);if(!off)continue;size_t track=a+off;
            if(track+4>ae)return 0;
            uint32_t nr=d[track],np=d[track+1];size_t pos=track+4+8*nr;
            if(pos+8*np>ae)return 0;
            for(uint32_t k=0;k<np;k++)for(int c=(b<=1?2:0);c<3;c++)
                if(!ts_add(p,d,len,pos+8*k+2+2*c,2))return 0;
        }
    }return 1;
}
static int ts_cmp(const void *a,const void *b){uint32_t x=((const TTSizePoint*)a)->offset,y=((const TTSizePoint*)b)->offset;return x<y?-1:x>y;}
static int ts_finish(TTSizePlan *p){
    qsort(p->points,p->count,sizeof *p->points,ts_cmp);size_t used=0;
    for(size_t i=0;i<p->count;i++){
        if(used&&p->points[used-1].offset==p->points[i].offset){if(p->points[used-1].kind!=p->points[i].kind)return 0;continue;}
        if(used&&p->points[used-1].offset+p->points[used-1].kind>p->points[i].offset)return 0;
        p->points[used++]=p->points[i];
    }p->count=used;return used>0;
}
static int ts_apply(const TTSizePlan *p,unsigned char *d,size_t len,float previous,float scale){
    if(!isfinite(scale)||scale<0.5f||scale>1.0f)return 0;
    /* Transaccion: comprobar todos los lugares antes de escribir cualquiera. */
    for(size_t i=0;i<p->count;i++){
        const TTSizePoint *v=p->points+i;if(v->offset+v->kind>len)return 0;
        if(v->kind==4){float expected=v->value*previous;uint32_t want,got=ts_u32(d,v->offset);memcpy(&want,&expected,4);if(want!=got)return 0;}
        else {int16_t expected=(int16_t)lroundf(v->value*previous);if(ts_u16(d,v->offset)!=(uint16_t)expected)return 0;}
    }
    for(size_t i=0;i<p->count;i++){
        const TTSizePoint *v=p->points+i;
        if(v->kind==4){float x=v->value*scale;memcpy(d+v->offset,&x,4);}
        else {int16_t x=(int16_t)lroundf(v->value*scale);memcpy(d+v->offset,&x,2);}
    }return 1;
}
#endif
