#include "core/payload-generator.hpp"

namespace se {

const char *boundsTypeName(BoundsType t)
{
	switch (t) {
	case BoundsType::None: return "OBS_BOUNDS_NONE";
	case BoundsType::Stretch: return "OBS_BOUNDS_STRETCH";
	case BoundsType::ScaleInner: return "OBS_BOUNDS_SCALE_INNER";
	case BoundsType::ScaleOuter: return "OBS_BOUNDS_SCALE_OUTER";
	case BoundsType::ScaleToWidth: return "OBS_BOUNDS_SCALE_TO_WIDTH";
	case BoundsType::ScaleToHeight: return "OBS_BOUNDS_SCALE_TO_HEIGHT";
	case BoundsType::MaxOnly: return "OBS_BOUNDS_MAX_ONLY";
	}
	return "OBS_BOUNDS_NONE";
}

Json TransformPatch::toJson() const
{
	Json o = Json::object();
	// Order mirrors obs-websocket's own GetSceneItemTransform output.
	if (positionX) o.set("positionX", *positionX);
	if (positionY) o.set("positionY", *positionY);
	if (rotation) o.set("rotation", *rotation);
	if (scaleX) o.set("scaleX", *scaleX);
	if (scaleY) o.set("scaleY", *scaleY);
	if (alignment) o.set("alignment", *alignment);
	if (boundsType) o.set("boundsType", boundsTypeName(*boundsType));
	if (boundsAlignment) o.set("boundsAlignment", *boundsAlignment);
	if (boundsWidth) o.set("boundsWidth", *boundsWidth);
	if (boundsHeight) o.set("boundsHeight", *boundsHeight);
	if (cropLeft) o.set("cropLeft", *cropLeft);
	if (cropRight) o.set("cropRight", *cropRight);
	if (cropTop) o.set("cropTop", *cropTop);
	if (cropBottom) o.set("cropBottom", *cropBottom);
	if (cropToBounds) o.set("cropToBounds", *cropToBounds);
	return o;
}

TransformPatch defaultTransform(double canvasW, double canvasH)
{
	TransformPatch p;
	p.positionX = 0.0;
	p.positionY = 0.0;
	p.rotation = 0.0;
	p.scaleX = 1.0;
	p.scaleY = 1.0;
	p.alignment = align::TopLeft;
	p.boundsType = BoundsType::None;
	p.boundsAlignment = align::Center;
	p.boundsWidth = canvasW;
	p.boundsHeight = canvasH;
	p.cropLeft = p.cropRight = p.cropTop = p.cropBottom = 0;
	p.cropToBounds = false;
	return p;
}

// ---------------------------------------------------------------------------
// WebSocket v5
// ---------------------------------------------------------------------------
std::string wsRequest(const std::string &requestType, Json requestData, const std::string &requestId)
{
	Json d = Json::object();
	d.set("requestType", requestType);
	d.set("requestId", requestId);
	d.set("requestData", std::move(requestData));
	Json msg = Json::object();
	msg.set("op", 6); // OpCode 6 = Request
	msg.set("d", std::move(d));
	return msg.dump();
}

static Json itemRef(const SceneItemTarget &t)
{
	Json o = Json::object();
	o.set("sceneName", t.sceneName);
	o.set("sceneItemId", t.sceneItemId);
	return o;
}

static Json setTransformData(const SceneItemTarget &t, const TransformPatch &p)
{
	Json o = itemRef(t);
	o.set("sceneItemTransform", p.toJson());
	return o;
}

static Json setFilterData(const std::string &src, const std::string &filter, const Json &settings, bool overlay)
{
	Json o = Json::object();
	o.set("sourceName", src);
	o.set("filterName", filter);
	o.set("filterSettings", settings);
	o.set("overlay", overlay);
	return o;
}

static Json getFilterData(const std::string &src, const std::string &filter)
{
	Json o = Json::object();
	o.set("sourceName", src);
	o.set("filterName", filter);
	return o;
}

std::string wsSetSceneItemTransform(const SceneItemTarget &t, const TransformPatch &p, const std::string &id)
{
	return wsRequest("SetSceneItemTransform", setTransformData(t, p), id);
}

std::string wsGetSceneItemTransform(const SceneItemTarget &t, const std::string &id)
{
	return wsRequest("GetSceneItemTransform", itemRef(t), id);
}

std::string wsSetSourceFilterSettings(const std::string &src, const std::string &filter, const Json &settings,
				      bool overlay, const std::string &id)
{
	return wsRequest("SetSourceFilterSettings", setFilterData(src, filter, settings, overlay), id);
}

std::string wsGetSourceFilter(const std::string &src, const std::string &filter, const std::string &id)
{
	return wsRequest("GetSourceFilter", getFilterData(src, filter), id);
}

// ---------------------------------------------------------------------------
// Streamer.bot
// ---------------------------------------------------------------------------
// C# verbatim string literal: only '"' needs escaping (doubled).
static std::string csVerbatim(const std::string &s)
{
	std::string out = "@\"";
	for (char c : s) {
		if (c == '"')
			out += "\"\"";
		else
			out += c;
	}
	out += '"';
	return out;
}

static std::string sbRaw(const std::string &requestType, const Json &requestData, int conn,
			 bool captureResponse = false)
{
	const std::string c = std::to_string(conn);
	std::string s;
	s += "if (!CPH.ObsIsConnected(" + c + "))\n";
	s += "{\n";
	s += "    CPH.LogWarn(\"[SourceExplorer] OBS not connected\");\n";
	s += "    return false;\n";
	s += "}\n";
	if (captureResponse)
		s += "string response = ";
	s += "CPH.ObsSendRaw(\"" + requestType + "\", " + csVerbatim(requestData.dump()) + ", " + c + ");\n";
	if (captureResponse)
		s += "CPH.LogInfo(\"[SourceExplorer] \" + response);\n";
	return s;
}

std::string sbSetSceneItemTransform(const SceneItemTarget &t, const TransformPatch &p, int conn)
{
	return sbRaw("SetSceneItemTransform", setTransformData(t, p), conn);
}

std::string sbGetSceneItemTransform(const SceneItemTarget &t, int conn)
{
	return sbRaw("GetSceneItemTransform", itemRef(t), conn, true);
}

std::string sbSetSourceFilterSettings(const std::string &src, const std::string &filter, const Json &settings,
				      bool overlay, int conn)
{
	return sbRaw("SetSourceFilterSettings", setFilterData(src, filter, settings, overlay), conn);
}

std::string sbGetSourceFilter(const std::string &src, const std::string &filter, int conn)
{
	return sbRaw("GetSourceFilter", getFilterData(src, filter), conn, true);
}

std::string wsFrom(const ObsRequest &r, const std::string &id)
{
	return wsRequest(r.type, r.data, id);
}

std::string sbFrom(const ObsRequest &r, int conn)
{
	return sbRaw(r.type, r.data, conn, r.isGetter());
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
const char *monitorTypeName(MonitorType t)
{
	switch (t) {
	case MonitorType::None: return "OBS_MONITORING_TYPE_NONE";
	case MonitorType::MonitorOnly: return "OBS_MONITORING_TYPE_MONITOR_ONLY";
	case MonitorType::MonitorAndOutput: return "OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT";
	}
	return "OBS_MONITORING_TYPE_NONE";
}

double clampVolumeDb(double db)
{
	if (db != db) // NaN
		return 0.0;
	return db < kMinVolumeDb ? kMinVolumeDb : (db > kMaxVolumeDb ? kMaxVolumeDb : db);
}

int clampSyncOffsetMs(int ms)
{
	return ms < kMinSyncOffsetMs ? kMinSyncOffsetMs : (ms > kMaxSyncOffsetMs ? kMaxSyncOffsetMs : ms);
}

double clampVolumeMul(double mul)
{
	if (mul != mul)
		return 1.0;
	return mul < 0.0 ? 0.0 : (mul > kMaxVolumeMul ? kMaxVolumeMul : mul);
}

double clampBalance(double balance)
{
	if (balance != balance)
		return 0.5;
	return balance < 0.0 ? 0.0 : (balance > 1.0 ? 1.0 : balance);
}

static Json inputRef(const std::string &input)
{
	Json o = Json::object();
	o.set("inputName", input);
	return o;
}

ObsRequest reqSetInputVolumeDb(const std::string &in, double db)
{
	Json d = inputRef(in);
	d.set("inputVolumeDb", clampVolumeDb(db));
	return {"SetInputVolume", std::move(d)};
}
ObsRequest reqGetInputVolume(const std::string &in) { return {"GetInputVolume", inputRef(in)}; }

ObsRequest reqSetInputMute(const std::string &in, bool muted)
{
	Json d = inputRef(in);
	d.set("inputMuted", muted);
	return {"SetInputMute", std::move(d)};
}
ObsRequest reqGetInputMute(const std::string &in) { return {"GetInputMute", inputRef(in)}; }

ObsRequest reqSetInputAudioSyncOffset(const std::string &in, int ms)
{
	Json d = inputRef(in);
	d.set("inputAudioSyncOffset", clampSyncOffsetMs(ms));
	return {"SetInputAudioSyncOffset", std::move(d)};
}
ObsRequest reqGetInputAudioSyncOffset(const std::string &in)
{
	return {"GetInputAudioSyncOffset", inputRef(in)};
}

ObsRequest reqSetInputVolumeMul(const std::string &in, double mul)
{
	Json d = inputRef(in);
	d.set("inputVolumeMul", clampVolumeMul(mul));
	return {"SetInputVolume", std::move(d)};
}

ObsRequest reqSetInputAudioBalance(const std::string &in, double balance)
{
	Json d = inputRef(in);
	d.set("inputAudioBalance", clampBalance(balance));
	return {"SetInputAudioBalance", std::move(d)};
}
ObsRequest reqGetInputAudioBalance(const std::string &in) { return {"GetInputAudioBalance", inputRef(in)}; }

ObsRequest reqSetInputAudioTracks(const std::string &in, const AudioTracks &tracks)
{
	Json t = Json::object();
	for (size_t i = 0; i < tracks.size(); ++i)
		t.set(std::to_string(i + 1), tracks[i]); // keys are "1".."6"
	Json d = inputRef(in);
	d.set("inputAudioTracks", std::move(t));
	return {"SetInputAudioTracks", std::move(d)};
}
ObsRequest reqGetInputAudioTracks(const std::string &in) { return {"GetInputAudioTracks", inputRef(in)}; }

ObsRequest reqSetInputAudioMonitorType(const std::string &in, MonitorType t)
{
	Json d = inputRef(in);
	d.set("monitorType", monitorTypeName(t));
	return {"SetInputAudioMonitorType", std::move(d)};
}
ObsRequest reqGetInputAudioMonitorType(const std::string &in)
{
	return {"GetInputAudioMonitorType", inputRef(in)};
}

ObsRequest reqSetSceneItemTransform(const SceneItemTarget &t, const TransformPatch &p)
{
	return {"SetSceneItemTransform", setTransformData(t, p)};
}
ObsRequest reqGetSceneItemTransform(const SceneItemTarget &t) { return {"GetSceneItemTransform", itemRef(t)}; }

ObsRequest reqSetSceneItemEnabled(const SceneItemTarget &t, bool enabled)
{
	Json d = itemRef(t);
	d.set("sceneItemEnabled", enabled);
	return {"SetSceneItemEnabled", std::move(d)};
}
ObsRequest reqGetSceneItemEnabled(const SceneItemTarget &t) { return {"GetSceneItemEnabled", itemRef(t)}; }

ObsRequest reqSetSceneItemLocked(const SceneItemTarget &t, bool locked)
{
	Json d = itemRef(t);
	d.set("sceneItemLocked", locked);
	return {"SetSceneItemLocked", std::move(d)};
}
ObsRequest reqGetSceneItemLocked(const SceneItemTarget &t) { return {"GetSceneItemLocked", itemRef(t)}; }

ObsRequest reqSetCurrentProgramScene(const std::string &sceneName)
{
	Json d = Json::object();
	d.set("sceneName", sceneName);
	return {"SetCurrentProgramScene", std::move(d)};
}
ObsRequest reqGetCurrentProgramScene() { return {"GetCurrentProgramScene", Json::object()}; }

ObsRequest reqGetSourceScreenshot(const std::string &source, const std::string &fmt)
{
	Json d = Json::object();
	d.set("sourceName", source);
	d.set("imageFormat", fmt);
	return {"GetSourceScreenshot", std::move(d)};
}

ObsRequest reqSaveSourceScreenshot(const std::string &source, const std::string &fmt, const std::string &path)
{
	Json d = Json::object();
	d.set("sourceName", source);
	d.set("imageFormat", fmt);
	d.set("imageFilePath", path);
	return {"SaveSourceScreenshot", std::move(d)};
}

const char *blendModeName(BlendMode m)
{
	switch (m) {
	case BlendMode::Normal: return "OBS_BLEND_NORMAL";
	case BlendMode::Additive: return "OBS_BLEND_ADDITIVE";
	case BlendMode::Subtract: return "OBS_BLEND_SUBTRACT";
	case BlendMode::Screen: return "OBS_BLEND_SCREEN";
	case BlendMode::Multiply: return "OBS_BLEND_MULTIPLY";
	case BlendMode::Lighten: return "OBS_BLEND_LIGHTEN";
	case BlendMode::Darken: return "OBS_BLEND_DARKEN";
	}
	return "OBS_BLEND_NORMAL";
}

ObsRequest reqSetInputName(const std::string &input, const std::string &newName)
{
	Json d = Json::object();
	d.set("inputName", input);
	d.set("newInputName", newName);
	return {"SetInputName", std::move(d)};
}

ObsRequest reqSetSceneName(const std::string &scene, const std::string &newName)
{
	Json d = Json::object();
	d.set("sceneName", scene);
	d.set("newSceneName", newName);
	return {"SetSceneName", std::move(d)};
}

ObsRequest reqSetSceneItemIndex(const SceneItemTarget &t, int index)
{
	Json d = itemRef(t);
	d.set("sceneItemIndex", index < 0 ? 0 : index);
	return {"SetSceneItemIndex", std::move(d)};
}
ObsRequest reqGetSceneItemIndex(const SceneItemTarget &t) { return {"GetSceneItemIndex", itemRef(t)}; }

ObsRequest reqSetSceneItemBlendMode(const SceneItemTarget &t, BlendMode mode)
{
	Json d = itemRef(t);
	d.set("sceneItemBlendMode", blendModeName(mode));
	return {"SetSceneItemBlendMode", std::move(d)};
}
ObsRequest reqGetSceneItemBlendMode(const SceneItemTarget &t) { return {"GetSceneItemBlendMode", itemRef(t)}; }

const char *mediaActionName(MediaAction a)
{
	switch (a) {
	case MediaAction::Play: return "OBS_WEBSOCKET_MEDIA_INPUT_ACTION_PLAY";
	case MediaAction::Pause: return "OBS_WEBSOCKET_MEDIA_INPUT_ACTION_PAUSE";
	case MediaAction::Stop: return "OBS_WEBSOCKET_MEDIA_INPUT_ACTION_STOP";
	case MediaAction::Restart: return "OBS_WEBSOCKET_MEDIA_INPUT_ACTION_RESTART";
	case MediaAction::Next: return "OBS_WEBSOCKET_MEDIA_INPUT_ACTION_NEXT";
	case MediaAction::Previous: return "OBS_WEBSOCKET_MEDIA_INPUT_ACTION_PREVIOUS";
	}
	return "OBS_WEBSOCKET_MEDIA_INPUT_ACTION_NONE";
}

ObsRequest reqPressInputPropertiesButton(const std::string &input, const std::string &propertyName)
{
	Json d = Json::object();
	d.set("inputName", input);
	d.set("propertyName", propertyName);
	return {"PressInputPropertiesButton", std::move(d)};
}

ObsRequest reqTriggerMediaInputAction(const std::string &input, MediaAction action)
{
	Json d = Json::object();
	d.set("inputName", input);
	d.set("mediaAction", mediaActionName(action));
	return {"TriggerMediaInputAction", std::move(d)};
}

ObsRequest reqGetMediaInputStatus(const std::string &input) { return {"GetMediaInputStatus", Json::object().set("inputName", input)}; }

ObsRequest reqSetInputSettings(const std::string &in, Json settings, bool overlay)
{
	Json d = inputRef(in);
	d.set("inputSettings", std::move(settings));
	d.set("overlay", overlay);
	return {"SetInputSettings", std::move(d)};
}

ObsRequest reqGetInputSettings(const std::string &in) { return {"GetInputSettings", inputRef(in)}; }

// ---------------------------------------------------------------------------
// Filter management
// ---------------------------------------------------------------------------
static Json sourceRef(const std::string &source)
{
	Json o = Json::object();
	o.set("sourceName", source);
	return o;
}

ObsRequest reqGetSourceFilterList(const std::string &source)
{
	return {"GetSourceFilterList", sourceRef(source)};
}

ObsRequest reqGetSourceFilter(const std::string &source, const std::string &filter)
{
	return {"GetSourceFilter", getFilterData(source, filter)};
}

ObsRequest reqSetSourceFilterSettings(const std::string &source, const std::string &filter, Json settings,
				      bool overlay)
{
	return {"SetSourceFilterSettings", setFilterData(source, filter, settings, overlay)};
}

ObsRequest reqSetSourceFilterEnabled(const std::string &source, const std::string &filter, bool enabled)
{
	Json d = getFilterData(source, filter);
	d.set("filterEnabled", enabled);
	return {"SetSourceFilterEnabled", std::move(d)};
}

ObsRequest reqSetSourceFilterIndex(const std::string &source, const std::string &filter, int index)
{
	Json d = getFilterData(source, filter);
	d.set("filterIndex", index < 0 ? 0 : index);
	return {"SetSourceFilterIndex", std::move(d)};
}

ObsRequest reqRemoveSourceFilter(const std::string &source, const std::string &filter)
{
	return {"RemoveSourceFilter", getFilterData(source, filter)};
}

ObsRequest reqCreateSourceFilter(const std::string &source, const std::string &filter, const std::string &kind,
				 Json settings)
{
	Json d = Json::object();
	d.set("sourceName", source);
	d.set("filterName", filter);
	d.set("filterKind", kind);
	d.set("filterSettings", std::move(settings));
	return {"CreateSourceFilter", std::move(d)};
}

// ---------------------------------------------------------------------------
// Filter settings
// ---------------------------------------------------------------------------
Json ColorCorrectionSettings::toJson() const
{
	Json o = Json::object();
	if (gamma) o.set("gamma", *gamma);
	if (contrast) o.set("contrast", *contrast);
	if (brightness) o.set("brightness", *brightness);
	if (saturation) o.set("saturation", *saturation);
	if (hueShift) o.set("hue_shift", *hueShift);
	if (opacity) o.set("opacity", *opacity);
	if (colorMultiply) o.set("color_multiply", *colorMultiply);
	if (colorAdd) o.set("color_add", *colorAdd);
	return o;
}

ColorCorrectionSettings colorCorrectionDefaults()
{
	ColorCorrectionSettings s;
	s.gamma = 0.0;
	s.contrast = 0.0;
	s.brightness = 0.0;
	s.saturation = 0.0;
	s.hueShift = 0.0;
	s.opacity = 1.0;
	s.colorMultiply = 0xFFFFFF;
	s.colorAdd = 0;
	return s;
}

const char *blurFilterKind(BlurFlavor f)
{
	return f == BlurFlavor::Composite ? "composite_blur" : "streamfx-filter-blur";
}

double blurDefaultSize(BlurFlavor f)
{
	return f == BlurFlavor::Composite ? 1.0 : 1.0;
}

Json BlurSettings::toJson() const
{
	Json o = Json::object();
	if (size)
		o.set(flavor == BlurFlavor::Composite ? "radius" : "Filter.Blur.Size", *size);
	return o;
}

} // namespace se
