#include "core/transform-math.hpp"

#include <algorithm>
#include <cmath>

namespace se {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Anchor offset along one axis for a signed extent. Both or neither flag => center.
double anchorOffset(int alignment, int minFlag, int maxFlag, double extent)
{
	const bool lo = (alignment & minFlag) != 0;
	const bool hi = (alignment & maxFlag) != 0;
	if (lo == hi)
		return extent / 2.0;
	return lo ? 0.0 : extent;
}

double sign(double v, double fallback = 1.0)
{
	return v < 0.0 ? -1.0 : (v > 0.0 ? 1.0 : fallback);
}

// True if rotation is a multiple of 90 degrees; sets `swap` for odd multiples.
bool axisAligned(double rotation, bool &swap)
{
	const double q = rotation / 90.0;
	const double r = std::round(q);
	if (std::abs(q - r) > 1e-6)
		return false;
	swap = (static_cast<long long>(r) % 2) != 0;
	return true;
}

// Vector from the alignment anchor (== pos) to the item's visual center, in canvas space.
Point centerOffset(const ItemGeometry &g)
{
	const double sw = g.croppedW() * g.scaleX; // signed: a flipped item extends the other way
	const double sh = g.croppedH() * g.scaleY;

	const double ox = sw / 2.0 - anchorOffset(g.alignment, align::Left, align::Right, sw);
	const double oy = sh / 2.0 - anchorOffset(g.alignment, align::Top, align::Bottom, sh);

	const double rad = g.rotation * kPi / 180.0;
	const double cs = std::cos(rad), sn = std::sin(rad);
	return {ox * cs - oy * sn, ox * sn + oy * cs};
}

} // namespace

Point visualCenter(const ItemGeometry &g)
{
	const Point o = centerOffset(g);
	return {g.posX + o.x, g.posY + o.y};
}

double normalizeDegrees(double degrees)
{
	double d = std::fmod(degrees, 360.0);
	if (d < 0.0)
		d += 360.0;
	return d >= 360.0 ? 0.0 : d;
}

TransformPatch rotateAboutCenter(const ItemGeometry &g, double deltaDegrees)
{
	const Point center = visualCenter(g);
	ItemGeometry r = g;
	r.rotation = normalizeDegrees(g.rotation + deltaDegrees);
	const Point o = centerOffset(r);

	TransformPatch p;
	p.rotation = r.rotation;
	p.positionX = center.x - o.x;
	p.positionY = center.y - o.y;
	return p;
}

TransformPatch centerHorizontally(const ItemGeometry &g, Canvas c)
{
	TransformPatch p;
	p.positionX = centerOnCanvas(g, c).positionX;
	return p;
}

TransformPatch centerVertically(const ItemGeometry &g, Canvas c)
{
	TransformPatch p;
	p.positionY = centerOnCanvas(g, c).positionY;
	return p;
}

CopiedTransform copyTransform(const ItemGeometry &g)
{
	CopiedTransform c;
	c.posX = g.posX;
	c.posY = g.posY;
	c.rotation = g.rotation;
	c.alignment = g.alignment;
	c.cropLeft = g.cropLeft;
	c.cropRight = g.cropRight;
	c.cropTop = g.cropTop;
	c.cropBottom = g.cropBottom;
	const SizePx s = sizeOf(g);
	c.sizeW = s.w;
	c.sizeH = s.h;
	c.flipX = g.scaleX < 0.0;
	c.flipY = g.scaleY < 0.0;
	return c;
}

TransformPatch pasteTransform(const CopiedTransform &c, const ItemGeometry &target, bool targetUsesBounds)
{
	TransformPatch p;
	p.positionX = c.posX;
	p.positionY = c.posY;
	p.rotation = c.rotation;
	p.alignment = c.alignment;
	p.cropLeft = c.cropLeft;
	p.cropRight = c.cropRight;
	p.cropTop = c.cropTop;
	p.cropBottom = c.cropBottom;

	if (targetUsesBounds) {
		p.boundsWidth = c.sizeW;
		p.boundsHeight = c.sizeH;
		return p;
	}
	// The size is of the cropped rectangle, so work it out with the pasted crop in place.
	ItemGeometry t = target;
	t.cropLeft = c.cropLeft;
	t.cropRight = c.cropRight;
	t.cropTop = c.cropTop;
	t.cropBottom = c.cropBottom;
	const TransformPatch s = sizeToScalePatch(t, c.sizeW, c.sizeH);
	if (s.scaleX && s.scaleY) {
		p.scaleX = (c.flipX ? -1.0 : 1.0) * std::abs(*s.scaleX);
		p.scaleY = (c.flipY ? -1.0 : 1.0) * std::abs(*s.scaleY);
	}
	return p;
}

