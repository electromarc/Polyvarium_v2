// hw_inputs_stm32.h
#pragma once
#include <stdbool.h>
#include "stm32h5xx_hal.h"
#include "main.h"

// Thermostat (contact sec). Exemple: PA0
#define TH_GPIO_Port      INPUT_1_GPIO_Port
#define TH_Pin            INPUT_1_Pin
#define TH_ACTIVE_LOW     1  

// Provider bi-énergie (ex: 1 = ELEC, 0 = GAS). 
#define PROV_GPIO_Port    INPUT_2_GPIO_Port
#define PROV_Pin          INPUT_2_Pin
#define PROV_ACTIVE_LOW   0

// Sélecteur utilisateur 3 positions exclusives (fils A/B/C)
#define MODEA_GPIO_Port   INPUT_3_GPIO_Port
#define MODEA_Pin         GPIO_PIN_3
#define MODEA_ACTIVE_LOW  0

#define MODEB_GPIO_Port   INPUT_4_GPIO_Port
#define MODEB_Pin         INPUT_4_Pin
#define MODEB_ACTIVE_LOW  0

#define MODEC_GPIO_Port   INPUT_5_GPIO_Port
#define MODEC_Pin         INPUT_5_Pin
#define MODEC_ACTIVE_LOW  0

// Helper "non câblé"
#ifndef NC_Pin
#define NC_Pin ((uint16_t)0)
#endif
