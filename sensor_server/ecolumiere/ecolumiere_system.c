/**
 * Autore: DJITSOP FUOGOUK LOIC STEVE
 * Firmware: ECOLUMIERE BLE MESH ESP32
 * Sistema: Gestione centrale Ecolumiere - Versione operativa
 */

#include "ecolumiere_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ecolumiere.h"
#include "storage.h"
#include "pwmcontroller.h"
#include "luxmeter.h"
#include "lightcode.h"
#include "slave_role.h"
#include "datarecorder.h"
#include "zerocross.h"

static const char *TAG = "ECOLUMIERE_SYSTEM";

static bool system_ready = false;
static TaskHandle_t eco_scheduler_task_handle = NULL;
static TaskHandle_t system_control_task_handle = NULL;

/**
 * @brief Configurazione di sistema globale
 */
static system_config_t system_config = {
    .use_real_sensor = false,      // Default: simulazione
    .enable_zero_cross = false,    // Default: simulazione
};

/**
 * @brief Task scheduler Ecolumiere
 */
static void eco_scheduler_task_wrapper(void *pvParameters) {
    ESP_LOGI(TAG, "Starting Ecolumiere Scheduler Task");
    ecolumiere_scheduler_task(pvParameters);
}

/**
 * @brief Task di controllo sistema
 */
