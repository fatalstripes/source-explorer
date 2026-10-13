// SPDX-License-Identifier: GPL-2.0-or-later
// Dependency-free tests for core/payload-generator. Exit code 0 == pass.
#include "core/payload-generator.hpp"

#include <cstdio>
#include <limits>
#include <string>

static int g_failed = 0;
static int g_checks = 0;

#define CHECK_EQ(actual, expected)                                                                   \
	do {                                                                                          \
		++g_checks;                                                                           \
		const std::string a_ = (actual), e_ = (expected);                                     \
		if (a_ != e_) {                                                                       \
			++g_failed;                                                                   \
			std::printf("FAIL %s:%d\n  expected: %s\n  actual:   %s\n", __FILE__, __LINE__, \
				    e_.c_str(), a_.c_str());                                          \
		}                                                                                     \
	} while (0)

#define CHECK(cond)                                                                                  \
	do {                                                                                          \
		++g_checks;                                                                           \
		if (!(cond)) {                                                                        \
			++g_failed;                                                                   \
			std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                  \
		}                                                                                     \
	} while (0)

using namespace se;

static const SceneItemTarget kItem{"Main Scene", 7};

static void test_transform_position()
{
	TransformPatch p;
	p.positionX = 960.0;
	p.positionY = 540.5;
	CHECK_EQ(wsSetSceneItemTransform(kItem, p, "r1"),
		 R"({"op":6,"d":{"requestType":"SetSceneItemTransform","requestId":"r1","requestData":)"
		 R"({"sceneName":"Main Scene","sceneItemId":7,"sceneItemTransform":)"
		 R"({"positionX":960.0,"positionY":540.5}}}})");
}

static void test_transform_scale_and_bounds()
{
	TransformPatch p;
	p.scaleX = 0.5;
	p.scaleY = 0.5;
	p.boundsType = BoundsType::ScaleInner;
	p.boundsAlignment = align::Center;
	p.boundsWidth = 1920.0;
	p.boundsHeight = 1080.0;
	CHECK_EQ(p.toJson().dump(),
		 R"({"scaleX":0.5,"scaleY":0.5,"boundsType":"OBS_BOUNDS_SCALE_INNER",)"
		 R"("boundsAlignment":0,"boundsWidth":1920.0,"boundsHeight":1080.0})");
}

static void test_transform_partial_omits_unset()
{
	TransformPatch p;
	p.rotation = 90.0;
	CHECK_EQ(p.toJson().dump(), R"({"rotation":90.0})");
	CHECK(p.toJson().dump().find("scale") == std::string::npos);
	CHECK_EQ(TransformPatch{}.toJson().dump(), "{}");
}

static void test_transform_get()
{
	CHECK_EQ(wsGetSceneItemTransform(kItem, "g1"),
		 R"({"op":6,"d":{"requestType":"GetSceneItemTransform","requestId":"g1","requestData":)"
		 R"({"sceneName":"Main Scene","sceneItemId":7}}})");
}

static void test_transform_defaults_vertical_canvas()
{
	// Canvas-aware: bounds default follows the canvas, e.g. 1080x1920 vertical.
	auto d = defaultTransform(1080.0, 1920.0);
	CHECK_EQ(d.toJson().dump(),
		 R"({"positionX":0.0,"positionY":0.0,"rotation":0.0,"scaleX":1.0,"scaleY":1.0,"alignment":5,)"
		 R"("boundsType":"OBS_BOUNDS_NONE","boundsAlignment":0,"boundsWidth":1080.0,"boundsHeight":1920.0,)"
		 R"("cropLeft":0,"cropRight":0,"cropTop":0,"cropBottom":0,"cropToBounds":false})");
}

static void test_filter_color_correction()
{
	ColorCorrectionSettings s;
	s.brightness = 0.25;
	s.saturation = 1.5;
	s.hueShift = -30.0;
	CHECK_EQ(wsSetSourceFilterSettings("Webcam", "Color Correction", s.toJson(), true, "f1"),
		 R"({"op":6,"d":{"requestType":"SetSourceFilterSettings","requestId":"f1","requestData":)"
		 R"({"sourceName":"Webcam","filterName":"Color Correction","filterSettings":)"
		 R"({"brightness":0.25,"saturation":1.5,"hue_shift":-30.0},"overlay":true}}})");
}

static void test_filter_color_correction_defaults()
{
	CHECK_EQ(colorCorrectionDefaults().toJson().dump(),
		 R"({"gamma":0.0,"contrast":0.0,"brightness":0.0,"saturation":0.0,"hue_shift":0.0,)"
		 R"("opacity":1.0,"color_multiply":16777215,"color_add":0})");
}

