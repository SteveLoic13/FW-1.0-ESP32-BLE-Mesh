# Neodelis BLE Mesh Test Device

## Testing Environment

1. Prerequisiti per avviare l'ambiente di svilppo:

   - clonare la repository di Espressif: https://github.com/espressif/esp-idf
   - assicurarsi di aver eseguito ". ./esp/esp-idf/export.sh" nel terminale di lavoro corrente

2. Una volta eseguito quanto sopra, clonare questo progetto in una cartella, ed assicurarsi di copiare le cartelle components/ e tools/ da /esp/esp-idf/ nella directory del progetto, per ottenere un direttorio del tipo:

```ini
project-neodelis-ecolumiere-meshtestdevice/
├── components/
├── sensor_server/
├── tools/
└── README.md
```

3. Assicurati che il comando "idf.py" lanciato da terminale non dia errore (in caso ripetere punto 2 dei prerequisiti). Fatto ciò entra nella cartella sensor_server/ e lancia "idf.py menuconfig"

4. Nel pannello appena aperto, assicurarsi che le seguenti opzioni siano attivate:

   - Component config --> ESP BLE Mesh Support --> (*) Support for BLE Mesh Node
   - Component config --> ESP BLE Mesh Support --> Support for BLE Mesh Client/Server models --> (*) Sensor Server model
   - Component config --> ESP BLE Mesh Support --> Support for BLE Mesh Client/Server models --> (*) Lighting Server models

   Adesso uscire (q) e salvare (y).

5. Lanciare "idf.py set-target esp32" per impostare il tipo di scheda con cui si vuole lavorare.

6. Lanciare "idf.py build" per compilare il progetto, ed infine "idf.py -p PORTASERIALE flash monitor" per flashare i binari alla scheda. Esempio di PORTASERIALE è /dev/ttyUSB0.
   Usare la combinazione "Ctrl + AltGr + ]" per uscire dal monitor seriale appena aperto.

## Funzionamento

