//
// Created by Admin on 02/12/2025.
//

#include "scheduler.h"
#include "esp_log.h"
#include "string.h"
#include "stdlib.h"
#include "ecolumiere.h"
#include "pwmcontroller.h"
#include "slave_role.h"
#include "ble_mesh_ecolumiere.h"
#include "esp_timer.h"

static const char *TAG = "SCHEDULER";

// Contesto globale dello scheduler
static scheduler_context_t g_scheduler = {0};

// =============================================
// HANDLER DI DEFAULT PER OGNI TIPO DI EVENTO
// =============================================

static void handle_algo_process_event(void *p_event_data, uint16_t event_size);

static void scheduler_task_function(void *pvParameters);

// Handler BLE Mesh
void handle_ble_mesh_event(void *p_event_data, uint16_t event_size) {
    ble_mesh_event_t *event = (ble_mesh_event_t *)p_event_data;

    // Calcola ritardo
    uint64_t delay_us = esp_timer_get_time() - event->timestamp;

    ESP_LOGI(TAG, "⚡ Processing BLE Mesh Event from scheduler:");
    ESP_LOGI(TAG, "   Lightness: %u%%", event->brightness);
    ESP_LOGI(TAG, "   PWM Level: %u/32", event->pwm_level);
    ESP_LOGI(TAG, "   Hue: %u, Sat: %u", event->hue, event->saturation);
    ESP_LOGI(TAG, "   Queue Delay: %.2f ms", delay_us / 1000.0);

    // ✅ 1. Gestisci comando PWM
    ecolumiere_handle_mesh_command(event->pwm_level, event->is_override);

    // ✅ 2. Sincronizza stato lampada BLE Mesh
    sync_nodo_lampada_with_hsl(event->hue, event->saturation, event->brightness);

    ESP_LOGI(TAG, "✅ BLE Mesh event processed");
}


// Handler PWM Update
static void handle_pwm_update_event(void *p_event_data, uint16_t event_size) {
    pwm_event_t *event = (pwm_event_t *)p_event_data;

    ESP_LOGD(TAG, "🎛️ PWM Update: level=%u, source=%u", event->level, event->source);

    // Controlla se PWM controller è inizializzato
    // if (is_pwm_initialized()) {
    //     pwmcontroller_set_level(event->level);
    // }
}

// Handler Lux Measurement
static void handle_lux_measurement_event(void *p_event_data, uint16_t event_size) {
    lux_event_t *event = (lux_event_t *)p_event_data;

    ESP_LOGD(TAG, "🔆 Lux Measurement: natural=%lu, env=%lu, source=%u",
             event->natural_lux, event->env_lux, event->source);

    // Prepara evento per algoritmo
    algo_sched_event_t algo_event = {
        .source = event->source,
        .measure = (event->source == LUX_SOURCE_NATURAL) ? event->natural_lux : event->env_lux
    };

    // Invia a scheduler per processamento algoritmo
    scheduler_put_event(&algo_event, sizeof(algo_event),
                       SCH_EVT_ALGO_PROCESS, handle_algo_process_event);
}

// Handler Algoritmo
static void handle_algo_process_event(void *p_event_data, uint16_t event_size) {
    algo_sched_event_t *event = (algo_sched_event_t *)p_event_data;

    ESP_LOGI(TAG, "🧠 Algo Process Triggered: source=%u, measure=%lu",
             event->source, event->measure);

    // Chiama la tua funzione di aggiornamento lux
    // ecolumiere_update_lux(p_event_data, event_size);
}

// Handler Storage
static void handle_storage_event(void *p_event_data, uint16_t event_size) {
    storage_event_t *event = (storage_event_t *)p_event_data;

    ESP_LOGD(TAG, "💾 Storage Event: op=%u, size=%u", event->operation, event->size);

    switch (event->operation) {
        case 0: // Read
            // storage_read(event->file_id, event->data, event->size);
            break;
        case 1: // Write
            // storage_write(event->file_id, event->data, event->size);
            break;
        case 2: // Erase
            // storage_erase(event->file_id);
            break;
    }
}

// Handler Zero-Cross
static void handle_zero_cross_event(void *p_event_data, uint16_t event_size) {
    zero_cross_event_t *event = (zero_cross_event_t *)p_event_data;

    ESP_LOGD(TAG, "⚡ Zero-Cross: edge=%u, time=%llu", event->edge, event->timestamp_us);

    // Calcola fase e programma PWM
    // pwm_apply_phase_controlled_duty();
}

