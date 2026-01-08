//
// Created by Admin on 12/11/2025.
//

/**
 * @file ble_mesh_ecolumiere.c
 * @brief Implementazione BLE Mesh personalizzata per sistema Ecolumiere
 * @details Gestione modelli Sensor e HSL con dati reali dal sistema
 */

// ---------------------------------------------------------------------------------
// SEZIONE INCLUDES - ORDINATI PER DIPENDENZE
// ---------------------------------------------------------------------------------

// 1. LIBRERIE DI SISTEMA ESP32 (Fondamentali)
#include "esp_log.h"        // Sistema di logging (ESP_LOGI, ESP_LOGE, etc.)
#include "nvs_flash.h"      // Gestione memoria non volatile (NVS)
#include "esp_timer.h"      // Timer ad alta risoluzione

// 2. CORE BLE MESH (Dipendenze esterne primarie)
#include "esp_ble_mesh_defs.h"                     // Definizioni base BLE Mesh
#include "esp_ble_mesh_common_api.h"               // API comuni BLE Mesh
#include "esp_ble_mesh_networking_api.h"           // API networking mesh
#include "esp_ble_mesh_provisioning_api.h"         // API provisioning
#include "esp_ble_mesh_config_model_api.h"         // API modello Configuration Server
#include "esp_ble_mesh_sensor_model_api.h"         // API modello Sensor
#include "esp_ble_mesh_local_data_operation_api.h" // API operazioni dati locali

// 3. MODELLI BLE MESH SPECIFICI (Dipendenze esterne secondarie)
#include "esp_ble_mesh_lighting_model_api.h"       // API modelli illuminazione (HSL)

// 4. LIBRERIE/ESEMPI ESTERNI (Dipendenze di terze parti)
#include "ble_mesh_example_init.h"                 // Inizializzazione esempi mesh (ESP-IDF)

// 5. MODULI DI SISTEMA INTERNI (Architettura applicativa)
#include "scheduler.h"                             // Sistema di scheduling eventi

// 6. MODULI APPLICATIVI ECOLLUMIERE (Business logic)
#include "board.h"                                 // Driver hardware/scheda
#include "luxmeter.h"                              // Sensore di luminosità
#include "pwmcontroller.h"                         // Controllo PWM LED
#include "slave_role.h"                            // Gestione ruolo slave
#include "ecolumiere_system.h"                     // Sistema principale Ecolumiere
#include "datarecorder.h"                          // Registrazione dati/log

// 7. HEADER LOCALE (Questo file stesso - sempre ultimo)
#include "ble_mesh_ecolumiere.h"

// Tag per il logging di sistema
static const char *TAG = "BLE_MESH_ECOLUMIERE";

// ---------------------------------------------------------------------------------
// SEZIONE 1: DATI DI SENSORI SIMULATI/STATICI
// ---------------------------------------------------------------------------------
// Questi valori rappresentano dati di sensori fittizi utilizzati per il
// prototipo del sistema. In un'implementazione reale verrebbero sostituiti
// con letture da sensori fisici.
static int8_t indoor_temp = 40;                 /* Temperatura interna: 20°C (risoluzione 0.5) */
static uint16_t potenza_istantanea_assorbita = 2410; /* Potenza assorbita in Watt (BIG ENDIAN) */
static uint16_t humidity_sensor = 10000;        /* Umidità: 100% con risoluzione 0.01% */
static uint16_t pressure_sensor = 10000;        /* Pressione: 1000.00 hPa con risoluzione 0.01 hPa */
static uint8_t error_code = 0;                  /* Codice errore: 0 = nessun errore */
static uint32_t illuminance_sensor = 300;       /* Illuminamento: 300 lux */
static uint16_t voltage_sensor = 2300;          /* Tensione: 23.00 V con risoluzione 0.01 V */
static uint16_t current_sensor = 100;           /* Corrente: 1.00 A con risoluzione 0.01 A */

// ---------------------------------------------------------------------------------
// SEZIONE 2: CONFIGURAZIONE SERVER BLE MESH
// ---------------------------------------------------------------------------------
// Configurazione del server di base (Configuration Server) del nodo mesh.
// Definisce parametri di rete come relay, proxy GATT, beacon, ecc.
esp_ble_mesh_cfg_srv_t config_server = {
    .net_transmit = ESP_BLE_MESH_TRANSMIT(4, 50),      // 4 trasmissioni con intervallo 50ms
    .relay = ESP_BLE_MESH_RELAY_ENABLED,               // Abilita funzionalità di relay
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(4, 50),  // Parametri di ritrasmissione per relay
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,             // Abilita beacon di sicurezza
#if defined(CONFIG_BLE_MESH_GATT_PROXY_SERVER)
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,     // Proxy GATT abilitato se configurato
#else
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
#endif
#if defined(CONFIG_BLE_MESH_FRIEND)
    .friend_state = ESP_BLE_MESH_FRIEND_ENABLED,       // Modalità Friend abilitata se configurata
#else
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
#endif
    .default_ttl = DEFAULT_TTL,                        // Time To Live di default per i messaggi
};

// ---------------------------------------------------------------------------------
// SEZIONE 3: BUFFER PER DATI DEI SENSORI
// ---------------------------------------------------------------------------------
// Buffer di rete (NetBuf) statici per contenere i valori grezzi dei dati dei sensori.
// Ogni buffer corrisponde a un diverso tipo di misurazione sensoriale.
NET_BUF_SIMPLE_DEFINE_STATIC(sensor_data_0, 1);    // Temperatura (1 byte)
NET_BUF_SIMPLE_DEFINE_STATIC(sensor_data_1, 2);    // Potenza istantanea (2 byte, BIG ENDIAN)
NET_BUF_SIMPLE_DEFINE_STATIC(sensor_data_2, 2);    // Umidità (2 byte) 
NET_BUF_SIMPLE_DEFINE_STATIC(sensor_data_3, 2);    // Pressione (2 byte)
NET_BUF_SIMPLE_DEFINE_STATIC(sensor_data_4, 1);    // Codice errore (1 byte)
NET_BUF_SIMPLE_DEFINE_STATIC(sensor_data_5, 2);    // Illuminamento (2 byte)
NET_BUF_SIMPLE_DEFINE_STATIC(sensor_data_6, 2);    // Tensione (2 byte)
NET_BUF_SIMPLE_DEFINE_STATIC(sensor_data_7, 2);    // Corrente (2 byte)

// UUID del dispositivo (16 byte) - utilizzato durante il provisioning
uint8_t dev_uuid[ESP_BLE_MESH_OCTET16_LEN] = { 0x32, 0x10 };

