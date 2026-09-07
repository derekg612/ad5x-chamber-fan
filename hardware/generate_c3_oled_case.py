#!/usr/bin/env python3
"""Generate a two-part snap-fit enclosure for the ESP32-C3 OLED 0.42" build.

Produces case_bottom.stl and case_top.stl next to this script.

Design notes:
  * The board is housed WITHOUT soldered headers, so the case is slim -- there
    is only enough room under the PCB for solder bumps and via tails.
  * Interior headroom above the PCB clears a TO-92 (2N2222A) lying flat on the
    board, which is the tallest thing inside.
  * The two halves snap together with half-round detent ridges on the lid's
    lip engaging matching grooves in the tray wall.
  * A cable slot in the end wall passes the fan return, ground bond, thermistor
    pair, and button wires.

Run with:  ~/.pythonenvs/stl/bin/python3 generate_c3_oled_case.py

IMPORTANT -- verify before printing:
  Dimensions marked ASSUMED below were not measured on the actual board. Check
  them with calipers and re-run. In particular the OLED window position is a
  guess at "centered"; if the display sits off-center on your board, adjust
  OLED_WINDOW_CX. Printing the tray alone first is a cheap way to test fit.
"""

import os

import numpy as np
import trimesh

# ---------------------------------------------------------------------------
# Board dimensions
# ---------------------------------------------------------------------------
PCB_L = 24.8          # MEASURED (board reference): length, USB end to far end
PCB_W = 20.5          # MEASURED (board reference): width
PCB_T = 1.6           # ASSUMED: standard 1.6 mm FR4

# USB-C receptacle on the +X end. ASSUMED: typical SMD vertical receptacle,
# centered on the short edge, sitting on top of the PCB.
USB_W = 9.6           # ASSUMED: opening width (connector ~9.0 plus clearance)
USB_H = 4.0           # ASSUMED: opening height (connector ~3.2 plus clearance)

# OLED window in the lid. ASSUMED position: centered. The 0.42" module's active
# area is only about 8.9 x 5.0 mm; the window is deliberately oversized so a
# small placement error still shows the whole display.
OLED_WINDOW_L = 13.0
OLED_WINDOW_W = 8.0
OLED_WINDOW_CX = 0.0  # ASSUMED: shift along X if the display is not centered
OLED_WINDOW_CY = 0.0

# ---------------------------------------------------------------------------
# Case parameters
# ---------------------------------------------------------------------------
FIT = 0.25            # clearance around the PCB on each side
# The wall has to be thick enough that the rebate step (WALL - LIP_T -
# LIP_CLEAR) still has material left after the snap groove is cut into it.
# check_parameters() enforces this.
WALL = 2.4            # side wall thickness
FLOOR = 1.2           # tray floor thickness
CEIL = 1.2            # lid top plate thickness

UNDER_PCB = 1.2       # space beneath the PCB for solder bumps / via tails
OVER_PCB = 7.0        # headroom above the PCB; TO-92 lying flat is ~4.5 mm
LEDGE = 1.5           # width of the ledge the PCB rests on
# Material left above the wall openings, bridging them to the tray rim. The
# split line has to sit at least this far above the tallest opening or the
# bridge prints as a sliver.
OPENING_WEB = 1.0

LIP_T = 0.9           # lid lip thickness
LIP_CLEAR = 0.15      # sliding clearance between lip and tray rebate
# Snap detent. The ridge protrudes DETENT_R past the tray wall during
# insertion, so DETENT_R *is* the deflection the lip ring has to absorb before
# it drops into the groove. The lip is a closed rectangular ring, which is far
# stiffer than a free cantilever, so keep this modest -- much past 0.4 mm and
# PLA tends to crack or simply refuse to seat.
DETENT_R = 0.35       # half-round snap detent radius
DETENT_CLEAR = 0.1    # groove cut oversize so the ridge seats without binding
DETENT_LEN = 12.0     # length of each detent ridge along X
DETENT_DEFLECTION_RANGE = (0.25, 0.45)  # sane snap force band

# Minimum feature sizes for a 0.4 mm nozzle.
MIN_PRINTABLE_WALL = 0.6
TO92_FLAT_HEIGHT = 4.5  # TO-92 package lying on its side

PAD_REACH = 1.5       # how far the lid's retention pads overhang the PCB edge
PAD_LEN = 5.0         # length of each retention pad
PAD_ENABLED = True    # pads press the PCB down onto the ledge (no screws)

# Cable slot in the -X end wall: fan return, ground bond, thermistor pair,
# plus button wires.
CABLE_W = 8.0
CABLE_H = 3.6

SEGMENTS = 48         # cylinder facet count

# ---------------------------------------------------------------------------
# Derived geometry
# ---------------------------------------------------------------------------
INNER_L = PCB_L + 2 * FIT
INNER_W = PCB_W + 2 * FIT
OUTER_L = INNER_L + 2 * WALL
OUTER_W = INNER_W + 2 * WALL

