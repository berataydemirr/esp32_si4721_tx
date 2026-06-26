#pragma once

#include "esp_err.h"

/** @brief SD kart uzerinde debug.log dosyasini ac ve ESP_LOG hook'unu etkinlestir */
esp_err_t sd_logger_init(void);

/** @brief Log dosyasini kapat ve hook'u kaldir */
void sd_logger_deinit(void);

/** @brief Log dosyasini gecici olarak kapat (dosya descriptor serbest kalir) */
void sd_logger_pause(void);

/** @brief Log dosyasini tekrar ac (append) */
void sd_logger_resume(void);
