#include <stdio.h>
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gatts_api.h"
#include "esp_gap_ble_api.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_sleep.h"
#include "esp_sntp.h"
#include "esp_netif_sntp.h"
#include "mqtt_client.h"
#include "esp_timer.h"
#include "esp_system.h"

#define MAX_RETRY_RECONNECT 5
#define conn 0x01

esp_err_t error;
nvs_handle_t nvs;
char ssid[32], password[64];
uint8_t service_uuid[16] = {0x7D, 0xA2, 0xE9, 0x41, 0x6B, 0xF3, 0x48, 0xC7, 0xA5, 0x1D, 0x92, 0xBE, 0x30, 0x67, 0xF8, 0x14};
uint8_t characteristic_uuid[16] = {0xC2, 0x6F, 0x85, 0xD3, 0x19, 0xA7, 0x4E, 0xB0, 0x93, 0x5C, 0xE1, 0x72, 0x4A, 0x08, 0xDB, 0x65};
uint16_t srv_hndl, chr_hndl;
int retry_count, mqtt_connected, msg_id;
static EventGroupHandle_t wifi_event, mqtt_event;

esp_ble_adv_data_t data =
{
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .service_uuid_len = sizeof(service_uuid),
    .p_service_uuid = service_uuid,
    .flag = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT
};
esp_ble_adv_params_t specs =
{
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY
};

