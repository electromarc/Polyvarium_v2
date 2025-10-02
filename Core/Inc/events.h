#pragma once
#include <stdint.h>
#include <stdbool.h>

/* Priorités d’événements */
typedef enum {
    EVQ_NORMAL = 0,
    EVQ_FAULTS = 1
} EvQueueId;

/* Dictionnaire des événements (liste canon V2) */
typedef enum {
    /* Entrées (fronts) */
    EVT_TH_ON = 1,
    EVT_TH_OFF,
    EVT_PROVIDER_TO_ELEC,
    EVT_PROVIDER_TO_GAS,
    EVT_USER_MODE_ELEC,
    EVT_USER_MODE_GAS,
    EVT_USER_MODE_BI,

    /* Timers (one-shot) */
    EVT_SEQ_STEP_TIMEOUT,
    EVT_MIN_ON_DONE,
    EVT_MIN_OFF_DONE,
    EVT_COOLDOWN_TIMEOUT,

    /* Capteurs / seuils */
    EVT_TEMP_SAFE,
    EVT_OVERTEMP_WARN,
    EVT_OVERTEMP_CRIT,

    /* Sécurité / watchdogs */
    EVT_FAULT_REDUNDANCY,
    EVT_FAULT_TIME_BURNER,
    EVT_FAULT_TIME_ELEMS,
    EVT_SENSOR_FAULT,
    EVT_FAULT_CLEAR,

    /* Orchestration interne */
    EVT_SEQ_DONE,
    EVT_TRANSITION_REQ,

    /* Réserves */
    EVT_RESERVED_1,
    EVT_RESERVED_2,

    EVT_MAX_ENUM
} EventType;

/* Option: petite donnée attachée à l’événement (id timer, code capteur, etc.) */
typedef struct {
    uint8_t u8;
    uint16_t u16;
} EventArg;

/* Message d’événement */
typedef struct {
    EventType type;   // genre EVT_TH_ON, EVT_TIMEOUT...
    EventArg  arg;    // petite donnée optionnelle
    uint32_t  tick;   // instant où l’event a été produit
} EventMsg;

/* Statistiques utiles pour debug/telemetry */
typedef struct {
    uint32_t pushed;
    uint32_t popped;
    uint32_t dropped;
    uint32_t coalesced;
    uint32_t ignored;   /* par la FSM */
} EvQueueStats;

/* Taille des files (chauffage = tranquille) */
#ifndef EVQ_NORMAL_CAP
#define EVQ_NORMAL_CAP  32
#endif
#ifndef EVQ_FAULTS_CAP
#define EVQ_FAULTS_CAP  8
#endif

/* API files d’événements */
void evq_init(void);
bool evq_push(EvQueueId qid, EventType type, EventArg arg);
bool evq_pop_next(EventMsg* out);       /* vide d’abord FAULTS, puis NORMAL */
void evq_note_ignored(EventType type);  /* compteur "ignored" */
void evq_get_stats(EvQueueId qid, EvQueueStats* out);

/* Coalescence configurable (on/off selon type) */
bool evq_set_coalesce(EventType type, bool enable);

/* Petites aides pour arg */
static inline EventArg EVARG_U8(uint8_t v){ EventArg a={.u8=v,.u16=0}; return a; }
static inline EventArg EVARG_U16(uint16_t v){ EventArg a={.u8=0,.u16=v}; return a; }
static inline EventArg EVARG_NONE(void){ EventArg a={0}; return a; }
