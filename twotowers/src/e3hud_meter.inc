/* e3hud_meter.inc -- the experience numbers behind the retail thermometer.
 * The HUD update (0x000CF9A0) stores, every frame,
 *   0x4A848C = experience earned since the current level started
 *   0x4A3750 = experience that level needs (10000 at the top level),
 * and draws the thermometer at 87 px * 0x4A848C / 0x4A3750. */
static void meter_read(int *v, int *m)
{
    int32_t cur = (int32_t)G32(0x4A848Cu), need = (int32_t)G32(0x4A3750u);
    if (need <= 0 || need > 1000000 || cur < 0 || cur > need) { *v = -1; *m = -1; return; }
    *v = cur; *m = need;
}

/* recolour the two retail fills to the demo's while the mod is on (the
 * health colour is also set every frame by the game: see tt_e3hud_prehud) */
static void fills_apply(int on)
{
    static int was_on;
    uint32_t h = G32(HUD_HEALTH_FILL_COL), e = G32(HUD_EXP_FILL_COL);
    if (on) {
        if (h == RETAIL_HEALTH_COL) G32(HUD_HEALTH_FILL_COL) = E3_HEALTH_COL;
        if (e == RETAIL_EXP_COL) G32(HUD_EXP_FILL_COL) = E3_EXP_COL;
        was_on = 1;
    } else if (was_on) {
        if (h == E3_HEALTH_COL) G32(HUD_HEALTH_FILL_COL) = RETAIL_HEALTH_COL;
        if (e == E3_EXP_COL) G32(HUD_EXP_FILL_COL) = RETAIL_EXP_COL;
        was_on = 0;
    }
}
