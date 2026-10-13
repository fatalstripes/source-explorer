// SPDX-License-Identifier: GPL-2.0-or-later
#include "ui/panels/source-panel.hpp"

#include "ui/panels/properties-panel.hpp"
#include "ui/widgets/command-menu.hpp"

#include <obs-frontend-api.h>
#include <obs.h>

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <string>

namespace se {

// Our enums are index-compatible with libobs; enforce it.
static_assert(static_cast<int>(BlendMode::Normal) == OBS_BLEND_NORMAL);
static_assert(static_cast<int>(BlendMode::Additive) == OBS_BLEND_ADDITIVE);
static_assert(static_cast<int>(BlendMode::Subtract) == OBS_BLEND_SUBTRACT);
static_assert(static_cast<int>(BlendMode::Screen) == OBS_BLEND_SCREEN);
static_assert(static_cast<int>(BlendMode::Multiply) == OBS_BLEND_MULTIPLY);
static_assert(static_cast<int>(BlendMode::Lighten) == OBS_BLEND_LIGHTEN);
static_assert(static_cast<int>(BlendMode::Darken) == OBS_BLEND_DARKEN);

namespace {

std::string nameOf(obs_source_t *s)
{
	const char *n = s ? obs_source_get_name(s) : nullptr;
	return n ? n : "";
}

struct CountItems {
	int count = 0;
	static bool cb(obs_scene_t *, obs_sceneitem_t *, void *p)
	{
		++static_cast<CountItems *>(p)->count;
		return true;
	}
};

// How many scenes (including groups' parents) contain this source, searching into groups.
int scenesUsing(obs_source_t *src)
{
	if (!src)
		return 0;
	const char *name = obs_source_get_name(src);
	int n = 0;
	obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; ++i) {
		obs_scene_t *sc = obs_scene_from_source(scenes.sources.array[i]);
		if (sc && obs_scene_find_source_recursive(sc, name))
			++n;
	}
	obs_frontend_source_list_free(&scenes);
	return n;
}

} // namespace

SourcePanel::SourcePanel(QWidget *parent) : QWidget(parent)
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

SourcePanel::~SourcePanel() = default;

obs_source_t *SourcePanel::shown() const
{
	return item_ ? obs_sceneitem_get_source(item_.get()) : source_.get();
}

SceneItemTarget SourcePanel::target() const
{
	SceneItemTarget t;
	if (item_) {
		// Group children are addressed through the group's name.
		if (obs_scene_t *scene = obs_sceneitem_get_scene(item_.get()))
			t.sceneName = nameOf(obs_scene_get_source(scene));
		t.sceneItemId = obs_sceneitem_get_id(item_.get());
	}
	return t;
}

void SourcePanel::say(const QString &message)
{
	status_->setText(message);
	status_->setVisible(!message.isEmpty());
	QPointer<SourcePanel> self(this);
	QTimer::singleShot(5000, this, [self, message] {
		if (self && self->status_->text() == message)
			self->status_->setVisible(false);
	});
}

QComboBox *SourcePanel::makeTransitionBox(bool /*show*/)
{
	auto *box = new QComboBox(this);
	box->addItem(tr("None"), QString());
	const char *id = nullptr;
	for (size_t i = 0; obs_enum_transition_types(i, &id); ++i)
		box->addItem(QString::fromUtf8(obs_source_get_display_name(id)), QString::fromUtf8(id));
	return box;
}

// Item transitions are private transition instances owned by the item, not entries in OBS's
// transition list. A new one is created per change; the item keeps its own reference.
void SourcePanel::applyTransition(bool show, const QString &typeId)
{
	if (!item_)
		return;
	if (typeId.isEmpty()) {
		obs_sceneitem_set_transition(item_.get(), show, nullptr);
		return;
	}
	const QByteArray id = typeId.toUtf8();
	obs::SourceRef t = obs::adopt(obs_source_create_private(id.constData(), id.constData(), nullptr));
	if (!t)
		return;
	obs_sceneitem_set_transition(item_.get(), show, t.get());
	if (obs_sceneitem_get_transition_duration(item_.get(), show) == 0)
		obs_sceneitem_set_transition_duration(item_.get(), show, 300); // OBS's usual default
}

