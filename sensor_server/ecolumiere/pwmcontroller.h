/**
 * Autore: DJITSOP FUOGOUK LOIC STEVE
 * Firmware: ECOLUMIERE BLE MESH ESP32
 * Modulo: PWM Controller - Sistema controllo illuminazione BLE Mesh
 * Descrizione: Implementa il controllo PWM per illuminazione con logica slot-based
 *              ispirata ai sistemi Nordic per gestione sincronizzata
 */

#ifndef PWMCONTROLLER_H
#define PWMCONTROLLER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/************************************************
 * PUBLIC DEFINES AND MACRO                     *
 ************************************************/

#define LIGHT_MAX_LEVEL                 32      // Livello massimo dimming (0-32)
#define SLOT_COUNT                      10      // Numero slot per ciclo completo
#define PWM_SEQUENCE_LEN                32      // Lunghezza sequenza PWM (campionamenti)
#define PWM_MAX_VALUE                   8191    // Valore massimo PWM 13-bit (0-8191)

// Definizioni slot temporali per gestione eventi
#define DEVICE_ID_SLOT                  0       // Slot comunicazione ID dispositivo
#define NATURAL_MEASURE_SLOT            2       // Slot misurazione luce naturale
#define ENV_MEASURE_SLOT                6       // Slot misurazione luce ambiente

// Tipi di evento per gestione sequenze PWM
#define DEFAULT_EVENT                   0x00    // Evento normale: regolazione luminosità
#define NATURAL_MEASURE_EVENT           0x01    // Evento misurazione luce naturale
#define ENV_MEASURE_EVENT               0x02    // Evento misurazione luce ambiente
#define DEVICE_ID_EVENT                 0x04    // Evento comunicazione ID dispositivo
#define MEASURE_INVALID                 0xFFFF

/************************************************
 * PUBLIC TYPES DECLARATIONS                   *
 ************************************************/

/**
 * @brief Direzione del dimming luminoso
 * @enum LIGHT_DIMMING_UNDEF: Stato non definito
 * @enum LIGHT_DIMMING_DOWN: Dimming in diminuzione
 * @enum LIGHT_DIMMING_UP: Dimming in aumento
 */
typedef enum {
    LIGHT_DIMMING_UNDEF,
    LIGHT_DIMMING_DOWN,
    LIGHT_DIMMING_UP
} light_dimm_t;

/**
 * @brief Ruolo del dispositivo nella rete
 * @enum ROLE_ID_BROADCASTER: Dispositivo MASTER che trasmette codici
 * @enum ROLE_ID_RECEIVER: Dispositivo SLAVE che riceve codici
 */
typedef enum {
    ROLE_ID_BROADCASTER = 1,
    ROLE_ID_RECEIVER = 0
} device_id_role_t;

/************************************************
 * PUBLIC PROTOTYPES                           *
 ************************************************/

/**
 * @brief Inizializza il sistema PWM controller
 * @desc Configura hardware PWM, sistema a slot temporali, buffer sequenze
 *       e timer per gestione eventi periodici. Deve essere chiamato all'avvio.
 * @return ESP_OK se inizializzazione riuscita, codice errore altrimenti
 */
esp_err_t pwmcontroller_init(void);

/**
 * @brief Imposta il duty cycle target per il dimming
 * @desc Definisce il livello luminoso target verso cui eseguire il fade graduale.
 *       Il valore viene salvato anche nel data recorder per tracciamento storico.
 * @param duty_cycle: Valore target tra 0 e LIGHT_MAX_LEVEL
 */
void pwm_set_duty_cycle(uint32_t duty_cycle);

/**
 * @brief Applica transizione graduale verso livello target
 * @desc Esegue fade smooth tra livello corrente e target, chiamato periodicamente
 *       dal sistema a slot per garantire transizioni fluide.
 */
void pwm_fade(void);

/**
 * @brief Restituisce il ruolo corrente del dispositivo
 * @desc Indica se il dispositivo opera come MASTER o SLAVE nella comunicazione.
 * @return Ruolo corrente del dispositivo (sempre SLAVE in questa implementazione)
 */
device_id_role_t pwm_get_id_role(void);

/**
 * @brief Imposta il ruolo del dispositivo
 * @desc Permette di configurare il ruolo (solo SLAVE supportato in questa versione).
 * @param role: Ruolo da impostare (ROLE_ID_RECEIVER per SLAVE)
 */
void pwm_set_id_role(device_id_role_t role);

/**
 * @brief Ferma il sistema PWM
 * @desc Disattiva timer slot e resetta stato interno. Utilizzato per shutdown.
 */
void pwm_stop(void);

/**
 * @brief Restituisce il livello luminoso corrente
 * @desc Fornisce il valore attuale del dimming tra 0 e LIGHT_MAX_LEVEL.
 * @return Livello corrente di illuminazione
 */
uint16_t pwmcontroller_get_current_level(void);

/**
 * @brief Applica duty cycle sincronizzato con zero-cross
 * @desc Funzione chiamata dall'ISR zero-cross per applicare il PWM sincronizzato
 *       con il passaggio per zero della tensione AC (phase-cutting).
 */
void pwm_apply_phase_controlled_duty(void);

/**
 * @brief Restituisce indirizzo task playback PWM
 * @desc Fornisce l'indirizzo della funzione di applicazione PWM per integrazione
 *       con sistema zero-cross detection.
 * @return Indirizzo della funzione di applicazione PWM
 */
uint32_t pwm_get_playback_task(void);

/**
 * @brief Imposta livello PWM per compatibilità BLE Mesh
 * @desc Interfaccia semplificata per controllo da BLE Mesh models.
 * @param level: Livello PWM da impostare (0-255 mappato a 0-32)
 */
void pwmcontroller_set_level(uint8_t level);

/**
 * @brief Restituisce lo slot temporale corrente
 * @desc Fornisce l'indice dello slot attualmente in esecuzione.
 * @return Numero slot corrente (0-SLOT_COUNT-1)
 */
uint8_t pwm_get_current_slot(void);

/**
 * @brief Avanza al prossimo slot temporale
 * @desc Incrementa il contatore slot in modalità circolare.
 */
void pwm_advance_slot(void);

/**
 * @brief Imposta livello output PWM diretto
 * @desc Funzione per impostazione diretta livello (utilizzata per slot sicurezza).
 * @param level: Livello PWM da impostare
 */
void pwm_set_output_level(uint8_t level);


bool is_pwm_initialized(void);



uint8_t convert_intensity_to_pwm(uint16_t intensity);

#endif // PWMCONTROLLER_H