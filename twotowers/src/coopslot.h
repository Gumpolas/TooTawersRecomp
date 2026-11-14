/* (2.1) Co-op players 3 and 4: the "extra player slots".
 *
 * Player 2's machinery (twotowers_manual.c, heroes.c) keeps its state in plain globals and statics. Players 3 and 4 run
 * the same code: every variable that belongs to "the extra player" is put in one linker section (COOP_PS / COOP_PSG), and
 * the section is swapped with a saved copy per slot (coop_slot_set): slot 0 is player 2, slot 1 player 3, slot 2 player 4.
 * Whatever runs with a slot in place sees that player's actor in coop_p2, his saved globals in coop_s2, and so on.
 * A few arrays indexed by player ([0] player 1, [1] player 2) have only their [1] swapped (coop_slot_reg in
 * twotowers_manual.c). */
#ifndef COOPSLOT_H
#define COOPSLOT_H
#if defined(_WIN32)
#define COOP_SLOT_SEC ".cps$m"
#else
#define COOP_SLOT_SEC "cps"
#endif
#define COOP_PS static __attribute__((used, section(COOP_SLOT_SEC)))   /* a static (file scope or in a function): one per extra player */
#define COOP_PSG __attribute__((used, section(COOP_SLOT_SEC)))         /* the same for an external variable */
#define COOP_MAXX 3   /* extra players: 2, 3 and 4 */
extern int coop_slot_cur;          /* the slot in place (0 player 2, 1 player 3, 2 player 4) */
extern int tt_coop_nplayers(void); /* players in this co-op game: 2 to 4 */
void coop_slot_set(int k);
#endif
