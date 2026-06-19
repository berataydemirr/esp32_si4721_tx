// =====================================================================
//  ESP32-P4 + Si4721 FM transmitter
//  A melody is synthesized in RAM, streamed to the Si4721 over I2S, and
//  broadcast as an FM signal. Audio is boosted through the chip's ACOMP.
// =====================================================================
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

// ---------------- Pin map (ESP32 -> Si4721 J3 header) ----------------
#define SDA_PIN   7    // -> Pin 17 SDIO  (I2C data)
#define SCL_PIN   8    // -> Pin 16 SCLK  (I2C clock)
#define RST_PIN   4    // -> Pin 14 RST   (reset)
#define BCLK_PIN  20   // -> Pin 18 RCLK  (I2S bit clock)
#define WS_PIN    21   // -> Pin 7  DFS   (I2S word select)
#define DOUT_PIN  22   // -> Pin 9  DIN   (I2S data)
// Note: GPIO3 is tied high to 3.3V to select I2C control mode.

#define SI4721_ADDR  0x11
#define TUNE_FREQ    9010      // 90.10 MHz (MHz x 100). Tune your radio here.
#define SAMPLE_RATE  48000

// ==================== I2C layer ====================
static void i2c_init(void){
    i2c_config_t c={.mode=I2C_MODE_MASTER,.sda_io_num=SDA_PIN,.scl_io_num=SCL_PIN,
        .sda_pullup_en=GPIO_PULLUP_ENABLE,.scl_pullup_en=GPIO_PULLUP_ENABLE,
        .master.clk_speed=100000};
    i2c_param_config(I2C_NUM_0,&c);
    i2c_driver_install(I2C_NUM_0,I2C_MODE_MASTER,0,0,0);
}
// Wait until the chip reports Clear-To-Send (CTS).
static void wait_cts(void){
    uint8_t st=0; int n=0;
    while(!(st&0x80) && n++<100){
        i2c_cmd_handle_t h=i2c_cmd_link_create();
        i2c_master_start(h);
        i2c_master_write_byte(h,(SI4721_ADDR<<1)|I2C_MASTER_READ,true);
        i2c_master_read_byte(h,&st,I2C_MASTER_NACK);
        i2c_master_stop(h);
        i2c_master_cmd_begin(I2C_NUM_0,h,pdMS_TO_TICKS(10));
    }
}
// Send a command to the chip.
static void send_cmd(uint8_t *b,size_t n){
    wait_cts();
    i2c_cmd_handle_t h=i2c_cmd_link_create();
    i2c_master_start(h);
    i2c_master_write_byte(h,(SI4721_ADDR<<1)|I2C_MASTER_WRITE,true);
    i2c_master_write(h,b,n,true);
    i2c_master_stop(h);
    i2c_master_cmd_begin(I2C_NUM_0,h,pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(h);
}
// Read a response.
static void read_resp(uint8_t *b,size_t n){
    i2c_cmd_handle_t h=i2c_cmd_link_create();
    i2c_master_start(h);
    i2c_master_write_byte(h,(SI4721_ADDR<<1)|I2C_MASTER_READ,true);
    for(size_t i=0;i<n-1;i++) i2c_master_read_byte(h,&b[i],I2C_MASTER_ACK);
    i2c_master_read_byte(h,&b[n-1],I2C_MASTER_NACK);
    i2c_master_stop(h);
    i2c_master_cmd_begin(I2C_NUM_0,h,pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(h);
}
// Write a single property.
static void set_property(uint16_t p,uint16_t v){
    uint8_t c[6]={0x12,0x00,(p>>8),(p&0xFF),(v>>8),(v&0xFF)};
    send_cmd(c,6);
}

// ==================== I2S layer ====================
static void i2s_init(void){
    i2s_chan_config_t cc=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);
    i2s_new_channel(&cc,&tx_chan,NULL);
    // 48 kHz, 32-bit slots -> ~3.072 MHz bit clock (chip requires >= 2 MHz).
    i2s_std_config_t sc={
        .clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,I2S_SLOT_MODE_STEREO),
        .gpio_cfg={.mclk=I2S_GPIO_UNUSED,.bclk=BCLK_PIN,.ws=WS_PIN,.dout=DOUT_PIN,.din=I2S_GPIO_UNUSED},
    };
    i2s_channel_init_std_mode(tx_chan,&sc);
    i2s_channel_enable(tx_chan);
}

