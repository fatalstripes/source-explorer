#include "ui/panels/transform-panel.hpp"

#include "core/scene-item-ops.hpp"
#include "ui/widgets/alignment-grid.hpp"
#include "ui/widgets/command-menu.hpp"

#include <obs.h>

#include <cmath>
#include <optional>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace se {

// Our BoundsType enum is index-compatible with libobs; enforce it.
static_assert(static_cast<int>(BoundsType::None) == OBS_BOUNDS_NONE);
static_assert(static_cast<int>(BoundsType::Stretch) == OBS_BOUNDS_STRETCH);
static_assert(static_cast<int>(BoundsType::ScaleInner) == OBS_BOUNDS_SCALE_INNER);
static_assert(static_cast<int>(BoundsType::ScaleOuter) == OBS_BOUNDS_SCALE_OUTER);
static_assert(static_cast<int>(BoundsType::ScaleToWidth) == OBS_BOUNDS_SCALE_TO_WIDTH);
static_assert(static_cast<int>(BoundsType::ScaleToHeight) == OBS_BOUNDS_SCALE_TO_HEIGHT);
static_assert(static_cast<int>(BoundsType::MaxOnly) == OBS_BOUNDS_MAX_ONLY);

namespace {


} // namespace

TransformPanel::TransformPanel(QWidget *parent) : QWidget(parent)
{
	buildUi();
	setEnabled(false);
}

TransformPanel::~TransformPanel()
{
	disconnectSignals();
}

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------
TransformPanel::Field &TransformPanel::addField(QFormLayout *form, const QString &label, double min, double max,
						double step, int decimals, double def)
{
	Field &f = fields_.emplace_back();
	f.box = new QDoubleSpinBox(this);
	f.box->setRange(min, max);
	f.box->setSingleStep(step);
	f.box->setDecimals(decimals);
	f.defaultValue = def;
	form->addRow(label, f.box);
	return f;
}

void TransformPanel::attachMenu(QWidget *w, std::function<void(TransformPatch &)> fill, std::function<void()> reset)
{
	QPointer<TransformPanel> self(this);
	attachCommandMenu(w, [self, fill = std::move(fill), reset = std::move(reset)]() -> std::optional<CommandSet> {
		if (!self || !self->item_)
			return std::nullopt;
		const SceneItemTarget t = self->target();
		TransformPatch p;
		fill(p);
		CommandSet c;
		c.streamerBot = [t, p] { return sbSetSceneItemTransform(t, p); };
		c.wsSet = [t, p] { return wsSetSceneItemTransform(t, p); };
		c.wsGet = [t] { return wsGetSceneItemTransform(t); };
		c.reset = reset;
		c.resets.emplace_back("Reset transform", [self] {
			if (self)
				self->applyPatch(ops::defaultPatch());
		});
		return c;
	});
}

ItemGeometry TransformPanel::currentGeometry() const
{
	return item_ ? ops::geometry(item_.get()) : ItemGeometry{};
}

void TransformPanel::attachLabelMenu(QWidget *label, std::vector<GroupFill> groups)
{
	if (!label)
		return;
	QPointer<TransformPanel> self(this);
	attachCommandMenu(label, [self, groups = std::move(groups)]() -> std::optional<CommandSet> {
		if (!self || !self->item_)
			return std::nullopt;
		const SceneItemTarget t = self->target();
		const ObsRequest get = reqGetSceneItemTransform(t);

		CommandSet c;
		for (const auto &[text, fill] : groups) {
			TransformPatch p;
			fill(p); // snapshot now: the menu may outlive the current values
			c.choices.push_back(makeChoice(text, reqSetSceneItemTransform(t, p), get));
		}
		c.choices.push_back(makeChoice("Full transform", reqSetSceneItemTransform(t, ops::currentPatch(self->item_.get())), get));
		c.resets.emplace_back("Reset transform", [self] {
			if (self)
				self->applyPatch(ops::defaultPatch());
		});
		return c;
	});
}