// ---------------------------------------------------------------------------------
// SEZIONE 4: DEFINIZIONI PER DESCRITTORI DEI SENSORI
// ---------------------------------------------------------------------------------
// Costanti che definiscono le caratteristiche di misurazione dei sensori secondo
// lo standard Mesh Model Specification.
#define SENSOR_POSITIVE_TOLERANCE   ESP_BLE_MESH_SENSOR_UNSPECIFIED_POS_TOLERANCE
#define SENSOR_NEGATIVE_TOLERANCE   ESP_BLE_MESH_SENSOR_UNSPECIFIED_NEG_TOLERANCE
#define SENSOR_SAMPLE_FUNCTION      ESP_BLE_MESH_SAMPLE_FUNC_UNSPECIFIED
#define SENSOR_MEASURE_PERIOD       ESP_BLE_MESH_SENSOR_NOT_APPL_MEASURE_PERIOD
#define SENSOR_UPDATE_INTERVAL      ESP_BLE_MESH_SENSOR_NOT_APPL_UPDATE_INTERVAL

// Dichiarazione forward della funzione di conversione lightness -> PWM
static uint8_t convert_lightness_to_pwm(uint16_t lightness);

// ---------------------------------------------------------------------------------
// SEZIONE 5: STATO HSL (Hue, Saturation, Lightness)
// ---------------------------------------------------------------------------------
// Struttura che mantiene lo stato corrente del modello di luce HSL.
static esp_ble_mesh_light_hsl_state_t hsl_state = {
    .lightness = 0xFFFF,          // Livello di luminosità corrente (max)
    .hue = 0,                     // Tonalità corrente (0-360° o 0-0xFFFF)
    .saturation = 0xFFFF,         // Saturazione corrente (max)
    .target_lightness = 0xFFFF,   // Luminosità target per transizioni
    .target_hue = 0,              // Tonalità target per transizioni
    .target_saturation = 0xFFFF,  // Saturazione target per transizioni
    .status_code = ESP_BLE_MESH_MODEL_STATUS_SUCCESS, // Codice di stato dell'ultima operazione
};

// ---------------------------------------------------------------------------------
// SEZIONE 6: STATI DEI SENSORI
// ---------------------------------------------------------------------------------
// Array di strutture che rappresentano gli stati di 8 sensori distinti.
// Ogni elemento contiene il Property ID, i descrittori e i buffer dei dati grezzi.
static esp_ble_mesh_sensor_state_t sensor_states[8] = {
    // Sensore 0: Temperatura
    [0] = {
        .sensor_property_id = SENSOR_PROPERTY_ID_0,
        .descriptor = {
            .positive_tolerance = SENSOR_POSITIVE_TOLERANCE,
            .negative_tolerance = SENSOR_NEGATIVE_TOLERANCE,
            .sampling_function = SENSOR_SAMPLE_FUNCTION,
            .measure_period = SENSOR_MEASURE_PERIOD,
            .update_interval = SENSOR_UPDATE_INTERVAL,
        },
        .sensor_data = {
            .format = ESP_BLE_MESH_SENSOR_DATA_FORMAT_A,
            .length = 0, /* 0 rappresenta lunghezza = 1 byte */
            .raw_value = &sensor_data_0,
        },
    },
    // Sensore 1: Potenza istantanea
    [1] = {
        .sensor_property_id = SENSOR_PROPERTY_ID_1,
        .descriptor = {
            .positive_tolerance = SENSOR_POSITIVE_TOLERANCE,
            .negative_tolerance = SENSOR_NEGATIVE_TOLERANCE,
            .sampling_function = SENSOR_SAMPLE_FUNCTION,
            .measure_period = SENSOR_MEASURE_PERIOD,
            .update_interval = SENSOR_UPDATE_INTERVAL,
        },
        .sensor_data = {
            .format = ESP_BLE_MESH_SENSOR_DATA_FORMAT_A,
            .length = 1, /* 1 rappresenta lunghezza = 2 byte */
            .raw_value = &sensor_data_1,
        },
    },
    // Sensore 2: Umidità
    [2] = {
        .sensor_property_id = SENSOR_PROPERTY_ID_2,
        .descriptor = {
            .positive_tolerance = SENSOR_POSITIVE_TOLERANCE,
            .negative_tolerance = SENSOR_NEGATIVE_TOLERANCE,
            .sampling_function = SENSOR_SAMPLE_FUNCTION,
            .measure_period = SENSOR_MEASURE_PERIOD,
            .update_interval = SENSOR_UPDATE_INTERVAL,
        },
        .sensor_data = {
            .format = ESP_BLE_MESH_SENSOR_DATA_FORMAT_A,
            .length = 1, /* 1 rappresenta lunghezza = 2 byte */
            .raw_value = &sensor_data_2,
        },
    },
    // Sensore 3: Pressione
    [3] = {
        .sensor_property_id = SENSOR_PROPERTY_ID_3,
        .descriptor = {
            .positive_tolerance = SENSOR_POSITIVE_TOLERANCE,
            .negative_tolerance = SENSOR_NEGATIVE_TOLERANCE,
            .sampling_function = SENSOR_SAMPLE_FUNCTION,
            .measure_period = SENSOR_MEASURE_PERIOD,
            .update_interval = SENSOR_UPDATE_INTERVAL,
        },
        .sensor_data = {
            .format = ESP_BLE_MESH_SENSOR_DATA_FORMAT_A,
            .length = 1, /* 1 rappresenta lunghezza = 2 byte */
            .raw_value = &sensor_data_3,
        },
    },
    // Sensore 4: Codice errore
    [4] = {
        .sensor_property_id = SENSOR_PROPERTY_ID_4,
        .descriptor = {
            .positive_tolerance = SENSOR_POSITIVE_TOLERANCE,
            .negative_tolerance = SENSOR_NEGATIVE_TOLERANCE,
            .sampling_function = SENSOR_SAMPLE_FUNCTION,
            .measure_period = SENSOR_MEASURE_PERIOD,
            .update_interval = SENSOR_UPDATE_INTERVAL,
        },
        .sensor_data = {
            .format = ESP_BLE_MESH_SENSOR_DATA_FORMAT_A,
            .length = 1, /* 1 rappresenta lunghezza = 2 byte */
            .raw_value = &sensor_data_4,
        },
    },
    // Sensore 5: Illuminamento
    [5] = {
        .sensor_property_id = SENSOR_PROPERTY_ID_5,
        .descriptor = {
            .positive_tolerance = SENSOR_POSITIVE_TOLERANCE,
            .negative_tolerance = SENSOR_NEGATIVE_TOLERANCE,
            .sampling_function = SENSOR_SAMPLE_FUNCTION,
            .measure_period = SENSOR_MEASURE_PERIOD,
            .update_interval = SENSOR_UPDATE_INTERVAL,
        },
        .sensor_data = {
            .format = ESP_BLE_MESH_SENSOR_DATA_FORMAT_A,
            .length = 1, /* 1 rappresenta lunghezza = 2 byte */
            .raw_value = &sensor_data_5,
        },
    },
    // Sensore 6: Tensione
    [6] = {
        .sensor_property_id = SENSOR_PROPERTY_ID_6,
        .descriptor = {
            .positive_tolerance = SENSOR_POSITIVE_TOLERANCE,
            .negative_tolerance = SENSOR_NEGATIVE_TOLERANCE,
            .sampling_function = SENSOR_SAMPLE_FUNCTION,
            .measure_period = SENSOR_MEASURE_PERIOD,
            .update_interval = SENSOR_UPDATE_INTERVAL,
        },
        .sensor_data = {
            .format = ESP_BLE_MESH_SENSOR_DATA_FORMAT_A,
            .length = 1, /* 1 rappresenta lunghezza = 2 byte */
            .raw_value = &sensor_data_6,
        },
    },
    // Sensore 7: Corrente
    [7] = {
        .sensor_property_id = SENSOR_PROPERTY_ID_7,
        .descriptor = {
            .positive_tolerance = SENSOR_POSITIVE_TOLERANCE,
            .negative_tolerance = SENSOR_NEGATIVE_TOLERANCE,
            .sampling_function = SENSOR_SAMPLE_FUNCTION,
            .measure_period = SENSOR_MEASURE_PERIOD,
            .update_interval = SENSOR_UPDATE_INTERVAL,
        },
        .sensor_data = {
            .format = ESP_BLE_MESH_SENSOR_DATA_FORMAT_A,
            .length = 1, /* 1 rappresenta lunghezza = 2 byte */
            .raw_value = &sensor_data_7,
        },
    },
};

