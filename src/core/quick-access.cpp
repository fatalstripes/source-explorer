// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/quick-access.hpp"

namespace se {

std::vector<QuickButton> quickButtonsFor(const SourceTraits &t)
{
	std::vector<QuickButton> out;
	if (t.hasItem) {
		out.push_back(QuickButton::Visibility);
		out.push_back(QuickButton::Lock);
	}
	out.push_back(QuickButton::Screenshot);
	out.push_back(QuickButton::ResetUi);
	out.push_back(QuickButton::Pin);

	if (t.hasAudio) {
		out.push_back(QuickButton::Mute);
		out.push_back(QuickButton::Monitoring);
	}
	if (t.isBrowser)
		out.push_back(QuickButton::RefreshCache);
	if (t.hasActivateButton)
		out.push_back(QuickButton::ActivateToggle);
	if (t.hasItem)
		out.push_back(QuickButton::Transform);
	if (t.isMedia) {
		out.push_back(QuickButton::MediaPlayPause);
		out.push_back(QuickButton::MediaStop);
		out.push_back(QuickButton::MediaRestart);
	}
	return out;
}

MonitorType nextMonitorType(MonitorType t)
{
	switch (t) {
	case MonitorType::None: return MonitorType::MonitorOnly;
	case MonitorType::MonitorOnly: return MonitorType::MonitorAndOutput;
	case MonitorType::MonitorAndOutput: return MonitorType::None;
	}
	return MonitorType::None;
}

bool contextMismatch(std::string_view editedScene, std::string_view itemScene)
{
	if (editedScene.empty() || itemScene.empty())
		return false;
	return editedScene != itemScene;
}

} // namespace se
