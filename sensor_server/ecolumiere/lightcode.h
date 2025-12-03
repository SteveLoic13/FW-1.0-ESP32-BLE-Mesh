/**
* Autore: DJITSOP FUOGOUK LOIC STEVE
 * Firmware: ECOLUMIERE BLE MESH ESP32
 * Modulo: Lightcode - Sistema comunicazione ottica
 */

#ifndef LIGHTCODE_H
#define LIGHTCODE_H

#include <stdint.h>
#include <stdbool.h>

/************************************************
 * PUBLIC DEFINES AND MACRO                     *
 ************************************************/

#define LIGHT_CODE_ONE                   0x55    // Codice identificativo dispositivo master
#define LIGHT_CODE_MASK                  0x7E    // Maschera 7-bit per codifica dati
#define LIGHT_CODE_ZERO                  0x00    // Codice zero (nessuna comunicazione)

#define SENSE_QUEUE_SIZE                 120     // Dimensione buffer campioni
#define MEAN_SIZE                        4       // Dimensione filtro media mobile

/************************************************
 * PUBLIC PROTOTYPES                           *
 ************************************************/

/**
 * @brief Inizializza il sistema di comunicazione ottica
 */
void light_code_init(void);

/**
 * @brief Reset coda campioni per nuova acquisizione
 */
void light_code_reset_queue(void);

/**
 * @brief Acquisisce campioni e applica filtraggio
 */
void light_code_pickup(void);

/**
 * @brief Decodifica codice ricevuto da segnale ottico
 * @return Codice decodificato (0 in caso di errore)
 */
uint8_t light_code_check(void);

#endif // LIGHTCODE_H