Z_FLOOR_TOP = FLOOR
Z_PCB_BOT = Z_FLOOR_TOP + UNDER_PCB
Z_PCB_TOP = Z_PCB_BOT + PCB_T
Z_REBATE = Z_PCB_TOP + 0.3          # lid lip bottoms out just above the PCB
Z_CAVITY_TOP = Z_PCB_TOP + OVER_PCB
Z_CASE_TOP = Z_CAVITY_TOP + CEIL

# Split the halves above the USB opening so the mating line does not cut
# through the connector cutout, leaving OPENING_WEB of material bridging it.
Z_SPLIT = Z_PCB_TOP + 5.0

Z_USB_BOT = Z_PCB_TOP - 0.5
Z_USB_TOP = Z_USB_BOT + USB_H
Z_CABLE_BOT = Z_PCB_TOP + 0.2
Z_CABLE_TOP = Z_CABLE_BOT + CABLE_H
Z_DETENT = (Z_REBATE + Z_SPLIT) / 2.0

# Rebate: above Z_REBATE the tray wall steps outward to receive the lid lip.
REBATE_L = INNER_L + 2 * (LIP_T + LIP_CLEAR)
REBATE_W = INNER_W + 2 * (LIP_T + LIP_CLEAR)


def check_parameters():
    """Fail loudly on parameter combinations that would print badly.

    These are the constraints that are easy to break by nudging one dimension
    and hard to notice in a slicer preview.
    """
    rebate_wall = WALL - LIP_T - LIP_CLEAR
    left_at_groove = rebate_wall - (DETENT_R + DETENT_CLEAR)
    assert left_at_groove >= MIN_PRINTABLE_WALL, (
        f"Only {left_at_groove:.2f} mm of tray wall remains behind the snap groove "
        f"(need {MIN_PRINTABLE_WALL}). Increase WALL or reduce DETENT_R/LIP_T."
    )

    # The ridge is embedded in the lip by (DETENT_R - LIP_CLEAR); what is left
    # of the lip behind it still has to be printable.
    left_in_lip = LIP_T - (DETENT_R - LIP_CLEAR)
    assert left_in_lip >= 0.4, (
        f"Only {left_in_lip:.2f} mm of lip remains behind the snap ridge. "
        f"Increase LIP_T or reduce DETENT_R."
    )

    # DETENT_R is the deflection the lip ring absorbs on the way in.
    low, high = DETENT_DEFLECTION_RANGE
    assert low <= DETENT_R <= high, (
        f"Snap deflection {DETENT_R:.2f} mm is outside the {low}-{high} mm band: "
        f"too little will not hold the lid, too much cracks the lip."
    )

    assert OVER_PCB >= TO92_FLAT_HEIGHT + 0.5, (
        f"OVER_PCB ({OVER_PCB}) leaves no margin for a TO-92 lying flat "
        f"({TO92_FLAT_HEIGHT} mm)."
    )

    # Not just "the opening does not cross the split" -- there has to be enough
    # material bridging above it to actually print.
    for name, top in (("cable slot", Z_CABLE_TOP), ("USB opening", Z_USB_TOP)):
        web = Z_SPLIT - top
        assert web >= OPENING_WEB, (
            f"Only {web:.2f} mm of tray wall bridges above the {name} "
            f"(need {OPENING_WEB}). Raise Z_SPLIT or shorten the opening."
        )

    lid_depth = Z_CAVITY_TOP - Z_SPLIT
    assert lid_depth >= 1.5, (
        f"Lid interior is only {lid_depth:.2f} mm deep; raise OVER_PCB."
    )

    assert LEDGE * 2 < min(INNER_L, INNER_W), "Ledge consumes the whole pocket."


def box(lx, ly, z0, z1, cx=0.0, cy=0.0):
    """Axis-aligned box spanning z0..z1, centered on (cx, cy) in plan."""
    mesh = trimesh.creation.box(extents=(lx, ly, z1 - z0))
    mesh.apply_translation((cx, cy, 0.5 * (z0 + z1)))
    return mesh


def x_cylinder(radius, length, cx, cy, cz):
    """Cylinder whose axis runs along X."""
    mesh = trimesh.creation.cylinder(radius=radius, height=length, sections=SEGMENTS)
    mesh.apply_transform(trimesh.transformations.rotation_matrix(np.pi / 2.0, (0, 1, 0)))
    mesh.apply_translation((cx, cy, cz))
    return mesh


def detent_cylinders(radius):
    """Snap detents on both long walls, as half-round ridges running along X."""
    y = REBATE_W / 2.0
    return [
        x_cylinder(radius, DETENT_LEN, 0.0, +y, Z_DETENT),
        x_cylinder(radius, DETENT_LEN, 0.0, -y, Z_DETENT),
    ]


