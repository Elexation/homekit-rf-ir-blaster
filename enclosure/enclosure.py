#!/usr/bin/env python3
"""Build the two-shell PETG enclosure for the WROOM-1 PCB, then optionally test its fit."""

import argparse
import math
from pathlib import Path

from build123d import (
	Align,
	Axis,
	Box,
	Compound,
	Cone,
	Cylinder,
	GeomType,
	Keep,
	Plane,
	Pos,
	RegularPolygon,
	Rot,
	export_gltf,
	export_step,
	export_stl,
	extrude,
	fillet,
	import_step,
	split,
)

HERE = Path(__file__).resolve().parent
OUT = HERE / "out"
BOARD_STEP = HERE.parent / "pcb-exports" / "3D_PCB1_2026-10-11.step"

# Match the EasyEDA STEP frame: X right, Y negative toward the IR front, Z up from board bottom.
BOARD = 80.0
PCB_TOP = 1.5862
TALLEST = 12.086
HOLES = [(5.0, -5.0), (75.0, -5.0), (75.0, -62.0), (5.0, -60.0)]
SMA_X = (13.75, 60.75)
SMA_Z = 8.086
SMA_TIP_Y = 3.56
LEDS = [(5, -75), (15, -75), (25, -75), (30, -62), (40, -62), (50, -62), (55, -75), (65, -75), (75, -75)]
STATUS_LED = (40.0, -30.0)
RESET_SW = (30.0, -32.0)
USBC_MOUTH_X = 80.05
USBC_Y = -51.0
USBC_Z = 3.21
RX_X = 40.15
RX_Z = 4.43

CLEAR = 0.5
WALL = 2.0
FLOOR = 2.0
LID = 2.0
STANDOFF = 3.0
TOP_CLEAR = 1.0
OUTER_R = 3.5
LIP_W = 1.0
LIP_H = 1.5
LIP_GAP = 0.15

BOSS_D = 8.0
SCREW_D = 3.4
HEAD_D = 6.2
HEAD_DEPTH = 3.2
INSERT_D = 4.0
INSERT_DEPTH = 7.0

SMA_HOLE_D = 10.5
USBC_W = 13.0
USBC_H = 7.5
USBC_R = 1.5
RX_D = 5.0
RX_FLARE_DEG = 30.0
LED_HOLE_D = 7.0
LED_CHAMFER = 0.8
STATUS_D = 4.0
RESET_D = 2.0

# Mating hardware, used only by --check and --preview.
PLUG_W = 12.0
PLUG_H = 6.5
# Boobrie SMA male-female adapter (Amazon B07FKNMPKD); hex size is the SMA standard.
ADAPTER_LIP_D = 7.6
ADAPTER_LIP_L = 2.3
ADAPTER_HEX_AF = 7.94
ADAPTER_NUT_L = 8.4
ADAPTER_FLANGE_L = 1.2
ADAPTER_END_D = 6.1
ADAPTER_END_L = 6.0
ADAPTER_BORE_D = 6.4
# Nut face lands at the board edge, as on the real board.
ADAPTER_PLUG_DEPTH = 3.5

FLOOR_TOP = -STANDOFF
FLOOR_BOT = FLOOR_TOP - FLOOR
LID_IN = TALLEST + TOP_CLEAR
LID_TOP = LID_IN + LID
CAV = BOARD + 2 * CLEAR
CAV_X0 = -CLEAR
CAV_Y0 = -BOARD - CLEAR
CAV_X1 = CAV_X0 + CAV
CAV_Y1 = CAV_Y0 + CAV
INNER_R = OUTER_R - WALL
BASE_UP = (Align.CENTER, Align.CENTER, Align.MIN)


def rounded_box(x0, y0, z0, w, d, h, r):
	box = Pos(x0, y0, z0) * Box(w, d, h, align=Align.MIN)
	return fillet(box.edges().filter_by(Axis.Z), r)


def cavity(z0, h, grow=0.0):
	return rounded_box(CAV_X0 - grow, CAV_Y0 - grow, z0, CAV + 2 * grow, CAV + 2 * grow, h, INNER_R + grow)


def zcyl(x, y, z0, d, h):
	return Pos(x, y, z0) * Cylinder(d / 2, h, align=BASE_UP)


def along_minus_y(x, y0, z, shape):
	return Pos(x, y0, z) * Rot(90, 0, 0) * shape


