#include "fsm.h"
#include "stddef.h"
/* --------- Paramètres locaux de séquence --------- */
#ifndef SEQ_DELAY_MS
#define SEQ_DELAY_MS 12000U   /* délai 12 s entre étapes, adapte si besoin */
#endif

/* Séquence interne: sens + étape courante */
typedef enum { SEQ_DIR_NONE=0, SEQ_DIR_UP, SEQ_DIR_DOWN } seq_dir_t;
static seq_dir_t g_seq_dir = SEQ_DIR_NONE;
static uint8_t   g_seq_step = 0U;   /* 0..3 pour UP, 3..0 pour DOWN */

/* État courant de la FSM */
static FsmState g_state = ST_IDLE;

/* --------- Prototypes d'actions primitives (coté "intention") ---------
   Ici on n'appelle aucun driver directement. Tu brancheras plus tard
   vers un ActuatorManager/Safety. On se contente d'indiquer l'intention. */
static void seq_start_begin(void);
static void seq_stop_begin(void);
static void seq_step(void);
static void mark_enter_elec(void);
static void mark_enter_gas(void);
static void mark_enter_cool(void);
static void mark_all_off(void);
static void mark_enter_fault(void);

/* --------- Dispatchers guard/action --------- */
static bool guard_eval(GuardId g)
{
    switch (g) {
        case GUARD_NONE:           return true;
        case GUARD_LOCKOUT_CLEAR:  return guard_lockout_clear();
        case GUARD_TARGET_ELEC:    return guard_target_is_elec();
        case GUARD_TARGET_GAS:     return guard_target_is_gas();
        case GUARD_TEMP_SAFE:      return guard_temp_is_safe();
        case GUARD_NO_FAULT:       return guard_no_fault();
        default:                   return false;
    }
}

static void action_exec(ActionId a)
{
    switch (a) {
        case ACT_NONE:         break;
        case ACT_SEQ_START:    seq_start_begin(); break;
        case ACT_SEQ_STEP:     seq_step();        break;
        case ACT_SEQ_STOP:     seq_stop_begin();  break;
        case ACT_ENTER_ELEC:   mark_enter_elec(); break;
        case ACT_ENTER_GAS:    mark_enter_gas();  break;
        case ACT_ENTER_COOL:   mark_enter_cool(); break;
        case ACT_ALL_OFF:      mark_all_off();    break;
        case ACT_ENTER_FAULT:  mark_enter_fault();break;
        default:               break;
    }
}

/* --------- La table FSM (triée par événement) --------- */
static const FsmTransition FSM[] = {
    /* Thermostat ON (anti-flap + cible) */
    { ST_IDLE,      EVT_TH_ON,          GUARD_LOCKOUT_CLEAR, ACT_NONE,        ST_IDLE },      /* guard commune, affiner ci-dessous via TRANSITION_REQ si besoin */
    { ST_IDLE,      EVT_TH_ON,          GUARD_TARGET_ELEC,   ACT_SEQ_START,   ST_STARTING },
    { ST_IDLE,      EVT_TH_ON,          GUARD_TARGET_GAS,    ACT_ENTER_GAS,   ST_HEAT_GAS },

    /* Fin d'étape de séquence (STARTING/STOPPING) */
    { ST_STARTING,  EVT_SEQ_STEP_TIMEOUT, GUARD_NONE,       ACT_SEQ_STEP,    ST_STARTING },  /* reste en STARTING jusqu'à fin */
    { ST_STOPPING,  EVT_SEQ_STEP_TIMEOUT, GUARD_NONE,       ACT_SEQ_STEP,    ST_STOPPING },

    /* Séquence globale terminée (on s'auto-génère EVT_SEQ_DONE depuis seq_step quand c'est fini) */
    { ST_STARTING,  EVT_SEQ_DONE,      GUARD_NONE,          ACT_ENTER_ELEC,  ST_HEAT_ELEC },
    { ST_STOPPING,  EVT_SEQ_DONE,      GUARD_NONE,          ACT_ENTER_COOL,  ST_COOLDOWN },

    /* Thermostat OFF */
    { ST_HEAT_ELEC, EVT_TH_OFF,        GUARD_NONE,          ACT_SEQ_STOP,    ST_STOPPING },
    { ST_HEAT_GAS,  EVT_TH_OFF,        GUARD_NONE,          ACT_ENTER_COOL,  ST_COOLDOWN },

    /* Température redevenue sûre pendant COOLDOWN */
    { ST_COOLDOWN,  EVT_TEMP_SAFE,     GUARD_NONE,          ACT_ALL_OFF,     ST_IDLE },

    /* Bascule bi-énergie demandée (orchestration) */
    { ST_HEAT_ELEC, EVT_TRANSITION_REQ, GUARD_TARGET_GAS,   ACT_SEQ_STOP,    ST_STOPPING },
    { ST_HEAT_GAS,  EVT_TRANSITION_REQ, GUARD_TARGET_ELEC,  ACT_ENTER_COOL,  ST_COOLDOWN },

    /* Défauts critiques: on traite via fast-path ci-dessous pour éviter 6 lignes */
    /* … (voir fsm_handle_event) … */
};
static const uint32_t FSM_COUNT = (uint32_t)(sizeof(FSM)/sizeof(FSM[0]));