// A floating window with the transition's own properties (duration aside), like OBS's gear button.
void SourcePanel::openTransitionSettings(bool show)
{
	if (!item_)
		return;
	obs_source_t *t = obs_sceneitem_get_transition(item_.get(), show); // borrowed
	if (!t)
		return;
	QPointer<QWidget> &slot = transitionWindow_[show ? 1 : 0];
	if (slot)
		slot->close();
	auto *win = new QWidget(this, Qt::Window | Qt::Tool);
	win->setAttribute(Qt::WA_DeleteOnClose);
	win->setWindowTitle(show ? tr("Show transition settings") : tr("Hide transition settings"));
	auto *layout = new QVBoxLayout(win);
	auto *scroll = new QScrollArea(win);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *panel = new PropertiesPanel(PropertiesPanel::Mode::Embedded, scroll);
	panel->setTarget(t); // the panel keeps its own reference
	scroll->setWidget(panel);
	layout->addWidget(scroll);
	win->resize(380, 420);
	slot = win;
	win->show();
}

void SourcePanel::buildUi()
{
	auto *root = new QVBoxLayout(this);

	hint_ = new QLabel(tr("Select a source to see its settings."), this);
	hint_->setObjectName("seHint");
	hint_->setForegroundRole(QPalette::PlaceholderText);
	root->addWidget(hint_);

	form_ = new QFormLayout;
	root->addLayout(form_);

	name_ = new QLineEdit(this);
	form_->addRow(tr("Name"), name_);
	type_ = new QLabel(this);
	form_->addRow(tr("Type"), type_);
	itemId_ = new QLabel(this);
	form_->addRow(tr("Scene item ID"), itemId_);
	index_ = new QSpinBox(this);
	index_->setToolTip(tr("Stacking order in the scene: 0 is the bottom, the highest number is the top"));
	form_->addRow(tr("Scene item index"), index_);
	size_ = new QLabel(this);
	form_->addRow(tr("Source size"), size_);
	sceneCount_ = new QLabel(this);
	form_->addRow(tr("Used in scenes"), sceneCount_);
	// Not "Screenshot": OBS translates its own string keys everywhere, which turned this into "Screenshot Output".
	screenshot_ = new QPushButton(tr("Take screenshot"), this);
	form_->addRow(tr("Image"), screenshot_);

	auto makeTransitionRow = [this](bool show, QComboBox *&type, QSpinBox *&ms, QToolButton *&props) {
		auto *row = new QWidget(this);
		auto *h = new QHBoxLayout(row);
		h->setContentsMargins(0, 0, 0, 0);
		type = makeTransitionBox(show);
		ms = new QSpinBox(row);
		ms->setRange(0, 60000);
		ms->setSingleStep(50);
		ms->setSuffix(" ms");
		props = new QToolButton(row);
		props->setText(tr("Settings"));
		props->setToolTip(tr("Edit this transition's own settings"));
		h->addWidget(type, 1);
		h->addWidget(ms);
		h->addWidget(props);
		connect(props, &QToolButton::clicked, this, [this, show] { openTransitionSettings(show); });
		return row;
	};
	showRow_ = makeTransitionRow(true, showType_, showMs_, showProps_);
	form_->addRow(tr("Show transition"), showRow_);
	hideRow_ = makeTransitionRow(false, hideType_, hideMs_, hideProps_);
	form_->addRow(tr("Hide transition"), hideRow_);

	blendMode_ = new QComboBox(this);
	for (const char *n : {"Normal", "Additive", "Subtract", "Screen", "Multiply", "Lighten", "Darken"})
		blendMode_->addItem(tr(n));
	form_->addRow(tr("Blend mode"), blendMode_);
	blendMethod_ = new QComboBox(this);
	blendMethod_->addItem(tr("Default"));
	blendMethod_->addItem(tr("sRGB off"));
	blendMethod_->setToolTip(tr("Not available through obs-websocket, so there is no command to copy."));
	form_->addRow(tr("Blend method"), blendMethod_);

	scaleFilter_ = new QComboBox(this);
	for (const char *n : {"Disabled", "Point", "Bicubic", "Bilinear", "Lanczos", "Area"})
		scaleFilter_->addItem(tr(n));
	scaleFilter_->setToolTip(tr("Not available through obs-websocket, so there is no command to copy."));
	form_->addRow(tr("Scale filtering"), scaleFilter_);

	deinterlace_ = new QComboBox(this);
	for (const char *n : {"Disable", "Discard", "Retro", "Blend", "Blend 2x", "Linear", "Linear 2x", "Yadif", "Yadif 2x"})
		deinterlace_->addItem(tr(n));
	deinterlace_->setToolTip(tr("Not available through obs-websocket, so there is no command to copy."));
	form_->addRow(tr("Deinterlacing"), deinterlace_);
	fieldOrder_ = new QComboBox(this);
	fieldOrder_->addItem(tr("Top field first"));
	fieldOrder_->addItem(tr("Bottom field first"));
	form_->addRow(tr("Field order"), fieldOrder_);

	status_ = new QLabel(this);
	status_->setWordWrap(true);
	status_->setVisible(false);
	root->addWidget(status_);
	root->addStretch(1);

	// Edits -> OBS --------------------------------------------------------------------------
	connect(name_, &QLineEdit::editingFinished, this, [this] {
		obs_source_t *src = shown();
		if (updating_ || !src)
			return;
		const QString wanted = name_->text().trimmed();
		const std::string current = nameOf(src);
		if (wanted.isEmpty() || wanted.toStdString() == current) {
			name_->setText(QString::fromStdString(current));
			return;
		}
		// OBS requires unique source names.
		if (obs::SourceRef clash = obs::sourceByName(wanted.toUtf8().constData()); clash && clash.get() != src) {
			say(tr("A source named \"%1\" already exists.").arg(wanted));
			name_->setText(QString::fromStdString(current));
			return;
		}
		obs_source_set_name(src, wanted.toUtf8().constData());
	});
	connect(index_, &QSpinBox::valueChanged, this, [this](int v) {
		if (!updating_ && item_)
			obs_sceneitem_set_order_position(item_.get(), v);
	});
	connect(screenshot_, &QPushButton::clicked, this, [this] {
		if (obs_source_t *src = shown()) {
			obs_frontend_take_source_screenshot(src); // saved to OBS's screenshot folder
			say(tr("Screenshot of \"%1\" requested. OBS saves it to its screenshot folder.")
				    .arg(QString::fromStdString(nameOf(src))));
		}
	});
	for (bool show : {true, false}) {
		QComboBox *type = show ? showType_ : hideType_;
		QSpinBox *ms = show ? showMs_ : hideMs_;
		connect(type, &QComboBox::activated, this, [this, show, type](int i) {
			if (!updating_ && i >= 0)
				applyTransition(show, type->itemData(i).toString());
		});
		connect(ms, &QSpinBox::valueChanged, this, [this, show](int v) {
			if (!updating_ && item_)
				obs_sceneitem_set_transition_duration(item_.get(), show, uint32_t(v));
		});
	}
	connect(blendMode_, &QComboBox::activated, this, [this](int i) {
		if (!updating_ && item_ && i >= 0)
			obs_sceneitem_set_blending_mode(item_.get(), static_cast<obs_blending_type>(i));
	});
	connect(blendMethod_, &QComboBox::activated, this, [this](int i) {
		if (!updating_ && item_ && i >= 0)
			obs_sceneitem_set_blending_method(item_.get(), static_cast<obs_blending_method>(i));
	});
	connect(scaleFilter_, &QComboBox::activated, this, [this](int i) {
		if (!updating_ && item_ && i >= 0)
			obs_sceneitem_set_scale_filter(item_.get(), static_cast<obs_scale_type>(i));
	});
	connect(deinterlace_, &QComboBox::activated, this, [this](int i) {
		obs_source_t *src = shown();
		if (!updating_ && src && i >= 0)
			obs_source_set_deinterlace_mode(src, static_cast<obs_deinterlace_mode>(i));
	});
	connect(fieldOrder_, &QComboBox::activated, this, [this](int i) {
		obs_source_t *src = shown();
		if (!updating_ && src && i >= 0)
			obs_source_set_deinterlace_field_order(src, static_cast<obs_deinterlace_field_order>(i));
	});

	// Command copier: only where obs-websocket has a request -----------------------------------
	QPointer<SourcePanel> self(this);
	attachCommandMenu(name_, [self]() -> std::optional<CommandSet> {
		if (!self || !self->shown())
			return std::nullopt;
		obs_source_t *src = self->shown();
		const std::string current = nameOf(src);
		const std::string wanted = self->name_->text().toStdString();
		const ObsRequest set = obs_source_get_type(src) == OBS_SOURCE_TYPE_SCENE ? reqSetSceneName(current, wanted)
											  : reqSetInputName(current, wanted);
		CommandSet c; // there is no "get name" request, so only the Set forms are offered
		c.streamerBot = [set] { return sbFrom(set); };
		c.wsSet = [set] { return wsFrom(set); };
		return c;
	});
	attachCommandMenu(index_, [self]() -> std::optional<CommandSet> {
		if (!self || !self->item_)
			return std::nullopt;
		const SceneItemTarget t = self->target();
		return makeSet(reqSetSceneItemIndex(t, self->index_->value()), reqGetSceneItemIndex(t));
	});
	attachCommandMenu(blendMode_, [self]() -> std::optional<CommandSet> {
		if (!self || !self->item_)
			return std::nullopt;
		const SceneItemTarget t = self->target();
		CommandSet c = makeSet(
			reqSetSceneItemBlendMode(t, static_cast<BlendMode>(self->blendMode_->currentIndex())),
			reqGetSceneItemBlendMode(t));
		c.reset = [self] {
			if (self && self->item_)
				obs_sceneitem_set_blending_mode(self->item_.get(), OBS_BLEND_NORMAL);
		};
		return c;
	});
	attachCommandMenu(screenshot_, [self]() -> std::optional<CommandSet> {
		if (!self || !self->shown())
			return std::nullopt;
		const std::string name = nameOf(self->shown());
		// Save needs a path on the machine running OBS; this one is a placeholder to edit.
		return makeSet(reqSaveSourceScreenshot(name, "png", "C:/snapshots/" + name + ".png"),
			       reqGetSourceScreenshot(name, "png"));
	});

	// Defaults for the fields that have no command (reset only).
	auto resetOnly = [this, self](QWidget *w, std::function<void()> action) {
		attachCommandMenu(w, [self, action = std::move(action)]() -> std::optional<CommandSet> {
			if (!self || !self->shown())
				return std::nullopt;
			CommandSet c;
			c.reset = action;
			return c;
		});
	};
	resetOnly(blendMethod_, [self] {
		if (self && self->item_)
			obs_sceneitem_set_blending_method(self->item_.get(), OBS_BLEND_METHOD_DEFAULT);
	});
	resetOnly(scaleFilter_, [self] { // libobs creates items with scaling disabled
		if (self && self->item_)
			obs_sceneitem_set_scale_filter(self->item_.get(), OBS_SCALE_DISABLE);
	});
	resetOnly(deinterlace_, [self] {
		if (self && self->shown())
			obs_source_set_deinterlace_mode(self->shown(), OBS_DEINTERLACE_MODE_DISABLE);
	});
	resetOnly(fieldOrder_, [self] {
		if (self && self->shown())
			obs_source_set_deinterlace_field_order(self->shown(), OBS_DEINTERLACE_FIELD_ORDER_TOP);
	});
}

