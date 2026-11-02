# 0006. Audio: on-board codec and included speaker

- Status: Proposed
- Date: 2026-11-02

## Decision

On-board ES8311 codec and amplifier (I2S on GPIO8/9/10/16/45; PA_CTRL on GPIO46 switches the amp off), included 8 ohm 2 W speaker, sounds stored on the TF card. Considerations in docs/sound.md.

## Alternatives rejected

External MAX98357A.

## Rationale

Already on the board. About 0.5-1 W on battery, roughly 80 dB at 1 m, so wake-up volume sits at 5-20%.
