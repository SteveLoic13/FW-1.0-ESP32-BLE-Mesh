/**
 * Autore: DJITSOP FUOGOUK LOIC STEVE
 * Firmware: ECOLUMIERE BLE MESH ESP32
 * Modulo: Storage - Gestione memoria persistente NVS
 * Descrizione: Gestisce il salvataggio e caricamento di configurazioni e registri su memoria flash
 */

#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include "slave_role.h"

/**
 * @brief Inizializza il modulo di storage
 * @desc Configura la memoria NVS, apre il namespace dedicato e prepara
 *       il sistema per le operazioni di lettura/scrittura
 */
void storage_init(void);

/**
 * @brief Task periodico di gestione operazioni storage
 * @desc Processa le operazioni pendenti di scrittura configurazione e registro.
 *       Deve essere chiamato periodicamente nel loop principale.
 */
void storage_task(void);

/**
 * @brief Verifica se il modulo storage è pronto per operazioni
 * @return true: Storage pronto, false: Storage occupato o non inizializzato
 */
bool storage_ready(void);

/**
 * @brief Salva la configurazione algoritmo nella memoria flash
 * @desc Copia la configurazione in un buffer interno e programma la scrittura
 *       differita su NVS. La scrittura effettiva avviene in storage_task()
 * @param config: Puntatore alla struttura di configurazione da salvare
 * @return true: Salvataggio accettato, false: Operazione già in corso
 */
bool storage_save_config(void *config);

/**
 * @brief Carica la configurazione algoritmo dalla memoria flash
 * @desc Recupera la configurazione precedentemente salvata. Se non trovata,
 *       inizializza la struttura con valori zero.
 * @param config: Puntatore alla struttura dove caricare la configurazione
 * @return true: Configurazione caricata correttamente, false: Configurazione non trovata
 */
bool storage_load_config(void *config);

/**
 * @brief Salva il registro dispositivo nella memoria flash
 * @desc Copia il registro in un buffer interno e programma la scrittura
 *       differita su NVS. La scrittura effettiva avviene in storage_task()
 * @param registry: Puntatore alla struttura registro da salvare
 * @return true: Salvataggio accettato, false: Operazione già in corso
 */
bool storage_save_registry(void *registry);

/**
 * @brief Carica il registro dispositivo dalla memoria flash
 * @desc Recupera il registro precedentemente salvato. Se non trovato,
 *       inizializza la struttura con valori zero.
 * @param registry: Puntatore alla struttura dove caricare il registro
 */
void storage_load_registry(void *registry);


bool storage_is_ready_for_write(void);



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////// NUOVE FUNZIONI ////////////////////////////////////////////////////

bool storage_save_lampada_state(const NodoLampada *lampada);

bool storage_load_lampada_state(NodoLampada *lampada);

bool storage_lampada_state_exists(void);

#endif //STORAGE_H