#!/usr/bin/env python3
"""Render preview images of the ESP32-C3 OLED case for visual inspection.

Writes preview_iso.png (shaded 3D views) and preview_sections.png (cut-through
views) next to this script.

The section views are the useful ones. They cut through the assembled case so
you can actually see the lid's lip sitting in the tray's rebate, the snap ridge
in its groove, the PCB resting on its ledge, and the wall openings -- the
geometry that is easy to get numerically right and still have look wrong.

Run with:  ~/.pythonenvs/stl/bin/python3 preview_case.py
"""

import os

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt
import numpy as np
import trimesh
from matplotlib.collections import LineCollection
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

import generate_c3_oled_case as g

TRAY_COLOR = "#2f6f9f"
LID_COLOR = "#c0642a"
PCB_COLOR = "#2e7d32"
BJT_COLOR = "#7b1fa2"


def shaded(ax, mesh, base_color, alpha=1.0):
    """Draw a mesh as flat-shaded triangles lit from the upper front left."""
    light = np.array([-0.4, -0.7, 0.6])
    light /= np.linalg.norm(light)
    shade = 0.35 + 0.65 * np.clip(mesh.face_normals @ light, 0.0, 1.0)

    rgb = np.array(matplotlib.colors.to_rgb(base_color))
    colors = np.clip(shade[:, None] * rgb[None, :], 0.0, 1.0)

    coll = Poly3DCollection(mesh.triangles, facecolors=colors, alpha=alpha)
    coll.set_edgecolor("none")
    ax.add_collection3d(coll)


def frame_3d(ax, meshes, title):
    bounds = np.vstack([m.bounds for m in meshes])
    lo, hi = bounds.min(axis=0), bounds.max(axis=0)
    span = (hi - lo).max() / 2.0
    mid = (hi + lo) / 2.0
    ax.set_xlim(mid[0] - span, mid[0] + span)
    ax.set_ylim(mid[1] - span, mid[1] + span)
    ax.set_zlim(mid[2] - span, mid[2] + span)
    ax.set_box_aspect((1, 1, 1))
    ax.set_title(title, fontsize=10)
    ax.set_axis_off()


def render_iso(tray, lid, path):
    fig = plt.figure(figsize=(13, 4.6))

    ax = fig.add_subplot(131, projection="3d")
    shaded(ax, tray, TRAY_COLOR)
    frame_3d(ax, [tray], "Tray (USB-C right, cable hole in the floor)")
    ax.view_init(elev=32, azim=-52)

    # Show the lid from underneath -- that is where the lip, snap ridges and
    # PCB retention pads are.
    ax2 = fig.add_subplot(132, projection="3d")
    shaded(ax2, lid, LID_COLOR)
    frame_3d(ax2, [lid], "Lid, viewed from below (lip + snap ridges + pads)")
    ax2.view_init(elev=-28, azim=-52)

    ax3 = fig.add_subplot(133, projection="3d")
    exploded = lid.copy()
    exploded.apply_translation((0.0, 0.0, 9.0))
    shaded(ax3, tray, TRAY_COLOR)
    shaded(ax3, exploded, LID_COLOR, alpha=0.95)
    frame_3d(ax3, [tray, exploded], "Exploded")
    ax3.view_init(elev=22, azim=-52)

    fig.tight_layout()
    fig.savefig(path, dpi=130)
    plt.close(fig)


def section_segments(mesh, normal, origin):
    """Return 2D line segments where a plane cuts a mesh.

    The two in-plane axes are returned in (horizontal, vertical) order, with
    vertical always Z.
    """
    lines = trimesh.intersections.mesh_plane(
        mesh, plane_normal=np.array(normal, dtype=float),
        plane_origin=np.array(origin, dtype=float))
    if len(lines) == 0:
        return np.zeros((0, 2, 2))
    horizontal_axis = 1 if normal[0] else 0
    return lines[:, :, [horizontal_axis, 2]]


def draw_section(ax, tray, lid, normal, origin, title, extras=True):
    for mesh, color, label in ((tray, TRAY_COLOR, "tray"), (lid, LID_COLOR, "lid")):
        segs = section_segments(mesh, normal, origin)
        ax.add_collection(LineCollection(segs, colors=color, linewidths=1.6, label=label))

    if extras:
        # PCB and a TO-92 lying flat, drawn as reference outlines.
        half = (g.PCB_W if normal[0] else g.PCB_L) / 2.0
        ax.add_patch(plt.Rectangle(
            (-half, g.Z_PCB_BOT), 2 * half, g.PCB_T,
            fill=False, ec=PCB_COLOR, lw=1.4, ls="--", label="PCB"))
        ax.add_patch(plt.Rectangle(
            (-2.3, g.Z_PCB_TOP), 4.6, 4.5,
            fill=False, ec=BJT_COLOR, lw=1.2, ls=":", label="TO-92 flat"))

    ax.set_aspect("equal")
    ax.set_title(title, fontsize=10)
    ax.set_xlabel("mm")
    ax.grid(alpha=0.25, lw=0.5)


def render_sections(tray, lid, path):
    fig, axes = plt.subplots(1, 3, figsize=(15, 5))

    draw_section(axes[0], tray, lid, (1, 0, 0), (0, 0, 0),
                 "Section across width (x=0)\nlip in rebate, snap ridge in groove")
    axes[0].legend(fontsize=7, loc="upper right")

    draw_section(axes[1], tray, lid, (0, 1, 0), (0, 0, 0),
                 "Section along length (y=0)\nUSB-C right, cable hole through the floor")

    # Zoom on one snap joint.
    draw_section(axes[2], tray, lid, (1, 0, 0), (0, 0, 0),
                 "Snap joint detail", extras=False)
    axes[2].set_xlim(g.OUTER_W / 2.0 - 4.0, g.OUTER_W / 2.0 + 0.6)
    axes[2].set_ylim(g.Z_REBATE - 1.5, g.Z_SPLIT + 1.5)
    axes[2].annotate(
        f"{g.DETENT_R:.2f} mm deflection to seat\n"
        f"{g.WALL - g.LIP_T - g.LIP_CLEAR - g.DETENT_R - g.DETENT_CLEAR:.2f} mm wall behind groove",
        xy=(g.OUTER_W / 2.0 - 2.0, g.Z_DETENT), fontsize=7,
        xytext=(-70, 28), textcoords="offset points",
        arrowprops=dict(arrowstyle="->", lw=0.8))

    for ax in axes:
        ax.set_ylabel("z (mm)")

    fig.tight_layout()
    fig.savefig(path, dpi=130)
    plt.close(fig)


def main():
    out_dir = os.path.dirname(os.path.abspath(__file__))
    g.check_parameters()
    tray, lid = g.build_bottom(), g.build_top()

    iso_path = os.path.join(out_dir, "preview_iso.png")
    sec_path = os.path.join(out_dir, "preview_sections.png")
    render_iso(tray, lid, iso_path)
    render_sections(tray, lid, sec_path)

    print(f"wrote {iso_path}")
    print(f"wrote {sec_path}")


if __name__ == "__main__":
    main()
