// hw_inputs_stm32.c
#include "hw_inputs_stm32.h"
#include "inputs.h"

// Lis brut: 1 si le GPIO est "SET", 0 sinon.
// Pas d’inversion ici. L’inversion se fait dans inputs.c (active_low).
bool hw_read_thermostat_raw(void) {
    return (HAL_GPIO_ReadPin(TH_GPIO_Port, TH_Pin) == GPIO_PIN_SET);
}

bool hw_read_provider_raw(void) {
    return (HAL_GPIO_ReadPin(PROV_GPIO_Port, PROV_Pin) == GPIO_PIN_SET);
}

static inline bool read_or_idle(GPIO_TypeDef* port, uint16_t pin) {
    if (pin == NC_Pin) return false; // si non câblé: niveau bas
    return (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET);
}

bool hw_read_modeA_raw(void) { return read_or_idle(MODEA_GPIO_Port, MODEA_Pin); }
bool hw_read_modeB_raw(void) { return read_or_idle(MODEB_GPIO_Port, MODEB_Pin); }
bool hw_read_modeC_raw(void) { return read_or_idle(MODEC_GPIO_Port, MODEC_Pin); }