SizePx sizeOf(const ItemGeometry &g)
{
	return {std::abs(g.croppedW() * g.scaleX), std::abs(g.croppedH() * g.scaleY)};
}

TransformPatch sizeToScalePatch(const ItemGeometry &g, double widthPx, double heightPx)
{
	const double w = g.croppedW(), h = g.croppedH();
	if (w <= 0.0 || h <= 0.0 || !std::isfinite(widthPx) || !std::isfinite(heightPx))
		return {};
	TransformPatch p;
	p.scaleX = sign(g.scaleX) * std::abs(widthPx) / w;
	p.scaleY = sign(g.scaleY) * std::abs(heightPx) / h;
	return p;
}

TransformPatch reanchorAboutCenter(const ItemGeometry &g, int newAlignment)
{
	const Point center = visualCenter(g);
	ItemGeometry r = g;
	r.alignment = newAlignment;
	const Point o = centerOffset(r);

	TransformPatch p;
	p.alignment = newAlignment;
	p.positionX = center.x - o.x;
	p.positionY = center.y - o.y;
	return p;
}

TransformPatch flipAboutCenter(const ItemGeometry &g, bool horizontal)
{
	const Point center = visualCenter(g);
	ItemGeometry f = g;
	if (horizontal)
		f.scaleX = -g.scaleX;
	else
		f.scaleY = -g.scaleY;
	const Point o = centerOffset(f);

	TransformPatch p;
	if (horizontal)
		p.scaleX = f.scaleX;
	else
		p.scaleY = f.scaleY;
	p.positionX = center.x - o.x;
	p.positionY = center.y - o.y;
	return p;
}

TransformPatch centerOnCanvas(const ItemGeometry &g, Canvas c)
{
	const Point o = centerOffset(g);
	TransformPatch p;
	p.positionX = c.w / 2.0 - o.x;
	p.positionY = c.h / 2.0 - o.y;
	return p;
}

TransformPatch fitToCanvas(const ItemGeometry &g, Canvas c, FitMode mode)
{
	const double w = g.croppedW(), h = g.croppedH();
	if (w <= 0.0 || h <= 0.0 || c.w <= 0.0 || c.h <= 0.0)
		return {};

	const double rad = g.rotation * kPi / 180.0;
	const double bw = std::abs(w * std::cos(rad)) + std::abs(h * std::sin(rad)); // bbox at scale 1
	const double bh = std::abs(w * std::sin(rad)) + std::abs(h * std::cos(rad));

	double sx, sy;
	bool swap = false;
	if (mode == FitMode::Stretch && axisAligned(g.rotation, swap)) {
		sx = (swap ? c.h : c.w) / w;
		sy = (swap ? c.w : c.h) / h;
	} else {
		const double fx = c.w / bw, fy = c.h / bh;
		sx = sy = (mode == FitMode::Cover) ? std::max(fx, fy) : std::min(fx, fy);
	}

	ItemGeometry fitted = g;
	fitted.scaleX = sx * sign(g.scaleX);
	fitted.scaleY = sy * sign(g.scaleY);

	TransformPatch p = centerOnCanvas(fitted, c);
	p.scaleX = fitted.scaleX;
	p.scaleY = fitted.scaleY;
	return p;
}

TransformPatch fitBoundsToCanvas(int boundsAlignment, Canvas c)
{
	TransformPatch p;
	p.boundsWidth = c.w;
	p.boundsHeight = c.h;
	p.positionX = anchorOffset(boundsAlignment, align::Left, align::Right, c.w);
	p.positionY = anchorOffset(boundsAlignment, align::Top, align::Bottom, c.h);
	return p;
}

} // namespace se
