#pragma once

#include "esp_err.h"
#include <stdbool.h>

esp_err_t uart_cmd_init(void);
bool uart_cmd_logs_paused(void);
