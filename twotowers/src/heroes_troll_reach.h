/* (133) Respaldo del golpe del troll cuando la consulta precisa (BA110) falla
 * con una victima que el juego ya ha elegido como candidata: la cabeza de la
 * maza (y 1..5,2 de la DB, cinco muestras) entre la matriz del fotograma
 * anterior y la actual (cinco instantes) contra el eje vertical de la
 * victima. (134) Pegado al troll la cabeza le pasa por encima y cae mas
 * alla: tambien cuenta el recorrido del cuerpo del troll a la cabeza (brazo
 * y mango), nunca detras de el. Geometria pura: sin memoria del juego. */
#ifndef TT_HEROES_TROLL_REACH_H
#define TT_HEROES_TROLL_REACH_H
#define TROLL_REACH_R    2.5f   /* horizontal: cuerpo de la victima + media cabeza */
#define TROLL_REACH_ZLO -2.0f   /* bajo los pies (pendientes, golpe al suelo) */
#define TROLL_REACH_ZHI  7.0f   /* sobre los pies: hasta la cabeza de un Uruk */
#define TROLL_REACH_BODY 6      /* muestras del cuerpo del troll a la cabeza */
/* body: posicion del troll (NULL = solo la maza) */
static int troll_reach_hit(const float prev[16],const float cur[16],const float victim[3],const float body[3],float pos[3],float normal[3],float *frac){
 static const float ys[5]={1.f,2.f,3.f,4.f,5.19682591f};
 float best=TROLL_REACH_R*TROLL_REACH_R;int found=0;
 for(int c=0;c<16;c++)if(!isfinite(prev[c])||!isfinite(cur[c]))return 0;
 for(int c=0;c<3;c++)if(!isfinite(victim[c])||(body&&!isfinite(body[c])))return 0;
 for(int k=0;k<5;k++)for(int j=0;j<5+(body?TROLL_REACH_BODY:0);j++){
  float t=.25f*k,p[3];
  if(j<5)for(int c=0;c<3;c++){float a=ys[j]*prev[4+c]+prev[12+c],b=ys[j]*cur[4+c]+cur[12+c];p[c]=a+(b-a)*t;}
  else{float s=(j-4)/(float)(TROLL_REACH_BODY+1);   /* 1/7..6/7: ni el centro del troll ni la cabeza */
   for(int c=0;c<3;c++){float a=ys[4]*prev[4+c]+prev[12+c],b=ys[4]*cur[4+c]+cur[12+c],h=a+(b-a)*t;p[c]=body[c]+(h-body[c])*s;}}
  float dz=p[2]-victim[2],dx=p[0]-victim[0],dy=p[1]-victim[1],d2=dx*dx+dy*dy;
  /* a igual distancia gana el primer instante (primer contacto) */
  if(dz<TROLL_REACH_ZLO||dz>TROLL_REACH_ZHI||!(found?d2<best:d2<=best))continue;
  best=d2;found=1;*frac=t;
  for(int c=0;c<3;c++)pos[c]=p[c];
  float l=sqrtf(d2);
  if(l>1e-4f){normal[0]=dx/l;normal[1]=dy/l;normal[2]=0;}else{normal[0]=normal[1]=0;normal[2]=1;}
 }
 return found;
}
#endif
