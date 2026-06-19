<p align="center">
  <img src="https://img.shields.io/badge/ESP--IDF-v5.x-blue?style=flat-square&logo=espressif" />
  <img src="https://img.shields.io/badge/MCU-ESP32--P4-orange?style=flat-square" />
  <img src="https://img.shields.io/badge/RF-Si4721-critical?style=flat-square" />
  <img src="https://img.shields.io/badge/Band-FM%2087.5--108%20MHz-green?style=flat-square" />
</p>

<h1 align="center">📡 ESP32-P4 FM Transmitter</h1>

<p align="center">
  <b>Turn an ESP32-P4 into a real FM radio station using the Si4721 RF chip.</b><br>
  Synthesize audio in RAM, stream it over I2S, and broadcast on any FM frequency.
</p>

---

## Overview

This project uses an **ESP32-P4 Nano** as an FM transmitter by interfacing with a **Si4721** FM transceiver breakout board. Audio is generated entirely in firmware — no SD card, no external audio source needed. The ESP32 synthesizes a melody in real time, feeds it to the Si4721 over **I2S**, and the chip modulates it onto an FM carrier wave that any standard FM radio can pick up.

**Two communication protocols work together:**

| Protocol | Role | Purpose |
|:--------:|:----:|:--------|
| **I2C** | Control plane | Configure the Si4721 — set frequency, power, audio format, compression |
| **I2S** | Data plane | Stream 48 kHz / 32-bit stereo PCM audio samples to the chip |

## Architecture

```
┌─────────────┐    I2C (100 kHz)     ┌────────────┐
│             │──────────────────────▶│            │
│  ESP32-P4   │                      │  Si4721    │───── ANT )))  FM
│  Nano       │  I2S (48 kHz/32-bit) │  Breakout  │      📻
│             │──────────────────────▶│            │
└─────────────┘                      └────────────┘
   Firmware:                            RF output:
   - Sine wave synthesis                - 87.5–108.0 MHz
   - Attack/release envelope            - Up to 120 dBμV
   - ACOMP (dynamic compression)        - Mono / Stereo
```

## Pin Mapping

| ESP32-P4 GPIO | Si4721 J3 Pin | Function |
|:-------------:|:-------------:|:---------|
| GPIO 7 | Pin 17 (SDIO) | I2C SDA |
| GPIO 8 | Pin 16 (SCLK) | I2C SCL |
| GPIO 4 | Pin 14 (RST) | Hardware Reset |
| GPIO 20 | Pin 18 (RCLK) | I2S Bit Clock (BCLK) |
| GPIO 21 | Pin 7 (DFS) | I2S Word Select (WS) |
| GPIO 22 | Pin 9 (DIN) | I2S Data Out |
| GPIO 3 | 3.3V | I2C mode select (tied high) |

## Quick Start

**Prerequisites:** [ESP-IDF v5.x](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/) installed and configured.

```bash
# Clone
git clone https://github.com/berataydemirr/esp32_si4721_tx_rx_project.git
cd esp32_si4721_tx_rx_project

# Set your target
idf.py set-target esp32p4

# Build, flash, and monitor
idf.py build flash monitor
```

Tune any FM radio to the configured frequency (default: **90.10 MHz**) and you should hear the melody playing.

## Configuration

All tunable parameters are defined at the top of [`main/main.c`](main/main.c):

```c
#define TUNE_FREQ    9010      // FM frequency (MHz × 100)
#define SAMPLE_RATE  48000     // Audio sample rate (Hz)
#define NOTE_MS      200       // Note duration (ms)
#define AMP          24000.0f  // Output amplitude (0–32000)
```

**Transmit power** is set during Si4721 initialization (default: 115 dBμV, range: 88–120).

## How It Works

1. **I2C init** — ESP32 becomes I2C master at 100 kHz
2. **I2S init** — Configured as 48 kHz, 32-bit stereo, Philips standard
3. **Si4721 init** — Hardware reset → power up in FM TX mode → tune to frequency → set power → configure digital audio input → enable ACOMP
4. **Melody loop** — Each note is a sine wave with a short attack/release envelope to prevent clicks, synthesized into a RAM buffer and streamed over I2S

