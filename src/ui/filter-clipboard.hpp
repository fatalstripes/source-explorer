// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Copy / paste of filters between sources. The clipboard is kept in a file so it survives restarts.
// (OBS's own filter clipboard lives inside its main window and cannot be reached through the public
// API, so the two are separate.) GUI thread only.
#include <obs.h>

#include <string>
#include <vector>

namespace se {

struct ClipFilter {
	std::string name;
	std::string kind; // filter type id
	bool enabled = true;
	std::string settingsJson; // only the user-set values
};

struct PasteResult {
	int pasted = 0;
	int skipped = 0; // wrong kind of source for the filter, or the filter type is not installed
	std::string lastName;
};

ClipFilter snapshotFilter(obs_source_t *filter);

std::vector<ClipFilter> readFilterClipboard();
void writeFilterClipboard(const std::vector<ClipFilter> &filters);
bool filterClipboardEmpty();

// Adds copies of `filters` to the end of `parent`'s chain, with unique names.
PasteResult pasteFilters(obs_source_t *parent, const std::vector<ClipFilter> &filters);

} // namespace se
