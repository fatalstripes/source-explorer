// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Qt-free model for the source tree drawer: what to show, and search filtering.
#include <string>
#include <vector>

namespace se {

enum class NodeKind { Section, Scene, Group, Item, GlobalAudio };

struct TreeNode {
	std::string key; // stable identity across refreshes (used to keep expansion/selection)
	std::string label;
	NodeKind kind = NodeKind::Item;
	bool visible = true; // scene-item enabled (Item/Group)
	bool locked = false; // scene-item locked (Item/Group)
	// Addressing for Item/Group nodes. `scene` is the scene or group that directly
	// contains the item, which is exactly the "sceneName" obs-websocket expects.
	std::string scene;
	long long itemId = 0;
	std::string rootScene; // top-level scene the node lives under (to switch the edit scene)
	std::vector<TreeNode> children;
};

// Case-insensitive (ASCII) substring search over labels. A node is kept when its
// own label matches (with its whole subtree) or when any descendant matches (with
// only the matching branches). A blank query returns the tree unchanged.
std::vector<TreeNode> filterTree(const std::vector<TreeNode> &nodes, const std::string &query);

// Changes whenever anything the drawer displays changes (structure, labels, toggles).
std::string treeSignature(const std::vector<TreeNode> &nodes);

} // namespace se
