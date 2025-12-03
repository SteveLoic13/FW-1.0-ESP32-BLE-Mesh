/**
 * Autore: DJITSOP FUOGOUK LOIC STEVE
 * @brief Implementazione sistema misurazione intensità luminosa per ESP32
 * @description Versione semplificata senza simulazione, focalizzata su acquisizione reale
 */

#include "luxmeter.h"
#include "pwmcontroller.h"
#include "ecolumiere.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include <math.h>

/************************************************
 * PRIVATE DEFINES AND CONSTANTS               *
 ************************************************/

static const char* TAG = "LUXMETER";

// Configurazione hardware (simile a Nordic)
#define SENSOR_CONVERSION_RESISTANCE            22000.0
#define LUX_SENSOR_ADC_CHANNEL                  ADC_CHANNEL_4
#define ADC_UNIT                                ADC_UNIT_1

// Configurazione acquisizione (stessi parametri Nordic)
#define SAMPLES_PER_CHANNEL                     45
#define TIMER_INTERVAL_US                       1000
#define SAMPLE_BUFFER_FIRST_VALUE_INDEX         20
#define SAMPLE_BUFFER_LAST_VALUE_INDEX          42

// Fattore di conversione (come Nordic)
static const double saadc_lsb = 3.3 / 4096.0; // 3.3V reference / 12-bit

/************************************************
 * PRIVATE GLOBAL VARIABLES                    *
 ************************************************/

static int samples_buffer[SAMPLES_PER_CHANNEL];
static float measure_mean = 0.0;
static uint32_t measure_index = 0;
static adc_oneshot_unit_handle_t adc_handle = NULL;
static TimerHandle_t sampling_timer = NULL;
static int sample_count = 0;
static bool conversion_active = false;

/**
 * @brief Mappa compensazione offset (stessa di Nordic)
 */
static const uint8_t offset_map[] = {
    0, 8, 10, 12, 11, 14, 17, 11, 14, 15, 18, 19, 21, 22, 22, 22,
    22, 22, 22, 21, 21, 22, 23, 24, 25, 26, 27, 28, 30, 31, 33, 34, 38
};

/************************************************
 * PRIVATE FUNCTIONS IMPLEMENTATION           *
 ************************************************/

/**
 * @brief Elabora buffer ADC e calcola valore lux (stesso algoritmo Nordic)
 */
static void luxmeter_process_adc_buffer(void) {
    uint32_t samples_mean = 0;
    uint16_t valid_samples_count = 0;

    // Calcola media sui campioni validi (stesso range Nordic)
    for (uint16_t i = SAMPLE_BUFFER_FIRST_VALUE_INDEX; i <= SAMPLE_BUFFER_LAST_VALUE_INDEX; i++) {
        samples_mean += (uint32_t)samples_buffer[i];
        valid_samples_count++;
    }

    if (valid_samples_count > 0) {
        samples_mean /= valid_samples_count;
    }

    // Conversione ADC → Volt → Resistenza → Lux (stessa formula Nordic)
    measure_mean = ((double)(4095 - samples_mean) * saadc_lsb) / SENSOR_CONVERSION_RESISTANCE;

    // Aggiorna indice debug (stesso comportamento Nordic)
    if (++measure_index == 8) { // SLOT_COUNT/2 dal codice Nordic
        measure_index = 0;
    }

    ESP_LOGD(TAG, "ADC processing - Mean: %lu, Value: %.6f", samples_mean, measure_mean);
}

/**
 * @brief Callback timer campionamento periodico
 */
static void luxmeter_timer_callback(TimerHandle_t xTimer) {
    if (!conversion_active || sample_count >= SAMPLES_PER_CHANNEL) {
        return;
    }

    // Lettura valore ADC
    int adc_value;
    esp_err_t ret = adc_oneshot_read(adc_handle, LUX_SENSOR_ADC_CHANNEL, &adc_value);

    if (ret == ESP_OK) {
        samples_buffer[sample_count++] = adc_value;

        // Elabora quando buffer è completo
        if (sample_count >= SAMPLES_PER_CHANNEL) {
            luxmeter_process_adc_buffer();
            sample_count = 0;
        }
    }
}

