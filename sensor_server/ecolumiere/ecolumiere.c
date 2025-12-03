//
// Created by Admin on 20/10/2025.
//

#include "driver/gpio.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "ecolumiere.h"
#include "storage.h"
#include "pwmcontroller.h"
#include "slave_role.h"
#include "lightcode.h"
#include "config.h"
#include "datarecorder.h"
#include "scheduler.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

// Macro stampe condizionate
#ifdef CONFIG_ECO_DEBUG
#define ECO_LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#define ECO_LOGD(...) ESP_LOGD(TAG, __VA_ARGS__)
#else
#define ECO_LOGI(...)
#define ECO_LOGD(...)
#endif
#define ECO_LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)

static QueueHandle_t scheduler_queue = NULL;

static const char* TAG = "ECOLUMIERE";

// Configurazioni
#define ECO_DEBUG_OUTPUT                5
#define NATURAL_LUX_AVG_ORDER           50
#define ENV_LUX_AVG_ORDER               50
#define SLAVE_LIGHT_MAX_LEVEL           32
#define SLAVE_LIGHT_MIN_LEVEL           3
#define FOLLOW_UP_TIMEOUT_MS            (3 * 60 * 1000)

////////////////////////////////////////////////////////////

#define SLAVE     1  // sempre SLAVE

// Aggiungi dopo le altre variabili statiche in ecolumiere.c
static bool mesh_override_active = false;
static uint32_t mesh_override_timeout = 0;
static uint8_t mesh_override_level = 0;
#define MESH_OVERRIDE_DURATION_MS (30 * 1000) // 30 secondi

///////////////////////////////////////////////////////////

#if defined SLAVE
 #define ALGO_AVG                        10
#else
 #if 0
  #define ALGO_AVG                      10
 #else
  #define ALGO_AVG                      240
 #endif
 #define LAST_ALGO_AVG                  10
#endif

#define ALGO_AVG_LIVE                   10
#define ALGO_AVG_LAST                   20
#define LAMP_MAX_LUX                    600
#define POWER_EFFICIENCY                18.75
#define LAMP_DISTANCE_M                 1
#define DEFAULT_TARGET_LUX              400
#define CONFIG_CRC_INIT_VALUE           0xFFFF
#define CODE_WINDOW_PRESCALER           20


#define TEST_TARGET_LUX_TO_ENTER        0xE1F1AA10
#define TEST_ALGO_AVG                   10
#define TEST_TARGET_LUX                 400
#define TEST_TARGET_EFFICIENCY          18.75f
#define TEST_TARGET_DISTANCE            2.5f
#define TEST_TARGET_IN_PL               1
#define TEST_TARGET_DIMM_STEP           0.3f
#define TEST_TARGET_PERC_MIN            0.01f
#define TEST_TARGET_TRANSPARENCY        1
#define TEST_TARGET_EMAX                2000

#if defined SET_DEFAULT_VALUES
  #define DEFALUT_TARGET_LUX                 50
  #define DEFALUT_TARGET_EFFICIENCY          18.75f
  #define DEFALUT_TARGET_DISTANCE            2.5f
  #define DEFALUT_TARGET_IN_PL               2
  #define DEFALUT_TARGET_DIMM_STEP           0.1f
  #define DEFALUT_TARGET_PERC_MIN            0.20f
  #define DEFALUT_TARGET_TRANSPARENCY        1
#endif

typedef struct measure_avg_t
{
  uint32_t sum;
  uint8_t size;
  uint8_t count;
  int32_t measure;
} measure_avg_t;

typedef struct algo_avg_t
{
  uint32_t natural_sum;
  uint32_t env_sum;
  uint8_t count;
  uint8_t size;
  float enatural;
  float eenv;
} algo_avg_t;

typedef struct algo_data_t
{
  uint32_t target_lux;
  float perc_min;
  float distance;
  uint32_t in_pl;
  float power_efficiency;
  float transparency;
  float dimm_step;
  float variation;
  float elamp;
  float emax;
  float enew;
  float pnew;
  float emin;
  float enatural;
  float eenv;
} algo_data_t;

static measure_avg_t natural_avg;
static measure_avg_t env_avg;
static algo_avg_t algo_avg_live;
static algo_avg_t algo_avg;
static algo_data_t algo_data;
static algo_config_data_t algo_config_data;
static ecl_registry_t ecl_registry;
static uint8_t code_window[CODE_WINDOW_SIZE];
static bool test_on = false;

