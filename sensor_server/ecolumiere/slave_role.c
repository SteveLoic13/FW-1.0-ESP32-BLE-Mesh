/**
 * Autore: DJITSOP FUOGOUK LOIC STEVE
 * Firmware: ECOLUMIERE BLE MESH ESP32
 * Implementazione: Slave Role - Gestione identità dispositivo in rete mesh
 */

#include "slave_role.h"
#include "esp_log.h"
#include "string.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "pwmcontroller.h"
#include "storage.h"
#include "board.h"

/************************************************
 * PRIVATE OBJECTS OF THE MODULE                *
 ************************************************/
static const char *TAG = "SLAVE_ROLE";

// Struttura identità del dispositivo slave
static slave_identity_t slave_identity;

static slave_node_t slave_node = {0};

/************************************************
 * PRIVATE FUNCTIONS IMPLEMENTATION            *
 ************************************************/

/**
 * @brief Ottiene l'indirizzo MAC Bluetooth del dispositivo
 * @param mac_addr: buffer dove salvare il MAC address (deve essere di almeno 6 byte)
 * @return true se successo, false se errore
 */
static bool get_bt_mac_address(uint8_t *mac_addr) {
    if (!mac_addr) {
        ESP_LOGE(TAG, "Invalid buffer for MAC address");
        return false;
    }

    const uint8_t *mac = esp_bt_dev_get_address();
    if (!mac) {
        ESP_LOGE(TAG, "Failed to get Bluetooth MAC address");
        return false;
    }

    memcpy(mac_addr, mac, 6);
    return true;
}

/**
 * @brief Genera un ID dispositivo univoco basato sul MAC address
 * @param mac_addr: array di 6 byte con il MAC address
 * @param device_id: puntatore dove salvare il device_id generato
 * @param company_id: puntatore dove salvare il company_id generato
 */
static void generate_id_from_mac(const uint8_t *mac_addr, uint16_t *device_id, uint16_t *company_id) {
    if (!mac_addr || !device_id || !company_id) {
        ESP_LOGE(TAG, "Invalid parameters for ID generation");
        return;
    }

    // Genera device_id dai primi 4 byte del MAC
    *device_id = (mac_addr[0] << 8) | mac_addr[1];
    *device_id ^= (mac_addr[2] << 8) | mac_addr[3];

    // Genera company_id dagli ultimi 2 byte + fixed company prefix
    // Usiamo un company ID fisso per tutti i dispositivi Ecolumiere (0xEC01)
    // e combiniamo con gli ultimi 2 byte del MAC per variabilità
    *company_id = 0xEC01;
    *company_id ^= (mac_addr[4] << 8) | mac_addr[5];
}

/**
 * @brief Genera il nome dispositivo basato sul MAC address
 * @param mac_addr: array di 6 byte con il MAC address
 * @param name_buffer: buffer dove salvare il nome
 * @param buffer_size: dimensione del buffer
 */
static void generate_name_from_mac(const uint8_t *mac_addr, char *name_buffer, size_t buffer_size) {
    if (!mac_addr || !name_buffer) {
        ESP_LOGE(TAG, "Invalid parameters for name generation");
        return;
    }

    // Formato: ECL_3C8A1F80AE36 (MAC address senza separatori)
    snprintf(name_buffer, buffer_size, "ECL_%02X%02X%02X%02X%02X%02X",
             mac_addr[0], mac_addr[1], mac_addr[2],
             mac_addr[3], mac_addr[4], mac_addr[5]);
}

/**
 * @brief Calcola CRC per la configurazione basata sul MAC
 * @param mac_addr: array di 6 byte con il MAC address
 * @return CRC16 calcolato
 */
static uint16_t calculate_mac_crc(const uint8_t *mac_addr) {
    if (!mac_addr) {
        return 0xFFFF;
    }

    // CRC semplice basato sul MAC address
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < 6; i++) {
        crc ^= (mac_addr[i] << 8);
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}


/**
 * @brief Genera l'identità del dispositivo basata sul MAC address Bluetooth
 * @desc Usa l'indirizzo MAC hardware per creare un'identità univoca e persistente
 */
