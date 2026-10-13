#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QDockWidget>

#include "ui/dock-placement.hpp"
#include "ui/explorer-dock.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("source-explorer", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Source Explorer: inspect and edit OBS sources, and copy Streamer.bot / obs-websocket commands.";
}

static constexpr const char *kDockId = "source-explorer-dock";

// OBS_FRONTEND_EVENT_FINISHED_LOADING guarantees the main window exists.
static void onFrontendEvent(enum obs_frontend_event event, void *)
{
	if (event != OBS_FRONTEND_EVENT_FINISHED_LOADING)
		return;
	// OBS takes ownership of the widget and wraps it in a QDockWidget.
	auto *dock = new se::ExplorerDock;
	if (!obs_frontend_add_dock_by_id(kDockId, "Source Explorer", dock)) {
		delete dock;
		return;
	}
	// A floating dock must always be grabbable: keep its title bar on screen and its size sane.
	if (auto *wrapper = qobject_cast<QDockWidget *>(dock->parentWidget()))
		new se::FloatingDockRescue(wrapper);
}

bool obs_module_load(void)
{
	obs_frontend_add_event_callback(onFrontendEvent, nullptr);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
	obs_frontend_remove_dock(kDockId);
}
