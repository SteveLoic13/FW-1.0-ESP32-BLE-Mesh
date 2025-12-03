// Created by Admin on 12/11/2025.
/**
 * @file ble_mesh_ecolumiere.h
 * @brief Header per implementazione BLE Mesh personalizzata Ecolumiere
 */

#ifndef BLE_MESH_ECOLUMIERE_H
#define BLE_MESH_ECOLUMIERE_H

#include "esp_ble_mesh_defs.h"

/* Sensor Property ID */
#define SENSOR_PROPERTY_ID_0        0x0056  /* Temperatura */
#define SENSOR_PROPERTY_ID_1        0x2A5D  /* Potenza istantanea assorbita !!BIG ENDIAN!! */
#define SENSOR_PROPERTY_ID_2        0x004F  /* Humidity */
#define SENSOR_PROPERTY_ID_3        0x0061  /* Pressure */
#define SENSOR_PROPERTY_ID_4        0x7777  /* Error */
#define SENSOR_PROPERTY_ID_5        0x0045  /* Illuminance */
#define SENSOR_PROPERTY_ID_6        0x0046  /* tensione */
#define SENSOR_PROPERTY_ID_7        0x0047  /* corrente */

#define DEFAULT_TTL 7
#define CID_ESP     0x02E5
#define LIGHTNESS_MAX    100


#define ESP_BLE_MESH_VND_MODEL_ID_CLIENT    0x0000
#define ESP_BLE_MESH_VND_MODEL_ID_SERVER    0x0001

#define ESP_BLE_MESH_VND_MODEL_OP_SEND      ESP_BLE_MESH_MODEL_OP_3(0x00, CID_ESP)
#define ESP_BLE_MESH_VND_MODEL_OP_STATUS    ESP_BLE_MESH_MODEL_OP_3(0x01, CID_ESP)



// Struttura per dati di configurazione estesa
typedef struct {
 int32_t brightness;
 int32_t color_temp;
 int32_t rgb[3];
 int32_t dimStep;
 int32_t on_delay;
 int32_t off_delay;
} configdata_t;

/**
 * @brief Inizializza BLE Mesh per sistema Ecolumiere
 * @return esp_err_t
 */
esp_err_t ble_mesh_ecolumiere_init(void);

/**
 * @brief Restituisce UUID dispositivo
 * @param uuid Buffer per UUID (16 byte)
 */
void ble_mesh_ecolumiere_get_dev_uuid(uint8_t *uuid);

/**
 * @brief Aggiorna dati sensori in tempo reale
 */
void ble_mesh_ecolumiere_update_sensor_data(void);


void sync_nodo_lampada_with_hsl(uint16_t hue, uint16_t saturation, uint16_t lightness);

#endif //BLE_MESH_ECOLUMIERE_H
