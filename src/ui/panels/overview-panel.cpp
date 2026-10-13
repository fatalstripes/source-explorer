// SPDX-License-Identifier: GPL-2.0-or-later
#include "ui/panels/overview-panel.hpp"

#include "core/scene-item-ops.hpp"

#include <obs-frontend-api.h>
#include <obs.h>

#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace se {

namespace {

std::string nameOf(obs_source_t *s)
{
	const char *n = s ? obs_source_get_name(s) : nullptr;
	return n ? n : "";
}

// An empty patch means "not applicable right now" (no visible size, or flip on a bounds item).
bool hasEffect(const TransformPatch &p)
{
	return p.positionX || p.positionY || p.scaleX || p.scaleY || p.rotation || p.boundsWidth || p.boundsHeight;
}

QString dims(double w, double h)
{
	return QString("%1 x %2").arg(std::lround(w)).arg(std::lround(h));
}

} // namespace

OverviewPanel::OverviewPanel(QWidget *parent) : QWidget(parent)
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

OverviewPanel::~OverviewPanel() = default;

// ---------------------------------------------------------------------------
void OverviewPanel::buildUi()
{
	auto *root = new QVBoxLayout(this);

	auto *info = new QGroupBox(tr("Selection"), this);
	auto *form = new QFormLayout(info);
	auto row = [&](const QString &label) {
		auto *l = new QLabel(info);
		l->setTextInteractionFlags(Qt::TextSelectableByMouse);
		form->addRow(label, l);
		return l;
	};
	name_ = row(tr("Name"));
	type_ = row(tr("Type"));
	sourceSize_ = row(tr("Source size"));
	onCanvas_ = row(tr("On canvas"));
	position_ = row(tr("Position"));
	rotation_ = row(tr("Rotation"));
	canvasSize_ = row(tr("Canvas"));
	root->addWidget(info);

	auto *box = new QGroupBox(tr("Quick actions"), this);
	auto *grid = new QGridLayout(box);

	addAction(grid, 0, 0, tr("Center"), [this] { return ops::planCenter(item_.get()); }, QString());
	addAction(grid, 0, 1, tr("Fit to Canvas"), [this] { return ops::planFit(item_.get(), FitMode::Contain); },
		  QString());
	addAction(grid, 1, 0, tr("Rotate Left 90"), [this] { return ops::planRotate(item_.get(), -90.0); }, QString());
	addAction(grid, 1, 1, tr("Rotate Right 90"), [this] { return ops::planRotate(item_.get(), 90.0); }, QString());
	addAction(grid, 2, 0, tr("Flip Horizontal"), [this] { return ops::planFlip(item_.get(), true); },
		  tr("Flip is not available for items with a bounding box."));
	addAction(grid, 2, 1, tr("Flip Vertical"), [this] { return ops::planFlip(item_.get(), false); },
		  tr("Flip is not available for items with a bounding box."));

	snapshotButton_ = new QPushButton(tr("Snapshot"), box);
	grid->addWidget(snapshotButton_, 3, 0, 1, 2);
	connect(snapshotButton_, &QPushButton::clicked, this, &OverviewPanel::snapshot);
	attachActionMenu(snapshotButton_, [this]() -> std::optional<CommandSet> {
		obs_source_t *src = shownSource();
		if (!src)
			return std::nullopt;
		const std::string name = nameOf(src);
		// Save needs a path on the machine running OBS; this one is a placeholder to edit.
		const ObsRequest save = reqSaveSourceScreenshot(name, "png", "C:/snapshots/" + name + ".png");
		const ObsRequest get = reqGetSourceScreenshot(name, "png");
		CommandSet c;
		c.streamerBot = [save] { return sbFrom(save); };
		c.wsSet = [save] { return wsFrom(save); };
		c.wsGet = [get] { return wsFrom(get); };
		return c;
	});

	root->addWidget(box);

	hint_ = new QLabel(tr("Select a source in the OBS preview, or open Sources above to pick one."), this);
	hint_->setObjectName("seHint");
	hint_->setWordWrap(true);
	hint_->setForegroundRole(QPalette::PlaceholderText);
	root->addWidget(hint_);

	status_ = new QLabel(this);
	status_->setWordWrap(true);
	status_->setVisible(false);
	root->addWidget(status_);
	root->addStretch(1);
}

