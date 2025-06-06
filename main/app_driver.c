/* Projeto base ESP RainMaker para dispositivos com botão físico e LED externo
   
   Este projeto serve como template para novos dispositivos ESP RainMaker.
   Implementa controle de relé/saída via botão físico (GPIO4) e LED indicador externo (GPIO5).
   Reset Wi-Fi é feito via BOOT (GPIO0) com um toque.
   
   Código em domínio público (Public Domain/CC0).
*/

#include <sdkconfig.h>
#include <iot_button.h>
#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_utils.h> // Para esp_rmaker_wifi_reset
#include "app_priv.h"
#include "app_driver.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include <esp_log.h>
#include "driver/ledc.h"
#define TAG "BATERIA"

// Flag global para controle do estado de provisioning
static bool is_provisioning_active = false;

void set_provisioning_state(bool active) {
    is_provisioning_active = active;
}

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_sleep.h"
#include "esp_sntp.h"
#include <time.h>
#include <inttypes.h>

static time_t boot_time = 0;

// --- Definições de hardware ---
#define BUTTON_RESET_GPIO 12 // Botão físico de reset Wi-Fi
#define BUTTON_LED_GPIO   13 // Botão liga/desliga LED externo
#define LED2_GPIO         5  // LED externo
#define BUZZER_GPIO       18 // PWM para buzzer
#define BATTERY_RELAY_GPIO 19 // Relé de medição
#define VBAT_ADC_CHANNEL  ADC1_CHANNEL_6 // GPIO34
#define VBAT_DIV_R1 44.5f
#define VBAT_DIV_R2 44.5f
#define VBAT_SLEEP_THRESHOLD 3.1f
#define VBAT_LIGHT_SLEEP_THRESHOLD 3.5f
#define VBAT_WAKE_THRESHOLD 3.7f
#define VBAT_CHECK_INTERVAL_SEC (60 * 60) // 1 hora

// --- Variáveis globais ---
static bool led2_state = false;

esp_rmaker_param_t *battery_voltage_param = NULL;
esp_rmaker_param_t *battery_status_param = NULL;

// Valor inicial seguro para evitar vbat=0.00V antes da primeira medição
static float ultima_tensao_bateria = 3.7f;

float ler_tensao_bateria(void) {
    gpio_set_level(BATTERY_RELAY_GPIO, 0); // Ativa relé
    vTaskDelay(pdMS_TO_TICKS(20));
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(VBAT_ADC_CHANNEL, ADC_ATTEN_DB_12);
    float soma = 0;
    const int n = 16;
    for (int i = 0; i < n; i++) {
        int leituraADC = adc1_get_raw(VBAT_ADC_CHANNEL);
        float vADC = leituraADC * 3.3f / 4095.0f;
        float vBat = vADC * ((VBAT_DIV_R1 + VBAT_DIV_R2) / VBAT_DIV_R2);
        // Calibração: ajuste para que a leitura "bata" com 4.057V reais
        vBat *= 1.15f; // Fator de calibração inicial (ajuste conforme necessário)
        ESP_LOGI(TAG, "ADC=%d vADC=%.3f vBat=%.3f", leituraADC, vADC, vBat);
        soma += vBat;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    float vbat = soma / n;
    ultima_tensao_bateria = vbat;
    gpio_set_level(BATTERY_RELAY_GPIO, 1); // Desativa relé
    return vbat;
}

void app_driver_set_led(bool on) {
    led2_state = on;
    gpio_set_level(LED2_GPIO, on ? 1 : 0);
}

// --- PWM para buzzer ---
void buzzer_beep(int freq, int duration_ms) {
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = freq,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer);
    ledc_channel_config_t channel = {
        .gpio_num = BUZZER_GPIO,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 512,
        .hpoint = 0
    };
    ledc_channel_config(&channel);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    ledc_stop(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, 0);
}

// --- Callback do botão liga/desliga LED ---
static void led_btn_cb(void *arg) {
    led2_state = !led2_state;
    app_driver_set_led(led2_state);
    app_driver_update_led_param(led2_state); // Notifica o app RainMaker
    buzzer_beep(2000, 100);
}

// --- Callback do botão de reset Wi-Fi ---
static void wifi_reset_btn_cb(void *arg) {
    ESP_LOGI(TAG, "Wi-Fi reset solicitado via botão GPIO12");
    buzzer_beep(1000, 200);
    esp_rmaker_wifi_reset(0, 2);
}

