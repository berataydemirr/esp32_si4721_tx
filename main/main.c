#include "config.h"
#include "config_manager.h"
#include "si4721.h"
#include "i2s_audio.h"
#include "sd_player.h"
#include "sd_logger.h"
#include "uart_cmd.h"
#include "esp_log.h"
#include "esp_system.h"
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
        default:               return "UNKNOWN";
    }
}

void app_main(void)
{
    ESP_LOGW(TAG, "RESET: %s", reset_reason_str(esp_reset_reason()));
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   ESP32-P4 FM TRANSMITTER v3.0         ");
    ESP_LOGI(TAG, "========================================");

    // 1. I2C + RST
    si4721_init();
    ESP_LOGI(TAG, "I2C + RST hazir");

    // 2. I2S TX (POWER_UP'tan ONCE)
    i2s_audio_init();

    // 3. SD kart
    if (sd_player_init() != ESP_OK) {
        ESP_LOGE(TAG, "SD kart basarisiz!");
        return;
    }
    sd_logger_init();

    // 4. Config yukle
    config_manager_init();
    config_manager_load();
    const runtime_config_t *rcfg = config_manager_get();

    ESP_LOGI(TAG, "FREKANS : %d.%02d MHz", rcfg->tune_freq / 100, rcfg->tune_freq % 100);
    ESP_LOGI(TAG, "GUC     : %d dBuV", rcfg->tx_power);
    ESP_LOGI(TAG, "PLAYLIST: %d dosya", rcfg->playlist_count);

    // 5. WAV ac + audio task
    char wav_path[128];
    snprintf(wav_path, sizeof(wav_path), "/sdcard/%s", rcfg->playlist[0]);
    if (sd_player_open(wav_path) == ESP_OK && rcfg->autoplay) {
        i2s_audio_start_task();
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    // 6. Si4721 TX baslat
    int attempt = 1;
    while (si4721_power_up() != ESP_OK) {
        ESP_LOGE(TAG, "POWER_UP basarisiz (deneme %d)", attempt++);
        if (attempt > 10) attempt = 1;
    }
    ESP_LOGI(TAG, "POWER_UP OK (%d. denemede)", attempt);

    // 7. Configure + RDS
    si4721_configure();
    si4721_rds_init();
    if (rcfg->rds_rt[0] != '\0') {
        si4721_rds_set_rt(rcfg->rds_rt);
    }

    // 8. Terminal
    uart_cmd_init();

    // 9. Property raporu
    ESP_LOGI(TAG, "--- DOGRULAMA ---");
    ESP_LOGI(TAG, "FORMAT  = 0x%04X", si4721_get_property(SI_PROP_DIGITAL_INPUT_FORMAT));
    ESP_LOGI(TAG, "SR      = %d", si4721_get_property(SI_PROP_DIGITAL_INPUT_SAMPLE_RATE));
    ESP_LOGI(TAG, "REFCLK  = %d", si4721_get_property(SI_PROP_REFCLK_FREQ));
    ESP_LOGI(TAG, "DEV     = %d", si4721_get_property(SI_PROP_TX_AUDIO_DEVIATION));
    ESP_LOGI(TAG, "INLEVEL = %d dBFS", si4721_get_inlevel());

    ESP_LOGI(TAG, "=== TX AKTIF — YAYIN YAPILIYOR ===");

    // 10. Izleme
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(CFG_LOG_INTERVAL_MS));
        if (uart_cmd_logs_paused()) continue;

        int8_t inlevel = si4721_get_inlevel();
        if (inlevel <= CFG_INLEVEL_ALARM) {
            ESP_LOGE(TAG, "ALARM! INLEVEL: %d dBFS", inlevel);
        } else {
            ESP_LOGI(TAG, "INLEVEL: %d dBFS | %d.%02d MHz | %s",
                     inlevel, rcfg->tune_freq / 100, rcfg->tune_freq % 100,
                     i2s_audio_is_playing() ? "PLAYING" : "STOPPED");
        }
    }
}