// ---------------------------------------------------------------------------------
// SEZIONE 7: DEFINIZIONE MODELLI SERVER BLE MESH
// ---------------------------------------------------------------------------------
// In BLE Mesh, un "modello" è un'entità software che gestisce specifici tipi di 
// messaggi e stati. Il nodo implementa:
// 1. Modello HSL (controllo luce) - Standard SIG
// 2. Modello Sensor (letture sensori) - Standard SIG  
// 3. Modello Vendor (comandi custom Ecolumiere) - Personalizzato

// NOTA: I modelli standard HSL e Sensor sono divisi in due componenti:
//   - Server principale: gestisce lo stato operativo (es: imposta luminosità)
//   - Setup Server: gestisce la configurazione (binding, pubblicazione, ecc.)

// ---------------------------------------------------------------------------------
// 7.1 MODELLO HSL: COMPONENTE OPERATIVA (Light Hue-Saturation-Lightness Server)
// ---------------------------------------------------------------------------------
// Gestisce le operazioni in tempo reale sul controllo luce:
// - Riceve messaggi SET/GET per hue, saturation, lightness
// - Mantiene lo stato corrente della luce
// - Implementa transizioni graduali

// Definizione del buffer di pubblicazione per il server HSL
// Parametri: nome_buffer, dimensione_max, ruolo_nodo
ESP_BLE_MESH_MODEL_PUB_DEFINE(hsl_pub, 2 + 9, ROLE_NODE);

// Istanza del server HSL con configurazione delle risposte automatiche
static esp_ble_mesh_light_hsl_srv_t hsl_server = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,  // Risponde automaticamente ai GET
        .set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,  // Risponde automaticamente ai SET
    },
    .state = &hsl_state,  // Puntatore allo stato HSL globale (definito sopra)
};

// ---------------------------------------------------------------------------------
// 7.2 MODELLO HSL: COMPONENTE DI CONFIGURAZIONE (HSL Setup Server)
// ---------------------------------------------------------------------------------
// Gestisce esclusivamente la configurazione del modello HSL:
// - Binding a chiavi applicative (AppKey binding) - un'AppKey è una chiave di criptazione
// - Configurazione indirizzi di pubblicazione
// - Sottoscrizioni a gruppi
// NON gestisce lo stato operativo della luce, solo la configurazione del modello.

// Buffer di pubblicazione separato per il setup server
ESP_BLE_MESH_MODEL_PUB_DEFINE(hsl_setup_pub, 2 + 9, ROLE_NODE);

// Istanza del setup server HSL
static esp_ble_mesh_light_hsl_setup_srv_t hsl_setup_server = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,  // Risposte automatiche abilitate
        .set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    },
    .state = &hsl_state,  // Condivide lo stesso stato del server HSL principale
    // Anche se il setup server non modifica lo stato operativo, ha bisogno
    // del riferimento allo stato per alcune operazioni di configurazione.
};

// ---------------------------------------------------------------------------------
// 7.3 MODELLO VENDOR PERSONALIZZATO (Comandi Custom Ecolumiere)
// ---------------------------------------------------------------------------------
// Modello non-standard che gestisce comandi specifici del sistema Ecolumiere.
// Utilizza opcode personalizzati (Company ID + Vendor OP) per:
// - Configurazioni avanzate non coperte dallo standard HSL
// - Comandi specifici dell'applicazione Ecolumiere
// - Gestione parametri custom come color_temp, modalità operative, ecc.

// Definizione degli opcode supportati dal modello vendor
// Ogni entry specifica: (opcode, lunghezza_minima_messaggio)
static esp_ble_mesh_model_op_t vnd_op[] = {
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_SEND, 2),  // Opcode principale custom
    ESP_BLE_MESH_MODEL_OP_END,  // Marcatore di fine array
};

// Definizione del modello vendor con Company ID di Espressif (0x02E5)
static esp_ble_mesh_model_t vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(
        CID_ESP,                           // Company ID: Espressif Systems
        ESP_BLE_MESH_VND_MODEL_ID_SERVER,  // Vendor Model ID (definito in ble_mesh_ecolumiere.h)
        vnd_op,                            // Array opcode supportati
        NULL,                              // Non ha publisher proprio (usa quello dell'elemento)
        NULL                               // Dati utente aggiuntivi (non utilizzati)
    ),
};

// NOTA SULLA RELAZIONE TRA I MODELLI:
// - hsl_server e hsl_setup_server sono DUE PARTI DELLO STESSO MODELLO HSL
// - Insieme formano il "Light HSL Model" completo secondo le specifiche SIG
// - Il modello vendor è COMPLETAMENTE SEPARATO e gestisce logica custom

// 7.4 Server Sensori
ESP_BLE_MESH_MODEL_PUB_DEFINE(sensor_pub, 20, ROLE_NODE);
static esp_ble_mesh_sensor_srv_t sensor_server = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_RSP_BY_APP,  // Risposta gestita dall'applicazione
        .set_auto_rsp = ESP_BLE_MESH_SERVER_RSP_BY_APP,
    },
    .state_count = ARRAY_SIZE(sensor_states),  // Numero di stati sensore gestiti
    .states = sensor_states,                   // Array degli stati dei sensori
};

