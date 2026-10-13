#pragma once
// Pure C++ (no libobs / Qt) generator for the "Command Copier" feature.
// Emits obs-websocket v5 request payloads and Streamer.bot C# snippets.
#include "core/json-value.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace se {

// obs-websocket v5 alignment bitmask (same as libobs OBS_ALIGN_*).
namespace align {
inline constexpr int Center = 0, Left = 1, Right = 2, Top = 4, Bottom = 8;
inline constexpr int TopLeft = Top | Left, TopRight = Top | Right;
inline constexpr int BottomLeft = Bottom | Left, BottomRight = Bottom | Right;
} // namespace align

enum class BoundsType {
	None,
	Stretch,
	ScaleInner,
	ScaleOuter,
	ScaleToWidth,
	ScaleToHeight,
	MaxOnly
};
const char *boundsTypeName(BoundsType t); // "OBS_BOUNDS_STRETCH", ...

struct SceneItemTarget {
	std::string sceneName;
	std::int64_t sceneItemId = 0;
};

// Partial transform: only engaged fields are emitted, so right-clicking a single
// field copies a minimal payload. Read-only keys (sourceWidth/Height, width,
// height) are intentionally absent: obs-websocket ignores/rejects them on Set.
struct TransformPatch {
	std::optional<double> positionX, positionY;
	std::optional<double> scaleX, scaleY;
	std::optional<double> rotation;
	std::optional<int> alignment;
	std::optional<BoundsType> boundsType;
	std::optional<int> boundsAlignment;
	std::optional<double> boundsWidth, boundsHeight;
	std::optional<int> cropLeft, cropRight, cropTop, cropBottom;
	std::optional<bool> cropToBounds; // obs-websocket >= 5.4

	Json toJson() const; // the "sceneItemTransform" object
};

// Factory defaults for a freshly added scene item (full patch).
TransformPatch defaultTransform(double canvasW, double canvasH);

// ---------------------------------------------------------------------------
// obs-websocket v5. Each returns a complete op 6 "Request" message, compact.
// requestId is a caller-supplied correlation id.
// ---------------------------------------------------------------------------
std::string wsRequest(const std::string &requestType, Json requestData, const std::string &requestId);

std::string wsSetSceneItemTransform(const SceneItemTarget &t, const TransformPatch &p,
				    const std::string &requestId = "se-set-transform");
std::string wsGetSceneItemTransform(const SceneItemTarget &t, const std::string &requestId = "se-get-transform");

std::string wsSetSourceFilterSettings(const std::string &sourceName, const std::string &filterName,
				      const Json &filterSettings, bool overlay = true,
				      const std::string &requestId = "se-set-filter");
std::string wsGetSourceFilter(const std::string &sourceName, const std::string &filterName,
			      const std::string &requestId = "se-get-filter");

// ---------------------------------------------------------------------------
// Streamer.bot C# (CPHInline). Uses CPH.ObsSendRaw so the call maps 1:1 to the
// obs-websocket v5 request and is stable across Streamer.bot releases.
// Output is a ready-to-paste statement block (guards OBS connection first).
// ---------------------------------------------------------------------------
std::string sbSetSceneItemTransform(const SceneItemTarget &t, const TransformPatch &p, int obsConnection = 0);
std::string sbGetSceneItemTransform(const SceneItemTarget &t, int obsConnection = 0);
std::string sbSetSourceFilterSettings(const std::string &sourceName, const std::string &filterName,
				      const Json &filterSettings, bool overlay = true, int obsConnection = 0);
std::string sbGetSourceFilter(const std::string &sourceName, const std::string &filterName,
			      int obsConnection = 0);

// ---------------------------------------------------------------------------
// Generic request + renderers (used by the audio requests below).
// ---------------------------------------------------------------------------
struct ObsRequest {
	std::string type; // obs-websocket v5 requestType
	Json data;        // requestData
	bool isGetter() const { return type.rfind("Get", 0) == 0; }
};
std::string wsFrom(const ObsRequest &r, const std::string &requestId = "se-req");
// Getters capture and log the response string (CPH.ObsSendRaw returns it).
std::string sbFrom(const ObsRequest &r, int obsConnection = 0);

