/**
 * Autore: DJITSOP FUOGOUK LOIC STEVE
 * Firmware: ECOLUMIERE BLE MESH ESP32
 * Implementazione: Data Recorder - Sistema di registrazione dati su NVS
 */

#include "datarecorder.h"
#include "slave_role.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

/************************************************
 * DEFINES AND MACRO                            *
 ************************************************/
#define HISTORY_MAX_RECORDS       100    // Capacità massima storage flash (~6KB)
#define HISTORY_RAM_BUFFER_SIZE   20      // Dimensione buffer circolare in RAM
#define HISTORY_FLUSH_INTERVAL_MS 5000    // Intervallo flush automatico (5s)
#define HISTORY_FLUSH_THRESHOLD   16      // Threshold flush a 80% del buffer
#define HISTORY_NAMESPACE         "ecl_history"  // Namespace NVS
#define HISTORY_KEY_PREFIX        "hist"         // Prefisso chiavi record

/************************************************
 * PRIVATE OBJECTS OF THE MODULE                *
 ************************************************/
static const char* TAG = "DATARECORDER";

// Handle e stato NVS
static nvs_handle_t history_handle;
static uint32_t history_write_index = 0;
static uint32_t history_count = 0;
static uint8_t current_session_id = 0;

// Buffer circolare in RAM per scritture differite
static history_record_t ram_buffer[HISTORY_RAM_BUFFER_SIZE];
static uint32_t ram_buffer_head = 0;
static uint32_t ram_buffer_tail = 0;
static uint32_t ram_buffer_count = 0;
static uint32_t last_flush_time = 0;

// ⭐ NUOVA VARIABILE PER CONTROLLO SOVRASCRITTURA
static bool overwrite_warning_issued = false;

/************************************************
 * FUNCTIONS IMPLEMENTATION                     *
 ************************************************/

/**
 * @brief Inizializza il sistema Data Recorder
 * @desc Configura la memoria NVS, recupera lo stato precedente,
 *       inizializza il buffer RAM e genera un session ID unico.
 *       Implementa meccanismi di recovery in caso di corruzione NVS.
 */
void data_recorder_init(void) {
    static bool first = true;
    esp_err_t err;

    if (first) {
        // Inizializzazione NVS con recovery da corruzione
        err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_LOGW(TAG, "NVS corrupted, performing recovery...");
            nvs_flash_erase();
            err = nvs_flash_init();
        }

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "NVS initialization failed: %s", esp_err_to_name(err));
            return;
        }

        // Apertura namespace dedicato per i record storici
        err = nvs_open(HISTORY_NAMESPACE, NVS_READWRITE, &history_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open history namespace: %s", esp_err_to_name(err));
            return;
        }

       // ✅ USA SOLO LA PARTE MAC DEL DEVICE NAME COME CHIAVE UNICA
        const slave_identity_t *identity = slave_node_get_identity();
        char storage_key[13];  // Solo la parte MAC: 12 caratteri

         // Estrae "3C8A1F80AE36" da "ECL_3C8A1F80AE36"
        const char *device_name = identity->device_name;
        if (strncmp(device_name, "ECL_", 4) == 0) {
            // Prende solo la parte dopo "ECL_"
            strncpy(storage_key, device_name + 4, sizeof(storage_key) - 1);
            storage_key[sizeof(storage_key) - 1] = '\0'; // Null terminator
        } else {
            // Fallback: usa tutto il nome (troncato se necessario)
            strncpy(storage_key, device_name, sizeof(storage_key) - 1);
            storage_key[sizeof(storage_key) - 1] = '\0';
        }

        // ✅ USA LA STESSA CHIAVE PER TUTTO - è unica per dispositivo
        // Recupero indici di scrittura e conteggio dall'NVS
        nvs_get_u32(history_handle, storage_key, &history_write_index);

        // Per il count, usa la stessa chiave con suffisso se necessario,
        // oppure calcolalo dal write_index
        history_count = history_write_index; // O qualsiasi altra logica

        // Verifica integrità indici e reset se corrotti
        if (history_write_index > 1000000 || history_count > HISTORY_MAX_RECORDS) {
            ESP_LOGW(TAG, "NVS indices corrupted, resetting...");
            history_write_index = 0;
            history_count = 0;
            nvs_set_u32(history_handle, storage_key, history_write_index);
            //nvs_set_u32(history_handle, count_key, history_count);
            nvs_commit(history_handle);
        }

        // Inizializzazione buffer circolare in RAM
        ram_buffer_head = 0;
        ram_buffer_tail = 0;
        ram_buffer_count = 0;
        last_flush_time = esp_timer_get_time() / 1000;

        // Generazione session ID unico basato sul timer
        current_session_id = (uint8_t)(esp_timer_get_time() & 0xFF);
        if (current_session_id == HISTORY_INVALID_SESSION_ID) {
            current_session_id = 0x01;
        }

        first = false;

        ESP_LOGI(TAG, "Data Recorder initialized for %s - Records: %d, Index: %d, Session: 0x%02X",
                 storage_key, history_count, history_write_index, current_session_id);
    }
}