// 7.5 Setup Server Sensori
ESP_BLE_MESH_MODEL_PUB_DEFINE(sensor_setup_pub, 20, ROLE_NODE);
static esp_ble_mesh_sensor_setup_srv_t sensor_setup_server = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_RSP_BY_APP,
        .set_auto_rsp = ESP_BLE_MESH_SERVER_RSP_BY_APP,
    },
    .state_count = ARRAY_SIZE(sensor_states),
    .states = sensor_states,
};

// ---------------------------------------------------------------------------------
// SEZIONE 8: MODELLI ROOT E COMPOSIZIONE
// ---------------------------------------------------------------------------------
// Definizione dei modelli presenti nell'elemento root del dispositivo.
static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),               // Configuration Server
    ESP_BLE_MESH_MODEL_SENSOR_SRV(&sensor_pub, &sensor_server), // Sensor Server
    ESP_BLE_MESH_MODEL_SENSOR_SETUP_SRV(&sensor_setup_pub, &sensor_setup_server), // Sensor Setup
    ESP_BLE_MESH_MODEL_LIGHT_HSL_SRV(&hsl_pub, &hsl_server),  // HSL Server
    ESP_BLE_MESH_MODEL_LIGHT_HSL_SETUP_SRV(&hsl_setup_pub, &hsl_setup_server), // HSL Setup
};

// Definizione dell'elemento (element) del dispositivo mesh.
// In questo caso un solo elemento che contiene tutti i modelli root e vendor.
static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, vnd_models),  // Elemento 0 con modelli root e vendor
};

// Composizione del dispositivo: definisce CID e elementi.
static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,                     // Company Identifier: Espressif
    .element_count = ARRAY_SIZE(elements), // Numero di elementi
    .elements = elements,               // Array di elementi
};

// ---------------------------------------------------------------------------------
// SEZIONE 9: PROVISIONING
// ---------------------------------------------------------------------------------
// Configurazione del provisioning del dispositivo.
static esp_ble_mesh_prov_t provision = {
    .uuid = dev_uuid,  // UUID utilizzato durante il provisioning
};

// ---------------------------------------------------------------------------------
// SEZIONE 10: FUNZIONI INTERNE
// ---------------------------------------------------------------------------------

/**
 * @brief Inizializza i dati dei sensori con valori reali dal sistema
 * 
 * Questa funzione riempie i buffer dei dati dei sensori con i valori
 * delle variabili statiche definite all'inizio del file.
 */
static void sensor_data_initialize(void)
{
    // Temperatura (1 byte)
    net_buf_simple_add_u8(&sensor_data_0, indoor_temp);
    
    // Potenza istantanea (2 byte, little-endian)
    net_buf_simple_add_le16(&sensor_data_1, potenza_istantanea_assorbita);
    
    // Umidità (2 byte, little-endian)
    net_buf_simple_add_le16(&sensor_data_2, humidity_sensor);
    
    // Pressione (2 byte, little-endian)
    net_buf_simple_add_le16(&sensor_data_3, pressure_sensor);
    
    // Codice errore (1 byte)
    net_buf_simple_add_u8(&sensor_data_4, error_code);
    
    // Illuminamento (4 byte, little-endian)
    net_buf_simple_add_le32(&sensor_data_5, illuminance_sensor);
    
    // Tensione (2 byte, little-endian)
    net_buf_simple_add_le16(&sensor_data_6, voltage_sensor);
    
    // Corrente (2 byte, little-endian)
    net_buf_simple_add_le16(&sensor_data_7, current_sensor);

// ?? CODICE COMMENTATO DA VERIFICARE/COMPLETARE:
// Questo codice tenta di ri-abilitare esplicitamente il relay dopo il provisioning,
// ma è attualmente commentato e potrebbe contenere errori.
// esp_ble_mesh_cfg_srv_t *cfg_srv = NULL;
// esp_ble_mesh_model_t *model = &root_models[0]
// if (model) {
//     cfg_srv = model->user_data;
//     if (cfg_srv) {
//         cfg_srv->relay = ESP_BLE_MESH_RELAY_ENABLED;
//         cfg_srv->relay_retransmit = ESP_BLE_MESH_TRANSMIT(4, 50);
//         ESP_LOGI(TAG, "Relay explicitly enabled after provisioning");
//     }
// }

uint16_t initial_pwm = pwmcontroller_get_current_level();

ESP_LOGI(TAG, "🔧 Tutti i Sensor dati initializzati - Avviato Con PWM: %u", initial_pwm);
}

/**
 * @brief Callback chiamata al completamento del provisioning
 * 
 * @param net_idx Indice della rete mesh
 * @param addr Indirizzo assegnato al dispositivo
 * @param flags Flag di provisioning
 * @param iv_index Indice del vettore di inizializzazione (IV)
 */
static void prov_complete(uint16_t net_idx, uint16_t addr, uint8_t flags, uint32_t iv_index)
{
    ESP_LOGI(TAG, "net_idx 0x%03x, addr 0x%04x", net_idx, addr);
    ESP_LOGI(TAG, "flags 0x%02x, iv_index 0x%08" PRIx32, flags, iv_index);
    
    // Spegne LED verde (indicatore di provisioning in corso)
    board_led_operation(LED_G, LED_OFF);

    // Notifica al modulo slave che il provisioning è completato
    slave_node_on_provisioned(addr);

    // Avvia l'acquisizione del luxmeter (sensore di luminosità)
    luxmeter_start_acquisition();

    // Inizializza i dati dei sensori
    sensor_data_initialize();
}

/**
 * @brief Callback per eventi di provisioning BLE Mesh
 * 
 * Gestisce tutti gli eventi relativi al processo di provisioning.
 */
static void example_ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                                             esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:   // Stack provisioning inizializzato - pronto per operare
        // Log dello stato di registrazione: 0 = successo, altri valori = errore specifico
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROV_REGISTER_COMP_EVT, err_code %d", 
                 param->prov_register_comp.err_code);
        break;

    case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:    // Nodo ora visibile e in attesa di provisioning
        // Log conferma: dispositivo può essere scoperto dai provisioner (app/gateway)
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT, err_code %d", 
                 param->node_prov_enable_comp.err_code);
        break;
        
    case ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT:  // Un provisioner ha iniziato sessione con questo nodo
        // Log tipo di connessione: PB-ADV (messaggi advertising) o PB-GATT (connessione BLE classica)
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT, bearer %s",
            param->node_prov_link_open.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
        break;
        
    case ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT: // Sessione provisioning terminata (successo o fallimento)
        // Log chiusura connessione - il bearer indica il canale usato durante la sessione
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT, bearer %s",
            param->node_prov_link_close.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
        break;
        
    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:   // MOMENTO CRITICO: provisioning completato con successo
        // Log evento e chiamata a funzione che gestisce post-provisioning
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT");
        // Chiama prov_complete() che: 1) spegne LED verde, 2) notifica slave node,
        // 3) avvia luxmeter, 4) inizializza dati sensori
        prov_complete(param->node_prov_complete.net_idx, param->node_prov_complete.addr,
            param->node_prov_complete.flags, param->node_prov_complete.iv_index);
        break;
        
    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:      // Reset a fabbrica - perdita di tutte le configurazioni
        // Log reset: il dispositivo torna allo stato "unprovisioned" e può essere riprovisionato
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_RESET_EVT");
        break;
        
    case ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT:    // Nome personalizzato configurato per fase discovery
        // Log risultato impostazione nome (es: "Ecolumiere-Light-01")
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT, err_code %d", 
                 param->node_set_unprov_dev_name_comp.err_code);
        break;
        
    default:
        break;
    }
}

