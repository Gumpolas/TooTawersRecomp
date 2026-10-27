/* (135) Registro de combate del troll del jugador: troll_combate.csv junto
 * al ejecutable (el de la partida anterior queda en
 * troll_combate_anterior.csv). Solo lee memoria del juego: no cambia dano,
 * candidatos ni resultados. TT_NOCOMBATLOG=1 lo desactiva.
 *
 * Filas:
 *  GOLPEO  cada fotograma de golpe del troll (rutina BE0D0): modo, dano,
 *          tipo y victimas antes/despues
 *  CERCA   enemigos conocidos a menos de 12 del troll en ese fotograma
 *  SONDA   cada enemigo que el juego prueba contra la maza: sondaN (acierto
 *          preciso), respaldo (acierto del alcance ampliado) o fallo
 *  GOLPE   golpe aplicado (dano y tipo finales)
 *  RECIBE  un enemigo golpea al troll (asi se conocen los que estan pegados)
 * Distancias horizontales; dist_maza = a la linea mano-cabeza de la maza.
 *  TIRO    (137) una roca del troll lanzada: salida (ax..), objetivo (mx..),
 *          fijado/libre, mano original y direccion
 *  ROCA    cada fotograma de vuelo: enemigo conocido mas cercano (ax..),
 *          roca (mx..), distancia 3D (dist_maza) y altura roca-enemigo (dz)
 *  IMPACTO dano que no es de la maza (roca, otro arma) cerca de un tiro */
