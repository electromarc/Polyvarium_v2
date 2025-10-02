#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "events.h"   /* pour EventType / EventArg / evq_push */

/* Granularité du tick (en ms). Ex: 10 ms recommandé. */
#ifndef TMR_TICK_MS
#define TMR_TICK_MS 10U
#endif

/* Nombre de timers logiciels disponibles.
   Tu peux augmenter si tu en veux plus. */
#ifndef TMR_COUNT
#define TMR_COUNT 8U
#endif

/* Identifiants de timers.
   Réserve-les proprement pour ton appli (séquence, lockout, etc.). */
typedef enum {
    TMR_SEQ = 0,        /* 1-2-3 / 3-2-1 */
    TMR_MIN_OFF,        /* anti-flap thermostat: 120 s */
    TMR_MIN_ON,         /* optionnel */
    TMR_COOLDOWN_MIN,   /* ventilation minimale */
    TMR_MAX_BURNER,     /* sécurité */
    TMR_MAX_ELEMS,      /* sécurité */
    TMR_USER_0,         /* libre */
    TMR_USER_1,         /* libre */
    /* ... jusqu’à TMR_COUNT-1 */
} TimerId;

/* Initialisation du service de timers. */
void tmr_init(void);

/* Armer un timer one-shot.
   delay_ms sera arrondi à la granularité TMR_TICK_MS vers le haut.
   À l’expiration: un événement (type/arg) est poussé sur EVQ_NORMAL. */
bool tmr_set(TimerId id, uint32_t delay_ms, EventType evt, EventArg arg);

/* Annuler un timer. */
void tmr_cancel(TimerId id);

/* Est-ce que le timer est actif ? */
bool tmr_is_active(TimerId id);

/* Temps restant (ms) arrondi au multiple de TMR_TICK_MS. 0 si inactif. */
uint32_t tmr_remaining_ms(TimerId id);

/* À appeler périodiquement toutes les TMR_TICK_MS (ex: depuis un ISR ou une tâche).
   Décrémente, émet l’événement à 0, désarme. */
void tmr_tick(void);
