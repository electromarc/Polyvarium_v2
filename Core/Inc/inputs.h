#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "events.h"

/* Fréquence d’échantillonnage du debounce (ms) */
#ifndef INP_TICK_MS
#define INP_TICK_MS 1U
#endif

/* Durées de stabilisation (ms) */
#ifndef INP_DEBOUNCE_MS
#define INP_DEBOUNCE_MS 30U     /* 3 échantillons à 10 ms */
#endif

#ifndef INP_PROVIDER_STABLE_MS
#define INP_PROVIDER_STABLE_MS 2000U  /* provider: stabilité 2 s avant event */
#endif

#ifndef INP_MODE_STABLE_MS
#define INP_MODE_STABLE_MS 200U /* sélecteur utilisateur plus rapide */
#endif

/* Types d’entrées gérées ici */
typedef struct {
    /* Vrai si l’entrée est active à 0 (pull-up + contact à la masse) */
    uint8_t thermostat_active_low;
    uint8_t provider_active_low;     /* 1: ELEC, 0: GAS (ou l’inverse, voir mapping) */
    uint8_t modeA_active_low;        /* mode sélecteur: ELEGAS/BI: trois fils, un par position */
    uint8_t modeB_active_low;
    uint8_t modeC_active_low;
} InputsConfig;

/* Initialisation: stocke la config, remet le debounce.
   Ne génère PAS d’événements. */
void inputs_init(const InputsConfig* cfg);

/* À appeler à chaque tick de INP_TICK_MS (ex: 10 ms).
   Échantillonne, applique le debounce et pousse des événements:
     - EVT_TH_ON / EVT_TH_OFF
     - EVT_PROVIDER_TO_ELEC / EVT_PROVIDER_TO_GAS (après stabilité INP_PROVIDER_STABLE_MS)
     - EVT_USER_MODE_ELEC / EVT_USER_MODE_GAS / EVT_USER_MODE_BI (après stabilité INP_MODE_STABLE_MS)
*/
void inputs_tick(void);

/* Option: seed initial pour éviter un déluge d’événements au boot.
   Lit l’état matériel brut et le prend comme 'stable' sans pousser d’events. */
void inputs_seed_from_hw(void);

/* Hooks HARDWARE à fournir ailleurs: lecture niveau logique brut (0/1)
   Ces fonctions DOIVENT être rapides et non bloquantes. */
bool hw_read_thermostat_raw(void);
bool hw_read_provider_raw(void);  /* ex: 1=ELEC, 0=GAS (ou l’inverse, selon ton câblage) */
bool hw_read_modeA_raw(void);     /* sélecteur utilisateur (3 entrées exclusives) */
bool hw_read_modeB_raw(void);
bool hw_read_modeC_raw(void);
