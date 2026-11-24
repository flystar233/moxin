// Replacement keycaps for the two round holes and the long rocker slot in the original case.
// The case is only imported for fit checks. This file does not generate a new frame or lid.
use <eink-case.scad>

part = "keys";  // keys / keytest / preview / clearance / insertion
$fn = 40;

OUT = 64.5;
KEY_Z = 2.15;
BOARD_EDGE = 62.10;
TIP = 62.82;           // Bare switch stem ends at y=62.70
FLOOR = -1.45;
ROUND_D = 3.62;
ROCKER_W = 15.72;
ROCKER_H = 3.02;
ROCKER_X = 39.91;
SW_PITCH = 8;
CENTER_GAP = 0.10;
CENTER_PAD_W = 1.20;
END_GAP = 0.42;
KEY_X = [23.87, ROCKER_X, 55.48];
KINDS = ["menu", "rocker", "power"];

module rounded_rect(w, h, r) {
  offset(r=r) square([w-2*r,h-2*r], center=true);
}

module flat_profile(kind) {
  intersection() {
    if (kind == "rocker") rounded_rect(ROCKER_W,ROCKER_H,1.08);
    else circle(d=ROUND_D);
    translate([-9,FLOOR]) square([18,5]);
  }
}

module along_y(y0,y1) {
  translate([0,y1,0]) rotate([90,0,0]) linear_extrude(y1-y0) children();
}

// Polygon coordinates are outward depth and height. Extrude from left to right.
module across_x(cx, width, yz) {
  multmatrix([[0,0,1,cx-width/2],[1,0,0,0],[0,1,0,0],[0,0,0,1]])
    linear_extrude(width) polygon(yz);
}

module keeper_lip(width, lip) {
  across_x(0,width,[[61.97,1.43],[lip,1.43],[lip,2.47],[61.97,2.47]]);
}

module keeper(kind) {
  // One tapered inner lip retains the whole cap. For the rocker the lip is centred,
  // with its corners relieved to clear the frame at full tilt.
  width = kind == "rocker" ? 3.00 : 2.00;
  across_x(0,width,[[TIP+0.09,0.60],[62.42,1.00],[61.97,1.45],
                   [61.97,1.48],[62.38,1.48],[62.46,1.39],[TIP+0.09,1.36]]);
  if (kind == "rocker")
    hull() {
      keeper_lip(0.75,62.46);
      keeper_lip(3.00,62.38);
    }
  else keeper_lip(2.00,62.46);
}

module travel_pad(cx, width, gap) {
  // A broad rounded-off foot stops against the existing PCB edge below the switch.
  // Its underside shares the printing plane of the cap.
  back = BOARD_EDGE + gap;
  across_x(cx,width,[[back+0.08,FLOOR],[TIP+0.24,FLOOR],[TIP+0.24,-0.84],
                    [back+0.12,-0.84],[back,-0.97],[back,FLOOR+0.11]]);
}

module cap(kind) {
  // One uninterrupted plane across the entire pressing face, including the rocker.
  face = kind == "power" ? OUT-0.10 : OUT+0.55;
  union() {
    along_y(TIP,face-0.22) flat_profile(kind);
    hull() {
      along_y(face-0.23,face-0.17) flat_profile(kind);
      along_y(face-0.03,face) offset(delta=-0.16) flat_profile(kind);
    }
    keeper(kind);
    if (kind == "rocker") {
      for (end = [-1,1]) travel_pad(end*SW_PITCH/2,1.85,END_GAP);
      travel_pad(0,CENTER_PAD_W,CENTER_GAP);
    } else {
      travel_pad(0,1.45,END_GAP);
    }
  }
}

module fitted(i, inward=0, angle=0) {
  translate([KEY_X[i],0,KEY_Z]) mirror([0,1,0])
    translate([0,BOARD_EDGE,0]) rotate([0,0,angle])
      translate([0,-BOARD_EDGE-inward,0]) cap(KINDS[i]);
}

module print_keys() {
  for (i=[0:2]) translate([i*17,-61.9,-FLOOR]) cap(KINDS[i]);
}

module old_key_wall() {
  intersection() {
    frame();
    translate([18,-65,-1.9]) cube([44,7,7.4]);
  }
}

function rocker_shift(angle) = CENTER_GAP - CENTER_PAD_W/2*tan(abs(angle));

// Reports a solid if a cap hits the original frame during the nominal movements.
module clearance_check() {
  intersection() {
    old_key_wall();
    union() {
      for (i=[0:2], t=[0,0.04]) fitted(i,t);
      for (i=[0,2], t=[0.20,0.40]) fitted(i,t);
      for (a=[-4.15,-1.7,0,1.7,4.15]) fitted(1,rocker_shift(a)-0.01,a);
    }
  }
}

module insertion_check() {
  intersection() {
    old_key_wall();
    for (i=[0:2], t=[0.10,0.50,1.10,1.80,2.60]) fitted(i,t);
  }
}

if (part == "clearance") clearance_check();
else if (part == "insertion") insertion_check();
else if (part == "preview") {
  color([0.84,0.87,0.89]) old_key_wall();
  for (i=[0:2]) color(i==2 ? [0.66,0.30,0.25] : [0.26,0.50,0.58]) fitted(i);
} else if (part == "keytest") {
  translate([-18,65,1.526]) old_key_wall();
  translate([5,-10,0]) print_keys();
} else print_keys();