// ---------------------------------------------------------------------------
// Audio inputs. Ranges follow obs-websocket v5 and are clamped by the builders.
// ---------------------------------------------------------------------------
inline constexpr double kMinVolumeDb = -100.0, kMaxVolumeDb = 26.0;
inline constexpr int kMinSyncOffsetMs = -950, kMaxSyncOffsetMs = 20000;

enum class MonitorType { None, MonitorOnly, MonitorAndOutput }; // same order as libobs
const char *monitorTypeName(MonitorType t); // "OBS_MONITORING_TYPE_NONE", ...

double clampVolumeDb(double db);
int clampSyncOffsetMs(int ms);

ObsRequest reqSetInputVolumeDb(const std::string &input, double db);
ObsRequest reqGetInputVolume(const std::string &input);
ObsRequest reqSetInputMute(const std::string &input, bool muted);
ObsRequest reqGetInputMute(const std::string &input);
ObsRequest reqSetInputAudioSyncOffset(const std::string &input, int ms);
ObsRequest reqGetInputAudioSyncOffset(const std::string &input);
ObsRequest reqSetInputAudioMonitorType(const std::string &input, MonitorType t);
ObsRequest reqGetInputAudioMonitorType(const std::string &input);

// Volume as a linear multiplier (what the % view shows: 100% == 1.0). obs-websocket allows 0..20.
inline constexpr double kMaxVolumeMul = 20.0;
double clampVolumeMul(double mul);
ObsRequest reqSetInputVolumeMul(const std::string &input, double mul);

// Balance: 0.0 = full left, 0.5 = centre, 1.0 = full right.
double clampBalance(double balance);
ObsRequest reqSetInputAudioBalance(const std::string &input, double balance);
ObsRequest reqGetInputAudioBalance(const std::string &input);

// Mixer tracks 1..6, in order.
using AudioTracks = std::array<bool, 6>;
ObsRequest reqSetInputAudioTracks(const std::string &input, const AudioTracks &tracks);
ObsRequest reqGetInputAudioTracks(const std::string &input);

// Structured forms of the transform requests above (same payloads), for the menu code.
ObsRequest reqSetSceneItemTransform(const SceneItemTarget &t, const TransformPatch &p);
ObsRequest reqGetSceneItemTransform(const SceneItemTarget &t);

// ---------------------------------------------------------------------------
// Scene tree. For items inside a group, obs-websocket addresses the group by its
// name as the "sceneName".
// ---------------------------------------------------------------------------
ObsRequest reqSetSceneItemEnabled(const SceneItemTarget &t, bool enabled);
ObsRequest reqGetSceneItemEnabled(const SceneItemTarget &t);
ObsRequest reqSetSceneItemLocked(const SceneItemTarget &t, bool locked);
ObsRequest reqGetSceneItemLocked(const SceneItemTarget &t);
ObsRequest reqSetCurrentProgramScene(const std::string &sceneName);
ObsRequest reqGetCurrentProgramScene();

// ---------------------------------------------------------------------------
// Snapshots. Get returns the image as base64 in the response; Save writes it to
// a path on the machine running OBS.
// ---------------------------------------------------------------------------
ObsRequest reqGetSourceScreenshot(const std::string &source, const std::string &imageFormat = "png");
ObsRequest reqSaveSourceScreenshot(const std::string &source, const std::string &imageFormat,
				   const std::string &imageFilePath);

// ---------------------------------------------------------------------------
// Source tab: rename, stacking order, blend mode.
// obs-websocket has no request for scale filtering or deinterlacing, so none is offered.
// ---------------------------------------------------------------------------
enum class BlendMode { Normal, Additive, Subtract, Screen, Multiply, Lighten, Darken }; // libobs order
const char *blendModeName(BlendMode m); // "OBS_BLEND_NORMAL", ...