void app_driver_init() {
    // Botão liga/desliga LED (GPIO13)
    gpio_config_t btn_led_conf = {
        .pin_bit_mask = (1ULL << BUTTON_LED_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&btn_led_conf);
    button_handle_t btn_led = iot_button_create(BUTTON_LED_GPIO, 0);
    if (btn_led) {
        iot_button_set_evt_cb(btn_led, BUTTON_CB_TAP, led_btn_cb, NULL);
    }
    // Botão reset Wi-Fi (GPIO12)
    gpio_config_t btn_reset_conf = {
        .pin_bit_mask = (1ULL << BUTTON_RESET_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&btn_reset_conf);
    button_handle_t btn_reset = iot_button_create(BUTTON_RESET_GPIO, 0);
    if (btn_reset) {
        iot_button_set_evt_cb(btn_reset, BUTTON_CB_TAP, wifi_reset_btn_cb, NULL);
    }
    // LED externo
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << LED2_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&led_conf);
    gpio_set_level(LED2_GPIO, 0);
    // Relé de medição
    gpio_config_t relay_conf = {
        .pin_bit_mask = (1ULL << BATTERY_RELAY_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&relay_conf);
    gpio_set_level(BATTERY_RELAY_GPIO, 0);
    // Buzzer (PWM)
    gpio_config_t buzzer_conf = {
        .pin_bit_mask = (1ULL << BUZZER_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&buzzer_conf);
}

void app_driver_update_led_param(bool led_on)
{
    esp_rmaker_param_t *led_param = esp_rmaker_device_get_param_by_name(switch_device, "LED");
    if (led_param) {
        esp_rmaker_param_update_and_notify(led_param, esp_rmaker_bool(led_on));
    }
}

// --- Função chamada pelo botão físico para alterar o estado de relé/saída ---
int IRAM_ATTR app_driver_set_state(bool state)
{
    // Agora o botão virtual do RainMaker controla o LED externo (GPIO5)
    app_driver_set_led(state);
    app_driver_update_led_param(state);
    return ESP_OK;
}

bool app_driver_get_state(void)
{
    return led2_state;
}

// --- Monitoramento de bateria ---
void app_driver_update_battery_voltage(void) {
    float vbat = ler_tensao_bateria();
    if (battery_voltage_param) {
        float vbat_2d = ((int)(vbat * 100 + 0.5f)) / 100.0f;
        esp_rmaker_param_update(battery_voltage_param, esp_rmaker_float(vbat_2d));
    }
}

void battery_voltage_task(void *param) {
    extern SemaphoreHandle_t medir_mutex; // Usa o mesmo mutex global
    while (1) {
        if (medir_mutex && xSemaphoreTake(medir_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            gpio_set_level(BATTERY_RELAY_GPIO, 0); // Atraca (nível baixo)
            vTaskDelay(pdMS_TO_TICKS(20000));      // 20 segundos
            app_driver_update_battery_voltage();    // Mede a bateria
            gpio_set_level(BATTERY_RELAY_GPIO, 1); // Desatraca (nível alto)
            xSemaphoreGive(medir_mutex);
        } else {
            ESP_LOGW(TAG, "Medição automática pulada: medição manual em andamento.");
        }
        vTaskDelay(pdMS_TO_TICKS(1200000));    // 20 minutos
    }
}

// --- ATENÇÃO: O relé de medição (GPIO19) deve ser controlado SOMENTE pela battery_voltage_task.
// O LED externo (GPIO5) só deve acompanhar o relé principal, nunca o de medição.
// Não crie outras tasks ou funções que alterem o estado deste relé.

// --- Funções auxiliares de tempo ---
static void get_local_time(struct tm *info)
{
    time_t now = time(NULL);
    localtime_r(&now, info);
}

static bool is_light_sleep_time(void)
{
    struct tm info;
    get_local_time(&info);
    int hour = info.tm_hour;
    int min = info.tm_min;
    if (hour == 18 && min >= 30) return true;
    if (hour == 19) return true;
    return false;
}

static bool is_forced_hibernation_time(void)
{
    struct tm info;
    get_local_time(&info);
    int hour = info.tm_hour;
    // Hiberna das 3h às 9h
    if (hour >= 3 && hour < 9) return true;
    return false;
}

void check_battery_and_sleep(void) {
    float vbat = ultima_tensao_bateria;
    static char last_status[32] = "";
    const char *status_msg = "OK";
    
    // Não entrar em hibernação se estiver em processo de provisioning
    if (is_provisioning_active) {
        ESP_LOGI(TAG, "Ignorando verificação de bateria durante provisioning");
        return;
    }

    if (vbat < 3.20f) {
        status_msg = "Entrar em hibernação";
        // Hibernação imediata se <= 3.10V
        if (vbat <= 3.10f && !is_provisioning_active) {
            ESP_LOGW("BATERIA", "Entrando em hibernação (vbat=%.2fV)", vbat);
            if (battery_status_param) {
                esp_rmaker_param_update_and_notify(battery_status_param, esp_rmaker_str("Entrar em hibernação"));
            }
            vTaskDelay(pdMS_TO_TICKS(2000));
            ESP_LOGW("HIBERNACAO", "Entrando em deep sleep. Só acorda com RESET.");
            esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
            esp_deep_sleep_start();
            ESP_LOGE("HIBERNACAO", "ERRO: Código continuou após esp_deep_sleep_start()! Isto NÃO deveria acontecer.");
        }
    } else if (vbat < 3.30f) {
        status_msg = "Preparar hibernação";
        // Aviso sonoro alto no buzzer
        buzzer_beep(3000, 800); // 3kHz, 800ms
    } else if (vbat < 3.50f) {
        status_msg = "Redução de consumo";
    }
    if (battery_status_param && strcmp(last_status, status_msg) != 0) {
        esp_rmaker_param_update_and_notify(battery_status_param, esp_rmaker_str(status_msg));
        strncpy(last_status, status_msg, sizeof(last_status)-1);
        last_status[sizeof(last_status)-1] = '\0';
    }
    
    // --- NOVA LÓGICA: hiberna imediatamente ao entrar no horário de hibernação (3h-9h) ---
    // Mas não durante provisioning
    if (is_forced_hibernation_time() && !is_provisioning_active) {
        ESP_LOGW("HIBERNACAO", "Fora do horário ativo (3h-9h). Entrando em deep sleep. Só acorda com RESET.");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        esp_deep_sleep_start();
        ESP_LOGE("HIBERNACAO", "ERRO: Código continuou após esp_deep_sleep_start()! Isto NÃO deveria acontecer.");
    } else if (vbat <= 3.10f && !is_provisioning_active) {
        ESP_LOGW("BATERIA", "Entrando em hibernação (vbat=%.2fV)", vbat);
        if (battery_status_param) {
            esp_rmaker_param_update_and_notify(battery_status_param, esp_rmaker_str("Entrar em hibernação"));
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
        ESP_LOGW("HIBERNACAO", "Entrando em deep sleep. Só acorda com RESET.");
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        esp_deep_sleep_start();
        ESP_LOGE("HIBERNACAO", "ERRO: Código continuou após esp_deep_sleep_start()! Isto NÃO deveria acontecer.");
    } else if (vbat < 3.20f) {
        ESP_LOGW("BATERIA", "Preparar hibernação (vbat=%.2fV)", vbat);
        if (battery_status_param) {
            esp_rmaker_param_update_and_notify(battery_status_param, esp_rmaker_str("Preparar hibernação"));
        }
    } else if (vbat <= 3.50f && !is_provisioning_active) {
        ESP_LOGW("BATERIA", "Redução de consumo (vbat=%.2fV)", vbat);
        if (battery_status_param) {
            esp_rmaker_param_update_and_notify(battery_status_param, esp_rmaker_str("Redução de consumo"));
        }
        esp_light_sleep_start();
    } else if (is_light_sleep_time() && vbat > VBAT_LIGHT_SLEEP_THRESHOLD && !is_provisioning_active) {
        ESP_LOGW("BATERIA", "Entrando em light sleep (18:30-19:59, vbat=%.2fV)", vbat);
        esp_light_sleep_start();
    }
}

void check_battery_on_wakeup(void) {
    float vbat = ler_tensao_bateria();
    if (battery_status_param) {
        const char *status_msg = "OK";
        if (vbat < 3.20f) {
            status_msg = "Entrar em hibernação";
        } else if (vbat < 3.30f) {
            status_msg = "Preparar hibernação";
        } else if (vbat < 3.50f) {
            status_msg = "Redução de consumo";
        }
        esp_rmaker_param_update_and_notify(battery_status_param, esp_rmaker_str(status_msg));
    }
    if (is_forced_hibernation_time()) {
        ESP_LOGW("BATERIA", "(ignorado) Acordou em horário restrito. Voltaria à hibernação, mas hibernação está desabilitada.");
    } else if (vbat < VBAT_WAKE_THRESHOLD) {
        ESP_LOGW("BATERIA", "(ignorado) Acordou, mas tensão ainda baixa (%.2fV). Voltaria à hibernação, mas hibernação está desabilitada.", vbat);
        if (battery_status_param) {
            esp_rmaker_param_update_and_notify(battery_status_param, esp_rmaker_str("Entrar em hibernação"));
        }
    } else {
        ESP_LOGI("BATERIA", "Tensão recuperada (%.2fV) e horário permitido. Retomando operação normal.", vbat);
        if (battery_status_param) {
            esp_rmaker_param_update_and_notify(battery_status_param, esp_rmaker_str("OK"));
        }
    }
}

void app_driver_monitor_state(void)
{
    int led5 = gpio_get_level(LED2_GPIO);
    int relay19 = gpio_get_level(BATTERY_RELAY_GPIO);
    ESP_LOGI("MONITOR", "GPIO5 (LED): %d | GPIO19 (relé): %d", led5, relay19);
}
