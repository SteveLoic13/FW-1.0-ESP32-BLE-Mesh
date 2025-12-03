//
// Created by Admin on 12/11/2025.
//

/**
 * @file ble_mesh_ecolumiere.c
 * @brief Implementazione BLE Mesh personalizzata per sistema Ecolumiere
 * @details Gestione modelli Sensor e HSL con dati reali dal sistema
 */

#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_timer.h"

#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_sensor_model_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"

// HSL model
#include "esp_ble_mesh_lighting_model_api.h"
#include "ble_mesh_example_init.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "scheduler.h"

#include "ble_mesh_ecolumiere.h"
#include "board.h"
#include "luxmeter.h"
#include "pwmcontroller.h"
#include "slave_role.h"
#include "ecolumiere_system.h"
#include "datarecorder.h"


static const char *TAG = "BLE_MESH_ECOLUMIERE";

static int8_t indoor_temp = 40;     /* Indoor temperature is 20 Degrees Celsius */
static uint16_t potenza_istantanea_assorbita = 2410;    /* Potenza istantanea assorbita !!BIG ENDIAN!!*/
static uint16_t humidity_sensor = 10000; /* Humidity is 100% with a resolution of 0.01% */
static uint16_t pressure_sensor = 10000; /* Pressure is 1000.00 hPa with a resolution of 0.01 hPa */
static uint8_t error_code = 0;          /* Error code 0 means no error */
static uint32_t illuminance_sensor = 300; /* Illuminance is 300 lx */
static uint16_t voltage_sensor = 2300; /* Voltage is 23.00 V with a resolution of 0.01 V */
static uint16_t current_sensor = 100; /* Current is 1.00 A with a resolution of 0.01 A */


// Configurazione server
esp_ble_mesh_cfg_srv_t config_server = {
    .net_transmit = ESP_BLE_MESH_TRANSMIT(4, 50),
    .relay = ESP_BLE_MESH_RELAY_ENABLED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(4, 50),
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
#if defined(CONFIG_BLE_MESH_GATT_PROXY_SERVER)
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
#else
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
#endif
#if defined(CONFIG_BLE_MESH_FRIEND)
    .friend_state = ESP_BLE_MESH_FRIEND_ENABLED,
#else
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
#endif
    .default_ttl = DEFAULT_TTL,
};

// Buffer per dati sensore
NET_BUF_SIMPLE_DEFINE_STATIC(sensor_data_0, 1);    //Temperatura
NET_BUF_SIMPLE_DEFINE_STATIC(sensor_data_1, 2);    //Potenza
NET_BUF_SIMPLE_DEFINE_STATIC(sensor_data_2, 1);    //Humidita'
NET_BUF_SIMPLE_DEFINE_STATIC(sensor_data_3, 2);    //Pressione
NET_BUF_SIMPLE_DEFINE_STATIC(sensor_data_4, 1);    //Error
NET_BUF_SIMPLE_DEFINE_STATIC(sensor_data_5, 2);    //Illuminazione
NET_BUF_SIMPLE_DEFINE_STATIC(sensor_data_6, 2);    //Tensione
NET_BUF_SIMPLE_DEFINE_STATIC(sensor_data_7, 2);    //Corrente

// UUID dispositivo
uint8_t dev_uuid[ESP_BLE_MESH_OCTET16_LEN] = { 0x32, 0x10 };

// Definizioni tolleranze sensore
#define SENSOR_POSITIVE_TOLERANCE   ESP_BLE_MESH_SENSOR_UNSPECIFIED_POS_TOLERANCE
#define SENSOR_NEGATIVE_TOLERANCE   ESP_BLE_MESH_SENSOR_UNSPECIFIED_NEG_TOLERANCE
#define SENSOR_SAMPLE_FUNCTION      ESP_BLE_MESH_SAMPLE_FUNC_UNSPECIFIED
#define SENSOR_MEASURE_PERIOD       ESP_BLE_MESH_SENSOR_NOT_APPL_MEASURE_PERIOD
#define SENSOR_UPDATE_INTERVAL      ESP_BLE_MESH_SENSOR_NOT_APPL_UPDATE_INTERVAL

