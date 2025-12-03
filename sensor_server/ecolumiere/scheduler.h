//
// Created by Admin on 02/12/2025.
//

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================
// TIPI DI EVENTI PER IL TUO SISTEMA
// =============================================
typedef enum {
    SCH_EVT_BLE_MESH_RX = 0,      // Messaggio BLE Mesh ricevuto
    SCH_EVT_PWM_UPDATE,           // Aggiornamento PWM
    SCH_EVT_LUX_MEASUREMENT,      // Misurazione lux completata
    SCH_EVT_ALGO_PROCESS,         // Esecuzione algoritmo
    SCH_EVT_STORAGE_WRITE,        // Scrittura storage
    SCH_EVT_STORAGE_READ,         // Lettura storage
    SCH_EVT_ZERO_CROSS,           // Evento zero-cross
    SCH_EVT_LIGHT_CODE,           // Codice luce rilevato
    SCH_EVT_TIMER,                // Timer scaduto
    SCH_EVT_SERIAL_CMD,           // Comando seriale
    SCH_EVT_SYSTEM_CMD,           // Comando di sistema
    SCH_EVT_LAMPADA_UPDATE,       // Aggiornamento stato lampada
    SCH_EVT_DATA_RECORDER,        // Log dati
    SCH_EVT_MAX
} scheduler_event_type_t;

// =============================================
// STRUTTURA EVENTO GENERICO
// =============================================
typedef struct {
    scheduler_event_type_t type;   // Tipo evento
    uint32_t timestamp;            // Timestamp
    void *p_event_data;            // Dati evento
    uint16_t event_size;           // Dimensione dati
    void (*handler)(void *, uint16_t); // Handler specifico
} scheduler_event_t;

// =============================================
// STRUTTURE DATI PER OGNI TIPO DI EVENTO
// =============================================

    // Tipi di messaggi BLE Mesh (se non li hai già)
    typedef enum {
        MESH_MSG_LIGHT_SET = 0,
        MESH_MSG_CONFIG_UPDATE,
        MESH_MSG_SENSOR_DATA,
        MESH_MSG_STATUS
    } mesh_msg_type_t;


// Evento BLE Mesh
    typedef struct {
        uint8_t brightness;      // 0-100% (lightness)
        uint8_t pwm_level;       // 0-32
        uint16_t hue;           // 0-360 (se serve)
        uint16_t saturation;    // 0-100 (se serve)
        bool is_override;       // true/false
        uint64_t timestamp;     // quando ricevuto
    } ble_mesh_event_t;

// Evento PWM
typedef struct {
    uint8_t level;
    uint8_t source;  // 0=algoritmo, 1=BLE, 2=seriale, 3=manual
    uint32_t duration_ms;
} pwm_event_t;

// Evento Lux
typedef struct {
    uint32_t natural_lux;
    uint32_t env_lux;
    uint8_t source;  // LUX_SOURCE_NATURAL, LUX_SOURCE_ENVIRONMENT
} lux_event_t;

// Evento Algoritmo
typedef struct {
    uint8_t trigger;  // 0=normale, 1=forzato, 2=test
    uint32_t natural_lux;
    uint32_t env_lux;
} algo_event_t;

// Evento Storage
typedef struct {
    uint8_t operation;  // 0=read, 1=write, 2=erase
    void *data;
    size_t size;
    uint16_t file_id;
} storage_event_t;

// Evento Zero-Cross
typedef struct {
    uint64_t timestamp_us;
    uint8_t edge;  // 0=falling, 1=rising
} zero_cross_event_t;

// Evento Light Code
typedef struct {
    uint8_t code;
    uint8_t window[10];
} light_code_event_t;

// Evento Seriale
typedef struct {
    char command[32];
    char params[64];
} serial_event_t;

// =============================================
// INTERFACCIA PUBBLICA SCHEDULER
// =============================================
typedef struct {
    QueueHandle_t event_queue;
    SemaphoreHandle_t mutex;
    TaskHandle_t scheduler_task;
    uint32_t queue_size;
    uint32_t max_event_size;
    bool initialized;
    bool running;
    uint32_t events_processed;
    uint32_t events_dropped;
} scheduler_context_t;

// Inizializzazione
esp_err_t scheduler_init(uint32_t queue_size, uint32_t max_event_size);
esp_err_t scheduler_start(UBaseType_t task_priority, uint32_t stack_size);

// Gestione eventi
esp_err_t scheduler_put_event(void *p_event_data, uint16_t event_size,
                             scheduler_event_type_t type,
                             void (*handler)(void *, uint16_t));

esp_err_t scheduler_put_event_isr(void *p_event_data, uint16_t event_size,
                                 scheduler_event_type_t type,
                                 void (*handler)(void *, uint16_t));

// Esecuzione (da chiamare nel task scheduler)
void scheduler_execute(void);

// Utility
bool scheduler_is_initialized(void);
uint32_t scheduler_get_queue_count(void);
uint32_t scheduler_get_events_processed(void);
uint32_t scheduler_get_events_dropped(void);
void scheduler_get_stats(uint32_t *processed, uint32_t *dropped, uint32_t *queued);

// Gestione specifica per i tuoi moduli
esp_err_t scheduler_put_ble_mesh_event(uint16_t lightness, bool is_override);
esp_err_t scheduler_put_pwm_event(uint8_t level, uint8_t source);
esp_err_t scheduler_put_lux_event(uint32_t natural_lux, uint32_t env_lux, uint8_t source);
esp_err_t scheduler_put_algo_event(uint8_t trigger);
esp_err_t scheduler_put_storage_write(void *data, size_t size);
esp_err_t scheduler_put_serial_command(const char *cmd, const char *params);

void handle_ble_mesh_event(void *p_event_data, uint16_t event_size);

// Macro per facilità d'uso
#define SCH_PUT_EVENT(data, type, handler) \
    scheduler_put_event(data, sizeof(*data), type, handler)

#define SCH_PUT_SIMPLE_EVENT(type, handler) \
    scheduler_put_event(NULL, 0, type, handler)

#ifdef __cplusplus
}
#endif


#endif //SCHEDULER_H
