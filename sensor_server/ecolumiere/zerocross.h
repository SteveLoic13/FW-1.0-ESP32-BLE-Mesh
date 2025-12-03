/**
 * Autore: DJITSOP FUOGOUK LOIC STEVE
 * Firmware: ECOLUMIERE BLE MESH ESP32
 * Zero-cross detection ottimizzata con hardware dedicato
 */
#pragma once
#include "esp_err.h"
#include <stdbool.h>


#ifndef ZEROCROSS_H
#define ZEROCROSS_H

// Configurazione rete elettrica
#define AC_FREQUENCY                    50      // Hz
#define SEMI_PERIOD_US                  (1000000 / (AC_FREQUENCY * 2)) // 10000μs


esp_err_t zero_cross_init(void);

void zero_cross_enable(void);

void zero_cross_disable(void);

#endif // ZEROCROSS_H