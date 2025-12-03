/**
 * Autore: DJITSOP FUOGOUK LOIC STEVE
 * Firmware: ECOLUMIERE BLE MESH ESP32
 * Modulo: Slave Role - Gestione identità dispositivo in rete mesh
 * Descrizione: Gestisce l'identità del dispositivo slave nella rete BLE Mesh
 */

#ifndef SLAVE_ROLE_H
#define SLAVE_ROLE_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "ecolumiere.h"

#ifdef __cplusplus
extern "C" {
#endif


// Definizione della struttura position_more_value
 typedef struct{
  uint16_t numero;
  uint16_t punti_cardinali;
 } position_more_value;

 typedef struct {
  uint32_t id_nodo;
  bool stato;
  uint16_t intensita_luminosa;
  uint16_t hue;
  uint16_t saturation;
  uint16_t temperatura_colore;
  uint16_t tempo_accensione;
  uint16_t tempo_spegnimento;
  position_more_value stabile, piano, stanza;

  uint8_t consumi_giornalieri;
  float efficienza_energetica;
  float tensione;
  float corrente;
  uint8_t frequenza_accensione;

  float temperatura_ambientale;
  float umidita;
  float pressione;

  bool controllo_remoto;
  bool sincronizzazione;
  char giorni_programmati[7][10];

  struct {
   uint8_t qualita_segnale_last;
   uint8_t intensita_segnale_last;
   uint8_t numero_hop_last;
  } ultima_connessione;

  bool stato_online;
  uint8_t qualita_segnale;
  uint8_t intensita_segnale;
  uint8_t hop;
  char protocollo[20];

  uint8_t ore_vita_led;
  char guasti[50];
  struct {
   bool reset;
   char data_reset[20];
   bool firmware_aggiornato;
   uint8_t firmware_version;
  } reset;

  bool sensore_movimento;
  int sensore_luce;
  bool illuminazione_intelligente;
 } NodoLampada;


/**
 * @brief Struttura identità completo del dispositivo slave
 * @field unicast_addr: Indirizzo mesh assegnato dal Provisioner
 * @field device_id: ID univoco del dispositivo da registro Ecolumiere
 * @field company_id: ID azienda produttrice da registro Ecolumiere
 * @field config_crc: CRC di verifica configurazione da registro Ecolumiere
 * @field device_name: Nome identificativo nel formato "ECL_XXXX_XXXX"
 * @field serial_number: Numero seriale univoco del dispositivo
 * @field mac_address: Indirizzo MAC Bluetooth del dispositivo (6 byte)
 */
typedef struct {
    uint16_t unicast_addr;
    uint16_t device_id;
    uint16_t company_id;
    uint16_t config_crc;
    char device_name[32];
    char serial_number[20];          // Cambiato da uint8_t a char
    uint8_t mac_address[6];          // NUOVO CAMPO: MAC address originale
} slave_identity_t;


// Struttura unificata del nodo slave
typedef struct {
    slave_identity_t identity;     // Identità esistente
    NodoLampada lampada;           // Nuova struttura estesa
    bool is_provisioned;           // Stato provisioning
    uint32_t last_heartbeat;       // Ultimo heartbeat
} slave_node_t;


/**
 * @brief Inizializza il modulo slave node
 * @desc Recupera l'identità del dispositivo dal registro Ecolumiere,
 *       genera il nome dispositivo e prepara la struttura identità.
 *       Deve essere chiamato all'avvio prima di ecolumiere_system_init()
 */
void slave_node_init(void);

/**
 * @brief Notifica il provisioning del nodo nella rete mesh
 * @desc Aggiorna l'indirizzo unicast assegnato e rigenera l'identità
 *       per riflettere lo stato di provisioning completato
 * @param assigned_addr: Indirizzo unicast assegnato dal Provisioner
 */
void slave_node_on_provisioned(uint16_t assigned_addr);

/**
 * @brief Restituisce l'identità completa del nodo slave
 * @return Puntatore costante alla struttura identità del dispositivo
 */
const slave_identity_t* slave_node_get_identity(void);

/**
 * @brief Restituisce il nome identificativo del dispositivo
 * @return Stringa costante con nome nel formato "ECL_XXXX_XXXX"
 */
const char* slave_node_get_name(void);

/**
 * @brief Restituisce l'indirizzo unicast mesh assegnato
 * @return Indirizzo unicast (0x0000 se dispositivo non provisionato)
 */
uint16_t slave_node_get_unicast_addr(void);

/**
 * @brief Restituisce il MAC address come array di byte
 * @return Puntatore costante all'array di 6 byte del MAC address
 */
const uint8_t* slave_node_get_mac_address(void);

/**
 * @brief Restituisce il MAC address come stringa formattata
 * @return Stringa con MAC address nel formato "3C:8A:1F:80:AE:36"
 */
const char* slave_node_get_mac_string(void);

/**
 * @brief Log dell'identità completa del nodo
 * @desc Stampa tutti i dettagli dell'identità per scopi di debug
 *       e verifica dello stato del dispositivo
 */
void slave_node_log_identity(void);

///////////////////////////////////////////////////////////////////////////////////////////////////////

 // Nuove funzioni per NodoLampada
 void slave_node_update_lampada_data(const NodoLampada *new_data);

 const NodoLampada *slave_node_get_lampada_data(void);

 void slave_node_set_lampada_stato(bool stato);

 void slave_node_set_lampada_intensita(uint16_t intensita);

 void slave_node_load_saved_state(void);


#ifdef __cplusplus
}
#endif

#endif //SLAVE_ROLE_H