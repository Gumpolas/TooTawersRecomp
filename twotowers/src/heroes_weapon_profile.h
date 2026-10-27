/* Perfil del arma en la copia privada del nivel. DBDB Xbox, tipo 920.
 * Validacion completa antes de escribir; NPC y archivos originales intactos. */
#ifndef TT_HEROES_WEAPON_PROFILE_H
#define TT_HEROES_WEAPON_PROFILE_H
/* Localizar tres campos sin escribir; validar hasta el final antes de
 * permitir una copia. Los duplicados o DB truncadas fallan en cerrado. */
static int db_weapon_fields(const unsigned char *d,size_t dl,unsigned id,const char *const *names,int nn,unsigned *where){
    if(dl<28||memcmp(d,"DBDB",4)||nn<1||nn>8)return 0;
    unsigned nr=rd32(d,12),p=rd32(d,16),pool=rd32(d,24),matches=0;
    if(nr>4096||pool>dl||p<28||p>pool)return 0;
    unsigned hashes[8];memset(where,0,nn*sizeof *where);
    for(int k=0;k<nn;k++){unsigned h=0;for(const unsigned char *s=(const unsigned char*)names[k];*s;s++)h+=(h<<(*s&7))+*s;hashes[k]=h;}
    for(unsigned r=0;r<nr;r++){
        if(p>pool||pool-p<4)return 0;unsigned n=rd32(d,p);if(n>128||n>(pool-p-4)/8)return 0;
        int selected=0;for(unsigned j=0;j<n;j++)if(rd32(d,p+4+8*j)==0x465D5F9Fu&&rd32(d,p+8+8*j)==id)selected=1;
        if(selected){if(matches++)return 0;for(unsigned j=0;j<n;j++)for(int k=0;k<nn;k++)if(rd32(d,p+4+8*j)==hashes[k]){if(where[k])return 0;where[k]=p+8+8*j;}}
        p+=4+8*n;
    }
    if(p!=pool||matches!=1)return 0;
    for(int k=0;k<nn;k++)if(!where[k])return 0;
    return 1;
}
static int troll_weapon_fields(const unsigned char *d,size_t dl,unsigned id,unsigned where[3]){
    static const char *const names[]={"WEAPON_MIN_EXTENTS","WEAPON_MAX_EXTENTS","WEAPON_NUM_SAMPLES"};
    return db_weapon_fields(d,dl,id,names,3,where);
}
/* (132) Dano del arma del jugador = mitad de la maza del troll NPC (200 ->
 * 100); tt_troll_hit lo dobla en cada golpe del troll: el del NPC. Si el
 * gancho no actuara, 100 sigue siendo mas del triple de la espada (30).
 * Las mejoras del jugador escalan sobre esta base. */