/**
 * @brief Callback per eventi del Configuration Server
 * 
 * Gestisce cambiamenti di configurazione inviati dal provisioner (es: telefono/gateway).
 * Il Configuration Server è un modello speciale che gestisce la configurazione di rete.
 */
static void example_ble_mesh_config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                                              esp_ble_mesh_cfg_server_cb_param_t *param)
{
    // Solo gli eventi di cambio stato sono gestiti (altri eventi ignorati)
    if (event == ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT) {
        switch (param->ctx.recv_op) {
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD: // Provisioner ha aggiunto una AppKey al nodo
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD"); // Log dell'operazione e dettagli della chiave applicativa
            ESP_LOGI(TAG, "net_idx 0x%04x, app_idx 0x%04x", // net_idx: indice rete, app_idx: indice AppKey
                param->value.state_change.appkey_add.net_idx,
                param->value.state_change.appkey_add.app_idx);
            ESP_LOG_BUFFER_HEX("AppKey", param->value.state_change.appkey_add.app_key, 16); // Stampa i 16 byte della AppKey (chiave di crittografia per messaggi applicativi)
            break;
            
        case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND: // AppKey associata a un modello specifico
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND");
            
            // elem_addr: indirizzo elemento, app_idx: quale AppKey
            // cid: Company ID, mod_id: ID modello
            ESP_LOGI(TAG, "elem_addr 0x%04x, app_idx 0x%04x, cid 0x%04x, mod_id 0x%04x",
                param->value.state_change.mod_app_bind.element_addr,
                param->value.state_change.mod_app_bind.app_idx,
                param->value.state_change.mod_app_bind.company_id,
                param->value.state_change.mod_app_bind.model_id);
            break;
            
        case ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD: // Aggiunge modello alla lista di ascolto di un gruppo
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD");
            
            // elem_addr: elemento locale, sub_addr: indirizzo gruppo
            // cid/mod_id: identificano il modello sottoscritto
            ESP_LOGI(TAG, "elem_addr 0x%04x, sub_addr 0x%04x, cid 0x%04x, mod_id 0x%04x",
                param->value.state_change.mod_sub_add.element_addr,
                param->value.state_change.mod_sub_add.sub_addr,
                param->value.state_change.mod_sub_add.company_id,
                param->value.state_change.mod_sub_add.model_id);
            break;
            
        default:
            break;
        }
    }
}

/**
 * @brief Callback per il modello vendor personalizzato
 * 
 * Gestisce i comandi custom inviati attraverso il modello vendor.
 * In particolare, gestisce il comando di configurazione luminosità.
 */
static void example_ble_mesh_custom_model_cb(esp_ble_mesh_model_cb_event_t event,
                                             esp_ble_mesh_model_cb_param_t *param)
{
    switch (event) {
    // Ricevuto messaggio destinato a un modello

    // Verifica se è un messaggio per il nostro modello vendor personalizzato
    if (param->model_operation.opcode == ESP_BLE_MESH_VND_MODEL_OP_SEND) {
        // Gestisce il comando custom del modello vendor (configdata_t)
            // Verifica che la lunghezza del messaggio corrisponda alla struttura configdata_t
            if (param->model_operation.length == sizeof(configdata_t)) {
                configdata_t *cfg = (configdata_t *)param->model_operation.msg;

                // Estrae la luminosità (0-100%)
                uint8_t brightness_percent = cfg->brightness;

                // Gestione speciale per brightness=1 (la piattaforma non accetta il valore 0)
                if(cfg->brightness == 1){
                    brightness_percent = 0;
                }

                // Converte percentuale (0-100%) in livello PWM (0-32)
                uint8_t pwm_level = (brightness_percent * LIGHT_MAX_LEVEL) / 100;

                // Limita il valore massimo a LIGHT_MAX_LEVEL (32)
                if (pwm_level > LIGHT_MAX_LEVEL) {
                    pwm_level = LIGHT_MAX_LEVEL;
                }

                ESP_LOGI(TAG, "📱 Ricevuto comando BLE Mesh Luminosita: %d%% → PWM=%u/32",
                         brightness_percent, pwm_level);

                // Crea evento per lo scheduler
                ble_mesh_event_t mesh_event = {
                    .brightness = brightness_percent,  // 0-100%
                    .pwm_level = pwm_level,            // 0-32
                    .hue = 0,
                    .saturation = 0,
                    .is_override = true,
                    .timestamp = esp_timer_get_time()
                };

                // Invia evento allo scheduler per l'elaborazione asincrona
                esp_err_t sched_err = scheduler_put_event(&mesh_event, sizeof(mesh_event),
                                                         SCH_EVT_BLE_MESH_RX, handle_ble_mesh_event);

                if (sched_err == ESP_OK) {
                    ESP_LOGI(TAG, "📨 Evento messo in coda scheduler");
                } else {
                    ESP_LOGE(TAG, "❌ Errore mettendo evento in coda");
                }

                // Controllo immediato del LED (feedback visivo)
                if (pwm_level > 0) {
                    board_led_operation(LED_R, LED_ON);
                } else {
                    board_led_operation(LED_R, LED_OFF);
                }

                // Log altri parametri ricevuti (per debug)
                ESP_LOGI(TAG, "color_temp=%d, rgb={%d,%d,%d}, dimStep=%d",
                         cfg->color_temp, cfg->rgb[0], cfg->rgb[1], cfg->rgb[2], cfg->dimStep);

            } else {
                ESP_LOGW(TAG, "Lunghezza errata: %d, atteso: %d",
                         param->model_operation.length, sizeof(configdata_t));
            }

            // Invia risposta di conferma (status) al mittente
            uint16_t tid = 0x01;  // Transaction ID
            esp_err_t err = esp_ble_mesh_server_model_send_msg(
                &vnd_models[0], param->model_operation.ctx,
                ESP_BLE_MESH_VND_MODEL_OP_STATUS, sizeof(tid), (uint8_t *)&tid);

            if (err != ESP_OK) {
                ESP_LOGE(TAG, "❌ Failed to send BLE Mesh response");
            }
        }
        break;

    case ESP_BLE_MESH_MODEL_SEND_COMP_EVT:  // Invio messaggio completato (conferma trasmissione)
    // Controlla se l'invio ha avuto errori (err_code != 0 significa fallimento)
        if (param->model_send_comp.err_code) {
            ESP_LOGE(TAG, "Failed to send message 0x%06" PRIx32, param->model_send_comp.opcode);
            break;
        }
        ESP_LOGI(TAG, "Send 0x%06" PRIx32, param->model_send_comp.opcode);
        break;
        
    default:
        break;
    }
}