#ifndef TT_HEROES_TROLL_LOG_H
#define TT_HEROES_TROLL_LOG_H
#define CLOG_MAX_BYTES (40L*1024*1024)
#define CLOG_NEAR 12.0f
#define CLOG_KEEP_MS 8000u
static FILE *s_clog;static int s_clog_off=-1;static long s_clog_bytes;
static struct {uint32_t a;DWORD seen;} s_clog_known[48];
static int clog_ptr(uint32_t a){return a>=0x80000000u&&a<0x83FFF000u;}
static int clog_ready(void){
    if(s_clog_off<0){
        s_clog_off=getenv("TT_NOCOMBATLOG")?1:0;
        if(!s_clog_off){
            char p[MAX_PATH+40],q[MAX_PATH+40];paths();
            snprintf(p,sizeof p,"%s/troll_combate.csv",s_dir);snprintf(q,sizeof q,"%s/troll_combate_anterior.csv",s_dir);
            remove(q);rename(p,q);
            if(!(s_clog=fopen(p,"w")))s_clog_off=1;
            else{s_clog_bytes=fprintf(s_clog,"ms,evento,ataque,actor,tipo,ax,ay,az,tx,ty,tz,dist_troll,mx,my,mz,dist_maza,dz_maza,resultado,detalle\n");
                 tt_dbg("[TROLL-LOG] registro de combate en %s\n",p);}
        }
    }
    return !s_clog_off&&s_clog&&s_clog_bytes<CLOG_MAX_BYTES;
}
static int clog_active(void){return s_guest==SK_TROLL&&G8(HERO_BYTE)==4&&!G8(0x43BABCu)&&clog_ptr(G32(0x43B15Cu));}
static void clog_seen(uint32_t a){
    if(!clog_ptr(a)||a==G32(0x43B15Cu))return;
    DWORD now=GetTickCount();int free_i=-1,old_i=0;
    for(int i=0;i<48;i++){
        if(s_clog_known[i].a==a){s_clog_known[i].seen=now;return;}
        if(!s_clog_known[i].a||now-s_clog_known[i].seen>CLOG_KEEP_MS){if(free_i<0)free_i=i;}
        else if(s_clog_known[i].seen<s_clog_known[old_i].seen)old_i=i;
    }
    int i=free_i>=0?free_i:old_i;s_clog_known[i].a=a;s_clog_known[i].seen=now;
}
/* la maza: linea de la mano (y 0) a la cabeza (y 5,2) en la matriz actual */
static int clog_mace(uint32_t attack,float m[3],float *dist,float *dz,const float at[3]){
    uint32_t w=TT_ATTACK_OK(attack)?G32(attack+0x14):0;
    if(!clog_ptr(w))return 0;
    float best=1e30f;
    for(int j=0;j<=6;j++){
        float y=j<6?(float)j:5.19682591f,p[3];
        for(int c=0;c<3;c++)p[c]=y*GF(w+0x60+16u+4u*c)+GF(w+0x60+48u+4u*c);
        if(j==6)for(int c=0;c<3;c++)m[c]=p[c];
        if(at){float dx=p[0]-at[0],dy=p[1]-at[1],d=dx*dx+dy*dy;if(d<best){best=d;*dist=sqrtf(d);*dz=p[2]-at[2];}}
    }
    return 1;
}
static void clog_row(const char *ev,uint32_t attack,uint32_t actor,const char *res,const char *detail){
    if(!clog_ready())return;
    uint32_t troll=G32(0x43B15Cu);float a[3]={0,0,0},t[3]={0,0,0},m[3]={0,0,0},dm=-1,dz=0,dt=-1;
    for(int c=0;c<3;c++){if(clog_ptr(troll))t[c]=GF(troll+0x90+4u*c);if(clog_ptr(actor))a[c]=GF(actor+0x90+4u*c);}
    int has_mace=clog_mace(attack,m,&dm,&dz,clog_ptr(actor)?a:NULL);
    if(clog_ptr(actor)&&clog_ptr(troll)){float dx=a[0]-t[0],dy=a[1]-t[1];dt=sqrtf(dx*dx+dy*dy);}
    s_clog_bytes+=fprintf(s_clog,"%u,%s,%08X,%08X,%u,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,",(unsigned)GetTickCount(),ev,attack,actor,
        clog_ptr(actor)?G32(actor+0x2F0):0u,a[0],a[1],a[2],t[0],t[1],t[2],dt);
    if(has_mace)s_clog_bytes+=fprintf(s_clog,"%.2f,%.2f,%.2f,%.2f,%.2f,",m[0],m[1],m[2],dm,dz);
    else s_clog_bytes+=fprintf(s_clog,",,,,,");
    s_clog_bytes+=fprintf(s_clog,"%s,%s\n",res?res:"",detail?detail:"");
    if(s_clog_bytes>=CLOG_MAX_BYTES){fprintf(s_clog,"%u,FIN,,,,,,,,,,,,,,,,limite,40 MB\n",(unsigned)GetTickCount());fflush(s_clog);}
}
static int clog_player_attack(uint32_t attack){
    if(!TT_ATTACK_OK(attack))return 0;
    uint32_t w=G32(attack+0x14);
    return G32(attack+8)==G32(0x43B15Cu)&&clog_ptr(w)&&G32(w+0x2F4)==1;
}
/* la rutina de golpe BE0D0, antes y despues (envoltorio en twotowers_manual.c) */
void tt_clog_swing(uint32_t attack,int after){
    static uint32_t before;
    if(!clog_active()||!clog_player_attack(attack))return;
    if(!after){before=G32(attack+0x28);return;}
    if(!clog_ready())return;
    char d[96];snprintf(d,sizeof d,"modo=%u dano=%u tipo=%u victimas=%u>%u",G32(attack+0x24),G32(attack+0x18),G32(attack+0x1C),before,G32(attack+0x28));
    clog_row("GOLPEO",attack,0,"",d);
    uint32_t troll=G32(0x43B15Cu);DWORD now=GetTickCount();
    for(int i=0;i<48;i++){
        uint32_t a=s_clog_known[i].a;if(!a||now-s_clog_known[i].seen>CLOG_KEEP_MS)continue;
        float dx=GF(a+0x90)-GF(troll+0x90),dy=GF(a+0x94)-GF(troll+0x94);
        if(dx*dx+dy*dy<CLOG_NEAR*CLOG_NEAR)clog_row("CERCA",attack,a,"","");
    }
    fflush(s_clog);
}
/* cada consulta de la maza (multisonda): sonda acertada, respaldo o fallo */
void tt_sweep_report(const uint32_t args[6],int lane,int fallback){
    if(!clog_active()||args[0]!=0xBEAB0u||!clog_player_attack(args[1]))return;
    clog_seen(args[2]);
    char r[16];if(lane>=0)snprintf(r,sizeof r,"sonda%d",lane);else snprintf(r,sizeof r,"%s",fallback?"respaldo":"fallo");
    clog_row("SONDA",args[1],args[2],r,"");
}
/* (137) sus rocas: TIRO al lanzarla (salida, objetivo, mano original,
 * direccion), ROCA cada fotograma de vuelo (posicion, enemigo conocido mas
 * cercano y su distancia 3D) e IMPACTO cada dano que no sea de la maza */
