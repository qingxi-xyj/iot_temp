#include "wakeword_service.h"

#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "WAKEWORD"

#define SAMPLE_RATE_HZ 16000
#define I2S_PORT       I2S_NUM_0
#define BUF_BYTES      (320)        // 16000Hz * 10ms * 16bit * 1ch = 320B

static bool s_waked = false;
bool  wakeword_is_waked(void) { return s_waked; }
void  wakeword_reset(void)    { s_waked = false; }

/* ---------- AFE & WakeNet 句柄 ---------- */
static esp_afe_sr_handle_t  s_afe_handle  = NULL;
static esp_wn_iface_t      *s_wn_iface    = NULL;
static model_iface_data_t  *s_wn_model    = NULL;

/* ---------- I2S 驱动 ---------- */
static i2s_chan_handle_t s_rx_chan = NULL;
static void i2s_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_PORT, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_rx_chan, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(16, I2S_SLOT_MODE_MONO),
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

/* ---------- 唤醒词任务 ---------- */
static void wakeword_task(void *arg)
{
    int16_t pcm_buff[BUF_BYTES / 2];   // 16-bit 单声道
    size_t  bytes_read;

    /* 1. 初始化 I2S 采集 */
    i2s_init();

    /* 2. 初始化 AFE */
    afe_config_t afe_cfg = AFE_CONFIG_DEFAULT();
    afe_cfg.sample_rate            = SAMPLE_RATE_HZ;
    afe_cfg.voice_communication    = false;        // 只做唤醒
    afe_cfg.aec_enable             = false;        // EchoBase 没有回采可先关
    s_afe_handle = esp_afe_sr_init(&afe_cfg);
    ESP_LOGI(TAG, "AFE ready");

    /* 3. 初始化 WakeNet */
    s_wn_model = esp_wn_models[0];                  // models/wn9_nihaoxiaozhi_tts
    s_wn_iface = esp_wn_handle_from_name("wn9");    // v2.x 的通用接口
    void *wn_state = s_wn_iface->create(s_wn_model->model_data, s_wn_model->model_len);

    while (1) {
        /* 3.1 采集 10 ms 音频 */
        if (i2s_channel_read(s_rx_chan, pcm_buff, BUF_BYTES, &bytes_read,
                             portMAX_DELAY) == ESP_OK && bytes_read == BUF_BYTES) {

            /* 3.2 输入 AFE；返回 result 每 30 ms 触发一次 */
            afe_fetch_result_t afe_res = esp_afe_sr_provide(s_afe_handle, pcm_buff);
            if (afe_res == AFE_FETCH_RESULT_OK) {
                int16_t *data_frame = NULL;
                while (esp_afe_sr_fetch(s_afe_handle, &data_frame) == AFE_FETCH_RESULT_OK) {
                    int wn_ret = s_wn_iface->detect(wn_state, data_frame);
                    if (wn_ret > 0) {   // 唤醒成功
                        ESP_LOGI(TAG, "** WAKE-WORD DETECTED (%d) **", wn_ret);
                        s_waked = true;
                        vTaskDelay(pdMS_TO_TICKS(400)); // 防抖
                    }
                }
            }
        }
    }
}

void wakeword_service_start(void)
{
    xTaskCreatePinnedToCore(
        wakeword_task, "wakeword_task",
        CONFIG_WAKEWORD_STACK_SIZE,
        NULL, 5, NULL, 1);
}
