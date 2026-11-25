# 0015. Input: capacitive pads in the shell

- Status: Proposed
- Date: 2026-11-02
- Amended by ADR-0018 (2026-11-25): until the pads exist, KEY + BOOT are the main input, with a button menu drawn on the panel.

## Decision

Copper-tape pads inside the shell: top pad on GPIO1 (tap = snooze, hold 2 s = dismiss), side pad on GPIO2 (tap = night glow, hold 5 s = web UI). The board's KEY button is a backup snooze. GPIO3 spare for a third pad.

## Alternatives rejected

Touch panel; buttons only.

## Rationale

Easier to hit half asleep than a spot on a screen, almost free, and a pad can wake the chip from deep sleep.
