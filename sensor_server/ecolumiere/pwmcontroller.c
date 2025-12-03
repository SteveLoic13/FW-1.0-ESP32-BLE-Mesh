/**
 * Autore: DJITSOP FUOGOUK LOIC STEVE
 * Firmware: ECOLUMIERE BLE MESH ESP32
 * Implementazione: PWM Controller - Sistema controllo illuminazione BLE Mesh
 */

#include "driver/ledc.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "pwmcontroller.h"
#include "luxmeter.h"
#include "ecolumiere.h"
#include "lightcode.h"
#include "slave_role.h"
#include "datarecorder.h"
#include "config.h"

/************************************************
 * PRIVATE DEFINES AND MACRO                   *
 ************************************************/
static const char *TAG = "PWMCONTROLLER";

#define PWM_OUT_PIN             5      // GPIO per uscita PWM principale
#define DIM_CTRL_PIN            21      // GPIO per controllo dimming (opzionale)
#define SLOT_TIME_MS            500     // Durata di ogni slot temporale in ms


/************************************************
 * PRIVATE TYPES AND STRUCTURES                *
 ************************************************/

/**
 * @brief Struttura stato interno sistema PWM - OTTIMIZZATA
 */
static struct {
    bool broadcast;
    uint16_t device_code;
    uint8_t current_slot;
    uint16_t light_level;
    uint16_t target_duty;
    uint16_t sequence_0[PWM_SEQUENCE_LEN];
    uint16_t sequence_1[PWM_SEQUENCE_LEN];
    uint8_t active_sequence;
    uint16_t *current_sequence;
    uint16_t current_pwm_hw;

    // 🔥 NUOVI CONTATORI PER RIDURRE FREQUENZA OPERAZIONI
    uint8_t fade_counter;
    uint8_t env_measure_counter;
    uint8_t sequence_update_counter;
    uint32_t log_counter;
} pwm_state;

/************************************************
 * PRIVATE GLOBAL VARIABLES                    *
 ************************************************/
static bool pwm_initialized = false;

static TimerHandle_t slot_timer;

/************************************************
 * PRIVATE HARDWARE CONFIGURATION              *
 ************************************************/

/**
 * @brief Configurazione timer LEDC per generazione PWM
 */
static const ledc_timer_config_t ledc_timer = {
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .timer_num = LEDC_TIMER_0,
    .duty_resolution = LEDC_TIMER_13_BIT,
    .freq_hz = 1000,
    .clk_cfg = LEDC_AUTO_CLK
};

/**
 * @brief Configurazione canale LEDC per output PWM
 */
static const ledc_channel_config_t ledc_channel = {
    .gpio_num = PWM_OUT_PIN,
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .channel = LEDC_CHANNEL_0,
    .intr_type = LEDC_INTR_DISABLE,
    .timer_sel = LEDC_TIMER_0,
    .duty = 0,
    .hpoint = 0
};

/************************************************
 * PRIVATE FUNCTIONS IMPLEMENTATION           *
 ************************************************/

/**
 * @brief Aggiorna sequenza PWM in base all'evento corrente
 */
static void pwm_sequence_update(uint8_t event) {
    uint16_t *sequence = pwm_state.current_sequence;

    for (uint16_t i = 0; i < PWM_SEQUENCE_LEN; i++) {
        if (event == NATURAL_MEASURE_EVENT || event == ENV_MEASURE_EVENT) {
            sequence[i] = 0x1FFF;  // PWM OFF per misurazioni
        }
        else if (event == DEFAULT_EVENT) {
            if (i < pwm_state.light_level) {
                sequence[i] = 0x0000;  // ON per primi light_level campioni
            } else {
                sequence[i] = 0x1FFF;  // OFF per campioni rimanenti
            }
        }
        else {
            sequence[i] = 0x1FFF;  // PWM OFF
        }
    }
}

/**
 * @brief Applica sequenza corrente all'hardware PWM
 */
