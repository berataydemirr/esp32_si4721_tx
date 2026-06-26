#pragma once

#include "esp_err.h"
#include <stdint.h>

esp_err_t sd_player_init(void);
esp_err_t sd_player_open(const char *path);
int       sd_player_read(int16_t *mono_buf, int samples);
void      sd_player_rewind(void);
void      sd_player_close(void);

/** @brief Mevcut dosyayi kapat, yeni dosyayi ac ve basa sar */
esp_err_t sd_player_play(const char *filename);
