#pragma once

#include "esp_err.h"
#include <stdbool.h>

esp_err_t i2s_audio_init(void);
void      i2s_audio_start_task(void);
void      i2s_audio_stop(void);
void      i2s_audio_resume(void);
bool      i2s_audio_is_playing(void);
