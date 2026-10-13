// SPDX-License-Identifier: GPL-2.0-or-later
#include "ui/panels/misc-panel.hpp"

#include "ui/scene-switch.hpp"
#include "ui/widgets/command-menu.hpp"

#include <obs-frontend-api.h>
#include <obs.h>

#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

namespace se {

namespace {

std::string nameOf(obs_source_t *s)
{
	const char *n = s ? obs_source_get_name(s) : nullptr;
	return n ? n : "";
}

QString yesNo(bool v)
{
	return v ? QObject::tr("Yes") : QObject::tr("No");
}

const char *typeName(obs_source_type t)
{
	switch (t) {
	case OBS_SOURCE_TYPE_INPUT: return "Input";
	case OBS_SOURCE_TYPE_FILTER: return "Filter";
	case OBS_SOURCE_TYPE_TRANSITION: return "Transition";
	case OBS_SOURCE_TYPE_SCENE: return "Scene";
	}
	return "Unknown";
}

// Output flags as the words OBS's headers use, so they can be looked up.
QString flagList(uint32_t flags)
{
	struct F {
		uint32_t bit;
		const char *name;
	};
	static const F table[] = {
		{OBS_SOURCE_VIDEO, "VIDEO"},
		{OBS_SOURCE_AUDIO, "AUDIO"},
		{OBS_SOURCE_ASYNC, "ASYNC"},
		{OBS_SOURCE_CUSTOM_DRAW, "CUSTOM_DRAW"},
		{OBS_SOURCE_INTERACTION, "INTERACTION"},
		{OBS_SOURCE_COMPOSITE, "COMPOSITE"},
		{OBS_SOURCE_DO_NOT_DUPLICATE, "DO_NOT_DUPLICATE"},
		{OBS_SOURCE_DEPRECATED, "DEPRECATED"},
		{OBS_SOURCE_DO_NOT_SELF_MONITOR, "DO_NOT_SELF_MONITOR"},
		{OBS_SOURCE_CAP_DISABLED, "CAP_DISABLED"},
		{OBS_SOURCE_MONITOR_BY_DEFAULT, "MONITOR_BY_DEFAULT"},
		{OBS_SOURCE_SUBMIX, "SUBMIX"},
		{OBS_SOURCE_CONTROLLABLE_MEDIA, "CONTROLLABLE_MEDIA"},
		{OBS_SOURCE_CEA_708, "CEA_708"},
		{OBS_SOURCE_SRGB, "SRGB"},
		{OBS_SOURCE_REQUIRES_CANVAS, "REQUIRES_CANVAS"},
	};
	QStringList names;
	for (const F &f : table)
		if (flags & f.bit)
			names << f.name;
	return names.isEmpty() ? QObject::tr("none") : names.join(", ");
}

// Scenes that contain `src` (searching into groups).
std::vector<std::string> scenesContaining(obs_source_t *src)
{
	std::vector<std::string> out;
	if (!src)
		return out;
	const char *name = obs_source_get_name(src);
	obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; ++i) {
		obs_source_t *s = scenes.sources.array[i];
		obs_scene_t *sc = obs_scene_from_source(s);
		if (sc && s != src && obs_scene_find_source_recursive(sc, name))
			out.push_back(nameOf(s));
	}
	obs_frontend_source_list_free(&scenes);
	return out;
}

} // namespace

MiscPanel::MiscPanel(QWidget *parent) : QWidget(parent)
{
	buildUi();
	poll_ = new QTimer(this);
	poll_->setInterval(500);
	connect(poll_, &QTimer::timeout, this, [this] {
		if (isVisible())
			refresh();
	});
	poll_->start();
	refresh();
}

MiscPanel::~MiscPanel() = default;

obs_source_t *MiscPanel::shown() const
{
	return item_ ? obs_sceneitem_get_source(item_.get()) : source_.get();
}