static uint8_t convert_lightness_to_pwm(uint16_t lightness);

// Stato HSL
static esp_ble_mesh_light_hsl_state_t hsl_state = {
    .lightness = 0xFFFF,
    .hue = 0,
    .saturation = 0xFFFF,
    .target_lightness = 0xFFFF,
    .target_hue = 0,
    .target_saturation = 0xFFFF,
    .status_code = ESP_BLE_MESH_MODEL_STATUS_SUCCESS,
};

// Stati sensori
static esp_ble_mesh_sensor_state_t sensor_states[8] = {
    /* Mesh Model Spec:
     * Multiple instances of the Sensor states may be present within the same model,
     * provided that each instance has a unique value of the Sensor Property ID to
     * allow the instances to be differentiated. Such sensors are known as multisensors.
     * In this example, two instances of the Sensor states within the same model are
     * provided.
     */
    [0] = {
        /* Mesh Model Spec:
         * Sensor Property ID is a 2-octet value referencing a device property
         * that describes the meaning and format of data reported by a sensor.
         * 0x0000 is prohibited.
         */
        .sensor_property_id = SENSOR_PROPERTY_ID_0,
        /* Mesh Model Spec:
         * Sensor Descriptor state represents the attributes describing the sensor
         * data. This state does not change throughout the lifetime of an element.
         */
        .descriptor = {
            .positive_tolerance = SENSOR_POSITIVE_TOLERANCE,
            .negative_tolerance = SENSOR_NEGATIVE_TOLERANCE,
            .sampling_function = SENSOR_SAMPLE_FUNCTION,
            .measure_period = SENSOR_MEASURE_PERIOD,
            .update_interval = SENSOR_UPDATE_INTERVAL,
        },
        .sensor_data = {
            .format = ESP_BLE_MESH_SENSOR_DATA_FORMAT_A,
            .length = 0, /* 0 represents the length is 1 */
            .raw_value = &sensor_data_0,
        },
    },
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
            .length = 1, /* 0 represents the length is 1 */
            .raw_value = &sensor_data_1,
        },
    },
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
            .length = 1, /* 0 represents the length is 1 */
            .raw_value = &sensor_data_2,
        },
    },
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
            .length = 1, /* 0 represents the length is 1 */
            .raw_value = &sensor_data_3,
        },
    },
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
            .length = 1, /* 0 represents the length is 1 */
            .raw_value = &sensor_data_4,
        },
    },
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
            .length = 1, /* 0 represents the length is 1 */
            .raw_value = &sensor_data_5,
        },
    },
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
            .length = 1, /* 0 represents the length is 1 */
            .raw_value = &sensor_data_6,
        },
    },
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
            .length = 1, /* 0 represents the length is 1 */
            .raw_value = &sensor_data_7,
        },
    },
};

// Server HSL
ESP_BLE_MESH_MODEL_PUB_DEFINE(hsl_pub, 2 + 9, ROLE_NODE);
static esp_ble_mesh_light_hsl_srv_t hsl_server = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
        .set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    },
    .state = &hsl_state,
};

// Setup Server HSL
ESP_BLE_MESH_MODEL_PUB_DEFINE(hsl_setup_pub, 2 + 9, ROLE_NODE);
static esp_ble_mesh_light_hsl_setup_srv_t hsl_setup_server = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
        .set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    },
    .state = &hsl_state,
};


static esp_ble_mesh_model_op_t vnd_op[] = {
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_SEND, 2),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_model_t vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(CID_ESP, ESP_BLE_MESH_VND_MODEL_ID_SERVER,
    vnd_op, NULL, NULL),
};


// Server Sensore
ESP_BLE_MESH_MODEL_PUB_DEFINE(sensor_pub, 20, ROLE_NODE);
static esp_ble_mesh_sensor_srv_t sensor_server = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_RSP_BY_APP,
        .set_auto_rsp = ESP_BLE_MESH_SERVER_RSP_BY_APP,
    },
    .state_count = ARRAY_SIZE(sensor_states),
    .states = sensor_states,
};