int store_into_nvs(char *key, char *value)
{
    error = nvs_open("nvs", NVS_READWRITE, &nvs);
    if (error != ESP_OK)
    {
        printf("some opening error : %s\n", esp_err_to_name(error));
        return 1;
    }  

    error = nvs_set_str(nvs, key, value);
    if (error != ESP_OK)
    {
        printf("some %s storing error : %s\n", key, esp_err_to_name(error));
        return 1;
    }

    nvs_commit(nvs);
    nvs_close(nvs);
    return 0;
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)  // handler for gap (advertising) server
{
    switch (event)
    {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        {
            printf("advertising data set, getting advertised\n");
            esp_ble_gap_start_advertising(&specs);
            break;
        }

        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        {
            printf("advertising started, Entered ble mode\n");
            break;
        }

        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        {
            printf("advertising stopped\n");
            break;
        }

        case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        {
            printf("updating connection parameters\n");
            break;
        }

        case ESP_GAP_BLE_SET_PKT_LENGTH_COMPLETE_EVT:
        {
            printf("setting packet length completed\n");
            break;
        }

        case ESP_GAP_BLE_LOCAL_IR_EVT:
        {
            printf("ble local ir event\n");
            break;
        }

        case ESP_GAP_BLE_LOCAL_ER_EVT:
        {
            printf("ble local er event\n");
            break;
        }

        default:
        {
            printf("miscelleneous gap events happening\nevent : %d\n", event);
            break;
        }
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)  // handler for gatt (connection and transmission) server
{
    switch (event)
    {
        case ESP_GATTS_REG_EVT:
        {
            printf("gatt server initialized\n");
            esp_bt_uuid_t srv_uuid =
            {
                .len = ESP_UUID_LEN_128
            };
            memcpy(srv_uuid.uuid.uuid128, service_uuid, srv_uuid.len);

            esp_gatt_srvc_id_t srv_id =
            {
                .id =
                {
                    .uuid = srv_uuid,
                    .inst_id = 0
                },
                .is_primary = true
            };

            printf("gatt interface registered: %d\n", gatts_if);
            esp_ble_gatts_create_service(gatts_if, &srv_id, 5);
            break;
        }

        case ESP_GATTS_CREATE_EVT:
        {
            printf("service created with handle\n");
            srv_hndl = param->create.service_handle;
            esp_ble_gatts_start_service(srv_hndl);

            esp_bt_uuid_t chr_uuid =
            {
                .len = ESP_UUID_LEN_128
            };
            memcpy(chr_uuid.uuid.uuid128, characteristic_uuid, chr_uuid.len);
            esp_ble_gatts_add_char(srv_hndl, &chr_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, 
                ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE, NULL, NULL);  // permittion read and write permissions
            break;
        }

        case ESP_GATTS_START_EVT:
        {
            printf("gatt server started\n");
            break;
        }

        case ESP_GATTS_ADD_CHAR_EVT:
        {
            printf("characteristics added to handle\n");
            chr_hndl = param->add_char.attr_handle;
            break;
        }

        case ESP_GATTS_CONNECT_EVT:
        {
            printf("device connected\n");
            esp_bd_addr_t remote_device;
            memcpy(remote_device, param->connect.remote_bda, ESP_BD_ADDR_LEN);
            esp_ble_gap_stop_advertising();
            break;
        }

        case ESP_GATTS_DISCONNECT_EVT:
        {
            printf("device disconnected\n");
            esp_restart();  // to restart the device to work with stored credentials
            break;
        }

        case ESP_GATTS_MTU_EVT:
        {
            printf("mtu configuration request from the client is received\n");
            break;
        }

        case ESP_GATTS_WRITE_EVT:  // receive the credentials through ble gatt server
        {
            printf("write request received\n");
            if (param->write.handle == chr_hndl)
            {
                char wifi_cred[96] = {0};
                memcpy(wifi_cred, param->write.value, param->write.len);
                char *id = strtok(wifi_cred, ",");
                char *pw = strtok(NULL, ",");

                if (store_into_nvs("ssid", id))  // store the received credentials in nvs storage
                {
                    printf("ssid storing failed\n");
                    return;
                }
                if (store_into_nvs("password", pw))
                {
                    printf("password storing failed\n");
                    return;
                }
            }
            else
            {
                printf("error in parsing data\n");
            }

            if (param->write.need_rsp)
            {
                esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);  // for acknowledging purpose
            }
            else
            {
                esp_ble_gatts_close(gatts_if, param->write.conn_id);
            }

            break;
        }

        default:
        {
            printf("miscelleneous gatt events happening\nevent : %d\n", event);
            break;
        }
    }
}

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)  // handler for wifi, ip events
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
            case WIFI_EVENT_STA_START:
                printf("starting wifi connection\n");
                esp_wifi_connect();
                break;

            case WIFI_EVENT_STA_DISCONNECTED:
                xEventGroupClearBits(wifi_event, conn);
                if ((retry_count != 0) && !(retry_count % MAX_RETRY_RECONNECT))
                {
                    printf("reconnecting...\n");
                    esp_wifi_disconnect(); 
                    esp_wifi_connect();   
                }
                else
                {
                    printf("disconnected\nretrying...\n");
                    esp_wifi_connect();
                    retry_count++;
                }
                break;

            case WIFI_EVENT_STA_CONNECTED:
                printf("connected to wifi station\n");
                break;

            default:
                break;
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        printf("connected to the network\n");
        printf("ip address: " IPSTR "\n", IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event, conn);
        retry_count = 0;
    }
}

void wifi_connect(char *sd, char *pw)
{
    wifi_event = xEventGroupCreate();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL);  
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL);

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&init_config);  // wifi initialization
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, sd, strlen(sd) + 1);  // impose credentials for the network connection
    strncpy((char *)wifi_config.sta.password, pw, strlen(pw) + 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (esp_wifi_start() != ESP_OK)  // start the wifi procedure to connect to ap
    {
        printf("issue in starting wifi\n");
        return;
    } 
    xEventGroupWaitBits(wifi_event, conn, pdFALSE, pdFALSE, portMAX_DELAY);  // wait until ip address is recieved
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)  // vent handler for mqtt events
{
    printf("event dispatched from event loop base = %s, event_id = %" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) 
    {
        case MQTT_EVENT_CONNECTED:
            printf("mqtt connected\n");
            xEventGroupSetBits(mqtt_event, conn);
            break;

        case MQTT_EVENT_DISCONNECTED:
            printf("mqtt disconnected\n");
            xEventGroupClearBits(mqtt_event, conn);
            break;

        case MQTT_EVENT_PUBLISHED:
            printf("mqtt published\n, msg_id=%d\n", event->msg_id);
            break;

        case MQTT_EVENT_DATA:
            printf("event data\n");
            printf("topic = %.*s\r\n", event->topic_len, event->topic);
            printf("data = %.*s\r\n", event->data_len, event->data);
            break;

        default:
            printf("other event id : %d\n", event->event_id);
            break;
    }
}