void MiscPanel::buildUi()
{
	auto *root = new QVBoxLayout(this);

	hint_ = new QLabel(tr("Select a source to see its details."), this);
	hint_->setObjectName("seHint");
	hint_->setForegroundRole(QPalette::PlaceholderText);
	root->addWidget(hint_);

	// Identifiers --------------------------------------------------------------------------
	auto *info = new QGroupBox(tr("Source info"), this);
	auto *infoForm = new QFormLayout(info);
	auto row = [&](const QString &label, bool wrap = false) {
		auto *l = new QLabel(info);
		l->setTextInteractionFlags(Qt::TextSelectableByMouse);
		l->setWordWrap(wrap);
		infoForm->addRow(label, l);
		return l;
	};
	id_ = row(tr("Source ID"));
	unversionedId_ = row(tr("Type ID"));
	kind_ = row(tr("Kind"));
	uuid_ = row(tr("UUID"), true);
	flags_ = row(tr("Flags"), true);
	dimensions_ = row(tr("Dimensions"));
	root->addWidget(info);

	// Status ------------------------------------------------------------------------------
	auto *status = new QGroupBox(tr("Status"), this);
	toggleForm_ = new QFormLayout(status);
	muted_ = new QCheckBox(status);
	toggleForm_->addRow(tr("Muted"), muted_);
	visible_ = new QCheckBox(status);
	toggleForm_->addRow(tr("Visible in scene"), visible_);
	locked_ = new QCheckBox(status);
	toggleForm_->addRow(tr("Locked in scene"), locked_);
	active_ = new QLabel(status);
	toggleForm_->addRow(tr("Active"), active_);
	showing_ = new QLabel(status);
	toggleForm_->addRow(tr("Showing"), showing_);
	enabled_ = new QLabel(status);
	toggleForm_->addRow(tr("Enabled"), enabled_);
	filters_ = new QLabel(status);
	toggleForm_->addRow(tr("Filters"), filters_);
	root->addWidget(status);

	// Scenes containing the source ---------------------------------------------------------
	auto *scenesBox = new QGroupBox(tr("Scenes containing this source"), this);
	scenes_ = new QVBoxLayout(scenesBox);
	scenesEmpty_ = new QLabel(tr("Not in any scene."), scenesBox);
	scenesEmpty_->setForegroundRole(QPalette::PlaceholderText);
	scenes_->addWidget(scenesEmpty_);
	root->addWidget(scenesBox);
	root->addStretch(1);

	// Edits -> OBS ----------------------------------------------------------------------------
	connect(muted_, &QCheckBox::toggled, this, [this](bool on) {
		if (!updating_ && shown())
			obs_source_set_muted(shown(), on);
	});
	connect(visible_, &QCheckBox::toggled, this, [this](bool on) {
		if (!updating_ && item_)
			obs_sceneitem_set_visible(item_.get(), on);
	});
	connect(locked_, &QCheckBox::toggled, this, [this](bool on) {
		if (!updating_ && item_)
			obs_sceneitem_set_locked(item_.get(), on);
	});

	// Command copier on the toggles ----------------------------------------------------------
	QPointer<MiscPanel> self(this);
	auto itemTarget = [self]() {
		SceneItemTarget t;
		if (self && self->item_) {
			if (obs_scene_t *sc = obs_sceneitem_get_scene(self->item_.get()))
				t.sceneName = nameOf(obs_scene_get_source(sc));
			t.sceneItemId = obs_sceneitem_get_id(self->item_.get());
		}
		return t;
	};
	attachCommandMenu(muted_, [self]() -> std::optional<CommandSet> {
		if (!self || !self->shown())
			return std::nullopt;
		const std::string n = nameOf(self->shown());
		CommandSet c = makeSet(reqSetInputMute(n, self->muted_->isChecked()), reqGetInputMute(n));
		c.reset = [self] {
			if (self && self->shown())
				obs_source_set_muted(self->shown(), false);
		};
		return c;
	});
	attachCommandMenu(visible_, [self, itemTarget]() -> std::optional<CommandSet> {
		if (!self || !self->item_)
			return std::nullopt;
		const SceneItemTarget t = itemTarget();
		CommandSet c = makeSet(reqSetSceneItemEnabled(t, self->visible_->isChecked()), reqGetSceneItemEnabled(t));
		c.reset = [self] {
			if (self && self->item_)
				obs_sceneitem_set_visible(self->item_.get(), true);
		};
		return c;
	});
	attachCommandMenu(locked_, [self, itemTarget]() -> std::optional<CommandSet> {
		if (!self || !self->item_)
			return std::nullopt;
		const SceneItemTarget t = itemTarget();
		CommandSet c = makeSet(reqSetSceneItemLocked(t, self->locked_->isChecked()), reqGetSceneItemLocked(t));
		c.reset = [self] {
			if (self && self->item_)
				obs_sceneitem_set_locked(self->item_.get(), false);
		};
		return c;
	});
}