/**
 * @brief Inizializzazione sistema ADC (equivalente a SAADC Nordic)
 */
static void luxmeter_adc_init(void) {
    ESP_LOGI(TAG, "Initializing ADC for light sensor");

    // Configurazione unità ADC
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT,
    };

    esp_err_t ret = adc_oneshot_new_unit(&init_config, &adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC unit initialization failed: %s", esp_err_to_name(ret));
        return;
    }

    // Configurazione canale ADC (come Nordic)
    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,    // Range 0-3.3V
        .bitwidth = ADC_BITWIDTH_12, // 12-bit resolution
    };

    ret = adc_oneshot_config_channel(adc_handle, LUX_SENSOR_ADC_CHANNEL, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC channel configuration failed: %s", esp_err_to_name(ret));
        return;
    }

    // Inizializzazione variabili
    sample_count = 0;
    measure_mean = 0.0;
    measure_index = 0;

    ESP_LOGI(TAG, "ADC initialized - Channel: %d", LUX_SENSOR_ADC_CHANNEL);
}

/**
 * @brief Inizializzazione timer campionamento (equivalente a PPI Nordic)
 */
static void luxmeter_sampling_timer_init(void) {
    // Timer per campionamento periodico
    sampling_timer = xTimerCreate(
        "LuxmeterSamplingTimer",
        pdMS_TO_TICKS(TIMER_INTERVAL_US / 100),
        pdTRUE,
        NULL,
        luxmeter_timer_callback
    );

    if (sampling_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create sampling timer");
        return;
    }

    ESP_LOGD(TAG, "Sampling timer initialized - Interval: %d μs", TIMER_INTERVAL_US);
}

/************************************************
 * PUBLIC FUNCTIONS IMPLEMENTATION             *
 ************************************************/

/**
 * @brief Inizializzazione completa sistema luxmeter
 */
void luxmeter_init(void) {
    ESP_LOGI(TAG, "🚀 Initializing Luxmeter system (Real mode only)");

    luxmeter_adc_init();
    luxmeter_sampling_timer_init();

    conversion_active = false;

    ESP_LOGI(TAG, "✅ Luxmeter system initialized - Ready for real measurements");
}

/**
 * @brief Acquisizione misurazione luminosa (stesso comportamento Nordic)
 */
void luxmeter_pickup(luxmeter_measure_t measure, uint16_t pwm_level, uint32_t *lux, uint32_t *index) {
    uint32_t lux_value;
    uint32_t offset = 0;

    // Conversione valore elaborato in lux (stessa formula Nordic)
    lux_value = (uint32_t)pow(10.0, measure_mean / 10e-6);

    // Applicazione compensazione offset (stessa mappa Nordic)
    if (pwm_level < sizeof(offset_map)) {
        offset = (offset_map[pwm_level] > lux_value) ? lux_value : offset_map[pwm_level];
    }

    lux_value -= offset;

    // Restituzione risultati
    *index = measure_index;
    *lux = lux_value;

    ESP_LOGD(TAG, "Lux measurement - Type: %d, PWM: %d, Value: %lu, Offset: %lu",
             measure, pwm_level, *lux, offset);
}

/**
 * @brief Avvia acquisizione continua
 */
void luxmeter_start_acquisition(void) {
    conversion_active = true;
    sample_count = 0;

    if (sampling_timer && !xTimerIsTimerActive(sampling_timer)) {
        xTimerStart(sampling_timer, 0);
    }

    ESP_LOGI(TAG, "🎯 Continuous acquisition started");
}

/**
 * @brief Arresta acquisizione continua
 */
void luxmeter_stop_acquisition(void) {
    conversion_active = false;

    if (sampling_timer && xTimerIsTimerActive(sampling_timer)) {
        xTimerStop(sampling_timer, 0);
    }

    ESP_LOGI(TAG, "⏹️ Continuous acquisition stopped");
}