/**
 * @brief Aggiunge un record al buffer RAM per scrittura differita
 * @desc Crea un nuovo record con timestamp e session ID, lo inserisce
 *       nel buffer circolare in RAM. La scrittura su flash avverrà
 *       successivamente tramite data_recorder_task()
 * @param value: Valore del dato da registrare
 * @return true se il record è stato accettato, false se buffer pieno
 */
bool data_recorder_enqueue(uint8_t value) {
    if (ram_buffer_count >= HISTORY_RAM_BUFFER_SIZE) {
        ESP_LOGW(TAG, "RAM buffer full, record dropped: %d", value);
        return false;
    }

    // Creazione nuovo record nel buffer
    history_record_t *record = &ram_buffer[ram_buffer_head];
    record->session_id = current_session_id;
    record->timestamp = (uint32_t)(esp_timer_get_time() / 1000000);
    record->value = value;
    record->counter = history_count + ram_buffer_count;
    record->spare = 0;

    // Aggiornamento indici buffer circolare
    ram_buffer_head = (ram_buffer_head + 1) % HISTORY_RAM_BUFFER_SIZE;
    ram_buffer_count++;

    ESP_LOGD(TAG, "Record enqueued: value=%d, buffer_count=%d/%d",
             value, ram_buffer_count, HISTORY_RAM_BUFFER_SIZE);
    return true;
}

/**
 * @brief Versione legacy per aggiunta record al buffer RAM
 * @desc Funzionalmente equivalente a data_recorder_enqueue()
 *       mantenuta per compatibilità
 */
void data_recorder_push_history_data(uint8_t value) {
    if (ram_buffer_count >= HISTORY_RAM_BUFFER_SIZE) {
        ESP_LOGW(TAG, "RAM buffer full, record dropped: %d", value);
        return;
    }

    history_record_t *record = &ram_buffer[ram_buffer_head];
    record->session_id = current_session_id;
    record->timestamp = (uint32_t)(esp_timer_get_time() / 1000000);
    record->value = value;
    record->counter = history_count + ram_buffer_count;
    record->spare = 0;

    ram_buffer_head = (ram_buffer_head + 1) % HISTORY_RAM_BUFFER_SIZE;
    ram_buffer_count++;

    ESP_LOGD(TAG, "Record enqueued in RAM: value=%d, buffer_count=%d/%d",
             value, ram_buffer_count, HISTORY_RAM_BUFFER_SIZE);
}

/**
 * @brief Task principale per gestione scritture su flash
 * @desc Monitora condizioni per flush automatico: threshold del buffer
 *       o timeout. Gestisce la scrittura batch dei record su NVS
 *       e l'aggiornamento degli indici.
 */