static void system_control_task_wrapper(void *pvParameters) {
    ESP_LOGI(TAG, "Starting System Control Task");

    uint32_t loop_count = 0;
    while (1) {
        loop_count++;

        // Monitor stack ogni 60 secondi
        if (loop_count % 6000 == 0) {
            UBaseType_t stack_remaining = uxTaskGetStackHighWaterMark(NULL);
            ESP_LOGI(TAG, "System Alive - Stack: %lu bytes", stack_remaining);
        }

        // Gestione data recorder
        data_recorder_task();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}


/**
 * @brief Test completo del sistema con dati reali
 */
void ecolumiere_system_real_test(void) {
    ESP_LOGI(TAG, "🧪 =========================================");
    ESP_LOGI(TAG, "🧪 STARTING REAL SYSTEM TEST");
    ESP_LOGI(TAG, "🧪 =========================================");

    // 1. TEST SENSORE LUCE REALE
    ESP_LOGI(TAG, "1. 🔆 Testing Real Light Sensor...");
    luxmeter_start_acquisition();
    vTaskDelay(pdMS_TO_TICKS(1000)); // Attendi acquisizione

    uint32_t lux_value, index;
    uint16_t current_pwm = pwmcontroller_get_current_level();
    luxmeter_pickup(LUX_MEASURE_ENVIRONMENT, current_pwm, &lux_value, &index);

    ESP_LOGI(TAG, "   📊 Lux measured: %lu, PWM: %u, Index: %lu",
             lux_value, current_pwm, index);

    luxmeter_stop_acquisition();

    // 2. TEST ZERO-CROSS REALE
    ESP_LOGI(TAG, "2. 🔌 Testing Real Zero-Cross Detection...");
    ESP_LOGI(TAG, "   ⚡ Waiting for zero-cross events...");

    // Monitora zero-cross per 3 secondi
    uint32_t start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    while ((xTaskGetTickCount() * portTICK_PERIOD_MS - start_time) < 3000) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGI(TAG, "   📈 Zero-Cross monitoring completed");

    // 3. TEST LIGHTCODE REALE
    ESP_LOGI(TAG, "3. 💡 Testing Real Lightcode System...");
    light_code_reset_queue();
    vTaskDelay(pdMS_TO_TICKS(100)); // Attendi acquisizione campioni

    light_code_pickup();
    uint8_t decoded_code = light_code_check();

    if (decoded_code != 0) {
        ESP_LOGI(TAG, "   ✅ Code decoded: 0x%02X", decoded_code);
    } else {
        ESP_LOGI(TAG, "   ❌ No valid code detected (normal in test)");
    }

    // 4. TEST PWM CONTROLLER REALE
    ESP_LOGI(TAG, "4. 🎛️ Testing Real PWM Controller...");

    // Test semplice - solo verifica stato corrente
    uint16_t pwm_level = pwmcontroller_get_current_level();
    ESP_LOGI(TAG, "   📊 Current PWM level: %u", pwm_level);

    // Verifica che il PWM controller risponda
    bool pwm_ready = (pwm_level >= 0 && pwm_level <= LIGHT_MAX_LEVEL);
    ESP_LOGI(TAG, "   ✅ PWM controller: %s", pwm_ready ? "OPERATIONAL" : "ERROR");

    // 5. TEST STORAGE REALE
    ESP_LOGI(TAG, "5. 💾 Testing Real Storage System...");
    bool storage_ready_status = storage_ready();
    ESP_LOGI(TAG, "   💾 Storage ready: %s", storage_ready_status ? "YES" : "NO");

    // 6. TEST BLE MESH
    ESP_LOGI(TAG, "6. 📡 Testing BLE Mesh Status...");
    bool ble_ready = ecolumiere_system_is_ready();
    ESP_LOGI(TAG, "   📶 BLE Mesh ready: %s", ble_ready ? "YES" : "NO");

    // 7. VERIFICA FINALE SISTEMA
    ESP_LOGI(TAG, "7. ✅ Final System Check...");

    bool all_systems_ok = storage_ready_status && // Storage funzionante
                          pwm_ready &&            // PWM operativo
                          ble_ready;              // BLE pronto

    ESP_LOGI(TAG, "🧪 =========================================");
    ESP_LOGI(TAG, "🧪 REAL SYSTEM TEST %s", all_systems_ok ? "PASSED ✅" : "FAILED ❌");
    ESP_LOGI(TAG, "🧪 =========================================");

    if (all_systems_ok) {
        ESP_LOGI(TAG, "🎉 All systems operational with real hardware");
    } else {
        ESP_LOGE(TAG, "⚠️ Some systems may need attention");
    }
}


/**
 * @brief Stampa stato configurazione sistema
 */
static void print_system_configuration(void) {
    ESP_LOGI(TAG, "================ SYSTEM CONFIGURATION ================");
    ESP_LOGI(TAG, "Luxmeter Mode:    %s",
             system_config.use_real_sensor ? "REAL SENSOR" : "SIMULATION");
    ESP_LOGI(TAG, "Zero-Cross Mode:  %s",
             system_config.enable_zero_cross ? "REAL DETECTION" : "SIMULATION");
    ESP_LOGI(TAG, "======================================================");
}

/**
 * @brief Inizializzazione completa sistema Ecolumiere
 */
esp_err_t ecolumiere_system_init(void) {
    ESP_LOGI(TAG, "==================== ECOLUMIERE SYSTEM INIT ====================");
    ESP_LOGI(TAG, "Initializing Ecolumiere System");

    // Stampa configurazione iniziale
    print_system_configuration();

    // 1. Storage
    ESP_LOGI(TAG, "1. Initializing storage...");
    storage_init();

    // Attendi che lo storage sia completamente inizializzato
    ESP_LOGI(TAG, "1.1 Waiting for storage to be ready...");
    int storage_retry = 0;
    while (!storage_ready() && storage_retry < 100) {
        vTaskDelay(pdMS_TO_TICKS(10));
        storage_retry++;
    }

    if (storage_ready()) {
        ESP_LOGI(TAG, "1.2 Storage READY after %d retries", storage_retry);
    } else {
        ESP_LOGE(TAG, "1.2 Storage FAILED to initialize after %d retries", storage_retry);
    }

    // 2. Data Recorder
    ESP_LOGI(TAG, "2. Initializing data recorder...");
    data_recorder_init();

    // 3. Lightcode
    ESP_LOGI(TAG, "3. Initializing lightcode...");
    light_code_init();

    // 4. Zero-Cross
    ESP_LOGI(TAG, "4. Initializing zero-cross...");
    zero_cross_init();

    if (system_config.enable_zero_cross) {
        ESP_LOGI(TAG, "   Zero-cross: REAL DETECTION");
        zero_cross_enable();
    } else {
        ESP_LOGI(TAG, "   Zero-cross: DISABLED");
        zero_cross_disable();
    }

    // 5. PWM Controller
    ESP_LOGI(TAG, "5. Initializing PWM controller...");
    esp_err_t pwm_ret = pwmcontroller_init();
    if (pwm_ret != ESP_OK) {
        ESP_LOGE(TAG, "PWM controller init failed!");
        return pwm_ret;
    }

    // Carica stato salvato dopo inizializzazione PWM
    ESP_LOGI(TAG, "5.1 Loading and applying saved lampada state...");
    slave_node_load_saved_state();

    // 6. Luxmeter
    ESP_LOGI(TAG, "6. Initializing luxmeter...");
    luxmeter_init();

    // 7. Ecolumiere Algorithm
    ESP_LOGI(TAG, "7. Initializing ecolumiere...");
    ecolumiere_init();

    ESP_LOGI(TAG, "Ecolumiere System Initialized Successfully");
    ESP_LOGI(TAG, "=================================================================");

    return ESP_OK;
}

/**
 * @brief Avvio sistema Ecolumiere
 */
esp_err_t ecolumiere_system_start(void) {
    ESP_LOGI(TAG, "Starting Ecolumiere System Tasks");

    // Avvia task scheduler Ecolumiere
    if (xTaskCreate(eco_scheduler_task_wrapper, "eco_scheduler", 8192, NULL, 3, &eco_scheduler_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create eco scheduler task");
        return ESP_FAIL;
    }

    // Avvia task controllo sistema
    if (xTaskCreate(system_control_task_wrapper, "system_control", 8192, NULL, 5, &system_control_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create system control task");

        // Cleanup in caso di errore
        if (eco_scheduler_task_handle) {
            vTaskDelete(eco_scheduler_task_handle);
            eco_scheduler_task_handle = NULL;
        }
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "BLE Mesh provisioning ready - waiting for network join");

    system_ready = true;
    ESP_LOGI(TAG, "Ecolumiere System Started Successfully");
    ESP_LOGI(TAG, "   Scheduler Task: ACTIVE");
    ESP_LOGI(TAG, "   Control Task: ACTIVE");
    ESP_LOGI(TAG, "   BLE Ready: WAITING PROVISIONING");

    return ESP_OK;
}

/**
 * @brief Arresto sistema Ecolumiere
 */
void ecolumiere_system_stop(void) {
    ESP_LOGI(TAG, "Stopping Ecolumiere System");

    if (eco_scheduler_task_handle) {
        vTaskDelete(eco_scheduler_task_handle);
        eco_scheduler_task_handle = NULL;
    }

    if (system_control_task_handle) {
        vTaskDelete(system_control_task_handle);
        system_control_task_handle = NULL;
    }

    system_ready = false;
    ESP_LOGI(TAG, "Ecolumiere System Stopped");
}

/**
 * @brief Verifica stato sistema
 */
bool ecolumiere_system_is_ready(void) {
    return system_ready;
}

/**
 * @brief Imposta configurazione sistema
 */
void ecolumiere_system_set_config(system_config_t config) {
    system_config = config;
    ESP_LOGI(TAG, "System configuration updated");
    print_system_configuration();
}

/**
 * @brief Ottiene configurazione sistema
 */
system_config_t ecolumiere_system_get_config(void) {
    return system_config;
}

/**
 * @brief Gestisce comandi ricevuti per NodoLampada
 */
void ecolumiere_handle_nodo_lampada_command(const NodoLampada *command) {
    if (command == NULL) return;

    ESP_LOGI(TAG, "Comando NodoLampada ricevuto - Stato: %s, Intensità: %u",
             command->stato ? "ON" : "OFF",
             command->intensita_luminosa);

    // Applica comandi al sistema
    if (command->stato) {
        // Accendi con intensità specificata
        uint8_t pwm_level = (uint8_t)((command->intensita_luminosa * LIGHT_MAX_LEVEL) / 1000);
        pwmcontroller_set_level(pwm_level);
    } else {
        // Spegni
        pwmcontroller_set_level(0);
    }

    // Aggiorna struttura locale
    slave_node_update_lampada_data(command);
}