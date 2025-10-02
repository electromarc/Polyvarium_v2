#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "events.h"
#include "timers.h"

/* États du procédé (simples) */
typedef enum {
    ST_IDLE = 0,
    ST_STARTING,     /* séquence élec 1→2→3 */
    ST_HEAT_ELEC,
    ST_HEAT_GAS,
    ST_STOPPING,     /* séquence arrêt 3→2→1 */
    ST_COOLDOWN,
    ST_FAULT,
    ST_MAX
} FsmState;

/* Guards = conditions booléennes sans effet de bord */
typedef enum {
    GUARD_NONE = 0,
    GUARD_LOCKOUT_CLEAR,   /* anti-flap thermostat expiré */
    GUARD_TARGET_ELEC,     /* cible finale électricité */
    GUARD_TARGET_GAS,      /* cible finale gaz */
    GUARD_TEMP_SAFE,       /* T° sous seuil safe (avec hystérésis) */
    GUARD_NO_FAULT,        /* aucune faute latched */
    GUARD_MAX
} GuardId;

/* Actions = intentions atomiques, dispatchées dans fsm.c */
typedef enum {
    ACT_NONE = 0,
    ACT_SEQ_START,     /* lance séquence 1→2→3 (arme TMR_SEQ) */
    ACT_SEQ_STEP,      /* étape suivante de la séquence (1→2→3 ou 3→2→1) */
    ACT_SEQ_STOP,      /* lance séquence 3→2→1 (arme TMR_SEQ) */
    ACT_ENTER_ELEC,    /* tag interne: en chauffe élec */
    ACT_ENTER_GAS,     /* tag interne: en chauffe gaz  */
    ACT_ENTER_COOL,    /* tag interne: en cooldown     */
    ACT_ALL_OFF,       /* tout OFF (fan selon safety)  */
    ACT_ENTER_FAULT,   /* bascule en défaut            */
    ACT_MAX
} ActionId;

/* Entrée de table FSM */
typedef struct {
    FsmState src;
    EventType evt;
    GuardId guard;     /* GUARD_NONE si non utilisé */
    ActionId act;      /* ACT_NONE si pas d'action */
    FsmState dst;
} FsmTransition;

/* API FSM */
void fsm_init(FsmState init);
FsmState fsm_state(void);

/* Traite un événement: retourne true si une transition a été appliquée */
bool fsm_handle_event(const EventMsg* ev);

/* Hooks optionnels fournis par d'autres modules (implémentés ailleurs) */
bool guard_lockout_clear(void);
bool guard_target_is_elec(void);
bool guard_target_is_gas(void);
bool guard_temp_is_safe(void);
bool guard_no_fault(void);
