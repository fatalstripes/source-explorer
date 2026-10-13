// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/filter-rules.hpp"

#include <algorithm>
#include <cctype>

namespace se {

FilterSection sectionFor(uint32_t filterFlags)
{
	return (filterFlags & kSourceFlagAsync) ? FilterSection::AudioVideo : FilterSection::Effect;
}

// Same truth table as filter_compatible() in OBS's OBSBasicFilters.cpp.
bool fitsSection(FilterSection section, uint32_t sourceFlags, uint32_t filterFlags)
{
	const bool async = section == FilterSection::AudioVideo;
	const bool filterVideo = (filterFlags & kSourceFlagVideo) != 0;
	const bool filterAsync = (filterFlags & kSourceFlagAsync) != 0;
	const bool filterAudio = (filterFlags & kSourceFlagAudio) != 0;
	const bool audio = (sourceFlags & kSourceFlagAudio) != 0;
	const bool audioOnly = (sourceFlags & kSourceFlagVideo) == 0;
	const bool asyncSource = (sourceFlags & kSourceFlagAsync) != 0;

	if (async && ((audioOnly && filterVideo) || (!audio && !asyncSource) || (filterAudio && !audio) ||
		      (!asyncSource && !filterAudio)))
		return false;

	// OBS hides the Effect list for audio-only sources, so nothing can be added to it there.
	if (!async && audioOnly)
		return false;

	return (async && (filterAudio || filterAsync)) || (!async && !filterAudio && !filterAsync);
}

bool fitsSource(uint32_t sourceFlags, uint32_t filterFlags)
{
	return fitsSection(sectionFor(filterFlags), sourceFlags, filterFlags);
}

std::string uniqueFilterName(const std::string &base, const std::function<bool(const std::string &)> &exists)
{
	std::string name = base;
	for (int n = 2; exists(name); ++n)
		name = base + " " + std::to_string(n);
	return name;
}

std::vector<int> chainIndicesOf(const std::vector<FilterSection> &chain, FilterSection section)
{
	std::vector<int> out;
	for (size_t i = 0; i < chain.size(); ++i)
		if (chain[i] == section)
			out.push_back(int(i));
	return out;
}

int chainIndexForRow(const std::vector<FilterSection> &chain, FilterSection section, int row)
{
	const auto members = chainIndicesOf(chain, section);
	if (members.empty())
		return -1;
	return members[size_t(std::clamp(row, 0, int(members.size()) - 1))];
}

namespace {
std::string lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
	return s;
}
} // namespace

bool matchesFilterSearch(const std::string &name, const std::string &kindDisplay, const std::string &query)
{
	const auto b = query.find_first_not_of(" \t");
	if (b == std::string::npos)
		return true;
	const auto e = query.find_last_not_of(" \t");
	const std::string q = lower(query.substr(b, e - b + 1));
	return lower(name).find(q) != std::string::npos || lower(kindDisplay).find(q) != std::string::npos;
}

} // namespace se
