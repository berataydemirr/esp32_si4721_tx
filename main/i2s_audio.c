#include "i2s_audio.h"
#include "sd_player.h"
#include "uart_cmd.h"
#include "config.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "AUDIO";
static i2s_chan_handle_t tx_chan;
static i2s_chan_handle_t rx_chan;
static volatile bool active = true;
static TaskHandle_t audio_task_handle;

#define FRAME_COUNT 240

// ==================== I2S TX INIT (SD → Si4721) ====================

esp_err_t i2s_audio_init_tx(void)
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
    ESP_LOGI(TAG, "I2S TX: BCLK=%d WS=%d DOUT=%d", CFG_I2S_BCLK, CFG_I2S_WS, CFG_I2S_DOUT);
    return ESP_OK;
}

// ==================== I2S RX INIT (Si4721 → ESP32 → Hoparlor) ====================

esp_err_t i2s_audio_init_rx(void)
{
    // TX+RX birlikte olustur: TX clock uretimine ve hoparlore veri basmaya devam eder
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.dma_frame_num = FRAME_COUNT;
    cc.dma_desc_num  = 4;
    ESP_ERROR_CHECK(i2s_new_channel(&cc, &tx_chan, &rx_chan));

    i2s_std_config_t sc = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(CFG_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_16BIT,
                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CFG_I2S_BCLK,
            .ws   = CFG_I2S_WS,
            .dout = CFG_I2S_DOUT,   // Hoparlore passthrough icin
            .din  = CFG_I2S_DIN,    // Si4721 DOUT'tan veri okuma
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &sc));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &sc));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));
    ESP_LOGI(TAG, "I2S RX+TX: BCLK=%d WS=%d DIN=%d DOUT=%d (passthrough)",
             CFG_I2S_BCLK, CFG_I2S_WS, CFG_I2S_DIN, CFG_I2S_DOUT);
    return ESP_OK;
}

// ==================== DEINIT ====================

void i2s_audio_deinit(void)
{
    active = false;
    vTaskDelay(pdMS_TO_TICKS(200));

    if (audio_task_handle) {
        vTaskDelete(audio_task_handle);
        audio_task_handle = NULL;
    }
    if (tx_chan) {
        i2s_channel_disable(tx_chan);
        i2s_del_channel(tx_chan);
        tx_chan = NULL;
    }
    if (rx_chan) {
        i2s_channel_disable(rx_chan);
        i2s_del_channel(rx_chan);
        rx_chan = NULL;
    }
    ESP_LOGI(TAG, "I2S deinit OK");
}

// ==================== TX TASK (SD → I2S → Si4721 DIN) ====================

static void tx_task(void *arg)
{
    int16_t buf[FRAME_COUNT * 2];
    int16_t mono[FRAME_COUNT];
    uint32_t loop_count = 0;

    ESP_LOGI(TAG, "[TX] Task basladi. Heap=%lu", (unsigned long)esp_get_free_heap_size());

    while (1) {
        if (!active) {
            memset(buf, 0, sizeof(buf));
            size_t w;
            i2s_channel_write(tx_chan, buf, sizeof(buf), &w, pdMS_TO_TICKS(100));
            continue;
        }

        int read = sd_player_read(mono, FRAME_COUNT);
        if (read <= 0) {
            loop_count++;
            if (!uart_cmd_logs_paused()) {
                ESP_LOGI(TAG, "[TX] Loop #%lu", loop_count);
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

// ==================== RX TASK (Si4721 DOUT → I2S → Debug + Passthrough) ====================

static void rx_task(void *arg)
{
    int16_t rx_buf[FRAME_COUNT * 2];
    uint32_t total_samples = 0;
    uint32_t nonzero_total = 0;
    bool first_data = true;

    ESP_LOGI(TAG, "[RX] Task basladi. GPIO%d'den veri okunuyor...", CFG_I2S_DIN);

    while (1) {
        if (!active) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(rx_chan, rx_buf, sizeof(rx_buf), &bytes_read, pdMS_TO_TICKS(1000));

        if (ret != ESP_OK || bytes_read == 0) {
            continue;
        }

        int sample_count = bytes_read / sizeof(int16_t);

        // Ilk veri paketi — debug log
        if (first_data) {
            ESP_LOGI(TAG, "[RX] Ilk paket: %d byte, %d sample", (int)bytes_read, sample_count);
            int show = sample_count > 16 ? 16 : sample_count;
            for (int i = 0; i < show; i++) {
                ESP_LOGI(TAG, "[RX]   sample[%d] = %d", i, rx_buf[i]);
            }
            first_data = false;
        }

        // Sifir olmayan sample say — sinyal var mi?
        int nonzero = 0;
        for (int i = 0; i < sample_count; i++) {
            if (rx_buf[i] != 0) nonzero++;
        }
        nonzero_total += nonzero;
        total_samples += sample_count;

        // Passthrough: okunan veriyi hoparlore bas
        size_t written;
        i2s_channel_write(tx_chan, rx_buf, bytes_read, &written, pdMS_TO_TICKS(100));

        // Her 5 saniyede debug raporu
        if (!uart_cmd_logs_paused() && (total_samples % (CFG_SAMPLE_RATE * 5 * 2)) < (uint32_t)(FRAME_COUNT * 2)) {
            int pct = (total_samples > 0) ? (int)(nonzero_total * 100 / total_samples) : 0;
            ESP_LOGI(TAG, "[RX] Toplam: %lu sample | Sifir-olmayan: %lu (%d%%) | %s",
                     total_samples, nonzero_total, pct,
                     pct > 5 ? "SINYAL VAR" : "SESSIZ (anten kontrol et)");
        }
    }
}

// ==================== PUBLIC API ====================

void i2s_audio_start_tx_task(void)
{
    active = true;
    xTaskCreate(tx_task, "audio_tx", 4096, NULL, 5, &audio_task_handle);
}

void i2s_audio_start_rx_task(void)
{
    active = true;
    xTaskCreate(rx_task, "audio_rx", 4096, NULL, 5, &audio_task_handle);
}

void i2s_audio_stop(void)
{
    active = false;
    ESP_LOGI(TAG, "Durduruldu.");
}

void i2s_audio_resume(void)
{
    active = true;
    ESP_LOGI(TAG, "Devam ediyor.");
}

bool i2s_audio_is_playing(void)
{
    return active;
}
