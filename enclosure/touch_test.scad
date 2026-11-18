// SPDX-License-Identifier: CERN-OHL-S-2.0
// M0 touch test piece: four cells of different skin thickness over copper-tape
// pads, to tune pad sensitivity before the shell exists (docs/m0-spike.md).
// Print flat, back side up is fine; same filament as the shell (PETG).
// Stick a 20 x 20 mm copper tape pad into each pocket, solder or clip a wire to
// one pad at a time, and run "w" in the spike firmware.

skins  = [1.0, 1.6, 2.4, 3.2];  // front skin over the pad, mm; 2.4 = shell wall
pad    = 20;                    // copper pad size, mm
margin = 4;
depth  = 4.0;                   // total plate thickness, mm
cell   = pad + 2 * margin;

$fn = 32;

difference() {
  cube([cell * len(skins), cell + 8, depth]);
  for (i = [0:len(skins) - 1]) {
    // pad pocket from the back (z = depth side), leaving skins[i] of material in front
    translate([i * cell + margin, margin, skins[i]]) cube([pad, pad, depth]);
    // wire slot from pocket to the edge
    translate([i * cell + cell / 2 - 1.5, -1, skins[i]]) cube([3, margin + 2, depth]);
    // label strip: skin thickness engraved on the back
    translate([i * cell + cell / 2, cell + 4, depth - 0.6])
      linear_extrude(1) text(str(skins[i]), size = 4, halign = "center", valign = "center");
  }
}