Tutti i codici op sono standard Bluetooth Mesh BLE. Attenersi alla lista [Mesh Model Message Opcodes](https://www.bluetooth.com/wp-content/uploads/Files/Specification/Assigned_Numbers.html#bookmark141) ufficiale.

### Sensori

Si ricorda che i valori di temperatura e potenza dissipata sono fittizi e impostati a priori dal programmatore, in quanto ambiente di sviluppo.
Si notino le assegnazioni di `indoor_temp = 40` e `potenza_istantanea_assorbita = 2410`.
Quest'ultima, in particolare, verrà divisa per 100 in fase di elaborazione dal gateway, di modo che il nodo possa lavorare più agevolmente con semplici interi (quindi il valore reale visto nel DB sarà 24.10 W).

Al momento, sono integrati tre tipi di dati sensoriali:

- Temperatura indoor (0x0056)
- Potenza istantanea dissipata (0x025d)
- Qualità del segnale (ricavata dal gateway tramite RSSI)
- Hop effettuati (ricavati dal gateway tramite TTL)

Ad una richiesta con opcode `ESP_BLE_MESH_MODEL_OP_SENSOR_GET` (0x8231) cadenzata dal gateway ogni SENSOR_TIME secondi, il nodo risponde con un pacchetto di opcode `ESP_BLE_MESH_MODEL_OP_SENSOR_STATUS` (0x52) contente i dati dei sensori.

Il gateway riceverà informazioni del tipo:

```sh
 ADDR          RSSI         TTL         ID_S1   S1_DATA     ID_S2      S2_DATA
0x0005      0xffffffe1     0x0007      0x0056     28        0x025d      6A09 
```

Nel caso della potenza assorbita, notare che i dati sensoriali sono riportati in big endian.

### Luce

Il nodo accetta comandi per:

- Modificare il colore e la luminosità della luce in HSL: `ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_SET` (0x8276)

Una volta aggiornata la lampada con le impostazioni appena inviate, il nodo risponderà rispettivamente con

- `ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_STATUS` (0x8278)

### Integrazione con il gateway

Una volta alimentata la scheda, essa aspetterà il proprio provisioning, da parte del gateway. Una volta completato, si avvierà il timer del gateway citato sopra, per l'acquisizione dei dati sensoriali.
Rimarrà quindi in ascolto di evenutuali comandi di SET.

Questi comandi sono impartiti dal gateway, a cui, a sua volta, vengono inoltrati via UART da un altro processore, che andrà a consulatre il database. Si può osservare il seguente schema per avere un'idea complessiva del sistema.

![Schema di comunicazione](schema-comm.png)

Un esempio di comando inviato via UART può essere `hsl 12 34 56`, che il gateway interpreterà come invio di opcode 0x8276 con valori 12, 34, 56. In futuro il comando prenderà la forma di `hsl 0x1234 12 34 56`, per una rete con molteplici nodi.

## SPECIFICHE SISTEMA ECOLUMIERE MESH

Le lampade intelligenti all’interno di un sistema Ecolumiere Mesh presentano parametri fondamentali per il monitoraggio e la gestione. Questi parametri possono essere suddivisi in caratteristiche fisiche, prestazioni energetiche, condizioni ambientali, interazione con l’utente e manutenzione. Le lampade sfruttano la tecnologia Bluetooth Mesh, in cui i nodi (lampade) sono collegati a un gateway che funge da ponte tra la rete Bluetooth e quella Wi-Fi.

### Caratteristiche dei nodi

- I nodi non devono essere distanti più di 10 metri l’uno dall’altro, preferibilmente senza ostacoli intermedi.
- I nodi che aggregano altri nodi vengono elevati a relay node, creando una gerarchia all’interno della rete. Ad esempio, i nodi dei corridoi o delle scale possono diventare relay node, mentre quelli delle stanze restano nodi semplici. Se la comunicazione diretta tra alcuni nodi viene interrotta, il relay node garantisce l’inoltro corretto dei messaggi, mantenendo la continuità della rete Bluetooth Mesh.

### Informazioni raccolte dal gateway

Il gateway raccoglie i seguenti dati per ciascun nodo:
- `ID`: identificativo univoco della lampada
- `Caratteristiche fisiche`
  - **Stato ON/OFF**
  - **Intensità luminosa** (lumen)
  - **Temperatura del colore**: luce bianca calda (~2500K) o bianca fredda (~6500K)
  - **Colori della luce**: bianca o RGB; in caso RGB indicare il colore attuale
  - **Durata di accensione** e spegnimento: tempo totale (in ore/minuti) durante il quale la lampada è rimasta accesa o spenta.
  - **Posizione del nodo**: identificazione alfanumerica basata sulla planimetria, comprendente stabile, piano, stanza e numero progressivo della lampada (Es: S1–P3–S5–L14 indica Stabile 1, Piano 3, Stanza 5, Lampada 14). La numerazione di ciascun elemento dovrebbe essere definita in fase di stesura della planimetria
- `Prestazioni energetiche`
  - **Consumo giornaliero di potenza** (W)
  - **Efficienza energetica**: rapporto tra luminosità emessa e potenza consumata
  - **Tensione e corrente**: parametri di stabilità ed efficienza dell’alimentatore
  - **Energia residua**: utile per valutare la disponibilità complessiva del sistema
  - **Frequenza di accensione e spegnimento**: consente di capire quando la lampada è effettivamente in uso
- `Condizioni ambientali`
  - **Temperatura** (°C): monitorare eventuali surriscaldamenti
  - **Umidità**
  - **Pressione atmosferica**: ottenere informazioni ambientali a più ampio spettro
- `Interazione e controlli`
  - **Controllo remoto**: verifica se la lampada è stata monitorata tramite APP e registra l’ultimo accesso
  - **Sincronizzazione**: tiene traccia dello stato della rete e dei dispositivi vicini
  - **Giorni programmati**: possibilità di programmare accensione, spegnimento e intensità luminosa della lampada in base a giorni e orari
- `Connettività`
  - **Ultima connessione**: registra orario, qualità e intensità del segnale, numero di hop necessari
  - **Stato online** della lampada rispetto al gateway
  - **Qualità e intensità del segnale**
  - **Numero di hop** necessari per raggiungere la destinazione
  - **Tipo di protocollo** utilizzato
- `Manutenzione`
  - **Ore di vita** dei led
  - **Guasti**: cortocircuiti o sovraccarichi
  - **Reset**: verifica eventuali reset della lampada, aggiornamenti firmware e orario dell’ultimo aggiornamento
- `Funzioni extra`
  - **Sensore di movimento**: rileva la presenza di persone in una stanza e lo stato di funzionamento
  - **Sensore di luce**: rileva e riporta i valori luminosi ambientali
  - **Illuminazione intelligente**: autoregolazione del sistema in base alle condizioni ambientali e alle impostazioni di programmazione
 
### Scansione dei nodi

Si consiglia di eseguire la scansione dei nodi due volte al giorno, al mattino e alla sera, corrispondenti all’apertura e alla chiusura dell’azienda. Il gateway verifica quali nodi hanno risposto alle richieste inviate e genera un report. Se qualche lampada non risponde, confronta l’elenco dei nodi programmati con quelli che hanno risposto e invia un alert all’app per segnalare eventuali mancanze. Un singolo gateway può gestire fino a circa 30.000 nodi all’interno di una rete.

## Struttura del Progetto EcolumiereBleMeshESP32

```EcolumiereBleMeshESP32/
├── 📁 main/                         # Applicazione principale
│   ├── main.c                       # Entry point (app_main)
│   ├── board.c                      # Inizializzazione hardware specifica
│   ├── board.h                      # Definizioni hardware
│   ├── component.mk                 # Configurazione componenti (legacy)
│   └── CMakeLists.txt               # Build configuration
│
├── 📁 ecolumiere/                   # Framework core del sistema
│   ├── ecolumiere_system.c/.h       # Gestione e configurazione sistema
│   ├── scheduler.c/.h               # Scheduler eventi e gestione code
│   ├── pwmcontroller.c/.h           # Controllo PWM LED e sequenze
│   ├── zerocross.c/.h               # Rilevamento zero-cross per dimming AC
│   ├── luxmeter.c/.h                # Gestione sensore luce (ADC)
│   ├── lightcode.c/.h               # Sistema comunicazione ottica
│   ├── storage.c/.h                 # Gestione memoria flash NVS
│   ├── datarecorder.c/.h            # Logging dati e storico
│   ├── slave_role.c/.h              # Identità dispositivo e logica slave
│   ├── ecolumiere.c/.h              # Algoritmo intelligente principale
│   └── CMakeLists.txt               # Configurazione build componente
│
├── 📁 ble_mesh_ecolumiere/          # Comunicazione BLE Mesh
│   ├── ble_mesh_ecolumiere.c        # Implementazione principale BLE Mesh
│   └── ble_mesh_ecolumiere.h        # Header file
│
├── 📁 tools/                        # Strumenti e utility
│   ├── flash_tool.py                # Script Python per flashing
│   ├── config_generator.py          # Generatore configurazioni
│   ├── monitor_serial.py            # Monitor seriale avanzato
│   └── README_tools.md              # Documentazione tools
│
├── 📁 components/                  # Componenti ESP-IDF opzionali
│   └── 📄 CMakeLists.txt           # Configurazione build per eventuali componenti futuri
│
├── 📄 CMakeLists.txt               # Configurazione build principale
├── 📄 sdkconfig                    # Configurazione ESP-IDF
├── 📄 .gitignore                   # File ignorati da Git
└── 📄 README.md                    # Documentazione principale
```


ble_mesh_ecolumiere.c - Sistema BLE Mesh per Illuminazione Intelligente Ecolumiere
Panoramica
Questo file implementa il nodo BLE Mesh per il sistema di illuminazione intelligente Ecolumiere. Combina modelli standard (Sensor, HSL) con un modello vendor personalizzato per gestire controllo luci, monitoraggio sensori e comandi applicativi specifici.

Architettura del Nodo
1. Modelli Implementati
Il nodo implementa tre modelli principali BLE Mesh:

a) Modello Sensor (Standard SIG)
Scopo: Esporre letture da 8 sensori ambientali ed energetici

