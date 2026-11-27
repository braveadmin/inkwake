# Sound and wake-up considerations

The included 8 ohm 2 W speaker is loud enough (about 80 dB at 1 m on battery). How it sounds depends on the shell, the firmware and the sound choice. Apply these from M4 and shell task S2 onwards.

- [ ] **Sealed back chamber.** Give the speaker a sealed 30-50 cm3 chamber in the shell with an open grille. The chamber matters more than the speaker; an open back sounds tinny.
- [ ] **High-pass around 200 Hz.** Filter in firmware so the speaker doesn't distort chasing bass it can't produce.
- [ ] **Fade in from near silence.** 2-5 min, using the codec's fine volume steps at the quiet end, where smoothness matters most.
- [ ] **Sounds that suit a small speaker.** Birdsong, light rain, chimes, singing bowls, soft piano. Avoid ocean waves, thunder, deep drones and bass-heavy music.
- [ ] **Gapless loops at 22-44 kHz.** WAV from flash (#44, ADR-0006 amendment); no audible seam when a loop restarts. The firmware loops without a gap; a seam in the recording itself still shows, so cut loops at zero crossings.
- [ ] **Budget the battery.** A 20-30 min ambient lead-in at low volume costs about 25-50 mAh, similar to a day of standby. Default to ~10 min of sound inside a longer light ramp.

## Wake sequence (F3 + F4)

One setting per alarm: the light ramps up over 20-30 min, ambient sound fades in for the last ~10 min, and a firmer sound plays at alarm time if not yet dismissed.

## Alarm sound files (#44)

Each alarm plays the built-in beep (`default`) or one of your own files, looped, with the alarm's volume level and rising/steady setting.

- **Format:** WAV, 16-bit PCM, mono, 16 to 48 kHz. One file can fill the whole sound storage except 256 KB kept for settings and widgets: about 3.1 MB (about 100 s at 16 kHz, 70 s at 22.05 kHz, 35 s at 44.1 kHz). Several files share that space, 20 files at most; the Sounds section shows the exact limit and what is used. Anything else is refused on upload, with the reason.
- **Convert:** Audacity: Tracks > Mix > Mix Stereo Down to Mono, Tracks > Resample (22050 Hz), File > Export > Export as WAV, "Signed 16-bit PCM". Or ffmpeg: `ffmpeg -i in.mp3 -ac 1 -ar 22050 -sample_fmt s16 out.wav`.
- **Names:** the file name becomes the sound's name: lowercased, spaces and dots become `-`, other characters dropped, at most 15 characters ("Morning Birds.wav" -> `morning-birds`). Uploading the same name again replaces the file.
- **Upload:** open setup (KEY held 5 s, or `setup` on the serial console), join the inkwake network, and use the Sounds section: pick a file, Upload. The first time setup opens after this firmware, the clock prepares the storage (screen "Preparing sound storage", can take up to a minute, once).
- **Choose:** in the Alarms section, Edit an alarm, pick its Sound, Test, Save. On the serial console: `alarm sound ID NAME`, `alarm sound ID default`.
- **Serial:** `sound` lists the files (rate, length, space used), `sound play NAME [1-10]` plays one for 6 s, `sound del NAME` deletes one.
- **Fallback:** if the file is missing, unreadable or not a supported WAV, the alarm beeps instead (the serial log says why). Deleting a sound that an alarm uses is allowed; that alarm then beeps.
- **Erase Flash wipes the sounds** (and the settings). Normal firmware updates (firmware.bin only) keep them.
- **Sources:** CC0 sounds, e.g. on freesound.org (filter by licence "Creative Commons 0"). Prefer birdsong, chimes, bowls and soft piano (see the list above).