def build_bottom():
    """Tray: PCB pocket on a ledge, USB and cable openings, detent grooves."""
    part = box(OUTER_L, OUTER_W, 0.0, Z_SPLIT)

    # Pocket the PCB sits in, open to the top of the tray.
    part = part.difference(box(INNER_L, INNER_W, Z_PCB_BOT, Z_SPLIT + 1.0))

    # Relief under the board so solder bumps clear the floor, leaving a
    # perimeter ledge for the PCB to rest on.
    part = part.difference(
        box(INNER_L - 2 * LEDGE, INNER_W - 2 * LEDGE, Z_FLOOR_TOP, Z_PCB_BOT + 0.01)
    )

    # Step the wall outward above the PCB to receive the lid's lip.
    part = part.difference(box(REBATE_L, REBATE_W, Z_REBATE, Z_SPLIT + 1.0))

    # USB-C opening in the +X end wall.
    part = part.difference(
        box(2 * WALL + 2.0, USB_W, Z_USB_BOT, Z_USB_TOP, cx=OUTER_L / 2.0)
    )

    # Cable slot in the -X end wall.
    part = part.difference(
        box(2 * WALL + 2.0, CABLE_W, Z_CABLE_BOT, Z_CABLE_TOP, cx=-OUTER_L / 2.0)
    )

    # Snap grooves, cut slightly oversize so the ridges seat without binding.
    for cyl in detent_cylinders(DETENT_R + DETENT_CLEAR):
        part = part.difference(cyl)

    return part


def build_top():
    """Lid: top plate, depending lip with snap ridges, OLED window, PCB pads."""
    part = box(OUTER_L, OUTER_W, Z_SPLIT, Z_CASE_TOP)
    part = part.difference(box(INNER_L, INNER_W, Z_SPLIT - 1.0, Z_CAVITY_TOP))

    # Lip that drops into the tray's rebate.
    lip = box(
        REBATE_L - 2 * LIP_CLEAR, REBATE_W - 2 * LIP_CLEAR, Z_REBATE, Z_SPLIT
    ).difference(box(INNER_L, INNER_W, Z_REBATE - 1.0, Z_SPLIT + 1.0))
    part = part.union(lip)

    # Snap ridges on the outside of the lip.
    for cyl in detent_cylinders(DETENT_R):
        part = part.union(cyl)

    # Trim anything the ridges pushed past the case footprint.
    part = part.intersection(box(OUTER_L, OUTER_W, Z_REBATE - 1.0, Z_CASE_TOP + 1.0))

    # Pads that hold the PCB down against the ledge. They sit over the unsoldered
    # header rows along the long edges -- move them if they foul a component.
    if PAD_ENABLED:
        for sx in (-1.0, 1.0):
            for sy in (-1.0, 1.0):
                part = part.union(
                    box(
                        PAD_LEN,
                        PAD_REACH,
                        Z_PCB_TOP,
                        Z_REBATE,
                        cx=sx * PCB_L / 4.0,
                        cy=sy * (INNER_W / 2.0 - PAD_REACH / 2.0),
                    )
                )

    # OLED window through the top plate.
    part = part.difference(
        box(
            OLED_WINDOW_L,
            OLED_WINDOW_W,
            Z_CAVITY_TOP - 1.0,
            Z_CASE_TOP + 1.0,
            cx=OLED_WINDOW_CX,
            cy=OLED_WINDOW_CY,
        )
    )

    return part


def report(name, mesh):
    ext = mesh.extents
    status = "watertight" if mesh.is_watertight else "NOT watertight"
    print(
        f"  {name:<16} {ext[0]:5.1f} x {ext[1]:5.1f} x {ext[2]:5.1f} mm"
        f"   {mesh.volume / 1000.0:6.2f} cm^3   {status}"
    )


def main():
    check_parameters()
    out_dir = os.path.dirname(os.path.abspath(__file__))

    print("ESP32-C3 OLED 0.42\" enclosure")
    print(f"  PCB {PCB_L} x {PCB_W} x {PCB_T} mm, {OVER_PCB} mm headroom above the board")
    print(f"  Case {OUTER_L:.1f} x {OUTER_W:.1f} x {Z_CASE_TOP:.1f} mm overall")
    print(f"  Snap: {Z_SPLIT - Z_REBATE:.1f} mm lip, {DETENT_R:.2f} mm deflection, "
          f"{WALL - LIP_T - LIP_CLEAR - DETENT_R - DETENT_CLEAR:.2f} mm wall behind the groove")
    print()

    for name, mesh in (("case_bottom", build_bottom()), ("case_top", build_top())):
        report(name, mesh)
        path = os.path.join(out_dir, f"{name}.stl")
        mesh.export(path)
        print(f"  {'':<16} -> {path}")

    print()
    print("Print both parts flat on the bed, outside face down, no supports needed.")
    print("The lid's snap ridges are half-round and self-chamfering, so they")
    print("bridge fine unsupported at this size.")


if __name__ == "__main__":
    main()
