#include "inputs.h"
#include <string.h>

typedef struct {
    /* Debounce générique */
    uint8_t stable;     /* 0/1 stable courant (niveau logique après inversion active_low) */
    uint8_t last_raw;   /* dernier échantillon brut (0/1, non inversé) */
    uint16_t acc_ms;    /* ms accumulées sur la même valeur */
    uint16_t thresh_ms; /* ms nécessaires pour considérer stable */
    uint8_t active_low; /* inversion du niveau si besoin */
} DebIn;

/* Sélecteur 3 positions exclusives → index 0/1/2 (ELEC/GAS/BI) */
typedef struct {
    uint8_t stable_idx; /* 0..2 */
    uint8_t last_idx;   /* 0..2 */
    uint16_t acc_ms;
    uint16_t thresh_ms;
    uint8_t a_active_low, b_active_low, c_active_low;
} TriSel;

static InputsConfig g_cfg;
static DebIn g_th;          /* thermostat */
static DebIn g_provider;    /* provider bi-énergie */
static TriSel g_mode;       /* sélecteur utilisateur */

/* Helpers */
static uint8_t apply_active_low(uint8_t raw, uint8_t active_low) { return (uint8_t)(active_low ? (uint8_t)(raw == 0U) : raw); }

/* Lit l’index du sélecteur (0=A, 1=B, 2=C), retourne 255 si ambigu (2 ON) */
static uint8_t mode_read_index(void)
{
    uint8_t a = hw_read_modeA_raw() ? 1U : 0U;
    uint8_t b = hw_read_modeB_raw() ? 1U : 0U;
    uint8_t c = hw_read_modeC_raw() ? 1U : 0U;

    a = apply_active_low(a, g_mode.a_active_low);
    b = apply_active_low(b, g_mode.b_active_low);
    c = apply_active_low(c, g_mode.c_active_low);

    /* Exclusif idéalement: un seul à 1 */
    if ((a + b + c) != 1U) {
        return 255U; /* état invalide/transition mécanique */
    }
    if (a == 1U) return 0U;
    if (b == 1U) return 1U;
    return 2U; /* c == 1 */
}

static void deb_init(DebIn* d, uint8_t active_low, uint16_t thresh_ms)
{
    d->stable = 0U;
    d->last_raw = 0U;
    d->acc_ms = 0U;
    d->thresh_ms = thresh_ms;
    d->active_low = active_low;
}

static void tris_init(TriSel* t, uint16_t thresh_ms, uint8_t a_low, uint8_t b_low, uint8_t c_low)
{
    t->stable_idx = 0U;
    t->last_idx = 0U;
    t->acc_ms = 0U;
    t->thresh_ms = thresh_ms;
    t->a_active_low = a_low;
    t->b_active_low = b_low;
    t->c_active_low = c_low;
}

void inputs_init(const InputsConfig* cfg)
{
    if (cfg != NULL) { g_cfg = *cfg; }
    deb_init(&g_th,       g_cfg.thermostat_active_low, INP_DEBOUNCE_MS);
    deb_init(&g_provider, g_cfg.provider_active_low,   INP_PROVIDER_STABLE_MS);
    tris_init(&g_mode,    INP_MODE_STABLE_MS, g_cfg.modeA_active_low, g_cfg.modeB_active_low, g_cfg.modeC_active_low);
}

void inputs_seed_from_hw(void)
{
    /* Seed thermostat */
    uint8_t raw = hw_read_thermostat_raw() ? 1U : 0U;
    uint8_t lvl = apply_active_low(raw, g_th.active_low);
    g_th.stable = lvl;
    g_th.last_raw = raw;
    g_th.acc_ms = g_th.thresh_ms;

    /* Seed provider */
    raw = hw_read_provider_raw() ? 1U : 0U;
    lvl = apply_active_low(raw, g_provider.active_low);
    g_provider.stable = lvl;
    g_provider.last_raw = raw;
    g_provider.acc_ms = g_provider.thresh_ms;

    /* Seed mode */
    uint8_t idx = mode_read_index();
    if (idx != 255U) {
        g_mode.stable_idx = idx;
        g_mode.last_idx = idx;
        g_mode.acc_ms = g_mode.thresh_ms;
    } else {
        g_mode.stable_idx = 0U;
        g_mode.last_idx = 0U;
        g_mode.acc_ms = 0U;
    }
}

