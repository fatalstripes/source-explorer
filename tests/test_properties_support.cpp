// Tests for the Properties panel's Qt-free support code.
#include "core/color-util.hpp"
#include "core/payload-generator.hpp"

#include <cstdio>
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

using namespace se;

int main()
{
	// OBS color layout is 0xAABBGGRR.
	EQ(std::to_string(toObsColor({255, 0, 0, 255})), std::to_string(0xFF0000FFLL));
	EQ(std::to_string(toObsColor({0, 255, 0, 255})), std::to_string(0xFF00FF00LL));
	EQ(std::to_string(toObsColor({0, 0, 255, 255})), std::to_string(0xFFFF0000LL));
	EQ(std::to_string(toObsColor({0x11, 0x22, 0x33, 0x44})), std::to_string(0x44332211LL));
	EQ(std::to_string(toObsColor({300, -5, 0, 255})), std::to_string(0xFF0000FFLL)); // clamped

	for (const Rgba c : {Rgba{1, 2, 3, 4}, Rgba{255, 255, 255, 255}, Rgba{0, 0, 0, 0}, Rgba{128, 64, 32, 255}}) {
		++g_checks;
		if (!(fromObsColor(toObsColor(c)) == c)) {
			++g_failed;
			std::printf("FAIL roundtrip %d,%d,%d,%d\n", c.r, c.g, c.b, c.a);
		}
	}
	// A value read back as a negative signed 32-bit int decodes the same.
	++g_checks;
	if (!(fromObsColor(static_cast<std::int32_t>(0xFF0000FFu)) == Rgba{255, 0, 0, 255})) {
		++g_failed;
		std::puts("FAIL negative int32 decode");
	}

	// Input settings requests.
	Json s = Json::object();
	s.set("url", "https://example.com/a?b=1&c=\"2\"");
	s.set("width", 1280);
	EQ(reqSetInputSettings("Browser", s).data.dump(),
	   R"({"inputName":"Browser","inputSettings":{"url":"https://example.com/a?b=1&c=\"2\"","width":1280},)"
	   R"("overlay":true})");
	EQ(wsFrom(reqGetInputSettings("Browser"), "q"),
	   R"({"op":6,"d":{"requestType":"GetInputSettings","requestId":"q","requestData":{"inputName":"Browser"}}})");
	EQ(reqSetInputSettings("B", Json::object(), false).data.dump(),
	   R"({"inputName":"B","inputSettings":{},"overlay":false})");

	// Large 64-bit ints (colors) must not be mangled into doubles.
	Json c = Json::object();
	c.set("color", static_cast<std::int64_t>(toObsColor({255, 255, 255, 255})));
	EQ(c.dump(), R"({"color":4294967295})");

	std::printf("%d checks, %d failed\n", g_checks, g_failed);
	return g_failed ? 1 : 0;
}