void TransformPanel::buildUi()
{
	auto *root = new QVBoxLayout(this);

	// Quick actions ---------------------------------------------------------
	auto *quick = new QGridLayout;
	auto plan = [this](auto &&fn) { return [this, fn] { return item_ ? fn(item_.get()) : TransformPatch{}; }; };
	addQuickAction(quick, 0, 0, tr("Center"), plan([](auto *i) { return ops::planCenter(i); }));
	addQuickAction(quick, 0, 1, tr("Center vertically"), plan([](auto *i) { return ops::planCenterVertically(i); }));
	addQuickAction(quick, 0, 2, tr("Center horizontally"), plan([](auto *i) { return ops::planCenterHorizontally(i); }));
	addQuickAction(quick, 1, 0, tr("Fit to screen"), plan([](auto *i) { return ops::planFit(i, FitMode::Contain); }));
	addQuickAction(quick, 1, 1, tr("Fill screen"), plan([](auto *i) { return ops::planFit(i, FitMode::Cover); }));
	addQuickAction(quick, 1, 2, tr("Stretch to screen"), plan([](auto *i) { return ops::planFit(i, FitMode::Stretch); }));
	addQuickAction(quick, 2, 0, tr("Rotate 90 CCW"), plan([](auto *i) { return ops::planRotate(i, -90.0); }));
	addQuickAction(quick, 2, 1, tr("Rotate 90 CW"), plan([](auto *i) { return ops::planRotate(i, 90.0); }));
	addQuickAction(quick, 2, 2, tr("Rotate 180"), plan([](auto *i) { return ops::planRotate(i, 180.0); }));
	addQuickAction(quick, 3, 0, tr("Flip horizontal"), plan([](auto *i) { return ops::planFlip(i, true); }));
	addQuickAction(quick, 3, 1, tr("Flip vertical"), plan([](auto *i) { return ops::planFlip(i, false); }));

	// Copy / paste: the copy is shared between sources and kept for the session.
	auto *copyButton = new QPushButton(tr("Copy transform"), this);
	connect(copyButton, &QPushButton::clicked, this, [this] {
		if (!item_)
			return;
		ops::copyTransform(item_.get());
		pasteButton_->setEnabled(true);
	});
	quick->addWidget(copyButton, 4, 0);
	addQuickAction(quick, 4, 1, tr("Paste transform"), plan([](auto *i) { return ops::planPaste(i); }));
	pasteButton_ = qobject_cast<QPushButton *>(quick->itemAtPosition(4, 1)->widget());
	pasteButton_->setEnabled(ops::hasCopiedTransform());
	pasteButton_->setToolTip(tr("Paste position, size, rotation, alignment and crop copied from another source"));
	root->addLayout(quick);

	// Position / scale / rotation ------------------------------------------
	auto *tbox = new QGroupBox(tr("Transform"), this);
	auto *form = new QFormLayout(tbox);

	auto &px = addField(form, tr("Position X"), -100000, 100000, 1, 2, 0);
	px.read = [](auto &t, auto &) { return double(t.pos.x); };
	px.write = [](auto &t, auto &, double v) { t.pos.x = float(v); };
	px.patch = [](TransformPatch &p, double v) { p.positionX = v; };

	auto &py = addField(form, tr("Position Y"), -100000, 100000, 1, 2, 0);
	py.read = [](auto &t, auto &) { return double(t.pos.y); };
	py.write = [](auto &t, auto &, double v) { t.pos.y = float(v); };
	py.patch = [](TransformPatch &p, double v) { p.positionY = v; };

	// Size in pixels, as OBS's Transform dialog shows it. obs-websocket cannot set width/height, so
	// edits (and copied commands) are expressed as the scale that produces that size.
	auto sizeSign = [](float scale) { return scale < 0.0f ? -1.0 : 1.0; };
	auto &sw = addField(form, tr("Width"), 0, 1000000, 1, 2, 0);
	sw.read = [this](auto &, auto &) { return item_ ? sizeOf(ops::geometry(item_.get())).w : 0.0; };
	sw.write = [this, sizeSign](auto &t, auto &, double v) {
		const double w = ops::geometry(item_.get()).croppedW();
		if (w > 0.0)
			t.scale.x = float(sizeSign(t.scale.x) * std::abs(v) / w);
	};
	sw.patch = [this](TransformPatch &p, double v) {
		const ItemGeometry g = currentGeometry();
		p.scaleX = sizeToScalePatch(g, v, sizeOf(g).h).scaleX;
	};
	sw.reset = [this] { // native (uncropped-by-scale) size: scale 1, flip kept
		const ItemGeometry g = currentGeometry();
		TransformPatch p;
		p.scaleX = g.scaleX < 0 ? -1.0 : 1.0;
		applyPatch(p);
	};
	sizeWidthBox_ = sw.box;

	auto &sh = addField(form, tr("Height"), 0, 1000000, 1, 2, 0);
	sh.read = [this](auto &, auto &) { return item_ ? sizeOf(ops::geometry(item_.get())).h : 0.0; };
	sh.write = [this, sizeSign](auto &t, auto &, double v) {
		const double h = ops::geometry(item_.get()).croppedH();
		if (h > 0.0)
			t.scale.y = float(sizeSign(t.scale.y) * std::abs(v) / h);
	};
	sh.patch = [this](TransformPatch &p, double v) {
		const ItemGeometry g = currentGeometry();
		p.scaleY = sizeToScalePatch(g, sizeOf(g).w, v).scaleY;
	};
	sh.reset = [this] {
		const ItemGeometry g = currentGeometry();
		TransformPatch p;
		p.scaleY = g.scaleY < 0 ? -1.0 : 1.0;
		applyPatch(p);
	};
	sizeHeightBox_ = sh.box;

	auto &rot = addField(form, tr("Rotation"), -360, 360, 1, 2, 0);
	rot.read = [](auto &t, auto &) { return double(t.rot); };
	rot.write = [](auto &t, auto &, double v) { t.rot = float(v); };
	rot.patch = [](TransformPatch &p, double v) { p.rotation = v; };
	root->addWidget(tbox);

	// Alignment ---------------------------------------------------------------
	auto *abox = new QGroupBox(tr("Alignment"), this);
	auto *aform = new QFormLayout(abox);
	alignGrid_ = new AlignmentGrid(this);
	aform->addRow(tr("Anchor"), alignGrid_);
	root->addWidget(abox);

	// Bounds ------------------------------------------------------------------
	auto *bbox = new QGroupBox(tr("Bounding box"), this);
	auto *bform = new QFormLayout(bbox);
	boundsType_ = new QComboBox(this);
	const char *boundsNames[] = {"No bounds",     "Stretch to bounds", "Scale to inner bounds",
				     "Scale to outer bounds", "Scale to width",   "Scale to height",
				     "Maximum size"};
	for (const char *n : boundsNames)
		boundsType_->addItem(tr(n));
	bform->addRow(tr("Type"), boundsType_);

	auto &bw = addField(bform, tr("Width"), 1, 100000, 1, 2, 1920);
	bw.read = [](auto &t, auto &) { return double(t.bounds.x); };
	bw.write = [](auto &t, auto &, double v) { t.bounds.x = float(v); };
	bw.patch = [](TransformPatch &p, double v) { p.boundsWidth = v; };

	auto &bh = addField(bform, tr("Height"), 1, 100000, 1, 2, 1080);
	bh.read = [](auto &t, auto &) { return double(t.bounds.y); };
	bh.write = [](auto &t, auto &, double v) { t.bounds.y = float(v); };
	bh.patch = [](TransformPatch &p, double v) { p.boundsHeight = v; };
	boundsWidthBox_ = bw.box;
	boundsHeightBox_ = bh.box;

	boundsAlignGrid_ = new AlignmentGrid(this);
	bform->addRow(tr("Bounds alignment"), boundsAlignGrid_);
	root->addWidget(bbox);

	// Crop --------------------------------------------------------------------
	auto *cbox = new QGroupBox(tr("Crop"), this);
	auto *cform = new QFormLayout(cbox);
	struct CropDef {
		const char *label;
		int obs_sceneitem_crop::*member;
		std::optional<int> TransformPatch::*patchMember;
	};
	const CropDef crops[] = {{"Left", &obs_sceneitem_crop::left, &TransformPatch::cropLeft},
				 {"Right", &obs_sceneitem_crop::right, &TransformPatch::cropRight},
				 {"Top", &obs_sceneitem_crop::top, &TransformPatch::cropTop},
				 {"Bottom", &obs_sceneitem_crop::bottom, &TransformPatch::cropBottom}};
	for (const auto &c : crops) {
		auto &f = addField(cform, tr(c.label), 0, 100000, 1, 0, 0);
		f.read = [m = c.member](auto &, auto &cr) { return double(cr.*m); };
		f.write = [m = c.member](auto &, auto &cr, double v) { cr.*m = int(v); };
		f.patch = [pm = c.patchMember](TransformPatch &p, double v) { p.*pm = int(v); };
	}
	root->addWidget(cbox);

	auto *resetButton = new QPushButton(tr("Reset transform"), this);
	connect(resetButton, &QPushButton::clicked, this, [this] {
		if (item_)
			applyPatch(ops::defaultPatch());
	});
	root->addWidget(resetButton);
	attachCommandMenu(resetButton, [this]() -> std::optional<CommandSet> {
		if (!item_)
			return std::nullopt;
		const SceneItemTarget t = target();
		return makeSet(reqSetSceneItemTransform(t, ops::defaultPatch()), reqGetSceneItemTransform(t));
	});
	root->addStretch(1);

	// Wire fields: edit -> OBS; right-click -> command copier ------------------
	for (Field &f : fields_) {
		connect(f.box, &QDoubleSpinBox::valueChanged, this, [this, &f](double v) {
			if (updating_ || !item_)
				return;
			obs_transform_info ti;
			obs_sceneitem_crop crop;
			if (!readItem(ti, crop))
				return;
			f.write(ti, crop, v);
			obs_sceneitem_set_info2(item_.get(), &ti);
			obs_sceneitem_set_crop(item_.get(), &crop);
		});
		attachMenu(
			f.box, [&f](TransformPatch &p) { f.patch(p, f.box->value()); },
			[this, &f] {
				// Size has no fixed default (it depends on the source), so it supplies its own reset.
				if (f.reset)
					f.reset();
				else
					f.box->setValue(f.defaultValue);
			});
	}

	// Alignment grids and bounds type edit OBS directly.
	// Position is the anchor's location, so a new anchor also needs a new position; otherwise the
	// item jumps. Recompute it so the item stays exactly where it is and Position follows the anchor.
	connect(alignGrid_, &AlignmentGrid::alignmentChanged, this, [this](int a) {
		if (updating_ || !item_)
			return;
		applyPatch(ops::planReanchor(item_.get(), a));
	});
	connect(boundsAlignGrid_, &AlignmentGrid::alignmentChanged, this, [this](int a) {
		if (updating_ || !item_)
			return;
		obs_transform_info ti;
		obs_sceneitem_crop crop;
		if (!readItem(ti, crop))
			return;
		ti.bounds_alignment = static_cast<uint32_t>(a);
		obs_sceneitem_set_info2(item_.get(), &ti);
	});
	connect(boundsType_, &QComboBox::currentIndexChanged, this, [this](int idx) {
		if (updating_ || !item_)
			return;
		obs_transform_info ti;
		obs_sceneitem_crop crop;
		if (!readItem(ti, crop))
			return;
		ti.bounds_type = static_cast<obs_bounds_type>(idx);
		obs_sceneitem_set_info2(item_.get(), &ti);
	});

	// The copied command is what a click sends: the anchor AND the position that keeps the item put.
	attachMenu(
		alignGrid_,
		[this](TransformPatch &p) {
			if (item_)
				p = ops::planReanchor(item_.get(), alignGrid_->alignment());
		},
		[this] { applyPatch(ops::planReanchor(item_.get(), align::TopLeft)); });
	attachMenu(
		boundsAlignGrid_, [this](TransformPatch &p) { p.boundsAlignment = boundsAlignGrid_->alignment(); },
		[this] { applyPatch(TransformPatch{.boundsAlignment = align::Center}); });
	attachMenu(
		boundsType_, [this](TransformPatch &p) { p.boundsType = static_cast<BoundsType>(boundsType_->currentIndex()); },
		[this] { applyPatch(TransformPatch{.boundsType = BoundsType::None}); });
	// Label right-click: the multi-value commands each label belongs to (plus the full transform).
	const GroupFill positionGroup{"Position (X and Y)", [&px, &py](TransformPatch &p) {
					      p.positionX = px.box->value();
					      p.positionY = py.box->value();
				      }};
	const GroupFill sizeGroup{"Size (width and height)", [this, &sw, &sh](TransformPatch &p) {
					  const TransformPatch s =
						  sizeToScalePatch(currentGeometry(), sw.box->value(), sh.box->value());
					  p.scaleX = s.scaleX;
					  p.scaleY = s.scaleY;
				  }};
	const GroupFill rotationGroup{"Rotation", [&rot](TransformPatch &p) { p.rotation = rot.box->value(); }};
	const GroupFill anchorGroup{"Alignment (anchor and position)", [this](TransformPatch &p) {
					    p = ops::planReanchor(item_.get(), alignGrid_->alignment());
				    }};
	const GroupFill boundsGroup{"Bounding box (type, size, alignment)", [this, &bw, &bh](TransformPatch &p) {
					    p.boundsType = static_cast<BoundsType>(boundsType_->currentIndex());
					    p.boundsWidth = bw.box->value();
					    p.boundsHeight = bh.box->value();
					    p.boundsAlignment = boundsAlignGrid_->alignment();
				    }};
	std::vector<Field *> cropFields;
	for (Field &f : fields_)
		if (f.box->suffix().isEmpty() && f.defaultValue == 0.0 && f.box->decimals() == 0)
			cropFields.push_back(&f); // the four crop boxes are the only 0-decimal fields
	const GroupFill cropGroup{"Crop (all four sides)", [cropFields](TransformPatch &p) {
					  for (Field *f : cropFields)
						  f->patch(p, f->box->value());
				  }};

	attachLabelMenu(form->labelForField(px.box), {positionGroup});
	attachLabelMenu(form->labelForField(py.box), {positionGroup});
	attachLabelMenu(form->labelForField(sw.box), {sizeGroup});
	attachLabelMenu(form->labelForField(sh.box), {sizeGroup});
	attachLabelMenu(form->labelForField(rot.box), {rotationGroup});
	attachLabelMenu(aform->labelForField(alignGrid_), {anchorGroup});
	attachLabelMenu(bform->labelForField(boundsType_), {boundsGroup});
	attachLabelMenu(bform->labelForField(bw.box), {boundsGroup});
	attachLabelMenu(bform->labelForField(bh.box), {boundsGroup});
	attachLabelMenu(bform->labelForField(boundsAlignGrid_), {boundsGroup});
	for (Field *f : cropFields)
		attachLabelMenu(cform->labelForField(f->box), {cropGroup});
}