// ---------------------------------------------------------------------------
void SourcePanel::setItem(obs_sceneitem_t *item)
{
	item_ = obs::retain(item);
	source_.reset();
	refresh();
}

void SourcePanel::setSource(obs_source_t *source)
{
	item_.reset();
	source_ = obs::retain(source);
	refresh();
}

void SourcePanel::refresh()
{
	obs_source_t *src = shown();
	const bool hasSource = src != nullptr;
	const bool hasItem = static_cast<bool>(item_);
	hint_->setVisible(!hasSource);

	// Rows that only make sense with a scene item.
	for (QWidget *w : {static_cast<QWidget *>(itemId_), static_cast<QWidget *>(index_),
			   static_cast<QWidget *>(showRow_), static_cast<QWidget *>(hideRow_),
			   static_cast<QWidget *>(blendMode_), static_cast<QWidget *>(blendMethod_),
			   static_cast<QWidget *>(scaleFilter_)})
		form_->setRowVisible(w, hasItem);
	// Deinterlacing applies to video-capture style (asynchronous video) sources only.
	const bool async = hasSource && (obs_source_get_output_flags(src) & OBS_SOURCE_ASYNC_VIDEO) == OBS_SOURCE_ASYNC_VIDEO;
	form_->setRowVisible(deinterlace_, async);
	form_->setRowVisible(fieldOrder_, async);
	for (QWidget *w : {static_cast<QWidget *>(name_), static_cast<QWidget *>(type_), static_cast<QWidget *>(size_),
			   static_cast<QWidget *>(sceneCount_), static_cast<QWidget *>(screenshot_)})
		form_->setRowVisible(w, hasSource);
	if (!hasSource)
		return;

	updating_ = true;
	{
		QSignalBlocker b1(name_), b2(index_), b3(showType_), b4(showMs_), b5(hideType_), b6(hideMs_),
			b7(blendMode_), b8(blendMethod_), b9(scaleFilter_), b10(deinterlace_), b11(fieldOrder_);
		if (!name_->hasFocus())
			name_->setText(QString::fromStdString(nameOf(src)));
		type_->setText(QString::fromUtf8(obs_source_get_display_name(obs_source_get_id(src))));
		const uint32_t w = obs_source_get_width(src), h = obs_source_get_height(src);
		size_->setText(w ? QString("%1 x %2").arg(w).arg(h) : tr("n/a"));
		sceneCount_->setText(QString::number(scenesUsing(src)));
		screenshot_->setEnabled(obs_source_get_output_flags(src) & OBS_SOURCE_VIDEO);

		if (hasItem) {
			obs_sceneitem_t *it = item_.get();
			itemId_->setText(QString::number(obs_sceneitem_get_id(it)));
			CountItems counter;
			if (obs_scene_t *sc = obs_sceneitem_get_scene(it))
				obs_scene_enum_items(sc, &CountItems::cb, &counter);
			index_->setRange(0, counter.count > 0 ? counter.count - 1 : 0);
			if (!index_->hasFocus())
				index_->setValue(obs_sceneitem_get_order_position(it));

			auto selectTransition = [&](bool show, QComboBox *type, QSpinBox *ms, QToolButton *props) {
				obs_source_t *t = obs_sceneitem_get_transition(it, show); // borrowed
				const QString id = t ? QString::fromUtf8(obs_source_get_id(t)) : QString();
				const int at = type->findData(id);
				type->setCurrentIndex(at >= 0 ? at : 0);
				if (!ms->hasFocus())
					ms->setValue(int(obs_sceneitem_get_transition_duration(it, show)));
				ms->setEnabled(t != nullptr);
				props->setEnabled(t != nullptr);
			};
			selectTransition(true, showType_, showMs_, showProps_);
			selectTransition(false, hideType_, hideMs_, hideProps_);

			blendMode_->setCurrentIndex(int(obs_sceneitem_get_blending_mode(it)));
			blendMethod_->setCurrentIndex(int(obs_sceneitem_get_blending_method(it)));
			scaleFilter_->setCurrentIndex(int(obs_sceneitem_get_scale_filter(it)));
		}
		if (async) {
			deinterlace_->setCurrentIndex(int(obs_source_get_deinterlace_mode(src)));
			fieldOrder_->setCurrentIndex(int(obs_source_get_deinterlace_field_order(src)));
			fieldOrder_->setEnabled(obs_source_get_deinterlace_mode(src) != OBS_DEINTERLACE_MODE_DISABLE);
		}
	}
	updating_ = false;
}

} // namespace se