def cutters():
	parts = []
	for x, y in HOLES:
		parts.append(zcyl(x, y, FLOOR_BOT - 1, SCREW_D, -FLOOR_BOT + 1.5))
		parts.append(zcyl(x, y, FLOOR_BOT - 1, HEAD_D, HEAD_DEPTH + 1))
		parts.append(zcyl(x, y, PCB_TOP - 0.5, INSERT_D, INSERT_DEPTH + 0.5))
	for x in SMA_X:
		parts.append(along_minus_y(x, CAV_Y1 + WALL + 1, SMA_Z, Cylinder(SMA_HOLE_D / 2, WALL + 2, align=BASE_UP)))
	usbc = Pos(CAV_X1 + WALL / 2, USBC_Y, USBC_Z) * Box(WALL + 2, USBC_W, USBC_H)
	parts.append(fillet(usbc.edges().filter_by(Axis.X), USBC_R))
	rx_out = RX_D / 2 + WALL * math.tan(math.radians(RX_FLARE_DEG))
	parts.append(along_minus_y(RX_X, CAV_Y0 + 0.6, RX_Z, Cylinder(RX_D / 2, 0.6, align=BASE_UP)))
	parts.append(along_minus_y(RX_X, CAV_Y0, RX_Z, Cone(RX_D / 2, rx_out, WALL, align=BASE_UP)))
	parts.append(along_minus_y(RX_X, CAV_Y0 - WALL, RX_Z, Cylinder(rx_out, 1, align=BASE_UP)))
	for x, y in LEDS:
		parts.append(zcyl(x, y, LID_IN - 0.5, LED_HOLE_D, LID + 1))
		r = LED_HOLE_D / 2
		parts.append(Pos(x, y, LID_TOP - LED_CHAMFER) * Cone(r, r + LED_CHAMFER + 0.5, LED_CHAMFER + 0.5, align=BASE_UP))
	parts.append(zcyl(*STATUS_LED, LID_IN - 0.5, STATUS_D, LID + 1))
	parts.append(zcyl(*RESET_SW, LID_IN - 0.5, RESET_D, LID + 1))
	cutter = parts[0]
	for p in parts[1:]:
		cutter += p
	return cutter


def shells():
	outer = rounded_box(CAV_X0 - WALL, CAV_Y0 - WALL, FLOOR_BOT, CAV + 2 * WALL, CAV + 2 * WALL, LID_TOP - FLOOR_BOT, OUTER_R)
	body = outer - cavity(FLOOR_TOP, LID_IN - FLOOR_TOP)
	seam = Plane.XY.offset(SMA_Z)
	bottom = split(body, seam, keep=Keep.BOTTOM)
	top = split(body, seam, keep=Keep.TOP)

	# Lip rides in the inner half of the wall, so the cavity stays clear.
	inner = cavity(SMA_Z - 1, LIP_H + 2)
	bottom += cavity(SMA_Z, LIP_H, LIP_W) - inner
	top -= cavity(SMA_Z - 0.01, LIP_H + LIP_GAP + 0.01, LIP_W + LIP_GAP) - inner

	for x, y in HOLES:
		bottom += zcyl(x, y, FLOOR_TOP - 0.5, BOSS_D, STANDOFF + 0.5)
		top += zcyl(x, y, PCB_TOP, BOSS_D, LID_IN - PCB_TOP + 0.5)

	cutter = cutters()
	bottom = bottom - cutter
	top = top - cutter
	bottom.label = "bottom shell"
	top.label = "top shell"
	return bottom, top


def export(bottom, top):
	OUT.mkdir(exist_ok=True)
	export_stl(Pos(0, 0, -FLOOR_BOT) * bottom, OUT / "enclosure_bottom.stl")
	export_stl(Pos(0, 0, LID_TOP) * Rot(180, 0, 0) * top, OUT / "enclosure_top.stl")
	export_step(Compound([bottom, top]), OUT / "enclosure.step")


def adapter(x):
	hex_r = ADAPTER_HEX_AF / 2 / math.cos(math.radians(30))
	body = Cylinder(ADAPTER_LIP_D / 2, ADAPTER_LIP_L, align=BASE_UP)
	body += Pos(0, 0, ADAPTER_LIP_L) * extrude(RegularPolygon(hex_r, 6), amount=ADAPTER_NUT_L - ADAPTER_LIP_L)
	body += Pos(0, 0, ADAPTER_NUT_L) * extrude(RegularPolygon(hex_r, 6), amount=ADAPTER_FLANGE_L)
	body += Pos(0, 0, ADAPTER_NUT_L + ADAPTER_FLANGE_L) * Cylinder(ADAPTER_END_D / 2, ADAPTER_END_L, align=BASE_UP)
	body -= Pos(0, 0, -0.1) * Cylinder(ADAPTER_BORE_D / 2, ADAPTER_PLUG_DEPTH + 0.12, align=BASE_UP)
	return Pos(x, SMA_TIP_Y - ADAPTER_PLUG_DEPTH, SMA_Z) * Rot(-90, 0, 0) * body