void data_recorder_task(void) {

    uint32_t current_time = esp_timer_get_time() / 1000;

    // Condizioni per attivare il flush
    bool buffer_threshold = (ram_buffer_count >= HISTORY_FLUSH_THRESHOLD);
    bool timeout_elapsed = ((current_time - last_flush_time) >= HISTORY_FLUSH_INTERVAL_MS);
    bool minimum_time_elapsed = (ram_buffer_count > 0 && (current_time - last_flush_time) >= 1000);

    if ((buffer_threshold || timeout_elapsed) && minimum_time_elapsed) {
        if (ram_buffer_count == 0) {
            return;
        }

        ESP_LOGD(TAG, "Flushing %d records to flash", ram_buffer_count);
        uint32_t successful_writes = 0;

        // ✅ USA LA STESSA CHIAVE UNICA
        const slave_identity_t *identity = slave_node_get_identity();
        char storage_key[13];

        // Estrae "3C8A1F80AE36" da "ECL_3C8A1F80AE36"
        const char *device_name = identity->device_name;
        if (strncmp(device_name, "ECL_", 4) == 0) {
            strncpy(storage_key, device_name + 4, sizeof(storage_key) - 1);
            storage_key[sizeof(storage_key) - 1] = '\0';
        } else {
            strncpy(storage_key, device_name, sizeof(storage_key) - 1);
            storage_key[sizeof(storage_key) - 1] = '\0';
        }

        // ⭐ CONTROLLO SOVRASCRITTURA - PRIMA DELLA SCRITTURA
        if (history_count >= HISTORY_MAX_RECORDS && !overwrite_warning_issued) {
            ESP_LOGW(TAG, "⚠️  MEMORIA PIENA NUMERO MAX RECORDS RAGGIUNTO! I record più vecchi verranno sovrascritti");
            overwrite_warning_issued = true;
        }

        // Scrittura batch di tutti i record nel buffer
        for (uint32_t i = 0; i < ram_buffer_count; i++) {
            history_record_t *record = &ram_buffer[ram_buffer_tail];

            // Generazione chiave unica per il record
            char key[20];  // Aumentato per sicurezza
            snprintf(key, sizeof(key), "%s_%03ld", HISTORY_KEY_PREFIX,
                     history_write_index % HISTORY_MAX_RECORDS);

            // ⭐ AVVISO PER OGNI SOVRASCRITTURA
            if (history_count >= HISTORY_MAX_RECORDS) {
                ESP_LOGW(TAG, "🔁 Sovrascrittura Record: %s (indice flash: %ld)",
                         key, history_write_index % HISTORY_MAX_RECORDS);
            }

            // Scrittura record su NVS
            esp_err_t err = nvs_set_blob(history_handle, key, record, sizeof(history_record_t));
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to write record %d to flash", i);
                break;
            }

            err = nvs_commit(history_handle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to commit record %d to flash", i);
                break;
            }

            // Aggiornamento indici globali
            history_write_index++;

            //history_count = (history_count < HISTORY_MAX_RECORDS) ? history_count + 1 : HISTORY_MAX_RECORDS;

            // ⭐ MODIFICA: RESET AVVISO SE TORNATO SOTTO IL LIMITE
            if (history_count < HISTORY_MAX_RECORDS) {
                history_count++;
                overwrite_warning_issued = false;  // Reset avviso
            }
            // Se history_count era già al massimo, rimane HISTORY_MAX_RECORDS

            // Salvataggio indici aggiornati
            nvs_set_u32(history_handle, storage_key, history_write_index);

            //nvs_set_u32(history_handle, count_key, history_count);
            nvs_commit(history_handle);

            successful_writes++;
            ram_buffer_tail = (ram_buffer_tail + 1) % HISTORY_RAM_BUFFER_SIZE;
        }

        // Aggiornamento stato buffer RAM
        ram_buffer_count -= successful_writes;

        if (ram_buffer_count > 0) {
            ESP_LOGW(TAG, "Partial flush: %d records remaining in buffer", ram_buffer_count);
        } else {
            // Reset completo del buffer se completamente svuotato
            ram_buffer_head = 0;
            ram_buffer_tail = 0;
        }

        last_flush_time = esp_timer_get_time() / 1000;
        ESP_LOGD(TAG, "Flush completed: %d records written, total flash: %d",
                 successful_writes, history_count);
    }

    // Log periodico dello stato ogni 30 secondi
    static uint32_t last_status_log = 0;
    if (current_time - last_status_log >= 30000) {
        // ⭐ AGGIUNGI INFO SOVRASCRITTURA NEL LOG PERIODICO
        if (history_count >= HISTORY_MAX_RECORDS) {
            ESP_LOGW(TAG, "Status - MEMORIA PIENA RECORDS MAX RAGGIUNTI - RAM: %d/%d, Flash: %d/%d (SOVRASCRITTURA ATTIVA)",
                     ram_buffer_count, HISTORY_RAM_BUFFER_SIZE,
                     history_count, HISTORY_MAX_RECORDS);
        } else {
            ESP_LOGI(TAG, "Status - RAM: %d/%d, Flash: %d/%d",
                     ram_buffer_count, HISTORY_RAM_BUFFER_SIZE,
                     history_count, HISTORY_MAX_RECORDS);
        }
        last_status_log = current_time;
    }
}

