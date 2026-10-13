// Tests for core/quick-access and the media / property-button requests behind the quick-access bar.
#include "core/quick-access.hpp"

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
using QB = QuickButton;

static std::string names(const std::vector<QB> &v)
{
	std::string s;
	for (QB b : v) {
		switch (b) {
		case QB::Visibility: s += "Vis,"; break;
		case QB::Lock: s += "Lock,"; break;
		case QB::Screenshot: s += "Shot,"; break;
		case QB::ResetUi: s += "Reset,"; break;
		case QB::Pin: s += "Pin,"; break;
		case QB::Mute: s += "Mute,"; break;
		case QB::Monitoring: s += "Mon,"; break;
		case QB::RefreshCache: s += "Refresh,"; break;
		case QB::ActivateToggle: s += "Activate,"; break;
		case QB::Transform: s += "Transform,"; break;
		case QB::MediaPlayPause: s += "PlayPause,"; break;
		case QB::MediaStop: s += "Stop,"; break;
		case QB::MediaRestart: s += "Restart,"; break;
		}
	}
	return s;
}

int main()
{
	// Nothing but the always-available buttons for a bare source with no scene item.
	EQ(names(quickButtonsFor({})), "Shot,Reset,Pin,");

	// A plain scene item (e.g. an image): visibility, lock and the transform popup.
	SourceTraits image;
	image.hasItem = true;
	EQ(names(quickButtonsFor(image)), "Vis,Lock,Shot,Reset,Pin,Transform,");

	// A browser source with audio, as an item: the doc's browser + audio additions.
	SourceTraits browser;
	browser.hasItem = true;
	browser.hasAudio = true;
	browser.isBrowser = true;
	EQ(names(quickButtonsFor(browser)), "Vis,Lock,Shot,Reset,Pin,Mute,Mon,Refresh,Transform,");

	// A video capture device.
	SourceTraits cam;
	cam.hasItem = true;
	cam.hasActivateButton = true;
	EQ(names(quickButtonsFor(cam)), "Vis,Lock,Shot,Reset,Pin,Activate,Transform,");

	// A media source with audio.
	SourceTraits media;
	media.hasItem = true;
	media.hasAudio = true;
	media.isMedia = true;
	EQ(names(quickButtonsFor(media)), "Vis,Lock,Shot,Reset,Pin,Mute,Mon,Transform,PlayPause,Stop,Restart,");

	// A global audio device is shown without a scene item: no visibility/lock/transform.
	SourceTraits mic;
	mic.hasAudio = true;
	EQ(names(quickButtonsFor(mic)), "Shot,Reset,Pin,Mute,Mon,");

	// Monitoring cycles through all three states and returns to the start.
	EQ(monitorTypeName(nextMonitorType(MonitorType::None)), "OBS_MONITORING_TYPE_MONITOR_ONLY");
	EQ(monitorTypeName(nextMonitorType(MonitorType::MonitorOnly)), "OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT");
	EQ(monitorTypeName(nextMonitorType(MonitorType::MonitorAndOutput)), "OBS_MONITORING_TYPE_NONE");
	MonitorType t = MonitorType::None;
	for (int i = 0; i < 3; ++i)
		t = nextMonitorType(t);
	EQ(monitorTypeName(t), "OBS_MONITORING_TYPE_NONE");

	// Warning when the loaded item is in a scene other than the one being edited.
	EQ(contextMismatch("Gameplay", "Gameplay") ? "warn" : "ok", "ok");
	EQ(contextMismatch("Gameplay", "Starting Soon") ? "warn" : "ok", "warn");
	EQ(contextMismatch("", "Starting Soon") ? "warn" : "ok", "ok"); // nothing being edited: cannot tell
	EQ(contextMismatch("Gameplay", "") ? "warn" : "ok", "ok");      // no item context (device, scene)
	EQ(contextMismatch("gameplay", "Gameplay") ? "warn" : "ok", "warn"); // names are case-sensitive in OBS

	// Property buttons and media actions (obs-websocket v5 names, verified against the installed plugin).
	EQ(reqPressInputPropertiesButton("Cam", "activate").data.dump(), R"({"inputName":"Cam","propertyName":"activate"})");
	EQ(reqPressInputPropertiesButton("Browser", "refreshnocache").type, "PressInputPropertiesButton");
	EQ(reqTriggerMediaInputAction("Clip", MediaAction::Play).data.dump(),
	   R"({"inputName":"Clip","mediaAction":"OBS_WEBSOCKET_MEDIA_INPUT_ACTION_PLAY"})");
	const MediaAction acts[] = {MediaAction::Play, MediaAction::Pause, MediaAction::Stop,
				    MediaAction::Restart, MediaAction::Next, MediaAction::Previous};
	const char *want[] = {"PLAY", "PAUSE", "STOP", "RESTART", "NEXT", "PREVIOUS"};
	for (int i = 0; i < 6; ++i)
		EQ(mediaActionName(acts[i]), std::string("OBS_WEBSOCKET_MEDIA_INPUT_ACTION_") + want[i]);
	EQ(reqGetMediaInputStatus("Clip").type, "GetMediaInputStatus");
	EQ(wsFrom(reqTriggerMediaInputAction("Clip", MediaAction::Pause), "m"),
	   R"({"op":6,"d":{"requestType":"TriggerMediaInputAction","requestId":"m","requestData":)"
	   R"({"inputName":"Clip","mediaAction":"OBS_WEBSOCKET_MEDIA_INPUT_ACTION_PAUSE"}}})");

	std::printf("%d checks, %d failed\n", g_checks, g_failed);
	return g_failed ? 1 : 0;
}