## Troubleshooting

I ran into most of these issues during development. Sharing them here so you don't have to.

### Hardware

| Symptom | Likely cause | Fix |
|:--------|:-------------|:----|
| Si4721 not responding at all | SEN pin floating — chip doesn't know whether to use I2C or SPI | Tie **GPIO3 (SEN)** to **3.3V** through a 10kΩ pull-up for I2C mode |
| I2C works sometimes, fails randomly | Missing or weak pull-up resistors on SDA/SCL | Add **4.7kΩ external pull-ups** to 3.3V on both lines. Internal pull-ups alone are unreliable at 100 kHz |
| Chip resets or behaves erratically | Insufficient decoupling on VDD | Place a **100nF ceramic cap** as close to the Si4721 VDD pin as possible |
| No RF output despite correct logs | Antenna connected to **RF IN** instead of **TX/ANT** pad | RF IN is the *receiver* input. Use the **TX** antenna pad for transmitting |
| Weak signal / short range | Antenna too short or wrong impedance | Use a **~75 cm wire** (quarter-wave at FM band). Even 20 cm helps for bench testing |

> **Before writing any code**, verify your hardware with a multimeter:
> - 3.3V on VDD and SEN
> - SDA/SCL idle high (~3.3V)
> - RST goes low then high on boot
> - BCLK, WS, DOUT toggling (use an oscilloscope if available)

### I2C (Control Bus)

| Symptom | Likely cause | Fix |
|:--------|:-------------|:----|
| `CTS timeout` — chip never reports ready | Wrong I2C address. Si4721 uses **0x11** when SEN is high, **0x63** when low | Confirm SEN wiring matches your `SI4721_ADDR` define |
| `ACK` failures on every transaction | Bus contention or incorrect pin assignment | Double-check SDA/SCL pin numbers. Run an I2C scan to see what's on the bus |
| `NACK` after POWER_UP command | Chip not fully booted after reset | Increase the delay after RST goes high — **100ms minimum**, some boards need 200ms |

### I2S (Audio Bus)

| Symptom | Likely cause | Fix |
|:--------|:-------------|:----|
| Silence — no audio at all | I2S clock not running or wrong format | Si4721 expects **Philips (I2S) standard**, MSB-first, 32-bit slots. Verify `I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG` is used |
| Distorted or garbled audio | Sample rate mismatch between I2S and Si4721 property `0x0103` | Both must match — default is **48000 Hz** (`0xBB80`) |
| Audio plays too fast or too slow | BCLK frequency wrong | At 48 kHz / 32-bit / stereo, BCLK should be **~3.072 MHz**. The Si4721 needs ≥ 2 MHz |
| Clicks or pops between notes | No envelope on note transitions | Apply a short **attack/release ramp** (5–25 ms) to avoid discontinuities |

### Software

| Symptom | Likely cause | Fix |
|:--------|:-------------|:----|
| Task crash / stack overflow | Audio buffer allocated on the task stack | Always use `malloc()` for audio buffers. Keep task stack at **4096+** bytes |
| `ESP_ERR_TIMEOUT` on SD card init | SD card not inserted or wrong pins | If you're not using SD, remove all SDMMC/FATFS code and dependencies from `CMakeLists.txt` |
| Build fails with missing `fatfs` or `sdmmc` | Leftover dependencies in `CMakeLists.txt` | Component `REQUIRES` should only list `driver` if SD is not used |

## Project Structure

```
├── CMakeLists.txt          # Top-level ESP-IDF project file
├── main/
│   ├── CMakeLists.txt      # Component registration
│   └── main.c              # All firmware logic (single-file)
├── sdkconfig               # ESP-IDF configuration
└── README.md
```

## License

This project is open source and available under the [MIT License](LICENSE).
