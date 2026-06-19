#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"

static const char *TAG = "FM";
static i2s_chan_handle_t tx_chan;

// ===================== PINLER =====================
#define SDA_PIN   7    // → J3 Pin 17 (SDIO)
#define SCL_PIN   8    // → J3 Pin 16 (SCLK)
#define RST_PIN   4    // → J3 Pin 14 (RST)
#define BCLK_PIN  20   // → J3 Pin 18 (RCLK)
#define WS_PIN    21   // → J3 Pin 7  (DFS_IN)
#define DOUT_PIN  22   // → J3 Pin 9  (DIN)

#define SI4721_ADDR  0x11
#define TUNE_FREQ    9000      // 90.00 MHz. Radyoyu da 90.0'a ayarla!
#define SAMPLE_RATE  48000

// ===================== I2C =====================
static void i2c_init(void) {
    i2c_config_t conf = {
        .mode=I2C_MODE_MASTER, .sda_io_num=SDA_PIN, .scl_io_num=SCL_PIN,
        .sda_pullup_en=GPIO_PULLUP_ENABLE, .scl_pullup_en=GPIO_PULLUP_ENABLE,
        .master.clk_speed=100000,
    };
    i2c_param_config(I2C_NUM_0, &conf);
    i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
}
static void wait_cts(void) {
    uint8_t st=0; int n=0;
    while(!(st&0x80) && n++<100){
        i2c_cmd_handle_t h=i2c_cmd_link_create();
        i2c_master_start(h);
        i2c_master_write_byte(h,(SI4721_ADDR<<1)|I2C_MASTER_READ,true);
        i2c_master_read_byte(h,&st,I2C_MASTER_NACK);
        i2c_master_stop(h);
        i2c_master_cmd_begin(I2C_NUM_0,h,pdMS_TO_TICKS(10));
        i2c_cmd_link_delete(h); vTaskDelay(pdMS_TO_TICKS(5));
    }
}
static void send_cmd(uint8_t *buf, size_t len){
    wait_cts();
    i2c_cmd_handle_t h=i2c_cmd_link_create();
    i2c_master_start(h);
    i2c_master_write_byte(h,(SI4721_ADDR<<1)|I2C_MASTER_WRITE,true);
    i2c_master_write(h,buf,len,true);
    i2c_master_stop(h);
    i2c_master_cmd_begin(I2C_NUM_0,h,pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(h);
}
static void read_resp(uint8_t *buf, size_t len){
    i2c_cmd_handle_t h=i2c_cmd_link_create();
    i2c_master_start(h);
    i2c_master_write_byte(h,(SI4721_ADDR<<1)|I2C_MASTER_READ,true);
    for(size_t i=0;i<len-1;i++) i2c_master_read_byte(h,&buf[i],I2C_MASTER_ACK);
    i2c_master_read_byte(h,&buf[len-1],I2C_MASTER_NACK);
    i2c_master_stop(h);
    i2c_master_cmd_begin(I2C_NUM_0,h,pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(h);
}
static void set_property(uint16_t prop, uint16_t val){
    uint8_t cmd[6]={0x12,0x00,(prop>>8),(prop&0xFF),(val>>8),(val&0xFF)};
    send_cmd(cmd,6);
}

// ===================== I2S =====================
static void i2s_init(void){
    i2s_chan_config_t cc=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);
    i2s_new_channel(&cc,&tx_chan,NULL);
    i2s_std_config_t sc={
        .clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,I2S_SLOT_MODE_STEREO),
        .gpio_cfg={.mclk=I2S_GPIO_UNUSED,.bclk=BCLK_PIN,.ws=WS_PIN,.dout=DOUT_PIN,.din=I2S_GPIO_UNUSED},
    };
    i2s_channel_init_std_mode(tx_chan,&sc);
    i2s_channel_enable(tx_chan);
}

