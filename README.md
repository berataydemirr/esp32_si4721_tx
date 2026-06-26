# ESP32-P4 FM Transceiver

ESP32-P4-NANO and Si4721 based FM transceiver system with SD card WAV playback, RDS support, and runtime TX/RX mode switching.

## Hardware

| Component | Model |
|-----------|-------|
| MCU | ESP32-P4-NANO (RISC-V, ESP-IDF v5.5.4) |
| RF Chip | Si4721 FM Transceiver Breakout |
| SD Card | ESP32-P4-NANO onboard TF/SD slot (SDMMC slot 0) |
| Speaker | Onboard NS4150B Class-D amplifier |

## Pin Map

### I2S (Audio)

| ESP32 GPIO | Si4721 Pin | Function |
|-----------|-----------|----------|
| GPIO 20 | Pin 17 (DCLK) | I2S Bit Clock (ESP32 Master) |
| GPIO 21 | Pin 14+16 (DFS) | I2S Word Select (Pin 14 and 16 shorted) |
| GPIO 22 | Pin 13 (DIN) | TX mode: ESP32 -> Si4721 audio data |
| GPIO 23 | Pin 15 (DOUT) | RX mode: Si4721 -> ESP32 audio data |

### I2C (Control)

| ESP32 GPIO | Si4721 Pin | Function |
|-----------|-----------|----------|
| GPIO 7 | SDA | I2C Data |
| GPIO 8 | SCL | I2C Clock |
| GPIO 4 | RST | Chip Reset |

### SD Card (SDMMC Slot 0)

| GPIO | Function |
|------|----------|
| GPIO 43 | CLK |
| GPIO 44 | CMD |
| GPIO 39-42 | D0-D3 |

### Other

| GPIO | Function |
|------|----------|
| GPIO 53 | Speaker amplifier enable (NS4150B PA_CTRL) |
| Si4721 Pin 4 (TXO) | TX antenna |
| Si4721 Pin 2 (FMI) | RX antenna (via 120nH inductor) |

## Project Structure

```
esp32_tx_rx/
  CMakeLists.txt
  sdkconfig.defaults
  settings_example.txt
  README.md
  main/
    CMakeLists.txt
    config.h               # All pin and parameter definitions
    config_manager.h/c      # Runtime config from SD card
    si4721.h/c              # Si4721 driver (TX + RX + RDS)
    i2s_audio.h/c           # Bidirectional I2S audio bridge
    sd_player.h/c           # SD card WAV reader
    sd_logger.h/c           # Crash-safe file logger
    uart_cmd.h/c            # USB-CDC interactive terminal
    main.c                  # State machine and init flow
```

## Features

- **TX Mode:** Play WAV files from SD card over FM with RDS (PS + RadioText)
- **RX Mode:** Receive FM and passthrough to onboard speaker
- **Runtime Mode Switch:** Change between TX/RX without reboot via terminal
- **SD Card Config:** All parameters configurable via `settings.txt`
- **Interactive Terminal:** USB-CDC command interface with echo and backspace
- **Crash-Safe Logging:** All logs written to SD card with fsync
- **Power Protection:** Boot delay, LDO stabilization, retry mechanism for adapter power

## Terminal Commands

| Command | Description |
|---------|-------------|
| `MODE=TX` | Switch to transmitter mode |
| `MODE=RX` | Switch to receiver mode |
| `PLAY:file.wav` | Play WAV file from SD (TX mode) |
| `STOP` | Stop playback |
| `RESUME` | Resume playback |
| `MSG:text` | Set RDS PS text (max 8 chars) |
| `RT:text` | Set RDS RadioText (max 64 chars) |
| `STATUS` | System status report |
| `RELOAD_CONFIG` | Reload settings.txt from SD |
| `PAUSELOG / RESUMELOG` | Toggle log output |
| `CLS` | Clear terminal |
| `HELP` | Show command list |

## SD Card Files

### settings.txt

```
mode=TX
frequency=9010
tx_power=90
deviation=6825
preemphasis=50us
rds_ps=FM TX
rds_rt=ESP32 FM TRANSMITTER
playlist=audio.wav
autoplay=true
```

### WAV Files
- Format: 16-bit PCM
- Sample rate: 48000 Hz
- Channels: Mono or Stereo (stereo downmixed to mono)

## Build and Flash

```bash
idf.py set-target esp32p4
idf.py build
idf.py -p COM3 flash monitor
```

## Si4721 Init Sequences

### TX Mode
1. `POWER_UP [0x01, 0x02, 0x0F]`
2. `GPIO_CTL [0x80, 0x07]` (enable GPO3)
3. REFCLK 32768 Hz
4. Digital Input Format: I2S
5. Component Enable + Deviation + Pre-emphasis
6. `TX_TUNE_FREQ` -> wait STC
7. Digital Input Sample Rate: 48000 (AFTER tune!)
8. `TX_TUNE_POWER` -> wait STC

### RX Mode
1. `POWER_UP [0x01, 0xC0, 0xB0]` (0xB0 enables DOUT)
2. REFCLK 32768 Hz
3. Digital Output Sample Rate: 48000
4. Digital Output Format: I2S
5. Antenna Input: FMI
6. De-emphasis: 50us
7. Volume: max, Hard Mute: off
8. Soft Mute: disabled (0x1302 = 0x0000)
9. `FM_TUNE_FREQ` -> wait STC -> INTACK

## Critical Notes

1. I2S must start BEFORE Si4721 POWER_UP
2. `DIGITAL_INPUT_SAMPLE_RATE` must be set AFTER `TX_TUNE_FREQ`
3. Pin 14 and Pin 16 are hardware shorted (DFS shared for TX/RX)
4. XOSCEN=0 required (external crystal on RCLK)
5. `GPIO_CTL {0x80, 0x07}` required for digital audio
6. RX requires 0xB0 in POWER_UP ARG2 (enables DOUT)
7. Soft Mute must be disabled in RX mode
8. SD card powered by on-chip LDO channel 4
9. RX antenna (~75cm wire on FMI pin) required for signal reception

## Troubleshooting

| Issue | Cause | Fix |
|-------|-------|-----|
| INLEVEL = -96 | I2S not started before POWER_UP | Check init order in main.c |
| STC Timeout | Wrong REFCLK | Verify REFCLK_FREQ=32768 |
| SD mount fail | LDO not initialized | Check sd_pwr_ctrl_new_on_chip_ldo |
| Boot loop | Power supply insufficient | Lower TX_POWER, check brownout |
| RX samples = 0 | No antenna or missing 0xB0 | Add antenna to FMI, check POWER_UP |
| Terminal frozen | Logs too fast | Type PAUSELOG or increase LOG_INTERVAL |
| Config not loading | File descriptors full | Verify max_files >= 5 |
| Chip revision error | Stale build | idf.py fullclean && idf.py build |

## License

MIT License