/**
 * @brief Estrae i dati grezzi da uno stato sensore
 * 
 * @param state Puntatore allo stato del sensore
 * @param data Buffer dove memorizzare i dati estratti
 * @return uint16_t Lunghezza totale dei dati estratti (MPID + raw data)
 * 
 * Formatta i dati secondo le specifiche Mesh Model:
 * - MPID (Measurement Property ID) seguito dai dati grezzi
 * - Supporta sia Format A che Format B
 */
static uint16_t example_ble_mesh_get_sensor_data(esp_ble_mesh_sensor_state_t *state, uint8_t *data)
{
    uint8_t mpid_len = 0, data_len = 0;
    uint32_t mpid = 0;

    if (state == NULL || data == NULL) {
        ESP_LOGE(TAG, "%s, Invalid parameter", __func__);
        return 0;
    }

    // Gestisce dati di lunghezza zero (Format B speciale)
    if (state->sensor_data.length == ESP_BLE_MESH_SENSOR_DATA_ZERO_LEN) {
        /* Per dati sensore a lunghezza zero */
        mpid = ESP_BLE_MESH_SENSOR_DATA_FORMAT_B_MPID(state->sensor_data.length, state->sensor_property_id);
        mpid_len = ESP_BLE_MESH_SENSOR_DATA_FORMAT_B_MPID_LEN;
        data_len = 0;
    } else {
        // Dati normali: sceglie formato
        if (state->sensor_data.format == ESP_BLE_MESH_SENSOR_DATA_FORMAT_A) {
            mpid = ESP_BLE_MESH_SENSOR_DATA_FORMAT_A_MPID(state->sensor_data.length, state->sensor_property_id);
            mpid_len = ESP_BLE_MESH_SENSOR_DATA_FORMAT_A_MPID_LEN;
        } else {
            mpid = ESP_BLE_MESH_SENSOR_DATA_FORMAT_B_MPID(state->sensor_data.length, state->sensor_property_id);
            mpid_len = ESP_BLE_MESH_SENSOR_DATA_FORMAT_B_MPID_LEN;
        }
        /* Utilizza "state->sensor_data.length + 1" perché la lunghezza dei dati è zero-based */
        data_len = state->sensor_data.length + 1;
    }

    // Copia MPID e dati grezzi nel buffer di output
    memcpy(data, &mpid, mpid_len);
    memcpy(data + mpid_len, state->sensor_data.raw_value->data, data_len);

    return (mpid_len + data_len);
}

/**
 * @brief Invia messaggio di status con i dati di tutti i sensori
 * 
 * @param param Parametri della callback contenenti il contesto della richiesta
 * 
 * Costruisce un messaggio Sensor Status in risposta a una richiesta GET.
 * Può includere tutti i sensori o solo quello specificato nella richiesta.
 */
static void example_ble_mesh_send_sensor_status(esp_ble_mesh_sensor_server_cb_param_t *param)
{
    uint8_t *status = NULL;
    uint16_t buf_size = 0;
    uint16_t length = 0;
    uint32_t mpid = 0;
    esp_err_t err;
    int i;

    // Calcola dimensione totale necessaria per tutti i dati dei sensori
    for (i = 0; i < ARRAY_SIZE(sensor_states); i++) {
        esp_ble_mesh_sensor_state_t *state = &sensor_states[i];
        if (state->sensor_data.length == ESP_BLE_MESH_SENSOR_DATA_ZERO_LEN) {
            buf_size += ESP_BLE_MESH_SENSOR_DATA_FORMAT_B_MPID_LEN;
        } else {
            /* Utilizza "state->sensor_data.length + 1" perché la lunghezza è zero-based */
            if (state->sensor_data.format == ESP_BLE_MESH_SENSOR_DATA_FORMAT_A) {
                buf_size += ESP_BLE_MESH_SENSOR_DATA_FORMAT_A_MPID_LEN + state->sensor_data.length + 1;
            } else {
                buf_size += ESP_BLE_MESH_SENSOR_DATA_FORMAT_B_MPID_LEN + state->sensor_data.length + 1;
            }
        }
    }

    // Alloca buffer per lo status
    status = (uint8_t *)calloc(1, buf_size);
    if (!status) {
        ESP_LOGE(TAG, "No memory for sensor status!");
        return;
    }

    // Se la richiesta GET non specifica un Property ID, invia tutti i sensori
    if (param->value.get.sensor_data.op_en == false) {
        /* Specifica Mesh Model:
         * Se il messaggio è una risposta a Sensor Get e il campo Property ID
         * è omesso, il campo Marshalled Sensor Data deve contenere dati per
         * tutte le proprietà del dispositivo all'interno di un sensore.
         */
        for (i = 0; i < ARRAY_SIZE(sensor_states); i++) {
            length += example_ble_mesh_get_sensor_data(&sensor_states[i], status + length);
        }
        goto send;
    }

    // Altrimenti il campo Marshalled Sensor data conterrà solo i dati per le proprietà del dispositivo richiesto
    for (i = 0; i < ARRAY_SIZE(sensor_states); i++) {
        if (param->value.get.sensor_data.property_id == sensor_states[i].sensor_property_id) {
            length = example_ble_mesh_get_sensor_data(&sensor_states[i], status);
            goto send;
        }
    }

    /* Specifica Mesh Model:
     * Se la proprietà richiesta non è riconosciuta dal Sensor Server,
     * la Lunghezza deve essere zero e il campo Raw Value deve contenere
     * solo il Property ID.
     */
    mpid = ESP_BLE_MESH_SENSOR_DATA_FORMAT_B_MPID(ESP_BLE_MESH_SENSOR_DATA_ZERO_LEN,
            param->value.get.sensor_data.property_id);
    memcpy(status, &mpid, ESP_BLE_MESH_SENSOR_DATA_FORMAT_B_MPID_LEN);
    length = ESP_BLE_MESH_SENSOR_DATA_FORMAT_B_MPID_LEN;

send:
    // Log dei dati inviati (per debug)
    ESP_LOG_BUFFER_HEX("Sensor Data", status, length);

    // Invia il messaggio di status
    err = esp_ble_mesh_server_model_send_msg(param->model, &param->ctx,
            ESP_BLE_MESH_MODEL_OP_SENSOR_STATUS, length, status);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send Sensor Status");
    }
    free(status);
}

/**
 * @brief Callback per eventi del Sensor Server
 * 
 * Gestisce le richieste GET inviate al modello Sensor.
 */
