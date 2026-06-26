#include "sd_logger.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>

static const char *TAG = "SDLOG";
static FILE *log_file;
static vprintf_like_t original_vprintf;
static bool logger_enabled;

#define LOG_FILE_PATH "/sdcard/debug.log"

static void force_flush(void)
{
    if (!log_file) return;
    fflush(log_file);
    int fd = fileno(log_file);
    if (fd >= 0) {
        fsync(fd);
    }
}

static int dual_vprintf(const char *fmt, va_list args)
{
    va_list args_copy;
    va_copy(args_copy, args);
    int ret = original_vprintf(fmt, args_copy);
    va_end(args_copy);

    if (logger_enabled && log_file) {
        int written = vfprintf(log_file, fmt, args);
        if (written < 0) {
            logger_enabled = false;
        } else {
            force_flush();
        }
    }

    return ret;
}

esp_err_t sd_logger_init(void)
{
    log_file = fopen(LOG_FILE_PATH, "a");
    if (!log_file) {
        ESP_LOGE(TAG, "Log dosyasi acilamadi: %s", LOG_FILE_PATH);
        return ESP_FAIL;
    }

    fprintf(log_file, "\n========== BOOT ==========\n");
    force_flush();

    logger_enabled = true;
    original_vprintf = esp_log_set_vprintf(dual_vprintf);
    ESP_LOGI(TAG, "SD logger aktif: %s", LOG_FILE_PATH);
    return ESP_OK;
}

void sd_logger_deinit(void)
{
    logger_enabled = false;
    if (original_vprintf) {
        esp_log_set_vprintf(original_vprintf);
        original_vprintf = NULL;
    }
    if (log_file) {
        force_flush();
        fclose(log_file);
        log_file = NULL;
    }
}

void sd_logger_pause(void)
{
    logger_enabled = false;
    if (log_file) {
        force_flush();
        fclose(log_file);
        log_file = NULL;
    }
}

void sd_logger_resume(void)
{
    if (log_file) return;
    log_file = fopen(LOG_FILE_PATH, "a");
    if (log_file) {
        logger_enabled = true;
    }
}