static void generate_device_identity(void) {
    uint8_t mac_addr[6] = {0};

    // Ottiene il MAC address Bluetooth
    if (!get_bt_mac_address(mac_addr)) {
        ESP_LOGE(TAG, "Failed to get MAC address, using fallback identity");
        // Fallback per errore
        slave_identity.device_id = 0xFFFF;
        slave_identity.company_id = 0xEC01;
        slave_identity.config_crc = 0xFFFF;
        snprintf(slave_identity.device_name, sizeof(slave_identity.device_name),
                 "ECL_FALLBACK_DEVICE");
        strncpy(slave_identity.serial_number, "SN_FALLBACK", sizeof(slave_identity.serial_number) - 1);
        slave_identity.serial_number[sizeof(slave_identity.serial_number) - 1] = '\0';
        memset(slave_identity.mac_address, 0xFF, sizeof(slave_identity.mac_address));
        return;
    }

    // SALVA IL MAC ADDRESS ORIGINALE nella struttura
    memcpy(slave_identity.mac_address, mac_addr, sizeof(slave_identity.mac_address));

    // Log del MAC address per debug
    ESP_LOGI(TAG, "Bluetooth MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             mac_addr[0], mac_addr[1], mac_addr[2],
             mac_addr[3], mac_addr[4], mac_addr[5]);

    // Genera ID univoci basati sul MAC
    generate_id_from_mac(mac_addr, &slave_identity.device_id, &slave_identity.company_id);

    // Genera nome dispositivo basato sul MAC
    generate_name_from_mac(mac_addr, slave_identity.device_name, sizeof(slave_identity.device_name));

    // Calcola CRC basato sul MAC
    slave_identity.config_crc = calculate_mac_crc(mac_addr);

    // Genera serial number COMPLETO basato sul MAC
    snprintf(slave_identity.serial_number, sizeof(slave_identity.serial_number),
             "SN_%02X%02X%02X%02X%02X%02X",
             mac_addr[0], mac_addr[1], mac_addr[2],
             mac_addr[3], mac_addr[4], mac_addr[5]);

    ESP_LOGI(TAG, "Generated device identity from MAC:");
    ESP_LOGI(TAG, "  Device ID: 0x%04X", slave_identity.device_id);
    ESP_LOGI(TAG, "  Company ID: 0x%04X", slave_identity.company_id);
    ESP_LOGI(TAG, "  Config CRC: 0x%04X", slave_identity.config_crc);
    ESP_LOGI(TAG, "  Device Name: %s", slave_identity.device_name);
    ESP_LOGI(TAG, "  Serial Number: %s", slave_identity.serial_number);
}

/************************************************
 * PUBLIC FUNCTIONS IMPLEMENTATION              *
 ************************************************/

/**
 * @brief Inizializza il modulo slave node
 * @desc Prepara la struttura identità basata sul MAC address hardware
 *       e inizializza l'indirizzo unicast a 0 (non provisionato).
 *       Deve essere chiamato all'avvio del sistema.
 */
/*void slave_node_init(void) {
    // Inizializzazione struttura identità
    memset(&slave_identity, 0, sizeof(slave_identity));

    // Generazione identità basata su MAC address hardware
    generate_device_identity();

    // Indirizzo iniziale non assegnato (verrà impostato durante provisioning)
    slave_identity.unicast_addr = 0x0000;

    ESP_LOGI(TAG, "Slave node initialized successfully with MAC-based identity");
}*/


/**
 * @brief Notifica il completamento del provisioning mesh
 * @desc Aggiorna l'indirizzo unicast assegnato.
 *       L'identità basata sul MAC rimane invariata.
 *       Chiamato automaticamente dal stack BLE Mesh dopo provisioning.
 * @param assigned_addr: Indirizzo unicast assegnato dal Provisioner
 */
void slave_node_on_provisioned(uint16_t assigned_addr) {
    ESP_LOGI(TAG, "Dispositivo provisionato con indirizzo: 0x%04X", assigned_addr);

    // Aggiornamento indirizzo unicast
    slave_identity.unicast_addr = assigned_addr;

    // Log identità completa aggiornata
    slave_node_log_identity();
}

/**
 * @brief Restituisce l'identità completa del nodo slave
 * @return Puntatore costante alla struttura identità del dispositivo
 */
const slave_identity_t* slave_node_get_identity(void) {
    return &slave_identity;
}

/**
 * @brief Restituisce il nome identificativo del dispositivo
 * @return Stringa costante con nome dispositivo formattato
 */
const char* slave_node_get_name(void) {
    return slave_identity.device_name;
}

/**
 * @brief Restituisce l'indirizzo unicast mesh assegnato
 * @return Indirizzo unicast (0x0000 indica dispositivo non provisionato)
 */
uint16_t slave_node_get_unicast_addr(void) {
    return slave_identity.unicast_addr;
}


/**
 * @brief Restituisce il MAC address come array di byte
 * @return Puntatore costante all'array di 6 byte del MAC address
 */
const uint8_t* slave_node_get_mac_address(void) {
    return slave_identity.mac_address;
}