static void test_filter_blur()
{
	BlurSettings b;
	b.size = 12.0;
	CHECK_EQ(b.toJson().dump(), R"({"radius":12.0})");
	b.flavor = BlurFlavor::StreamFX;
	b.size = 8.5;
	CHECK_EQ(b.toJson().dump(), R"({"Filter.Blur.Size":8.5})");
	CHECK_EQ(wsGetSourceFilter("Cam", "Blur", "g2"),
		 R"({"op":6,"d":{"requestType":"GetSourceFilter","requestId":"g2","requestData":)"
		 R"({"sourceName":"Cam","filterName":"Blur"}}})");
}

static void test_overlay_false_is_explicit()
{
	CHECK(wsSetSourceFilterSettings("A", "B", Json::object(), false).find(R"("overlay":false)") !=
	      std::string::npos);
}

static void test_escaping()
{
	SceneItemTarget t{"Scene \"Q\"\\1\n", 1};
	TransformPatch p;
	p.rotation = 1.0;
	std::string out = wsSetSceneItemTransform(t, p);
	const std::string expected = R"("sceneName":"Scene \"Q\"\\1\n")";
	CHECK(out.find(expected) != std::string::npos);
	CHECK_EQ(jsonEscape(std::string("\x01")), "\\u0001");
}

static void test_number_formatting_is_locale_independent()
{
	CHECK_EQ(formatDouble(0.1), "0.1");
	CHECK_EQ(formatDouble(-0.0), "0.0");
	CHECK_EQ(formatDouble(100.0), "100.0");
	CHECK_EQ(formatDouble(std::numeric_limits<double>::infinity()), "0.0"); // inf never leaks into JSON
	CHECK(formatDouble(1234.5).find(',') == std::string::npos);
}

static void test_streamerbot_transform()
{
	TransformPatch p;
	p.positionX = 100.0;
	p.positionY = 50.0;
	const std::string cs = sbSetSceneItemTransform(kItem, p);
	CHECK_EQ(cs,
		 "if (!CPH.ObsIsConnected(0))\n"
		 "{\n"
		 "    CPH.LogWarn(\"[SourceExplorer] OBS not connected\");\n"
		 "    return false;\n"
		 "}\n"
		 "CPH.ObsSendRaw(\"SetSceneItemTransform\", "
		 "@\"{\"\"sceneName\"\":\"\"Main Scene\"\",\"\"sceneItemId\"\":7,\"\"sceneItemTransform\"\":"
		 "{\"\"positionX\"\":100.0,\"\"positionY\"\":50.0}}\", 0);\n");
}

static void test_streamerbot_filter_and_getters()
{
	ColorCorrectionSettings s;
	s.opacity = 0.5;
	const std::string cs = sbSetSourceFilterSettings("Cam", "CC", s.toJson(), true, 1);
	CHECK(cs.find("CPH.ObsIsConnected(1)") != std::string::npos);
	CHECK(cs.find("CPH.ObsSendRaw(\"SetSourceFilterSettings\", @\"{\"\"sourceName\"\":\"\"Cam\"\"") !=
	      std::string::npos);
	CHECK(cs.find("\"\"overlay\"\":true}\", 1);") != std::string::npos);

	const std::string get = sbGetSceneItemTransform(kItem);
	CHECK(get.find("string response = CPH.ObsSendRaw(\"GetSceneItemTransform\"") != std::string::npos);
	CHECK(get.find("CPH.LogInfo(") != std::string::npos);
}

static void test_screenshot_requests()
{
	CHECK_EQ(reqGetSourceScreenshot("Cam").data.dump(), R"({"sourceName":"Cam","imageFormat":"png"})");
	CHECK_EQ(reqSaveSourceScreenshot("Cam", "jpg", R"(C:\shots\a "b".jpg)").data.dump(),
		 R"({"sourceName":"Cam","imageFormat":"jpg","imageFilePath":"C:\\shots\\a \"b\".jpg"})");
	CHECK_EQ(reqGetSourceScreenshot("Cam").type, "GetSourceScreenshot");
}

