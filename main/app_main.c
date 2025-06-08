/* Switch Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include <inttypes.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "freertos/queue.h"

#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_types.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_standard_devices.h>
#include <esp_rmaker_schedule.h>
#include <esp_rmaker_scenes.h>
#include <esp_rmaker_console.h>
#include <esp_rmaker_ota.h>
#include <esp_rmaker_utils.h>

#include <esp_rmaker_common_events.h>

#include <app_network.h>
#include <app_insights.h>
#include "driver/gpio.h"
#include "network_provisioning/manager.h"

#include "app_priv.h"
#include "app_driver.h"

// Declaração da função externa
extern void set_provisioning_state(bool active);

static const char *TAG = "app_main";
esp_rmaker_device_t *switch_device;
esp_rmaker_param_t *reset_param = NULL;

/* Mutex para proteger medição manual */
SemaphoreHandle_t medir_mutex = NULL;

/* Fila para pedidos de medição manual */
static QueueHandle_t medir_queue = NULL;

/* Callback to handle commands received from the RainMaker cloud */
static esp_err_t write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
            const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    static int64_t ultima_medir_ms = 0;
    const int64_t DEBOUNCE_MS = 10000; // 10 segundos
    if (ctx) {
        ESP_LOGI(TAG, "Received write request via : %s", esp_rmaker_device_cb_src_to_str(ctx->src));
    }
    const char *param_name = esp_rmaker_param_get_name(param);
    if (strcmp(param_name, "Relé") == 0) {
        // Parâmetro removido: não faz mais nada
    } else if (strcmp(param_name, "Medir Bateria") == 0) {
        ESP_LOGI(TAG, "Received value = %s para rotina de medição manual", val.val.b ? "true" : "false");
        esp_rmaker_param_val_t current = *esp_rmaker_param_get_val((esp_rmaker_param_t *)param);
        if (!current.val.b && val.val.b) {
            int64_t agora = esp_timer_get_time() / 1000;
            if (agora - ultima_medir_ms < DEBOUNCE_MS) {
                ESP_LOGW(TAG, "Medição manual ignorada: debounce ativo (%lld ms restantes)",
                    DEBOUNCE_MS - (agora - ultima_medir_ms));
                esp_rmaker_param_update_and_notify((esp_rmaker_param_t *)param, esp_rmaker_bool(false));
                return ESP_OK;
            }
            if (medir_queue) {
                // Sinaliza pedido de medição manual para a task
                int dummy = 1;
                xQueueSend(medir_queue, &dummy, 0);
                ultima_medir_ms = agora;
            }
            esp_rmaker_param_update_and_notify((esp_rmaker_param_t *)param, esp_rmaker_bool(false));
        } else {
            esp_rmaker_param_update_and_notify((esp_rmaker_param_t *)param, esp_rmaker_bool(false));
        }
    } else if (strcmp(param_name, "LED") == 0) {
        ESP_LOGI(TAG, "Received value = %s for %s - %s (LED)",
                val.val.b? "true" : "false", esp_rmaker_device_get_name(device),
                param_name);
        app_driver_set_led(val.val.b);
        app_driver_update_led_param(val.val.b);
        esp_rmaker_param_update_and_notify(param, val);
    } else if (strcmp(param_name, "Reset ESP32") == 0) {
        ESP_LOGW(TAG, "Comando de reset remoto recebido pelo app!");
        if (val.val.b) {
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_rmaker_param_update_and_notify(reset_param, esp_rmaker_bool(false));
            esp_restart();
        }
    } else {
        ESP_LOGW(TAG, "Unhandled parameter: %s", param_name);
    }
    return ESP_OK;
}