#if defined SLAVE
static esp_timer_handle_t follow_up_timer_id;
#endif

static QueueHandle_t scheduler_queue;

static float calculate_initial_pwm(void);

/**
 * @brief Gestione degli eventi nel main loop
 */
void ecolumiere_app_sched_event_handler(void *p_event_data)
{
  ecl_live_t *ecl_live = (ecl_live_t *)p_event_data;

  if (ecl_live == NULL) {
    ESP_LOGW("ECOLUMIERE", "NULL event data in handler");
    return;
  }

  // ✅ AGGIORNA SCAN RESPONSE
  //multirole_scan_response_update(ecl_live);

 // In BLE Mesh i dati si inviano via pubblicazione, non scan response
    ESP_LOGW(TAG, "Scan response update disabled - BLE Mesh uses publishing");
}

#if defined SLAVE
/**
 * @Callback per la gestione dei timeout del timer
 */
static void ecolumiere_follow_up_timeout_timer_callback(void *p_context)
{
  pwm_set_duty_cycle(SLAVE_LIGHT_MIN_LEVEL);
}
#endif

static void ecolumiere_save_algo_config(void)
{

  uint16_t init_crc = CONFIG_CRC_INIT_VALUE;
  algo_config_data.crc = esp_rom_crc16_le(init_crc, (uint8_t *)&algo_config_data, sizeof(algo_config_data_t) - sizeof(uint16_t));
  storage_save_config(&algo_config_data);

  // BLE Mesh non usa advertising standard - l'identità viene gestita durante il provisioning
  ESP_LOGI("ECOLUMIERE", "Device identity ready for BLE Mesh provisioning");
  // Se vuoi loggare l'identità, puoi aggiungere:
  slave_node_log_identity();

}

static void ecolumiere_update_algo_data(void)
{
  if (ecolumiere_has_valid_config())
  {
    algo_data.target_lux = algo_config_data.target_lux;
    algo_data.power_efficiency = algo_config_data.efficiency;
    algo_data.distance = algo_config_data.distance;
    algo_data.in_pl = algo_config_data.in_pl;
    algo_data.transparency = algo_config_data.transparency;
    algo_data.emax = ((float)(LIGHT_MAX_LEVEL) * algo_config_data.efficiency * algo_config_data.transparency) / (algo_config_data.distance * algo_config_data.distance);
    algo_data.dimm_step = algo_config_data.dimm_step;
    algo_data.perc_min = algo_config_data.perc_min;
  }
  else
  {
    algo_data.target_lux = 0;
    algo_data.power_efficiency = 0;
    algo_data.distance = 0;
    algo_data.in_pl = 0;
    algo_data.transparency = 0;
    algo_data.emax = 0;
    algo_data.dimm_step = 0;
    algo_data.perc_min = 0;
  }

  if (test_on)
  {
    algo_data.target_lux = TEST_TARGET_LUX;
    algo_data.power_efficiency = TEST_TARGET_EFFICIENCY;
    algo_data.distance = TEST_TARGET_DISTANCE;
    algo_data.in_pl = TEST_TARGET_IN_PL;
    algo_data.transparency = TEST_TARGET_TRANSPARENCY;
    algo_data.emax = TEST_TARGET_EMAX;
    algo_data.dimm_step = TEST_TARGET_DIMM_STEP;
    algo_data.perc_min = TEST_TARGET_PERC_MIN;
  }

  if (algo_data.in_pl != 2)
  {
    algo_data.in_pl = 1;
  }
}


static void ecolumiuere_avg_calulator(algo_avg_t *algo_avg)
{
  algo_avg->enatural = (float)(algo_avg->natural_sum / algo_avg->count) / algo_data.transparency;

  if (algo_data.in_pl == 2)
  {
    algo_avg->eenv = (float)(algo_avg->env_sum / algo_avg->count) / ((algo_data.distance * algo_data.distance) * algo_data.transparency);
  }
  else
  {
    algo_avg->eenv = (float)(algo_avg->env_sum / algo_avg->count) * ((algo_data.distance * algo_data.distance) / algo_data.transparency);
  }

  algo_avg->count = 0;
  algo_avg->natural_sum = 0;
  algo_avg->env_sum = 0;
}