static void test_structured_transform_requests()
{
	// The structured builders must produce byte-identical output to the original string builders,
	// so every menu in the plugin copies the same thing.
	TransformPatch p;
	p.positionX = 10.0;
	p.positionY = 20.5;
	p.rotation = 90.0;
	CHECK_EQ(wsFrom(reqSetSceneItemTransform(kItem, p), "se-set-transform"), wsSetSceneItemTransform(kItem, p));
	CHECK_EQ(wsFrom(reqGetSceneItemTransform(kItem), "se-get-transform"), wsGetSceneItemTransform(kItem));
	CHECK_EQ(sbFrom(reqSetSceneItemTransform(kItem, p)), sbSetSceneItemTransform(kItem, p));
	CHECK_EQ(sbFrom(reqGetSceneItemTransform(kItem)), sbGetSceneItemTransform(kItem));

	// A full transform carries every field.
	TransformPatch full = defaultTransform(1920.0, 1080.0);
	const std::string json = reqSetSceneItemTransform(kItem, full).data.dump();
	for (const char *key : {"positionX", "positionY", "rotation", "scaleX", "scaleY", "alignment", "boundsType",
				"boundsAlignment", "boundsWidth", "boundsHeight", "cropLeft", "cropRight", "cropTop",
				"cropBottom"})
		CHECK(json.find(std::string("\"") + key + "\"") != std::string::npos);
}

static void test_source_tab_requests()
{
	CHECK_EQ(reqSetInputName("Cam", "New \"Cam\"").data.dump(),
		 R"({"inputName":"Cam","newInputName":"New \"Cam\""})");
	CHECK_EQ(reqSetSceneName("Gameplay", "Gameplay 2").data.dump(),
		 R"({"sceneName":"Gameplay","newSceneName":"Gameplay 2"})");
	CHECK_EQ(reqSetSceneItemIndex(kItem, 3).data.dump(),
		 R"({"sceneName":"Main Scene","sceneItemId":7,"sceneItemIndex":3})");
	CHECK_EQ(reqSetSceneItemIndex(kItem, -9).data.dump(),
		 R"({"sceneName":"Main Scene","sceneItemId":7,"sceneItemIndex":0})");
	CHECK_EQ(reqGetSceneItemIndex(kItem).type, "GetSceneItemIndex");
	CHECK_EQ(reqSetSceneItemBlendMode(kItem, BlendMode::Multiply).data.dump(),
		 R"({"sceneName":"Main Scene","sceneItemId":7,"sceneItemBlendMode":"OBS_BLEND_MULTIPLY"})");
	CHECK_EQ(reqGetSceneItemBlendMode(kItem).type, "GetSceneItemBlendMode");
	const BlendMode all[] = {BlendMode::Normal, BlendMode::Additive, BlendMode::Subtract, BlendMode::Screen,
				 BlendMode::Multiply, BlendMode::Lighten, BlendMode::Darken};
	const char *names[] = {"OBS_BLEND_NORMAL", "OBS_BLEND_ADDITIVE", "OBS_BLEND_SUBTRACT", "OBS_BLEND_SCREEN",
			       "OBS_BLEND_MULTIPLY", "OBS_BLEND_LIGHTEN", "OBS_BLEND_DARKEN"};
	for (int i = 0; i < 7; ++i)
		CHECK_EQ(blendModeName(all[i]), names[i]);
}

static void test_audio_extras()
{
	// Volume as a multiplier (the "%" view: 100% == 1.0), clamped to obs-websocket's 0..20.
	CHECK_EQ(reqSetInputVolumeMul("Mic", 0.5).data.dump(), R"({"inputName":"Mic","inputVolumeMul":0.5})");
	CHECK_EQ(reqSetInputVolumeMul("Mic", 99).data.dump(), R"({"inputName":"Mic","inputVolumeMul":20.0})");
	CHECK_EQ(reqSetInputVolumeMul("Mic", -1).data.dump(), R"({"inputName":"Mic","inputVolumeMul":0.0})");
	CHECK_EQ(reqSetInputVolumeMul("Mic", 1.0).type, "SetInputVolume");

	// Balance: 0 = left, 0.5 = centre, 1 = right.
	CHECK_EQ(reqSetInputAudioBalance("Mic", 0.25).data.dump(), R"({"inputName":"Mic","inputAudioBalance":0.25})");
	CHECK_EQ(reqSetInputAudioBalance("Mic", 3).data.dump(), R"({"inputName":"Mic","inputAudioBalance":1.0})");
	CHECK_EQ(reqGetInputAudioBalance("Mic").type, "GetInputAudioBalance");

	// Mixer tracks 1-6, keyed "1".."6".
	CHECK_EQ(reqSetInputAudioTracks("Mic", {true, false, true, false, false, true}).data.dump(),
		 R"({"inputName":"Mic","inputAudioTracks":{"1":true,"2":false,"3":true,"4":false,"5":false,"6":true}})");
	CHECK_EQ(reqGetInputAudioTracks("Mic").type, "GetInputAudioTracks");

	// Add-filter carries the kind and settings verbatim (e.g. copied from another source).
	CHECK_EQ(reqCreateSourceFilter("Cam", "CC", "color_filter_v2", Json::raw(R"({"gamma":0.5})")).data.dump(),
		 R"({"sourceName":"Cam","filterName":"CC","filterKind":"color_filter_v2","filterSettings":{"gamma":0.5}})");
}

