#include "i2s_audio.h"
#include "sd_player.h"
#include "uart_cmd.h"
#include "config.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "AUDIO";
static i2s_chan_handle_t tx_chan;
static volatile bool playing = true;

#define FRAME_COUNT 240

esp_err_t i2s_audio_init(void)
{
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.dma_frame_num = FRAME_COUNT;
    cc.dma_desc_num  = 4;
    ESP_ERROR_CHECK(i2s_new_channel(&cc, &tx_chan, NULL));

    i2s_std_config_t sc = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(CFG_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_16BIT,
                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CFG_I2S_BCLK,
            .ws   = CFG_I2S_WS,
            .dout = CFG_I2S_DOUT,
            .din  = I2S_GPIO_UNUSED,
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &sc));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));
    ESP_LOGI(TAG, "I2S TX: BCLK=%d WS=%d DOUT=%d (%d Hz)",
             CFG_I2S_BCLK, CFG_I2S_WS, CFG_I2S_DOUT, CFG_SAMPLE_RATE);
    return ESP_OK;
}

static void audio_task(void *arg)
{
    int16_t buf[FRAME_COUNT * 2];
    int16_t mono[FRAME_COUNT];
    uint32_t loop_count = 0;

    ESP_LOGI(TAG, "TX task basladi.");

    while (1) {
        if (!playing) {
            memset(buf, 0, sizeof(buf));
            size_t w;
            i2s_channel_write(tx_chan, buf, sizeof(buf), &w, pdMS_TO_TICKS(100));
            continue;
        }

        int read = sd_player_read(mono, FRAME_COUNT);
        if (read <= 0) {
            loop_count++;
            if (!uart_cmd_logs_paused()) {
                ESP_LOGI(TAG, "Loop #%lu", loop_count);
            }
            sd_player_rewind();
            continue;
        }

        for (int i = 0; i < read; i++) {
            buf[i * 2]     = mono[i];
            buf[i * 2 + 1] = mono[i];
        }

        size_t written;
        i2s_channel_write(tx_chan, buf, read * 2 * sizeof(int16_t), &written, pdMS_TO_TICKS(1000));
    }
}

void i2s_audio_start_task(void)
{
    playing = true;
    xTaskCreate(audio_task, "audio", 4096, NULL, 5, NULL);
}

void i2s_audio_stop(void)    { playing = false; ESP_LOGI(TAG, "Durduruldu."); }
void i2s_audio_resume(void)  { playing = true;  ESP_LOGI(TAG, "Devam ediyor."); }
bool i2s_audio_is_playing(void) { return playing; }