Sensori gestiti:

Temperatura interna (indoor_temp, 1 byte)

Potenza istantanea assorbita (potenza_istantanea_assorbita, 2 byte, BIG ENDIAN)

Umidità (humidity_sensor, 2 byte, risoluzione 0.01%)

Pressione (pressure_sensor, 2 byte, risoluzione 0.01 hPa)

Codice errore (error_code, 1 byte)

Illuminamento (illuminance_sensor, 4 byte, lux)

Tensione (voltage_sensor, 2 byte, risoluzione 0.01V)

Corrente (current_sensor, 2 byte, risoluzione 0.01A)

b) Modello HSL - Hue, Saturation, Lightness (Standard SIG)
Scopo: Controllo avanzato dell'illuminazione

Stato gestito: hsl_state con lightness (0-100%), hue (tonalità), saturation (saturazione)

Funzionalità: Transizioni graduali, controllo colore, impostazione target

c) Modello Vendor Personalizzato
Scopo: Comandi custom specifici del sistema Ecolumiere

Struttura dati: configdata_t con brightness, color_temp, RGB, dimStep

Uso: Configurazioni avanzate non coperte dallo standard HSL

2. Strutture Dati Principali
Stato HSL Globale
c
static esp_ble_mesh_light_hsl_state_t hsl_state = {
    .lightness = 0xFFFF,      // Luminosità corrente (max)
    .hue = 0,                 // Tonalità (0-360°)
    .saturation = 0xFFFF,     // Saturazione (max)
    .target_lightness = 0xFFFF,  // Valori target per transizioni
    .target_hue = 0,
    .target_saturation = 0xFFFF,
    .status_code = ESP_BLE_MESH_MODEL_STATUS_SUCCESS
};
Buffer Dati Sensori
8 buffer NetBuf statici contenenti i dati grezzi dei sensori, formattati secondo le specifiche Mesh Model.