static void test_audio_requests()
{
	CHECK_EQ(wsFrom(reqSetInputVolumeDb("Mic", -6.5), "a1"),
		 R"({"op":6,"d":{"requestType":"SetInputVolume","requestId":"a1","requestData":)"
		 R"({"inputName":"Mic","inputVolumeDb":-6.5}}})");
	CHECK_EQ(wsFrom(reqSetInputMute("Mic", true), "a2"),
		 R"({"op":6,"d":{"requestType":"SetInputMute","requestId":"a2","requestData":)"
		 R"({"inputName":"Mic","inputMuted":true}}})");
	CHECK_EQ(reqSetInputAudioSyncOffset("Mic", 150).data.dump(),
		 R"({"inputName":"Mic","inputAudioSyncOffset":150})");
	CHECK_EQ(reqSetInputAudioMonitorType("Mic", MonitorType::MonitorAndOutput).data.dump(),
		 R"({"inputName":"Mic","monitorType":"OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT"})");
	CHECK_EQ(reqGetInputAudioMonitorType("Mic").type, "GetInputAudioMonitorType");
	CHECK_EQ(wsFrom(reqGetInputVolume("Mic"), "a3"),
		 R"({"op":6,"d":{"requestType":"GetInputVolume","requestId":"a3","requestData":{"inputName":"Mic"}}})");
}

static void test_audio_clamping()
{
	CHECK_EQ(reqSetInputVolumeDb("M", -500.0).data.dump(), R"({"inputName":"M","inputVolumeDb":-100.0})");
	CHECK_EQ(reqSetInputVolumeDb("M", 99.0).data.dump(), R"({"inputName":"M","inputVolumeDb":26.0})");
	CHECK_EQ(reqSetInputVolumeDb("M", std::numeric_limits<double>::quiet_NaN()).data.dump(),
		 R"({"inputName":"M","inputVolumeDb":0.0})");
	CHECK_EQ(reqSetInputAudioSyncOffset("M", -5000).data.dump(),
		 R"({"inputName":"M","inputAudioSyncOffset":-950})");
	CHECK_EQ(reqSetInputAudioSyncOffset("M", 99999).data.dump(),
		 R"({"inputName":"M","inputAudioSyncOffset":20000})");
}

static void test_audio_streamerbot()
{
	const std::string set = sbFrom(reqSetInputMute("Mic", false));
	CHECK(set.find("string response") == std::string::npos); // setters don't capture
	const std::string expected = "CPH.ObsSendRaw(\"SetInputMute\", @\"{\"\"inputName\"\":\"\"Mic\"\","
				     "\"\"inputMuted\"\":false}\", 0);";
	CHECK(set.find(expected) != std::string::npos);
	const std::string get = sbFrom(reqGetInputVolume("Mic"), 2);
	CHECK(get.find("CPH.ObsIsConnected(2)") != std::string::npos);
	CHECK(get.find("string response = CPH.ObsSendRaw(\"GetInputVolume\"") != std::string::npos);
	CHECK(get.find("CPH.LogInfo(") != std::string::npos);
}

int main()
{
	test_screenshot_requests();
	test_structured_transform_requests();
	test_source_tab_requests();
	test_audio_extras();
	test_audio_requests();
	test_audio_clamping();
	test_audio_streamerbot();
	test_transform_position();
	test_transform_scale_and_bounds();
	test_transform_partial_omits_unset();
	test_transform_get();
	test_transform_defaults_vertical_canvas();
	test_filter_color_correction();
	test_filter_color_correction_defaults();
	test_filter_blur();
	test_overlay_false_is_explicit();
	test_escaping();
	test_number_formatting_is_locale_independent();
	test_streamerbot_transform();
	test_streamerbot_filter_and_getters();

	std::printf("%d checks, %d failed\n", g_checks, g_failed);
	return g_failed ? 1 : 0;
}