ObsRequest reqSetInputName(const std::string &input, const std::string &newName);
ObsRequest reqSetSceneName(const std::string &scene, const std::string &newName);
ObsRequest reqSetSceneItemIndex(const SceneItemTarget &t, int index); // 0 = bottom of the stack
ObsRequest reqGetSceneItemIndex(const SceneItemTarget &t);
ObsRequest reqSetSceneItemBlendMode(const SceneItemTarget &t, BlendMode mode);
ObsRequest reqGetSceneItemBlendMode(const SceneItemTarget &t);

// ---------------------------------------------------------------------------
// Quick-access bar: property buttons (browser refresh, capture activate) and media control.
// ---------------------------------------------------------------------------
enum class MediaAction { Play, Pause, Stop, Restart, Next, Previous };
const char *mediaActionName(MediaAction a); // "OBS_WEBSOCKET_MEDIA_INPUT_ACTION_PLAY", ...

// Presses a button property of an input, by the property's internal name ("refreshnocache" for a
// browser source's refresh, "activate" for a capture device's activate/deactivate).
ObsRequest reqPressInputPropertiesButton(const std::string &input, const std::string &propertyName);
ObsRequest reqTriggerMediaInputAction(const std::string &input, MediaAction action);
ObsRequest reqGetMediaInputStatus(const std::string &input);

// ---------------------------------------------------------------------------
// Input settings (the Properties panel). `overlay` merges into existing settings.
// ---------------------------------------------------------------------------
ObsRequest reqSetInputSettings(const std::string &input, Json settings, bool overlay = true);
ObsRequest reqGetInputSettings(const std::string &input);

// ---------------------------------------------------------------------------
// Filter management. `source` is any filterable source: input, scene or group.
// ---------------------------------------------------------------------------
ObsRequest reqGetSourceFilterList(const std::string &source);
ObsRequest reqGetSourceFilter(const std::string &source, const std::string &filter);
ObsRequest reqSetSourceFilterSettings(const std::string &source, const std::string &filter, Json settings,
				      bool overlay = true);
ObsRequest reqSetSourceFilterEnabled(const std::string &source, const std::string &filter, bool enabled);
ObsRequest reqSetSourceFilterIndex(const std::string &source, const std::string &filter, int index);
ObsRequest reqRemoveSourceFilter(const std::string &source, const std::string &filter);
// `filterKind` is the filter type id (e.g. "color_filter_v2"); settings are emitted verbatim.
ObsRequest reqCreateSourceFilter(const std::string &source, const std::string &filter,
				 const std::string &filterKind, Json filterSettings);

// ---------------------------------------------------------------------------
// Filter settings builders (only engaged fields are emitted -> use with overlay).
// ---------------------------------------------------------------------------
inline constexpr const char *kColorCorrectionKind = "color_filter_v2";

struct ColorCorrectionSettings {
	std::optional<double> gamma;       // -3.0 .. 3.0
	std::optional<double> contrast;    // -4.0 .. 4.0
	std::optional<double> brightness;  // -1.0 .. 1.0
	std::optional<double> saturation;  // -1.0 .. 5.0
	std::optional<double> hueShift;    // -180.0 .. 180.0
	std::optional<double> opacity;     // 0.0 .. 1.0 (UI shows 0-100%, stored as 0..1)
	std::optional<std::int64_t> colorMultiply; // 0xAABBGGRR as OBS stores it
	std::optional<std::int64_t> colorAdd;

	Json toJson() const;
};
ColorCorrectionSettings colorCorrectionDefaults();

// Blur is not a single built-in kind across OBS versions; pick the flavor.
enum class BlurFlavor {
	Composite, // obs-composite-blur bundled with OBS 30.1+  ("radius")
	StreamFX   // StreamFX Blur ("Filter.Blur.Size")
};
struct BlurSettings {
	BlurFlavor flavor = BlurFlavor::Composite;
	std::optional<double> size; // Composite: radius px; StreamFX: 1..64
	Json toJson() const;
};
const char *blurFilterKind(BlurFlavor f);
double blurDefaultSize(BlurFlavor f);

} // namespace se
