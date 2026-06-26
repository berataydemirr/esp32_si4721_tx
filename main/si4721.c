#include "si4721.h"
#include "config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "SI4721";

// ==================== DAHILI YARDIMCILAR ====================

static bool wait_cts(void)
{
    uint8_t st = 0;
    for (int i = 0; i < 300; i++) {
        i2c_cmd_handle_t h = i2c_cmd_link_create();
        i2c_master_start(h);
        i2c_master_write_byte(h, (CFG_SI_ADDR << 1) | 1, true);
        i2c_master_read_byte(h, &st, I2C_MASTER_NACK);
        i2c_master_stop(h);
        if (i2c_master_cmd_begin(I2C_NUM_0, h, pdMS_TO_TICKS(20)) == ESP_OK && (st & 0x80)) {
            i2c_cmd_link_delete(h);
            return true;
        }
        i2c_cmd_link_delete(h);
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    ESP_LOGE(TAG, "CTS zamansimi!");
    return false;
}

static void si_cmd(uint8_t *buf, size_t len)
{
    if (!wait_cts()) return;
    i2c_cmd_handle_t h = i2c_cmd_link_create();
    i2c_master_start(h);
    i2c_master_write_byte(h, (CFG_SI_ADDR << 1) | 0, true);
    i2c_master_write(h, buf, len, true);
    i2c_master_stop(h);
    esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, h, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C yazma hatasi: %s", esp_err_to_name(err));
    }
    i2c_cmd_link_delete(h);
}

static void si_prop(uint16_t prop, uint16_t value)
{
    uint8_t c[6] = {
        SI_CMD_SET_PROPERTY, 0x00,
        prop >> 8, prop & 0xFF,
        value >> 8, value & 0xFF
    };
    si_cmd(c, 6);
    vTaskDelay(pdMS_TO_TICKS(10));
}

