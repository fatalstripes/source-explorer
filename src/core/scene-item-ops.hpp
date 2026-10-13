// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// libobs glue between a scene item and the pure transform math. GUI thread only.
// "plan*" functions only compute a patch (so the UI can copy it as a command);
// apply() writes a patch to the item.
#include "core/transform-math.hpp"

#include <obs.h>

namespace se::ops {

// The base canvas: scene coordinates live in it, so fit/center work for any
// shape (landscape, vertical, ultrawide). Falls back to 1920x1080 if OBS has no video.
Canvas canvas();

bool usesBounds(obs_sceneitem_t *item);

// For bounding-box items the "visible rectangle" is the bounds box; scale and crop
// do not change what is drawn there, so they are presented as 1 / 0.
ItemGeometry geometry(obs_sceneitem_t *item);

TransformPatch planCenter(obs_sceneitem_t *item);
TransformPatch planFit(obs_sceneitem_t *item, FitMode mode);
TransformPatch planRotate(obs_sceneitem_t *item, double deltaDegrees);
TransformPatch planCenterHorizontally(obs_sceneitem_t *item);
TransformPatch planCenterVertically(obs_sceneitem_t *item);

// Copy / paste transform. The copy lives for the session and is shared by every panel.
void copyTransform(obs_sceneitem_t *item);
bool hasCopiedTransform();
TransformPatch planPaste(obs_sceneitem_t *item); // empty if nothing has been copied
// Empty patch for bounding-box items: libobs derives their scale from the bounds,
// so a sign flip there is not reliable.
TransformPatch planFlip(obs_sceneitem_t *item, bool horizontal);

// New anchor, same place on the canvas: the patch carries the alignment AND the recomputed position.
TransformPatch planReanchor(obs_sceneitem_t *item, int newAlignment);

// Every transform field the item has right now (position, rotation, scale, alignment, bounds, crop).
// Used for "full transform" copies and copy/paste.
TransformPatch currentPatch(obs_sceneitem_t *item);

// The factory transform for a fresh item on the current canvas.
TransformPatch defaultPatch();

void apply(obs_sceneitem_t *item, const TransformPatch &patch);

} // namespace se::ops