void ecolumiere_algo_process(void) {

    static ecl_live_t ecl_live;

    // ✅ 1. VERIFICA OVERRIDE MESH (solo per ESP32)
    if (mesh_override_active) {
        uint32_t current_time = esp_timer_get_time() / 1000;
        if (current_time > mesh_override_timeout) {
            mesh_override_active = false;
            ESP_LOGI(TAG, "⏰ Override Mesh SCADUTO");
        } else {
            return; // Algoritmo sospeso durante override
        }
    }

    // ✅ 2. INIZIALIZZA E CONTROLLA TARGET
    ecolumiere_update_algo_data();

    // Slave può funzionare anche con target_lux = 0 (autonomia)
    // if (algo_data.target_lux == 0) return; // ❌ RIMOSSO per Slave

    // ✅ 3. ACCUMULA DATI PER MEDIE (originale Nordic)
    algo_avg.natural_sum += natural_avg.measure;
    algo_avg.env_sum += env_avg.measure;

    algo_avg_live.natural_sum += natural_avg.measure;
    algo_avg_live.env_sum += env_avg.measure;

    // ✅ 4. MEDIE LIVE (notifiche BLE - originale Nordic)
    if (++algo_avg_live.count == algo_avg_live.size) {
        ecolumiuere_avg_calulator(&algo_avg_live);

        #if defined SUSPEND_DEVICE_ID_NATURAL_SLOTS
        algo_avg_live.enatural = algo_avg_live.eenv;
        #endif

        algo_data.enatural = algo_avg_live.enatural;
        algo_data.eenv = algo_avg_live.eenv;

        #if !defined SUSPEND_DEVICE_ID_NATURAL_SLOTS
        if (algo_data.eenv < algo_data.enatural) {
            algo_data.eenv = algo_data.enatural;
        }
        #else
        algo_data.enatural = algo_data.eenv;
        #endif

        // Notifica dati live (se implementato)
        // ecolumiere_service_notify_algo_status((void *)&algo_data, sizeof(algo_data_t));

        // Aggiorna dati advertise
        ecl_live.natural = algo_avg_live.enatural;
        ecl_live.env = algo_avg_live.eenv;
        // app_sched_event_put(...);
    }

    // ✅ 5. ATTENDI CAMPIONI SUFFICIENTI (originale Nordic)
    if (++algo_avg.count < algo_avg.size) {
        ESP_LOGD(TAG, "📊 Accumulo campioni: %d/%d", algo_avg.count, algo_avg.size);
        return;
    }

    // ✅ 6. CALCOLA MEDIE PRINCIPALI (originale Nordic)
    #if defined ALGO_AVG_LAST
    algo_avg.count = ALGO_AVG_LAST;
    #endif

    ecolumiuere_avg_calulator(&algo_avg);

    #if defined SUSPEND_DEVICE_ID_NATURAL_SLOTS
    algo_avg.enatural = algo_avg.eenv;
    #endif

    algo_data.enatural = algo_avg.enatural;
    algo_data.eenv = algo_avg.eenv;

    if (algo_data.eenv < algo_data.enatural) {
        algo_data.eenv = algo_data.enatural;
    }

    // ✅ 7. ALGORITMO ORIGINALE NORDIC (MODELO FISICO)

    // A. Limite minimo
    algo_data.emin = algo_data.perc_min * algo_data.emax;

    // B. Lux attuali prodotti dalla lampada
    algo_data.elamp = ((float)algo_data.pnew * algo_data.power_efficiency * algo_data.transparency) /
                     (algo_data.distance * algo_data.distance);

    // C. Calcola variazione necessaria (formula ORIGINALE Nordic)
    #if defined SUSPEND_DEVICE_ID_NATURAL_SLOTS
    if (algo_data.eenv) {
        algo_data.variation = (float)(algo_data.target_lux - (algo_data.elamp + algo_data.eenv)) *
                            ((algo_data.eenv / (float)algo_data.target_lux) * algo_data.dimm_step);
    } else {
        algo_data.variation = (float)(algo_data.target_lux - (algo_data.elamp + algo_data.eenv)) *
                            (algo_data.dimm_step);
    }
    #else
    if (algo_data.enatural) {
        algo_data.variation = (float)(algo_data.target_lux - (algo_data.elamp + algo_data.eenv)) *
                            ((algo_data.enatural / (float)algo_data.target_lux) * algo_data.dimm_step);
    } else {
        algo_data.variation = (float)(algo_data.target_lux - (algo_data.elamp + algo_data.eenv)) *
                            (algo_data.dimm_step);
    }
    #endif

    // D. Nuovi lux desiderati
    algo_data.enew = algo_data.elamp + algo_data.variation;

    // Applica limite minimo
    if (algo_data.enew < algo_data.emin) {
        algo_data.enew = algo_data.emin;
    }

    // E. Converti lux → PWM (modello fisico inverso - ORIGINALE)
    algo_data.pnew = (algo_data.enew * algo_data.distance * algo_data.distance) /
                    (algo_data.power_efficiency * algo_data.transparency);

    // F. Applica limite massimo
    if (algo_data.pnew > LIGHT_MAX_LEVEL) {
        algo_data.pnew = LIGHT_MAX_LEVEL;
    }

    // ✅ 8. APPLICA NUOVO PWM
    ESP_LOGI(TAG, "🔧 ALGO NORDIC - Target: %lu, Natural: %.1f, Env: %.1f, PWM: %.1f→%.1f",
             algo_data.target_lux, algo_data.enatural, algo_data.eenv,
             algo_data.pnew, (float)algo_data.pnew);

    pwm_set_duty_cycle((uint32_t)algo_data.pnew);
    ecolumiere_save_current_pwm((uint16_t)algo_data.pnew);

    // ✅ 9. AGGIORNA NOTIFICHE FINALI (originale Nordic)
    algo_data.enatural = algo_avg_live.enatural;
    algo_data.eenv = algo_avg_live.eenv;

    // Notifica dati aggiornati
    // ecolumiere_service_notify_algo_status((void *)&algo_data, sizeof(algo_data_t));

    // Aggiorna dati advertise finali
    ecl_live.natural = algo_data.enatural;
    ecl_live.env = algo_data.eenv;
    ecl_live.lamp_lux = algo_data.enew;
    ecl_live.duty_cycle = (uint32_t)algo_data.pnew;
    // app_sched_event_put(...);

    // ✅ 10. RESETTA CONTATORI MEDIE
    algo_avg.count = 0;
    algo_avg.natural_sum = 0;
    algo_avg.env_sum = 0;

    ESP_LOGI(TAG, "✅ Algoritmo Nordic ORIGINALE completato");
}


