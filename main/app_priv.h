/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define DEFAULT_POWER  false
#define LED2_GPIO 5 // GPIO5 para o LED exterior
#define BATTERY_RELAY_GPIO 19 // GPIO19 para controle do relé de medição
extern esp_rmaker_device_t *switch_device;
extern esp_rmaker_param_t *battery_voltage_param;
extern esp_rmaker_param_t *battery_status_param;
void app_driver_init(void);
int app_driver_set_state(bool state);
bool app_driver_get_state(void);
void app_driver_update_battery_voltage(void);
void check_battery_and_sleep(void);
void check_battery_on_wakeup(void);
void battery_voltage_task(void *param);
void app_driver_monitor_state(void);