static void example_ble_mesh_sensor_server_cb(esp_ble_mesh_sensor_server_cb_event_t event,
                                              esp_ble_mesh_sensor_server_cb_param_t *param)
{
    ESP_LOGD(TAG, "Sensor server, event %d, src 0x%04x, dst 0x%04x, model_id 0x%04x",
        event, param->ctx.addr, param->ctx.recv_dst, param->model->model_id);

    switch (event) {
    case ESP_BLE_MESH_SENSOR_SERVER_RECV_GET_MSG_EVT: // Ricevuto messaggio GET
        switch (param->ctx.recv_op) {
        case ESP_BLE_MESH_MODEL_OP_SENSOR_GET: // Richiesta dati sensore
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_SENSOR_GET");
            example_ble_mesh_send_sensor_status(param); // Invia risposta con i dati del sensore
            break;
        default: // Opcode GET non riconosciuto
            ESP_LOGE(TAG, "Unknown Sensor Get opcode 0x%04" PRIx32, param->ctx.recv_op);
            return;
        }
        break;
    default: // Evento non riconosciuto
        ESP_LOGE(TAG, "Unknown Sensor Server event %d", event);
        break;
    }
}

/**
 * @brief Converte un valore lightness (0-65535 o 0-100) in livello PWM (0-32)
 * 
 * @param lightness Valore di lightness da convertire
 * @return uint8_t Livello PWM corrispondente (0-32)
 */
static uint8_t convert_lightness_to_pwm(uint16_t lightness)
{
    uint32_t pwm_level;

    // Se lightness <= 100, assume scala percentuale (0-100%)
    if (lightness <= LIGHTNESS_MAX) {
        // Converte percentuale in livello PWM (scala 0-32)
        pwm_level = (lightness * LIGHT_MAX_LEVEL) / LIGHTNESS_MAX;
        ESP_LOGI(TAG, "🔧 SCALA 0-100: %u → %lu/%d", lightness, pwm_level, LIGHT_MAX_LEVEL);
    } else {
        // Se lightness > 100, imposta al valore massimo
        pwm_level = LIGHT_MAX_LEVEL;
        ESP_LOGW(TAG, "⚠️ Lightness fuori range: %u, impostato a MAX (%d)",
                 lightness, LIGHT_MAX_LEVEL);
    }

    // Controllo di sicurezza (dovrebbe essere ridondante ma meglio prevenire)
    if (pwm_level > LIGHT_MAX_LEVEL) {
        pwm_level = LIGHT_MAX_LEVEL;
        ESP_LOGE(TAG, "❌ ERRORE: pwm_level > MAX, corretto a %d", LIGHT_MAX_LEVEL);
    }

    return (uint8_t)pwm_level;
}

/**
 * @brief Callback per eventi del Lighting Server (HSL)
 * 
 * Gestisce tutte le operazioni sul modello HSL:
 * - SET/GET di hue, saturation, lightness
 * - Transizioni di stato
 */
static void example_ble_mesh_light_server_cb(esp_ble_mesh_lighting_server_cb_event_t event,
                                             esp_ble_mesh_lighting_server_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_LIGHTING_SERVER_STATE_CHANGE_EVT: // Stato HSL cambiato (dopo transizione)
        if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_SET) {
            uint16_t hue = hsl_state.hue;
            uint16_t sat = hsl_state.saturation;
            uint16_t lightness = hsl_state.lightness;  // ✅ già 0-100

            ESP_LOGI(TAG, "🎨 HSL Received: H:%u S:%u L:%u", hue, sat, lightness);

            // Converte lightness in livello PWM
            uint8_t pwm_level = convert_lightness_to_pwm(lightness);

            ESP_LOGI(TAG, "🎛️ BLE HSL → PWM: %u → %u/32", lightness, pwm_level);

            // Crea evento per lo scheduler
            ble_mesh_event_t mesh_event = {
                .brightness = lightness,
                .pwm_level = pwm_level,
                .hue = hue,
                .saturation = sat,
                .is_override = true,
                .timestamp = esp_timer_get_time()
            };

            // Invia allo scheduler per elaborazione asincrona
            esp_err_t sched_err = scheduler_put_event(&mesh_event, sizeof(mesh_event),
                                                     SCH_EVT_BLE_MESH_RX, handle_ble_mesh_event);

            if (sched_err == ESP_OK) { // Controllo del successo dell'invio
                ESP_LOGI(TAG, "📨 HSL Event queued to scheduler");
            } else {
                ESP_LOGE(TAG, "❌ Failed to queue HSL event");
            }

            // Feedback LED immediato
            if (pwm_level > 0) {
                board_led_operation(LED_R, LED_ON);
            } else {
                board_led_operation(LED_R, LED_OFF);
            }
        }
        break;

    case ESP_BLE_MESH_LIGHTING_SERVER_RECV_SET_MSG_EVT: // Ricevuto messaggio SET (con o senza ack)
        if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_SET ||
            param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_SET_UNACK) {

            uint16_t hue = param->value.set.hsl.hue;
            uint16_t sat = param->value.set.hsl.saturation;
            uint16_t lightness = param->value.set.hsl.lightness;

            // Aggiorna stato HSL corrente
            hsl_state.hue = hue;
            hsl_state.saturation = sat;
            hsl_state.lightness = lightness;

            // Aggiorna stati target (per transizioni graduali)
            hsl_state.target_hue = hue;
            hsl_state.target_saturation = sat;
            hsl_state.target_lightness = lightness;

            ESP_LOGI(TAG, "HSL Set: H:%u S:%u L:%u", hue, sat, lightness);

            // Converte in PWM e aggiorna hardware
            uint8_t pwm_level = convert_lightness_to_pwm(lightness);
            pwmcontroller_set_level(pwm_level);

            ESP_LOGI(TAG, "🎛️ BLE Set → PWM: %u → %u/32", lightness, pwm_level);

            // ?? Aggiorna lampadina fisica
            // board_set_led_hsl(hue, sat, lightness);

            // Sincronizza lo stato del nodo lampada con i nuovi valori HSL
            sync_nodo_lampada_with_hsl(hue, sat, lightness);

            // Controllo LED fisico
            if (pwm_level > 0) {
                ESP_LOGI(TAG, "💡 Comando BLE: ON - Accendo LED");
                board_led_operation(LED_R, LED_ON);
            } else {
                ESP_LOGI(TAG, "💡 Comando BLE: OFF - Spengo LED");
                board_led_operation(LED_R, LED_OFF);
            }
        }
        break;

    case ESP_BLE_MESH_LIGHTING_SERVER_RECV_GET_MSG_EVT: // Ricevuta richiesta GET per stato HSL
        if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_GET) {

            // Legge livello PWM corrente dall'hardware
            uint16_t current_pwm_level = pwmcontroller_get_current_level();

            // Converte PWM in lightness percentuale (0-100%)
            uint16_t current_lightness = (current_pwm_level * 100) / LIGHT_MAX_LEVEL;

            // Struttura dati per lo status response
            struct __attribute__((packed)) {
                uint16_t lightness;
                uint16_t hue;
                uint16_t saturation;
            } status = {
                current_lightness,
                hsl_state.hue,
                hsl_state.saturation
            };

            // Invia risposta di status
            esp_ble_mesh_server_model_send_msg(
                param->model,
                &param->ctx,
                ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_STATUS,
                sizeof(status),
                (uint8_t *)&status
            );

            ESP_LOGI(TAG, "📤 BLE Status Sent: %u/100 (from PWM: %u/32)",
                current_lightness, current_pwm_level);
        }
        break;

    default:
        break;
    }
}