// ---------------------------------------------------------------------------
// Item binding
// ---------------------------------------------------------------------------
void TransformPanel::setItem(obs_sceneitem_t *item)
{
	disconnectSignals();
	item_ = obs::retain(item);
	sceneSource_.reset();
	if (item_) {
		if (obs_scene_t *scene = obs_sceneitem_get_scene(item_.get()))
			sceneSource_ = obs::retain(obs_scene_get_source(scene));
	}
	setEnabled(static_cast<bool>(item_));
	connectSignals();
	reload();
}

SceneItemTarget TransformPanel::target() const
{
	SceneItemTarget t;
	if (sceneSource_)
		t.sceneName = obs_source_get_name(sceneSource_.get());
	if (item_)
		t.sceneItemId = obs_sceneitem_get_id(item_.get());
	return t;
}

bool TransformPanel::readItem(obs_transform_info &ti, obs_sceneitem_crop &crop) const
{
	if (!item_)
		return false;
	obs_sceneitem_get_info2(item_.get(), &ti);
	obs_sceneitem_get_crop(item_.get(), &crop);
	return true;
}

void TransformPanel::reload()
{
	obs_transform_info ti;
	obs_sceneitem_crop crop;
	if (!readItem(ti, crop))
		return;

	updating_ = true;
	for (Field &f : fields_) {
		QSignalBlocker block(f.box);
		f.box->setValue(f.read(ti, crop));
	}
	{
		QSignalBlocker b(boundsType_);
		boundsType_->setCurrentIndex(static_cast<int>(ti.bounds_type));
	}
	alignGrid_->setAlignment(static_cast<int>(ti.alignment));
	boundsAlignGrid_->setAlignment(static_cast<int>(ti.bounds_alignment));

	pasteButton_->setEnabled(ops::hasCopiedTransform());

	// Bounds size and alignment do nothing without a bounding box; grey them out.
	const bool hasBounds = ti.bounds_type != OBS_BOUNDS_NONE;
	boundsWidthBox_->setEnabled(hasBounds);
	boundsHeightBox_->setEnabled(hasBounds);
	boundsAlignGrid_->setEnabled(hasBounds);
	// With a bounding box the box drives the size; OBS shows Size read-only then too.
	sizeWidthBox_->setEnabled(!hasBounds);
	sizeHeightBox_->setEnabled(!hasBounds);
	updating_ = false;
}

