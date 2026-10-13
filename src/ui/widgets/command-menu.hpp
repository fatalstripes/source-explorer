// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// The "Command Copier": right-click menu offering Streamer.bot / obs-websocket
// copies and Reset to Default for any widget.
//
//   - Right-clicking a FIELD gives the command for that one value (the top three entries).
//   - Right-clicking a field's LABEL gives the multi-value commands as submenus, e.g. "Position
//     (X and Y)" or "Full transform" (the `choices` below); each submenu offers the same three copies.
#include "core/payload-generator.hpp"

#include <QWidget>

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace se {

// One copyable thing that is shown as a submenu: a single value or a combined group of values.
struct CommandChoice {
	std::string label; // e.g. "Position (X and Y)"
	std::function<std::string()> streamerBot;
	std::function<std::string()> wsSet;
	std::function<std::string()> wsGet;
};

struct CommandSet {
	std::function<std::string()> streamerBot; // Copy Streamer.Bot C# Command
	std::function<std::string()> wsSet;       // Copy OBS WebSocket v5 (Set)
	std::function<std::string()> wsGet;       // Copy OBS WebSocket v5 (Get)
	std::function<void()> reset;              // Reset to Default

	// Optional plain actions (label, action) that are not copies, e.g. "Pop out into a window".
	// Shown first, above the copy entries.
	std::vector<std::pair<std::string, std::function<void()>>> actions;

	// Optional additional copy entries (label, generator), shown after "Get".
	std::vector<std::pair<std::string, std::function<std::string()>>> extras;

	// Optional submenus, shown after the extras. A menu may have only choices (label menus do).
	std::vector<CommandChoice> choices;
	// Optional extra reset entries (label, action), e.g. "Reset transform".
	std::vector<std::pair<std::string, std::function<void()>>> resets;
};

// Builds a choice from a Set and a Get request. The requests are captured by value, so the menu
// copies what was true when it opened even if the UI changes while it is showing.
inline CommandChoice makeChoice(std::string label, ObsRequest set, ObsRequest get)
{
	CommandChoice c;
	c.label = std::move(label);
	c.streamerBot = [set] { return sbFrom(set); };
	c.wsSet = [set] { return wsFrom(set); };
	c.wsGet = [get] { return wsFrom(get); };
	return c;
}

// The top-level three entries from a Set and a Get request.
inline CommandSet makeSet(ObsRequest set, ObsRequest get)
{
	CommandSet c;
	c.streamerBot = [set] { return sbFrom(set); };
	c.wsSet = [set] { return wsFrom(set); };
	c.wsGet = [get] { return wsFrom(get); };
	return c;
}

// `provider` is evaluated on each right-click, so it always sees current state;
// return nullopt when there is no target (menu is then not shown).
// Also covers a QAbstractSpinBox's inner line edit. Works on labels too.
void attachCommandMenu(QWidget *w, std::function<std::optional<CommandSet>()> provider);

} // namespace se
