# Sound and wake-up considerations

The included 8 ohm 2 W speaker is loud enough (about 80 dB at 1 m on battery). How it sounds depends on the shell, the firmware and the sound choice. Apply these from M4 and shell task S2 onwards.

- [ ] **Sealed back chamber.** Give the speaker a sealed 30-50 cm3 chamber in the shell with an open grille. The chamber matters more than the speaker; an open back sounds tinny.
- [ ] **High-pass around 200 Hz.** Filter in firmware so the speaker doesn't distort chasing bass it can't produce.
- [ ] **Fade in from near silence.** 2-5 min, using the codec's fine volume steps at the quiet end, where smoothness matters most.
- [ ] **Sounds that suit a small speaker.** Birdsong, light rain, chimes, singing bowls, soft piano. Avoid ocean waves, thunder, deep drones and bass-heavy music.
- [ ] **Gapless loops at 22-44 kHz.** WAV or MP3 from the TF card; no audible seam when a loop restarts.
- [ ] **Budget the battery.** A 20-30 min ambient lead-in at low volume costs about 25-50 mAh, similar to a day of standby. Default to ~10 min of sound inside a longer light ramp.

## Wake sequence (F3 + F4)

One setting per alarm: the light ramps up over 20-30 min, ambient sound fades in for the last ~10 min, and a firmer sound plays at alarm time if not yet dismissed.
