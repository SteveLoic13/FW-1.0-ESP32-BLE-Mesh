/**
 * Autore: DJITSOP FUOGOUK LOIC STEVE
 * Firmware: ECOLUMIERE BLE MESH ESP32
 * Implementazione: Lightcode - Sistema comunicazione ottica
 * Basato su: Implementazione Nordic originale
 */

#include "lightcode.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

/************************************************
 * PRIVATE DEFINES AND MACRO                   *
 ************************************************/
static const char* TAG = "LIGHTCODE";

#define SENSE_DIGITAL_IN_PIN            27      // GPIO sensore luce digitale
#define DEBUG_PIN                       12      // GPIO debug (opzionale)

/************************************************
 * PRIVATE GLOBAL VARIABLES                    *
 ************************************************/

static uint8_t sense_queue_0[SENSE_QUEUE_SIZE]; // Buffer campioni primario
static uint8_t *queue_ptr = NULL;               // Puntatore buffer attivo
static uint32_t queue_index = 0;                // Indice scrittura corrente

static uint8_t mean_buffer[MEAN_SIZE];          // Buffer filtro media mobile
static bool mean_buffer_initialized = false;    // Flag inizializzazione filtro

static esp_timer_handle_t light_code_timer = NULL;  // Timer campionamento

/************************************************
 * PRIVATE FUNCTIONS IMPLEMENTATION           *
 ************************************************/

/**
 * @brief Callback timer campionamento ad alta frequenza
 * @desc Eseguito ogni 15μs per acquisire stato sensore luce.
 *       Identico al comportamento Nordic originale.
 */
static void light_code_timer_callback(void *arg) {
    if (queue_index < SENSE_QUEUE_SIZE) {
        // Lettura stato pin sensore digitale (come Nordic)
        int pin_state = gpio_get_level(SENSE_DIGITAL_IN_PIN);
        queue_ptr[queue_index++] = (uint8_t)pin_state;
    }
}

/**
 * @brief Reset sistema acquisizione per nuovo frame
 * @desc Identico al comportamento Nordic originale
 */
void light_code_reset_queue(void) {
    queue_index = 0;

    // Inizializzazione buffer filtro al primo reset (come Nordic)
    if (!mean_buffer_initialized) {
        memset(mean_buffer, 0, sizeof(mean_buffer));
        mean_buffer_initialized = true;
    }
}

/**
 * @brief Applica filtro media mobile ai campioni acquisiti
 * @desc Implementa ESATTAMENTE lo stesso algoritmo del Nordic originale
 */
void light_code_pickup(void) {
    if (queue_ptr == NULL || queue_index == 0) {
        return;
    }

    uint8_t temp_buffer[SENSE_QUEUE_SIZE];

    // Applicazione filtro media mobile - ALGORITMO IDENTICO A NORDIC
    for (uint8_t i = 0; i < SENSE_QUEUE_SIZE; i++) {
        uint8_t mean = 0;

        // Shift buffer media e accumulo valori - IDENTICO A NORDIC
        for (uint8_t j = 1; j < MEAN_SIZE; j++) {
            mean_buffer[j - 1] = mean_buffer[j];
            mean += mean_buffer[j];
        }

        // Inserimento nuovo campione e calcolo media - IDENTICO A NORDIC
        mean_buffer[MEAN_SIZE - 1] = queue_ptr[i];
        mean += mean_buffer[0];

        // Sostituzione campione con valore filtrato - IDENTICO A NORDIC
        temp_buffer[i] = (uint8_t)(((float)mean / (float)MEAN_SIZE) + 0.5);
    }

    // Copia buffer filtrato nel buffer principale
    memcpy(queue_ptr, temp_buffer, SENSE_QUEUE_SIZE);
}

/**
 * @brief Decodifica segnale ottico in codice dati
 * @return Codice decodificato (0 in caso di errore)
 * @desc Implementa ESATTAMENTE lo stesso algoritmo del Nordic originale
 */
uint8_t light_code_check(void) {

    static uint32_t count;
    static uint32_t bits;
    static uint8_t code;
    uint8_t bit_value = 0;

    // Inizializzazione come in Nordic originale
    code = 0;
    count = 0;
    bits = 0;

    // Scansione range ESATTAMENTE come Nordic: 20-80
    for (uint32_t i = 20; i < 80; i++) {
        if (i >= SENSE_QUEUE_SIZE) {
            break;  // Safety check
        }

        if (queue_ptr[i] == bit_value) {
            count++;
        } else {
            count = 0;
            bit_value = queue_ptr[i];  // Aggiorna bit_value quando cambia
        }

        // Logica ESATTAMENTE come Nordic originale
        if (count >= 5) {
            if (bits < 8) {
                code |= (bit_value << (7 - bits));  // MSB first - IDENTICO A NORDIC
            }

            bits++;
            bit_value = (bit_value) ? 0 : 1;  // Alterna bit - IDENTICO A NORDIC
            count = 0;
        }
    }

    // Verifica ESATTAMENTE come Nordic originale
    if (bits < 6 || bits > 7) {
        return 0;
    }

    return code & LIGHT_CODE_MASK;
}

/**
 * @brief Inizializzazione sistema comunicazione ottica
 * @desc Configura hardware sensore e timer campionamento.
 *       Basato su architettura Nordic con adattamento ESP32.
 */
void light_code_init(void) {
    esp_err_t ret;

    ESP_LOGI(TAG, "Initializing Lightcode communication system");

    // Inizializzazione strutture dati
    queue_ptr = sense_queue_0;
    queue_index = 0;
    mean_buffer_initialized = false;

    // Configurazione GPIO sensore input (come Nordic)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << SENSE_DIGITAL_IN_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure sensor GPIO: %s", esp_err_to_name(ret));
        return;
    }

    // Configurazione timer campionamento ad alta frequenza (15μs come Nordic)
    const esp_timer_create_args_t timer_args = {
        .callback = &light_code_timer_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "lightcode_sampling_timer",
    };

    ret = esp_timer_create(&timer_args, &light_code_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create sampling timer: %s", esp_err_to_name(ret));
        return;
    }

    // Timer periodico a 15μs (stesso periodo Nordic)
    ret = esp_timer_start_periodic(light_code_timer, 15);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start sampling timer: %s", esp_err_to_name(ret));
        esp_timer_delete(light_code_timer);
        light_code_timer = NULL;
        return;
    }

    // Reset iniziale sistema
    light_code_reset_queue();

    ESP_LOGI(TAG, "Lightcode system initialized successfully");
    ESP_LOGI(TAG, "Sampling rate: 15μs, Buffer size: %d samples", SENSE_QUEUE_SIZE);
}