// Setup Server Sensore
ESP_BLE_MESH_MODEL_PUB_DEFINE(sensor_setup_pub, 20, ROLE_NODE);
static esp_ble_mesh_sensor_setup_srv_t sensor_setup_server = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_RSP_BY_APP,
        .set_auto_rsp = ESP_BLE_MESH_SERVER_RSP_BY_APP,
    },
    .state_count = ARRAY_SIZE(sensor_states),
    .states = sensor_states,
};

// Modelli root
static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    // Sensor model
    ESP_BLE_MESH_MODEL_SENSOR_SRV(&sensor_pub, &sensor_server),
    ESP_BLE_MESH_MODEL_SENSOR_SETUP_SRV(&sensor_setup_pub, &sensor_setup_server),
    // HSL model
    ESP_BLE_MESH_MODEL_LIGHT_HSL_SRV(&hsl_pub, &hsl_server),
    ESP_BLE_MESH_MODEL_LIGHT_HSL_SETUP_SRV(&hsl_setup_pub, &hsl_setup_server),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, vnd_models),
};

// Composizione
static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .element_count = ARRAY_SIZE(elements),
    .elements = elements,
};

// Provisioning
static esp_ble_mesh_prov_t provision = {
    .uuid = dev_uuid,
};

/**
 * @brief Inizializza dati sensori con valori reali dal sistema
 */
static void sensor_data_initialize(void)
{
    //Inizializza sensori
    net_buf_simple_add_u8(&sensor_data_0, indoor_temp);
    net_buf_simple_add_le16(&sensor_data_1, potenza_istantanea_assorbita);
    net_buf_simple_add_le16(&sensor_data_2, humidity_sensor);
    net_buf_simple_add_le16(&sensor_data_3, pressure_sensor);
    net_buf_simple_add_u8(&sensor_data_4, error_code);
    net_buf_simple_add_le32(&sensor_data_5, illuminance_sensor);
    net_buf_simple_add_le16(&sensor_data_6, voltage_sensor);
    net_buf_simple_add_le16(&sensor_data_7, current_sensor);

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
 * @brief Callback completamento provisioning
 */
static void prov_complete(uint16_t net_idx, uint16_t addr, uint8_t flags, uint32_t iv_index)
{
    ESP_LOGI(TAG, "net_idx 0x%03x, addr 0x%04x", net_idx, addr);
    ESP_LOGI(TAG, "flags 0x%02x, iv_index 0x%08" PRIx32, flags, iv_index);
    board_led_operation(LED_G, LED_OFF);

    // Notifica slave node del provisioning.   ✅ IL DISPOSITIVO VIENE AUTOMATICAMENTE AGGIUNTO ALLA CODA nella funzione sotto
    slave_node_on_provisioned(addr);

    // Avvia acquisizione luxmeter
    luxmeter_start_acquisition();

    // Inizializza sensori con dati reali
    sensor_data_initialize();
}

/**
 * @brief Callback provisioning BLE Mesh
 */
static void example_ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                                             esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROV_REGISTER_COMP_EVT, err_code %d", param->prov_register_comp.err_code);
        break;
    case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT, err_code %d", param->node_prov_enable_comp.err_code);
        break;
    case ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT, bearer %s",
            param->node_prov_link_open.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
        break;
    case ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT, bearer %s",
            param->node_prov_link_close.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
        break;
    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT");
        prov_complete(param->node_prov_complete.net_idx, param->node_prov_complete.addr,
            param->node_prov_complete.flags, param->node_prov_complete.iv_index);
        break;
    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_RESET_EVT");
        break;
    case ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT, err_code %d", param->node_set_unprov_dev_name_comp.err_code);
        break;
    default:
        break;
    }
}

/**
 * @brief Callback configuration server
 */
