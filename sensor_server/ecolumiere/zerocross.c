

#include "zerocross.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "pwmcontroller.h"

static const char *TAG = "ZEROCROSS";

#define ZERO_CROSS_GPIO     4
#define DEBOUNCE_TIME_US    1000  // 1ms debounce

static esp_timer_handle_t phase_timer = NULL;
static volatile uint64_t last_cross_time = 0;

/**
 * @brief Timer callback - applica PWM dopo delay phase-cutting
 */
static void phase_timer_callback(void* arg) {
    pwm_apply_phase_controlled_duty();
}

/**
 * @brief ISR ultra-leggera - solo debounce e timer
 */
static void IRAM_ATTR zero_cross_isr(void* arg) {
    uint64_t current_time = esp_timer_get_time();

    // Debounce hardware-style
    if ((current_time - last_cross_time) < DEBOUNCE_TIME_US) {
        return;
    }
    last_cross_time = current_time;

    // Calcola delay in ISR (veloce)
    uint16_t level = pwmcontroller_get_current_level();
    uint32_t delay_us = 0;

    if (level > 0 && level < 32) {
        delay_us = (32 - level) * (10000 / 32); // 10ms semi-period
    }

    // Se delay > 0, programma timer
    if (delay_us > 0 && delay_us < 10000) {
        esp_timer_stop(phase_timer);
        esp_timer_start_once(phase_timer, delay_us);
    }
    // Se delay == 0, applica immediatamente
    else if (delay_us == 0) {
        pwm_apply_phase_controlled_duty();
    }
}

/**
 * @brief Inizializzazione semplice come Nordic
 */
esp_err_t zero_cross_init(void) {
    ESP_LOGI(TAG, "Initializing simple zero-cross (Nordic style)");

    // 1. Configura GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ZERO_CROSS_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_POSEDGE
    };
    gpio_config(&io_conf);

    // 2. Installa ISR service
    gpio_install_isr_service(0);
    gpio_isr_handler_add(ZERO_CROSS_GPIO, zero_cross_isr, NULL);

    // 3. Crea timer per phase-cutting
    esp_timer_create_args_t timer_args = {
        .callback = phase_timer_callback,
        .name = "phase_timer_simple"
    };
    esp_err_t ret = esp_timer_create(&timer_args, &phase_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create phase timer: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Simple zero-cross ready - GPIO %d", ZERO_CROSS_GPIO);
    return ESP_OK;
}

void zero_cross_enable(void) {
    gpio_intr_enable(ZERO_CROSS_GPIO);
    ESP_LOGI(TAG, "Zero-cross enabled");
}

void zero_cross_disable(void) {
    gpio_intr_disable(ZERO_CROSS_GPIO);
    ESP_LOGI(TAG, "Zero-cross disabled");
}