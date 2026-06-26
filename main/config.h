#pragma once

// ==================== I2S PIN TANIMLAMALARI ====================
// Ortak hatlar (ESP32 her zaman master)
#define CFG_I2S_BCLK        20      // Si4721 DCLK (Pin 17)
#define CFG_I2S_WS          21      // Si4721 DFS  (Pin 14+16 birlesik)
// Ayri data hatlari
#define CFG_I2S_DOUT        22      // ESP32 → Si4721 DIN  (Pin 13) TX modu
#define CFG_I2S_DIN         23      // Si4721 DOUT (Pin 15) → ESP32  RX modu

// ==================== I2C + KONTROL ====================
#define CFG_I2C_SDA         7
#define CFG_I2C_SCL         8
#define CFG_RST_PIN         4
#define CFG_PA_CTRL_PIN     53      // NS4150B amplifikator enable

// ==================== SI4721 AYARLARI ====================
#define CFG_SI_ADDR         0x11
#define CFG_TUNE_FREQ       9010
#define CFG_TX_POWER        90
#define CFG_DEVIATION       6825
#define CFG_PREEMPHASIS     0x0001
#define CFG_COMPONENT       0x0006

// ==================== RDS AYARLARI ====================
#define CFG_RDS_PI          0x1234
#define CFG_RDS_PTY         0
#define CFG_RDS_PS_DEFAULT  "FM TX"
#define CFG_RDS_DEVIATION   200

// ==================== AUDIO AYARLARI ====================
#define CFG_SAMPLE_RATE     48000
#define CFG_WAV_FILE        "/sdcard/audio.wav"

// ==================== UART AYARLARI ====================
#define CFG_UART_BUF_SIZE   256

// ==================== DEBUG AYARLARI ====================
#define CFG_LOG_INTERVAL_MS 5000
#define CFG_INLEVEL_ALARM   -90
