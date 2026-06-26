#pragma once

#include "esp_err.h"
#include <stdint.h>

// ==================== KOMUT KODLARI ====================
#define SI_CMD_POWER_UP         0x01
#define SI_CMD_GET_REV          0x10
#define SI_CMD_POWER_DOWN       0x11
#define SI_CMD_SET_PROPERTY     0x12
#define SI_CMD_GET_PROPERTY     0x13
#define SI_CMD_GET_INT_STATUS   0x14
#define SI_CMD_FM_TUNE_FREQ     0x20
#define SI_CMD_FM_TUNE_STATUS   0x22
#define SI_CMD_FM_RSQ_STATUS    0x23
#define SI_CMD_TX_TUNE_FREQ     0x30
#define SI_CMD_TX_TUNE_POWER    0x31
#define SI_CMD_TX_ASQ_STATUS    0x34
#define SI_CMD_TX_RDS_BUFF      0x35
#define SI_CMD_TX_RDS_PS        0x36
#define SI_CMD_GPIO_CTL         0x80

// ==================== TX PROPERTY ADRESLERI ====================
#define SI_PROP_DIGITAL_INPUT_FORMAT        0x0101
#define SI_PROP_DIGITAL_INPUT_SAMPLE_RATE   0x0103
#define SI_PROP_REFCLK_FREQ                 0x0201
#define SI_PROP_REFCLK_PRESCALE             0x0202
#define SI_PROP_TX_COMPONENT_ENABLE         0x2100
#define SI_PROP_TX_AUDIO_DEVIATION          0x2101
#define SI_PROP_TX_PREEMPHASIS              0x2106
#define SI_PROP_TX_ACOMP_ENABLE             0x2200
#define SI_PROP_TX_RDS_DEVIATION            0x2103
#define SI_PROP_TX_RDS_PI                   0x2C01
#define SI_PROP_TX_RDS_PS_MIX              0x2C02
#define SI_PROP_TX_RDS_PS_MISC             0x2C03
#define SI_PROP_TX_RDS_PS_REPEAT_COUNT     0x2C04
#define SI_PROP_TX_RDS_PS_MESSAGE_COUNT    0x2C05
#define SI_PROP_TX_RDS_FIFO_SIZE           0x2C07

// ==================== RX PROPERTY ADRESLERI ====================
#define SI_PROP_DIGITAL_OUTPUT_FORMAT       0x0102
#define SI_PROP_DIGITAL_OUTPUT_SAMPLE_RATE  0x0104
#define SI_PROP_FM_DEEMPHASIS               0x1100
#define SI_PROP_FM_ANTENNA_INPUT            0x1107
#define SI_PROP_RX_VOLUME                   0x4000
#define SI_PROP_RX_HARD_MUTE                0x4001

// ==================== TEMEL API ====================
esp_err_t si4721_init(void);
esp_err_t si4721_power_down(void);
uint16_t  si4721_get_property(uint16_t prop);
void      si4721_set_property(uint16_t prop, uint16_t value);

// ==================== TX API ====================
esp_err_t si4721_tx_power_up(void);
esp_err_t si4721_tx_configure(void);
int8_t    si4721_tx_get_inlevel(void);
esp_err_t si4721_retune(uint16_t freq, uint8_t power);

// ==================== RX API ====================
esp_err_t si4721_rx_power_up(void);
esp_err_t si4721_rx_configure(uint16_t freq);
int8_t    si4721_rx_get_rssi(void);
uint8_t   si4721_rx_get_snr(void);

// ==================== RDS API (TX only) ====================
esp_err_t si4721_rds_init(void);
esp_err_t si4721_rds_set_ps(const char *text);
esp_err_t si4721_rds_set_rt(const char *text);