void ecolumiere_update_lux(void *p_event_data, uint16_t event_size)
{
  static uint8_t code_prescaler = CODE_WINDOW_PRESCALER;
  static uint32_t counter = 0;

  algo_sched_event_t *algo_sched_event = (algo_sched_event_t *)p_event_data;
  measure_avg_t *measure_avg = NULL;

  switch (algo_sched_event->source)
  {
  case LUX_SOURCE_NATURAL:
    measure_avg = &natural_avg;
    break;
  case LUX_SOURCE_ENVIRONMENT:
    measure_avg = &env_avg;
    break;
  case LUX_SOURCE_DEVICE_ID:
    measure_avg = NULL;
    break;
  }

  if (algo_sched_event->source == LUX_SOURCE_DEVICE_ID)
  {
    if (--code_prescaler == 0)
    {
      for (uint8_t i = 1; i < CODE_WINDOW_SIZE; i++)
      {
        code_window[i - 1] = code_window[i];
      }
      code_window[CODE_WINDOW_SIZE - 1] = algo_sched_event->code;
      code_prescaler = CODE_WINDOW_PRESCALER;
      counter++;
    }
  }

  if (measure_avg == NULL) return;

  measure_avg->sum += algo_sched_event->measure;
  if (++measure_avg->count == measure_avg->size)
  {
    measure_avg->measure = measure_avg->sum / measure_avg->size;
    measure_avg->sum = 0;
    measure_avg->count = 0;
  }

  if (algo_sched_event->source == LUX_SOURCE_ENVIRONMENT && measure_avg->count == 0)
  {
    ecolumiere_algo_process();
  }
}

void ecolumiere_set_target(int32_t target)
{
  if (TEST_TARGET_LUX_TO_ENTER == target)
  {
    test_on = true;
    algo_avg.size = TEST_ALGO_AVG;
    return;
  }
  else if (target > 0)
  {
    algo_data.target_lux = target;
  }
  else if (target < 0)
  {
    algo_data.target_lux = 0;
    pwm_set_duty_cycle(abs(target));
  }
  else
  {
    algo_data.target_lux = 0;
    pwm_set_duty_cycle(0);
  }

  algo_config_data.target_lux = algo_data.target_lux;
  ecolumiere_save_algo_config();
}