static int troll_weapon_damage_patch(unsigned char *o,const unsigned char *t,size_t tl,size_t len){
    static const char *const names[]={"WEAPON_DAMAGE"};
    unsigned a,e,ty,where,sw;
    if(!res_find(t,tl,1,920,&a,&e,&ty)||e<a||e>len||!db_weapon_fields(t+a,e-a,1,names,1,&where))return 0;
    char path[600];size_t sl;unsigned char *s;
    if(!host_path(s_skins[9].src,path,sizeof path)||(s=read_all(path,&sl))==NULL)return 0;
    unsigned sa,se,st,npc=0;int valid=res_find(s,sl,1,920,&sa,&se,&st)&&se>=sa&&se<=sl
        &&db_weapon_fields(s+sa,se-sa,6,names,1,&sw);
    if(valid)npc=rd32(s+sa,sw);free(s);
    unsigned own=rd32(t+a,where),value=npc/2;
    if(!valid||npc<2||npc>10000||value<=own)return 0;
    wr32(o,a+where,value);
    tt_dbg("[TROLL-NPC] dano arma jugador %u -> %u (maza NPC %u; x2 por golpe)\n",own,value,npc);
    return 1;
}
static int troll_weapon_db_patch(unsigned char *o,const unsigned char *t,size_t tl,size_t len){
    unsigned a,e,ty,where[3],values[3];
    if(!res_find(t,tl,1,920,&a,&e,&ty)||e<a||e>len||!troll_weapon_fields(t+a,e-a,1,where))return 0;
    /* Leer la maza 6 del troll original. Solo se transfieren la geometria
     * y el numero de muestras: el dano, la progresion y la faccion son
     * los del jugador. Nunca se modifica el registro del troll enemigo. */
    char path[600];size_t sl;unsigned char *s;
    if(!host_path(s_skins[9].src,path,sizeof path)||(s=read_all(path,&sl))==NULL)return 0;
    unsigned sa,se,st,sw[3];int valid=res_find(s,sl,1,920,&sa,&se,&st)&&se>=sa&&se<=sl
        &&troll_weapon_fields(s+sa,se-sa,6,sw);
    if(valid)for(int k=0;k<3;k++)values[k]=rd32(s+sa,sw[k]);free(s);
    if(!valid)return 0;
    float lo,hi;memcpy(&lo,values,4);memcpy(&hi,values+1,4);
    if(!isfinite(lo)||!isfinite(hi)||hi<=lo||values[2]<2||values[2]>64)return 0;
    for(int k=0;k<3;k++)wr32(o,a+where[k],values[k]);
    tt_dbg("[TROLL-NPC] maza 6 -> arma jugador: extents %.3f..%.3f, %u muestras; dano/progresion conservados\n",lo,hi,values[2]);
    return 3;
}
static int weapon_db_patch(unsigned char *o,const unsigned char *t,size_t tl,size_t len,int sk){
    if(sk!=2)return 0; /* Frodo: Dardo, no la espada invisible de Aragorn. */
    uint32_t a,e,ty;if(!res_find(t,tl,1,920,&a,&e,&ty)||e<a||e>len||e-a<32||memcmp(t+a,"DBDB",4))return 0;
    const unsigned char *d=t+a;size_t dl=e-a;unsigned nr=rd32(d,12),p=rd32(d,16),pool=rd32(d,24);
    if(nr>4096||pool>dl||p<28||p>pool)return 0;
    /* Los hashes de estela se calculan con el algoritmo del propio juego. */
    const char *names[]={"WEAPON_DAMAGE","WEAPON_MIN_EXTENTS","WEAPON_MAX_EXTENTS","WEAPON_NUM_SAMPLES",
                        "WEAPON_TRAIL_DURATION","WEAPON_TRAIL_MIN_EXTENTS","WEAPON_TRAIL_MAX_EXTENTS"};
    uint32_t hashes[7],where[7]={0};unsigned matches=0;
    for(int k=0;k<7;k++){uint32_t h=0;for(const unsigned char *s=(const unsigned char*)names[k];*s;s++)h+=(h<<(*s&7))+*s;hashes[k]=h;}
    for(unsigned r=0;r<nr;r++){
        if(p>pool||pool-p<4)return 0;unsigned n=rd32(d,p);if(n>128||n>(pool-p-4)/8)return 0;
        int player=0;for(unsigned j=0;j<n;j++)if(rd32(d,p+4+8*j)==0x465D5F9Fu&&rd32(d,p+8+8*j)==1)player=1;
        if(player){if(matches++)return 0;for(unsigned j=0;j<n;j++)for(int k=0;k<7;k++)if(rd32(d,p+4+8*j)==hashes[k]){if(where[k])return 0;where[k]=a+p+8+8*j;}}
        p+=4+8*n;
    }
    if(p!=pool||matches!=1)return 0;for(int k=0;k<7;k++)if(!where[k])return 0;
    uint32_t values[7]={24,0,0,5,0,0,0};float fv[7]={0,-.44f,1.85f,0,.16f,.10f,1.65f};
    for(int k=0;k<7;k++)if(k!=0&&k!=3)memcpy(values+k,fv+k,4);
    for(int k=0;k<7;k++)wr32(o,where[k],values[k]);
    return 7;
}
#endif