static void pwm_apply_current_sequence(void) {
    if (!pwm_initialized) return;

    uint32_t sum_on = 0;
    for (int i = 0; i < PWM_SEQUENCE_LEN; i++) {
        if (pwm_state.current_sequence[i] == 0x0000) {
            sum_on++;
        }
    }

    uint32_t duty = (sum_on * PWM_MAX_VALUE) / PWM_SEQUENCE_LEN;

    if (duty != pwm_state.current_pwm_hw) {
        ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, duty);
        ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
        pwm_state.current_pwm_hw = duty;

        ESP_LOGD(TAG, "Sequence applied: %lu/%lu ON samples, duty=%lu",
                 sum_on, PWM_SEQUENCE_LEN, duty);
    }
}

/**
 * @brief Gestisce slot comunicazione device ID
 */
static void handle_device_id_slot(void) {
    light_code_pickup();
    uint8_t received_code = light_code_check();

    algo_sched_event_t event = {
        .source = LUX_SOURCE_DEVICE_ID,
        .code = received_code
    };
    ecolumiere_update_lux(&event, sizeof(event));

    if (received_code == LIGHT_CODE_ONE) {
        ESP_LOGD(TAG, "Master signal detected - Code: 0x%02X", received_code);
    }
}

/**
 * @brief Gestisce slot misurazione luce naturale
 */
static void handle_natural_light_slot(void) {
    uint32_t natural_lux, index;

    luxmeter_pickup(LUX_MEASURE_NATURAL, pwm_state.light_level, &natural_lux, &index);

    if (natural_lux != MEASURE_INVALID) {
        algo_sched_event_t event = {
            .source = LUX_SOURCE_NATURAL,
            .measure = natural_lux
        };
        ecolumiere_update_lux(&event, sizeof(event));

        ESP_LOGD(TAG, "Natural light: %lu lux", natural_lux);
    }
}

/**
 * @brief Gestisce slot misurazione luce ambiente
 */
static void handle_env_light_slot(void) {
    uint32_t env_lux, index;

    luxmeter_pickup(LUX_MEASURE_ENVIRONMENT, pwm_state.light_level, &env_lux, &index);

    if (env_lux != MEASURE_INVALID) {
        algo_sched_event_t event = {
            .source = LUX_SOURCE_ENVIRONMENT,
            .measure = env_lux
        };
        ecolumiere_update_lux(&event, sizeof(event));

        ecolumiere_algo_process();

        ESP_LOGD(TAG, "Environment light: %lu lux - Algorithm triggered", env_lux);
    }
}

/**
 * @brief Applica transizione graduale verso livello target
 */
static void apply_fade(void) {
    if (pwm_state.light_level < pwm_state.target_duty) {
        pwm_state.light_level++;
    }

    if (pwm_state.light_level > pwm_state.target_duty) {
        pwm_state.light_level--;
    }
}

/**
 * @brief Callback timer slot - VERSIONE OTTIMIZZATA
 */
static void slot_timer_callback(TimerHandle_t timer) {
    // 🔥 CONTROLLO SICUREZZA RINFORZATO
    if (!pwm_initialized || !timer) {
        return;
    }

    // 🔥 OTTIMIZZAZIONE: Salta ogni secondo callback
    static uint8_t skip_counter = 0;
    if (++skip_counter < 2) {
        pwm_advance_slot();
        return;
    }
    skip_counter = 0;

    uint8_t current_slot = pwm_get_current_slot();

    // 🔥 FADE - SOLO OGNI 4 SLOT (2 secondi)
    if (++pwm_state.fade_counter >= 4) {
        apply_fade();
        pwm_state.fade_counter = 0;
    }

    // 🔥 GESTIONE SLOT MOLTO RIDOTTA
    switch (current_slot) {
        case DEVICE_ID_SLOT:
            // Device ID - solo ogni 4 cicli (8 secondi)
            static uint8_t device_id_counter = 0;
            if (++device_id_counter >= 4) {
                handle_device_id_slot();
                device_id_counter = 0;
            }
            break;

        case NATURAL_MEASURE_SLOT:
            // Misura naturale - solo ogni 2 cicli (4 secondi)
            static uint8_t natural_measure_counter = 0;
            if (++natural_measure_counter >= 2) {
                handle_natural_light_slot();
                natural_measure_counter = 0;
            }
            break;

        case ENV_MEASURE_SLOT:
            // Misura ambiente - solo ogni ciclo (2 secondi)
            if (++pwm_state.env_measure_counter >= 1) {
                handle_env_light_slot();
                pwm_state.env_measure_counter = 0;
            }
            break;

        default:
            // Slot vuoti - nessuna operazione
            break;
    }

    // 🔥 SEQUENCE UPDATE - SOLO OGNI 2 CALLBACK (1 secondo)
    if (++pwm_state.sequence_update_counter >= 2) {
        pwm_sequence_update(DEFAULT_EVENT);
        pwm_apply_current_sequence();
        pwm_state.sequence_update_counter = 0;
    }

    pwm_advance_slot();

    // 🔥 LOG MOLTO RIDOTTO - solo ogni 20 callback (10 secondi)
    if (++pwm_state.log_counter >= 20) {
        ESP_LOGD(TAG, "Slot %d - PWM: %d/%d",
                 current_slot, pwm_state.light_level, pwm_state.target_duty);
        pwm_state.log_counter = 0;
    }
}

