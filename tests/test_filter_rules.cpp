// SPDX-License-Identifier: GPL-2.0-or-later
// Tests for core/filter-rules: sections, compatibility, naming, chain mapping and search.
#include "core/filter-rules.hpp"

#include <cstdio>
#include <set>
#include <string>

static int g_failed = 0, g_checks = 0;

static void eq(const std::string &a, const std::string &e, int line)
{
	++g_checks;
	if (a != e) {
		++g_failed;
		std::printf("FAIL line %d\n  expected: %s\n  actual:   %s\n", line, e.c_str(), a.c_str());
	}
}
#define EQ(a, e) eq((a), (e), __LINE__)
static std::string yn(bool b) { return b ? "yes" : "no"; }

using namespace se;
using S = FilterSection;

constexpr uint32_t V = kSourceFlagVideo, A = kSourceFlagAudio, AS = kSourceFlagAsync;

int main()
{
	// Typical OBS source / filter flag combinations.
	const uint32_t videoCapture = V | AS | A; // async video with audio (e.g. a capture card)
	const uint32_t imageSource = V;           // synchronous video, no audio
	const uint32_t audioInput = A;            // audio only (mic)
	const uint32_t scene = V | A;
	const uint32_t colorCorrection = V;       // effect filter
	const uint32_t chromaKeyAsyncFlavour = V | AS; // async video filter (e.g. sharpness variants)
	const uint32_t gain = A;                  // audio filter

	// Sections follow the async flag, as in OBS.
	EQ(yn(sectionFor(colorCorrection) == S::Effect), "yes");
	EQ(yn(sectionFor(chromaKeyAsyncFlavour) == S::AudioVideo), "yes");
	EQ(yn(sectionFor(gain | AS) == S::AudioVideo), "yes");

	// Effect filters fit any video source, not audio-only ones.
	EQ(yn(fitsSource(imageSource, colorCorrection)), "yes");
	EQ(yn(fitsSource(videoCapture, colorCorrection)), "yes");
	EQ(yn(fitsSource(audioInput, colorCorrection)), "no");

	// Audio filters (async + audio) fit sources with audio only.
	EQ(yn(fitsSource(audioInput, gain | AS)), "yes");
	EQ(yn(fitsSource(videoCapture, gain | AS)), "yes");
	EQ(yn(fitsSource(imageSource, gain | AS)), "no");

	// Async video filters need an async source.
	EQ(yn(fitsSource(videoCapture, chromaKeyAsyncFlavour)), "yes");
	EQ(yn(fitsSource(imageSource, chromaKeyAsyncFlavour)), "no");
	EQ(yn(fitsSource(scene, chromaKeyAsyncFlavour)), "no");

	// Asking for the wrong section never matches: an effect filter does not fit the async list.
	EQ(yn(fitsSection(S::AudioVideo, videoCapture, colorCorrection)), "no");
	EQ(yn(fitsSection(S::Effect, videoCapture, gain | AS)), "no");

	// Unique names.
	std::set<std::string> taken = {"Blur", "Blur 2"};
	auto exists = [&](const std::string &n) { return taken.count(n) > 0; };
	EQ(uniqueFilterName("Sharpen", exists), "Sharpen");
	EQ(uniqueFilterName("Blur", exists), "Blur 3");

	// Chain mapping: [Effect, A/V, Effect, Effect, A/V]
	const std::vector<S> chain = {S::Effect, S::AudioVideo, S::Effect, S::Effect, S::AudioVideo};
	auto join = [](const std::vector<int> &v) {
		std::string s;
		for (int i : v)
			s += std::to_string(i) + ",";
		return s;
	};
	EQ(join(chainIndicesOf(chain, S::Effect)), "0,2,3,");
	EQ(join(chainIndicesOf(chain, S::AudioVideo)), "1,4,");
	EQ(std::to_string(chainIndexForRow(chain, S::Effect, 0)), "0");
	EQ(std::to_string(chainIndexForRow(chain, S::Effect, 2)), "3");
	EQ(std::to_string(chainIndexForRow(chain, S::Effect, 99)), "3"); // clamped
	EQ(std::to_string(chainIndexForRow(chain, S::Effect, -4)), "0");
	EQ(std::to_string(chainIndexForRow(chain, S::AudioVideo, 1)), "4");
	EQ(std::to_string(chainIndexForRow({S::Effect}, S::AudioVideo, 0)), "-1"); // empty section

	// Search: blank matches all; name or type display name; case-insensitive.
	EQ(yn(matchesFilterSearch("My Blur", "Composite Blur", "")), "yes");
	EQ(yn(matchesFilterSearch("My Blur", "Composite Blur", "   ")), "yes");
	EQ(yn(matchesFilterSearch("My Blur", "Composite Blur", "blur")), "yes");
	EQ(yn(matchesFilterSearch("Look", "Color Correction", "  COLOR ")), "yes");
	EQ(yn(matchesFilterSearch("Look", "Color Correction", "gain")), "no");

	std::printf("%d checks, %d failed\n", g_checks, g_failed);
	return g_failed ? 1 : 0;
}
