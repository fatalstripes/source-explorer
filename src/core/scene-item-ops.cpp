#include "core/scene-item-ops.hpp"

#include <cmath>
#include <optional>

namespace se::ops {

namespace {

bool finite(const std::optional<double> &v)
{
	return v && std::isfinite(*v);
}

} // namespace

Canvas canvas()
{
	obs_video_info ovi{};
	if (obs_get_video_info(&ovi) && ovi.base_width && ovi.base_height)
		return {static_cast<double>(ovi.base_width), static_cast<double>(ovi.base_height)};
	return {1920.0, 1080.0};
}

bool usesBounds(obs_sceneitem_t *item)
{
	return obs_sceneitem_get_bounds_type(item) != OBS_BOUNDS_NONE;
}

ItemGeometry geometry(obs_sceneitem_t *item)
{
	obs_transform_info ti;
	obs_sceneitem_get_info2(item, &ti);

	ItemGeometry g;
	g.rotation = ti.rot;
	g.alignment = static_cast<int>(ti.alignment);
	g.posX = ti.pos.x;
	g.posY = ti.pos.y;

	if (ti.bounds_type != OBS_BOUNDS_NONE) {
		g.srcW = ti.bounds.x;
		g.srcH = ti.bounds.y;
		return g; // scale 1, no crop: the bounds box is what is drawn
	}

	obs_sceneitem_crop crop;
	obs_sceneitem_get_crop(item, &crop);
	if (obs_source_t *src = obs_sceneitem_get_source(item)) {
		g.srcW = obs_source_get_width(src);
		g.srcH = obs_source_get_height(src);
	}
	g.cropLeft = crop.left;
	g.cropRight = crop.right;
	g.cropTop = crop.top;
	g.cropBottom = crop.bottom;
	g.scaleX = ti.scale.x;
	g.scaleY = ti.scale.y;
	return g;
}

TransformPatch planCenter(obs_sceneitem_t *item)
{
	return centerOnCanvas(geometry(item), canvas());
}

TransformPatch planFit(obs_sceneitem_t *item, FitMode mode)
{
	const Canvas c = canvas();
	const ItemGeometry g = geometry(item);
	if (!usesBounds(item))
		return fitToCanvas(g, c, mode);

	// Bounding-box item: "fit" means the box covers the canvas.
	if (normalizeDegrees(g.rotation) == 0.0)
		return fitBoundsToCanvas(g.alignment, c); // the anchor of `alignment` on the canvas

	// Rotated box: size it with the generic fit, then express the scale as a box size.
	TransformPatch p = fitToCanvas(g, c, mode);
	if (!p.scaleX || !p.scaleY)
		return {};
	p.boundsWidth = g.srcW * std::abs(*p.scaleX);
	p.boundsHeight = g.srcH * std::abs(*p.scaleY);
	p.scaleX.reset();
	p.scaleY.reset();
	return p;
}

TransformPatch planRotate(obs_sceneitem_t *item, double deltaDegrees)
{
	return rotateAboutCenter(geometry(item), deltaDegrees);
}

TransformPatch planCenterHorizontally(obs_sceneitem_t *item)
{
	return centerHorizontally(geometry(item), canvas());
}

TransformPatch planCenterVertically(obs_sceneitem_t *item)
{
	return centerVertically(geometry(item), canvas());
}

namespace {
std::optional<CopiedTransform> g_copied;
}

void copyTransform(obs_sceneitem_t *item)
{
	if (item)
		g_copied = se::copyTransform(geometry(item));
}

bool hasCopiedTransform()
{
	return g_copied.has_value();
}

TransformPatch planPaste(obs_sceneitem_t *item)
{
	if (!item || !g_copied)
		return {};
	return pasteTransform(*g_copied, geometry(item), usesBounds(item));
}

TransformPatch planFlip(obs_sceneitem_t *item, bool horizontal)
{
	if (usesBounds(item))
		return {};
	return flipAboutCenter(geometry(item), horizontal);
}

TransformPatch planReanchor(obs_sceneitem_t *item, int newAlignment)
{
	return reanchorAboutCenter(geometry(item), newAlignment);
}

TransformPatch currentPatch(obs_sceneitem_t *item)
{
	TransformPatch p;
	if (!item)
		return p;
	obs_transform_info ti;
	obs_sceneitem_crop crop;
	obs_sceneitem_get_info2(item, &ti);
	obs_sceneitem_get_crop(item, &crop);

	p.positionX = ti.pos.x;
	p.positionY = ti.pos.y;
	p.rotation = ti.rot;
	p.scaleX = ti.scale.x;
	p.scaleY = ti.scale.y;
	p.alignment = static_cast<int>(ti.alignment);
	p.boundsType = static_cast<BoundsType>(ti.bounds_type);
	p.boundsAlignment = static_cast<int>(ti.bounds_alignment);
	p.boundsWidth = ti.bounds.x;
	p.boundsHeight = ti.bounds.y;
	p.cropLeft = crop.left;
	p.cropRight = crop.right;
	p.cropTop = crop.top;
	p.cropBottom = crop.bottom;
	return p;
}

TransformPatch defaultPatch()
{
	const Canvas c = canvas();
	return defaultTransform(c.w, c.h);
}

void apply(obs_sceneitem_t *item, const TransformPatch &p)
{
	if (!item)
		return;
	obs_transform_info ti;
	obs_sceneitem_crop crop;
	obs_sceneitem_get_info2(item, &ti);
	obs_sceneitem_get_crop(item, &crop);

	if (finite(p.positionX)) ti.pos.x = float(*p.positionX);
	if (finite(p.positionY)) ti.pos.y = float(*p.positionY);
	if (finite(p.scaleX)) ti.scale.x = float(*p.scaleX);
	if (finite(p.scaleY)) ti.scale.y = float(*p.scaleY);
	if (finite(p.rotation)) ti.rot = float(*p.rotation);
	if (p.alignment) ti.alignment = uint32_t(*p.alignment);
	if (p.boundsType) ti.bounds_type = static_cast<obs_bounds_type>(*p.boundsType);
	if (p.boundsAlignment) ti.bounds_alignment = uint32_t(*p.boundsAlignment);
	if (finite(p.boundsWidth)) ti.bounds.x = float(*p.boundsWidth);
	if (finite(p.boundsHeight)) ti.bounds.y = float(*p.boundsHeight);
	if (p.cropLeft) crop.left = *p.cropLeft;
	if (p.cropRight) crop.right = *p.cropRight;
	if (p.cropTop) crop.top = *p.cropTop;
	if (p.cropBottom) crop.bottom = *p.cropBottom;

	obs_sceneitem_set_info2(item, &ti);
	obs_sceneitem_set_crop(item, &crop);
}

} // namespace se::ops