/**
 * @brief Restituisce il MAC address come stringa formattata
 * @return Stringa con MAC address nel formato "3C:8A:1F:80:AE:36"
 */
const char* slave_node_get_mac_string(void) {
    static char mac_str[18]; // 17 caratteri + null terminator
    const uint8_t *mac = slave_node_get_mac_address();

    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    return mac_str;
}

/**
 * @brief Log completo dell'identità del nodo
 * @desc Stampa tutti i campi della struttura identità per debug
 *       e verifica dello stato corrente del dispositivo
 */
void slave_node_log_identity(void) {
    const slave_identity_t *identity = slave_node_get_identity();

    ESP_LOGI(TAG, "=== SLAVE NODE IDENTITY ===");
    ESP_LOGI(TAG, "Device Name: %s", identity->device_name);
    ESP_LOGI(TAG, "MAC Address: %s", slave_node_get_mac_string());
    ESP_LOGI(TAG, "Unicast Address: 0x%04X", identity->unicast_addr);
    ESP_LOGI(TAG, "Device ID: 0x%04X", identity->device_id);
    ESP_LOGI(TAG, "Company ID: 0x%04X", identity->company_id);
    ESP_LOGI(TAG, "Config CRC: 0x%04X", identity->config_crc);
    ESP_LOGI(TAG, "Serial Number: %s", identity->serial_number);
    ESP_LOGI(TAG, "Provisioned: %s",
             (identity->unicast_addr != 0x0000) ? "YES" : "NO");
    ESP_LOGI(TAG, "============================");
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////// NUOVE FUNZIONI ////////////////////////////////////////////////////

// Aggiungi in FONDO al file esistente:
static uint32_t generate_node_id_from_mac(const uint8_t *mac) {
    return ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) |
           ((uint32_t)mac[4] << 8) | mac[5];
}

static void initialize_nodo_lampada_defaults(void) {

    const slave_identity_t *identity = slave_node_get_identity();

    // ✅ USA I NOMI ESATTI DEI CAMPI
    slave_node.lampada.id_nodo = generate_node_id_from_mac(identity->mac_address);

    // Controllo illuminazione
    slave_node.lampada.stato = false;
    slave_node.lampada.intensita_luminosa = 0;
    slave_node.lampada.hue = 0;
    slave_node.lampada.saturation = 0;
    slave_node.lampada.temperatura_colore = 4000;
    slave_node.lampada.tempo_accensione = 0;
    slave_node.lampada.tempo_spegnimento = 0;

    // Posizione
    slave_node.lampada.stabile.numero = 1;
    slave_node.lampada.stabile.punti_cardinali = 0;
    slave_node.lampada.piano.numero = 1;
    slave_node.lampada.piano.punti_cardinali = 0;
    slave_node.lampada.stanza.numero = 1;
    slave_node.lampada.stanza.punti_cardinali = 0;

    // Prestazioni energetiche
    slave_node.lampada.consumi_giornalieri = 0;
    slave_node.lampada.efficienza_energetica = 0.0f;
    slave_node.lampada.tensione = 230.0f;
    slave_node.lampada.corrente = 0.0f;
    slave_node.lampada.frequenza_accensione = 0;

    // Condizioni ambientali
    slave_node.lampada.temperatura_ambientale = 20.0f;
    slave_node.lampada.umidita = 50.0f;
    slave_node.lampada.pressione = 1013.0f;

    // Interazione
    slave_node.lampada.controllo_remoto = false;
    slave_node.lampada.sincronizzazione = false;
    memset(slave_node.lampada.giorni_programmati, 0, sizeof(slave_node.lampada.giorni_programmati));

    // Connettività
    slave_node.lampada.ultima_connessione.qualita_segnale_last = 0;
    slave_node.lampada.ultima_connessione.intensita_segnale_last = 0;
    slave_node.lampada.ultima_connessione.numero_hop_last = 0;
    slave_node.lampada.stato_online = false;
    slave_node.lampada.qualita_segnale = 0;
    slave_node.lampada.intensita_segnale = 0;
    slave_node.lampada.hop = 0;
    strncpy(slave_node.lampada.protocollo, "BLE Mesh", sizeof(slave_node.lampada.protocollo));

    // Manutenzione
    slave_node.lampada.ore_vita_led = 0;
    memset(slave_node.lampada.guasti, 0, sizeof(slave_node.lampada.guasti));
    slave_node.lampada.reset.reset = false;
    memset(slave_node.lampada.reset.data_reset, 0, sizeof(slave_node.lampada.reset.data_reset));
    slave_node.lampada.reset.firmware_aggiornato = false;
    slave_node.lampada.reset.firmware_version = 1;

    // Sensori
    slave_node.lampada.sensore_movimento = false;
    slave_node.lampada.sensore_luce = 0;
    slave_node.lampada.illuminazione_intelligente = true;
}


