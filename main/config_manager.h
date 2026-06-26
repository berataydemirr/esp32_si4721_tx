#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define CFG_MAX_PLAYLIST    8
#define CFG_MAX_FILENAME    64
#define CFG_SETTINGS_PATH   "/sdcard/settings.txt"

typedef enum {
    RADIO_MODE_TX = 0,
    RADIO_MODE_RX = 1,
} radio_mode_t;

typedef struct {
    radio_mode_t mode;
    uint16_t tune_freq;
    uint8_t  tx_power;
    uint16_t deviation;
    uint16_t preemphasis;
    char     rds_ps[9];
    char     rds_rt[65];
    char     playlist[CFG_MAX_PLAYLIST][CFG_MAX_FILENAME];
    int      playlist_count;
    bool     autoplay;
} runtime_config_t;

void config_manager_init(void);
esp_err_t config_manager_load(void);
const runtime_config_t *config_manager_get(void);
esp_err_t config_manager_apply(void);

/** @brief Runtime'da modu degistir (SWITCH komutu icin) */
void config_manager_set_mode(radio_mode_t mode);
