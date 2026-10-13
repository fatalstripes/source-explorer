// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Qt-free rules for the quick-access bar and the status bar.
#include "core/payload-generator.hpp"

#include <string_view>
#include <vector>

namespace se {

enum class QuickButton {
	Visibility,     // show/hide the scene item
	Lock,           // lock/unlock the scene item
	Screenshot,
	ResetUi,
	Pin,            // hold the current source instead of following OBS's selection
	Mute,
	Monitoring,     // cycles off -> monitor only -> monitor and output
	RefreshCache,   // browser sources
	ActivateToggle, // video capture devices (their "Activate/Deactivate" button)
	Transform,      // popup: flip, rotate, fit, stretch, centre
	MediaPlayPause,
	MediaStop,
	MediaRestart,
	CloseFilterWindows, // only while filter pop-out windows are open
};

// What the loaded source is and can do. Decided once per source, not per tick.
struct SourceTraits {
	bool hasItem = false;           // a scene item exists (not a bare device, scene or group)
	bool hasAudio = false;
	bool isBrowser = false;
	bool hasActivateButton = false; // the source exposes an "activate" button property
	bool isMedia = false;           // controllable media (play/pause/stop/restart)
	int openFilterWindows = 0;      // filter pop-out windows currently open (any source)
};

// The buttons to show, in display order. Visibility, Lock, Screenshot, Reset UI and Pin come first
// (the first two only when there is a scene item to act on); the rest depend on the source.
std::vector<QuickButton> quickButtonsFor(const SourceTraits &t);

// off -> monitor only -> monitor and output -> off
MonitorType nextMonitorType(MonitorType t);

// True when the loaded item belongs to a different scene from the one being edited, so the user
// should be warned that the context differs. Unknown on either side means no warning.
bool contextMismatch(std::string_view editedScene, std::string_view itemScene);

} // namespace se
