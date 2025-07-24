/*----------------------------------------------------------
 * File: main/app_main.c
 * Minimal skeleton that boots, initialises NVS and connects
 * to the Wi‑Fi AP you provided (SSID: 1018 / PASS: huxin820513).
 * Once connected it prints the acquired IP address. Nothing
 * else is started yet – this satisfies Milestone #1 in the
 * incremental plan and can be compiled & flashed as‑is.
 *---------------------------------------------------------*/

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "wakeword_service.h"

static const char *TAG = "APP";

#define WIFI_SSID "HUAWEI-ER1L9Y"
#define WIFI_PASS "2023AP13865"

//--------------------------------------------------
// Wi‑Fi event handler
//--------------------------------------------------
static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Disconnected – retrying …");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished");
}

void app_main(void)
{
    ESP_LOGI(TAG, "APP STARTED – build %s %s", __DATE__, __TIME__);

    // Initialise NVS (required by Wi‑Fi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    wifi_init_sta();
    wakeword_service_start();

    // Placeholder – nothing else yet. The RTOS idle task keeps running.
    while (1) {
        if (wakeword_is_waked()) {
            ESP_LOGI("APP", "捕获到唤醒词，进入下一步 STT");
            wakeword_reset();
            // TODO: 调用 STT 流程
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}