/**
 * @brief Lettura sequenziale dei record storici dalla flash
 * @desc Permette di scorrere tutti i record salvati mantenendo
 *       uno stato interno della posizione di lettura
 * @param record: Buffer dove salvare il record letto
 * @param reset: Se true, ricomincia dalla lettura del primo record
 * @return true se un record valido è stato letto, false altrimenti
 */
bool data_recorder_pull_history_data(history_record_t *record, bool reset) {
    static uint32_t current_read_index = 0;
    static uint32_t records_read = 0;

    if (reset) {
        // Calcolo punto di partenza per la lettura
        if (history_count == HISTORY_MAX_RECORDS) {
            // Buffer circolare pieno - inizia dal record più vecchio
            current_read_index = history_write_index;
        } else {
            // Buffer non pieno - inizia dal primo record
            current_read_index = 0;
        }
        records_read = 0;
        ESP_LOGD(TAG, "History read reset, start index: %d", current_read_index);
    }

    // Verifica presenza record da leggere
    if (records_read >= history_count) {
        return false;
    }

    // Lettura record dalla NVS
    char key[16];
    snprintf(key, sizeof(key), "%s_%03ld", HISTORY_KEY_PREFIX,
             (current_read_index + records_read) % HISTORY_MAX_RECORDS);

    size_t size = sizeof(history_record_t);
    esp_err_t err = nvs_get_blob(history_handle, key, record, &size);

    if (err == ESP_OK && size == sizeof(history_record_t)) {
        records_read++;
        ESP_LOGD(TAG, "History read: %s, value: %d, time: %lu",
                 key, record->value, record->timestamp);
        return true;
    }

    ESP_LOGE(TAG, "History read failed: %d, size: %d", err, size);
    return false;
}

/**
 * @brief Cancellazione completa di tutti i dati storici
 * @desc Svuota il buffer RAM, cancella tutti i record dalla flash NVS
 *       e resetta gli indici. Operazione irreversibile.
 */
