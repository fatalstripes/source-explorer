// SPDX-License-Identifier: GPL-2.0-or-later
#include "ui/quick-access-bar.hpp"

#include "core/scene-item-ops.hpp"
#include "ui/filter-popouts.hpp"
#include "ui/widgets/command-menu.hpp"
#include "ui/widgets/flow-layout.hpp"

#include <obs-frontend-api.h>
#include <obs.h>

#include <QMenu>
#include <QPointer>
#include <QSignalBlocker>
#include <QToolButton>

namespace se {

namespace {

std::string nameOf(obs_source_t *s)
{
	const char *n = s ? obs_source_get_name(s) : nullptr;
	return n ? n : "";
}

const char *monitorLabel(obs_monitoring_type t)
{
	switch (t) {
	case OBS_MONITORING_TYPE_MONITOR_ONLY: return "Monitor: only";
	case OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT: return "Monitor: + output";
	default: return "Monitor: off";
	}
}

} // namespace

QuickAccessBar::QuickAccessBar(QWidget *parent) : QWidget(parent)
{
	setObjectName("seQuickAccess");
	flow_ = new FlowLayout(this, 4);
	setLayout(flow_);
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
	traits_.openFilterWindows = FilterPopouts::instance().count();
	connect(&FilterPopouts::instance(), &FilterPopouts::countChanged, this, [this](int n) {
		const bool changed = (n > 0) != (traits_.openFilterWindows > 0);
		traits_.openFilterWindows = n;
		if (changed)
			rebuild();
		else if (auto it = buttons_.find(QuickButton::CloseFilterWindows); it != buttons_.end())
			it->second->setText(tr("Close all filter windows (%1)").arg(n));
	});
	rebuild();
}

QuickAccessBar::~QuickAccessBar() = default;

void QuickAccessBar::setPinned(bool pinned)
{
	pinned_ = pinned;
	if (auto it = buttons_.find(QuickButton::Pin); it != buttons_.end()) {
		QSignalBlocker block(it->second);
		it->second->setChecked(pinned);
	}
}

void QuickAccessBar::setTarget(obs_sceneitem_t *item, obs_source_t *source)
{
	const bool sameSource = source == source_.get() && source == traitsFor_;
	const bool sameItem = item == item_.get();
	if (sameSource && sameItem)
		return;

	item_ = obs::retain(item);
	source_ = obs::retain(source);

	SourceTraits t;
	t.hasItem = item != nullptr;
	t.openFilterWindows = traits_.openFilterWindows;
	if (source) {
		const uint32_t flags = obs_source_get_output_flags(source);
		const char *id = obs_source_get_id(source);
		t.hasAudio = (flags & OBS_SOURCE_AUDIO) != 0;
		t.isMedia = (flags & OBS_SOURCE_CONTROLLABLE_MEDIA) != 0;
		t.isBrowser = id && std::string(id) == "browser_source";
		// Probe the properties once per source, not per tick: building them can be costly.
		if (!sameSource) {
			obs::PropertiesRef props = obs::adopt(obs_source_properties(source));
			t.hasActivateButton = props && obs_properties_get(props.get(), "activate") != nullptr;
		} else {
			t.hasActivateButton = traits_.hasActivateButton;
		}
	}
	traitsFor_ = source;

	const bool changed = quickButtonsFor(t) != quickButtonsFor(traits_) || !sameSource || buttons_.empty();
	traits_ = t;
	if (changed)
		rebuild();
	refresh();
}

QToolButton *QuickAccessBar::addButton(QuickButton kind, const QString &text, const QString &tip, bool checkable)
{
	auto *b = new QToolButton(this);
	b->setObjectName("seQuickButton");
	b->setText(text);
	b->setToolTip(tip);
	b->setCheckable(checkable);
	b->setToolButtonStyle(Qt::ToolButtonTextOnly);
	flow_->addWidget(b);
	buttons_[kind] = b;
	return b;
}

bool QuickAccessBar::pressProperty(const char *propertyName)
{
	if (!source_)
		return false;
	obs::PropertiesRef props = obs::adopt(obs_source_properties(source_.get()));
	obs_property_t *p = props ? obs_properties_get(props.get(), propertyName) : nullptr;
	if (!p)
		return false;
	obs_property_button_clicked(p, source_.get());
	return true;
}

void QuickAccessBar::rebuild()
{
	while (QLayoutItem *it = flow_->takeAt(0)) {
		delete it->widget();
		delete it;
	}
	buttons_.clear();

	QPointer<QuickAccessBar> self(this);
	for (QuickButton kind : quickButtonsFor(traits_)) {
		switch (kind) {
		case QuickButton::Visibility: {
			auto *b = addButton(kind, tr("Visible"), tr("Show or hide this source in its scene"), true);
			connect(b, &QToolButton::toggled, this, [self](bool on) {
				if (self && self->item_)
					obs_sceneitem_set_visible(self->item_.get(), on);
			});
			break;
		}
		case QuickButton::Lock: {
			auto *b = addButton(kind, tr("Locked"), tr("Lock or unlock this source in its scene"), true);
			connect(b, &QToolButton::toggled, this, [self](bool on) {
				if (self && self->item_)
					obs_sceneitem_set_locked(self->item_.get(), on);
			});
			break;
		}
		case QuickButton::Screenshot: {
			// Not "Screenshot": OBS translates its own string keys everywhere, which would show
			// "Screenshot Output" here.
			auto *b = addButton(kind, tr("Snapshot"), tr("Take a screenshot of this source"), false);
			connect(b, &QToolButton::clicked, this, [self] {
				if (self && self->source_)
					obs_frontend_take_source_screenshot(self->source_.get()); // saved to OBS's screenshot folder
			});
			break;
		}
		case QuickButton::ResetUi: {
			auto *b = addButton(kind, tr("Reset UI"), tr("Close the source tree, go back to Overview and unpin"), false);
			connect(b, &QToolButton::clicked, this, &QuickAccessBar::resetUiRequested);
			break;
		}
		case QuickButton::Pin: {
			auto *b = addButton(kind, tr("Pin"), tr("Keep this source loaded instead of following OBS's selection"), true);
			b->setChecked(pinned_);
			connect(b, &QToolButton::toggled, this, [self](bool on) {
				if (!self)
					return;
				self->pinned_ = on;
				emit self->pinToggled(on);
			});
			break;
		}
		case QuickButton::Mute: {
			auto *b = addButton(kind, tr("Mute"), tr("Mute or unmute this source"), true);
			connect(b, &QToolButton::toggled, this, [self](bool on) {
				if (self && self->source_)
					obs_source_set_muted(self->source_.get(), on);
			});
			break;
		}
		case QuickButton::Monitoring: {
			auto *b = addButton(kind, tr(monitorLabel(OBS_MONITORING_TYPE_NONE)),
					    tr("Cycle monitoring: off, monitor only, monitor and output"), false);
			connect(b, &QToolButton::clicked, this, [self] {
				if (!self || !self->source_)
					return;
				const auto cur = static_cast<MonitorType>(obs_source_get_monitoring_type(self->source_.get()));
				obs_source_set_monitoring_type(self->source_.get(),
							       static_cast<obs_monitoring_type>(nextMonitorType(cur)));
				self->refresh();
			});
			break;
		}
		case QuickButton::RefreshCache: {
			auto *b = addButton(kind, tr("Refresh cache"), tr("Reload this browser source, ignoring its cache"), false);
			connect(b, &QToolButton::clicked, this, [self] {
				if (self)
					self->pressProperty("refreshnocache");
			});
			break;
		}
		case QuickButton::ActivateToggle: {
			auto *b = addButton(kind, tr("Activate / Deactivate"), tr("Start or stop this capture device"), false);
			connect(b, &QToolButton::clicked, this, [self] {
				if (self)
					self->pressProperty("activate");
			});
			break;
		}
		case QuickButton::Transform: {
			auto *b = addButton(kind, tr("Transform"), tr("Flip, rotate, fit, stretch and centre"), false);
			auto *menu = new QMenu(b);
			struct Entry {
				const char *text;
				std::function<TransformPatch(obs_sceneitem_t *)> plan;
			};
			const std::vector<Entry> entries = {
				{"Flip horizontal", [](auto *i) { return ops::planFlip(i, true); }},
				{"Flip vertical", [](auto *i) { return ops::planFlip(i, false); }},
				{"Rotate 90 CCW", [](auto *i) { return ops::planRotate(i, -90.0); }},
				{"Rotate 90 CW", [](auto *i) { return ops::planRotate(i, 90.0); }},
				{"Rotate 180", [](auto *i) { return ops::planRotate(i, 180.0); }},
				{"Fit to screen", [](auto *i) { return ops::planFit(i, FitMode::Contain); }},
				{"Stretch to screen", [](auto *i) { return ops::planFit(i, FitMode::Stretch); }},
				{"Center", [](auto *i) { return ops::planCenter(i); }},
				{"Center vertically", [](auto *i) { return ops::planCenterVertically(i); }},
				{"Center horizontally", [](auto *i) { return ops::planCenterHorizontally(i); }},
			};
			for (const Entry &e : entries) {
				menu->addAction(tr(e.text), this, [self, plan = e.plan] {
					if (!self || !self->item_)
						return;
					const TransformPatch p = plan(self->item_.get());
					if (p.toJson().dump() != "{}") // empty = not applicable right now
						ops::apply(self->item_.get(), p);
				});
			}
			b->setMenu(menu);
			b->setPopupMode(QToolButton::InstantPopup);
			break;
		}
		case QuickButton::MediaPlayPause: {
			auto *b = addButton(kind, tr("Play"), tr("Play or pause"), false);
			connect(b, &QToolButton::clicked, this, [self] {
				if (!self || !self->source_)
					return;
				const bool playing = obs_source_media_get_state(self->source_.get()) == OBS_MEDIA_STATE_PLAYING;
				obs_source_media_play_pause(self->source_.get(), playing); // true = pause
				self->refresh();
			});
			break;
		}
		case QuickButton::MediaStop: {
			auto *b = addButton(kind, tr("Stop"), tr("Stop playback"), false);
			connect(b, &QToolButton::clicked, this, [self] {
				if (self && self->source_)
					obs_source_media_stop(self->source_.get());
			});
			break;
		}
		case QuickButton::CloseFilterWindows: {
			auto *b = addButton(kind, tr("Close all filter windows (%1)").arg(traits_.openFilterWindows),
					    tr("Close every filter window that was popped out of the Filters tab"), false);
			connect(b, &QToolButton::clicked, this, [] { FilterPopouts::instance().closeAll(); });
			break;
		}
		case QuickButton::MediaRestart: {
			auto *b = addButton(kind, tr("Restart"), tr("Restart from the beginning"), false);
			connect(b, &QToolButton::clicked, this, [self] {
				if (self && self->source_)
					obs_source_media_restart(self->source_.get());
			});
			break;
		}
		}
	}
	attachMenus();
}

// Right-click copies the command a click would send right now, as in the rest of the dock.
void QuickAccessBar::attachMenus()
{
	QPointer<QuickAccessBar> self(this);
	auto target = [self]() {
		SceneItemTarget t;
		if (self && self->item_) {
			if (obs_scene_t *sc = obs_sceneitem_get_scene(self->item_.get()))
				t.sceneName = nameOf(obs_scene_get_source(sc));
			t.sceneItemId = obs_sceneitem_get_id(self->item_.get());
		}
		return t;
	};
	auto menuFor = [&](QuickButton kind, std::function<std::optional<CommandSet>()> provider) {
		if (auto it = buttons_.find(kind); it != buttons_.end())
			attachCommandMenu(it->second, std::move(provider));
	};

	menuFor(QuickButton::Visibility, [self, target]() -> std::optional<CommandSet> {
		if (!self || !self->item_)
			return std::nullopt;
		const SceneItemTarget t = target();
		return makeSet(reqSetSceneItemEnabled(t, !obs_sceneitem_visible(self->item_.get())), reqGetSceneItemEnabled(t));
	});
	menuFor(QuickButton::Lock, [self, target]() -> std::optional<CommandSet> {
		if (!self || !self->item_)
			return std::nullopt;
		const SceneItemTarget t = target();
		return makeSet(reqSetSceneItemLocked(t, !obs_sceneitem_locked(self->item_.get())), reqGetSceneItemLocked(t));
	});
	menuFor(QuickButton::Screenshot, [self]() -> std::optional<CommandSet> {
		if (!self || !self->source_)
			return std::nullopt;
		const std::string name = nameOf(self->source_.get());
		// Save needs a path on the machine running OBS; this one is a placeholder to edit.
		return makeSet(reqSaveSourceScreenshot(name, "png", "C:/snapshots/" + name + ".png"),
			       reqGetSourceScreenshot(name, "png"));
	});
	menuFor(QuickButton::Mute, [self]() -> std::optional<CommandSet> {
		if (!self || !self->source_)
			return std::nullopt;
		const std::string n = nameOf(self->source_.get());
		return makeSet(reqSetInputMute(n, !obs_source_muted(self->source_.get())), reqGetInputMute(n));
	});
	menuFor(QuickButton::Monitoring, [self]() -> std::optional<CommandSet> {
		if (!self || !self->source_)
			return std::nullopt;
		const std::string n = nameOf(self->source_.get());
		const auto cur = static_cast<MonitorType>(obs_source_get_monitoring_type(self->source_.get()));
		return makeSet(reqSetInputAudioMonitorType(n, nextMonitorType(cur)), reqGetInputAudioMonitorType(n));
	});
	menuFor(QuickButton::RefreshCache, [self]() -> std::optional<CommandSet> {
		if (!self || !self->source_)
			return std::nullopt;
		const std::string n = nameOf(self->source_.get());
		CommandSet c;
		c.streamerBot = [n] { return sbFrom(reqPressInputPropertiesButton(n, "refreshnocache")); };
		c.wsSet = [n] { return wsFrom(reqPressInputPropertiesButton(n, "refreshnocache")); };
		return c;
	});
	menuFor(QuickButton::ActivateToggle, [self]() -> std::optional<CommandSet> {
		if (!self || !self->source_)
			return std::nullopt;
		const std::string n = nameOf(self->source_.get());
		CommandSet c;
		c.streamerBot = [n] { return sbFrom(reqPressInputPropertiesButton(n, "activate")); };
		c.wsSet = [n] { return wsFrom(reqPressInputPropertiesButton(n, "activate")); };
		return c;
	});
	auto media = [&](QuickButton kind, std::function<MediaAction(obs_source_t *)> action) {
		menuFor(kind, [self, action]() -> std::optional<CommandSet> {
			if (!self || !self->source_)
				return std::nullopt;
			const std::string n = nameOf(self->source_.get());
			return makeSet(reqTriggerMediaInputAction(n, action(self->source_.get())), reqGetMediaInputStatus(n));
		});
	};
	media(QuickButton::MediaPlayPause, [](obs_source_t *s) {
		return obs_source_media_get_state(s) == OBS_MEDIA_STATE_PLAYING ? MediaAction::Pause : MediaAction::Play;
	});
	media(QuickButton::MediaStop, [](obs_source_t *) { return MediaAction::Stop; });
	media(QuickButton::MediaRestart, [](obs_source_t *) { return MediaAction::Restart; });
}

void QuickAccessBar::refresh()
{
	auto setChecked = [&](QuickButton kind, bool on) {
		if (auto it = buttons_.find(kind); it != buttons_.end() && it->second->isChecked() != on) {
			QSignalBlocker block(it->second); // reflect OBS's state, don't send it back
			it->second->setChecked(on);
		}
	};
	if (item_) {
		setChecked(QuickButton::Visibility, obs_sceneitem_visible(item_.get()));
		setChecked(QuickButton::Lock, obs_sceneitem_locked(item_.get()));
	}
	if (source_) {
		setChecked(QuickButton::Mute, obs_source_muted(source_.get()));
		if (auto it = buttons_.find(QuickButton::Monitoring); it != buttons_.end())
			it->second->setText(tr(monitorLabel(obs_source_get_monitoring_type(source_.get()))));
		if (auto it = buttons_.find(QuickButton::MediaPlayPause); it != buttons_.end())
			it->second->setText(obs_source_media_get_state(source_.get()) == OBS_MEDIA_STATE_PLAYING ? tr("Pause")
														 : tr("Play"));
		if (auto it = buttons_.find(QuickButton::Screenshot); it != buttons_.end())
			it->second->setEnabled((obs_source_get_output_flags(source_.get()) & OBS_SOURCE_VIDEO) != 0);
	}
	setChecked(QuickButton::Pin, pinned_);
}

} // namespace se
