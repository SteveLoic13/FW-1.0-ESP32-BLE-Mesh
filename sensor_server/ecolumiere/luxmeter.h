/**
* Autore: DJITSOP FUOGOUK LOIC STEVE
 * Firmware: ECOLUMIERE BLE MESH ESP32
 * Sistema misurazione intensità luminosa - Versione reale
 */

#ifndef LUXMETER_H
#define LUXMETER_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Tipi di misurazione luminosa supportati
 */
typedef enum {
 LUX_MEASURE_NATURAL,     ///< Luce naturale (senza contributo lampada)
 LUX_MEASURE_ENVIRONMENT, ///< Luce ambiente (con contributo lampada)
 LUX_MEASURE_NODE_ID      ///< Identificazione nodo
} luxmeter_measure_t;

/**
 * @brief Inizializza il sistema di misurazione luminosa
 */
void luxmeter_init(void);

/**
 * @brief Acquisisce una misurazione luminosa
 * @param measure Tipo di misurazione
 * @param pwm_level Livello PWM corrente per compensazione
 * @param lux Puntatore per valore lux misurato
 * @param index Puntatore per indice di debug
 */
void luxmeter_pickup(luxmeter_measure_t measure, uint16_t pwm_level, uint32_t *lux, uint32_t *index);

/**
 * @brief Avvia l'acquisizione continua
 */
void luxmeter_start_acquisition(void);

/**
 * @brief Arresta l'acquisizione continua
 */
void luxmeter_stop_acquisition(void);

#endif // LUXMETER_H