/**
 * Autore: DJITSOP FUOGOUK LOIC STEVE
 * Firmware: ECOLUMIERE BLE MESH ESP32
 * Modulo: Data Recorder - Sistema di registrazione dati su NVS
 * Descrizione: Gestisce la registrazione di dati storici con buffer RAM e scrittura differita su flash
 */

#ifndef DATARECORDER_H
#define DATARECORDER_H

#include <stdint.h>
#include <stdbool.h>
#include "slave_role.h"

/************************************************
 * PUBLIC DEFINES AND MACRO                     *
 ***********************************************/
#define HISTORY_INVALID_SESSION_ID              0xFF

/************************************************
* PUBLIC STRUCTURE DECLARATIONS AND TYPEDEF     *
************************************************/

/**
 * @brief Struttura per un record storico
 * @field session_id: Identificatore unico della sessione corrente
 * @field timestamp: Timestamp in secondi del record
 * @field value: Valore del dato registrato
 * @field spare: Byte di allineamento per word alignment
 * @field counter: Contatore progressivo del record
 */
typedef struct {
  uint8_t session_id;
  uint32_t timestamp;
  uint8_t value;
  uint8_t spare;
  uint8_t counter;
} history_record_t;

/************************************************
* PUBLIC PROTOTYPES                             *
************************************************/

/**
 * @brief Inizializza il modulo Data Recorder
 * @desc Configura NVS, recupera gli indici salvati, inizializza buffer RAM
 *       e genera un session ID unico. Gestisce automaticamente il recovery
 *       in caso di corruzione NVS.
 */
void data_recorder_init(void);

/**
 * @brief Aggiunge un record al buffer RAM per scrittura differita
 * @desc Inserisce il dato in un buffer circolare in RAM. La scrittura
 *       su flash avviene automaticamente tramite data_recorder_task()
 * @param value: Valore da salvare nel record
 * @return true: Record accettato in coda, false: Buffer RAM pieno
 */
bool data_recorder_enqueue(uint8_t value);

/**
 * @brief Versione legacy per aggiungere record al buffer RAM
 * @desc Funzionalmente equivalente a data_recorder_enqueue(), mantenuta
 *       per compatibilità con codice esistente
 * @param value: Valore da salvare nel record
 */
void data_recorder_push_history_data(uint8_t value);

/**
 * @brief Task principale di gestione scritture su flash
 * @desc Controlla periodicamente le condizioni per il flush del buffer RAM
 *       su flash NVS. Gestisce scritture basate su threshold e timeout.
 *       Deve essere chiamato periodicamente nel loop principale.
 */
void data_recorder_task(void);

/**
 * @brief Legge i record storici dalla flash in sequenza
 * @desc Permette di scorrere tutti i record salvati in ordine cronologico.
 *       Utilizza uno stato interno per mantenere la posizione di lettura.
 * @param record: Puntatore a struttura dove salvare il record letto
 * @param reset: Se true, ricomincia dalla lettura del primo record disponibile
 * @return true: Record letto correttamente, false: Fine records o errore
 */
bool data_recorder_pull_history_data(history_record_t *record, bool reset);

/**
 * @brief Cancella completamente tutti i dati storici
 * @desc Svuota buffer RAM, cancella tutti i record dalla flash NVS
 *       e resetta gli indici. Operazione distruttiva irreversibile.
 */
void data_recorder_clear_history_data(void);

/**
 * @brief Restituisce statistiche sullo stato del Data Recorder
 * @desc Fornisce informazioni sul numero di record in RAM e in flash
 *       per monitoraggio e debug del sistema
 * @param buffer_count: Puntatore per conteggio record in buffer RAM
 * @param flash_count: Puntatore per conteggio record salvati in flash
 */
void data_recorder_get_stats(uint32_t *buffer_count, uint32_t *flash_count);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////// NUOVE FUNZIONI ////////////////////////////////////////////////////


typedef enum {
 RECORD_TYPE_PWM = 0,
 RECORD_TYPE_LAMPADA_FULL = 1,
 RECORD_TYPE_SENSORS = 2,
 RECORD_TYPE_EVENTS = 3
} record_type_t;

typedef enum {
 EVENT_POWER_ON = 0,
 EVENT_POWER_OFF = 1,
 EVENT_COMMAND_RECEIVED = 2,
 EVENT_SENSOR_UPDATE = 3
} event_type_t;

bool data_recorder_enqueue_lampada_snapshot(const NodoLampada *lampada);


bool data_recorder_enqueue_sensor_data(float temperature, float humidity, uint32_t lux);


bool data_recorder_enqueue_lampada_event(event_type_t event_type, const char* description);

bool data_recorder_is_overwriting(void);

void data_recorder_get_detailed_stats(uint32_t *buffer_count, uint32_t *flash_count, uint32_t *total_written, bool *is_overwriting);


#endif //DATARECORDER_H