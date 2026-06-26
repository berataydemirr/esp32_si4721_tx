#include "sd_player.h"
#include "config.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>

static const char *TAG = "SD";

static FILE *wav_file;
static uint16_t wav_channels;
static long data_offset;

#define SD_MOUNT_RETRY_COUNT    5
#define SD_MOUNT_RETRY_DELAY_MS 500
#define SD_POWER_SETTLE_MS      1000

esp_err_t sd_player_init(void)
{
    // Guc stabilizasyonu: priz/adaptor boot'unda LDO oturmasi icin bekle
    ESP_LOGI(TAG, "[POWER] %d ms guc stabilizasyon beklemesi...", SD_POWER_SETTLE_MS);
    vTaskDelay(pdMS_TO_TICKS(SD_POWER_SETTLE_MS));

    // ESP32-P4: SD kart IO pinleri on-chip LDO kanal 4 ile beslenir
    ESP_LOGI(TAG, "[POWER] LDO kanal 4 baslatiliyor (3.3V)...");
    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = 4,
    };
    sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;
    esp_err_t ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[POWER] LDO init BASARISIZ: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "[POWER] LDO aktif, 500 ms voltaj oturma beklemesi...");
    vTaskDelay(pdMS_TO_TICKS(500));

    // Host ve slot konfigurasyonu
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.pwr_ctrl_handle = pwr_ctrl_handle;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = 43;
    slot.cmd = 44;
    slot.d0  = 39;
    slot.d1  = 40;
    slot.d2  = 41;
    slot.d3  = 42;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mcfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    // Retry dongusu: adaptor boot'unda kart hazir olmayabilir
    sdmmc_card_t *card = NULL;
    for (int attempt = 1; attempt <= SD_MOUNT_RETRY_COUNT; attempt++) {
        ESP_LOGI(TAG, "[MOUNT] Deneme %d/%d...", attempt, SD_MOUNT_RETRY_COUNT);

        ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mcfg, &card);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "[MOUNT] Basarili! (deneme %d)", attempt);
            break;
        }

        ESP_LOGW(TAG, "[MOUNT] Basarisiz: %s", esp_err_to_name(ret));
        if (attempt < SD_MOUNT_RETRY_COUNT) {
            ESP_LOGI(TAG, "[MOUNT] %d ms sonra tekrar deneniyor...", SD_MOUNT_RETRY_DELAY_MS);
            vTaskDelay(pdMS_TO_TICKS(SD_MOUNT_RETRY_DELAY_MS));
        }
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[MOUNT] %d denemede de basarisiz! Kart takili mi?", SD_MOUNT_RETRY_COUNT);
        return ret;
    }

    ESP_LOGI(TAG, "[INFO] Kart: %s | %llu MB | %d kHz",
             card->cid.name,
             ((uint64_t)card->csd.capacity) * card->csd.sector_size / (1024 * 1024),
             card->max_freq_khz);

    ESP_LOGI(TAG, "[INFO] /sdcard dizin icerigi:");
    DIR *dir = opendir("/sdcard");
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            ESP_LOGI(TAG, "  -> %s", ent->d_name);
        }
        closedir(dir);
    }

    FILE *test = fopen(CFG_WAV_FILE, "rb");
    if (test) {
        ESP_LOGI(TAG, "[INFO] %s BULUNDU", CFG_WAV_FILE);
        fclose(test);
    } else {
        ESP_LOGE(TAG, "[INFO] %s BULUNAMADI!", CFG_WAV_FILE);
    }

    return ESP_OK;
}

esp_err_t sd_player_open(const char *path)
{
    ESP_LOGI(TAG, "[OPEN] Dosya aciliyor: %s", path);

    wav_file = fopen(path, "rb");
    if (!wav_file) {
        ESP_LOGE(TAG, "Dosya acilamadi: %s", path);
        return ESP_FAIL;
    }

    uint8_t hdr[44];
    if (fread(hdr, 1, 44, wav_file) != 44) {
        ESP_LOGE(TAG, "WAV header okunamadi");
        fclose(wav_file);
        wav_file = NULL;
        return ESP_FAIL;
    }

    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
        ESP_LOGE(TAG, "Gecersiz WAV dosyasi");
        fclose(wav_file);
        wav_file = NULL;
        return ESP_FAIL;
    }

    wav_channels         = hdr[22] | (hdr[23] << 8);
    uint32_t sample_rate = hdr[24] | (hdr[25] << 8) | (hdr[26] << 16) | (hdr[27] << 24);
    uint16_t bits        = hdr[34] | (hdr[35] << 8);
    data_offset          = 44;

    ESP_LOGI(TAG, "[HEADER] %d Hz, %d-bit, %s",
             (int)sample_rate, bits,
             wav_channels == 1 ? "mono" : "stereo");

    if (sample_rate != CFG_SAMPLE_RATE) {
        ESP_LOGW(TAG, "[CHECK] Sample rate UYUMSUZ! WAV=%d, config=%d",
                 (int)sample_rate, CFG_SAMPLE_RATE);
    } else {
        ESP_LOGI(TAG, "[CHECK] Sample rate OK: %d Hz", (int)sample_rate);
    }

    if (bits != 16) {
        ESP_LOGE(TAG, "[CHECK] Sadece 16-bit desteklenir (bu: %d-bit)", bits);
        fclose(wav_file);
        wav_file = NULL;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "[CHECK] Bit depth OK: 16-bit");
    ESP_LOGI(TAG, "[CHECK] Data offset: %ld byte", data_offset);
    return ESP_OK;
}

int sd_player_read(int16_t *mono_buf, int samples)
{
    if (!wav_file) return 0;

    if (wav_channels == 1) {
        return (int)fread(mono_buf, sizeof(int16_t), samples, wav_file);
    }

    int16_t stereo[2];
    int count = 0;
    for (int i = 0; i < samples; i++) {
        if (fread(stereo, sizeof(int16_t), 2, wav_file) != 2) break;
        mono_buf[i] = stereo[0];
        count++;
    }
    return count;
}

void sd_player_rewind(void)
{
    if (wav_file) {
        fseek(wav_file, data_offset, SEEK_SET);
    }
}

void sd_player_close(void)
{
    if (wav_file) {
        fclose(wav_file);
        wav_file = NULL;
    }
}

esp_err_t sd_player_play(const char *filename)
{
    sd_player_close();

    char path[128];
    snprintf(path, sizeof(path), "/sdcard/%s", filename);
    ESP_LOGI(TAG, "[PLAY] Dosya degistiriliyor: %s", path);
    return sd_player_open(path);
}