// Handler Light Code
static void handle_light_code_event(void *p_event_data, uint16_t event_size) {
    light_code_event_t *event = (light_code_event_t *)p_event_data;

    ESP_LOGI(TAG, "💡 Light Code: 0x%02X", event->code);

    // Decodifica e gestisci codice
    // light_code_process(event->code);
}

// Handler Seriale
static void handle_serial_event(void *p_event_data, uint16_t event_size) {
    serial_event_t *event = (serial_event_t *)p_event_data;

    ESP_LOGI(TAG, "⌨️ Serial Command: %s %s", event->command, event->params);

    // Gestisci comando seriale
    if (strcmp(event->command, "ON") == 0) {
        pwm_event_t pwm_evt = {.level = LIGHT_MAX_LEVEL, .source = 2};
        scheduler_put_event(&pwm_evt, sizeof(pwm_evt), SCH_EVT_PWM_UPDATE, handle_pwm_update_event);
    }
    else if (strcmp(event->command, "OFF") == 0) {
        pwm_event_t pwm_evt = {.level = 0, .source = 2};
        scheduler_put_event(&pwm_evt, sizeof(pwm_evt), SCH_EVT_PWM_UPDATE, handle_pwm_update_event);
    }
    else if (strcmp(event->command, "TEST") == 0) {
        // ecolumiere_system_real_test();
    }
    else if (strcmp(event->command, "STATUS") == 0) {
        // ecolumiere_show_algorithm_status();
    }
}

// =============================================
// IMPLEMENTAZIONE SCHEDULER
// =============================================

