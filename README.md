# ESP32 PNG Animation

Axiometa Studio project — ST7735 TFT display animation player for the Genesis Mini board.

## Setup

### 1. Install the missing library

The cloud build environment is missing **PNGdec**. Install it in Arduino IDE:

> **Sketch → Include Library → Manage Libraries** → search `PNGdec` → install **PNGdec by Larry Bank**

Or via arduino-cli:
```bash
arduino-cli lib install "PNGdec"
```

### 2. Board & FQBN

```
esp32:esp32:axiometa_genesis_mini
```

Install the Axiometa board package if you haven't: add this URL to Arduino IDE Additional Boards Manager URLs:
```
https://axiometa.ai/package_axiometa_index.json
```

### 3. Build & flash

```bash
mkdir -p sketch && cp firmware/* sketch/
arduino-cli compile --fqbn esp32:esp32:axiometa_genesis_mini --output-dir build sketch
```

Then flash `build/sketch.ino.merged.bin` at offset `0x0`.

### 4. WiFi credentials

Replace `%%WIFI_SSID%%` and `%%WIFI_PASSWORD%%` in `firmware/sketch.ino` with your actual credentials before building locally.

## Usage

1. Flash the board — the TFT shows the board's IP address.
2. Open that IP in a browser.
3. Upload `frame000.png`, `frame001.png`, … (160×80 px PNG files).
4. Press **Play**.

## Hardware

| Module | Port |
|--------|------|
| ST7735 IPS TFT Display (AX22-0034) | P1 |