void data_recorder_clear_history_data(void) {
    ESP_LOGI(TAG, "Clearing all history data");

    // ⭐ RESET VARIABILE AVVISO
    overwrite_warning_issued = false;

    // Flush preliminare di eventuali record pendenti in RAM
    if (ram_buffer_count > 0) {
        ESP_LOGI(TAG, "Flushing %d pending records before clear", ram_buffer_count);

        uint32_t successful_writes = 0;
        for (uint32_t i = 0; i < ram_buffer_count; i++) {
            history_record_t *record = &ram_buffer[ram_buffer_tail];

            char key[16];
            snprintf(key, sizeof(key), "%s_%03ld", HISTORY_KEY_PREFIX,
                     history_write_index % HISTORY_MAX_RECORDS);

            esp_err_t err = nvs_set_blob(history_handle, key, record, sizeof(history_record_t));
            if (err != ESP_OK) break;

            err = nvs_commit(history_handle);
            if (err != ESP_OK) break;

            history_write_index++;
            history_count = (history_count < HISTORY_MAX_RECORDS) ? history_count + 1 : HISTORY_MAX_RECORDS;

            nvs_set_u32(history_handle, "write_index", history_write_index);
            nvs_set_u32(history_handle, "history_count", history_count);
            nvs_commit(history_handle);

            successful_writes++;
            ram_buffer_tail = (ram_buffer_tail + 1) % HISTORY_RAM_BUFFER_SIZE;
        }

        ram_buffer_count -= successful_writes;
        if (ram_buffer_count > 0) {
            ESP_LOGW(TAG, "Partial flush: %d records remaining in buffer", ram_buffer_count);
        } else {
            ram_buffer_head = 0;
            ram_buffer_tail = 0;
        }
    }

    // Cancellazione di tutti i record dalla flash
    for (uint32_t i = 0; i < HISTORY_MAX_RECORDS; i++) {
        char key[16];
        snprintf(key, sizeof(key), "%s_%03ld", HISTORY_KEY_PREFIX, i);
        nvs_erase_key(history_handle, key);
    }

    // Reset completo di tutti gli stati
    history_write_index = 0;
    history_count = 0;
    ram_buffer_head = 0;
    ram_buffer_tail = 0;
    ram_buffer_count = 0;

    // Salvataggio stati reset
    nvs_set_u32(history_handle, "write_index", 0);
    nvs_set_u32(history_handle, "history_count", 0);
    nvs_commit(history_handle);

    ESP_LOGI(TAG, "History data cleared completely");
}

/**
 * @brief Restituisce statistiche sullo stato del Data Recorder
 * @desc Fornisce informazioni quantitative sul numero di record
 *       attualmente in RAM e salvati in flash
 */
void data_recorder_get_stats(uint32_t *buffer_count, uint32_t *flash_count) {
    if (buffer_count) *buffer_count = ram_buffer_count;
    if (flash_count) *flash_count = history_count;
}



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////// NUOVE FUNZIONI ////////////////////////////////////////////////////

/**
 * @brief Registra evento NodoLampada (VERSIONE SEMPLICE)
 */
bool data_recorder_enqueue_lampada_event(event_type_t event_type, const char* description) {
    // ✅ VERSIONE SEMPLICE - usa la funzione esistente
    uint8_t event_value = (uint8_t)event_type;
    return data_recorder_enqueue(event_value);

    ESP_LOGD(TAG, "Evento lampada registrato - Tipo: %d", event_type);
}

/**
 * @brief Registra snapshot NodoLampada (VERSIONE SEMPLICE)
 */
bool data_recorder_enqueue_lampada_snapshot(const NodoLampada *lampada) {
    if (lampada == NULL) return false;

    // ✅ VERSIONE SEMPLICE - registra solo l'intensità
    uint8_t intensity = (uint8_t)(lampada->intensita_luminosa & 0xFF);
    return data_recorder_enqueue(intensity);

    ESP_LOGD(TAG, "Snapshot lampada registrato - Intensità: %u", lampada->intensita_luminosa);
}

/**
 * @brief Registra dati sensoriali (VERSIONE SEMPLICE)
 */
bool data_recorder_enqueue_sensor_data(float temperature, float humidity, uint32_t lux) {
    // ✅ VERSIONE SEMPLICE - registra solo il lux
    uint8_t lux_value = (uint8_t)(lux & 0xFF);
    return data_recorder_enqueue(lux_value);

    ESP_LOGD(TAG, "Dati sensoriali registrati - Lux: %lu", lux);
}


/**
 * @brief Verifica se è attiva la sovrascrittura
 * @return true se la memoria è piena e avviene sovrascrittura
 */
bool data_recorder_is_overwriting(void) {
    return (history_count >= HISTORY_MAX_RECORDS);
}

/**
 * @brief Ottiene statistiche avanzate
 * @desc Estende data_recorder_get_stats con info sovrascrittura
 */
void data_recorder_get_detailed_stats(uint32_t *buffer_count, uint32_t *flash_count,
                                      uint32_t *total_written, bool *is_overwriting) {
    if (buffer_count) *buffer_count = ram_buffer_count;
    if (flash_count) *flash_count = history_count;
    if (total_written) *total_written = history_write_index;
    if (is_overwriting) *is_overwriting = (history_count >= HISTORY_MAX_RECORDS);
}

