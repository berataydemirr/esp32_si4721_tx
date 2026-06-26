#include "config_manager.h"
#include "config.h"
#include "si4721.h"
#include "sd_logger.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "CFGMGR";
static runtime_config_t cfg;

static int clamp_int(int val, int lo, int hi, const char *name)
{
    if (val < lo) {
        ESP_LOGW(TAG, "%s=%d -> %d'e cekildi (min)", name, val, lo);
        return lo;
    }
    if (val > hi) {
        ESP_LOGW(TAG, "%s=%d -> %d'e cekildi (max)", name, val, hi);
        return hi;
    }
    return val;
}

static void trim(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' || s[len - 1] == ' ')) {
        s[--len] = '\0';
    }
}

void config_manager_init(void)
{
    cfg.mode        = RADIO_MODE_TX;
    cfg.tune_freq   = CFG_TUNE_FREQ;
    cfg.tx_power    = CFG_TX_POWER;
    cfg.deviation   = CFG_DEVIATION;
    cfg.preemphasis = CFG_PREEMPHASIS;
    cfg.autoplay    = true;
    cfg.playlist_count = 1;

    strncpy(cfg.rds_ps, CFG_RDS_PS_DEFAULT, 8);
    cfg.rds_ps[8] = '\0';
    cfg.rds_rt[0] = '\0';
    strncpy(cfg.playlist[0], "audio.wav", CFG_MAX_FILENAME - 1);

    ESP_LOGI(TAG, "Varsayilan config yuklendi.");
}

static void parse_playlist(const char *val)
{
    cfg.playlist_count = 0;
    char buf[256];
    strncpy(buf, val, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *tok = strtok(buf, ",");
    while (tok && cfg.playlist_count < CFG_MAX_PLAYLIST) {
        while (*tok == ' ') tok++;
        strncpy(cfg.playlist[cfg.playlist_count], tok, CFG_MAX_FILENAME - 1);
        cfg.playlist[cfg.playlist_count][CFG_MAX_FILENAME - 1] = '\0';
        cfg.playlist_count++;
        tok = strtok(NULL, ",");
    }
}

static void parse_line(char *line)
{
    trim(line);
    if (line[0] == '#' || line[0] == '\0') return;

    char *eq = strchr(line, '=');
    if (!eq) return;

    *eq = '\0';
    char *key = line;
    char *val = eq + 1;
    trim(key);
    // Value'nun basindaki bosluklari da temizle
    while (*val == ' ') val++;
    trim(val);

    ESP_LOGI(TAG, "[PARSE] key=\"%s\" val=\"%s\"", key, val);

    if (strcmp(key, "mode") == 0) {
        if (strcmp(val, "RX") == 0) cfg.mode = RADIO_MODE_RX;
        else cfg.mode = RADIO_MODE_TX;
        ESP_LOGI(TAG, "  -> mode = %s", cfg.mode == RADIO_MODE_RX ? "RX" : "TX");
    } else if (strcmp(key, "frequency") == 0) {
        cfg.tune_freq = (uint16_t)clamp_int(atoi(val), 7600, 10800, "frequency");
        ESP_LOGI(TAG, "  -> frequency = %d (%d.%02d MHz)", cfg.tune_freq, cfg.tune_freq/100, cfg.tune_freq%100);
    } else if (strcmp(key, "tx_power") == 0) {
        cfg.tx_power = (uint8_t)clamp_int(atoi(val), 88, 120, "tx_power");
        ESP_LOGI(TAG, "  -> tx_power = %d dBuV", cfg.tx_power);
    } else if (strcmp(key, "deviation") == 0) {
        cfg.deviation = (uint16_t)clamp_int(atoi(val), 0, 7500, "deviation");
        ESP_LOGI(TAG, "  -> deviation = %d", cfg.deviation);
    } else if (strcmp(key, "preemphasis") == 0) {
        if (strcmp(val, "75us") == 0) cfg.preemphasis = 0x0000;
        else cfg.preemphasis = 0x0001;
        ESP_LOGI(TAG, "  -> preemphasis = %s", cfg.preemphasis == 0 ? "75us" : "50us");
    } else if (strcmp(key, "rds_ps") == 0) {
        strncpy(cfg.rds_ps, val, 8);
        cfg.rds_ps[8] = '\0';
        ESP_LOGI(TAG, "  -> rds_ps = \"%s\"", cfg.rds_ps);
    } else if (strcmp(key, "rds_rt") == 0) {
        strncpy(cfg.rds_rt, val, 64);
        cfg.rds_rt[64] = '\0';
        ESP_LOGI(TAG, "  -> rds_rt = \"%s\"", cfg.rds_rt);
    } else if (strcmp(key, "playlist") == 0) {
        parse_playlist(val);
        ESP_LOGI(TAG, "  -> playlist = %d dosya", cfg.playlist_count);
    } else if (strcmp(key, "autoplay") == 0) {
        cfg.autoplay = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
        ESP_LOGI(TAG, "  -> autoplay = %s", cfg.autoplay ? "true" : "false");
    }
}

esp_err_t config_manager_load(void)
{
    ESP_LOGI(TAG, "Okunuyor: %s", CFG_SETTINGS_PATH);

    // Logger'i duraklat — dosya descriptor'u serbest birak
    sd_logger_pause();

    FILE *f = fopen(CFG_SETTINGS_PATH, "r");
    if (!f) {
        ESP_LOGW(TAG, "settings.txt bulunamadi, varsayilan degerler.");
        sd_logger_resume();
        return ESP_ERR_NOT_FOUND;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        parse_line(line);
    }
    fclose(f);

    // Logger'i geri ac
    sd_logger_resume();

    ESP_LOGI(TAG, "Yuklendi: %d.%02d MHz | %d dBuV | PS=\"%s\" | %d dosya",
             cfg.tune_freq / 100, cfg.tune_freq % 100,
             cfg.tx_power, cfg.rds_ps, cfg.playlist_count);
    return ESP_OK;
}

const runtime_config_t *config_manager_get(void)
{
    return &cfg;
}

void config_manager_set_mode(radio_mode_t mode)
{
    cfg.mode = mode;
}

esp_err_t config_manager_apply(void)
{
    ESP_LOGI(TAG, "Uygulaniyor (mod: %s)...", cfg.mode == RADIO_MODE_TX ? "TX" : "RX");

    if (cfg.mode == RADIO_MODE_TX) {
        si4721_retune(cfg.tune_freq, cfg.tx_power);
        si4721_set_property(SI_PROP_TX_AUDIO_DEVIATION, cfg.deviation);
        si4721_set_property(SI_PROP_TX_PREEMPHASIS, cfg.preemphasis);
        si4721_rds_set_ps(cfg.rds_ps);
        if (cfg.rds_rt[0] != '\0') {
            si4721_rds_set_rt(cfg.rds_rt);
        }
    } else {
        si4721_rx_configure(cfg.tune_freq);
    }

    ESP_LOGI(TAG, "Config uygulanadi.");
    return ESP_OK;
}