// OBS emits "item_transform" on the scene's signal handler, possibly on a
// non-GUI thread. Never touch widgets there: queue a reload onto the GUI thread.
// libobs serializes disconnect against in-flight callbacks, so `data` stays valid
// for the duration of this call.
static void onItemTransform(void *data, calldata_t *cd)
{
	QPointer<TransformPanel> guard = *static_cast<QPointer<TransformPanel> *>(data);
	if (!guard)
		return;
	auto *changed = static_cast<obs_sceneitem_t *>(calldata_ptr(cd, "item"));
	QMetaObject::invokeMethod(
		guard.data(),
		[guard, changed] {
			// Pointer is only compared, never dereferenced.
			if (guard && guard->item() == changed)
				QMetaObject::invokeMethod(guard.data(), "reload");
		},
		Qt::QueuedConnection);
}

void TransformPanel::connectSignals()
{
	if (!sceneSource_)
		return;
	if (signal_handler_t *sh = obs_source_get_signal_handler(sceneSource_.get())) {
		slot_ = new QPointer<TransformPanel>(this);
		signal_handler_connect(sh, "item_transform", &onItemTransform, slot_);
		signalsConnected_ = true;
	}
}

void TransformPanel::disconnectSignals()
{
	if (!signalsConnected_)
		return;
	if (sceneSource_) {
		if (signal_handler_t *sh = obs_source_get_signal_handler(sceneSource_.get()))
			signal_handler_disconnect(sh, "item_transform", &onItemTransform, slot_);
	}
	delete slot_;
	slot_ = nullptr;
	signalsConnected_ = false;
}

// ---------------------------------------------------------------------------
// Patch application + quick actions
// ---------------------------------------------------------------------------
void TransformPanel::applyPatch(const TransformPatch &p)
{
	ops::apply(item_.get(), p);
	reload();
}

// An empty patch means "not applicable right now" (nothing copied yet, a source with no size, ...).
void TransformPanel::addQuickAction(QGridLayout *grid, int row, int col, const QString &text,
				    std::function<TransformPatch()> plan)
{
	auto *b = new QPushButton(text, this);
	grid->addWidget(b, row, col);
	connect(b, &QPushButton::clicked, this, [this, plan] {
		if (!item_)
			return;
		const TransformPatch p = plan();
		if (p.toJson().dump() != "{}")
			applyPatch(p);
	});

	QPointer<TransformPanel> self(this);
	attachCommandMenu(b, [self, plan]() -> std::optional<CommandSet> {
		if (!self || !self->item_)
			return std::nullopt;
		const TransformPatch p = plan(); // computed now: the copy matches what a click would do
		if (p.toJson().dump() == "{}")
			return std::nullopt;
		const SceneItemTarget t = self->target();
		return makeSet(reqSetSceneItemTransform(t, p), reqGetSceneItemTransform(t));
	});
}

} // namespace se