// ==================== Si4721 setup ====================
static void si4721_init(void){
    // Hardware reset.
    gpio_set_direction(RST_PIN,GPIO_MODE_OUTPUT);
    gpio_set_level(RST_PIN,0); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(RST_PIN,1); vTaskDelay(pdMS_TO_TICKS(100));

    // Power up in FM TX mode with digital (I2S) audio input (0x0F).
    uint8_t pu[]={0x01,0x12,0x0F}; send_cmd(pu,3); vTaskDelay(pdMS_TO_TICKS(500));
    // Tune to the target frequency (must precede audio configuration).
    uint8_t tn[]={0x30,0x00,(TUNE_FREQ>>8),(TUNE_FREQ&0xFF)}; send_cmd(tn,4); vTaskDelay(pdMS_TO_TICKS(300));
    // Output power: 115 dBuV.
    uint8_t pw[]={0x31,0x00,0x00,115,0x00}; send_cmd(pw,5); vTaskDelay(pdMS_TO_TICKS(200));

    // Digital audio format.
    set_property(0x0101,0x0000);  // I2S, stereo, 16-bit
    set_property(0x0103,0xBB80);  // 48000 Hz
    set_property(0x2106,0x0001);  // 50us pre-emphasis (Europe)

    // Audio compression (ACOMP): lifts quiet audio above the noise floor.
    set_property(0x2200,0x0003);  // ACOMP + limiter enabled
    set_property(0x2204,0x0014);  // ACOMP gain: 20 dB (max)
    set_property(0x2201,0xFFD8);  // Threshold: -40 dBFS

    // Read back tune status.
    uint8_t ts[]={0x33,0x00}; send_cmd(ts,2); vTaskDelay(pdMS_TO_TICKS(10));
    uint8_t r[8]={0}; read_resp(r,8);
    ESP_LOGI(TAG,"Tuned: %.2f MHz, %d dBuV",((r[2]<<8)|r[3])/100.0f,r[5]);
}

// ==================== Melody (synthesized in RAM) ====================
// Note frequencies in Hz. Extend freely; each note is generated on the fly.
static const float MELODY[]={
    523.25f,659.25f,783.99f,880.00f,  // C  E  G  A
    783.99f,659.25f,587.33f,659.25f,  // G  E  D  E
    698.46f,880.00f,783.99f,698.46f,  // F  A  G  F
    659.25f,587.33f,523.25f,392.00f   // E  D  C  G (low)
};
static const int MELODY_LEN=sizeof(MELODY)/sizeof(MELODY[0]);
#define NOTE_MS  200      // note duration (smaller = faster)
#define AMP      24000.0f // output amplitude

// Synthesize one note in RAM and stream it over I2S.
static void play_note(float freq){
    int n=SAMPLE_RATE*NOTE_MS/1000;
    int32_t *buf=malloc(n*2*sizeof(int32_t));   // stereo
    int atk=SAMPLE_RATE*6/1000, rel=SAMPLE_RATE*25/1000;
    for(int i=0;i<n;i++){
        // Apply a short attack/release envelope to suppress clicks.
        float env=1.0f;
        if(i<atk)        env=(float)i/atk;
        if(i>n-rel)      env=(float)(n-i)/rel;
        float s=sinf(2.0f*M_PI*freq*i/SAMPLE_RATE)*AMP*env;
        int32_t v=((int32_t)s)<<16;             // sample in the upper 16 bits
        buf[i*2]=v; buf[i*2+1]=v;               // L = R
    }
    size_t w;
    i2s_channel_write(tx_chan,buf,n*2*sizeof(int32_t),&w,portMAX_DELAY);
    free(buf);
}

static void player_task(void *arg){
    int loop=0;
    while(1){
        for(int i=0;i<MELODY_LEN;i++) play_note(MELODY[i]);
        ESP_LOGI(TAG,"Melody loop #%d",++loop);
    }
}

// ==================== Entry point ====================
void app_main(void){
    i2c_init();
    i2s_init();
    si4721_init();
    ESP_LOGI(TAG,"Starting melody playback");
    xTaskCreate(player_task,"player",4096,NULL,5,NULL);
}
