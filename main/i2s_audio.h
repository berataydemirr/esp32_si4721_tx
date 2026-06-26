#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/** @brief I2S TX kanalini baslat (ESP32 → Si4721 DIN) */
esp_err_t i2s_audio_init_tx(void);

/** @brief I2S RX kanalini baslat (Si4721 DOUT → ESP32) */
esp_err_t i2s_audio_init_rx(void);

/** @brief I2S TX ve RX kanallarini kapat */
void i2s_audio_deinit(void);

/** @brief SD karttan WAV okuyan TX task'i baslat */
void i2s_audio_start_tx_task(void);

/** @brief Si4721'den I2S okuyan RX task'i baslat */
void i2s_audio_start_rx_task(void);

/** @brief Oynatmayi/dinlemeyi durdur */
void i2s_audio_stop(void);

/** @brief Oynatmaya devam et */
void i2s_audio_resume(void);

/** @brief Oynatma/dinleme durumu */
bool i2s_audio_is_playing(void);
