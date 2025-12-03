//
// Created by Admin on 20/10/2025.
//
#ifndef ECOLUMIERE_H
#define ECOLUMIERE_H

#include <stdint.h>
#include <stdbool.h>

#define CODE_WINDOW_SIZE                (25)
#define CODE_THRESHOLD_HIGH             (2)
#define CODE_THRESHOLD_LOW              (1)


/**
* @brief enum sorgenti di misura
*/
typedef enum lux_source_t
{
  LUX_SOURCE_NATURAL = 1,
  LUX_SOURCE_ENVIRONMENT,
  LUX_SOURCE_DEVICE_ID
} lux_source_t;

typedef struct algo_sched_event_t
{
  union
  {
    uint32_t measure;
    uint8_t code;
  };
  uint8_t source;
} algo_sched_event_t;

/**
* @brief struttura con i parametri dell'algo da salvare
*/
typedef struct __attribute__((packed))
{
  uint32_t target_lux;
  float efficiency;
  float distance;
  uint32_t in_pl;
  float dimm_step;
  float perc_min;
  float transparency;
  uint16_t current_pwm_level;
  uint16_t crc;  // deve essere l'ultimo campo della struttura
} algo_config_data_t;


/**
* @brief struttura di anagrafica completa
*/
typedef struct __attribute__((packed))
{
  uint16_t unicast_addr;
  uint16_t device_id;
  uint16_t company_id;
  uint16_t config_crc;
  char device_name[32];
  char serial_number[20];
  uint8_t mac_address[6];
} ecl_registry_t;

/**
* @brief struttura di dati live
*/
typedef struct __attribute__((packed))
{
  float natural;
  float env;
  float lamp_lux;
  uint8_t duty_cycle;
} ecl_live_t;

/**
* @brief Inizializza il modulo per la regolazione luminosa
*/
void ecolumiere_init(void);

/**
* @brief aggiorna la media con una nuova misura
*/
void ecolumiere_update_lux(void * p_event_data, uint16_t event_size);

/**
* @brief Processa l'algoritmo con le misure aggiornate
*/
void ecolumiere_algo_process(void);

/**
* @brief imposta il target
*/
void ecolumiere_set_target(int32_t target);

/**
* @brief Ritorna true se esiste una configurazione valida
*/
bool ecolumiere_has_valid_config(void);

/**
* @brief Legge i parametri di anagrafica
*/
void ecolumiere_get_registry(uint16_t * device_id, uint16_t * company_id, uint16_t * crc);

/**
* @brief Imposta i parametri di anagrafica
*/
void ecolumiere_set_registry(uint16_t device_id, uint16_t company_id);

/**
* @brief Leggi i parametri di configurazione dell'algoritmo
*/
void ecolumiere_get_algo_config(algo_config_data_t * algo_config);

/**
* @brief Imposta i parametri di configurazione dell'algoritmo
*/
void ecolumiere_set_algo_config(algo_config_data_t * algo_config);

/**
* @brief Task per processare eventi dalla coda scheduler
*/
void ecolumiere_scheduler_task(void *pvParameters);

/**
 * @brief Salva il livello PWM corrente nella configurazione persistente
 * @param pwm_level: Livello PWM da salvare (0-32)
 */
void ecolumiere_save_current_pwm(uint16_t pwm_level);

void ecolumiere_test_algo_status(void);


void ecolumiere_test_avg_status(void);

/**
 * @brief Gestisce comandi diretti dal Gateway via BLE Mesh
 * @param level: Livello PWM da impostare (0-32)
 * @param is_override: true = comando diretto, false = suggerimento algoritmo
 */
void ecolumiere_handle_mesh_command(uint8_t level, bool is_override);

// Aggiungi questi prototipi alla fine di ecolumiere.h

/**
 * @brief Restituisce true se è attivo un override mesh
 */
bool ecolumiere_is_mesh_override_active(void);

/**
 * @brief Restituisce il livello di override corrente
 */
uint8_t ecolumiere_get_mesh_override_level(void);

/**
 * @brief Restituisce il tempo rimanente per l'override (secondi)
 */
uint32_t ecolumiere_get_mesh_override_remaining(void);

// Aggiungi questi prototipi alla fine di ecolumiere.h

/**
 * @brief Test manuale dell'algoritmo con valori specifici
 */
void ecolumiere_test_algorithm(uint32_t natural_lux, uint32_t env_lux, uint32_t target_lux);

/**
 * @brief Mostra stato dettagliato dell'algoritmo
 */
void ecolumiere_show_algorithm_status(void);

#endif //ECOLUMIERE_H