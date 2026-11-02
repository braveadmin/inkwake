// SPDX-License-Identifier: CERN-OHL-S-2.0
// inkwake enclosure: wedge desk case, front shell + back plate.
// All board dimensions are PLACEHOLDERS until measured at the M0 gate.
// Render one part:  openscad -D 'part="front"' -o front.stl inkwake.scad

/* [Part] */
part = "assembly"; // [assembly, front, back]

/* [Board (measure at M0)] */
board_w = 125;      // PCB width, mm
board_h = 70;       // PCB height, mm
board_d = 12;       // PCB + parts depth, mm
view_w  = 98;       // visible display area width, mm
view_h  = 56;       // visible display area height, mm
view_dx = 0;        // display offset from board centre, mm
view_dy = 0;

/* [Case] */
wall      = 2.4;    // 6 perimeters at 0.4 mm nozzle
clearance = 0.4;
tilt      = 15;     // screen lean-back, degrees
depth     = 55;     // footprint depth, mm
corner_r  = 4;

/* [Speaker] */
spk_d     = 40;     // speaker diameter, mm
hole_d    = 2.5;
hole_gap  = 4.5;

/* [Sunrise LED ring] */
ring_od   = 0;      // 0 = no ring opening; set to ring outer diameter, mm
ring_w    = 6;

$fn = 48;

inner_w = board_w + 2 * clearance;
inner_h = board_h + 2 * clearance;
outer_w = inner_w + 2 * wall;
outer_h = inner_h + 2 * wall + 20;   // 20 mm chin for the speaker grille

module rounded_box(w, h, d, r) {
  hull() for (x = [r, w - r], y = [r, h - r]) translate([x, y, 0]) cylinder(r = r, h = d);
}

module front() {
  difference() {
    rounded_box(outer_w, outer_h, board_d + wall, corner_r);
    // board pocket, open at the back
    translate([wall, wall + 20, wall]) cube([inner_w, inner_h, board_d + 1]);
    // display window
    translate([outer_w / 2 + view_dx - view_w / 2, wall + 20 + inner_h / 2 + view_dy - view_h / 2, -1])
      cube([view_w, view_h, wall + 2]);
    // speaker grille in the chin
    translate([outer_w / 2, 10 + wall / 2, -1]) grille();
    // sunrise ring channel
    if (ring_od > 0)
      translate([outer_w / 2, outer_h / 2, -1]) difference() {
        cylinder(d = ring_od, h = wall + 2);
        translate([0, 0, -1]) cylinder(d = ring_od - 2 * ring_w, h = wall + 4);
      }
  }
}

module grille() {
  n = floor(spk_d / hole_gap / 2);
  for (i = [-n:n], j = [-1:1])
    if (abs(i * hole_gap) <= spk_d / 2)
      translate([i * hole_gap, j * hole_gap, 0]) cylinder(d = hole_d, h = wall + 2);
}

module back() {
  // plate with the tilt wedge; USB-C slot at the bottom edge
  difference() {
    hull() {
      rounded_box(outer_w, outer_h, wall, corner_r);
      translate([0, 0, 0]) rounded_box(outer_w, wall + corner_r, depth * sin(tilt) + wall, corner_r);
    }
    translate([outer_w / 2 - 6, -1, wall]) cube([12, wall + 2, 6]);
  }
}

if (part == "front") front();
else if (part == "back") back();
else {
  color("white") front();
  color("gray") translate([0, 0, board_d + wall + 5]) back();
}
