// SPDX-License-Identifier: GPL-2.0-or-later
#include "ui/filter-clipboard.hpp"

#include "core/filter-rules.hpp"
#include "core/obs-raii.hpp"
#include "util/plugin-config.hpp"

#include <QFile>

namespace se {

namespace {

std::string str(const char *s)
{
	return s ? s : "";
}

std::vector<ClipFilter> &memory()
{
	static std::vector<ClipFilter> v;
	return v;
}
bool &loaded()
{
	static bool b = false;
	return b;
}

} // namespace

ClipFilter snapshotFilter(obs_source_t *filter)
{
	ClipFilter c;
	c.name = str(obs_source_get_name(filter));
	c.kind = str(obs_source_get_id(filter));
	c.enabled = obs_source_enabled(filter);
	obs::DataRef settings = obs::settingsOf(filter);
	c.settingsJson = settings ? str(obs_data_get_json(settings.get())) : "{}";
	return c;
}

void writeFilterClipboard(const std::vector<ClipFilter> &filters)
{
	memory() = filters;
	loaded() = true;

	obs::DataRef root = obs::adopt(obs_data_create());
	obs::DataArrayRef arr = obs::adopt(obs_data_array_create());
	for (const ClipFilter &f : filters) {
		obs::DataRef item = obs::adopt(obs_data_create());
		obs_data_set_string(item.get(), "name", f.name.c_str());
		obs_data_set_string(item.get(), "kind", f.kind.c_str());
		obs_data_set_bool(item.get(), "enabled", f.enabled);
		obs_data_set_string(item.get(), "settings", f.settingsJson.c_str());
		obs_data_array_push_back(arr.get(), item.get());
	}
	obs_data_set_array(root.get(), "filters", arr.get());

	const QString path = configFile("filter-clipboard.json");
	if (path.isEmpty())
		return;
	QFile file(path);
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		file.write(str(obs_data_get_json(root.get())).c_str());
}

std::vector<ClipFilter> readFilterClipboard()
{
	if (loaded())
		return memory();
	loaded() = true;

	const QString path = configFile("filter-clipboard.json");
	QFile file(path);
	if (path.isEmpty() || !file.open(QIODevice::ReadOnly))
		return memory();
	const QByteArray bytes = file.readAll();
	obs::DataRef root = obs::adopt(obs_data_create_from_json(bytes.constData()));
	if (!root)
		return memory();
	obs::DataArrayRef arr = obs::adopt(obs_data_get_array(root.get(), "filters"));
	for (size_t i = 0, n = arr ? obs_data_array_count(arr.get()) : 0; i < n; ++i) {
		obs::DataRef item = obs::adopt(obs_data_array_item(arr.get(), i));
		ClipFilter c;
		c.name = str(obs_data_get_string(item.get(), "name"));
		c.kind = str(obs_data_get_string(item.get(), "kind"));
		c.enabled = obs_data_get_bool(item.get(), "enabled");
		c.settingsJson = str(obs_data_get_string(item.get(), "settings"));
		if (!c.kind.empty())
			memory().push_back(std::move(c));
	}
	return memory();
}

bool filterClipboardEmpty()
{
	return readFilterClipboard().empty();
}

PasteResult pasteFilters(obs_source_t *parent, const std::vector<ClipFilter> &filters)
{
	PasteResult result;
	if (!parent)
		return result;
	const uint32_t parentFlags = obs_source_get_output_flags(parent);

	for (const ClipFilter &f : filters) {
		const char *display = obs_source_get_display_name(f.kind.c_str());
		const uint32_t flags = display ? obs_get_source_output_flags(f.kind.c_str()) : 0;
		if (!display || !fitsSource(parentFlags, flags)) {
			++result.skipped;
			continue;
		}
		const std::string name = uniqueFilterName(f.name, [&](const std::string &n) {
			return obs::adopt(obs_source_get_filter_by_name(parent, n.c_str())) != nullptr;
		});
		obs::DataRef settings = obs::adopt(obs_data_create_from_json(f.settingsJson.c_str()));
		obs::SourceRef copy =
			obs::adopt(obs_source_create_private(f.kind.c_str(), name.c_str(), settings.get()));
		if (!copy) {
			++result.skipped;
			continue;
		}
		obs_source_set_enabled(copy.get(), f.enabled);
		obs_source_filter_add(parent, copy.get());
		++result.pasted;
		result.lastName = name;
	}
	return result;
}

} // namespace se
