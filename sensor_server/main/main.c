/**
 * @file main.c
 * @brief Applicazione principale sistema Ecolumiere
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "ble_mesh_example_init.h"

#include "board.h"
#include "ecolumiere_system.h"
#include "slave_role.h"
#include "ble_mesh_ecolumiere.h"
#include "scheduler.h"

static const char *TAG = "MAIN_ECOLUMIERE";


#include "driver/gpio.h"
#include "driver/uart.h"

#define BUF_SIZE (1024)

void serial_control_task(void *pvParameters) {
    ESP_LOGI(TAG, "🎮 Task controllo sistema avviato");

    // Configura UART (come prima)
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_param_config(UART_NUM_0, &uart_config);
    uart_driver_install(UART_NUM_0, BUF_SIZE * 2, 0, 0, NULL, 0);

    uint8_t data[BUF_SIZE];
    bool led_stato = false;

    ESP_LOGI(TAG, "🚀 Sistema pronto! Comandi:");
    ESP_LOGI(TAG, "  ON     - Accende il LED");
    ESP_LOGI(TAG, "  OFF    - Spegne il LED");
    ESP_LOGI(TAG, "  BLINK  - Fa lampeggiare il LED");
    ESP_LOGI(TAG, "  STATUS - Mostra stato sistema");
    ESP_LOGI(TAG, "  TEST   - Test completo sistema reale");
    ESP_LOGI(TAG, "  RESET  - Reset configurazione");
	ESP_LOGI(TAG, "  ALGO_STATUS         - Stato algoritmo");
    ESP_LOGI(TAG, "  ALGO_TEST N E T     - Test algoritmo (N=natural, E=env, T=target lux)");

    while(1) {
        int len = uart_read_bytes(UART_NUM_0, data, BUF_SIZE - 1, 100 / portTICK_PERIOD_MS);
        if (len > 0) {
            data[len] = '\0';

            char* comando = (char*)data;
            char *newline = strchr(comando, '\n');
            if (newline) *newline = '\0';
            newline = strchr(comando, '\r');
            if (newline) *newline = '\0';

            ESP_LOGI(TAG, "📨 Comando: '%s'", comando);

            if(strcmp(comando, "ON") == 0) {
                board_led_operation(LED_R, LED_ON);
                led_stato = true;
                ESP_LOGI(TAG, "💡 LED ACCESO");
            }
            else if(strcmp(comando, "OFF") == 0) {
                board_led_operation(LED_R, LED_OFF);
                led_stato = false;
                ESP_LOGI(TAG, "⚫ LED SPENTO");
            }
            else if(strcmp(comando, "BLINK") == 0) {
                ESP_LOGI(TAG, "✨ BLINK MODE - 5 lampeggi");
                for(int i = 0; i < 5; i++) {
                    board_led_operation(LED_R, LED_ON);
                    vTaskDelay(200 / portTICK_PERIOD_MS);
                    board_led_operation(LED_R, LED_OFF);
                    vTaskDelay(200 / portTICK_PERIOD_MS);
                }
                led_stato = false;
                ESP_LOGI(TAG, "✅ BLINK COMPLETATO");
            }
            else if(strcmp(comando, "STATUS") == 0) {
                ESP_LOGI(TAG, "📊 Stato LED: %s", led_stato ? "ACCESO" : "SPENTO");
                ESP_LOGI(TAG, "📈 Sistema pronto: %s", ecolumiere_system_is_ready() ? "SI" : "NO");
            }
            else if(strcmp(comando, "TEST") == 0) {
                ESP_LOGI(TAG, "🧪 Avvio test sistema reale...");
                ecolumiere_system_real_test();
            }
            else if(strcmp(comando, "RESET") == 0) {
                ESP_LOGI(TAG, "🔄 Reset configurazione...");
                // Ripristina configurazione produzione
                system_config_t config = {
                    .use_real_sensor = true,
                    .enable_zero_cross = true
                };
                ecolumiere_system_set_config(config);
                ESP_LOGI(TAG, "✅ Configurazione ripristinata");
            }

			            // ✅ NUOVI COMANDI ALGORITMO
            else if(strcmp(comando, "ALGO_STATUS") == 0) {
                ecolumiere_show_algorithm_status();
            }
            else if(strncmp(comando, "ALGO_TEST", 9) == 0) {
                // Formato: ALGO_TEST <natural> <env> <target>
                uint32_t natural_lux, env_lux, target_lux;
                if (sscanf(comando, "ALGO_TEST %lu %lu %lu",
                          &natural_lux, &env_lux, &target_lux) == 3) {
                    ecolumiere_test_algorithm(natural_lux, env_lux, target_lux);
                } else {
                    ESP_LOGI(TAG, "❌ Formato: ALGO_TEST <natural_lux> <env_lux> <target_lux>");
                    ESP_LOGI(TAG, "💡 Esempio: ALGO_TEST 100 50 200");
                }
            }
            else {
                ESP_LOGW(TAG, "❌ Comando non valido!");
                ESP_LOGI(TAG, "💡 Comandi: ON, OFF, BLINK, STATUS, TEST, RESET, ALGO_STATUS, ALGO_TEST");
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}


void emergency_nvs_cleanup(void) {
    ESP_LOGI("EMERGENCY", "🚨 INIZIO RESET COMPLETO NVS");

    // Chiudi NVS se aperto
    nvs_flash_deinit();

    // Cancella TUTTA la NVS
    esp_err_t err = nvs_flash_erase();
    if (err == ESP_OK) {
        ESP_LOGI("EMERGENCY", "✅ NVS cancellata completamente");
    } else {
        ESP_LOGE("EMERGENCY", "❌ Errore cancellazione NVS: %s", esp_err_to_name(err));
    }

    // Re-inizializza
    err = nvs_flash_init();
    if (err == ESP_OK) {
        ESP_LOGI("EMERGENCY", "✅ NVS re-inizializzata");
    } else {
        ESP_LOGE("EMERGENCY", "❌ Errore re-inizializzazione NVS: %s", esp_err_to_name(err));
    }

    ESP_LOGI("EMERGENCY", "🚨 RESET NVS COMPLETATO - RIAVVIA IL SISTEMA");
}



void app_main(void) {
    esp_err_t err;

    ESP_LOGI(TAG, "🚀 Avvio Sistema Ecolumiere...");

    // ✅ 1. INIZIALIZZA SCHEDULER PRIMA DI TUTTO
    ESP_LOGI(TAG, "🔄 Inizializzazione Scheduler...");
    err = scheduler_init(100, 256); // 100 eventi, max 256 bytes
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Scheduler init fallito");
        return;
    }

    // ✅ 2. AVVIA TASK SCHEDULER
    err = scheduler_start(tskIDLE_PRIORITY + 1, 4096);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Scheduler start fallito");
        return;
    }

    // ✅ 3. INIZIALIZZA BOARD
    ESP_LOGI(TAG, "💡 Inizializzazione Board...");
    board_init();

    // ✅ 4. SOLO NVS
    ESP_LOGI(TAG, "📦 Inizializzazione NVS...");
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGI(TAG, "NVS format changed, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // ✅ 5. CONFIGURAZIONE SISTEMA PRODUZIONE
    ESP_LOGI(TAG, "🔧 Configurazione sistema produzione...");
    system_config_t config = {
        .use_real_sensor = true,
        .enable_zero_cross = true
    };
    ecolumiere_system_set_config(config);

    // ✅ 6. BLUETOOTH
    ESP_LOGI(TAG, "📡 Inizializzazione Bluetooth...");
    err = bluetooth_init();
    if (err) {
        ESP_LOGE(TAG, "❌ Bluetooth init fallito");
        return;
    }

    // ✅ 7. SLAVE NODE
    ESP_LOGI(TAG, "🔗 Inizializzazione Slave Node...");
    slave_node_init();

    // ✅ 8. SISTEMA ECOLUMIERE
    ESP_LOGI(TAG, "⚙️ Inizializzazione Sistema Ecolumiere...");
    err = ecolumiere_system_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Sistema Ecolumiere init fallito");
        return;
    }

    // ✅ 9. BLE MESH
    ESP_LOGI(TAG, "📶 Inizializzazione BLE Mesh Ecolumiere...");
    uint8_t dev_uuid[ESP_BLE_MESH_OCTET16_LEN] = { 0x32, 0x10 };
    ble_mesh_ecolumiere_get_dev_uuid(dev_uuid);

    err = ble_mesh_ecolumiere_init();
    if (err) {
        ESP_LOGE(TAG, "❌ BLE Mesh Ecolumiere init fallito");
        return;
    }

    // ✅ 10. TASK CONTROLLO SERIALE
    ESP_LOGI(TAG, "🎮 Avvio controllo seriale LED...");
    xTaskCreate(serial_control_task, "serial_ctrl", 6144, NULL, 2, NULL);

    // ✅ 11. AVVIO SISTEMA
    ESP_LOGI(TAG, "🎯 Avvio Sistema Ecolumiere...");
    ecolumiere_system_start();

    ESP_LOGI(TAG, "🏭 SISTEMA PRODUZIONE AVVIATO CON SCHEDULER");
    ESP_LOGI(TAG, "📊 Tutti gli eventi ora passano attraverso lo scheduler");
}