/* Event handler for catching RainMaker events */
static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    ESP_LOGW(TAG, "[DEBUG] event_handler: event_base=%p, event_id=%ld", event_base, (long)event_id);
    if (event_base == RMAKER_EVENT) {
        switch (event_id) {
            case RMAKER_EVENT_INIT_DONE:
                ESP_LOGI(TAG, "RainMaker Initialised.");
                break;
            case RMAKER_EVENT_CLAIM_STARTED:
                ESP_LOGI(TAG, "RainMaker Claim Started.");
                break;
            case RMAKER_EVENT_CLAIM_SUCCESSFUL:
                ESP_LOGI(TAG, "RainMaker Claim Successful.");
                break;
            case RMAKER_EVENT_CLAIM_FAILED:
                ESP_LOGI(TAG, "RainMaker Claim Failed.");
                break;
            case RMAKER_EVENT_LOCAL_CTRL_STARTED:
                ESP_LOGI(TAG, "Local Control Started.");
                break;
            case RMAKER_EVENT_LOCAL_CTRL_STOPPED:
                ESP_LOGI(TAG, "Local Control Stopped.");
                break;
            default:
                ESP_LOGW(TAG, "Unhandled RainMaker Event: %"PRIi32, event_id);
        }
    } else if (event_base == RMAKER_COMMON_EVENT) {
        ESP_LOGW(TAG, "[DEBUG] RMAKER_COMMON_EVENT: event_id=%ld", (long)event_id);
        switch (event_id) {            case RMAKER_EVENT_REBOOT:
                ESP_LOGI(TAG, "Rebooting in %d seconds.", *((uint8_t *)event_data));
                break;
            case RMAKER_EVENT_WIFI_RESET:
                ESP_LOGW(TAG, "[DEBUG] >>> Wi-Fi credentials reset event received! <<<");
                break;
            case RMAKER_EVENT_FACTORY_RESET:
                ESP_LOGW(TAG, "[DEBUG] >>> Node reset to factory defaults event received! <<<");
                break;
            case RMAKER_MQTT_EVENT_CONNECTED:
                ESP_LOGI(TAG, "MQTT Connected.");
                set_provisioning_state(false); // Desativa flag de provisioning após conexão bem sucedida
                break;
            case RMAKER_MQTT_EVENT_DISCONNECTED:
                ESP_LOGI(TAG, "MQTT Disconnected.");
                break;
            case RMAKER_MQTT_EVENT_PUBLISHED:
                ESP_LOGI(TAG, "MQTT Published. Msg id: %d.", *((int *)event_data));
                break;
            default:
                ESP_LOGW(TAG, "Unhandled RainMaker Common Event: %"PRIi32, event_id);
        }    } else if (event_base == APP_NETWORK_EVENT) {
        switch (event_id) {
            case APP_NETWORK_EVENT_QR_DISPLAY:
                ESP_LOGI(TAG, "Provisioning QR : %s", (char *)event_data);
                set_provisioning_state(true); // Ativa flag de provisioning
                break;
            case APP_NETWORK_EVENT_PROV_TIMEOUT:
                ESP_LOGI(TAG, "Provisioning Timed Out. Please reboot.");
                set_provisioning_state(false); // Desativa flag de provisioning
                break;
            case APP_NETWORK_EVENT_PROV_RESTART:
                ESP_LOGI(TAG, "Provisioning has restarted due to failures.");
                set_provisioning_state(true); // Reativa flag de provisioning
                break;
            default:
                ESP_LOGW(TAG, "Unhandled App Wi-Fi Event: %"PRIi32, event_id);
                break;
        }
    } else if (event_base == RMAKER_OTA_EVENT) {
        switch(event_id) {
            case RMAKER_OTA_EVENT_STARTING:
                ESP_LOGI(TAG, "Starting OTA.");
                break;
            case RMAKER_OTA_EVENT_IN_PROGRESS:
                ESP_LOGI(TAG, "OTA is in progress.");
                break;
            case RMAKER_OTA_EVENT_SUCCESSFUL:
                ESP_LOGI(TAG, "OTA successful.");
                break;
            case RMAKER_OTA_EVENT_FAILED:
                ESP_LOGI(TAG, "OTA Failed.");
                break;
            case RMAKER_OTA_EVENT_REJECTED:
                ESP_LOGI(TAG, "OTA Rejected.");
                break;
            case RMAKER_OTA_EVENT_DELAYED:
                ESP_LOGI(TAG, "OTA Delayed.");
                break;
            case RMAKER_OTA_EVENT_REQ_FOR_REBOOT:
                ESP_LOGI(TAG, "Firmware image downloaded. Please reboot your device to apply the upgrade.");
                break;
            default:
                ESP_LOGW(TAG, "Unhandled OTA Event: %"PRIi32, event_id);
                break;
        }
    } else {
        ESP_LOGW(TAG, "Invalid event received!");
    }
}

void battery_management_task(void *param) {
    while (1) {
        check_battery_and_sleep();
        vTaskDelay(pdMS_TO_TICKS(30000)); // Checa a cada 30s
    }
}

void monitor_task(void *param)
{
    while (1) {
        app_driver_monitor_state();
        vTaskDelay(pdMS_TO_TICKS(10000)); // Log a cada 10 segundos
    }
}

// Protótipos para evitar erro de declaração implícita
void app_driver_set_led(bool on);
void app_driver_update_led_param(bool led_on);
void medir_bateria_manual_task(void *param);

