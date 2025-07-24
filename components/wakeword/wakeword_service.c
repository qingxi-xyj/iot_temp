#include "wakeword_service.h"

// 替换旧的头文件引用
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_srmodel_iface.h" // 新增：用于模型管理

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 引入官方示例中的板级初始化，您可能需要根据项目调整
#include "esp_board_init.h"

#define TAG "WAKEWORD"

#define SAMPLE_RATE_HZ 16000
#define I2S_PORT       I2S_NUM_0
// AFE 期望的音频帧大小，通过 get_feed_chunksize 获取
// #define BUF_BYTES      (320)  // 不再硬编码

static bool s_waked = false;
bool  wakeword_is_waked(void) { return s_waked; }
void  wakeword_reset(void)    { s_waked = false; }

/* ---------- AFE & WakeNet 句柄 (使用新 API) ---------- */
static esp_afe_sr_iface_t *s_afe_handle = NULL;
static esp_afe_sr_data_t  *s_afe_data  = NULL;

/* ---------- I2S 驱动 (保持不变) ---------- */
static i2s_chan_handle_t s_rx_chan = NULL;
static void i2s_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_PORT, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_rx_chan, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_SLOT_MODE_MONO, 16),
        .gpio_cfg = {
            .mclk = AUDIO_I2S_GPIO_MCLK,
            .bclk = AUDIO_I2S_GPIO_BCLK,
            .ws   = AUDIO_I2S_GPIO_WS,
            .din  = AUDIO_I2S_GPIO_DIN,
            .dout = I2S_GPIO_UNUSED,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx_chan));
}

/* ---------- 唤醒词任务 (适配新 API) ---------- */
static void wakeword_task(void *arg)
{
    /* 1. 初始化板级外设 (I2S, Codec等) */
    // 官方示例使用此函数统一初始化，它会处理好 I2S 的配置
    // 您可以保留自己的 i2s_init，或切换到 esp_board_init
    // 如果 esp_board_init 不符合您的硬件，请确保您的 i2s_init 能正确配置
    ESP_ERROR_CHECK(esp_board_init(16000, 1, 16));
    // i2s_init(); // 如果不使用 esp_board_init，请取消此行注释

    /* 2. 初始化 AFE (使用新流程) */
    // 2.1 从 "model" 分区加载模型信息
    srmodel_list_t *models = esp_srmodel_init("model");
    if (models) {
        ESP_LOGI(TAG, "Models loaded from 'model' partition.");
        for (int i = 0; i < models->num; i++) {
            ESP_LOGI(TAG, "  - Model %d: %s", i, models->model_name[i]);
        }
    } else {
        ESP_LOGE(TAG, "Failed to init speech recognition models!");
        vTaskDelete(NULL);
    }
    
    // 2.2 初始化 AFE 配置
    afe_config_t *afe_cfg = afe_config_init(esp_get_input_format(), models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    if (afe_cfg->wakenet_model_name) {
        ESP_LOGI(TAG, "WakeNet model selected: %s", afe_cfg->wakenet_model_name);
    }

    // 2.3 创建 AFE 实例
    s_afe_handle = esp_afe_handle_from_config(afe_cfg);
    s_afe_data = s_afe_handle->create_from_config(afe_cfg);

    // 释放临时配置对象
    afe_config_free(afe_cfg);
    ESP_LOGI(TAG, "AFE ready");

    /* 3. 获取 AFE 需要的音频块大小 */
    int audio_chunksize = s_afe_handle->get_feed_chunksize(s_afe_data);
    int16_t *pcm_buff = malloc(audio_chunksize * sizeof(int16_t));
    if (!pcm_buff) {
        ESP_LOGE(TAG, "Failed to allocate memory for PCM buffer");
        vTaskDelete(NULL);
    }
    size_t bytes_read;

    while (1) {
        /* 3.1 采集音频 */
        esp_get_feed_data(true, pcm_buff, audio_chunksize * sizeof(int16_t));

        /* 3.2 将音频送入 AFE 处理 */
        s_afe_handle->feed(s_afe_data, pcm_buff);

        /* 3.3 从 AFE 获取处理结果 */
        afe_fetch_result_t *res = s_afe_handle->fetch(s_afe_data);
        if (!res || res->ret_value == ESP_FAIL) {
            ESP_LOGE(TAG, "AFE fetch failed!");
            break;
        }

        /* 3.4 检查唤醒状态 */
        if (res->wakeup_state == WAKENET_DETECTED) {
            ESP_LOGI(TAG, "** WAKE-WORD DETECTED **");
            ESP_LOGI(TAG, "  - WakeNet Model Index: %d", res->wakenet_model_index);
            ESP_LOGI(TAG, "  - Wake Word Index: %d", res->wake_word_index);
            s_waked = true;
            vTaskDelay(pdMS_TO_TICKS(400)); // 防抖
        }
    }

    // 清理资源
    if (pcm_buff) free(pcm_buff);
    s_afe_handle->destroy(s_afe_data);
    vTaskDelete(NULL);
}

void wakeword_service_start(void)
{
    xTaskCreatePinnedToCore(
        wakeword_task, "wakeword_task",
        CONFIG_WAKEWORD_STACK_SIZE,
        NULL, 5, NULL, 1);
}
