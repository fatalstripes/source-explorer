// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Where the plugin keeps its own small files (UI state, the filter clipboard).
// Defaults to OBS's per-user plugin_config folder; the environment variable SE_CONFIG_DIR
// overrides it (the headless smoke test uses that so it never touches real settings).
#include <QString>

class QSettings;

namespace se {

QString configDir(); // created on demand
QString configFile(const QString &name);

// Remembered UI state (expanded filter rows, last filter tab...). GUI thread only.
QSettings &uiSettings();

} // namespace se