OverviewPanel::Action &OverviewPanel::addAction(QGridLayout *grid, int row, int col, const QString &text,
						std::function<TransformPatch()> plan, const QString &unavailable)
{
	Action &a = actions_.emplace_back();
	a.button = new QPushButton(text, this);
	a.plan = std::move(plan);
	a.unavailable = unavailable;
	grid->addWidget(a.button, row, col);

	Action *ap = &a; // deque: stable address
	connect(a.button, &QPushButton::clicked, this, [this, ap] { runAction(*ap); });

	attachActionMenu(a.button, [this, ap]() -> std::optional<CommandSet> {
		if (!item_)
			return std::nullopt;
		// The patch is computed now, so the copy matches what a click would do right now.
		const TransformPatch p = ap->plan();
		if (!hasEffect(p))
			return std::nullopt;
		const SceneItemTarget t = target();
		CommandSet c;
		c.streamerBot = [t, p] { return sbSetSceneItemTransform(t, p); };
		c.wsSet = [t, p] { return wsSetSceneItemTransform(t, p); };
		c.wsGet = [t] { return wsGetSceneItemTransform(t); };
		return c;
	});
	return a;
}

void OverviewPanel::attachActionMenu(QPushButton *b, std::function<std::optional<CommandSet>()> provider)
{
	attachCommandMenu(b, std::move(provider));
}

// ---------------------------------------------------------------------------
void OverviewPanel::setItem(obs_sceneitem_t *item)
{
	item_ = obs::retain(item);
	source_.reset();
	refresh();
}

void OverviewPanel::setSource(obs_source_t *source)
{
	item_.reset();
	source_ = obs::retain(source);
	refresh();
}

obs_source_t *OverviewPanel::shownSource() const
{
	return item_ ? obs_sceneitem_get_source(item_.get()) : source_.get();
}

SceneItemTarget OverviewPanel::target() const
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

void OverviewPanel::say(const QString &message)
{
	status_->setText(message);
	status_->setVisible(!message.isEmpty());
	QPointer<OverviewPanel> self(this);
	QTimer::singleShot(5000, this, [self, message] {
		if (self && self->status_->text() == message)
			self->status_->setVisible(false);
	});
}

// ---------------------------------------------------------------------------
void OverviewPanel::refresh()
{
	obs_source_t *src = shownSource();
	const Canvas canvas = ops::canvas();
	canvasSize_->setText(dims(canvas.w, canvas.h));

	hint_->setVisible(!src);
	if (!src) {
		for (auto *l : {name_, type_, sourceSize_, onCanvas_, position_, rotation_})
			l->clear();
		name_->setText(tr("Nothing selected"));
	} else {
		name_->setText(QString::fromStdString(nameOf(src)));
		type_->setText(QString::fromUtf8(obs_source_get_display_name(obs_source_get_id(src))));
		sourceSize_->setText(obs_source_get_width(src) ? dims(obs_source_get_width(src), obs_source_get_height(src))
							       : tr("n/a"));
		if (item_) {
			const ItemGeometry g = ops::geometry(item_.get());
			onCanvas_->setText(dims(std::abs(g.croppedW() * g.scaleX), std::abs(g.croppedH() * g.scaleY)));
			position_->setText(QString("%1, %2").arg(g.posX, 0, 'f', 1).arg(g.posY, 0, 'f', 1));
			rotation_->setText(QString("%1 deg").arg(g.rotation, 0, 'f', 1));
		} else {
			onCanvas_->setText(tr("n/a"));
			position_->setText(tr("n/a"));
			rotation_->setText(tr("n/a"));
		}
	}

	const bool hasItem = static_cast<bool>(item_);
	for (const Action &a : actions_)
		a.button->setEnabled(hasItem);
	// Snapshots need something that renders video (scenes and most inputs do).
	snapshotButton_->setEnabled(src && (obs_source_get_output_flags(src) & OBS_SOURCE_VIDEO));
}

void OverviewPanel::runAction(const Action &a)
{
	if (!item_)
		return;
	const TransformPatch p = a.plan();
	if (!hasEffect(p)) {
		say(a.unavailable.isEmpty() ? tr("Nothing to do: the item has no visible size yet.") : a.unavailable);
		return;
	}
	ops::apply(item_.get(), p);
	refresh();
}

void OverviewPanel::snapshot()
{
	obs_source_t *src = shownSource();
	if (!src)
		return;
	// Saved by OBS to its screenshot folder, the same as its own "Screenshot (Source)" hotkey.
	obs_frontend_take_source_screenshot(src);
	say(tr("Snapshot of \"%1\" requested. OBS saves it to its screenshot folder.")
		    .arg(QString::fromStdString(nameOf(src))));
}

} // namespace se