// ===================== SI4721 =====================
static void si4721_init(void){
    gpio_set_direction(RST_PIN,GPIO_MODE_OUTPUT);
    gpio_set_level(RST_PIN,0); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(RST_PIN,1); vTaskDelay(pdMS_TO_TICKS(100));

    uint8_t pu[]={0x01,0x12,0x0F}; send_cmd(pu,3); vTaskDelay(pdMS_TO_TICKS(500));
    uint8_t tn[]={0x30,0x00,(TUNE_FREQ>>8),(TUNE_FREQ&0xFF)}; send_cmd(tn,4); vTaskDelay(pdMS_TO_TICKS(300));
    uint8_t pw[]={0x31,0x00,0x00,115,0x00}; send_cmd(pw,5); vTaskDelay(pdMS_TO_TICKS(200));

    set_property(0x0101,0x0000);  // I2S stereo 16-bit
    set_property(0x0103,0xBB80);  // 48000 Hz
    set_property(0x2106,0x0001);  // pre-emphasis 50us

    // --- SES YUKSELTME (ACOMP) - gurultu ustune cikarir ---
    set_property(0x2200,0x0003);  // ACOMP + Limiter ACIK
    set_property(0x2204,0x0014);  // ACOMP kazanc = 20 dB (maksimum)
    set_property(0x2201,0xFFD8);  // Esik -40 dBFS

    uint8_t ts[]={0x33,0x00}; send_cmd(ts,2); vTaskDelay(pdMS_TO_TICKS(10));
    uint8_t r[8]={0}; read_resp(r,8);
    ESP_LOGI(TAG,"Kurulum: freq=%.2f MHz, guc=%d dBuV",((r[2]<<8)|r[3])/100.0f,r[5]);
}

// ===================== MELODI (kodda, RAM'de uretiliyor) =====================
// Notalar Hz cinsinden. Istedigin kadar uzatabilirsin, RAM derdi yok.
static const float MELODY[] = {
    523.25f, 659.25f, 783.99f, 880.00f,   // Do Mi Sol La
    783.99f, 659.25f, 587.33f, 523.25f    // Sol Mi Re Do
};
static const int MELODY_LEN = sizeof(MELODY)/sizeof(MELODY[0]);
#define NOTE_MS   220        // her notanin suresi (ms)
#define AMP       26000.0f   // ses genligi

// Bir notayi RAM tamponuna sentezleyip I2S'e yaz
static void play_note(float freq) {
    int samples = SAMPLE_RATE * NOTE_MS / 1000;   // bu notadaki ornek sayisi
    int32_t *buf = malloc(samples * 2 * sizeof(int32_t));  // stereo
    int atk = SAMPLE_RATE*6/1000;    // 6ms attack
    int rel = SAMPLE_RATE*25/1000;   // 25ms release (tik onler)

    for (int i = 0; i < samples; i++) {
        float env = 1.0f;
        if (i < atk)            env = (float)i/atk;
        if (i > samples-rel)    env = (float)(samples-i)/rel;
        float s = sinf(2.0f*M_PI*freq*i/SAMPLE_RATE) * AMP * env;
        int32_t v = ((int32_t)s) << 16;   // ses ust 16 bitte
        buf[i*2] = v; buf[i*2+1] = v;     // L = R
    }
    size_t w;
    i2s_channel_write(tx_chan, buf, samples*2*sizeof(int32_t), &w, portMAX_DELAY);
    free(buf);
}

static void player_task(void *arg) {
    int loop = 0;
    while (1) {
        for (int i = 0; i < MELODY_LEN; i++)
            play_note(MELODY[i]);
        ESP_LOGI(TAG, "Melodi tamamlandi, tekrar #%d", ++loop);
    }
}

void app_main(void) {
    i2c_init();
    i2s_init();
    si4721_init();
    ESP_LOGI(TAG,"===== MELODI BASLIYOR (RAM'den) =====");
    xTaskCreate(player_task,"player",4096,NULL,5,NULL);
}