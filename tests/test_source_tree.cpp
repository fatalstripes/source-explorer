// SPDX-License-Identifier: GPL-2.0-or-later
// Tests for core/tree-model and the scene-tree websocket requests.
#include "core/payload-generator.hpp"
#include "core/tree-model.hpp"

#include <cstdio>
#include <string>

static int g_failed = 0, g_checks = 0;

static void eq(const std::string &a, const std::string &e, int line)
{
	++g_checks;
	if (a != e) {
		++g_failed;
		std::printf("FAIL line %d\n  expected: %s\n  actual:   %s\n", line, e.c_str(), a.c_str());
	}
}
#define EQ(a, e) eq((a), (e), __LINE__)

using namespace se;

static TreeNode node(const std::string &label, NodeKind k = NodeKind::Item, std::vector<TreeNode> kids = {})
{
	TreeNode n;
	n.key = "k:" + label;
	n.label = label;
	n.kind = k;
	n.children = std::move(kids);
	return n;
}

// Flatten to "a/b/c;" style for compact assertions.
static void flat(const std::vector<TreeNode> &v, const std::string &prefix, std::string &out)
{
	for (const auto &n : v) {
		out += prefix + n.label + ";";
		flat(n.children, prefix + n.label + "/", out);
	}
}
static std::string flatten(const std::vector<TreeNode> &v)
{
	std::string s;
	flat(v, "", s);
	return s;
}

static std::string same(const std::vector<TreeNode> &a, const std::vector<TreeNode> &b)
{
	return treeSignature(a) == treeSignature(b) ? "same" : "diff";
}

int main()
{
	const std::vector<TreeNode> tree = {
		node("Gameplay", NodeKind::Scene,
		     {node("Webcam"), node("Overlays", NodeKind::Group, {node("Alerts"), node("Chat Box")}),
		      node("Game Capture")}),
		node("Starting Soon", NodeKind::Scene, {node("Countdown"), node("Webcam Frame")}),
		node("Global Audio", NodeKind::Section, {node("Mic/Aux", NodeKind::GlobalAudio)}),
	};

	// Blank / whitespace queries leave the tree alone.
	EQ(flatten(filterTree(tree, "")), flatten(tree));
	EQ(flatten(filterTree(tree, "   ")), flatten(tree));

	// Descendant match keeps the ancestor chain only.
	EQ(flatten(filterTree(tree, "alerts")), "Gameplay;Gameplay/Overlays;Gameplay/Overlays/Alerts;");

	// Case-insensitive; matches in several scenes.
	EQ(flatten(filterTree(tree, "WEBCAM")),
	   "Gameplay;Gameplay/Webcam;Starting Soon;Starting Soon/Webcam Frame;");

	// A label match keeps the node's whole subtree (the group and both children).
	EQ(flatten(filterTree(tree, "overlays")),
	   "Gameplay;Gameplay/Overlays;Gameplay/Overlays/Alerts;Gameplay/Overlays/Chat Box;");

	// Matching a scene name keeps all of its items.
	EQ(flatten(filterTree(tree, "starting")),
	   "Starting Soon;Starting Soon/Countdown;Starting Soon/Webcam Frame;");

	EQ(flatten(filterTree(tree, "zzz")), "");
	EQ(flatten(filterTree(tree, "mic")), "Global Audio;Global Audio/Mic/Aux;");

	// Filtering must not mutate the original.
	EQ(std::to_string(tree[0].children.size()), "3");

	// Signature reacts to toggles, renames and structure, and is stable otherwise.
	auto b = tree;
	EQ(same(tree, b), "same");
	b[0].children[0].visible = false;
	EQ(same(tree, b), "diff");
	b = tree;
	b[0].children[0].locked = true;
	EQ(same(tree, b), "diff");
	b = tree;
	b[1].label = "Intermission";
	EQ(same(tree, b), "diff");
	b = tree;
	b[0].children.pop_back();
	EQ(same(tree, b), "diff");
	// Moving a child to another parent changes the signature even though the same
	// nodes still exist.
	b = tree;
	auto moved = b[0].children.back();
	b[0].children.pop_back();
	b[1].children.push_back(moved);
	EQ(same(tree, b), "diff");

	// Scene-tree websocket requests; group children use the group's name as sceneName.
	const SceneItemTarget item{"Overlays", 12};
	EQ(wsFrom(reqSetSceneItemEnabled(item, false), "e1"),
	   R"({"op":6,"d":{"requestType":"SetSceneItemEnabled","requestId":"e1","requestData":)"
	   R"({"sceneName":"Overlays","sceneItemId":12,"sceneItemEnabled":false}}})");
	EQ(reqSetSceneItemLocked(item, true).data.dump(),
	   R"({"sceneName":"Overlays","sceneItemId":12,"sceneItemLocked":true})");
	EQ(reqGetSceneItemEnabled(item).data.dump(), R"({"sceneName":"Overlays","sceneItemId":12})");
	EQ(reqGetSceneItemLocked(item).type, "GetSceneItemLocked");
	EQ(reqSetCurrentProgramScene("Gameplay").data.dump(), R"({"sceneName":"Gameplay"})");
	EQ(wsFrom(reqGetCurrentProgramScene(), "p"),
	   R"({"op":6,"d":{"requestType":"GetCurrentProgramScene","requestId":"p","requestData":{}}})");

	std::printf("%d checks, %d failed\n", g_checks, g_failed);
	return g_failed ? 1 : 0;
}