static void clog_pos_row(const char *ev,uint32_t obj,uint32_t actor,uint32_t kind,const float a[3],const float m[3],float dt,float dm,float dz,const char *res,const char *detail){
    if(!clog_ready())return;
    uint32_t troll=G32(0x43B15Cu);float t[3]={0,0,0};
    if(clog_ptr(troll))for(int c=0;c<3;c++)t[c]=GF(troll+0x90+4u*c);
    s_clog_bytes+=fprintf(s_clog,"%u,%s,%08X,%08X,%u,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%s,%s\n",(unsigned)GetTickCount(),ev,obj,actor,kind,
        a[0],a[1],a[2],t[0],t[1],t[2],dt,m[0],m[1],m[2],dm,dz,res?res:"",detail?detail:"");
    if(s_clog_bytes>=CLOG_MAX_BYTES){fprintf(s_clog,"%u,FIN,,,,,,,,,,,,,,,,limite,40 MB\n",(unsigned)GetTickCount());fflush(s_clog);}
}
#define CLOG_ROCKS 8
#define CLOG_ROCK_FRAMES 400
static struct {uint32_t o;DWORD t;int n;} s_clog_rock[CLOG_ROCKS];
static struct {int armed,lock;float hand[3],dir[3];} s_clog_arm;
static uint32_t s_clog_rock_cur;static DWORD s_clog_rock_last;static int s_clog_rock_any;
static int clog_rock_slot(uint32_t o){
    if(!o)return -1;
    for(int i=0;i<CLOG_ROCKS;i++)if(s_clog_rock[i].o==o)return i;
    return -1;
}
static float clog_hdist(const float a[3],const float b[3]){float dx=a[0]-b[0],dy=a[1]-b[1];return sqrtf(dx*dx+dy*dy);}
static float clog_dist3(const float a[3],const float b[3]){float dx=a[0]-b[0],dy=a[1]-b[1],dz=a[2]-b[2];return sqrtf(dx*dx+dy*dy+dz*dz);}
/* hook 22, antes del lanzamiento: hand = la salida antes de moverla */
void clog_rock_arm(uint32_t shooter,uint32_t target,uint32_t dir,const float *hand){
    s_clog_arm.armed=0;
    if(!clog_active()||!clog_ptr(shooter))return;
    for(int c=0;c<3;c++){s_clog_arm.hand[c]=hand?hand[c]:GF(shooter+4u*c);s_clog_arm.dir[c]=dir>=0x10000u&&dir<0x83FFFFF0u?GF(dir+4u*c):0;}
    s_clog_arm.lock=target!=0;s_clog_arm.armed=1;
}
/* hook 22, ya lanzada: o = el proyectil */
void clog_rock_launched(uint32_t o){
    if(!s_clog_arm.armed)return;
    s_clog_arm.armed=0;
    if(!clog_active()||!clog_ptr(o))return;
    DWORD now=GetTickCount();int k=0;
    for(int i=0;i<CLOG_ROCKS;i++){
        if(s_clog_rock[i].o==o||!s_clog_rock[i].o||now-s_clog_rock[i].t>5000u){k=i;break;}
        if(s_clog_rock[i].t<s_clog_rock[k].t)k=i;
    }
    s_clog_rock[k].o=o;s_clog_rock[k].t=now;s_clog_rock[k].n=0;s_clog_rock_last=now;s_clog_rock_any=1;
    float st[3],tg[3],t[3]={0,0,0};uint32_t troll=G32(0x43B15Cu);
    for(int c=0;c<3;c++){st[c]=GF(o+0x3E8+4u*c);tg[c]=GF(o+0x3F8+4u*c);if(clog_ptr(troll))t[c]=GF(troll+0x90+4u*c);}
    char d[160];snprintf(d,sizeof d,"mano=%.2f %.2f %.2f dir=%.2f %.2f %.2f t=%.2f",s_clog_arm.hand[0],s_clog_arm.hand[1],s_clog_arm.hand[2],
        s_clog_arm.dir[0],s_clog_arm.dir[1],s_clog_arm.dir[2],GF(o+0x4C4));
    clog_pos_row("TIRO",o,0,G32(o+0x3D0),st,tg,clog_hdist(st,t),clog_hdist(st,tg),tg[2]-st[2],s_clog_arm.lock?"fijado":"libre",d);
    fflush(s_clog);
}
/* hook 23 (actualizacion de un proyectil): r1 es el proyectil en la fase 0 */
void clog_rock_update(int phase,uint32_t r1){
    if(!phase){s_clog_rock_cur=clog_rock_slot(r1)>=0?r1:0;return;}
    uint32_t o=s_clog_rock_cur;s_clog_rock_cur=0;
    int i=clog_rock_slot(o);
    if(i<0)return;
    if(!clog_active()){s_clog_rock[i].o=0;return;}
    float p[3],e[3]={0,0,0},t[3]={0,0,0},tg[3],best=-1;uint32_t near_a=0,troll=G32(0x43B15Cu);DWORD now=GetTickCount();
    for(int c=0;c<3;c++){p[c]=GF(o+0x408+4u*c);tg[c]=GF(o+0x3F8+4u*c);if(clog_ptr(troll))t[c]=GF(troll+0x90+4u*c);}
    for(int k=0;k<48;k++){
        uint32_t a=s_clog_known[k].a;if(!a||now-s_clog_known[k].seen>CLOG_KEEP_MS)continue;
        float q[3]={GF(a+0x90),GF(a+0x94),GF(a+0x98)},d=clog_dist3(p,q);
        if(best<0||d<best){best=d;near_a=a;for(int c=0;c<3;c++)e[c]=q[c];}
    }
    int dead=G8(o+0x3E4)!=0,n=++s_clog_rock[i].n,last=dead||n>=CLOG_ROCK_FRAMES;
    char d[64];snprintf(d,sizeof d,"n=%d objetivo=%.2f",n,clog_dist3(p,tg));
    clog_pos_row("ROCA",o,near_a,G32(o+0x3D0),e,p,near_a?clog_hdist(e,t):-1.f,best,near_a?p[2]-e[2]:0.f,dead?"fin":"vuela",d);
    if(last){s_clog_rock[i].o=0;fflush(s_clog);}
}
/* cualquier dano que no sea de la maza (C7610 sin volver a BECFF) mientras
 * vuelan sus rocas o hasta 4 s despues de lanzar, o cuyo arma/atacante sea una roca */