Funzioni Principali
1. Inizializzazione - ble_mesh_ecolumiere_init()
Scopo: Configura e avvia lo stack BLE Mesh

Azioni:

Registra tutte le callback necessarie

Inizializza modelli (Configuration, Sensor, HSL, Vendor)

Abilita provisioning su bearer ADV e GATT

Accende LED verde come indicatore di stato

2. Callback di Provisioning - example_ble_mesh_provisioning_cb()
Gestisce tutti gli eventi del processo di provisioning:

PROV_REGISTER_COMP_EVT: Registrazione componenti completata

NODE_PROV_ENABLE_COMP_EVT: Nodo visibile ai provisioner

NODE_PROV_LINK_OPEN_EVT: Collegamento provisioning iniziato

NODE_PROV_COMPLETE_EVT: Momento critico - provisioning completato

NODE_PROV_RESET_EVT: Reset a fabbrica

Nota: Al completamento del provisioning, chiama prov_complete() che:

Spegne LED verde

Notifica il modulo slave

Avvia l'acquisizione del luxmeter

Inizializza i dati dei sensori

3. Callback Configuration Server - example_ble_mesh_config_server_cb()
Gestisce la configurazione di rete inviata dal provisioner:

APP_KEY_ADD: Aggiunge chiave di crittografia applicativa (16 byte)

MODEL_APP_BIND: Associa AppKey a modello specifico (es: HSL usa AppKey 0x0000)

MODEL_SUB_ADD: Sottoscrive modello a indirizzo di gruppo (es: HSL ascolta gruppo 0xC001)

4. Callback Sensor Server - example_ble_mesh_sensor_server_cb()
Gestisce richieste di lettura sensori:

Risponde a SENSOR_GET con dati formattati secondo specifiche Mesh

Supporta richieste per singolo sensore o tutti i sensori

Usa example_ble_mesh_send_sensor_status() per costruire risposta

5. Callback Lighting Server - example_ble_mesh_light_server_cb()
Gestisce controllo illuminazione HSL:

LIGHT_HSL_SET: Imposta hue, saturation, lightness

LIGHT_HSL_GET: Restituisce stato corrente (convertito da PWM)

Conversione: Lightness (0-100%) → PWM (0-32) via convert_lightness_to_pwm()

