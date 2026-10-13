// Tests for filter-management requests, raw JSON embedding and drop-index math.
#include "core/list-reorder.hpp"
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
	EQ(reqGetSourceFilterList("Cam").data.dump(), R"({"sourceName":"Cam"})");
	EQ(reqSetSourceFilterEnabled("Cam", "Blur", false).data.dump(),
	   R"({"sourceName":"Cam","filterName":"Blur","filterEnabled":false})");
	EQ(reqSetSourceFilterIndex("Cam", "Blur", 2).data.dump(),
	   R"({"sourceName":"Cam","filterName":"Blur","filterIndex":2})");
	EQ(reqSetSourceFilterIndex("Cam", "Blur", -4).data.dump(),
	   R"({"sourceName":"Cam","filterName":"Blur","filterIndex":0})");
	EQ(reqRemoveSourceFilter("Cam", "Blur").type, "RemoveSourceFilter");
	EQ(wsFrom(reqGetSourceFilter("Cam", "Blur"), "x"),
	   R"({"op":6,"d":{"requestType":"GetSourceFilter","requestId":"x","requestData":)"
	   R"({"sourceName":"Cam","filterName":"Blur"}}})");

	// Raw settings JSON (as produced by obs_data_get_json) is embedded verbatim.
	EQ(reqSetSourceFilterSettings("Cam", "CC", Json::raw(R"({"gamma":0.5,"name":"a\"b"})")).data.dump(),
	   R"({"sourceName":"Cam","filterName":"CC","filterSettings":{"gamma":0.5,"name":"a\"b"},"overlay":true})");
	// An empty blob (filter with only defaults) becomes {} rather than invalid JSON.
	EQ(reqSetSourceFilterSettings("Cam", "CC", Json::raw("")).data.dump(),
	   R"({"sourceName":"Cam","filterName":"CC","filterSettings":{},"overlay":true})");
	// Raw also works through the Streamer.bot renderer (quotes doubled for C#).
	const std::string cs = sbFrom(reqSetSourceFilterSettings("C", "F", Json::raw(R"({"a":"b"})")));
	const std::string want = "\"\"filterSettings\"\":{\"\"a\"\":\"\"b\"\"}";
	++g_checks;
	if (cs.find(want) == std::string::npos) {
		++g_failed;
		std::printf("FAIL raw in C#: %s\n", cs.c_str());
	}

	// Drop math: Qt "insert before destRow" semantics, list of 5.
	EQ(std::to_string(dropTargetIndex(5, 1, 4)), "3"); // move down: lands before old row 4 => index 3
	EQ(std::to_string(dropTargetIndex(5, 3, 1)), "1"); // move up
	EQ(std::to_string(dropTargetIndex(5, 0, 5)), "4"); // append
	EQ(std::to_string(dropTargetIndex(5, 2, 2)), "2"); // drop on itself
	EQ(std::to_string(dropTargetIndex(5, 2, 3)), "2"); // just below itself: no-op
	EQ(std::to_string(dropTargetIndex(5, 4, 0)), "0"); // to top
	EQ(std::to_string(dropTargetIndex(0, 0, 0)), "-1");
	EQ(std::to_string(dropTargetIndex(3, 0, 99)), "2"); // clamped

	std::printf("%d checks, %d failed\n", g_checks, g_failed);
	return g_failed ? 1 : 0;
}
