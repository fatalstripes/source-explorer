// SPDX-License-Identifier: GPL-2.0-or-later
#include "util/plugin-config.hpp"

#include <util/base.h>
#include <util/bmem.h>
#include <util/platform.h>

#include <QDir>
#include <QSettings>

namespace se {

QString configDir()
{
	static QString dir = [] {
		QString d = qEnvironmentVariable("SE_CONFIG_DIR");
		if (d.isEmpty()) {
			char *p = os_get_config_path_ptr("obs-studio/plugin_config/source-explorer");
			if (p) {
				d = QString::fromUtf8(p);
				bfree(p);
			}
		}
		if (!d.isEmpty())
			QDir().mkpath(d);
		return d;
	}();
	return dir;
}

QString configFile(const QString &name)
{
	const QString d = configDir();
	return d.isEmpty() ? QString() : QDir(d).filePath(name);
}

QSettings &uiSettings()
{
	static QSettings *s = [] {
		const QString f = configFile("ui-state.ini");
		return f.isEmpty() ? new QSettings() : new QSettings(f, QSettings::IniFormat);
	}();
	return *s;
}

} // namespace se
