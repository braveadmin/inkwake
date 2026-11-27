# 0006. Audio: on-board codec and included speaker

- Status: Proposed
- Date: 2026-11-02
- Amended 2026-11-27 (#44): sound files live on the LittleFS partition in flash, not the TF card (see below).

## Decision

On-board ES8311 codec and amplifier (I2S on GPIO8/9/10/16/45; PA_CTRL on GPIO46 switches the amp off), included 8 ohm 2 W speaker, sounds stored on the TF card. Considerations in docs/sound.md.

### Amendment 2026-11-27 (#44, decided with the user)

- **Storage:** sound files go on the LittleFS partition in flash (`spiffs` in default_16MB.csv, about 3.3 MB), not the TF card. No card to buy, nothing to open, no card current in deep sleep, and files are uploaded from the setup page. The firmware formats the partition itself the first time setup opens (or `sound` runs), so there is no littlefs.bin to flash; Erase Flash wipes the sounds. The TF card can come back later if 3.3 MB proves too small.
- **Format:** WAV, 16-bit PCM, mono, 16-48 kHz only; anything else is refused on upload with a reason. No decoder: least CPU and battery while ringing. The I2S rate follows the file; the ES8311 registers stay the same, since MCLK is always 256 x fs.
- **Limits:** 1.5 MB per file, 20 files, 64 KB kept free.
- **No built-in sounds** besides the beep: the flash stays free for the user's own files, and no licence checks.
- **Volume:** a file plays with the alarm's own level and rising/steady setting (#43), scaled in software on the beep's -40..0 dBFS scale. The longer ambient lead-in stays with #4.
- **Fallback:** any trouble with the file (missing, bad format, storage not ready, a read failing mid-play) rings the beep instead; an alarm never stays silent because of a sound file.

## Alternatives rejected

External MAX98357A. For #44: TF card (card to buy, case to open, unmeasured sleep current), MP3 (decoder, CPU and battery while ringing), shipping CC0 sounds (flash space, licence checks).

## Rationale

Already on the board. About 0.5-1 W on battery, roughly 80 dB at 1 m, so wake-up volume sits at 5-20%.
