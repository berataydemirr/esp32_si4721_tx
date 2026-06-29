#include "uart_cmd.h"
#include "config.h"
#include "config_manager.h"
#include "sd_player.h"
#include "i2s_audio.h"
#include "si4721.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "CMD";
static volatile bool logs_paused = true;

bool uart_cmd_logs_paused(void) { return logs_paused; }

static void respond(const char *msg)
{
    printf("%s\r\n", msg);
    fflush(stdout);
}

static void print_help(void)
{
    const runtime_config_t *rc = config_manager_get();
    printf("--- TX | %d.%02d MHz | %d dBuV ---\r\n",
           rc->tune_freq / 100, rc->tune_freq % 100, rc->tx_power);
    respond("  PLAY:dosya.wav  - SD'den dosya cal");
    respond("  STOP / RESUME   - Durdur / Devam et");
    respond("  MSG:metin       - RDS PS (max 8 kar)");
    respond("  RT:metin        - RDS RT (max 64 kar)");
    respond("  STATUS          - Sistem durumu");
    respond("  RELOAD_CONFIG   - settings.txt oku");
    respond("  PAUSELOG / RESUMELOG");
    respond("  CLS / HELP");
    fflush(stdout);
}

static void handle_status(void)
{
    const runtime_config_t *rc = config_manager_get();
    int8_t inlevel = si4721_get_inlevel();
    printf("TX | INLEVEL=%d dBFS | %d.%02d MHz | %d dBuV | %s | Heap=%lu\r\n",
           inlevel, rc->tune_freq / 100, rc->tune_freq % 100,
           rc->tx_power,
           i2s_audio_is_playing() ? "PLAYING" : "STOPPED",
           (unsigned long)esp_get_free_heap_size());
    fflush(stdout);
}

static void process_command(char *line)
{
    size_t len = strlen(line);
    while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n' || line[len-1] == ' '))
        line[--len] = '\0';
    if (len == 0) return;

    if (strncmp(line, "PLAY:", 5) == 0) {
        i2s_audio_stop();
        if (sd_player_play(line + 5) == ESP_OK) {
            i2s_audio_resume();
            respond("OK:PLAYING");
        } else {
            respond("ERR:FILE_NOT_FOUND");
        }
    } else if (strcmp(line, "STOP") == 0) {
        i2s_audio_stop();
        respond("OK:STOPPED");
    } else if (strcmp(line, "RESUME") == 0) {
        i2s_audio_resume();
        respond("OK:RESUMED");
    } else if (strncmp(line, "MSG:", 4) == 0) {
        si4721_rds_set_ps(line + 4);
        respond("OK:RDS_PS");
    } else if (strncmp(line, "RT:", 3) == 0) {
        si4721_rds_set_rt(line + 3);
        respond("OK:RDS_RT");
    } else if (strcmp(line, "STATUS") == 0) {
        handle_status();
    } else if (strcmp(line, "PAUSELOG") == 0) {
        logs_paused = true;
        printf("\033[2J\033[H");
        respond("OK:LOGS_PAUSED");
    } else if (strcmp(line, "RESUMELOG") == 0) {
        logs_paused = false;
        respond("OK:LOGS_RESUMED");
    } else if (strcmp(line, "RELOAD_CONFIG") == 0) {
        if (config_manager_load() == ESP_OK) {
            config_manager_apply();
            respond("OK:CONFIG_RELOADED");
        } else {
            respond("ERR:CONFIG_LOAD_FAILED");
        }
    } else if (strcmp(line, "CLS") == 0) {
        printf("\033[2J\033[H");
        fflush(stdout);
    } else if (strcmp(line, "HELP") == 0) {
        print_help();
    } else {
        respond("ERR:CMD_INVALID — HELP yaz");
    }
}

static void cmd_task(void *arg)
{
    char line[CFG_UART_BUF_SIZE];
    int pos = 0;

    respond("=== FM Transmitter Terminal ===");
    respond("HELP yaz, komutlari gor.");
    printf("> ");
    fflush(stdout);

    while (1) {
        int c = fgetc(stdin);
        if (c == EOF) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        if (c == '\r' || c == '\n') {
            if (pos == 0) continue;
            printf("\r\n"); fflush(stdout);
            line[pos] = '\0';
            process_command(line);
            pos = 0;
            printf("> "); fflush(stdout);
            continue;
        }
        if (c == '\b' || c == 127) {
            if (pos > 0) { pos--; printf("\b \b"); fflush(stdout); }
            continue;
        }
        if (pos < (int)sizeof(line) - 1) {
            line[pos++] = (char)c;
            putchar(c); fflush(stdout);
        }
    }
}

esp_err_t uart_cmd_init(void)
{
    xTaskCreate(cmd_task, "cmd", 4096, NULL, 10, NULL);
    ESP_LOGI(TAG, "Terminal hazir");
    return ESP_OK;
}
