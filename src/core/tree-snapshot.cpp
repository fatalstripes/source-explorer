// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/tree-snapshot.hpp"

#include <obs-frontend-api.h>
#include <obs.h>

namespace se {

namespace {

constexpr int kMaxGroupDepth = 8; // groups cannot nest in OBS today; guard against cycles anyway
constexpr int kGlobalAudioChannels = 6; // OBS uses output channels 1-6 for desktop/aux devices

std::string nameOf(obs_source_t *s)
{
	const char *n = s ? obs_source_get_name(s) : nullptr;
	return n ? n : "";
}

struct ItemCollector {
	std::vector<obs::SceneItemRef> items;
	static bool cb(obs_scene_t *, obs_sceneitem_t *item, void *p)
	{
		static_cast<ItemCollector *>(p)->items.push_back(obs::retain(item)); // enum gives borrowed pointers
		return true;
	}
};

void addMetadata(TreeNode &n, obs_source_t *src)
{
	const char *id = obs_source_get_id(src);
	n.typeId = id ? id : "";
	n.active = obs_source_active(src);
	obs_source_enum_filters(
		src,
		[](obs_source_t *, obs_source_t *filter, void *p) {
			const char *fid = obs_source_get_id(filter);
			if (fid)
				static_cast<std::vector<std::string> *>(p)->push_back(fid);
		},
		&n.filterIds);
}

void collectItems(obs_scene_t *scene, const std::string &sceneName, const std::string &rootScene,
		  const std::string &parentKey, std::vector<TreeNode> &out, int depth, bool metadata)
{
	if (!scene || depth > kMaxGroupDepth)
		return;
	ItemCollector c;
	obs_scene_enum_items(scene, &ItemCollector::cb, &c);

	// libobs enumerates bottom-to-top; the tree shows the top-most item first, like OBS's own list.
	for (auto it = c.items.rbegin(); it != c.items.rend(); ++it) {
		obs_sceneitem_t *item = it->get();
		TreeNode n;
		n.itemId = obs_sceneitem_get_id(item);
		n.scene = sceneName;
		n.rootScene = rootScene;
		n.label = nameOf(obs_sceneitem_get_source(item));
		n.key = parentKey + "/" + std::to_string(n.itemId);
		n.visible = obs_sceneitem_visible(item);
		n.locked = obs_sceneitem_locked(item);
		if (metadata)
			addMetadata(n, obs_sceneitem_get_source(item));
		if (obs_sceneitem_is_group(item)) {
			n.kind = NodeKind::Group;
			collectItems(obs_sceneitem_group_get_scene(item), n.label, rootScene, n.key, n.children, depth + 1,
				     metadata);
		} else {
			n.kind = NodeKind::Item;
		}
		out.push_back(std::move(n));
	}
}

// Every selection change fires signals that other plugins and scripts may react to, so only touch
// items whose state actually has to change.
void deselectAllExcept(obs_scene_t *scene, obs_sceneitem_t *keep, int depth = 0)
{
	if (!scene || depth > kMaxGroupDepth)
		return;
	ItemCollector c;
	obs_scene_enum_items(scene, &ItemCollector::cb, &c);
	for (auto &item : c.items) {
		if (item.get() != keep && obs_sceneitem_selected(item.get()))
			obs_sceneitem_select(item.get(), false);
		if (obs_sceneitem_is_group(item.get()))
			deselectAllExcept(obs_sceneitem_group_get_scene(item.get()), keep, depth + 1);
	}
}

} // namespace

std::vector<TreeNode> snapshotTree(bool metadata)
{
	std::vector<TreeNode> out;

	auto addScene = [&](obs_source_t *s) {
		TreeNode n;
		n.label = nameOf(s);
		n.key = "s:" + n.label;
		n.kind = NodeKind::Scene;
		n.scene = n.rootScene = n.label;
		if (metadata)
			addMetadata(n, s);
		collectItems(obs_scene_from_source(s), n.label, n.label, n.key, n.children, 0, metadata);
		out.push_back(std::move(n));
	};

	obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes); // in the order of OBS's own scene list
	for (size_t i = 0; i < scenes.sources.num; ++i)
		addScene(scenes.sources.array[i]);
	obs_frontend_source_list_free(&scenes);
	if (out.empty()) {
		// No frontend (e.g. the headless test): fall back to libobs's own list of scenes.
		obs_enum_scenes(
			[](void *p, obs_source_t *s) {
				(*static_cast<decltype(addScene) *>(p))(s);
				return true;
			},
			&addScene);
	}

	TreeNode audio;
	audio.label = "Global Audio";
	audio.key = "g:";
	audio.kind = NodeKind::Section;
	for (uint32_t ch = 1; ch <= kGlobalAudioChannels; ++ch) {
		obs::SourceRef s = obs::adopt(obs_get_output_source(ch));
		if (!s)
			continue;
		TreeNode n;
		n.label = nameOf(s.get());
		n.key = "a:" + n.label;
		n.kind = NodeKind::GlobalAudio;
		if (metadata)
			addMetadata(n, s.get());
		audio.children.push_back(std::move(n));
	}
	if (!audio.children.empty())
		out.push_back(std::move(audio));
	return out;
}

obs::SceneItemRef findSceneItem(const std::string &sceneName, long long itemId)
{
	obs::SourceRef src = obs::sourceByName(sceneName.c_str());
	obs_scene_t *scene = src ? obs_scene_from_source(src.get()) : nullptr; // also works for groups
	return scene ? obs::retain(obs_scene_find_sceneitem_by_id(scene, itemId)) : obs::SceneItemRef();
}

obs::SourceRef editScene()
{
	return obs::adopt(obs_frontend_preview_program_mode_active() ? obs_frontend_get_current_preview_scene()
								     : obs_frontend_get_current_scene());
}

SelectResult selectSceneItem(const std::string &root, const std::string &sceneName, long long itemId)
{
	SelectResult r;
	obs::SourceRef rootSrc = obs::sourceByName(root.c_str());
	obs_scene_t *rootScene = rootSrc ? obs_scene_from_source(rootSrc.get()) : nullptr;
	if (!rootScene)
		return r;

	obs::SourceRef edit = editScene();
	if (!edit || nameOf(edit.get()) != root) {
		if (!obs_frontend_preview_program_mode_active()) {
			// Without Studio Mode, changing the edited scene changes what viewers see.
			r.message = "\"" + root +
				    "\" is not the live scene. Double-click it to switch, or turn on Studio Mode to edit it "
				    "without going live.";
			return r;
		}
		obs_frontend_set_current_preview_scene(rootSrc.get()); // preview only: safe
	}

	auto target = findSceneItem(sceneName, itemId);
	if (!target)
		return r;
	deselectAllExcept(rootScene, target.get());
	if (!obs_sceneitem_selected(target.get()))
		obs_sceneitem_select(target.get(), true);
	r.selected = true;
	return r;
}

} // namespace se