static void si_read(uint8_t *cmd, size_t cl, uint8_t *r, size_t rl)
{
    si_cmd(cmd, cl);
    if (!wait_cts()) return;
    i2c_cmd_handle_t h = i2c_cmd_link_create();
    i2c_master_start(h);
    i2c_master_write_byte(h, (CFG_SI_ADDR << 1) | 1, true);
    for (size_t i = 0; i < rl - 1; i++) {
        i2c_master_read_byte(h, &r[i], I2C_MASTER_ACK);
    }
    i2c_master_read_byte(h, &r[rl - 1], I2C_MASTER_NACK);
    i2c_master_stop(h);
    i2c_master_cmd_begin(I2C_NUM_0, h, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(h);
}

static bool wait_stc(void)
{
    for (int i = 0; i < 200; i++) {
        uint8_t cmd = SI_CMD_GET_INT_STATUS;
        uint8_t r[1] = {0};
        si_read(&cmd, 1, r, 1);
        if (r[0] & 0x01) return true;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    ESP_LOGE(TAG, "STC zamansimi!");
    return false;
}

static void hard_reset(void)
{
    gpio_set_level(CFG_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(CFG_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
}

static void print_rev(void)
{
    uint8_t rc[1] = {SI_CMD_GET_REV};
    uint8_t rv[9] = {0};
    si_read(rc, 1, rv, 9);
    ESP_LOGI(TAG, "Chip: Si47%02d FW:%c.%c", rv[1], rv[2], rv[3]);
}

// ==================== TEMEL API ====================

esp_err_t si4721_init(void)
{
    i2c_config_t cfg = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = CFG_I2C_SDA,
        .scl_io_num       = CFG_I2C_SCL,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &cfg));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));
    gpio_set_direction(CFG_RST_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(CFG_RST_PIN, 0);
    return ESP_OK;
}

esp_err_t si4721_power_down(void)
{
    uint8_t cmd[1] = {SI_CMD_POWER_DOWN};
    si_cmd(cmd, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "POWER_DOWN OK");
    return ESP_OK;
}

uint16_t si4721_get_property(uint16_t prop)
{
    uint8_t c[4] = {SI_CMD_GET_PROPERTY, 0x00, prop >> 8, prop & 0xFF};
    si_cmd(c, 4);
    if (!wait_cts()) return 0xFFFF;
    uint8_t r[4] = {0};
    i2c_cmd_handle_t h = i2c_cmd_link_create();
    i2c_master_start(h);
    i2c_master_write_byte(h, (CFG_SI_ADDR << 1) | 1, true);
    for (int i = 0; i < 3; i++) i2c_master_read_byte(h, &r[i], I2C_MASTER_ACK);
    i2c_master_read_byte(h, &r[3], I2C_MASTER_NACK);
    i2c_master_stop(h);
    i2c_master_cmd_begin(I2C_NUM_0, h, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(h);
    return ((uint16_t)r[2] << 8) | r[3];
}

void si4721_set_property(uint16_t prop, uint16_t value)
{
    si_prop(prop, value);
}

// ==================== TX API ====================

esp_err_t si4721_tx_power_up(void)
{
    hard_reset();
    // ARG1=0x02: FUNC=TX, ARG2=0x0F: Dijital ses girisi, XOSCEN=0
    uint8_t pu[3] = {SI_CMD_POWER_UP, 0x02, 0x0F};
    si_cmd(pu, 3);
    vTaskDelay(pdMS_TO_TICKS(500));
    if (!wait_cts()) {
        ESP_LOGE(TAG, "TX POWER_UP basarisiz!");
        return ESP_FAIL;
    }
    print_rev();
    ESP_LOGI(TAG, "TX POWER_UP OK (ARG1=0x02, ARG2=0x0F)");
    return ESP_OK;
}

esp_err_t si4721_tx_configure(void)
{
    ESP_LOGI(TAG, "TX Configure basliyor...");

    uint8_t gc[2] = {SI_CMD_GPIO_CTL, 0x07};
    si_cmd(gc, 2);
    vTaskDelay(pdMS_TO_TICKS(10));

    si_prop(SI_PROP_REFCLK_FREQ, 32768);
    si_prop(SI_PROP_REFCLK_PRESCALE, 1);
    si_prop(SI_PROP_DIGITAL_INPUT_FORMAT, 0x0000);
    si_prop(SI_PROP_TX_COMPONENT_ENABLE, CFG_COMPONENT);
    si_prop(SI_PROP_TX_AUDIO_DEVIATION, CFG_DEVIATION);
    si_prop(SI_PROP_TX_PREEMPHASIS, CFG_PREEMPHASIS);

    ESP_LOGI(TAG, "TX TUNE: %d.%02d MHz", CFG_TUNE_FREQ / 100, CFG_TUNE_FREQ % 100);
    uint8_t tn[4] = {SI_CMD_TX_TUNE_FREQ, 0x00, CFG_TUNE_FREQ >> 8, CFG_TUNE_FREQ & 0xFF};
    si_cmd(tn, 4);
    if (!wait_stc()) return ESP_FAIL;
    ESP_LOGI(TAG, "TX frekansa kilitlenildi!");

    si_prop(SI_PROP_DIGITAL_INPUT_SAMPLE_RATE, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    si_prop(SI_PROP_DIGITAL_INPUT_SAMPLE_RATE, CFG_SAMPLE_RATE);
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "TX POWER: %d dBuV", CFG_TX_POWER);
    uint8_t pw[5] = {SI_CMD_TX_TUNE_POWER, 0x00, 0x00, CFG_TX_POWER, 0x00};
    si_cmd(pw, 5);
    if (!wait_stc()) return ESP_FAIL;

    si_prop(SI_PROP_TX_ACOMP_ENABLE, 0x0000);
    ESP_LOGI(TAG, "TX Configure tamamlandi.");
    return ESP_OK;
}

int8_t si4721_tx_get_inlevel(void)
{
    uint8_t cmd[2] = {SI_CMD_TX_ASQ_STATUS, 0x00};
    uint8_t resp[5] = {0};
    si_read(cmd, 2, resp, 5);
    return (int8_t)resp[4];
}

esp_err_t si4721_retune(uint16_t freq, uint8_t power)
{
    ESP_LOGI(TAG, "Retune: %d.%02d MHz, %d dBuV", freq / 100, freq % 100, power);
    si_prop(SI_PROP_DIGITAL_INPUT_SAMPLE_RATE, 0);
    vTaskDelay(pdMS_TO_TICKS(50));

    uint8_t tn[4] = {SI_CMD_TX_TUNE_FREQ, 0x00, freq >> 8, freq & 0xFF};
    si_cmd(tn, 4);
    if (!wait_stc()) return ESP_FAIL;

    si_prop(SI_PROP_DIGITAL_INPUT_SAMPLE_RATE, CFG_SAMPLE_RATE);
    vTaskDelay(pdMS_TO_TICKS(50));

    uint8_t pw[5] = {SI_CMD_TX_TUNE_POWER, 0x00, 0x00, power, 0x00};
    si_cmd(pw, 5);
    if (!wait_stc()) return ESP_FAIL;
    ESP_LOGI(TAG, "Retune basarili!");
    return ESP_OK;
}

// ==================== RX API ====================

esp_err_t si4721_rx_power_up(void)
{
    hard_reset();
    // Spec: ARG1=0xC0 (CTSIEN+GPO2OEN+FUNC=RX), ARG2=0xB0 (Dijital+Analog)
    // Donanim notu: eger 0xC0 CTS timeout verirse 0x00 dene
    uint8_t pu[3] = {SI_CMD_POWER_UP, 0xC0, 0xB0};
    si_cmd(pu, 3);
    vTaskDelay(pdMS_TO_TICKS(500));
    if (!wait_cts()) {
        // Fallback: minimal argumenlerle tekrar dene
        ESP_LOGW(TAG, "RX POWER_UP 0xC0 basarisiz, 0x00 ile deneniyor...");
        hard_reset();
        uint8_t pu2[3] = {SI_CMD_POWER_UP, 0x00, 0xB0};
        si_cmd(pu2, 3);
        vTaskDelay(pdMS_TO_TICKS(500));
        if (!wait_cts()) {
            ESP_LOGE(TAG, "RX POWER_UP tamamen basarisiz!");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "RX POWER_UP OK (fallback ARG1=0x00)");
    } else {
        ESP_LOGI(TAG, "RX POWER_UP OK (ARG1=0xC0)");
    }
    print_rev();
    return ESP_OK;
}

esp_err_t si4721_rx_configure(uint16_t freq)
{
    ESP_LOGI(TAG, "RX Configure basliyor...");

    si_prop(SI_PROP_REFCLK_FREQ, 32768);
    si_prop(SI_PROP_REFCLK_PRESCALE, 1);

    // Dijital ses cikis ayarlari (spec sirasi)
    si_prop(SI_PROP_DIGITAL_OUTPUT_SAMPLE_RATE, CFG_SAMPLE_RATE); // 0x0104=0xBB80
    si_prop(SI_PROP_DIGITAL_OUTPUT_FORMAT, 0x0000);                // 0x0102=I2S
    ESP_LOGI(TAG, "  I2S Output: %d Hz, I2S format", CFG_SAMPLE_RATE);

    // Anten ve ses ayarlari
    si_prop(SI_PROP_FM_ANTENNA_INPUT, 0x0000);   // 0x1107=FMI
    si_prop(SI_PROP_FM_DEEMPHASIS, 0x0001);       // 0x1100=50us
    si_prop(SI_PROP_RX_VOLUME, 0x003F);            // 0x4000=max
    si_prop(SI_PROP_RX_HARD_MUTE, 0x0000);         // 0x4001=off

    // KRITIK: Soft Mute iptal — sinyal zayifken dijital sessizligi engelle
    si_prop(0x1302, 0x0000);  // FM_SOFT_MUTE_MAX_ATTENUATION = 0 (kapali)
    ESP_LOGI(TAG, "  Soft Mute: KAPALI (cizirtiyi I2S'e basacak)");

    // Frekansa kilitlen
    ESP_LOGI(TAG, "RX TUNE: %d.%02d MHz", freq / 100, freq % 100);
    uint8_t tn[5] = {SI_CMD_FM_TUNE_FREQ, 0x00, freq >> 8, freq & 0xFF, 0x00};
    si_cmd(tn, 5);
    if (!wait_stc()) return ESP_FAIL;

    // INTACK: tune status oku ve kesmeyi temizle
    uint8_t ts[2] = {SI_CMD_FM_TUNE_STATUS, 0x01};
    uint8_t tr[8] = {0};
    si_read(ts, 2, tr, 8);
    ESP_LOGI(TAG, "RX kilitlendi! RSSI=%d dBuV, SNR=%d dB, Freq=%d.%02d MHz",
             (int8_t)tr[4], tr[5], ((tr[2]<<8)|tr[3])/100, ((tr[2]<<8)|tr[3])%100);

    ESP_LOGI(TAG, "RX Configure tamamlandi.");
    return ESP_OK;
}

int8_t si4721_rx_get_rssi(void)
{
    uint8_t cmd[2] = {SI_CMD_FM_RSQ_STATUS, 0x00};
    uint8_t resp[8] = {0};
    si_read(cmd, 2, resp, 8);
    return (int8_t)resp[4];
}

uint8_t si4721_rx_get_snr(void)
{
    uint8_t cmd[2] = {SI_CMD_FM_RSQ_STATUS, 0x00};
    uint8_t resp[8] = {0};
    si_read(cmd, 2, resp, 8);
    return resp[5];
}

// ==================== RDS API (TX only) ====================

esp_err_t si4721_rds_init(void)
{
    uint16_t comp = CFG_COMPONENT | 0x0004;
    si_prop(SI_PROP_TX_COMPONENT_ENABLE, comp);
    si_prop(SI_PROP_TX_RDS_DEVIATION, CFG_RDS_DEVIATION);
    si_prop(SI_PROP_TX_RDS_PI, CFG_RDS_PI);
    si_prop(SI_PROP_TX_RDS_PS_MIX, 0x0003);
    si_prop(SI_PROP_TX_RDS_PS_MISC, 0x1008);
    si_prop(SI_PROP_TX_RDS_PS_REPEAT_COUNT, 0x0003);
    si_prop(SI_PROP_TX_RDS_PS_MESSAGE_COUNT, 0x0001);
    si_prop(SI_PROP_TX_RDS_FIFO_SIZE, 0x0000);
    si4721_rds_set_ps(CFG_RDS_PS_DEFAULT);
    ESP_LOGI(TAG, "RDS aktif. PI=0x%04X", CFG_RDS_PI);
    return ESP_OK;
}

esp_err_t si4721_rds_set_ps(const char *text)
{
    char padded[8];
    memset(padded, ' ', 8);
    size_t len = strlen(text);
    if (len > 8) len = 8;
    memcpy(padded, text, len);

    uint8_t ps0[6] = {SI_CMD_TX_RDS_PS, 0x00, padded[0], padded[1], padded[2], padded[3]};
    si_cmd(ps0, 6);
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t ps1[6] = {SI_CMD_TX_RDS_PS, 0x01, padded[4], padded[5], padded[6], padded[7]};
    si_cmd(ps1, 6);
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "RDS PS: \"%.8s\"", padded);
    return ESP_OK;
}

esp_err_t si4721_rds_set_rt(const char *text)
{
    size_t len = strlen(text);
    if (len > 64) len = 64;

    for (size_t i = 0; i < len; i += 4) {
        char block[4] = {' ', ' ', ' ', ' '};
        for (size_t j = 0; j < 4 && (i + j) < len; j++) {
            block[j] = text[i + j];
        }
        uint8_t flags = (i == 0) ? 0x07 : 0x03;
        uint8_t cmd[8] = {SI_CMD_TX_RDS_BUFF, flags, 0x20, 0x00, (uint8_t)(i/4), 0x00, block[0], block[1]};
        si_cmd(cmd, 8);
        vTaskDelay(pdMS_TO_TICKS(5));

        uint8_t cmd2[8] = {SI_CMD_TX_RDS_BUFF, 0x03, 0x20, 0x00, (uint8_t)(i/4), 0x00, block[2], block[3]};
        si_cmd(cmd2, 8);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    ESP_LOGI(TAG, "RDS RT: \"%.*s\"", (int)len, text);
    return ESP_OK;
}
