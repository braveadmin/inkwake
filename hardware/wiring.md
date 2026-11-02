# Wiring: Waveshare ESP32-S3-RLCD-4.2

From Waveshare's pin definition image. KEY/PWR GPIOs and battery sensing still to confirm in the schematic.

## Used on board

| Function | GPIO |
| --- | --- |
| RLCD (ST7305): DS, TE, SCK, DIN, CS, RESET | 5, 6, 11, 12, 40, 41 |
| I2C (SHTC3, PCF85063) | SDA 13, SCL 14 |
| RTC interrupt | 15 |
| I2S (ES8311 + ES7210): DSDIN, SCLK, ASDOUT, MCLK, LRCK | 8, 9, 10, 16, 45 |
| Speaker amp enable (PA_CTRL) | 46 |
| TF card: CMD, CLK, DATA | 21, 38, 39 (card detect on 17 not connected) |
| BOOT button | 0 |

## 2x8 header allocation

| Header pin | Use |
| --- | --- |
| GPIO1 (touch) | top pad: snooze / dismiss |
| GPIO2 (touch) | side pad: glow / web UI |
| GPIO3 (touch) | spare: third pad or battery divider |
| GPIO17 | sunrise LEDs, PWM to MOSFET gate |
| GPIO18 | spare: boost enable for the optional RGBW ring (unless KEY uses it) |
| GPIO13 / 14 | shared I2C, optional external SHT40 |
| GPIO43 / 44 | UART0, spare |
| GPIO0, 19 / 20 | leave: BOOT and USB |
| 3V3, VBUS, GND | power; VBUS only present on USB |

LED + comes from the 18650 positive terminal (one soldered wire); LED - through Q1 to GND.
