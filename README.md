# ESP32-P4 FM Transmitter

ESP32-P4-NANO and Si4721 based FM transmitter with SD card WAV playback, RDS support, and interactive terminal.

## Hardware

| Component | Model |
|-----------|-------|
| MCU | ESP32-P4-NANO (RISC-V, ESP-IDF v5.5.4) |
| RF Chip | Si4721 FM Transceiver Breakout (TX mode only) |
| SD Card | Onboard TF/SD slot (SDMMC 4-wire) |

## Pin Map

| ESP32 GPIO | Si4721 Pin | Function |
|-----------|-----------|----------|
| GPIO 20 | Pin 17 (DCLK) | I2S Bit Clock |
| GPIO 21 | Pin 14 (DFS) | I2S Word Select |
| GPIO 22 | Pin 13 (DIN) | I2S Data (ESP32 -> Si4721) |
| GPIO 7 | SDA | I2C Data |
| GPIO 8 | SCL | I2C Clock |
| GPIO 4 | RST | Chip Reset |

## Project Structure

```
main/
  config.h              All settings in one file
  config_manager.h/c    Runtime config from SD card
  si4721.h/c            Si4721 TX driver + RDS
  i2s_audio.h/c         I2S TX audio task
  sd_player.h/c         SD card WAV reader
  sd_logger.h/c         Crash-safe file logger
  uart_cmd.h/c          USB-CDC interactive terminal
  main.c                Init flow and monitoring
```

## Terminal Commands

| Command | Description |
|---------|-------------|
| `PLAY:file.wav` | Play WAV from SD card |
| `STOP` / `RESUME` | Stop / resume playback |
| `MSG:text` | Set RDS station name (8 chars) |
| `RT:text` | Set RDS RadioText (64 chars) |
| `STATUS` | System status |
| `RELOAD_CONFIG` | Reload settings.txt |
| `PAUSELOG` / `RESUMELOG` | Toggle log output |
| `CLS` / `HELP` | Clear screen / help |

## SD Card: settings.txt

```
frequency=9010
tx_power=90
deviation=6825
preemphasis=50us
rds_ps=FM TX
rds_rt=ESP32 FM TRANSMITTER
playlist=audio.wav
autoplay=true
```

## Build

```bash
idf.py set-target esp32p4
idf.py build
idf.py -p COM3 flash monitor
```

## TX Init Sequence

1. I2S start (before POWER_UP!)
2. `POWER_UP [0x01, 0xC2, 0x50]`
3. `GPIO_CTL [0x80, 0x07]`
4. REFCLK 32768 Hz
5. Digital Input Format: I2S
6. Component + Deviation + Pre-emphasis
7. `TX_TUNE_FREQ` -> wait STC
8. Sample Rate: 48000 (after tune!)
9. `TX_TUNE_POWER` -> wait STC
10. RDS init

## License

MIT License
