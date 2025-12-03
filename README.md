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