/* Debounce binaire 0/1 → retourne 1 si changement de 'stable' détecté, et place la nouvelle valeur dans *new_stable */
static uint8_t deb_tick(DebIn* d, uint8_t raw_now, uint8_t* new_stable)
{
    uint8_t changed = 0U;
    if (raw_now == d->last_raw) {
        if (d->acc_ms < 0xFFFFU) {
            d->acc_ms = (uint16_t)(d->acc_ms + INP_TICK_MS);
        }
        if ((d->acc_ms >= d->thresh_ms) != 0U) {
            uint8_t lvl = apply_active_low(raw_now, d->active_low);
            if (lvl != d->stable) {
                d->stable = lvl;
                changed = 1U;
                *new_stable = lvl;
            }
            /* On garde acc_ms saturé; pas besoin de la remettre à zéro tant que la valeur brute ne change pas */
        }
    } else {
        d->last_raw = raw_now;
        d->acc_ms = INP_TICK_MS; /* on recommence l’accumulation */
    }
    return changed;
}

/* Debounce 3 positions exclusives → retourne 1 si index stable change et place la nouvelle valeur dans *new_idx */
static uint8_t tris_tick(TriSel* t, uint8_t* new_idx)
{
    uint8_t idx = mode_read_index();
    if (idx == 255U) {
        /* position mécaniquement ambiguë: on remet le compteur, ne change pas stable */
        t->acc_ms = 0U;
        t->last_idx = 255U;
        return 0U;
    }

    if (idx == t->last_idx) {
        if (t->acc_ms < 0xFFFFU) {
            t->acc_ms = (uint16_t)(t->acc_ms + INP_TICK_MS);
        }
        if ((t->acc_ms >= t->thresh_ms) != 0U) {
            if (idx != t->stable_idx) {
                t->stable_idx = idx;
                *new_idx = idx;
                return 1U;
            }
        }
    } else {
        t->last_idx = idx;
        t->acc_ms = INP_TICK_MS;
    }
    return 0U;
}

void inputs_tick(void)
{
    /* 1) Thermostat -> EVT_TH_ON/OFF */
    uint8_t newlvl = 0U;
    uint8_t raw = hw_read_thermostat_raw() ? 1U : 0U;
    if (deb_tick(&g_th, raw, &newlvl) != 0U) {
        if (newlvl != 0U) {
            (void)evq_push(EVQ_NORMAL, EVT_TH_ON, EVARG_NONE());
        } else {
            (void)evq_push(EVQ_NORMAL, EVT_TH_OFF, EVARG_NONE());
        }
    }

    /* 2) Provider (stabilité longue) -> EVT_PROVIDER_TO_* */
    raw = hw_read_provider_raw() ? 1U : 0U;
    if (deb_tick(&g_provider, raw, &newlvl) != 0U) {
        /* Ici 'stable' représente “ELEC=1 / GAS=0” après inversion éventuelle */
        if (newlvl != 0U) {
            (void)evq_push(EVQ_NORMAL, EVT_PROVIDER_TO_ELEC, EVARG_NONE());
        } else {
            (void)evq_push(EVQ_NORMAL, EVT_PROVIDER_TO_GAS, EVARG_NONE());
        }
    }

    /* 3) Sélecteur utilisateur (3 positions) -> EVT_USER_MODE_* */
    uint8_t newidx = 0U;
    if (tris_tick(&g_mode, &newidx) != 0U) {
        switch (newidx) {
            case 0U: (void)evq_push(EVQ_NORMAL, EVT_USER_MODE_ELEC, EVARG_NONE()); break;
            case 1U: (void)evq_push(EVQ_NORMAL, EVT_USER_MODE_GAS,  EVARG_NONE()); break;
            case 2U: (void)evq_push(EVQ_NORMAL, EVT_USER_MODE_BI,   EVARG_NONE()); break;
            default: /* impossible */ break;
        }
    }
}
