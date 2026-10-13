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
	// Only filled in when asked for (the source picker's filters use them).
	std::string typeId;                    // source type id: "browser_source", "scene", "group"...
	std::vector<std::string> filterIds;    // type ids of the filters on the node's source
	bool active = false;                   // the node's source is on program / showing
	std::vector<TreeNode> children;
};

// Case-insensitive (ASCII) substring search over labels. A node is kept when its
// own label matches (with its whole subtree) or when any descendant matches (with
// only the matching branches). A blank query returns the tree unchanged.
std::vector<TreeNode> filterTree(const std::vector<TreeNode> &nodes, const std::string &query);

// What the source picker narrows the tree by. Defaults show everything.
struct TreeFilter {
	std::string query;       // name search, as filterTree
	std::string typeId;      // only sources of this type ("" = any)
	std::string filterId;    // only sources that have a filter of this type ("" = any)
	bool visibleOnly = false; // only scene items that are shown (a hidden group hides its children)
	bool activeOnly = false;  // only sources that are live
	bool showSources = true;  // false: scenes and groups only

	bool restrictsItems() const { return !typeId.empty() || !filterId.empty() || visibleOnly || activeOnly; }
};

// The tree after every part of `filter` is applied. A scene stays when something under it matches
// (or, with no item restrictions, always); ancestors of matches are kept, siblings are not.
std::vector<TreeNode> applyTreeFilter(const std::vector<TreeNode> &nodes, const TreeFilter &filter);

// Changes whenever anything the drawer displays changes (structure, labels, toggles).
std::string treeSignature(const std::vector<TreeNode> &nodes);

} // namespace se