void app_main()
{
    medir_mutex = xSemaphoreCreateMutex();
    medir_queue = xQueueCreate(2, sizeof(int));
    /* Força reset de provisionamento para debug BLE sempre ativo */
    network_prov_mgr_reset_wifi_provisioning();

    /* Initialize Application specific hardware drivers and
     * set initial state.
     */
    esp_rmaker_console_init();
    app_driver_init();
    app_driver_set_state(DEFAULT_POWER); // Pode ser removido se não usar relé, mas mantém para compatibilidade

    // Checa bateria ao acordar da hibernação, apenas se já provisionado
    bool provisioned = false;
    if (network_prov_mgr_is_wifi_provisioned(&provisioned) == ESP_OK && provisioned) {
        check_battery_on_wakeup();
    }

    /* Initialize NVS. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );

    /* Initialize Wi-Fi. Note that, this should be called before esp_rmaker_node_init()
     */
    app_network_init();

    /* Register an event handler to catch RainMaker events */
    ESP_ERROR_CHECK(esp_event_handler_register(RMAKER_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(RMAKER_COMMON_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(APP_NETWORK_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(RMAKER_OTA_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

    /* Initialize the ESP RainMaker Agent.
     * Note that this should be called after app_network_init() but before app_nenetworkk_start()
     * */
    esp_rmaker_config_t rainmaker_cfg = {
        .enable_time_sync = false,
    };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&rainmaker_cfg, "ESP RainMaker Device", "Switch");
    if (!node) {
        ESP_LOGE(TAG, "Could not initialise node. Aborting!!!");
        vTaskDelay(5000/portTICK_PERIOD_MS);
        abort();
    }

    /* Create a Switch device. */
    switch_device = esp_rmaker_device_create("Switch", ESP_RMAKER_DEVICE_SWITCH, NULL);
    esp_rmaker_device_add_cb(switch_device, write_cb, NULL);

    /* Add the standard name parameter (type: esp.param.name), which allows setting a persistent,
     * user friendly custom name from the phone apps. All devices are recommended to have this
     * parameter.
     */
    esp_rmaker_device_add_param(switch_device, esp_rmaker_name_param_create(ESP_RMAKER_DEF_NAME_PARAM, "Switch"));

    /* Add the relé parameter (was: power) */
    // esp_rmaker_param_t *rele_param = esp_rmaker_param_create("Relé", ESP_RMAKER_DEF_POWER_NAME, esp_rmaker_bool(DEFAULT_POWER), PROP_FLAG_READ | PROP_FLAG_WRITE);
    // esp_rmaker_param_add_ui_type(rele_param, "esp.ui.toggle");
    // esp_rmaker_device_add_param(switch_device, rele_param);

    // Adiciona parâmetro de controle do LED externo
    esp_rmaker_param_t *led_param = esp_rmaker_param_create("LED", NULL, esp_rmaker_bool(false), PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_param_add_ui_type(led_param, "esp.ui.toggle");
    esp_rmaker_device_add_param(switch_device, led_param);

    // Parâmetro para ativar rotina de medição manualmente
    esp_rmaker_param_t *medir_param = esp_rmaker_param_create("Medir Bateria", NULL, esp_rmaker_bool(false), PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_param_add_ui_type(medir_param, "esp.ui.toggle");
    esp_rmaker_device_add_param(switch_device, medir_param);

    // Adiciona parâmetro de tensão da bateria (float customizado)
    battery_voltage_param = esp_rmaker_param_create("Baterias 18650", "esp.param.voltage", esp_rmaker_float(0), PROP_FLAG_READ);
    esp_rmaker_param_add_ui_type(battery_voltage_param, "esp.ui.text");
    esp_rmaker_param_add_bounds(battery_voltage_param, esp_rmaker_float(0), esp_rmaker_float(20), esp_rmaker_float(0.01));
    esp_rmaker_device_add_param(switch_device, battery_voltage_param);

    // Adiciona parâmetro de status da bateria (string read-only)
    battery_status_param = esp_rmaker_param_create("Status Bateria", "esp.param.battery_status", esp_rmaker_str("OK"), PROP_FLAG_READ);
    esp_rmaker_param_add_ui_type(battery_status_param, "esp.ui.text");
    esp_rmaker_device_add_param(switch_device, battery_status_param);

    // Adiciona parâmetro de reset da placa
    reset_param = esp_rmaker_param_create("Reset ESP32", NULL, esp_rmaker_bool(false), PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_param_add_ui_type(reset_param, "esp.ui.toggle");
    esp_rmaker_device_add_param(switch_device, reset_param);

    // Adiciona parâmetro de percentagem da bateria (inteiro, 0-100)
    battery_percent_param = esp_rmaker_param_create("Bateria (%)", NULL, esp_rmaker_int(100), PROP_FLAG_READ);
    esp_rmaker_param_add_ui_type(battery_percent_param, "esp.ui.text");
    esp_rmaker_param_add_bounds(battery_percent_param, esp_rmaker_int(0), esp_rmaker_int(100), esp_rmaker_int(1));
    esp_rmaker_device_add_param(switch_device, battery_percent_param);
    // Torna global para uso em app_driver.c
    extern esp_rmaker_param_t *battery_percent_param;
    battery_percent_param = battery_percent_param;

    /* Assign the relé parameter as the primary, so that it can be controlled from the
     * home screen of the phone apps.
     */
    // esp_rmaker_device_assign_primary_param(switch_device, rele_param);

    /* Add this switch device to the node */
    esp_rmaker_node_add_device(node, switch_device);

    /* Enable OTA */
    esp_rmaker_ota_enable_default();

    /* Enable timezone service which will be require for setting appropriate timezone
     * from the phone apps for scheduling to work correctly.
     * For more information on the various ways of setting timezone, please check
     * https://rainmaker.espressif.com/docs/time-service.html.
     */
    esp_rmaker_timezone_service_enable();

    /* Enable scheduling. */
    esp_rmaker_schedule_enable();

    /* Enable Scenes */
    esp_rmaker_scenes_enable();

    /* Enable Insights. Requires CONFIG_ESP_INSIGHTS_ENABLED=y */
    app_insights_enable();

    /* Start the ESP RainMaker Agent */
    esp_rmaker_start();

    // Cria uma task para atualizar a tensão periodicamente
    xTaskCreate(battery_voltage_task, "battery_voltage_task", 4096, NULL, 5, NULL);
    xTaskCreate(battery_management_task, "battery_management_task", 4096, NULL, 5, NULL);
    xTaskCreate(monitor_task, "monitor_task", 2048, NULL, 1, NULL);
    xTaskCreate(medir_bateria_manual_task, "medir_bateria_manual_task", 4096, NULL, 5, NULL);

    err = app_network_set_custom_mfg_data(MGF_DATA_DEVICE_TYPE_SWITCH, MFG_DATA_DEVICE_SUBTYPE_SWITCH);
    /* Start the Wi-Fi.
     * If the node is provisioned, it will start connection attempts,
     * else, it will start Wi-Fi provisioning. The function will return
     * after a connection has been successfully established
     */
    // Força o uso de PoP fixo "abcd1234" e SECURITY_1 para máxima compatibilidade
    app_network_set_custom_pop("abcd1234");
    err = app_network_start(POP_TYPE_CUSTOM);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not start Wifi. Aborting!!!");
        vTaskDelay(5000/portTICK_PERIOD_MS);
        abort();
    }

    // Antes de iniciar o loop principal, só executa lógica de sleep se já provisionado
    provisioned = false;
    if (network_prov_mgr_is_wifi_provisioned(&provisioned) == ESP_OK && provisioned) {
        ESP_LOGI(TAG, "Aguardando sincronização de horário via SNTP...");
        esp_err_t time_sync_result = esp_rmaker_time_wait_for_sync(pdMS_TO_TICKS(30000)); // espera até 30s
        if (time_sync_result != ESP_OK) {
            ESP_LOGW(TAG, "Horário não sincronizado após 30s. Continuando mesmo assim.");
        } else {
            ESP_LOGI(TAG, "Horário sincronizado com sucesso.");
        }
        // Loga o horário local atual
        char local_time[64];
        if (esp_rmaker_get_local_time_str(local_time, sizeof(local_time)) == ESP_OK) {
            ESP_LOGI(TAG, "Horário local antes da decisão de sleep: %s", local_time);
        }
        check_battery_and_sleep();
    } else {
        ESP_LOGI(TAG, "Dispositivo não provisionado. Lógica de sleep desabilitada até concluir o provisionamento.");
    }

    /* Força timezone para Europe/Lisbon (Portugal, GMT+0/1) */
    esp_rmaker_time_set_timezone("Europe/Lisbon");
}

void medir_bateria_manual_task(void *param) {
    while (1) {
        int dummy;
        if (xQueueReceive(medir_queue, &dummy, portMAX_DELAY) == pdTRUE) {
            if (medir_mutex && xSemaphoreTake(medir_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                gpio_set_level(BATTERY_RELAY_GPIO, 0);
                vTaskDelay(pdMS_TO_TICKS(5000));
                app_driver_update_battery_voltage();
                gpio_set_level(BATTERY_RELAY_GPIO, 1);
                xSemaphoreGive(medir_mutex);
            } else {
                ESP_LOGW(TAG, "Medição manual ignorada: mutex ocupado.");
            }
        }
    }
}