/************************************************
 * PUBLIC FUNCTIONS IMPLEMENTATION             *
 ************************************************/

/**
 * @brief Inizializza sistema PWM controller - VERSIONE SICURA
 */
/**
 * @brief Inizializza sistema PWM controller - VERSIONE SICURA
 */
esp_err_t pwmcontroller_init(void) {

    if (pwm_initialized) return ESP_OK;

    ESP_LOGI(TAG, "Initializing PWM Controller (SLAVE mode) - OPTIMIZED");

    // ✅ CORREZIONE: USA ledc_timer
    esp_err_t ret = ledc_timer_config(&ledc_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // ✅ CORREZIONE: USA ledc_channel
    ret = ledc_channel_config(&ledc_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC channel config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 🔥 INIZIALIZZA STATO CON CONTATORI
    memset(&pwm_state, 0, sizeof(pwm_state)); // Reset completo

    pwm_state.device_code = 0x0055;
    pwm_state.current_slot = 0;
    pwm_state.light_level = 0;
    pwm_state.target_duty = 0;
    pwm_state.current_pwm_hw = 0xFFFF;
    pwm_state.fade_counter = 0;
    pwm_state.env_measure_counter = 0;
    pwm_state.sequence_update_counter = 0;
    pwm_state.log_counter = 0;

    for (int i = 0; i < PWM_SEQUENCE_LEN; i++) {
        pwm_state.sequence_0[i] = 0x1FFF;
        pwm_state.sequence_1[i] = 0x1FFF;
    }
    pwm_state.current_sequence = pwm_state.sequence_0;
    pwm_state.active_sequence = 0;

    // 🔥 CREA TIMER CON STACK AUMENTATO
    slot_timer = xTimerCreate(
        "PWMSlotTimer",
        pdMS_TO_TICKS(SLOT_TIME_MS), // 500ms
        pdTRUE,
        NULL,
        slot_timer_callback
    );

    if (slot_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create slot timer");
        return ESP_FAIL;
    }

    // 🔥 AVVIA TIMER CON DELAY PER STABILIZZAZIONE
    if (xTimerStart(slot_timer, pdMS_TO_TICKS(2000)) != pdPASS) { // Start dopo 2 secondi
        ESP_LOGE(TAG, "Failed to start slot timer");
        return ESP_FAIL;
    }

    pwm_initialized = true;

    // ✅ CORREZIONE: USA identity nel log
    const slave_identity_t *identity = slave_node_get_identity();
    ESP_LOGI(TAG, "PWM Controller initialized - OPTIMIZED MODE");
    ESP_LOGI(TAG, "Device: %s, Timer: %dms/slot",
             identity->device_name, SLOT_TIME_MS);

    return ESP_OK;
}

/**
 * @brief Imposta duty cycle target per dimming
 */
void pwm_set_duty_cycle(uint32_t duty_cycle) {

    if (!pwm_initialized) {
        ESP_LOGE(TAG, "PWM not initialized - call pwmcontroller_init() first");
        return;
    }

    if (duty_cycle > LIGHT_MAX_LEVEL) {
        duty_cycle = LIGHT_MAX_LEVEL;
    }

    pwm_state.target_duty = duty_cycle;
    data_recorder_push_history_data((uint8_t)duty_cycle);

    const slave_identity_t *identity = slave_node_get_identity();
    ESP_LOGI(TAG, "PWM target duty set - Device: %s, Duty Cycle: %lu/%d",
             identity->device_name, duty_cycle, LIGHT_MAX_LEVEL);
}

/**
 * @brief Restituisce ruolo dispositivo
 */
device_id_role_t pwm_get_id_role(void) {
    return ROLE_ID_RECEIVER;
}

/**
 * @brief Imposta ruolo dispositivo
 */
void pwm_set_id_role(device_id_role_t role) {
    if (role != ROLE_ID_RECEIVER) {
        ESP_LOGW(TAG, "Role change ignored - SLAVE mode only supported");
    }
    ESP_LOGI(TAG, "Device role: SLAVE (fixed)");
}

/**
 * @brief Ferma sistema PWM - VERSIONE SICURA
 */
void pwm_stop(void) {
    if (pwm_initialized) {
        if (slot_timer) {
            // 🔥 FERMA TIMER IN MODO SICURO
            xTimerStop(slot_timer, portMAX_DELAY);
            // 🔥 ATTENDI CHE IL TIMER SI FERMI COMPLETAMENTE
            while(xTimerIsTimerActive(slot_timer) != pdFALSE) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            // 🔥 ELIMINA TIMER
            xTimerDelete(slot_timer, portMAX_DELAY);
            slot_timer = NULL;
        }
        pwm_initialized = false;
        ESP_LOGI(TAG, "PWM system stopped safely");
    }
}

/**
 * @brief Restituisce livello luminoso corrente
 */
uint16_t pwmcontroller_get_current_level(void) {
    return pwm_state.light_level;
}

/**
 * @brief Applica duty cycle sincronizzato con zero-cross
 */
void pwm_apply_phase_controlled_duty(void) {
    if (!pwm_initialized) return;

    pwm_apply_current_sequence();
    ESP_LOGD(TAG, "Zero-cross: Applied phase-controlled duty");
}

/**
 * @brief Restituisce indirizzo task playback PWM
 */
uint32_t pwm_get_playback_task(void) {
    return (uint32_t)&pwm_apply_phase_controlled_duty;
}

/**
 * @brief Salva il livello PWM corrente nella configurazione persistente
 */
static void save_pwm_level_to_config(uint8_t level) {

    extern void ecolumiere_save_current_pwm(uint16_t pwm_level);

    ecolumiere_save_current_pwm(level);

    ESP_LOGI(TAG, "💾 PWM level saved to config: %d", level);
}

/**
 * @brief Imposta livello PWM per compatibilità BLE Mesh
 */
void pwmcontroller_set_level(uint8_t level) {
    ESP_LOGI("PWM", "🎛️ pwmcontroller_set_level CALLED with: %d", level);
    ESP_LOGI("PWM", "📍 Called from: %p", __builtin_return_address(0));

    pwm_set_duty_cycle(level);

    save_pwm_level_to_config(level);

    const slave_identity_t *identity = slave_node_get_identity();
    ESP_LOGI(TAG, "BLE Mesh level set - Device: %s, Level: %d/32",
             identity->device_name, level);
}

/**
 * @brief Restituisce slot corrente
 */
uint8_t pwm_get_current_slot(void) {
    return pwm_state.current_slot;
}

/**
 * @brief Avanza al prossimo slot
 */
void pwm_advance_slot(void) {
    pwm_state.current_slot = (pwm_state.current_slot + 1) % SLOT_COUNT;
}

/**
 * @brief Imposta livello output PWM diretto
 */
void pwm_set_output_level(uint8_t level) {
    if (level == 0) {
        // Logica specifica per sicurezza
    }
}

/**
 * @brief Applica fade graduale (wrapper pubblico)
 */
void pwm_fade(void) {
    apply_fade();
}

/**
 * @brief Verifica se PWM è inizializzato
 */
bool is_pwm_initialized(void) {
    return pwm_initialized;
}

/**
 * @brief Converte intensità luminosa (0-100) in PWM (0-32)
 */
uint8_t convert_intensity_to_pwm(uint16_t intensity) {
    if (intensity == 0) return 0;
    uint8_t pwm = (intensity * 32) / 100;
    return (pwm > 0) ? pwm : 1;
}