bool ecolumiere_has_valid_config(void)
{
  uint16_t crc = CONFIG_CRC_INIT_VALUE;
  crc = esp_rom_crc16_le(crc, (uint8_t *)&algo_config_data, sizeof(algo_config_data_t) - sizeof(uint16_t));
  return (crc == algo_config_data.crc);
}

void ecolumiere_get_registry(uint16_t *device_id, uint16_t *company_id, uint16_t *crc)
{
  if (!device_id || !company_id || !crc) return;

  *device_id = ecl_registry.device_id;
  *company_id = ecl_registry.company_id;
  *crc = algo_config_data.crc;
}

void ecolumiere_set_registry(uint16_t device_id, uint16_t company_id)
{
  ecl_registry.company_id = company_id;
  ecl_registry.device_id = device_id;
  storage_save_registry(&ecl_registry);

  //multirole_advertise_update();
	// BLE Mesh non usa advertising standard - l'identità viene gestita durante il provisioning
	ESP_LOGI("ECOLUMIERE", "Device identity ready for BLE Mesh provisioning");
	// Se vuoi loggare l'identità, puoi aggiungere:
	slave_node_log_identity();

}

void ecolumiere_get_algo_config(algo_config_data_t *algo_config)
{
  memcpy(algo_config, &algo_config_data, sizeof(algo_config_data_t));
}

