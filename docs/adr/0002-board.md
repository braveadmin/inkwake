# 0002. Board: low-power ESP32-S3 + Waveshare 4.2" e-paper

- Status: Proposed
- Date: 2026-11-02

## Decision

Low-power ESP32-S3 board with LiPo charger and N16R8 memory (Unexpected Maker FeatherS3 or DFRobot FireBeetle 2 ESP32-S3) driving a Waveshare 4.2" V2 e-paper (400x300) over SPI. Final pick at the M0 gate on measured sleep current and price.

## Alternatives rejected

M5Stack PaperS3; LilyGO T5 4.7" S3; generic ESP32-S3 DevKitC.

## Rationale

About EUR 20 cheaper than a PaperS3 build, all pins free, every part has several sources. DevKitC boards waste 5-10 mA in sleep (regulator, USB bridge, power LED), which rules them out on battery. The PaperS3 screen is better (960x540, 16 greys) but its spare pins for amp, LEDs and sensor are unproven.