static void apply_saved_state_to_system(void) {
    // ✅ CONTROLLA SE PWM È INIZIALIZZATO PRIMA
    if (!is_pwm_initialized()) {
        ESP_LOGW(TAG, "⏳ PWM non inizializzato - stato salvato verrà applicato dopo");
        return;
    }

 	if (slave_node.lampada.stato && slave_node.lampada.intensita_luminosa > 0) {
 	    ESP_LOGI(TAG, "💡 Stato salvato: ON - Accendo LED - Intensità: %u/100", slave_node.lampada.intensita_luminosa);
 	    board_led_operation(LED_R, LED_ON);

        // ✅ CORREGGI LA CONVERSIONE - la intensità è in scala da 0-100 a 0-32 PWM
        uint8_t pwm_level = (uint8_t)((slave_node.lampada.intensita_luminosa * LIGHT_MAX_LEVEL) / 100);

        if (pwm_level > LIGHT_MAX_LEVEL) pwm_level = LIGHT_MAX_LEVEL;

        ESP_LOGI(TAG, "🔢 Conversione - Intensità: %u/100 → PWM: %u/32",
                 slave_node.lampada.intensita_luminosa, pwm_level);

        pwmcontroller_set_level(pwm_level);
        ESP_LOGI(TAG, "🔌 Stato salvato applicato - PWM: %u/32", pwm_level);
    } else {
        ESP_LOGI(TAG, "💡 Stato salvato: OFF - Spengo LED");
        board_led_operation(LED_R, LED_OFF);

        pwmcontroller_set_level(0);
        ESP_LOGI(TAG, "🔌 Stato salvato applicato - SPENTO");
    }
}


void slave_node_init(void) {

    memset(&slave_node, 0, sizeof(slave_node));

    generate_device_identity();

    // ✅ SOLO INIZIALIZZAZIONE DEFAULT - CARICAMENTO LO FACCIAMO DOPO
    initialize_nodo_lampada_defaults();

    ESP_LOGI(TAG, "📝 NodoLampada inizializzato con valori default");
}

/**
 * @brief Carica stato salvato DOPO che storage è pronto
 */
void slave_node_load_saved_state(void) {
    ESP_LOGI(TAG, "🔍 Controllo storage esistenza...");

    if (storage_lampada_state_exists()) {
        ESP_LOGI(TAG, "🔄 Tentativo caricamento stato salvato...");

        if (storage_load_lampada_state(&slave_node.lampada)) {
            ESP_LOGI(TAG, "✅ NodoLampada caricato da storage");
            apply_saved_state_to_system();
        } else {
            ESP_LOGW(TAG, "❌ Fallback a valori default - Caricamento fallito");
        }
    } else {
        ESP_LOGI(TAG, "📝 Nessuno stato salvato trovato");
    }
}


void slave_node_update_lampada_data(const NodoLampada *new_data) {

    if (new_data == NULL) return;

    // ✅ 1. CALCOLA PWM dall'intensità luminosa
    uint8_t new_pwm = convert_intensity_to_pwm(new_data->intensita_luminosa);

    // ✅ 2. COPIA TUTTI I DATI DEL NODO LAMPADA
    memcpy(&slave_node.lampada, new_data, sizeof(NodoLampada));

    // ✅ 3. APPLICA PWM CORRISPONDENTE
    pwmcontroller_set_level(new_pwm);

    // ✅ 4. SALVA TUTTO LO STATO PERSISTENTE
    storage_save_lampada_state(&slave_node.lampada);

    ESP_LOGD(TAG, "NodoLampada aggiornato e salvato");
    ESP_LOGI(TAG, "💡 Lampada aggiornata - Intensità: %d%%, Hue: %d, Sat: %d, Stato: %s",
             new_data->intensita_luminosa, new_data->hue, new_data->saturation,
             new_data->stato ? "ON" : "OFF");
}

const NodoLampada *slave_node_get_lampada_data(void) {
    return &slave_node.lampada;
}

void slave_node_set_lampada_stato(bool stato) {
    slave_node.lampada.stato = stato;
    storage_save_lampada_state(&slave_node.lampada);
    ESP_LOGI(TAG, "Stato lampada: %s", stato ? "ON" : "OFF");
}

void slave_node_set_lampada_intensita(uint16_t intensita) {
    slave_node.lampada.intensita_luminosa = intensita;
    storage_save_lampada_state(&slave_node.lampada);
    ESP_LOGI(TAG, "Intensità: %u lumen", intensita);
}