// ---------------------------------------------------------------------------------
// SEZIONE 11: FUNZIONI PUBBLICHE
// ---------------------------------------------------------------------------------

/**
 * @brief Inizializzazione del modulo BLE Mesh Ecolumiere
 * 
 * @return esp_err_t 
 *   - ESP_OK: Inizializzazione completata con successo
 *   - ESP_FAIL: Scheduler non inizializzato
 *   - Altro: Errore nello stack BLE Mesh
 * 
 * Configura e avvia lo stack BLE Mesh con tutti i modelli necessari
 * per il sistema Ecolumiere.
 */
esp_err_t ble_mesh_ecolumiere_init(void)
{
    esp_err_t err;

    // Verifica che lo scheduler sia stato inizializzato
    if (!scheduler_is_initialized()) {
        ESP_LOGE(TAG, "❌ Scheduler not initialized! Call scheduler_init() first");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "📡 Initializing BLE Mesh with global scheduler");

    // Registra tutte le callback necessarie
    esp_ble_mesh_register_prov_callback(example_ble_mesh_provisioning_cb);
    esp_ble_mesh_register_config_server_callback(example_ble_mesh_config_server_cb);
    esp_ble_mesh_register_sensor_server_callback(example_ble_mesh_sensor_server_cb);
    
    // Callback per modelli HSL e personalizzati
    esp_ble_mesh_register_lighting_server_callback(example_ble_mesh_light_server_cb);
    esp_ble_mesh_register_custom_model_callback(example_ble_mesh_custom_model_cb);

    // Inizializza lo stack BLE Mesh
    err = esp_ble_mesh_init(&provision, &composition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to initialize mesh stack");
        return err;
    }

    // Abilita provisioning su entrambi i bearer (ADV e GATT)
    err = esp_ble_mesh_node_prov_enable((esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to enable mesh node");
        return err;
    }

    // Accende LED verde per indicare che il dispositivo è in attesa di provisioning
    board_led_operation(LED_G, LED_ON);
    ESP_LOGI(TAG, "✅ BLE Mesh Ecolumiere initialized with global scheduler");

    return ESP_OK;
}

/**
 * @brief Restituisce l'UUID del dispositivo
 * 
 * @param uuid Buffer dove copiare l'UUID (deve essere almeno ESP_BLE_MESH_OCTET16_LEN byte)
 */
void ble_mesh_ecolumiere_get_dev_uuid(uint8_t *uuid)
{
    if (uuid) {
        memcpy(uuid, dev_uuid, ESP_BLE_MESH_OCTET16_LEN);
    }
}

// ---------------------------------------------------------------------------------
// SEZIONE 12: SINCRONIZZAZIONE CON NODO LAMPADA
// ---------------------------------------------------------------------------------

/**
 * @brief Sincronizza la struttura NodoLampada con lo stato HSL corrente
 * 
 * @param hue Tonalità HSL (0-0xFFFF o 0-360°)
 * @param saturation Saturazione HSL (0-0xFFFF)
 * @param lightness Luminosità HSL (0-100%)
 * 
 * Aggiorna la struttura dati del nodo lampada per riflettere i comandi
 * ricevuti via BLE Mesh, registrando anche gli eventi di accensione/spegnimento.
 */
void sync_nodo_lampada_with_hsl(uint16_t hue, uint16_t saturation, uint16_t lightness) {

    NodoLampada lampada_aggiornata;

    // Ottiene lo stato corrente della lampada
    const NodoLampada *stato_corrente = slave_node_get_lampada_data();

    // Copia stato corrente o inizializza struttura vuota
    if (stato_corrente != NULL) {
        memcpy(&lampada_aggiornata, stato_corrente, sizeof(NodoLampada));
    } else {
        memset(&lampada_aggiornata, 0, sizeof(NodoLampada));
    }

    // Calcola intensità luminosa in scala 0-100%
    uint16_t nuova_intensita = lightness;

    // Verifica se ci sono cambiamenti significativi
    bool intensita_cambiata = (lampada_aggiornata.intensita_luminosa != nuova_intensita);
    bool stato_cambiato = (lampada_aggiornata.stato != (lightness > 0));

    // Se nessun cambiamento, esce senza aggiornamenti
    if (!intensita_cambiata && !stato_cambiato) {
        ESP_LOGD(TAG, "🔁 NodoLampada già sincronizzato - Intensità: %u/100", nuova_intensita);
        return;
    }

    // Aggiorna i dati della lampada
    lampada_aggiornata.stato = (lightness > 0);
    lampada_aggiornata.intensita_luminosa = nuova_intensita;
    lampada_aggiornata.temperatura_colore = 50; // Valore di default
    lampada_aggiornata.controllo_remoto = true; // Indica che il comando è remoto (BLE)

    // Gestione timestamp (tempo in secondi)
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000000);

    // Se la lampada viene accesa e non aveva timestamp di accensione
    if (lightness > 0 && lampada_aggiornata.tempo_accensione == 0) {
        lampada_aggiornata.tempo_accensione = now;
        ESP_LOGI(TAG, "⏰ Nuova accensione registrata");
    } 
    // Se la lampada viene spenta e aveva un timestamp di accensione
    else if (lightness == 0 && lampada_aggiornata.tempo_accensione > 0) {
        lampada_aggiornata.tempo_spegnimento = now;
        ESP_LOGI(TAG, "⏰ Spegnimento registrato");
    }

    // Salva lo stato aggiornato
    slave_node_update_lampada_data(&lampada_aggiornata);

    // Registra l'evento nel data recorder
    char event_desc[60];
    snprintf(event_desc, sizeof(event_desc), "HSL H:%u S:%u L:%u → Int:%u/100",
             hue, saturation, lightness, nuova_intensita);
    data_recorder_enqueue_lampada_event(EVENT_COMMAND_RECEIVED, event_desc);

    ESP_LOGI(TAG, "🔄 NodoLampada sincronizzato - HSL: %u → Intensità: %u/100, Stato: %s",
             lightness, nuova_intensita, lightness > 0 ? "ON" : "OFF");
}