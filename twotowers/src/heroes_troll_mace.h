/* Generado con perfil_maza.py desde 830B7594; cabeza de la envolvente
 * convexa de los vertices usados. No contiene texturas ni mallas. */
#ifndef TT_HEROES_TROLL_MACE_H
#define TT_HEROES_TROLL_MACE_H
static const float s_troll_mace_head[5][4][3]={
 {{-1.81175410f,1.00000000f,0.58544019f},{1.82010480f,1.00000000f,0.58542978f},{-0.01331978f,1.00000000f,-1.35238953f},{-0.01332098f,1.00000000f,1.35788039f}},
 {{-2.12052919f,2.00000000f,0.70846686f},{2.13604666f,2.00000000f,0.70840873f},{-0.01398697f,2.00000000f,-1.56974981f},{-0.01398697f,2.00000000f,1.56815100f}},
 {{-2.34680708f,3.00000000f,0.79863767f},{2.36763368f,3.00000000f,0.79855928f},{-0.01398604f,3.00000000f,-1.56974486f},{-0.01398604f,3.00000000f,1.56815100f}},
 {{-2.34679576f,4.00000000f,0.79868513f},{2.36783484f,4.00000000f,0.79866134f},{-0.01398511f,4.00000000f,-1.56973991f},{-0.01398511f,4.00000000f,1.56815100f}},
 {{-0.01697618f,5.19682591f,-1.56669609f},{-0.01092884f,5.19682591f,1.56716406f},{-0.01398400f,5.19682591f,-1.56973398f},{-0.01398400f,5.19682591f,1.56815100f}},
};
/* (132) la cabeza un 25% mas ancha (x, z) que la malla: el usuario pedia
 * un golpe "un poco mas grande que la maza"; el largo (y) es el de la DB */
#define TROLL_MACE_GROW 1.25f
static int troll_mace_point(float y,int lane,float out[3]){
 if(lane<1||lane>4||!isfinite(y)||y< -2.001f||y>6.001f)return 0;
 float sample=(y+2.f)*.5f;if(sample<0)sample=0;if(sample>4)sample=4;
 int k=(int)sample;if(k>3)k=3;float t=sample-k;
 for(int c=0;c<3;c++)out[c]=s_troll_mace_head[k][lane-1][c]*(1-t)+s_troll_mace_head[k+1][lane-1][c]*t;
 out[0]*=TROLL_MACE_GROW;out[2]*=TROLL_MACE_GROW;
 return 1;
}
#endif