def overlap(a, b):
	common = a & b
	return common.volume if common is not None else 0.0


def boxes_meet(a, b):
	p, q = a.bounding_box(), b.bounding_box()
	return all(getattr(p.min, k) < getattr(q.max, k) and getattr(p.max, k) > getattr(q.min, k) for k in "XYZ")


def dome_center(part):
	domes = [f for f in part.faces() if f.geom_type == GeomType.SPHERE]
	if not domes:
		return None
	area = sum(f.area for f in domes)
	centers = [(f.center(), f.area) for f in domes]
	return tuple(sum(getattr(c, axis) * a for c, a in centers) / area for axis in "XYZ")


def may_touch_shell(part):
	b = part.bounding_box()
	if b.min.X < CAV_X0 or b.max.X > CAV_X1 or b.min.Y < CAV_Y0 or b.max.Y > CAV_Y1:
		return True
	if b.min.Z < FLOOR_TOP or b.max.Z > LID_IN:
		return True
	r = BOSS_D / 2
	return any(b.min.X < x + r and b.max.X > x - r and b.min.Y < y + r and b.max.Y > y - r for x, y in HOLES)


def check(bottom, top, board):
	shells_by_name = (("bottom", bottom), ("top", top))
	problems = 0

	v = overlap(bottom, top)
	if v > 0.01:
		problems += 1
		print(f"COLLISION  bottom shell vs top shell: {v:.3f} mm3")

	for part in board.children:
		if not part.solids() or not may_touch_shell(part):
			continue
		for name, shell in shells_by_name:
			v = overlap(part, shell)
			if v > 0.01:
				problems += 1
				print(f"COLLISION  {part.label.split('~')[0]} vs {name} shell: {v:.3f} mm3")

	keepouts = {f"SMA adapter at X {x}": adapter(x) for x in SMA_X}
	keepouts["USB-C plug body"] = Pos(USBC_MOUTH_X, USBC_Y, USBC_Z) * Box(20, PLUG_W, PLUG_H, align=(Align.MIN, Align.CENTER, Align.CENTER))
	parts = [p for p in board.children if p.solids()]
	for label, k in keepouts.items():
		targets = [(f"{name} shell", shell) for name, shell in shells_by_name]
		targets += [(p.label.split("~")[0], p) for p in parts if boxes_meet(k, p)]
		for name, target in targets:
			v = overlap(k, target)
			if v > 0.01:
				problems += 1
				print(f"BLOCKED    {label} vs {name}: {v:.3f} mm3")

	for part in board.children:
		c = dome_center(part)
		if c is None:
			continue
		ref = part.label.split("~")[0]
		if "LED-TH" in part.label:
			hole = min(LEDS, key=lambda h: math.dist(h, c[:2]))
			off = math.dist(hole, c[:2])
			label = f"LED {ref:<4} dome ({c[0]:.2f}, {c[1]:.2f}) vs hole {hole}"
		elif "TSOP" in part.label:
			off = math.dist((RX_X, RX_Z), (c[0], c[2]))
			label = f"IR receiver lens (X {c[0]:.2f}, Z {c[2]:.2f}) vs window (X {RX_X}, Z {RX_Z})"
		else:
			continue
		if off >= 0.3:
			problems += 1
		print(f"{label}: {off:.2f} mm {'ok' if off < 0.3 else 'OFF-CENTER'}")

	print(f"check: {problems} problem(s)")
	return problems


def preview(bottom, top, board):
	OUT.mkdir(exist_ok=True)
	adapters = Compound([adapter(x) for x in SMA_X])
	for name, shape in (("bottom", bottom), ("top", top), ("board", board), ("adapters", adapters)):
		export_gltf(shape, OUT / f"preview_{name}.glb", binary=True, linear_deflection=0.05, angular_deflection=0.3)


def main():
	ap = argparse.ArgumentParser(description=__doc__)
	ap.add_argument("--check", action="store_true", help="test the fit against the board STEP")
	ap.add_argument("--preview", action="store_true", help="write GLB meshes of both shells and the board")
	args = ap.parse_args()

	bottom, top = shells()
	export(bottom, top)
	print(f"outside {CAV + 2 * WALL:.1f} x {CAV + 2 * WALL:.1f} x {LID_TOP - FLOOR_BOT:.2f} mm, seam at {SMA_Z - FLOOR_BOT:.2f} mm up")
	print(f"volume: bottom {bottom.volume / 1000:.1f} cm3, top {top.volume / 1000:.1f} cm3")

	problems = 0
	if args.check or args.preview:
		board = import_step(BOARD_STEP)
		if args.check:
			problems = check(bottom, top, board)
		if args.preview:
			preview(bottom, top, board)
	raise SystemExit(1 if problems else 0)


if __name__ == "__main__":
	main()
