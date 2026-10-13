#pragma once
// RAII wrappers for libobs reference-counted / owned handles.
//
// Ownership rules (mirrors libobs):
//   obs_source_t   : obs_source_get_ref / obs_source_release   (refcounted)
//   obs_scene_t    : obs_scene_get_ref  / obs_scene_release    (refcounted)
//   obs_sceneitem_t: obs_sceneitem_addref / obs_sceneitem_release
//   obs_data_t     : obs_data_addref    / obs_data_release
//
// Functions that return a *new* reference (obs_source_get_settings,
// obs_get_source_by_name, obs_frontend_get_current_scene, ...) must be adopted
// with adopt(); functions that return a borrowed pointer (obs_scene_find_source,
// obs_scene_from_source) must be retained with retain() if kept past the call.

#include <obs.h>

#include <memory>
#include <utility>

namespace se::obs {

struct SourceDeleter   { void operator()(obs_source_t *p) const noexcept { obs_source_release(p); } };
struct SceneDeleter    { void operator()(obs_scene_t *p) const noexcept { obs_scene_release(p); } };
struct SceneItemDeleter{ void operator()(obs_sceneitem_t *p) const noexcept { obs_sceneitem_release(p); } };
struct DataDeleter     { void operator()(obs_data_t *p) const noexcept { obs_data_release(p); } };
struct PropertiesDeleter{ void operator()(obs_properties_t *p) const noexcept { obs_properties_destroy(p); } };
struct DataArrayDeleter{ void operator()(obs_data_array_t *p) const noexcept { obs_data_array_release(p); } };

using SourceRef    = std::unique_ptr<obs_source_t, SourceDeleter>;
using SceneRef     = std::unique_ptr<obs_scene_t, SceneDeleter>;
using SceneItemRef = std::unique_ptr<obs_sceneitem_t, SceneItemDeleter>;
using DataRef      = std::unique_ptr<obs_data_t, DataDeleter>;
using PropertiesRef = std::unique_ptr<obs_properties_t, PropertiesDeleter>;
using DataArrayRef = std::unique_ptr<obs_data_array_t, DataArrayDeleter>;

// --- adopt: take ownership of an already-owned (+1) reference ---------------
inline SourceRef adopt(obs_source_t *p) noexcept { return SourceRef(p); }
inline SceneRef adopt(obs_scene_t *p) noexcept { return SceneRef(p); }
inline SceneItemRef adopt(obs_sceneitem_t *p) noexcept { return SceneItemRef(p); }
inline DataRef adopt(obs_data_t *p) noexcept { return DataRef(p); }
inline DataArrayRef adopt(obs_data_array_t *p) noexcept { return DataArrayRef(p); }
inline PropertiesRef adopt(obs_properties_t *p) noexcept { return PropertiesRef(p); }

// --- retain: take a *new* reference to a borrowed pointer -------------------
inline SourceRef retain(obs_source_t *p) noexcept { return SourceRef(p ? obs_source_get_ref(p) : nullptr); }
inline SceneRef retain(obs_scene_t *p) noexcept { return SceneRef(p ? obs_scene_get_ref(p) : nullptr); }
inline SceneItemRef retain(obs_sceneitem_t *p) noexcept
{
	if (p)
		obs_sceneitem_addref(p);
	return SceneItemRef(p);
}
inline DataRef retain(obs_data_t *p) noexcept
{
	if (p)
		obs_data_addref(p);
	return DataRef(p);
}

// --- common acquisition helpers --------------------------------------------
inline SourceRef sourceByName(const char *name) noexcept { return adopt(obs_get_source_by_name(name)); }
inline DataRef settingsOf(obs_source_t *src) noexcept { return adopt(obs_source_get_settings(src)); }

} // namespace se::obs
