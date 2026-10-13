// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Qt- and libobs-free rules for the Filters tab. They mirror what OBS's own filters dialog does
// (frontend/dialogs/OBSBasicFilters.cpp) so both show the same lists. The flag values are
// static_assert-ed against libobs in the UI code.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace se {

constexpr uint32_t kSourceFlagVideo = 1u << 0;
constexpr uint32_t kSourceFlagAudio = 1u << 1;
constexpr uint32_t kSourceFlagAsync = 1u << 2;

enum class FilterSection { AudioVideo, Effect };

// OBS puts a filter in the "Audio/Video Filters" list when it is async, otherwise in "Effect Filters".
FilterSection sectionFor(uint32_t filterFlags);

// Whether a filter of this kind may be added to a source with these output flags (the rule behind
// OBS's own Add menu).
bool fitsSection(FilterSection section, uint32_t sourceFlags, uint32_t filterFlags);
bool fitsSource(uint32_t sourceFlags, uint32_t filterFlags);

// "Blur", then "Blur 2", "Blur 3"... until `exists` says the name is free.
std::string uniqueFilterName(const std::string &base, const std::function<bool(const std::string &)> &exists);

// The chain is one ordered list; each section shows only its own members. These map between the two.
// chain[i] is the section of the filter at chain index i.
std::vector<int> chainIndicesOf(const std::vector<FilterSection> &chain, FilterSection section);
// Final chain index for a filter that should end up at `row` of its section's list (the row it will have
// after the move, i.e. what dropTargetIndex returns). Clamped to the section; -1 if the section is empty.
int chainIndexForRow(const std::vector<FilterSection> &chain, FilterSection section, int row);

// Case-insensitive substring match against the filter's name or its type's display name.
bool matchesFilterSearch(const std::string &name, const std::string &kindDisplay, const std::string &query);

} // namespace se
