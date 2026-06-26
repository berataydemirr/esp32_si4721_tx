#include "config.h"
#include "config_manager.h"
#include "si4721.h"
#include "i2s_audio.h"
#include "sd_player.h"
#include "sd_logger.h"
#include "uart_cmd.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAIN";

static const char *reset_reason_str(esp_reset_reason_t reason)
{
    switch (reason) {
        case ESP_RST_POWERON:  return "POWER_ON";
        case ESP_RST_SW:       return "SOFTWARE";
        case ESP_RST_PANIC:    return "PANIC";
        case ESP_RST_INT_WDT:  return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT:      return "WDT";
        case ESP_RST_BROWNOUT: return "BROWNOUT!";
        default:               return "BILINMIYOR";
    }
}

void app_main(void)
{
    ESP_LOGW(TAG, "RESET: %s", reset_reason_str(esp_reset_reason()));
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   ESP32-P4 FM TRANSCEIVER v3.0         ");
    ESP_LOGI(TAG, "========================================");

    // 1. I2C + RST + Hoparlor kontrolu
    si4721_init();
    gpio_set_direction(CFG_PA_CTRL_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(CFG_PA_CTRL_PIN, 0);
    ESP_LOGI(TAG, "I2C + RST + PA_CTRL hazir");

    // 2. SD kart
    if (sd_player_init() != ESP_OK) {
        ESP_LOGE(TAG, "SD kart basarisiz!");
        return;
    }
    sd_logger_init();

    // 3. Config yukle
    config_manager_init();
    config_manager_load();
    const runtime_config_t *rcfg = config_manager_get();

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "MOD     : %s", rcfg->mode == RADIO_MODE_TX ? ">>> TX (VERICI) <<<" : ">>> RX (ALICI) <<<");
    ESP_LOGI(TAG, "FREKANS : %d.%02d MHz", rcfg->tune_freq / 100, rcfg->tune_freq % 100);
    ESP_LOGI(TAG, "GUC     : %d dBuV", rcfg->tx_power);
    ESP_LOGI(TAG, "========================================");

    // 4. Moda gore baslat
    if (rcfg->mode == RADIO_MODE_TX) {
        // --- TX MODU ---
        i2s_audio_init_tx();

        char wav_path[128];
        snprintf(wav_path, sizeof(wav_path), "/sdcard/%s", rcfg->playlist[0]);
        if (sd_player_open(wav_path) == ESP_OK && rcfg->autoplay) {
            i2s_audio_start_tx_task();
        }
        vTaskDelay(pdMS_TO_TICKS(500));

        int attempt = 1;
        while (si4721_tx_power_up() != ESP_OK) {
            ESP_LOGE(TAG, "TX POWER_UP basarisiz (deneme %d)", attempt++);
            if (attempt > 10) attempt = 1;
        }
        si4721_tx_configure();
        si4721_rds_init();
        config_manager_apply();
        ESP_LOGI(TAG, "=== TX MODU AKTIF — YAYIN YAPILIYOR ===");

    } else {
        // --- RX MODU ---
        i2s_audio_init_rx();

        int attempt = 1;
        while (si4721_rx_power_up() != ESP_OK) {
            ESP_LOGE(TAG, "RX POWER_UP basarisiz (deneme %d)", attempt++);
            if (attempt > 10) attempt = 1;
        }
        si4721_rx_configure(rcfg->tune_freq);
        gpio_set_level(CFG_PA_CTRL_PIN, 1);
        i2s_audio_start_rx_task();
        ESP_LOGI(TAG, "=== RX MODU AKTIF — DINLENIYOR ===");
    }

    // 5. UART komut sistemi
    uart_cmd_init();

    // 6. Izleme dongusu
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(CFG_LOG_INTERVAL_MS));
        if (uart_cmd_logs_paused()) continue;

        const runtime_config_t *rc = config_manager_get();

        if (rc->mode == RADIO_MODE_TX) {
            int8_t inlevel = si4721_tx_get_inlevel();
            ESP_LOGI(TAG, "[TX] INLEVEL:%d dBFS | %d.%02d MHz | %d dBuV | %s",
                     inlevel, rc->tune_freq / 100, rc->tune_freq % 100,
                     rc->tx_power,
                     i2s_audio_is_playing() ? "PLAYING" : "STOPPED");
        } else {
            int8_t rssi = si4721_rx_get_rssi();
            uint8_t snr = si4721_rx_get_snr();
            ESP_LOGI(TAG, "[RX] RSSI:%d dBuV | SNR:%d dB | %d.%02d MHz | SPK:%s",
                     rssi, snr,
                     rc->tune_freq / 100, rc->tune_freq % 100,
                     gpio_get_level(CFG_PA_CTRL_PIN) ? "ON" : "OFF");
        }
    }
}