void heartbeat_msg(void *arg)
{
    mqtt_event = xEventGroupCreate();  // creating an event to wait for complete initialization
    esp_mqtt_client_config_t mqtt_cfg = 
    {
        .broker.address.uri = "mqtt://test.mosquitto.org:1883",  // public broker url to connect
    };
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    error = esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (error != ESP_OK)
    {
        printf("failed to register mqtt event handler\n");
        esp_mqtt_client_destroy(client);
        vTaskDelete(NULL);
        return;
    }
    error = esp_mqtt_client_start(client);  // connect to the broker
    if (error != ESP_OK)
    {
        printf("failed to start mqtt event handler\n");
        esp_mqtt_client_destroy(client);
        vTaskDelete(NULL);
        return;
    }

    TickType_t last = xTaskGetTickCount();  // get the latest tick count to delay the task for ticks equal to specified time interva;
    while (true)
    {
        if (xEventGroupGetBits(mqtt_event) & conn)  // only if the bits are obtained from the event, publishing is allowed
        {
            char payload[128];
            int len = snprintf(payload, sizeof(payload), "{\"uptime\":%lld,\"status\":\"alive\"}", esp_timer_get_time() / 1000000);
            msg_id = esp_mqtt_client_publish(client, "/heartbeat/data", payload, len, 1, 0);  // publish to the specified topic
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(5000));  // used to delay a task for absolute time specified waiting for exact time period
    }
    vTaskDelete(NULL);
}

void telemetry_task()
{
    xTaskCreate(heartbeat_msg, "periodic heartbeat message", 5 * 1024, NULL, 3, NULL);  // heartbeat message function to send for every 5 seconds

    setenv("TZ", "IST-5:30", 1);  // setting the timezone
    tzset();

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) 
    {
        printf("sntp initialization failed\n");
    }
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(5000)) != ESP_OK) 
    {
        printf("sntp synchronization timed out\n");
    }

    time_t now;
    char timebuff[64];
    struct tm info;
    time(&now);
    localtime_r(&now, &info);
    strftime(timebuff, sizeof(timebuff), "%c", &info);
    printf("the current date/time is : %s\n", timebuff);  // displaying the obtained date/time
}

void app_main(void)
{
    error = nvs_flash_init();  // initialize nvs flash memory
    if (error != ESP_OK)
    {
        printf("some initialization error : %s\n", esp_err_to_name(error));
        return;
    }

    error = nvs_open("nvs", NVS_READWRITE, &nvs);
    if (error != ESP_OK)
    {
        printf("some opening error : %s\n", esp_err_to_name(error));
        return;
    }

    size_t size = sizeof(ssid);
    error = nvs_get_str(nvs, "ssid", ssid, &size);  // retrieve the credentials
    if (error != ESP_OK)
    {
        printf("some ssid extracting error : %s\n", esp_err_to_name(error));
    }
    size = sizeof(password);
    error = nvs_get_str(nvs, "password", password, &size);
    if (error != ESP_OK)
    {
        printf("some password extracting error : %s\n", esp_err_to_name(error));
    }

    nvs_close(nvs);

    if (*ssid && *password)  // if credentials exist
    {
        esp_timer_init();
        wifi_connect(ssid, password);  // connects to wifi and waits until you get a reliable internet connection
        telemetry_task();  // containing heartbeat and time setting functions
    }
    else
    {
        esp_bt_controller_config_t bt_config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        esp_bt_controller_init(&bt_config);
        esp_bt_controller_enable(ESP_BT_MODE_BLE);
        esp_bluedroid_init();
        esp_bluedroid_enable();
        esp_ble_gap_set_device_name("Telemetry Device");
        esp_ble_gatts_register_callback(gatts_event_handler);
        esp_ble_gap_register_callback(gap_event_handler);
        esp_ble_gatts_app_register(0);
        esp_ble_gap_config_adv_data(&data);  // initialize the ble and start advertising
    }
}