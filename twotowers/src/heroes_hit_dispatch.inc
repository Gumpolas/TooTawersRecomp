/* Requiere heroes_sweep_query.h para conservar el estado de la CPU Xbox. */
#ifndef TT_HEROES_HIT_DISPATCH_H
#define TT_HEROES_HIT_DISPATCH_H
extern void sub_000C7610_gen(void);
extern int tt_troll_hit(uint32_t sp);
void sub_000C7610(void)
{
    TTSweepCPU cpu;tt_sweep_cpu_save(&cpu);
    tt_troll_hit(g_esp);
    tt_sweep_cpu_restore(&cpu);
    sub_000C7610_gen();
}
#endif
