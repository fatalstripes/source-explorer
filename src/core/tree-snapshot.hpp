// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// libobs glue shared by the source tree drawer and the source picker: read the scene hierarchy
// into TreeNodes, find items, and select one the way a click in the tree does. GUI thread only.
#include "core/obs-raii.hpp"
#include "core/tree-model.hpp"

#include <string>
#include <vector>

namespace se {

// Every scene with its items (groups nested), then the global audio devices. With `metadata`, nodes
// also carry their source type, filter types and live state, which costs an enumeration per source.
std::vector<TreeNode> snapshotTree(bool metadata);

obs::SceneItemRef findSceneItem(const std::string &sceneName, long long itemId);

// The scene being edited: the preview scene in Studio Mode, otherwise the program scene.
obs::SourceRef editScene();

struct SelectResult {
	bool selected = false;
	std::string message; // why nothing was selected, when the user should be told
};

// Select the scene item `itemId` of `sceneName` (a scene or a group) in OBS. If it lives in a scene
// other than the one being edited, the preview is switched when Studio Mode is on; otherwise nothing
// changes (switching would put that scene live) and `message` explains.
SelectResult selectSceneItem(const std::string &rootScene, const std::string &sceneName, long long itemId);

} // namespace se
