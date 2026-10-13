// Tests for core/frame-fit: keeping a floating dock usable and on screen.
#include "core/frame-fit.hpp"

#include <cstdio>
#include <string>

static int g_failed = 0, g_checks = 0;

static std::string str(const se::Rect &r)
{
	return std::to_string(r.x) + "," + std::to_string(r.y) + " " + std::to_string(r.w) + "x" + std::to_string(r.h);
}

static void check(const se::Rect &actual, const se::Rect &expected, int line)
{
	++g_checks;
	if (!(actual == expected)) {
		++g_failed;
		std::printf("FAIL line %d\n  expected: %s\n  actual:   %s\n", line, str(expected).c_str(),
			    str(actual).c_str());
	}
}
#define CHECK(a, e) check((a), (e), __LINE__)

using namespace se;

int main()
{
	const Rect screen{0, 0, 2560, 1392}; // available area of a 1440p monitor minus taskbar
	const Size pref{420, 640};

	// The exact case seen in OBS: frame at (-8,-31), 1146x1062, title bar above the screen.
	// Large but not screen-covering, so the size is kept and it is pulled fully on-screen.
	CHECK(fitFloatingFrame({-8, -31, 1146, 1062}, screen, pref), (Rect{0, 0, 1146, 1062}));

	// A window covering nearly the whole screen is replaced by the preferred size, centred.
	CHECK(fitFloatingFrame({0, 0, 2500, 1380}, screen, pref), (Rect{1070, 376, 420, 640}));

	// The dock's own default size is never "too big", even on a short screen, so a position the
	// user chose is kept (regression: a 60% rule re-centred it on every show).
	CHECK(fitFloatingFrame({300, 100, 420, 640}, Rect{0, 0, 1920, 1040}, pref), (Rect{300, 100, 420, 640}));

	// A usable frame is left alone.
	CHECK(fitFloatingFrame({100, 100, 420, 640}, screen, pref), (Rect{100, 100, 420, 640}));

	// Right size but title bar above the screen: only the position moves.
	CHECK(fitFloatingFrame({100, -20, 420, 640}, screen, pref), (Rect{100, 0, 420, 640}));

	// Dragged mostly off the right / bottom edge: pulled back so it is fully visible.
	CHECK(fitFloatingFrame({3000, 100, 420, 640}, screen, pref), (Rect{2140, 100, 420, 640}));
	CHECK(fitFloatingFrame({100, 1300, 420, 640}, screen, pref), (Rect{100, 752, 420, 640}));

	// Off to the left of the screen.
	CHECK(fitFloatingFrame({-900, 100, 420, 640}, screen, pref), (Rect{0, 100, 420, 640}));

	// Second monitor placed to the right: the available area has a non-zero origin.
	const Rect second{2560, 0, 1920, 1040};
	CHECK(fitFloatingFrame({100, 100, 420, 640}, second, pref), (Rect{2560, 100, 420, 640}));
	CHECK(fitFloatingFrame({4400, 900, 420, 640}, second, pref), (Rect{4060, 400, 420, 640}));

	// Tiny or degenerate frames get the preferred size, centred.
	CHECK(fitFloatingFrame({10, 10, 50, 50}, screen, pref), (Rect{1070, 376, 420, 640}));
	CHECK(fitFloatingFrame({0, 0, 0, 0}, screen, pref), (Rect{1070, 376, 420, 640}));

	// Small screen: preferred size is limited to 90% of it.
	CHECK(fitFloatingFrame({0, 0, 3000, 3000}, Rect{0, 0, 800, 600}, pref), (Rect{190, 30, 420, 540}));

	// A frame a little under the 60% limit that overhangs the edge is clamped, not resized.
	CHECK(fitFloatingFrame({2000, 600, 1500, 800}, screen, pref), (Rect{1060, 592, 1500, 800}));

	std::printf("%d checks, %d failed\n", g_checks, g_failed);
	return g_failed ? 1 : 0;
}