void ecolumiere_set_algo_config(algo_config_data_t *new_config)
{
  memcpy(&algo_config_data, new_config, sizeof(algo_config_data_t));
  ecolumiere_save_algo_config();
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////


/**
 * ✅ INIZIALIZZA STRUTTURE ALGORITMO
 */
static void initialize_algorithm_structures(void) {
    memset(&natural_avg, 0, sizeof(measure_avg_t));
    memset(&env_avg, 0, sizeof(measure_avg_t));
    memset(&algo_avg, 0, sizeof(algo_avg_t));
    memset(&algo_avg_live, 0, sizeof(algo_avg_t));

    natural_avg.size = NATURAL_LUX_AVG_ORDER;
    env_avg.size = ENV_LUX_AVG_ORDER;
    algo_avg.size = ALGO_AVG;
    algo_avg_live.size = ALGO_AVG_LIVE;
}

/**
 * ✅ CREA NUOVO REGISTRO
 */
static void create_new_registry(const slave_identity_t *identity) {
    ecl_registry_t new_registry;

    // ✅ COPIA SOLO I CAMPI NECESSARI PER IL REGISTRO
    new_registry.device_id = identity->device_id;
    new_registry.company_id = identity->company_id;
    new_registry.unicast_addr = identity->unicast_addr;
    new_registry.config_crc = identity->config_crc;
    strncpy(new_registry.device_name, identity->device_name, sizeof(new_registry.device_name));
    strncpy(new_registry.serial_number, identity->serial_number, sizeof(new_registry.serial_number));
    memcpy(new_registry.mac_address, identity->mac_address, sizeof(new_registry.mac_address));

    // Salva il nuovo registro
    if (storage_save_registry(&new_registry)) {
        ESP_LOGI("ECOLUMIERE", "💾 New registry saved: %s", identity->device_name);
    } else {
        ESP_LOGE("ECOLUMIERE", "❌ Failed to save registry");
    }
}

/**
 * ✅ GESTISCI IDENTITÀ DEL DISPOSITIVO
 */
static void handle_device_identity(void) {
    ESP_LOGI("ECOLUMIERE", "🔍 Loading device registry...");

    ecl_registry_t registry;
    storage_load_registry(&registry);

    const slave_identity_t *identity = slave_node_get_identity();

    // Verifica se il registro corrisponde all'identità MAC
    bool registry_matches = (registry.device_id == identity->device_id) &&
                           (registry.company_id == identity->company_id);

    if (!registry_matches) {
        ESP_LOGW("ECOLUMIERE", "🔄 Registry doesn't match MAC, creating new one");
        create_new_registry(identity);
    } else {
        ESP_LOGI("ECOLUMIERE", "✅ Registry matches MAC identity");
    }
}


/**
 * ✅ CREA CONFIGURAZIONE DEFAULT
 */
static void create_default_configuration(void) {
    // ✅ INIZIALIZZA CONFIGURAZIONE (algo_config_data_t - 30 bytes)
    memset(&algo_config_data, 0, sizeof(algo_config_data_t));

    // ✅ IMPOSTA VALORI DEFAULT
    algo_config_data.target_lux = 400;
    algo_config_data.efficiency = 18.75f;
    algo_config_data.distance = 1.0f;
    algo_config_data.in_pl = 1;
    algo_config_data.dimm_step = 0.1f;
    algo_config_data.perc_min = 0.01f;
    algo_config_data.transparency = 1.0f;

    // ✅ CALCOLA CRC
    uint16_t init_crc = 0xFFFF;
    algo_config_data.crc = esp_rom_crc16_le(init_crc,
        (uint8_t *)&algo_config_data, sizeof(algo_config_data_t) - sizeof(uint16_t));

    // ✅ SALVA CONFIGURAZIONE
    ecolumiere_save_algo_config();

    ESP_LOGI("ECOLUMIERE", "💾 Default configuration created and saved");
}


/**
 * @brief Salva il livello PWM corrente nella configurazione
 */
void ecolumiere_save_current_pwm(uint16_t pwm_level) {

	if(pwm_level != algo_config_data.current_pwm_level){

    algo_config_data.current_pwm_level = pwm_level;

    ecolumiere_save_algo_config();

    ESP_LOGI("ECOLUMIERE", "💾PWM level aggiornato con sucesso Nuovo Valore: %d", pwm_level);
	} else{

		ESP_LOGI("ECOLUMIERE", "💾PWM level non aggionato il valori sono gli stessi: %d", pwm_level);
	}
}


/**
 * ✅ GESTISCI CONFIGURAZIONE DEL DISPOSITIVO
 */
static void handle_device_configuration(void) {
    ESP_LOGI("ECOLUMIERE", "⚙️ Loading device configuration...");

    if (!storage_load_config(&algo_config_data)) {
        ESP_LOGW("ECOLUMIERE", "⚠️ No config found, creating defaults");
        create_default_configuration();
    } else {
        ESP_LOGI("ECOLUMIERE", "✅ Configuration loaded successfully");

        // ✅ CARICA IL VALORE PWM SALVATO
        if (algo_config_data.current_pwm_level >= 0 && algo_config_data.current_pwm_level <= LIGHT_MAX_LEVEL) {
            ESP_LOGI("ECOLUMIERE", "🔌 Restoring saved PWM level: %d", algo_config_data.current_pwm_level);
            pwm_set_duty_cycle(algo_config_data.current_pwm_level);
        } else {
			ESP_LOGW("ECOLUMIERE", "⚠️ Invalid saved PWM: %d, using default: 0", algo_config_data.current_pwm_level);
            pwm_set_duty_cycle(0);
        }
    }

    ecolumiere_update_algo_data();
}


/**
 * ✅ INIZIALIZZA COMPONENTI SISTEMA
 */
static void initialize_system_components(void) {
    // Crea coda scheduler
    scheduler_queue = xQueueCreate(10, sizeof(ecl_live_t *));
    if (!scheduler_queue) {
        ESP_LOGE("ECOLUMIERE", "❌ Failed to create scheduler queue");
        return;
    }

    // ✅ IMPOSTA VALORE INIZIALE INTELLIGENTE
    if (algo_config_data.current_pwm_level >= 0 && algo_config_data.current_pwm_level <= LIGHT_MAX_LEVEL) {
        // Usa valore salvato
        algo_data.pnew = (float)algo_config_data.current_pwm_level;
        ESP_LOGI(TAG, "🔌 PWM iniziale da memoria: %.1f", algo_data.pnew);
    } else {
        // ✅ CALCOLA VALORE INIZIALE BASATO SU CONFIGURAZIONE
        algo_data.pnew = calculate_initial_pwm();
        pwm_set_duty_cycle((uint32_t)algo_data.pnew);
        ecolumiere_save_current_pwm((uint16_t)algo_data.pnew);

        ESP_LOGI(TAG, "🎯 PWM iniziale calcolato: %.1f/32", algo_data.pnew);
    }

    // Avvia task scheduler
    if (xTaskCreate(ecolumiere_scheduler_task, "eco_scheduler", 4096, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE("ECOLUMIERE", "❌ Failed to create scheduler task");
        vQueueDelete(scheduler_queue);
        scheduler_queue = NULL;
        return;
    }

    // Crea timer follow-up (mantieni per sicurezza)
    const esp_timer_create_args_t timer_args = {
        .callback = &ecolumiere_follow_up_timeout_timer_callback,
        .name = "follow_up_timer"
    };
    esp_timer_create(&timer_args, &follow_up_timer_id);

    ESP_LOGI(TAG, "🔧 System components initialized - Algorithm: ACTIVE");
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ecolumiere_init(void)
{
    ESP_LOGI("ECOLUMIERE", "🚀 Initializing Ecolumiere System");

    // ✅ 1. INIZIALIZZA STRUTTURE ALGORITMO
    initialize_algorithm_structures();

    // ✅ 2. GESTISCI IDENTITÀ E REGISTRO
    handle_device_identity();

    // ✅ 3. GESTISCI CONFIGURAZIONE
    handle_device_configuration();

    // ✅ 4. INIZIALIZZA COMPONENTI SISTEMA
    initialize_system_components();

    ESP_LOGI("ECOLUMIERE", "✅ Ecolumiere System Initialized Successfully");
    slave_node_log_identity();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

void ecolumiere_scheduler_task(void *pvParameters) {
    ESP_LOGI(TAG, "🎯 Ecolumiere Scheduler Task Started");

    // Sostituisci la tua coda con lo scheduler globale
    // (rimuovi la gestione della tua coda privata)

    while (1) {
        // ✅ NON gestire più la tua coda, usa lo scheduler globale
        // Lo scheduler ha il suo task che chiama scheduler_execute()

        // ✅ PUOI ANCORA FARE ALTRE OPERAZIONI QUI
        // Controllo periodico dello stato, log, etc.

        static uint32_t last_status_log = 0;
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        if (now - last_status_log > 30000) { // Ogni 30 secondi
            last_status_log = now;

            // Log stato algoritmo
            // ecolumiere_show_algorithm_status();

            // Log statistiche scheduler
            uint32_t processed, dropped, queued;
            scheduler_get_stats(&processed, &dropped, &queued);
            ESP_LOGI(TAG, "📊 Scheduler Stats: P=%lu, D=%lu, Q=%lu",
                    processed, dropped, queued);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ============================================================================
// FUNZIONI AGUINTIVA PER  PWM
// ============================================================================

/**
 * @brief Calcola livello PWM iniziale intelligente
 */
static float calculate_initial_pwm(void) {
    // ✅ LOGICA PER CALCOLARE PWM INIZIALE BASATO SU CONFIGURAZIONE

    // Calcolo basato su target lux ed efficienza
    float base_level = (float)algo_data.target_lux / (algo_data.power_efficiency * 2.0f);

    // Applica limiti ragionevoli
    if (base_level < 3.0f) base_level = 3.0f;
    if (base_level > 20.0f) base_level = 20.0f;

    ESP_LOGI(TAG, "🎯 PWM iniziale calcolato - Target: %lu lux, PWM: %.1f",
             algo_data.target_lux, base_level);

    return base_level;
}


/**
 * @brief Gestisce comandi diretti dal Gateway via BLE Mesh
 * @param level: Livello PWM da impostare (0-32)
 * @param is_override: true = comando diretto, false = suggerimento algoritmo
 */
void ecolumiere_handle_mesh_command(uint8_t level, bool is_override) {
    ESP_LOGI(TAG, "📡 Ricevuto comando Mesh - Level: %d, Override: %s",
             level, is_override ? "SI" : "NO");

    if (is_override) {
        // ✅ COMANDO DIRETTO: Attiva override
        mesh_override_active = true;
        mesh_override_level = level;
        mesh_override_timeout = (esp_timer_get_time() / 1000) + MESH_OVERRIDE_DURATION_MS;

        if(level != pwmcontroller_get_current_level()) {

            // ✅ CREA NUOVA LAMPADA CON TUTTI I DATI AGGIORNATI
            NodoLampada lampada_aggiornata;
            memcpy(&lampada_aggiornata, slave_node_get_lampada_data(), sizeof(NodoLampada));

            // ✅ AGGIORNA INTENSITÀ E STATO
            lampada_aggiornata.intensita_luminosa = (level * 100) / 32; // PWM -> intensità
            lampada_aggiornata.stato = (level > 0) ? 1 : 0;
            //lampada_aggiornata.timestamp = esp_timer_get_time() / 1000;

            // ✅ SALVA TUTTI I DATI
            slave_node_update_lampada_data(&lampada_aggiornata);

            pwmcontroller_set_level(level);
            ESP_LOGI(TAG, "🎛️ Override Mesh ATTIVO - Level: %d/32, Timeout: %d secondi",
                     level, MESH_OVERRIDE_DURATION_MS / 1000);
        } else {
            ESP_LOGI(TAG, "🎛️ Override Mesh ATTIVO - Level già impostato: %d/32", level);
        }

    } else {
        // ✅ SUGGERIMENTO: Aggiorna solo target per algoritmo
        uint32_t new_target_lux = level * 25; // Approssimazione: 1 PWM = 25 lux

        if (new_target_lux != algo_data.target_lux) {
            algo_data.target_lux = new_target_lux;
            algo_config_data.target_lux = new_target_lux;
            ecolumiere_save_algo_config();

            ESP_LOGI(TAG, "💡 Suggerimento Mesh - Nuovo target: %lu lux (da PWM: %d)",
                     new_target_lux, level);

            // ✅ Ricalcola solo se non in override mesh
            if (!mesh_override_active) {
                ESP_LOGI(TAG, "🔍 Trigger algoritmo con nuovo target...");
                ecolumiere_algo_process();
            } else {
                ESP_LOGI(TAG, "⏸️  Algoritmo sospeso - Override mesh attivo");
            }
        } else {
            ESP_LOGI(TAG, "💡 Suggerimento Mesh - Target lux già impostato: %lu lux", new_target_lux);
        }
    }
}

// ============================================================================

/**
 * @brief Restituisce lo stato corrente dell'override mesh
 */
bool ecolumiere_is_mesh_override_active(void) {
    return mesh_override_active;
}

/**
 * @brief Restituisce il livello di override corrente
 */
uint8_t ecolumiere_get_mesh_override_level(void) {
    return mesh_override_level;
}

/**
 * @brief Restituisce il tempo rimanente per l'override (secondi)
 */
uint32_t ecolumiere_get_mesh_override_remaining(void) {
    if (!mesh_override_active) return 0;

    uint32_t current_time = esp_timer_get_time() / 1000;
    if (current_time >= mesh_override_timeout) return 0;

    return (mesh_override_timeout - current_time) / 1000;
}


// ============================================================================
// FUNZIONI DI TEST
// ============================================================================
/**
 * @brief Forza un test dell'algoritmo con valori specifici (per debug)
 */
void ecolumiere_test_algorithm(uint32_t natural_lux, uint32_t env_lux, uint32_t target_lux) {
    ESP_LOGI(TAG, "🧪 TEST ALGORITMO MANUALE");
    ESP_LOGI(TAG, "   Input - Natural: %lu lux, Env: %lu lux, Target: %lu lux",
             natural_lux, env_lux, target_lux);

    // Salva stato originale
    float original_pwm = algo_data.pnew;
    uint32_t original_target = algo_data.target_lux;

    // Imposta valori di test
    algo_avg.enatural = (float)natural_lux;
    algo_avg.eenv = (float)env_lux;
    algo_data.target_lux = target_lux;

    // Esegui algoritmo
    ecolumiere_algo_process();

    // Ripristina target originale
    algo_data.target_lux = original_target;

    ESP_LOGI(TAG, "🧪 RISULTATO TEST - PWM: %.1f → %.1f/32",
             original_pwm, algo_data.pnew);
}

/**
 * @brief Mostra stato dettagliato dell'algoritmo
 */
void ecolumiere_show_algorithm_status(void) {
    ESP_LOGI(TAG, "=== 🎯 STATO ALGORITMO ECOLIUMERE ===");
    ESP_LOGI(TAG, "Target Lux: %lu", algo_data.target_lux);
    ESP_LOGI(TAG, "Lux Natural: %.1f", algo_avg.enatural);
    ESP_LOGI(TAG, "Lux Environment: %.1f", algo_avg.eenv);
    ESP_LOGI(TAG, "Lux Totale: %.1f", algo_avg.enatural + algo_avg.eenv);
    ESP_LOGI(TAG, "PWM Attuale: %.1f/32", algo_data.pnew);
    ESP_LOGI(TAG, "Campioni: %d/%d", algo_avg.count, algo_avg.size);
    ESP_LOGI(TAG, "Override Mesh: %s", mesh_override_active ? "ATTIVO" : "INATTIVO");

    if (mesh_override_active) {
        ESP_LOGI(TAG, "Livello Override: %d/32", mesh_override_level);
        ESP_LOGI(TAG, "Tempo rimanente: %lu secondi",
                 ecolumiere_get_mesh_override_remaining());
    }

    ESP_LOGI(TAG, "Config Valida: %s", ecolumiere_has_valid_config() ? "SI" : "NO");
    ESP_LOGI(TAG, "======================================");
}