# LEXIN — Voice-Controlled Smart Assistant

A voice-controlled embedded assistant built on the ESP32-S3. Saying the wake
phrase **"Hi Lexin"** triggers a 3-second recording, which is sent to Wit.ai
for intent classification. Depending on the recognized command, the device
switches on a light, displays an animated heart on an LED matrix, or sets a
mood (red light + music playback), with live status shown on an OLED display.

## Hardware

- ESP32-S3 (WROOM-1)
- MAX4466 electret microphone breakout (with RC low-pass filter)
- MAX7219 8x8 LED matrix
- SSD1306 OLED, 128x64 (I2C)
- MP3-TF-16P (DFPlayer Mini) + TPA3116D2 amplifier + speaker
- White LED, red LED, push button

## Built With

This project is built on **ESP-IDF** (Espressif's official SDK/framework)
and the following components:

| Dependency | Purpose | Origin |
|---|---|---|
| [ESP-IDF](https://github.com/espressif/esp-idf) | Core framework — FreeRTOS, drivers (GPIO, UART, I2C, ADC), WiFi, HTTP client | Espressif |
| [`espressif/esp-sr`](https://components.espressif.com/components/espressif/esp-sr) | On-device wake-word detection (WakeNet, "Hi Lexin" model) | Espressif |
| [`k0i05/esp_ssd1306`](https://components.espressif.com/components/k0i05/esp_ssd1306) | SSD1306 OLED I2C driver | k0i05 |
| [Wit.ai](https://wit.ai/) | Cloud speech-to-intent classification (API) | Meta |

See `idf_component.yml` for exact versions.

## Original Work

The application logic, system integration, and the following are original,
written from datasheets/protocol specs with no external library:

- Wake-word → record → Wit.ai → intent-handling state machine
- MAX7219 driver (bit-banged, manual SPI-style framing)
- DFPlayer Mini UART command protocol implementation
- Wit.ai HTTP client integration and JSON intent parsing
- All pin assignments, power system design, and OLED status flow

## Building

```bash
idf.py build
idf.py -p <PORT> flash monitor
```

Requires a `WIT_TOKEN`, `WIFI_SSID`, and `WIFI_PASS` filled in at the top of
`main.c` before building (see comments in source — do not commit real
credentials).

## License

*(add your chosen license here, e.g. MIT — see LICENSE)*
