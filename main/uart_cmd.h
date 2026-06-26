#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Komut terminali task'i baslat (stdin/stdout via USB-CDC)
 * @note  Komutlar: PLAY:x.wav, STOP, RESUME, MSG:x, RT:x, STATUS, PAUSELOG, RESUMELOG, CLS
 */
esp_err_t uart_cmd_init(void);

/** @brief Loglar duraklatildi mi? (diger moduller bu flag'i kontrol eder) */
bool uart_cmd_logs_paused(void);