/* --------- API --------- */
void fsm_init(FsmState init) { g_state = init; g_seq_dir = SEQ_DIR_NONE; g_seq_step = 0U; }
FsmState fsm_state(void) { return g_state; }

/* Moteur: applique la première transition qui matche (src,evt,guard) */
bool fsm_handle_event(const EventMsg* ev)
{
    if (ev == NULL) { return false; }

    /* Fast-path sécurité: défaut critique → FAULT partout */
    if (ev->type == EVT_OVERTEMP_CRIT ||
        ev->type == EVT_FAULT_REDUNDANCY ||
        ev->type == EVT_FAULT_TIME_BURNER ||
        ev->type == EVT_FAULT_TIME_ELEMS ||
        ev->type == EVT_SENSOR_FAULT) {
        action_exec(ACT_ENTER_FAULT);
        g_state = ST_FAULT;
        return true;
    }

    for (uint32_t i = 0U; i < FSM_COUNT; i++) {
        const FsmTransition* t = &FSM[i];
        if ((t->evt == ev->type) && (t->src == g_state)) {
            if (!guard_eval(t->guard)) { continue; }
            action_exec(t->act);
            g_state = t->dst;
            return true;
        }
    }

    /* Aucun match: ignoré (le scheduler peut appeler evq_note_ignored(ev->type)) */
    return false;
}

/* --------- Implémentations d'actions internes ---------
   Ici on gère uniquement la logique de séquencement et les "tags" d'état.
   Les commandes physiques (fan/elements/burner) seront faites plus tard
   par un ActuatorManager quand tu traduiras ces intentions en sorties. */

static void seq_start_begin(void)
{
    g_seq_dir  = SEQ_DIR_UP;
    g_seq_step = 0U;  /* 0: E1, 1: E2, 2: E3 */
    /* TODO: intention: fan_on(); el1_on(); */
    (void)tmr_set(TMR_SEQ, SEQ_DELAY_MS, EVT_SEQ_STEP_TIMEOUT, EVARG_NONE());
}

static void seq_stop_begin(void)
{
    g_seq_dir  = SEQ_DIR_DOWN;
    g_seq_step = 3U;  /* 3: E3 off, 2: E2 off, 1: E1 off */
    /* TODO: intention: el3_off(); */
    (void)tmr_set(TMR_SEQ, SEQ_DELAY_MS, EVT_SEQ_STEP_TIMEOUT, EVARG_NONE());
}

/* Avance d'une étape. Si séquence terminée, émet EVT_SEQ_DONE. */
static void seq_step(void)
{
    if (g_seq_dir == SEQ_DIR_UP) {
        if (g_seq_step == 0U) {
            /* TODO: el2_on(); */
            g_seq_step = 1U;
            (void)tmr_set(TMR_SEQ, SEQ_DELAY_MS, EVT_SEQ_STEP_TIMEOUT, EVARG_NONE());
        } else if (g_seq_step == 1U) {
            /* TODO: el3_on(); */
            g_seq_step = 2U;
            /* Fin de séquence UP au prochain "done" immédiat */
            EventMsg done = { .type = EVT_SEQ_DONE, .arg = EVARG_NONE(), .tick = 0U };
            (void)evq_push(EVQ_NORMAL, done.type, done.arg);
            g_seq_dir = SEQ_DIR_NONE;
        } else {
            /* déjà fini */
        }
    } else if (g_seq_dir == SEQ_DIR_DOWN) {
        if (g_seq_step == 3U) {
            /* TODO: el2_off(); */
            g_seq_step = 2U;
            (void)tmr_set(TMR_SEQ, SEQ_DELAY_MS, EVT_SEQ_STEP_TIMEOUT, EVARG_NONE());
        } else if (g_seq_step == 2U) {
            /* TODO: el1_off(); */
            g_seq_step = 1U;
            (void)tmr_set(TMR_SEQ, SEQ_DELAY_MS, EVT_SEQ_STEP_TIMEOUT, EVARG_NONE());
        } else {
            /* plus rien à éteindre: fin de séquence DOWN */
            EventMsg done = { .type = EVT_SEQ_DONE, .arg = EVARG_NONE(), .tick = 0U };
            (void)evq_push(EVQ_NORMAL, done.type, done.arg);
            g_seq_dir = SEQ_DIR_NONE;
        }
    } else {
        /* pas en séquence: rien */
    }
}

static void mark_enter_elec(void)
{
    /* TODO: intention: status=HEAT_ELEC; garantir fan_on; */
    (void)0;
}

static void mark_enter_gas(void)
{
    /* TODO: intention: status=HEAT_GAS; garantir fan_on; burner_on; */
    (void)0;
}

static void mark_enter_cool(void)
{
    /* TODO: intention: status=COOLDOWN; fan_on; éventuellement tmr_set(TMR_COOLDOWN_MIN, ...) */
    (void)0;
}

static void mark_all_off(void)
{
    /* TODO: intention: tout OFF; status=IDLE; */
    (void)0;
}

static void mark_enter_fault(void)
{
    /* TODO: intention: outputs_off_sauf_fan; status=FAULT; latch; */
    g_seq_dir = SEQ_DIR_NONE;
}
