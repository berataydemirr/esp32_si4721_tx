#pragma once

// ==================== I2S PIN (TX Only) ====================
#define CFG_I2S_BCLK        20      // Si4721 DCLK (Pin 17)
#define CFG_I2S_WS          21      // Si4721 DFS  (Pin 14)
#define CFG_I2S_DOUT        22      // Si4721 DIN  (Pin 13)

// ==================== I2C + KONTROL ====================
#define CFG_I2C_SDA         7
#define CFG_I2C_SCL         8
#define CFG_RST_PIN         4

// ==================== SI4721 TX AYARLARI ====================
#define CFG_SI_ADDR         0x11
#define CFG_TUNE_FREQ       9010
#define CFG_TX_POWER        90
#define CFG_DEVIATION       6825
#define CFG_PREEMPHASIS     0x0001
#define CFG_COMPONENT       0x0006

// ==================== RDS ====================
#define CFG_RDS_PI          0x1234
#define CFG_RDS_PS_DEFAULT  "AMBULANS"
#define CFG_RDS_DEVIATION   200

// ==================== AUDIO ====================
#define CFG_SAMPLE_RATE     48000
#define CFG_WAV_FILE        "/sdcard/ambulans.wav"

// ==================== TERMINAL ====================
#define CFG_UART_BUF_SIZE   256

// ==================== DEBUG ====================
#define CFG_LOG_INTERVAL_MS 5000
#define CFG_INLEVEL_ALARM   -90