static void clog_impact(uint32_t sp){
    if(!clog_active())return;
    uint32_t victim=G32(sp+4),weapon=G32(sp+8),owner=G32(sp+12),player=G32(0x43B15Cu);
    uint32_t ro=clog_rock_slot(weapon)>=0?weapon:clog_rock_slot(owner)>=0?owner:0;
    if(!clog_ptr(victim)||(!ro&&owner!=player&&(!s_clog_rock_any||GetTickCount()-s_clog_rock_last>4000u)))return;
    if(victim!=player)clog_seen(victim);
    float a[3],m[3]={0,0,0},t[3]={0,0,0};
    for(int c=0;c<3;c++){a[c]=GF(victim+0x90+4u*c);if(ro)m[c]=GF(ro+0x408+4u*c);t[c]=GF(player+0x90+4u*c);}
    char d[128];snprintf(d,sizeof d,"ret=%08X arma=%08X dueno=%08X dano=%u tipo=%u",G32(sp),weapon,owner,G32(sp+0x30),G32(sp+0x34));
    clog_pos_row("IMPACTO",ro,victim,G32(victim+0x2F0),a,m,clog_hdist(a,t),ro?clog_dist3(a,m):-1.f,ro?m[2]-a[2]:0.f,
        ro?"roca":owner==player?"jugador":"otro",d);
}
/* cada evento de dano melee (C7610) con el troll en juego */
static void clog_event(uint32_t sp,int applied){
    if(!clog_active())return;
    uint32_t victim=G32(sp+4),weapon=G32(sp+8),owner=G32(sp+12),player=G32(0x43B15Cu);
    char d[96];
    if(victim==player&&clog_ptr(owner)&&owner!=player){
        clog_seen(owner);
        snprintf(d,sizeof d,"arma=%u dano=%u tipo=%u",clog_ptr(weapon)?G32(weapon+0x2F4):0u,G32(sp+0x30),G32(sp+0x34));
        clog_row("RECIBE",0,owner,"",d);
    }else if(owner==player&&clog_ptr(victim)){
        clog_seen(victim);
        snprintf(d,sizeof d,"dano=%u tipo=%u",G32(sp+0x30),G32(sp+0x34));
        clog_row("GOLPE",0,victim,applied?"aplicado":"sin_aplicar",d);
    }
}
#endif
