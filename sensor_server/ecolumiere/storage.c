/**
 * Autore: DJITSOP FUOGOUK LOIC STEVE
 * Firmware: ECOLUMIERE BLE MESH ESP32
 * Implementazione: Storage - Gestione memoria persistente NVS
 */

#include "storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "ecolumiere.h"
#include <string.h>
#include "esp_err.h"
#include "config.h"
#include "slave_role.h"
#include "esp_rom_crc.h"

/************************************************
 * DEFINES AND MACRO                            *
 ************************************************/
static const char* TAG = "STORAGE";

#define STORAGE_GC_MASK                         0x01
#define STORAGE_WRITE_CONFIG_MASK               0x02
#define STORAGE_WRITE_REGISTRY_MASK             0x04

#define STORAGE_EVT_INIT 0
#define STORAGE_EVT_WRITE 1
#define STORAGE_EVT_UPDATE 2
#define STORAGE_EVT_GC 3

/************************************************
 * PRIVATE OBJECTS OF THE MODULE                *
 ************************************************/
static uint8_t storage_pending_mask = 0x00;

/**
 * @brief Stati del modulo storage
 * @enum STORAGE_NULL: Modulo non inizializzato
 * @enum STORAGE_BUSY: Operazione in corso
 * @enum STORAGE_READY: Pronto per nuove operazioni
 */
typedef enum storage_status_t {
  STORAGE_NULL,
  STORAGE_BUSY,
  STORAGE_READY
} storage_status_t;

static storage_status_t storage_status = STORAGE_NULL;
static algo_config_data_t config_data;
static ecl_registry_t ecl_registry;
static nvs_handle_t nvs_handle_val;

/************************************************
 * PRIVATE FUNCTIONS IMPLEMENTATION            *
 ************************************************/

/**
 * @brief Gestore eventi del modulo storage
 */
static void storage_fds_evt_handler(uint32_t event_type, uint32_t result) {
  switch (event_type) {
  case STORAGE_EVT_INIT:
    if (result == ESP_OK) {
      storage_status = STORAGE_READY;
      ESP_LOGD(TAG, "Storage initialized successfully");
    }
    break;

  case STORAGE_EVT_WRITE:
  case STORAGE_EVT_UPDATE:
    if (result == ESP_OK) {
      if (storage_pending_mask & STORAGE_WRITE_CONFIG_MASK) {
        storage_pending_mask &= ~STORAGE_WRITE_CONFIG_MASK;
        ESP_LOGD(TAG, "Config write completed");
      }

      if (storage_pending_mask & STORAGE_WRITE_REGISTRY_MASK) {
        storage_pending_mask &= ~STORAGE_WRITE_REGISTRY_MASK;
        ESP_LOGD(TAG, "Registry write completed");
      }
    }
    storage_status = STORAGE_READY;
    break;

  case STORAGE_EVT_GC:
    if (result == ESP_OK) {
      storage_pending_mask &= ~STORAGE_GC_MASK;
      ESP_LOGD(TAG, "Garbage collection completed");
    }
    storage_status = STORAGE_READY;
    break;
  }
}

/**
 * @brief Verifica esistenza di una chiave nello storage NVS
 */
static bool storage_key_exists(const char* key_name) {
  size_t required_size = 0;
  esp_err_t ret = nvs_get_blob(nvs_handle_val, key_name, NULL, &required_size);
  return (ret == ESP_OK && required_size > 0);
}

/**
 * @brief Genera chiave univoca basata solo sul MAC address
 */
