/**
* Autore: DJITSOP FUOGOUK LOIC STEVE
 * Firmware: ECOLUMIERE BLE MESH ESP32
 * Sistema: Gestione centrale Ecolumiere
 */

#ifndef ECOLUMIERE_SYSTEM_H
#define ECOLUMIERE_SYSTEM_H

#include "esp_err.h"
#include <stdbool.h>
#include "slave_role.h"

/**
 * @brief Configurazione del sistema
 */
typedef struct {
 bool use_real_sensor;      ///< Utilizza sensore reale invece di simulazione
 bool enable_zero_cross;    ///< Abilita rilevamento zero-cross reale
} system_config_t;


/**
 * @brief Inizializza il sistema Ecolumiere
 * @return ESP_OK se successo, errore altrimenti
 */
esp_err_t ecolumiere_system_init(void);

/**
 * @brief Avvia il sistema Ecolumiere
 * @return ESP_OK se successo, errore altrimenti
 */
esp_err_t ecolumiere_system_start(void);

/**
 * @brief Ferma il sistema Ecolumiere
 */
void ecolumiere_system_stop(void);

/**
 * @brief Verifica se il sistema è pronto
 * @return true se sistema pronto, false altrimenti
 */
bool ecolumiere_system_is_ready(void);

/**
 * @brief Imposta la configurazione del sistema
 * @param config Configurazione da applicare
 */
void ecolumiere_system_set_config(system_config_t config);

/**
 * @brief Ottiene la configurazione corrente
 * @return Configurazione sistema corrente
 */
system_config_t ecolumiere_system_get_config(void);

/**
 * @brief Gestisce comandi ricevuti per NodoLampada
 * @param command Comando da applicare
 */
void ecolumiere_handle_nodo_lampada_command(const NodoLampada *command);

/**
 * @brief Test completo del sistema con dati reali
 * @note Testa tutti i sottosistemi senza simulazione
 */
void ecolumiere_system_real_test(void);

#endif // ECOLUMIERE_SYSTEM_H