static void example_ble_mesh_config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                                              esp_ble_mesh_cfg_server_cb_param_t *param)
{
    if (event == ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT) {
        switch (param->ctx.recv_op) {
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD");
            ESP_LOGI(TAG, "net_idx 0x%04x, app_idx 0x%04x",
                param->value.state_change.appkey_add.net_idx,
                param->value.state_change.appkey_add.app_idx);
            ESP_LOG_BUFFER_HEX("AppKey", param->value.state_change.appkey_add.app_key, 16);
            break;
        case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND");
            ESP_LOGI(TAG, "elem_addr 0x%04x, app_idx 0x%04x, cid 0x%04x, mod_id 0x%04x",
                param->value.state_change.mod_app_bind.element_addr,
                param->value.state_change.mod_app_bind.app_idx,
                param->value.state_change.mod_app_bind.company_id,
                param->value.state_change.mod_app_bind.model_id);
            break;
        case ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD:
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD");
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


static void example_ble_mesh_custom_model_cb(esp_ble_mesh_model_cb_event_t event,
                                             esp_ble_mesh_model_cb_param_t *param)
{
    switch (event) {

    case ESP_BLE_MESH_MODEL_OPERATION_EVT:
        if (param->model_operation.opcode == ESP_BLE_MESH_VND_MODEL_OP_SEND) {

            if (param->model_operation.length == sizeof(configdata_t)) {
                configdata_t *cfg = (configdata_t *)param->model_operation.msg;

                // ✅ brightness/lightness è 0-100%
                uint8_t brightness_percent = cfg->brightness;

                if(cfg->brightness == 1){
                    brightness_percent = 0;
                }

                // ✅ Converti 0-100% → PWM 0-32
                uint8_t pwm_level = (brightness_percent * LIGHT_MAX_LEVEL) / 100;

                // Limita a LIGHT_MAX_LEVEL (32)
                if (pwm_level > LIGHT_MAX_LEVEL) {
                    pwm_level = LIGHT_MAX_LEVEL;
                }

                ESP_LOGI(TAG, "📱 Ricevuto comando BLE Mesh Luminosita: %d%% → PWM=%u/32",
                         brightness_percent, pwm_level);

                // ✅ Crea evento per lo scheduler
                ble_mesh_event_t mesh_event = {
                    .brightness = brightness_percent,  // 0-100%
                    .pwm_level = pwm_level,            // 0-32
                    .hue = 0,
                    .saturation = 0,
                    .is_override = true,
                    .timestamp = esp_timer_get_time()
                };

                // ✅ Invia evento allo scheduler
                esp_err_t sched_err = scheduler_put_event(&mesh_event, sizeof(mesh_event),
                                                         SCH_EVT_BLE_MESH_RX, handle_ble_mesh_event);

                if (sched_err == ESP_OK) {
                    ESP_LOGI(TAG, "📨 Evento messo in coda scheduler");
                } else {
                    ESP_LOGE(TAG, "❌ Errore mettendo evento in coda");
                }

                // ✅ Controllo LED immediato
                if (pwm_level > 0) {
                    board_led_operation(LED_R, LED_ON);
                } else {
                    board_led_operation(LED_R, LED_OFF);
                }

                // Log altri parametri
                ESP_LOGI(TAG, "color_temp=%d, rgb={%d,%d,%d}, dimStep=%d",
                         cfg->color_temp, cfg->rgb[0], cfg->rgb[1], cfg->rgb[2], cfg->dimStep);

            } else {
                ESP_LOGW(TAG, "Lunghezza errata: %d, atteso: %d",
                         param->model_operation.length, sizeof(configdata_t));
            }

            // Risposta BLE Mesh
            uint16_t tid = 0x01;
            esp_err_t err = esp_ble_mesh_server_model_send_msg(
                &vnd_models[0], param->model_operation.ctx,
                ESP_BLE_MESH_VND_MODEL_OP_STATUS, sizeof(tid), (uint8_t *)&tid);

            if (err != ESP_OK) {
                ESP_LOGE(TAG, "❌ Failed to send BLE Mesh response");
            }
        }
        break;

    case ESP_BLE_MESH_MODEL_SEND_COMP_EVT:
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
 * @brief Legge dati sensore reali dal sistema Ecolumiere
 */
static uint16_t example_ble_mesh_get_sensor_data(esp_ble_mesh_sensor_state_t *state, uint8_t *data)
{
    uint8_t mpid_len = 0, data_len = 0;
    uint32_t mpid = 0;

    if (state == NULL || data == NULL) {
        ESP_LOGE(TAG, "%s, Invalid parameter", __func__);
        return 0;
    }

    if (state->sensor_data.length == ESP_BLE_MESH_SENSOR_DATA_ZERO_LEN) {
        /* For zero-length sensor data, the length is 0x7F, and the format is Format B. */
        mpid = ESP_BLE_MESH_SENSOR_DATA_FORMAT_B_MPID(state->sensor_data.length, state->sensor_property_id);
        mpid_len = ESP_BLE_MESH_SENSOR_DATA_FORMAT_B_MPID_LEN;
        data_len = 0;
    } else {
        if (state->sensor_data.format == ESP_BLE_MESH_SENSOR_DATA_FORMAT_A) {
            mpid = ESP_BLE_MESH_SENSOR_DATA_FORMAT_A_MPID(state->sensor_data.length, state->sensor_property_id);
            mpid_len = ESP_BLE_MESH_SENSOR_DATA_FORMAT_A_MPID_LEN;
        } else {
            mpid = ESP_BLE_MESH_SENSOR_DATA_FORMAT_B_MPID(state->sensor_data.length, state->sensor_property_id);
            mpid_len = ESP_BLE_MESH_SENSOR_DATA_FORMAT_B_MPID_LEN;
        }
        /* Use "state->sensor_data.length + 1" because the length of sensor data is zero-based. */
        data_len = state->sensor_data.length + 1;
    }

    memcpy(data, &mpid, mpid_len);
    memcpy(data + mpid_len, state->sensor_data.raw_value->data, data_len);

    return (mpid_len + data_len);
}

/**
 * @brief Invia status sensori
 */
static void example_ble_mesh_send_sensor_status(esp_ble_mesh_sensor_server_cb_param_t *param)
{
    uint8_t *status = NULL;
    uint16_t buf_size = 0;
    uint16_t length = 0;
    uint32_t mpid = 0;
    esp_err_t err;
    int i;

    for (i = 0; i < ARRAY_SIZE(sensor_states); i++) {
        esp_ble_mesh_sensor_state_t *state = &sensor_states[i];
        if (state->sensor_data.length == ESP_BLE_MESH_SENSOR_DATA_ZERO_LEN) {
            buf_size += ESP_BLE_MESH_SENSOR_DATA_FORMAT_B_MPID_LEN;
        } else {
            /* Use "state->sensor_data.length + 1" because the length of sensor data is zero-based. */
            if (state->sensor_data.format == ESP_BLE_MESH_SENSOR_DATA_FORMAT_A) {
                buf_size += ESP_BLE_MESH_SENSOR_DATA_FORMAT_A_MPID_LEN + state->sensor_data.length + 1;
            } else {
                buf_size += ESP_BLE_MESH_SENSOR_DATA_FORMAT_B_MPID_LEN + state->sensor_data.length + 1;
            }
        }
    }

    status = (uint8_t *)calloc(1, buf_size);
    if (!status) {
        ESP_LOGE(TAG, "No memory for sensor status!");
        return;
    }

    if (param->value.get.sensor_data.op_en == false) {
        /* Mesh Model Spec:
         * If the message is sent as a response to the Sensor Get message, and if the
         * Property ID field of the incoming message is omitted, the Marshalled Sensor
         * Data field shall contain data for all device properties within a sensor.
         */
        for (i = 0; i < ARRAY_SIZE(sensor_states); i++) {
            length += example_ble_mesh_get_sensor_data(&sensor_states[i], status + length);
        }
        goto send;
    }

    /* Mesh Model Spec:
     * Otherwise, the Marshalled Sensor Data field shall contain data for the requested
     * device property only.
     */
    for (i = 0; i < ARRAY_SIZE(sensor_states); i++) {
        if (param->value.get.sensor_data.property_id == sensor_states[i].sensor_property_id) {
            length = example_ble_mesh_get_sensor_data(&sensor_states[i], status);
            goto send;
        }
    }

    /* Mesh Model Spec:
     * Or the Length shall represent the value of zero and the Raw Value field shall
     * contain only the Property ID if the requested device property is not recognized
     * by the Sensor Server.
     */
    mpid = ESP_BLE_MESH_SENSOR_DATA_FORMAT_B_MPID(ESP_BLE_MESH_SENSOR_DATA_ZERO_LEN,
            param->value.get.sensor_data.property_id);
    memcpy(status, &mpid, ESP_BLE_MESH_SENSOR_DATA_FORMAT_B_MPID_LEN);
    length = ESP_BLE_MESH_SENSOR_DATA_FORMAT_B_MPID_LEN;

send:
    ESP_LOG_BUFFER_HEX("Sensor Data", status, length);

    err = esp_ble_mesh_server_model_send_msg(param->model, &param->ctx,
            ESP_BLE_MESH_MODEL_OP_SENSOR_STATUS, length, status);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send Sensor Status");
    }
    free(status);
}

/**
 * @brief Callback server sensori
 */
static void example_ble_mesh_sensor_server_cb(esp_ble_mesh_sensor_server_cb_event_t event,
                                              esp_ble_mesh_sensor_server_cb_param_t *param)
{
    ESP_LOGD(TAG, "Sensor server, event %d, src 0x%04x, dst 0x%04x, model_id 0x%04x",
        event, param->ctx.addr, param->ctx.recv_dst, param->model->model_id);

    switch (event) {
    case ESP_BLE_MESH_SENSOR_SERVER_RECV_GET_MSG_EVT:
        switch (param->ctx.recv_op) {
        case ESP_BLE_MESH_MODEL_OP_SENSOR_GET:
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_SENSOR_GET");
            example_ble_mesh_send_sensor_status(param);
            break;
        default:
            ESP_LOGE(TAG, "Unknown Sensor Get opcode 0x%04" PRIx32, param->ctx.recv_op);
            return;
        }
        break;
    default:
        ESP_LOGE(TAG, "Unknown Sensor Server event %d", event);
        break;
    }
}

/**
 * @brief Converte valore lightness in livello PWM
 */
static uint8_t convert_lightness_to_pwm(uint16_t lightness)
{
    uint32_t pwm_level;

    if (lightness <= LIGHTNESS_MAX) {
        // App usa scala 0-100
        pwm_level = (lightness * LIGHT_MAX_LEVEL) / LIGHTNESS_MAX;
        ESP_LOGI(TAG, "🔧 SCALA 0-100: %u → %lu/%d", lightness, pwm_level, LIGHT_MAX_LEVEL);
    } else {
        // Valore superiore a 100 - imposta massimo
        pwm_level = LIGHT_MAX_LEVEL;
        ESP_LOGW(TAG, "⚠️ Lightness fuori range: %u, impostato a MAX (%d)",
                 lightness, LIGHT_MAX_LEVEL);
    }

    // Safety check (dovrebbe essere ridondante ma meglio prevenire)
    if (pwm_level > LIGHT_MAX_LEVEL) {
        pwm_level = LIGHT_MAX_LEVEL;
        ESP_LOGE(TAG, "❌ ERRORE: pwm_level > MAX, corretto a %d", LIGHT_MAX_LEVEL);
    }

    return (uint8_t)pwm_level;
}


/**
 * @brief Callback server illuminazione HSL
 */
static void example_ble_mesh_light_server_cb(esp_ble_mesh_lighting_server_cb_event_t event,
                                             esp_ble_mesh_lighting_server_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_LIGHTING_SERVER_STATE_CHANGE_EVT:
        if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_SET) {
            uint16_t hue = hsl_state.hue;
            uint16_t sat = hsl_state.saturation;
            uint16_t lightness = hsl_state.lightness;  // ✅ già 0-100

            ESP_LOGI(TAG, "🎨 HSL Received: H:%u S:%u L:%u", hue, sat, lightness);

            // ✅ Converti lightness 0-100 → PWM 0-32
            uint8_t pwm_level = convert_lightness_to_pwm(lightness);

            ESP_LOGI(TAG, "🎛️ BLE HSL → PWM: %u → %u/32", lightness, pwm_level);

            // ✅ Crea evento per lo scheduler
            ble_mesh_event_t mesh_event = {
                .brightness = lightness,      // 0-100 (lightness)
                .pwm_level = pwm_level,       // 0-32
                .hue = hue,                   // mantieni hue
                .saturation = sat,            // mantieni sat
                .is_override = true,
                .timestamp = esp_timer_get_time()
            };

            // ✅ Invia allo scheduler invece di chiamare direttamente
            esp_err_t sched_err = scheduler_put_event(&mesh_event, sizeof(mesh_event),
                                                     SCH_EVT_BLE_MESH_RX, handle_ble_mesh_event);

            if (sched_err == ESP_OK) {
                ESP_LOGI(TAG, "📨 HSL Event queued to scheduler");
            } else {
                ESP_LOGE(TAG, "❌ Failed to queue HSL event");
            }

            // ✅ LED immediato (puoi mantenerlo)
            if (pwm_level > 0) {
                board_led_operation(LED_R, LED_ON);
            } else {
                board_led_operation(LED_R, LED_OFF);
            }
        }
        break;

    case ESP_BLE_MESH_LIGHTING_SERVER_RECV_SET_MSG_EVT:
        if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_SET ||
            param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_SET_UNACK) {

            uint16_t hue = param->value.set.hsl.hue;
            uint16_t sat = param->value.set.hsl.saturation;
            uint16_t lightness = param->value.set.hsl.lightness;

            // Aggiorna stato HSL
            hsl_state.hue = hue;
            hsl_state.saturation = sat;
            hsl_state.lightness = lightness;

            // Aggiorna stati target (per transizioni)
            hsl_state.target_hue = hue;
            hsl_state.target_saturation = sat;
            hsl_state.target_lightness = lightness;

            ESP_LOGI(TAG, "HSL Set: H:%u S:%u L:%u", hue, sat, lightness);

            uint8_t pwm_level = convert_lightness_to_pwm(lightness);

            pwmcontroller_set_level(pwm_level);

            ESP_LOGI(TAG, "🎛️ BLE Set → PWM: %u → %u/32", lightness, pwm_level);

            // Aggiorna lapmadina fisica
            // board_set_led_hsl(hue, sat, lightness);

            // ✅ USA LA NUOVA FUNZIONE DI SINCRONIZZAZIONE
            sync_nodo_lampada_with_hsl(hue, sat, lightness);

            //CONTROLLO DEL LED, Aggiorna lampadina fisica
            if (pwm_level > 0) {
                ESP_LOGI(TAG, "💡 Comando BLE: ON - Accendo LED");
                board_led_operation(LED_R, LED_ON);
            } else {
                ESP_LOGI(TAG, "💡 Comando BLE: OFF - Spengo LED");
                board_led_operation(LED_R, LED_OFF);
            }
        }
        break;

    case ESP_BLE_MESH_LIGHTING_SERVER_RECV_GET_MSG_EVT:
        if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_GET) {

            uint16_t current_pwm_level = pwmcontroller_get_current_level();

            uint16_t current_lightness = (current_pwm_level * 100) / LIGHT_MAX_LEVEL;

            struct __attribute__((packed)) {
                uint16_t lightness;
                uint16_t hue;
                uint16_t saturation;
            } status = {
                current_lightness,
                hsl_state.hue,
                hsl_state.saturation
            };

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

/**
 * @brief Inizializzazione BLE Mesh Ecolumiere
 */
esp_err_t ble_mesh_ecolumiere_init(void)
{
    esp_err_t err;

    // ✅ VERIFICA CHE LO SCHEDULER SIA INIZIALIZZATO
    if (!scheduler_is_initialized()) {
        ESP_LOGE(TAG, "❌ Scheduler not initialized! Call scheduler_init() first");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "📡 Initializing BLE Mesh with global scheduler");

    // ✅ REGISTRA TUTTE LE CALLBACK BLE MESH
    esp_ble_mesh_register_prov_callback(example_ble_mesh_provisioning_cb);
    esp_ble_mesh_register_config_server_callback(example_ble_mesh_config_server_cb);
    esp_ble_mesh_register_sensor_server_callback(example_ble_mesh_sensor_server_cb);

    // HSL model
    esp_ble_mesh_register_lighting_server_callback(example_ble_mesh_light_server_cb);
    esp_ble_mesh_register_custom_model_callback(example_ble_mesh_custom_model_cb);

    // ✅ INIZIALIZZA STACK MESH
    err = esp_ble_mesh_init(&provision, &composition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to initialize mesh stack");
        return err;
    }

    // ✅ ABILITA PROVISIONING
    err = esp_ble_mesh_node_prov_enable((esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to enable mesh node");
        return err;
    }

    // ✅ LED INDICATORE
    board_led_operation(LED_G, LED_ON);
    ESP_LOGI(TAG, "✅ BLE Mesh Ecolumiere initialized with global scheduler");

    return ESP_OK;
}


/**
 * @brief Restituisce UUID dispositivo
 */
void ble_mesh_ecolumiere_get_dev_uuid(uint8_t *uuid)
{
    if (uuid) {
        memcpy(uuid, dev_uuid, ESP_BLE_MESH_OCTET16_LEN);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Sincronizza NodoLampada con stato HSL corrente
 */
void sync_nodo_lampada_with_hsl(uint16_t hue, uint16_t saturation, uint16_t lightness) {

    NodoLampada lampada_aggiornata;

    const NodoLampada *stato_corrente = slave_node_get_lampada_data();

    // Copia stato corrente o inizializza
    if (stato_corrente != NULL) {
        memcpy(&lampada_aggiornata, stato_corrente, sizeof(NodoLampada));
    } else {
        memset(&lampada_aggiornata, 0, sizeof(NodoLampada));
    }

    // ✅ CALCOLA INTENSITÀ IN SCALA 0-100
    uint16_t nuova_intensita = lightness;

    // ✅ VERIFICA SE C'È UN CAMBIAMENTO SIGNIFICATIVO
    bool intensita_cambiata = (lampada_aggiornata.intensita_luminosa != nuova_intensita);
    bool stato_cambiato = (lampada_aggiornata.stato != (lightness > 0));

    if (!intensita_cambiata && !stato_cambiato) {
        ESP_LOGD(TAG, "🔁 NodoLampada già sincronizzato - Intensità: %u/100", nuova_intensita);
        return;
    }

    // ✅ AGGIORNA DATI
    lampada_aggiornata.stato = (lightness > 0);
    lampada_aggiornata.intensita_luminosa = nuova_intensita;
    lampada_aggiornata.temperatura_colore = 50; // Default
    lampada_aggiornata.controllo_remoto = true;

    // ✅ GESTISCI TIMESTAMP
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000000); // secondi

    if (lightness > 0 && lampada_aggiornata.tempo_accensione == 0) {
        lampada_aggiornata.tempo_accensione = now;
        ESP_LOGI(TAG, "⏰ Nuova accensione registrata");
    } else if (lightness == 0 && lampada_aggiornata.tempo_accensione > 0) {
        lampada_aggiornata.tempo_spegnimento = now;
        ESP_LOGI(TAG, "⏰ Spegnimento registrato");
    }

    // ✅ SALVA STATO AGGIORNATO
    slave_node_update_lampada_data(&lampada_aggiornata);

    // ✅ REGISTRA EVENTO
    char event_desc[60];
    snprintf(event_desc, sizeof(event_desc), "HSL H:%u S:%u L:%u → Int:%u/100",
             hue, saturation, lightness, nuova_intensita);
    data_recorder_enqueue_lampada_event(EVENT_COMMAND_RECEIVED, event_desc);

    ESP_LOGI(TAG, "🔄 NodoLampada sincronizzato - HSL: %u → Intensità: %u/100, Stato: %s",
             lightness, nuova_intensita, lightness > 0 ? "ON" : "OFF");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////