static void generate_device_key(const char* prefix, char* key_buffer, size_t buffer_size) {
    const slave_identity_t *identity = slave_node_get_identity();
    const uint8_t *mac = identity->mac_address;

  // ✅ USA SOLO IL MAC ADDRESS CHIAVI DIVERSE: "REG_3C8A1F80AE36" e "CFG_3C8A1F80AE36"
  snprintf(key_buffer, buffer_size, "%s_%02X%02X%02X%02X%02X%02X",
           prefix, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    ESP_LOGD(TAG, "Generated storage key from MAC: %s", key_buffer);
}

/**
 * @brief Scrive la configurazione nella memoria NVS
 */
static void storage_write_config(void) {
  if (nvs_handle_val == 0) {
    ESP_LOGE(TAG, "NVS handle not valid, storage not initialized");
    return;
  }

  esp_err_t err_code;
  char key_name[16];  // "CG_" + 12 caratteri MAC + null

  // ✅ USA PREFIX "CG" PER CONFIG
  generate_device_key("CG", key_name, sizeof(key_name));

  ESP_LOGD(TAG, "Saving config with key: %s, size: %d", key_name, sizeof(algo_config_data_t));

  err_code = nvs_set_blob(nvs_handle_val, key_name, &config_data, sizeof(algo_config_data_t));

  if (err_code == ESP_OK) {
    err_code = nvs_commit(nvs_handle_val);
  }

  if (err_code != ESP_OK) {
    ESP_LOGE(TAG, "Config write failed - MAC Key: %s, Error: %s (%d)",
             key_name, esp_err_to_name(err_code), err_code);
  } else {
    storage_fds_evt_handler(STORAGE_EVT_WRITE, ESP_OK);
    ESP_LOGI(TAG, "✅ Config sovrascritta successfully with MAC key: %s, Size: %d", key_name, sizeof(algo_config_data_t));
  }
}

/**
 * @brief Scrive il registro nella memoria NVS
 */
static void storage_write_registry(void) {
  if (nvs_handle_val == 0) {
    ESP_LOGE(TAG, "NVS handle not valid, storage not initialized");
    return;
  }

  esp_err_t err_code;
  char key_name[16];   // "RG_" + 12 caratteri MAC + null

  // ✅ USA PREFIX "RG" PER REGISTRY
  generate_device_key("RG", key_name, sizeof(key_name));

  ESP_LOGD(TAG, "Saving registry with key: %s, size: %d", key_name, sizeof(ecl_registry_t));

  err_code = nvs_set_blob(nvs_handle_val, key_name, &ecl_registry, sizeof(ecl_registry_t));

  if (err_code == ESP_OK) {
    err_code = nvs_commit(nvs_handle_val);
  }

  if (err_code != ESP_OK) {
    ESP_LOGE(TAG, "Registry write failed - MAC Key: %s, Error: %s (%d)",
             key_name, esp_err_to_name(err_code), err_code);
  } else {
    storage_fds_evt_handler(STORAGE_EVT_WRITE, ESP_OK);
    ESP_LOGI(TAG, "✅ Registry saved successfully with MAC key: %s, Size: %d", key_name, sizeof(ecl_registry_t));
  }
}

/************************************************
 * PUBLIC FUNCTIONS IMPLEMENTATION              *
 ************************************************/

/**
 * @brief Inizializza il sistema di storage
 */
void storage_init(void) {
  esp_err_t err_code;

  // ✅ INIZIALIZZA HANDLE A 0
  nvs_handle_val = 0;
  memset(&config_data, 0, sizeof(algo_config_data_t));

  //err_code = nvs_flash_init();

  /*if (err_code == ESP_ERR_NVS_NO_FREE_PAGES || err_code == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGI(TAG, "NVS flash erased and reinitialized");
    ESP_ERROR_CHECK(nvs_flash_erase());
    err_code = nvs_flash_init();
  }*/

  //ESP_ERROR_CHECK(err_code);

  err_code = nvs_open("ecl_storage", NVS_READWRITE, &nvs_handle_val);
  if (err_code != ESP_OK) {
    ESP_LOGE(TAG, "NVS open failed: %d", err_code);
    nvs_handle_val = 0; // ✅ ASSICURATI CHE SIA 0 IN CASO DI ERRORE
    return;
  }


  storage_fds_evt_handler(STORAGE_EVT_INIT, ESP_OK);
  ESP_LOGI(TAG, "Storage initialized successfully");
}

/**
 * @brief Salva la configurazione algoritmo
 */
bool storage_save_config(void * config) {
  if (storage_pending_mask & STORAGE_WRITE_CONFIG_MASK) {
    ESP_LOGD(TAG, "Config write already pending");
    return false;
  }

  memcpy((void*)&config_data, config, sizeof(algo_config_data_t));
  storage_pending_mask |= STORAGE_WRITE_CONFIG_MASK;

  storage_write_config();
  ESP_LOGD(TAG, "Config save requested");

  return true;
}

/**
 * @brief Carica la configurazione algoritmo
 */
bool storage_load_config(void * config) {
  char key_name[16];
  generate_device_key("CG", key_name, sizeof(key_name));

  ESP_LOGI(TAG, "🔍 Loading config with key: %s", key_name);
  ESP_LOGI(TAG, "📏 Expected config size: %d bytes", sizeof(algo_config_data_t));

  if (storage_key_exists(key_name)) {
    size_t required_size = 0;
    ESP_LOGI(TAG, "✅ Config key FOUND: %s", key_name);

    // ✅ PRIMA LEGGI SOLO LA DIMENSIONE
    nvs_get_blob(nvs_handle_val, key_name, NULL, &required_size);
    ESP_LOGI(TAG, "📊 Stored config size: %d bytes", required_size);

    if (required_size == 4) {
      ESP_LOGW(TAG, "⚠️ Found old config format (4 bytes), deleting...");
      nvs_erase_key(nvs_handle_val, key_name);
      nvs_commit(nvs_handle_val);
      memset(config, 0, sizeof(algo_config_data_t));
      return false;
    }

    // ✅ POI PROVA A CARICARE
    esp_err_t err_code = nvs_get_blob(nvs_handle_val, key_name, config, &required_size);

    ESP_LOGI(TAG, "🔧 Config load result - err: %s, required_size: %d",
             esp_err_to_name(err_code), required_size);

    if (err_code == ESP_OK && required_size == sizeof(algo_config_data_t)) {
      ESP_LOGI(TAG, "✅ Config loaded successfully - Key: %s, Size: %d", key_name, required_size);
      return true;
    } else {
      ESP_LOGE(TAG, "❌ Config load failed - Error: %s, Expected: %d, Got: %d",
               esp_err_to_name(err_code), sizeof(algo_config_data_t), required_size);

      // ✅ CANCELLA CONFIGURAZIONE CORROTTA
      if (err_code == ESP_OK) {
        ESP_LOGW(TAG, "🗑️ Deleting corrupted config with wrong size");
        nvs_erase_key(nvs_handle_val, key_name);
        nvs_commit(nvs_handle_val);
      }
    }
  } else {
    ESP_LOGI(TAG, "📭 No config found with key: %s", key_name);
  }

  memset(config, 0, sizeof(algo_config_data_t));
  return false;
}

/**
 * @brief Salva il registro dispositivo
 */
bool storage_save_registry(void * registry) {
  if (storage_pending_mask & STORAGE_WRITE_REGISTRY_MASK) {
    ESP_LOGD(TAG, "Registry write already pending");
    return false;
  }

  // ✅ COPIA DIRETTAMENTE LA STRUTTURA COMPLETA
  memcpy((void*)&ecl_registry, registry, sizeof(ecl_registry_t));
  storage_pending_mask |= STORAGE_WRITE_REGISTRY_MASK;

  storage_write_registry();
  ESP_LOGD(TAG, "Registry save requested");

  return true;
}

/**
 * @brief Carica il registro dispositivo
 */
void storage_load_registry(void * registry) {
  char key_name[16];
  generate_device_key("RG", key_name, sizeof(key_name));

  ESP_LOGI(TAG, "Loading registry with MAC key: %s", key_name);
  ESP_LOGI(TAG, "Expected registry size: %d bytes", sizeof(ecl_registry_t));

  if (storage_key_exists(key_name)) {
    ESP_LOGI(TAG, "✅ Registry key FOUND: %s", key_name);

    size_t required_size = sizeof(ecl_registry_t);
    esp_err_t err_code = nvs_get_blob(nvs_handle_val, key_name, registry, &required_size);

    ESP_LOGI(TAG, "Registry load - err: %s, expected: %d, got: %d",
             esp_err_to_name(err_code), sizeof(ecl_registry_t), required_size);

    if (err_code == ESP_OK && required_size == sizeof(ecl_registry_t)) {
      ecl_registry_t *reg = (ecl_registry_t *)registry;
      ESP_LOGI(TAG, "Registry loaded successfully");
      ESP_LOGI(TAG, "  Device: 0x%04X, Company: 0x%04X", reg->device_id, reg->company_id);
      ESP_LOGI(TAG, "  Name: %s, Addr: 0x%04X", reg->device_name, reg->unicast_addr);
      return;
    } else {
      ESP_LOGE(TAG, "Registry load failed: %s, expected size: %d, got: %d",
               esp_err_to_name(err_code), sizeof(ecl_registry_t), required_size);
    }
  } else {
    ESP_LOGI(TAG, "No registry found with MAC key: %s - using defaults", key_name);
  }

  // ✅ INIZIALIZZA CON VALORI DEFAULT
  ecl_registry_t *reg = (ecl_registry_t *)registry;
  memset(reg, 0, sizeof(ecl_registry_t));
  reg->device_id = 0x0000;
  reg->company_id = 0x0000;
  reg->unicast_addr = 0x0000;
  reg->config_crc = 0x0000;

  ESP_LOGI(TAG, "Initialized registry with default values");
}

/**
 * @brief Task periodico di gestione storage
 */
void storage_task(void) {
  if (storage_status != STORAGE_READY) return;

  if (storage_pending_mask & STORAGE_GC_MASK) {
    storage_status = STORAGE_BUSY;
    ESP_LOGI(TAG, "Performing garbage collection");
    storage_fds_evt_handler(STORAGE_EVT_GC, ESP_OK);
  } else if (storage_pending_mask & STORAGE_WRITE_REGISTRY_MASK) {
    storage_write_registry();
  } else if (storage_pending_mask & STORAGE_WRITE_CONFIG_MASK) {
    storage_write_config();
  }
}

/**
 * @brief Verifica disponibilità del modulo storage
 */
bool storage_ready(void) {
  if (storage_status != STORAGE_READY) return false;
  return true;
}

/**
 * @brief Verifica se lo storage è pronto per operazioni di scrittura
 */
bool storage_is_ready_for_write(void) {
  return (storage_status == STORAGE_READY && nvs_handle_val != 0);
}

/**
 * @brief Crea una configurazione default e la salva
 */
void storage_create_default_config(void) {
  ESP_LOGI(TAG, "⚙️ Creating default configuration...");

  // ✅ INIZIALIZZA config_data CON VALORI DEFAULT
  memset(&config_data, 0, sizeof(algo_config_data_t));
  config_data.target_lux = 400;  // DEFAULT_TARGET_LUX
  config_data.efficiency = 18.75f;  // POWER_EFFICIENCY
  config_data.distance = 1.0f;  // LAMP_DISTANCE_M
  config_data.in_pl = 1;
  config_data.dimm_step = 0.1f;
  config_data.perc_min = 0.01f;
  config_data.transparency = 1.0f;

  // ✅ USA esp_rom_crc16_le SE DISPONIBILE, ALTRIMENTI FALLBACK
  #ifdef esp_rom_crc16_le
  uint16_t init_crc = 0xFFFF;  // CONFIG_CRC_INIT_VALUE
  config_data.crc = esp_rom_crc16_le(init_crc, (uint8_t *)&config_data, sizeof(algo_config_data_t) - sizeof(uint16_t));
  #else
  // ✅ FALLBACK: CRC SEMPLICE
  config_data.crc = 0x1234;  // CRC placeholder
  ESP_LOGW(TAG, "Using placeholder CRC - esp_rom_crc16_le not available");
  #endif

  // ✅ SALVA
  storage_save_config(&config_data);
  ESP_LOGI(TAG, "✅ Default configuration created and saved");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////// NUOVE FUNZIONI ////////////////////////////////////////////////////

// Aggiungi in FONDO al file esistente:

static void generate_lampada_key(char* key_buffer, size_t buffer_size) {
  const slave_identity_t *identity = slave_node_get_identity();
  const uint8_t *mac = identity->mac_address;
  snprintf(key_buffer, buffer_size, "LP_%02X%02X%02X%02X%02X%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void storage_write_lampada_state(const NodoLampada *lampada) {
  if (nvs_handle_val == 0) return;

  char key_name[16];
  generate_lampada_key(key_name, sizeof(key_name));

  esp_err_t err_code = nvs_set_blob(nvs_handle_val, key_name, lampada, sizeof(NodoLampada));
  if (err_code == ESP_OK) {
    nvs_commit(nvs_handle_val);
    ESP_LOGI(TAG, "✅ NodoLampada salvato - Key: %s", key_name);
  }
}


bool storage_save_lampada_state(const NodoLampada *lampada) {
  if (lampada == NULL || !storage_is_ready_for_write()) return false;
  storage_write_lampada_state(lampada);
  return true;
}


bool storage_load_lampada_state(NodoLampada *lampada) {

  if (lampada == NULL) {
    ESP_LOGE(TAG, "❌ lampada è NULL");
    return false;
  }

  char key_name[16];
  generate_lampada_key(key_name, sizeof(key_name));

  ESP_LOGI(TAG, "🔍 Tentativo caricamento - Key: %s", key_name);

  if (storage_key_exists(key_name)) {
    ESP_LOGI(TAG, "✅ Chiave TROVATA in storage");

    size_t required_size = sizeof(NodoLampada);
    esp_err_t err_code = nvs_get_blob(nvs_handle_val, key_name, lampada, &required_size);

    ESP_LOGI(TAG, "📏 Risultato caricamento - Err: %s, Size: %d, Expected: %d",
             esp_err_to_name(err_code), required_size, sizeof(NodoLampada));

    if (err_code == ESP_OK && required_size == sizeof(NodoLampada)) {
      ESP_LOGI(TAG, "✅ NodoLampada caricato CORRETTAMENTE");
      ESP_LOGI(TAG, "📊 Dati caricati - Stato: %s, Intensità: %u",
               lampada->stato ? "ON" : "OFF", lampada->intensita_luminosa);
      return true;
    } else {
      ESP_LOGE(TAG, "❌ Caricamento FALLITO");
    }
  } else {
    ESP_LOGW(TAG, "❌ Chiave NON TROVATA in storage: %s", key_name);
  }

  return false;
}


bool storage_lampada_state_exists(void) {

  char key_name[16];
  generate_lampada_key(key_name, sizeof(key_name));

  bool exists = storage_key_exists(key_name);
  ESP_LOGI(TAG, "🔍 Storage exists - Key: %s, Esiste: %s",
           key_name, exists ? "SI" : "NO");

  return exists;

}