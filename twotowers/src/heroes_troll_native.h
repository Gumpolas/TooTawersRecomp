/* Combate del troll: las ordenes de impacto son registros de 24 bytes,
 * uno por marca {tiempo,hueso}; NO son una lista ordenada de intervalos.
 * sub_00027E50 avanza 0x18 por marca. 04 abre/cierra el arma; el resto
 * puede controlar efectos o el guion NPC y no se trasplanta al jugador. */
#ifndef TT_HEROES_TROLL_NATIVE_H
#define TT_HEROES_TROLL_NATIVE_H
/* Solo acciones vacias del heroe: los pares 41/42, 43/44, 45/46 y
 * las salidas 60/194 no abren arma ni emiten eventos. No convertir
 * la salida de un golpe ya completo en otro ataque sin dano. */
static int troll_recovery(int g,unsigned slot,const unsigned char *r,size_t len){
    return g==SK_TROLL&&len>=64&&(slot==42||slot==44||slot==46||slot==60||slot==194)
        &&rd32(r,0x30)==0&&rd32(r,0x18)==0;
}
static int troll_damage_pair(const unsigned char *r,size_t len,unsigned index[2]){
    if(len<64)return 0;
    unsigned n=rd32(r,0x30),t=rd32(r,0x34),c=rd32(r,0x38),found=0;
    if(n<2||n>64||!t||!c||t>len||c>len||n>(len-t)/4||n>(len-c)/24)return 0;
    for(unsigned j=0;j<n;j++)if(r[c+24*j]==4){
        if(found==2)return 0;index[found++]=j;
    }
    if(found!=2)return 0;
    unsigned a=index[0],b=index[1];
    return rd32(r,c+24*a)==0x204u&&rd32(r,c+24*b)==0x1004u
        &&rd16(r,t+4*a)<rd16(r,t+4*b)&&rd16(r,t+4*b)<=rd32(r,8);
}
static int troll_native_attack(int g,int cls,const unsigned char *a,size_t al,
 const unsigned char *r,size_t rl){
    unsigned ha[2],na[2];
    return g==SK_TROLL&&(cls==AC_SPEED||cls==AC_FIERCE)
        &&troll_damage_pair(a,al,ha)&&troll_damage_pair(r,rl,na);
}
static int troll_native_damage(unsigned char *o,size_t ol,const unsigned char *r,size_t rl){
    unsigned hi[2],ni[2];if(!troll_damage_pair(o,ol,hi)||!troll_damage_pair(r,rl,ni))return 0;
    unsigned ht=rd32(o,0x34),hc=rd32(o,0x38),nt=rd32(r,0x34),nc=rd32(r,0x38);
    for(int k=0;k<2;k++){
        memcpy(o+ht+4*hi[k],r+nt+4*ni[k],4);
        memcpy(o+hc+24*hi[k],r+nc+24*ni[k],24);
    }
    return 1;
}
#endif
