// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Canvas-aware transform math (pure C++, no libobs). Works for any canvas
// shape: landscape, vertical (1080x1920), ultrawide, etc.
//
// Conventions match libobs: pos is the point on the (cropped) item at the
// alignment anchor; rotation is degrees clockwise (y down) about that anchor;
// negative scale flips and extends the item the other way from the anchor.
#include "core/payload-generator.hpp"

namespace se {

struct Canvas {
	double w = 1920.0;
	double h = 1080.0;
};

struct ItemGeometry {
	double srcW = 0.0, srcH = 0.0; // uncropped source size
	int cropLeft = 0, cropRight = 0, cropTop = 0, cropBottom = 0;
	double scaleX = 1.0, scaleY = 1.0;
	double rotation = 0.0;
	int alignment = align::TopLeft;
	double posX = 0.0, posY = 0.0; // only needed by the "about center" operations

	double croppedW() const { return srcW - cropLeft - cropRight; }
	double croppedH() const { return srcH - cropTop - cropBottom; }
};

struct Point {
	double x = 0.0, y = 0.0;
};

// Canvas-space center of the item's visible (cropped, scaled, rotated) rectangle.
Point visualCenter(const ItemGeometry &g);

// Rotation in [0, 360).
double normalizeDegrees(double degrees);

// Rotate/flip while keeping the item's visual center where it is, so the item turns
// in place instead of swinging around its alignment anchor. Patches carry only the
// changed fields (rotation or scale, plus the compensating position).
TransformPatch rotateAboutCenter(const ItemGeometry &g, double deltaDegrees);

// Centre on one axis only: the patch carries just that axis' position.
TransformPatch centerHorizontally(const ItemGeometry &g, Canvas c);
TransformPatch centerVertically(const ItemGeometry &g, Canvas c);

// "Copy transform" / "Paste transform": position, size, rotation, alignment, crop and flips travel
// as plain values and are applied to any other source. Size is in pixels, so pasting onto a source
// of a different native size gives the same on-canvas size (not the same scale).
struct CopiedTransform {
	double posX = 0.0, posY = 0.0;
	double rotation = 0.0;
	int alignment = align::TopLeft;
	int cropLeft = 0, cropRight = 0, cropTop = 0, cropBottom = 0;
	double sizeW = 0.0, sizeH = 0.0; // on-canvas pixels
	bool flipX = false, flipY = false;
};
CopiedTransform copyTransform(const ItemGeometry &g);
// `targetUsesBounds`: the target is sized by a bounding box, so the size goes to the box.
TransformPatch pasteTransform(const CopiedTransform &c, const ItemGeometry &target, bool targetUsesBounds);

// On-canvas size in pixels (cropped source size x |scale|), as OBS's Transform dialog shows it.
struct SizePx {
	double w = 0.0, h = 0.0;
};
SizePx sizeOf(const ItemGeometry &g);

// obs-websocket cannot set width/height (they are read-only), so a size edit is expressed as the
// scale that produces it. Flips (negative scale) are kept. Empty patch if the source has no size.
TransformPatch sizeToScalePatch(const ItemGeometry &g, double widthPx, double heightPx);

// Change the alignment (anchor) without moving the item on the canvas. Position is the location
// of the anchor, so a new anchor needs a new position. The patch carries the alignment and the
// recomputed position; the item's visual centre is unchanged, for any rotation, flip or crop.
TransformPatch reanchorAboutCenter(const ItemGeometry &g, int newAlignment);
TransformPatch flipAboutCenter(const ItemGeometry &g, bool horizontal);

enum class FitMode {
	Contain, // letterbox: whole item visible, aspect preserved
	Cover,   // fill canvas, aspect preserved, overflow cropped by canvas
	Stretch  // fill canvas exactly (axis-aligned rotations only; else Contain)
};

// Position only: centers the item's visual center on the canvas.
TransformPatch centerOnCanvas(const ItemGeometry &g, Canvas c);

// Scale + position. Returns an empty patch if the item has no visible size.
TransformPatch fitToCanvas(const ItemGeometry &g, Canvas c, FitMode mode);

// For items using a bounding box: bounds = canvas, position = anchor of the
// bounds alignment on the canvas.
TransformPatch fitBoundsToCanvas(int boundsAlignment, Canvas c);

} // namespace se
