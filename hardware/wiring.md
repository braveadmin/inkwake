# Wiring

Pin numbers are assigned at M0 once the board is picked. All loads that draw current in sleep sit behind Q1.

| Signal | Board pin | Peripheral |
| --- | --- | --- |
| SPI SCK / MOSI | TBD | e-paper CLK / DIN |
| EPD CS / DC / RST / BUSY | TBD | e-paper control |
| I2C SDA / SCL | TBD | SHT40, RV-3028 |
| RTC INT | TBD (RTC-capable GPIO) | RV-3028 INT, optional wake source |
| Touch pad top | TBD (touch pin) | copper pad, snooze/dismiss |
| Touch pad side | TBD (touch pin) | copper pad, glow / web UI |
| I2S BCLK / LRCLK / DOUT | TBD | MAX98357A BCLK / LRC / DIN |
| AMP SD | TBD | MAX98357A shutdown |
| LED data | TBD | SK6812 DIN (via 330 ohm) |
| RAIL EN | TBD | Q1 gate: LED ring + amp power |
| Buzzer | TBD | piezo |
| BAT | board battery input | LiPo via JST-PH |
| VBAT sense | board ADC (if exposed) | battery level |
