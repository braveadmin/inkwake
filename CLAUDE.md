# inkwake: notes for coding sessions

Open source battery alarm clock on the Waveshare ESP32-S3-RLCD-4.2. Plan: docs/roadmap.md, scope: docs/features.md, decisions: docs/adr/.
Each feature is worked in its own session, tracked as a GitHub issue (braveadmin/inkwake). Update this file when a session changes something below.

## Status (2026-11-18)

- M0 hardware spike: partly done, rest deferred to a hardware phase before M7 (issue #17, docs/m0-spike.md).
  Confirmed: display, KEY, BOOT, battery sense, touch on bare pins; deep sleep with the panel in LPM and DC/CS/RST held keeps the image and redraws without flicker. Not measured: sleep current, PWR circuit, touch through plastic.
- M1: milestones, labels and issues created with scripts/bootstrap-github.sh (re-runnable, skips existing). Open: #21 pin the pioarduino platform release.
- M2 clock core in progress. #18 display driver + partial refresh + clock face: done and verified on the board 2026-11-18 (`box` top-left, `set`, in-place minute updates, battery deep sleep). Lives on `m0-spike`; the issue closes when PR #1 merges. Next: #19 PCF85063 RTC, #12 F11 room temperature, #20 Wi-Fi provisioning, #13 F12 NTP + timezone.
- Device firmware (env `device`) is now the clock: deep sleep between minutes on battery, stays awake with serial commands (`set YYYY-MM-DD HH:MM`, `full`, `box`) while a USB host is plugged in. Time runs on the ESP32 system clock (starts at build time, UTC, no TZ, drifts) until #19/#13.
- Shell work S1/S2 (#14, #15) moved to the hardware phase before M7.

## Branches

- `m0-spike`, PR #1: spike firmware and M0 docs. **Do not merge yet.** #18, #19 and #12 are integrated on this branch (commit there, one feature per session), then the PR merges.
- #20 and #13 come after, from main or a branch off `m0-spike`.

## Build and flash

**Rule: whenever a change alters how the user flashes (new or removed image files, changed addresses, partition table, erase needed or not, new flasher tool or steps), tell the user explicitly in that session, before they flash, with the full updated file/address table.** Also update this section. Current procedure: three files, 0x0 bootloader.bin, 0x8000 partitions.bin, 0x10000 firmware.bin.

- CI (.github/workflows/firmware.yml) runs lint (clang-format), `pio test -e native`, `pio run -e device` and `pio run -e spike`, and uploads `firmware-<sha>` and `spike-<sha>` artifacts (bootloader.bin, partitions.bin, firmware.bin).
- The user flashes from the browser with https://adafruit.github.io/Adafruit_WebSerial_ESPTool/: erase, then 0x0 bootloader.bin, 0x8000 partitions.bin, 0x10000 firmware.bin. esptool-js (espressif.github.io) fails here with "CryptoJS is not defined" (CDN blocked).
- Erase Flash wipes NVS (saved settings, Wi-Fi credentials). For normal updates tell the user to flash only firmware.bin at 0x10000 without erasing; full erase + three files for a first install or recovery, and after OTA has been used (otadata may point to the second app slot). Tell the user when a new image (e.g. littlefs.bin in M5) or a partition change adds or moves an address.
- Serial console: https://googlechromelabs.github.io/serial-terminal/ at 115200. Firmware output must end lines with `\r\n` (that terminal does not add CR).
- Local builds do not work on the user's work machine: the corporate TLS proxy breaks PlatformIO package downloads even with the CA bundle (/etc/ssl/certs/kapsch-bundle.pem) added to certifi. Rely on CI. clang-format can be installed with pip for local lint.
- Sources outside `src/` (e.g. `spike/`) need framework libraries listed in `lib_deps` (SPI, Preferences), or the library finder misses them.

## Hardware facts (verified unless noted)

Full map: hardware/wiring.md. Source of truth for Waveshare details: github.com/waveshareteam/ESP32-S3-RLCD-4.2 (Apache-2.0), wiki docs.waveshare.com/ESP32-S3-RLCD-4.2.

| Item | Value |
| --- | --- |
| Display ST7305, 400x300 landscape | SCK 11, MOSI 12, DC 5, CS 40, RST 41; init sequence and pixel packing in firmware/spike/rlcd.cpp |
| Pixel packing (landscape) | byte = 2 columns x 4 rows, bit set = white; index = (x>>1)*75 + ((299-y)>>2), bit 7-(((299-y)&3)<<1 \| (x&1)) |
| Panel power modes | 0x38 HPM, 0x39 LPM; firmware writes RAM in HPM then parks in LPM |
| Partial refresh | 0x2B row window = landscape x/2 (0..199). 0x2A column addresses are **mirrored**: buffer column c (3 bytes = 12 px in y, c = 0 at the bottom) is address 0x2A - c (seen on the panel 2026-11-18: unmirrored, a top-left box showed bottom-left). Math in firmware/lib/display/src/panel_window.* |
| KEY / BOOT | GPIO18 / GPIO0, active low, external pull-ups; KEY wakes from deep sleep via ext1 |
| PWR | hardware switch, no GPIO (circuit not checked) |
| Battery sense | GPIO4 (ADC1_CH3), 1:3 divider |
| I2C (SHTC3, PCF85063) | SDA 13, SCL 14; RTC interrupt GPIO15 (from pin image, not yet used) |
| Speaker amp enable | GPIO46, hold low in sleep |
| Touch | GPIO1/2/3: idle ~26k, bare finger +100k..1M, noise ±20, neighbour crosstalk up to +2k. S3 deep sleep wakes on one touch channel only (to confirm) |
| Free header pins | GPIO1, 2, 3, 17 (LEDs), 43/44; GPIO18 is KEY |

## Conventions

- Hardware-free logic in firmware/lib, unit tested with `pio test -e native`; device code in firmware/src.
- Without PlatformIO locally, the native tests also build with plain g++ and Unity's three source files (`g++ -std=gnu++17 -Ilib/display/src -I<unity> test/test_display/test_display.cpp lib/display/src/*.cpp unity.o`).
- Fonts: 1-bit headers in firmware/lib/display/src/fonts, generated by firmware/tools/gen_font.py (Pillow) from Lato (OFL-1.1, Debian fonts-lato); don't edit them, regenerate. Licence notes in fonts/LICENSE-fonts.md. Lint skips them via `// clang-format off`.
- Clock face preview without the board: firmware/tools/preview_face.cpp writes a PBM (build line in the file).
- CI lints with clang-format 18 (Ubuntu 24.04); check locally with `pip install clang-format==18.1.3`.
- `RTC_DATA_ATTR` variables must be constant-initialized (`= {}`, trivial members): a type with member defaults can otherwise get startup code that reruns on every deep-sleep wake and wipes them.
- Conventional commits; licences: firmware GPL-3.0, enclosure CERN-OHL-S, docs per docs/LICENSE.
- The user is new to ESP32 work: give click-by-click flashing and test steps, and say what they should see.