void MiscPanel::setItem(obs_sceneitem_t *item)
{
	item_ = obs::retain(item);
	source_.reset();
	refresh();
}

void MiscPanel::setSource(obs_source_t *source)
{
	item_.reset();
	source_ = obs::retain(source);
	refresh();
}

void MiscPanel::rebuildSceneButtons(const std::vector<std::string> &names)
{
	// Remove the old buttons (everything after the "empty" label).
	while (scenes_->count() > 1) {
		QLayoutItem *it = scenes_->takeAt(1);
		delete it->widget();
		delete it;
	}
	scenesEmpty_->setVisible(names.empty());
	for (const std::string &name : names) {
		auto *b = new QPushButton(QString::fromStdString(name));
		b->setToolTip(tr("Switch to this scene"));
		QPointer<MiscPanel> self(this);
		connect(b, &QPushButton::clicked, this, [self, name] {
			if (!self)
				return;
			obs::SourceRef scene = obs::sourceByName(name.c_str());
			if (scene)
				requestSceneSwitch(self, scene.get()); // studio mode: preview; live: asks first
		});
		scenes_->addWidget(b);
	}
	sceneNames_ = names;
}

void MiscPanel::refresh()
{
	obs_source_t *src = shown();
	hint_->setVisible(!src);
	for (QGroupBox *g : findChildren<QGroupBox *>())
		g->setVisible(src != nullptr);
	if (!src)
		return;

	const bool hasItem = static_cast<bool>(item_);
	const uint32_t flags = obs_source_get_output_flags(src);

	updating_ = true;
	{
		QSignalBlocker b1(muted_), b2(visible_), b3(locked_);
		id_->setText(QString::fromUtf8(obs_source_get_id(src)));
		const char *unv = obs_source_get_unversioned_id(src);
		unversionedId_->setText(QString::fromUtf8(unv ? unv : ""));
		kind_->setText(typeName(obs_source_get_type(src)));
		const char *uuid = obs_source_get_uuid(src);
		uuid_->setText(QString::fromUtf8(uuid ? uuid : ""));
		flags_->setText(flagList(flags));
		const uint32_t w = obs_source_get_width(src), h = obs_source_get_height(src);
		dimensions_->setText(w ? QString("%1 x %2").arg(w).arg(h) : tr("n/a"));

		muted_->setChecked(obs_source_muted(src));
		active_->setText(yesNo(obs_source_active(src)));
		showing_->setText(yesNo(obs_source_showing(src)));
		enabled_->setText(yesNo(obs_source_enabled(src)));
		filters_->setText(QString::number(obs_source_filter_count(src)));
		if (hasItem) {
			visible_->setChecked(obs_sceneitem_visible(item_.get()));
			locked_->setChecked(obs_sceneitem_locked(item_.get()));
		}
	}
	updating_ = false;

	muted_->setEnabled(flags & OBS_SOURCE_AUDIO);
	toggleForm_->setRowVisible(muted_, (flags & OBS_SOURCE_AUDIO) != 0);
	toggleForm_->setRowVisible(visible_, hasItem);
	toggleForm_->setRowVisible(locked_, hasItem);

	const auto names = scenesContaining(src);
	if (names != sceneNames_)
		rebuildSceneButtons(names);
}

} // namespace se