esp_err_t scheduler_init(uint32_t queue_size, uint32_t max_event_size) {
    if (g_scheduler.initialized) {
        ESP_LOGW(TAG, "Scheduler already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing scheduler: queue_size=%u, max_event_size=%u",
             queue_size, max_event_size);

    // Crea mutex per thread safety
    g_scheduler.mutex = xSemaphoreCreateMutex();
    if (g_scheduler.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Crea coda eventi
    g_scheduler.event_queue = xQueueCreate(queue_size, sizeof(scheduler_event_t));
    if (g_scheduler.event_queue == NULL) {
        vSemaphoreDelete(g_scheduler.mutex);
        ESP_LOGE(TAG, "Failed to create event queue");
        return ESP_ERR_NO_MEM;
    }

    g_scheduler.queue_size = queue_size;
    g_scheduler.max_event_size = max_event_size;
    g_scheduler.initialized = true;
    g_scheduler.running = false;
    g_scheduler.events_processed = 0;
    g_scheduler.events_dropped = 0;

    ESP_LOGI(TAG, "✅ Scheduler initialized successfully");
    return ESP_OK;
}

esp_err_t scheduler_start(UBaseType_t task_priority, uint32_t stack_size) {
    if (!g_scheduler.initialized) {
        ESP_LOGE(TAG, "Scheduler not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (g_scheduler.running) {
        ESP_LOGW(TAG, "Scheduler already running");
        return ESP_OK;
    }

    // Crea task scheduler
    BaseType_t result = xTaskCreate(
        scheduler_task_function,
        "scheduler_task",
        stack_size,
        NULL,
        task_priority,
        &g_scheduler.scheduler_task
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create scheduler task");
        return ESP_FAIL;
    }

    g_scheduler.running = true;
    ESP_LOGI(TAG, "✅ Scheduler task started (priority: %u, stack: %u)",
             task_priority, stack_size);

    return ESP_OK;
}

// scheduler.c - Versione con blocking queue

static void scheduler_task_function(void *pvParameters) {
    ESP_LOGI(TAG, "🚀 Scheduler task started (blocking mode)");

    scheduler_event_t event;

    while (1) {
        // ✅ BLOCCA IL TASK FINCHÉ NON ARRIVA UN EVENTO
        // Questo è più efficiente e non spreca CPU
        if (xQueueReceive(g_scheduler.event_queue, &event, portMAX_DELAY) == pdTRUE) {

            g_scheduler.events_processed++;

            ESP_LOGD(TAG, "⚡ Executing event: type=%u, size=%u",
                     event.type, event.event_size);

            // Esegui handler
            if (event.handler != NULL) {
                event.handler(event.p_event_data, event.event_size);
            }

            // Libera memoria
            if (event.p_event_data != NULL) {
                free(event.p_event_data);
            }

            // ✅ DOPO AVER PROCESSATO UN EVENTO, CONTROLLA SE CE NE SONO ALTRI
            // Processa tutti gli eventi disponibili senza bloccare
            while (xQueueReceive(g_scheduler.event_queue, &event, 0) == pdTRUE) {
                g_scheduler.events_processed++;

                ESP_LOGD(TAG, "⚡ Executing queued event: type=%u", event.type);

                if (event.handler != NULL) {
                    event.handler(event.p_event_data, event.event_size);
                }

                if (event.p_event_data != NULL) {
                    free(event.p_event_data);
                }
            }
        }
    }
}


esp_err_t scheduler_put_event(void *p_event_data, uint16_t event_size,
                             scheduler_event_type_t type,
                             void (*handler)(void *, uint16_t)) {

    if (!g_scheduler.initialized) {
        ESP_LOGE(TAG, "Scheduler not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (event_size > g_scheduler.max_event_size) {
        ESP_LOGE(TAG, "Event size too large: %u > %u", event_size, g_scheduler.max_event_size);
        return ESP_ERR_INVALID_SIZE;
    }

    // Alloca e copia i dati dell'evento
    void *event_data_copy = NULL;
    if (event_size > 0 && p_event_data != NULL) {
        event_data_copy = malloc(event_size);
        if (event_data_copy == NULL) {
            ESP_LOGE(TAG, "Failed to allocate event data");
            return ESP_ERR_NO_MEM;
        }
        memcpy(event_data_copy, p_event_data, event_size);
    }

    // Prepara struttura evento
    scheduler_event_t event = {
        .type = type,
        .timestamp = xTaskGetTickCount(),
        .p_event_data = event_data_copy,
        .event_size = event_size,
        .handler = handler
    };

    // Invia alla coda (con timeout di 10ms)
    if (xQueueSend(g_scheduler.event_queue, &event, pdMS_TO_TICKS(10)) != pdTRUE) {
        if (event_data_copy != NULL) {
            free(event_data_copy);
        }
        g_scheduler.events_dropped++;
        ESP_LOGW(TAG, "Event dropped (queue full): type=%u", type);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "📨 Event queued: type=%u, size=%u", type, event_size);
    return ESP_OK;
}

esp_err_t scheduler_put_event_isr(void *p_event_data, uint16_t event_size,
                                 scheduler_event_type_t type,
                                 void (*handler)(void *, uint16_t)) {

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Alloca memoria in contesto ISR
    void *event_data_copy = NULL;
    if (event_size > 0 && p_event_data != NULL) {
        event_data_copy = pvPortMalloc(event_size);
        if (event_data_copy == NULL) {
            return ESP_ERR_NO_MEM;
        }
        memcpy(event_data_copy, p_event_data, event_size);
    }

    scheduler_event_t event = {
        .type = type,
        .timestamp = xTaskGetTickCountFromISR(),
        .p_event_data = event_data_copy,
        .event_size = event_size,
        .handler = handler
    };

    // Invia dalla ISR
    if (xQueueSendFromISR(g_scheduler.event_queue, &event, &xHigherPriorityTaskWoken) != pdTRUE) {
        if (event_data_copy != NULL) {
            vPortFree(event_data_copy);
        }
        return ESP_FAIL;
    }

    // Switch context se necessario
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }

    return ESP_OK;
}

void scheduler_execute(void) {
    if (!g_scheduler.initialized) {
        return;
    }

    scheduler_event_t event;
    uint32_t processed = 0;

    // ✅ LIMITA IL NUMERO DI EVENTI PROCESSATI PER CICLO
    // Per evitare di bloccare il sistema troppo a lungo
    const uint32_t MAX_EVENTS_PER_CYCLE = 5;

    // ✅ CONTROLLA SE CI SONO EVENTI PRIMA DI PROCESSARE
    UBaseType_t queue_items = uxQueueMessagesWaiting(g_scheduler.event_queue);
    if (queue_items == 0) {
        return; // Niente da fare, ritorna subito
    }

    ESP_LOGD(TAG, "📊 Queue has %u events", queue_items);

    // Processa massimo MAX_EVENTS_PER_CYCLE eventi
    while (processed < MAX_EVENTS_PER_CYCLE &&
           xQueueReceive(g_scheduler.event_queue, &event, 0) == pdTRUE) {

        processed++;
        g_scheduler.events_processed++;

        ESP_LOGD(TAG, "⚡ Executing event: type=%u, size=%u",
                 event.type, event.event_size);

        // ✅ MISURA IL TEMPO DI ESECUZIONE (debug)
        // TickType_t start_time = xTaskGetTickCount();

        // Esegui handler se definito
        if (event.handler != NULL) {
            event.handler(event.p_event_data, event.event_size);
        } else {
            ESP_LOGW(TAG, "Event has no handler: type=%u", event.type);
        }

        // ✅ VERIFICA SE L'HANDLER HA BLOCCATO TROPPO
        // TickType_t execution_time = xTaskGetTickCount() - start_time;
        // if (execution_time > pdMS_TO_TICKS(10)) {
        //     ESP_LOGW(TAG, "Handler took %lu ms", execution_time * portTICK_PERIOD_MS);
        // }

        // Libera memoria dati evento
        if (event.p_event_data != NULL) {
            free(event.p_event_data);
        }
    }

    if (processed > 0) {
        ESP_LOGD(TAG, "Processed %u events", processed);

        // ✅ Se ci sono ancora eventi, schedula un altro ciclo presto
        if (uxQueueMessagesWaiting(g_scheduler.event_queue) > 0) {
            // Potremmo eseguire un altro ciclo subito se necessario
            // Ma per ora lasciamo al prossimo tick
        }
    }
}

// =============================================
// FUNZIONI DI UTILITY
// =============================================

bool scheduler_is_initialized(void) {
    return g_scheduler.initialized;
}

uint32_t scheduler_get_queue_count(void) {
    if (!g_scheduler.initialized) {
        return 0;
    }
    return uxQueueMessagesWaiting(g_scheduler.event_queue);
}

uint32_t scheduler_get_events_processed(void) {
    return g_scheduler.events_processed;
}

uint32_t scheduler_get_events_dropped(void) {
    return g_scheduler.events_dropped;
}

void scheduler_get_stats(uint32_t *processed, uint32_t *dropped, uint32_t *queued) {
    if (processed) *processed = g_scheduler.events_processed;
    if (dropped) *dropped = g_scheduler.events_dropped;
    if (queued) *queued = scheduler_get_queue_count();
}

// =============================================
// FUNZIONI SPECIFICHE PER I TUOI MODULI
// =============================================

esp_err_t scheduler_put_ble_mesh_event(uint16_t lightness, bool is_override) {
    ble_mesh_event_t event = {
        .brightness = lightness,
        .is_override = is_override
    };

    return scheduler_put_event(&event, sizeof(event),
                              SCH_EVT_BLE_MESH_RX, handle_ble_mesh_event);
}

esp_err_t scheduler_put_pwm_event(uint8_t level, uint8_t source) {
    pwm_event_t event = {
        .level = level,
        .source = source,
        .duration_ms = 0
    };

    return scheduler_put_event(&event, sizeof(event),
                              SCH_EVT_PWM_UPDATE, handle_pwm_update_event);
}

esp_err_t scheduler_put_lux_event(uint32_t natural_lux, uint32_t env_lux, uint8_t source) {
    lux_event_t event = {
        .natural_lux = natural_lux,
        .env_lux = env_lux,
        .source = source
    };

    return scheduler_put_event(&event, sizeof(event),
                              SCH_EVT_LUX_MEASUREMENT, handle_lux_measurement_event);
}

esp_err_t scheduler_put_algo_event(uint8_t trigger) {
    algo_event_t event = {
        .trigger = trigger
    };

    return scheduler_put_event(&event, sizeof(event),
                              SCH_EVT_ALGO_PROCESS, handle_algo_process_event);
}

esp_err_t scheduler_put_storage_write(void *data, size_t size) {
    storage_event_t event = {
        .operation = 1, // Write
        .data = data,
        .size = size
    };

    return scheduler_put_event(&event, sizeof(event),
                              SCH_EVT_STORAGE_WRITE, handle_storage_event);
}

esp_err_t scheduler_put_serial_command(const char *cmd, const char *params) {
    serial_event_t event;
    strncpy(event.command, cmd, sizeof(event.command) - 1);
    event.command[sizeof(event.command) - 1] = '\0';

    if (params) {
        strncpy(event.params, params, sizeof(event.params) - 1);
        event.params[sizeof(event.params) - 1] = '\0';
    } else {
        event.params[0] = '\0';
    }

    return scheduler_put_event(&event, sizeof(event),
                              SCH_EVT_SERIAL_CMD, handle_serial_event);
}