Sincronizzazione: Aggiorna struttura NodoLampada via sync_nodo_lampada_with_hsl()

6. Callback Modello Custom - example_ble_mesh_custom_model_cb()
Gestisce comandi vendor personalizzati:

Opcode: ESP_BLE_MESH_VND_MODEL_OP_SEND

Dati: Struttura configdata_t con parametri applicativi

Flusso:

Riceve comando custom

Converte brightness a PWM

Crea evento per scheduler (SCH_EVT_BLE_MESH_RX)

Invia risposta di conferma

7. Funzioni di Supporto
sensor_data_initialize()
Inizializza buffer sensori con valori predefiniti (prototipo) o letture reali.

convert_lightness_to_pwm(uint16_t lightness)
Converte valori lightness (0-100% o 0-65535) a livello PWM (0-32) per controllo LED.

example_ble_mesh_get_sensor_data()
Formatta dati sensore secondo specifiche Mesh (MPID + raw value).

example_ble_mesh_send_sensor_status()
Costruisce e invia messaggio SENSOR_STATUS con dati di uno o tutti i sensori.

sync_nodo_lampada_with_hsl()
Sincronizza struttura NodoLampada con stato HSL corrente, gestendo:

Intensità luminosa (0-100%)

Timestamp accensione/spegnimento

Registrazione eventi nel data recorder

Flusso di Controllo Luce
text
APP → [Comando HSL/Vendor] → RETE MESH → Nodo Ecolumiere
                                        ↓
1. Callback riceve messaggio (MODEL_OPERATION_EVT)
2. Se HSL: aggiorna hsl_state → convert_lightness_to_pwm()
3. Se Vendor: estrae configdata_t → crea evento scheduler
4. Imposta PWM via pwmcontroller_set_level()
5. Aggiorna LED fisico (board_led_operation())
6. Sincronizza NodoLampada (sync_nodo_lampada_with_hsl())
7. Invia risposta (MODEL_SEND_COMP_EVT)
Configurazione Mesh
Composizione Nodo
c
static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,              // Company ID: Espressif (0x02E5)
    .element_count = 1,          // Singolo elemento
    .elements = elements         // Modelli root + vendor
};
Provisioning
c
static esp_ble_mesh_prov_t provision = {
    .uuid = dev_uuid,            // UUID dispositivo: {0x32, 0x10}
};
Integrazione con Sistema Ecolumiere
Scheduler
Tutti i comandi BLE Mesh vengono convertiti in eventi (ble_mesh_event_t) e inseriti nello scheduler globale per elaborazione asincrona.

Slave Node
Al completamento provisioning, notifica slave_node_on_provisioned() per aggiornare lo stato del sistema.

Data Recorder
Registra eventi significativi (comandi ricevuti, cambiamenti stato) via data_recorder_enqueue_lampada_event().

Hardware Control
PWM: pwmcontroller_set_level() per controllo intensità LED

Luxmeter: luxmeter_start_acquisition() avviato post-provisioning

LED Board: Feedback visivo stato (verde=provisioning, rosso=attivo)

Considerazioni di Sicurezza
AppKey Binding: Ogni modello usa AppKey specifica per crittografia

Validazione Messaggi: Solo messaggi crittografati con AppKey valida sono processati

Controlli Accesso: Binding definisce quali modelli possono usare quali chiavi

Note di Sviluppo
Buffer Sensori: Alcuni buffer potrebbero essere dimensionati erroneamente (es: sensor_data_2 dichiarato 1 byte ma contiene uint16_t)

Codice Commentato: Sezioni per ri-abilitazione relay post-provisioning necessitano completamento

Logging: Sistema di log esteso per debug provisioning e operazioni mesh

Dipendenze
c
// BLE Mesh Core
esp_ble_mesh_*              // Stack BLE Mesh ESP-IDF

// Moduli Ecolumiere
scheduler.h                 // Gestione eventi asincroni
board.h                     // Driver hardware
luxmeter.h                  // Sensore illuminamento
pwmcontroller.h             // Controllo PWM LED
slave_role.h                // Gestione ruolo slave
ecolumiere_system.h         // Sistema principale
datarecorder.h              // Registrazione eventi
