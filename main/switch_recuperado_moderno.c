// Projeto ESP RainMaker customizado - versão moderna e estável
// GPIO34: leitura de tensão de bateria (ADC)
// GPIO12: botão físico de reset Wi-Fi
// GPIO13: botão liga/desliga LED externo (GPIO5)
// GPIO18: PWM para buzzer
// GPIO19: relé de medição de bateria

#include <sdkconfig.h>
#include <iot_button.h>
#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_utils.h>
#include "app_priv.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include <esp_log.h>
#include "driver/ledc.h" // Para PWM do buzzer
#include <freertos/queue.h>
#include <freertos/task.h>
#include <esp_sleep.h>
#include <esp_sntp.h>
#include <time.h>
#include <inttypes.h>

#define TAG "PROJ_SWITCH"

// --- Definições de hardware ---
#define BUTTON_RESET_GPIO 12 // Botão físico de reset Wi-Fi
#define BUTTON_LED_GPIO   13 // Botão liga/desliga LED externo
#define LED2_GPIO         5  // LED externo
#define BUZZER_GPIO       18 // PWM para buzzer
#define BATTERY_RELAY_GPIO 19 // Relé de medição
#define VBAT_ADC_CHANNEL  ADC1_CHANNEL_6 // GPIO34

static bool led2_state = false;
static bool is_provisioning_active = false;

void set_provisioning_state(bool active) { is_provisioning_active = active; }

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

// --- Funções de controle do LED externo ---
void app_driver_set_led(bool on) {
    led2_state = on;
    gpio_set_level(LED2_GPIO, on ? 1 : 0);
}

// --- Callback do botão liga/desliga LED ---
static void led_btn_cb(void *arg) {
    led2_state = !led2_state;
    app_driver_set_led(led2_state);
    // Opcional: beep ao alternar
    buzzer_beep(2000, 100);
}

// --- Callback do botão de reset Wi-Fi ---
static void wifi_reset_btn_cb(void *arg) {
    ESP_LOGI(TAG, "Wi-Fi reset solicitado via botão GPIO12");
    buzzer_beep(1000, 200);
    esp_rmaker_wifi_reset(0, 2);
}

// --- Inicialização dos GPIOs e botões ---
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

// --- Leitura de tensão da bateria (GPIO34) ---
#define VBAT_DIV_R1 68.0f
#define VBAT_DIV_R2 33.0f
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
        vBat *= 0.715f;
        soma += vBat;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    float vbat = soma / n;
    ultima_tensao_bateria = vbat;
    gpio_set_level(BATTERY_RELAY_GPIO, 1); // Desativa relé
    return vbat;
}

// --- Task de monitoramento de bateria ---
void battery_voltage_task(void *param) {
    while (1) {
        float vbat = ler_tensao_bateria();
        ESP_LOGI(TAG, "Tensão da bateria: %.2fV", vbat);
        vTaskDelay(pdMS_TO_TICKS(60000)); // Mede a cada 60s
    }
}

// --- Função principal ---
void app_main(void) {
    app_driver_init();
    xTaskCreate(battery_voltage_task, "battery_voltage_task", 2048, NULL, 5, NULL);
    // ...inicialização RainMaker, Wi-Fi, etc...
}
