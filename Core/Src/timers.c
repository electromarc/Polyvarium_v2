#include "timers.h"
#include <string.h>

/* Représentation interne d’un timer one-shot. */
typedef struct {
    uint32_t ticks;     /* temps restant en unités de TMR_TICK_MS */
    EventType evt;      /* à émettre à l’expiration */
    EventArg  arg;      /* payload optionnel */
    uint8_t   active;   /* 0/1 */
} sw_timer_t;

/* Tableau statique: zéro alloc dynamique, MISRA-friendly. */
static sw_timer_t g_timers[TMR_COUNT];

/* Compteur système optionnel (ticks de TMR_TICK_MS) si tu veux time-stamper.
   Si tu t’en fiches, ignore-le. */
static uint32_t g_tmr_uptime_ticks;

/* Helpers */
static inline uint32_t ms_to_ticks(uint32_t ms)
{
    /* Arrondi plafond pour ne jamais expirer trop tôt */
    uint32_t q = ms / TMR_TICK_MS;
    if ((ms % TMR_TICK_MS) != 0U) { q++; }
    if (q == 0U) { q = 1U; }  /* éviter 0 → expire au prochain tick */
    return q;
}

void tmr_init(void)
{
    (void)memset(g_timers, 0, sizeof(g_timers));
    g_tmr_uptime_ticks = 0U;
}

bool tmr_set(TimerId id, uint32_t delay_ms, EventType evt, EventArg arg)
{
    if ((uint32_t)id >= TMR_COUNT) { return false; }
    if ((evt <= 0) || (evt >= EVT_MAX_ENUM)) { return false; }

    sw_timer_t* t = &g_timers[id];
    t->ticks = ms_to_ticks(delay_ms);
    t->evt   = evt;
    t->arg   = arg;
    t->active = 1U;
    return true;
}

void tmr_cancel(TimerId id)
{
    if ((uint32_t)id >= TMR_COUNT) { return; }
    g_timers[id].active = 0U;
    g_timers[id].ticks  = 0U;
}

bool tmr_is_active(TimerId id)
{
    if ((uint32_t)id >= TMR_COUNT) { return false; }
    return (g_timers[id].active != 0U);
}

uint32_t tmr_remaining_ms(TimerId id)
{
    if ((uint32_t)id >= TMR_COUNT) { return 0U; }
    uint32_t ticks = g_timers[id].ticks;
    return ticks * (uint32_t)TMR_TICK_MS;
}

/* Politique d’émission:
   - Chaque timer expiré pousse 1 event sur EVQ_NORMAL.
   - En cas d’échec de push (queue pleine), on retente au tick suivant
     en NE remettant pas ticks à 0 tant que l’event n’a pas été accepté.
     → On garantit de ne pas perdre l’expiration (au prix d’un retard si la file déborde). */
void tmr_tick(void)
{
    g_tmr_uptime_ticks++;

    for (uint32_t i = 0U; i < (uint32_t)TMR_COUNT; i++) {
        sw_timer_t* t = &g_timers[i];
        if (t->active == 0U) { continue; }

        if (t->ticks > 0U) {
            t->ticks--;
        }

        if ((t->ticks == 0U) && (t->active != 0U)) {
            /* Tente d’émettre l’événement d’expiration */
            EventArg a = t->arg;
            const bool ok = evq_push(EVQ_NORMAL, t->evt, a);
            if (ok) {
                /* Désarme seulement si l’événement a été accepté */
                t->active = 0U;
            } else {
                /* File NORMAL pleine: on réessaie au prochain tick.
                   Laisse ticks à 0 pour retenter sans re-décompter. */
            }
        }
    }
}
