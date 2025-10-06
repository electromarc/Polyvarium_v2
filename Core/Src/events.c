#include "events.h"
#include <string.h>

/* Files circulaires séparées: FAULTS et NORMAL */
typedef struct {
    EventMsg buf[EVQ_NORMAL_CAP];
    volatile uint16_t head, tail;
    EvQueueStats stats;
} EvqNormal;

typedef struct {
    EventMsg buf[EVQ_FAULTS_CAP];
    volatile uint16_t head, tail;
    EvQueueStats stats;
} EvqFaults;

static EvqNormal q_normal;
static EvqFaults q_faults;

/* Table de coalescence: 1 = on évite les doublons déjà en file */
static uint8_t g_coalesce[EVT_MAX_ENUM];

/* Helpers de ring buffer */
static inline bool rb_empty_u16(uint16_t head, uint16_t tail) { return head == tail; }
static inline uint16_t rb_next_u16(uint16_t idx, uint16_t cap) { return (uint16_t)((idx + 1U) % cap); }

/* Politique: 
   - FAULTS: jamais drop → si plein, on écrase le plus vieux FAULT.
   - NORMAL: on drop le nouveau si plein (et on compte). */
static bool push_faults(EventMsg m){
    uint16_t next = rb_next_u16(q_faults.head, EVQ_FAULTS_CAP);
    if (next == q_faults.tail) {
        /* Plein: écrase le plus vieux, on avance tail */
        q_faults.tail = rb_next_u16(q_faults.tail, EVQ_FAULTS_CAP);
        q_faults.stats.dropped++; /* on compte quand même, utile en télémétrie */
    }
    q_faults.buf[q_faults.head] = m;
    q_faults.head = next;
    q_faults.stats.pushed++;
    return true;
}

static bool push_normal(EventMsg m){
    uint16_t next = rb_next_u16(q_normal.head, EVQ_NORMAL_CAP);
    if (next == q_normal.tail) {
        q_normal.stats.dropped++; /* on drop le nouveau */
        return false;
    }
    q_normal.buf[q_normal.head] = m;
    q_normal.head = next;
    q_normal.stats.pushed++;
    return true;
}

/* Coalescence: regarde si un event du même type est déjà en file (sans payload, volontaire) */
static bool already_queued(EvQueueId qid, EventType t){
    if (qid == EVQ_FAULTS) {
        for (uint16_t i = q_faults.tail; i != q_faults.head; i = rb_next_u16(i, EVQ_FAULTS_CAP))
            if (q_faults.buf[i].type == t) return true;
    } else {
        for (uint16_t i = q_normal.tail; i != q_normal.head; i = rb_next_u16(i, EVQ_NORMAL_CAP))
            if (q_normal.buf[i].type == t) return true;
    }
    return false;
}

/* API */
void evq_init(void){
    memset(&q_normal, 0, sizeof(q_normal));
    memset(&q_faults, 0, sizeof(q_faults));

    /* Par défaut: coalesce ON pour les fronts et requêtes transition; OFF pour timeouts/faute */
    memset(g_coalesce, 0, sizeof(g_coalesce));
    g_coalesce[EVT_TH_ON] = 1;
    g_coalesce[EVT_TH_OFF] = 1;
    g_coalesce[EVT_TRANSITION_REQ] = 1;
    g_coalesce[EVT_PROVIDER_TO_ELEC] = 1;
    g_coalesce[EVT_PROVIDER_TO_GAS] = 1;
}

bool evq_set_coalesce(EventType type, bool enable){
    if (type <= 0 || type >= EVT_MAX_ENUM) return false;
    g_coalesce[type] = (uint8_t)(enable ? 1 : 0);
    return true;
}

bool evq_push(EvQueueId qid, EventType type, EventArg arg){
    if (type <= 0 || type >= EVT_MAX_ENUM) return false;

    EventMsg m = { .type = type, .arg = arg };

    /* Coalescence: on évite les doublons en file pour certains types */
    if (g_coalesce[type] && already_queued(qid, type)) {
        if (qid == EVQ_FAULTS) q_faults.stats.coalesced++;
        else                   q_normal.stats.coalesced++;
        return true; /* coalescé, considéré "accepté" */
    }

    /* Route vers la bonne file */
    switch (qid) {
        case EVQ_FAULTS: return push_faults(m);
        case EVQ_NORMAL: return push_normal(m);
        default: return false;
    }
}

/* Pop: d’abord FAULTS, puis NORMAL. Retourne false si rien à lire. */
bool evq_pop_next(EventMsg* out){
    if (!out) return false;

    /* FAULTS prioritaire */
    if (!rb_empty_u16(q_faults.head, q_faults.tail)) {
        *out = q_faults.buf[q_faults.tail];
        q_faults.tail = rb_next_u16(q_faults.tail, EVQ_FAULTS_CAP);
        q_faults.stats.popped++;
        return true;
    }
    /* Puis NORMAL */
    if (!rb_empty_u16(q_normal.head, q_normal.tail)) {
        *out = q_normal.buf[q_normal.tail];
        q_normal.tail = rb_next_u16(q_normal.tail, EVQ_NORMAL_CAP);
        q_normal.stats.popped++;
        return true;
    }
    return false;
}

void evq_get_stats(EvQueueId qid, EvQueueStats* out){
    if (!out) return;
    if (qid == EVQ_FAULTS) { *out = q_faults.stats; }
    else { *out = q_normal.stats; }
}

void evq_note_ignored(EventType type){
    (void)type;
    /* Option: compter par type. Ici on incrémente juste un compteur global NORMAL. */
    q_normal.stats.ignored++;
}
