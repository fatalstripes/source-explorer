#include "ui/panels/filters-panel.hpp"

#include "core/list-reorder.hpp"
#include "core/payload-generator.hpp"
#include "ui/widgets/command-menu.hpp"
#include "ui/widgets/reorder-list.hpp"

#include <obs.h>

#include <QCursor>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cstring>

namespace se {

// ---------------------------------------------------------------------------
// Quick-edit specs for well-known filters. Keys are the settings keys OBS stores
// (not the UI labels). Filters without a spec are still listed, toggled, reordered
// and copyable; their full settings belong to the Properties panel.
// NOTE: the blur kind ids/keys are best-effort - verify with GetSourceFilter.
// ---------------------------------------------------------------------------
namespace {

struct KeySpec {
	const char *key;
	const char *label;
	double min, max, step, def;
	int decimals;
};

struct EditorSpec {
	const char *kind;
	std::vector<KeySpec> keys;
};

const std::vector<EditorSpec> &editorSpecs()
{
	static const std::vector<EditorSpec> specs = {
		{kColorCorrectionKind,
		 {{"gamma", "Gamma", -3.0, 3.0, 0.05, 0.0, 2},
		  {"contrast", "Contrast", -4.0, 4.0, 0.05, 0.0, 2},
		  {"brightness", "Brightness", -1.0, 1.0, 0.01, 0.0, 2},
		  {"saturation", "Saturation", -1.0, 5.0, 0.05, 0.0, 2},
		  {"hue_shift", "Hue Shift", -180.0, 180.0, 1.0, 0.0, 1},
		  {"opacity", "Opacity", 0.0, 1.0, 0.01, 1.0, 2}}},
		{blurFilterKind(BlurFlavor::Composite), {{"radius", "Radius", 0.0, 100.0, 0.5, 1.0, 1}}},
		{blurFilterKind(BlurFlavor::StreamFX), {{"Filter.Blur.Size", "Size", 1.0, 64.0, 0.5, 1.0, 1}}},
	};
	return specs;
}

const EditorSpec *specForKind(const std::string &kind)
{
	for (const auto &s : editorSpecs())
		if (kind == s.kind)
			return &s;
	return nullptr;
}

std::string sourceName(obs_source_t *s)
{
	const char *n = s ? obs_source_get_name(s) : nullptr;
	return n ? n : "";
}

constexpr int kNameRole = Qt::UserRole;

} // namespace

struct FiltersPanel::EditorField {
	QDoubleSpinBox *box = nullptr;
	const KeySpec *spec = nullptr;
};

// ---------------------------------------------------------------------------
FiltersPanel::FiltersPanel(QWidget *parent) : QWidget(parent)
{
	auto *root = new QVBoxLayout(this);

	tabs_ = new QTabBar(this);
	tabs_->addTab(tr("Source"));
	tabs_->addTab(tr("Group"));
	tabs_->addTab(tr("Scene"));
	root->addWidget(tabs_);

	list_ = new ReorderListWidget(this);
	list_->setMinimumHeight(120);
	root->addWidget(list_, 1);

	auto *buttons = new QHBoxLayout;
	addButton_ = new QToolButton(this);
	addButton_->setText(tr("+ Add"));
	addButton_->setPopupMode(QToolButton::InstantPopup);
	auto *menu = new QMenu(addButton_);
	addButton_->setMenu(menu);
	connect(menu, &QMenu::aboutToShow, this, &FiltersPanel::rebuildAddMenu);
	removeButton_ = new QPushButton(tr("Remove"), this);
	buttons->addWidget(addButton_);
	resetAllButton_ = new QPushButton(tr("Reset all"), this);
	resetAllButton_->setToolTip(tr("Reset every filter in this list to its default settings"));
	buttons->addWidget(removeButton_);
	buttons->addWidget(resetAllButton_);
	buttons->addStretch(1);
	root->addLayout(buttons);

	editorHost_ = new QWidget(this);
	editorForm_ = new QFormLayout(editorHost_);
	editorForm_->setContentsMargins(0, 0, 0, 0);
	root->addWidget(editorHost_);

	connect(tabs_, &QTabBar::currentChanged, this, &FiltersPanel::onTabChanged);
	connect(list_, &ReorderListWidget::rowMoveRequested, this, &FiltersPanel::onRowMoveRequested);
	connect(list_, &QListWidget::currentRowChanged, this, [this] { onSelectionChanged(); });
	connect(removeButton_, &QPushButton::clicked, this, &FiltersPanel::removeSelected);
	connect(resetAllButton_, &QPushButton::clicked, this, &FiltersPanel::resetAllFilters);

	// Checkbox in the row == filter enabled.
	connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
		if (updating_)
			return;
		if (auto f = filterByName(item->data(kNameRole).toString().toStdString()))
			obs_source_set_enabled(f.get(), item->checkState() == Qt::Checked);
	});

	attachRowMenu();

	poll_ = new QTimer(this);
	poll_->setInterval(500);
	connect(poll_, &QTimer::timeout, this, &FiltersPanel::poll);
	poll_->start();

	setContext(nullptr, nullptr, nullptr);
}

FiltersPanel::~FiltersPanel() = default;

// ---------------------------------------------------------------------------
// Context / tabs
// ---------------------------------------------------------------------------
void FiltersPanel::setContext(obs_source_t *source, obs_source_t *group, obs_source_t *scene)
{
	sources_[SourceTab] = obs::retain(source);
	sources_[GroupTab] = obs::retain(group);
	sources_[SceneTab] = obs::retain(scene);

	for (int i = 0; i < TabCount; ++i)
		tabs_->setTabEnabled(i, static_cast<bool>(sources_[i]));
	if (!sources_[tabs_->currentIndex()]) {
		for (int i = 0; i < TabCount; ++i) {
			if (sources_[i]) {
				QSignalBlocker b(tabs_);
				tabs_->setCurrentIndex(i);
				break;
			}
		}
	}
	rebuildList(true);
}

obs_source_t *FiltersPanel::parent() const
{
	const int i = tabs_->currentIndex();
	return i >= 0 && i < TabCount ? sources_[i].get() : nullptr;
}

void FiltersPanel::onTabChanged(int)
{
	rebuildList(true);
}

// ---------------------------------------------------------------------------
// OBS access
// ---------------------------------------------------------------------------
std::vector<FiltersPanel::Entry> FiltersPanel::entries(obs_source_t *p) const
{
	std::vector<Entry> out;
	if (!p)
		return out;
	obs_source_enum_filters(
		p,
		[](obs_source_t *, obs_source_t *filter, void *param) {
			Entry e;
			e.name = sourceName(filter);
			e.kind = obs_source_get_id(filter) ? obs_source_get_id(filter) : "";
			e.enabled = obs_source_enabled(filter);
			static_cast<std::vector<Entry> *>(param)->push_back(std::move(e));
		},
		&out);
	return out;
}

obs::SourceRef FiltersPanel::filterByName(const std::string &name) const
{
	obs_source_t *p = parent();
	return p ? obs::adopt(obs_source_get_filter_by_name(p, name.c_str())) : obs::SourceRef();
}

std::string FiltersPanel::selectedName() const
{
	auto *item = list_->currentItem();
	return item ? item->data(kNameRole).toString().toStdString() : std::string();
}

// ---------------------------------------------------------------------------
// List
// ---------------------------------------------------------------------------
void FiltersPanel::poll()
{
	if (!isVisible())
		return;
	rebuildList(false);
	refreshEditorValues();
}

void FiltersPanel::rebuildList(bool force)
{
	obs_source_t *p = parent();
	const auto list = entries(p);

	std::string sig = sourceName(p) + '\n';
	for (const auto &e : list)
		sig += e.name + '|' + e.kind + '|' + (e.enabled ? '1' : '0') + '\n';
	if (!force && sig == signature_)
		return;
	signature_ = std::move(sig);

	const std::string keep = selectedName();
	updating_ = true;
	{
		QSignalBlocker b(list_);
		list_->clear();
		int selectRow = -1;
		for (const auto &e : list) {
			auto *item = new QListWidgetItem(QString::fromStdString(e.name), list_);
			item->setData(kNameRole, QString::fromStdString(e.name));
			item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable |
				       Qt::ItemIsDragEnabled);
			item->setCheckState(e.enabled ? Qt::Checked : Qt::Unchecked);
			if (const char *disp = obs_source_get_display_name(e.kind.c_str()))
				item->setToolTip(QString::fromUtf8(disp));
			if (e.name == keep)
				selectRow = list_->count() - 1;
		}
		if (selectRow >= 0)
			list_->setCurrentRow(selectRow);
		else if (list_->count() > 0)
			list_->setCurrentRow(0);
	}
	updating_ = false;

	removeButton_->setEnabled(list_->count() > 0);
	resetAllButton_->setEnabled(list_->count() > 0);
	addButton_->setEnabled(p != nullptr);
	rebuildEditor();
}

void FiltersPanel::onSelectionChanged()
{
	if (!updating_)
		rebuildEditor();
}

// ---------------------------------------------------------------------------
// Reordering
// ---------------------------------------------------------------------------
void FiltersPanel::onRowMoveRequested(int from, int destRow)
{
	const auto list = entries(parent());
	const int target = dropTargetIndex(int(list.size()), from, destRow);
	if (target < 0 || from < 0 || from >= int(list.size()) || target == from)
		return;
	moveFilterTo(list[size_t(from)].name, target);
	rebuildList(true);
}

// libobs only offers relative moves. The meaning of MOVE_UP vs. our row order is
// verified on the first real move (and corrected if it was the other way round).
void FiltersPanel::moveFilterTo(const std::string &name, int target)
{
	obs_source_t *p = parent();
	auto filter = filterByName(name);
	if (!p || !filter)
		return;

	auto indexOf = [&] {
		const auto list = entries(p);
		for (size_t i = 0; i < list.size(); ++i)
			if (list[i].name == name)
				return int(i);
		return -1;
	};
	auto step = [&](bool towardLowerIndex) {
		const bool up = (towardLowerIndex == upDecreasesIndex_);
		obs_source_filter_set_order(p, filter.get(), up ? OBS_ORDER_MOVE_UP : OBS_ORDER_MOVE_DOWN);
	};

	int idx = indexOf();
	for (int guard = 0; idx >= 0 && idx != target && guard < 1000; ++guard) {
		const bool wantLower = target < idx;
		step(wantLower);
		const int now = indexOf();
		if (now == idx)
			break; // cannot move further
		if (!directionProbed_) {
			directionProbed_ = true;
			if ((now < idx) != wantLower) { // moved the wrong way: flip mapping, undo, retry
				upDecreasesIndex_ = !upDecreasesIndex_;
				step(!wantLower);
				idx = indexOf();
				continue;
			}
		}
		idx = now;
	}
}

// ---------------------------------------------------------------------------
// Add / remove
// ---------------------------------------------------------------------------
void FiltersPanel::rebuildAddMenu()
{
	QMenu *menu = addButton_->menu();
	menu->clear();
	obs_source_t *p = parent();
	if (!p)
		return;
	const uint32_t parentFlags = obs_source_get_output_flags(p);

	struct Candidate {
		QString display;
		std::string id;
	};
	std::vector<Candidate> found;
	const char *id = nullptr;
	for (size_t i = 0; obs_enum_filter_types(i, &id); ++i) {
		const uint32_t f = obs_get_source_output_flags(id);
		if (f & (OBS_SOURCE_CAP_DISABLED | OBS_SOURCE_DEPRECATED))
			continue;
		const bool video = (f & OBS_SOURCE_VIDEO) && (parentFlags & OBS_SOURCE_VIDEO);
		const bool audio = (f & OBS_SOURCE_AUDIO) && (parentFlags & OBS_SOURCE_AUDIO);
		if (!video && !audio)
			continue;
		found.push_back({QString::fromUtf8(obs_source_get_display_name(id)), id});
	}
	std::sort(found.begin(), found.end(),
		  [](const Candidate &a, const Candidate &b) { return a.display.localeAwareCompare(b.display) < 0; });
	for (const auto &c : found) {
		const std::string typeId = c.id;
		menu->addAction(c.display, this, [this, typeId] { addFilter(typeId.c_str()); });
	}
}

void FiltersPanel::addFilter(const char *typeId)
{
	obs_source_t *p = parent();
	if (!p)
		return;
	const std::string base = obs_source_get_display_name(typeId);
	std::string name = base;
	for (int n = 2; filterByName(name); ++n)
		name = base + " " + std::to_string(n);

	obs::SourceRef filter = obs::adopt(obs_source_create(typeId, name.c_str(), nullptr, nullptr));
	if (!filter)
		return;
	obs_source_filter_add(p, filter.get()); // parent takes its own reference
	rebuildList(true);
	for (int r = 0; r < list_->count(); ++r)
		if (list_->item(r)->data(kNameRole).toString().toStdString() == name)
			list_->setCurrentRow(r);
}

void FiltersPanel::removeSelected()
{
	obs_source_t *p = parent();
	if (auto f = filterByName(selectedName()); p && f) {
		obs_source_filter_remove(p, f.get());
		rebuildList(true);
	}
}

void FiltersPanel::resetAllFilters()
{
	obs_source_t *p = parent();
	const auto list = entries(p);
	if (!p || list.empty())
		return;
	const auto answer = QMessageBox::question(
		this, tr("Reset all filters"),
		tr("Reset all %1 filters on \"%2\" to their default settings? Their current settings will be lost.")
			.arg(list.size())
			.arg(QString::fromStdString(sourceName(p))));
	if (answer != QMessageBox::Yes)
		return;
	for (const Entry &e : list) {
		if (auto f = filterByName(e.name)) {
			obs::DataRef empty = obs::adopt(obs_data_create());
			obs_source_reset_settings(f.get(), empty.get()); // same as OBS's "Defaults" button
		}
	}
	refreshEditorValues();
}

// ---------------------------------------------------------------------------
// Command copier: right-click on a row
// ---------------------------------------------------------------------------
void FiltersPanel::attachRowMenu()
{
	QPointer<FiltersPanel> self(this);
	attachCommandMenu(list_, [self]() -> std::optional<CommandSet> {
		if (!self)
			return std::nullopt;
		auto *item = self->list_->itemAt(self->list_->viewport()->mapFromGlobal(QCursor::pos()));
		if (!item) {
			// Not on a row: the commands for the list as a whole.
			obs_source_t *p = self->parent();
			if (!p)
				return std::nullopt;
			const ObsRequest all = reqGetSourceFilterList(sourceName(p));
			CommandSet c;
			c.streamerBot = [all] { return sbFrom(all); };
			c.wsGet = [all] { return wsFrom(all); };
			c.resets.emplace_back("Reset all filters to defaults", [self] {
				if (self)
					self->resetAllFilters();
			});
			return c;
		}
		const std::string name = item->data(kNameRole).toString().toStdString();
		const std::string src = sourceName(self->parent());
		auto filter = self->filterByName(name);
		if (!filter)
			return std::nullopt;

		// Snapshot now; menu actions may run after the chain has changed.
		// obs_data_get_json yields only user-set values, which is what a Set should carry.
		obs::DataRef data = obs::settingsOf(filter.get());
		const char *js = data ? obs_data_get_json(data.get()) : nullptr;
		const std::string json = js ? js : "";
		const bool enabled = obs_source_enabled(filter.get());
		const int row = self->list_->row(item);

		const ObsRequest set = reqSetSourceFilterSettings(src, name, Json::raw(json));
		const ObsRequest get = reqGetSourceFilter(src, name);
		const ObsRequest en = reqSetSourceFilterEnabled(src, name, enabled);
		const ObsRequest idx = reqSetSourceFilterIndex(src, name, row);
		const char *kindId = obs_source_get_id(filter.get());
		// Add: recreates this filter (kind and settings) on whichever source the command is aimed at.
		const ObsRequest add = reqCreateSourceFilter(src, name, kindId ? kindId : "", Json::raw(json));
		const ObsRequest remove = reqRemoveSourceFilter(src, name);
		const ObsRequest all = reqGetSourceFilterList(src);

		CommandSet c;
		c.streamerBot = [set] { return sbFrom(set); };
		c.wsSet = [set] { return wsFrom(set); };
		c.wsGet = [get] { return wsFrom(get); };
		c.extras = {
			{"Copy Streamer.Bot C# (Set Enabled)", [en] { return sbFrom(en); }},
			{"Copy OBS WebSocket v5 (Set Enabled)", [en] { return wsFrom(en); }},
			{"Copy Streamer.Bot C# (Set Index)", [idx] { return sbFrom(idx); }},
			{"Copy OBS WebSocket v5 (Set Index)", [idx] { return wsFrom(idx); }},
			{"Copy Streamer.Bot C# (Add Filter)", [add] { return sbFrom(add); }},
			{"Copy OBS WebSocket v5 (Add Filter)", [add] { return wsFrom(add); }},
			{"Copy Streamer.Bot C# (Remove Filter)", [remove] { return sbFrom(remove); }},
			{"Copy OBS WebSocket v5 (Remove Filter)", [remove] { return wsFrom(remove); }},
			{"Copy Streamer.Bot C# (Get All Filters)", [all] { return sbFrom(all); }},
			{"Copy OBS WebSocket v5 (Get All Filters)", [all] { return wsFrom(all); }},
		};
		c.resets.emplace_back("Reset all filters to defaults", [self] {
			if (self)
				self->resetAllFilters();
		});
		c.reset = [self, name] {
			if (!self)
				return;
			if (auto f = self->filterByName(name)) {
				obs::DataRef empty = obs::adopt(obs_data_create());
				obs_source_reset_settings(f.get(), empty.get()); // same as OBS's "Defaults"
			}
		};
		return c;
	});
}

// ---------------------------------------------------------------------------
// Quick editor for the selected filter
// ---------------------------------------------------------------------------
void FiltersPanel::rebuildEditor()
{
	editorFields_.clear();
	while (editorForm_->rowCount() > 0)
		editorForm_->removeRow(0);

	const std::string name = selectedName();
	auto filter = name.empty() ? obs::SourceRef() : filterByName(name);
	if (!filter)
		return;
	const char *kindId = obs_source_get_id(filter.get());
	const EditorSpec *spec = specForKind(kindId ? kindId : "");
	if (!spec) {
		editorForm_->addRow(new QLabel(tr("Full settings for this filter are in the Properties tab."), this));
		return;
	}

	QPointer<FiltersPanel> self(this);
	for (const KeySpec &k : spec->keys) {
		auto *box = new QDoubleSpinBox(this);
		box->setRange(k.min, k.max);
		box->setSingleStep(k.step);
		box->setDecimals(k.decimals);
		editorForm_->addRow(tr(k.label), box);
		editorFields_.push_back({box, &k});

		connect(box, &QDoubleSpinBox::valueChanged, this, [self, name, key = std::string(k.key)](double v) {
			if (!self || self->updating_)
				return;
			if (auto f = self->filterByName(name)) {
				obs::DataRef patch = obs::adopt(obs_data_create());
				obs_data_set_double(patch.get(), key.c_str(), v);
				obs_source_update(f.get(), patch.get()); // merges into existing settings
			}
		});

		// Right-click the LABEL: this filter's whole settings as one command, plus the bulk resets.
		if (QWidget *labelW = editorForm_->labelForField(box)) {
			attachCommandMenu(labelW, [self, name]() -> std::optional<CommandSet> {
				if (!self || !self->parent())
					return std::nullopt;
				auto f = self->filterByName(name);
				if (!f)
					return std::nullopt;
				const std::string src = sourceName(self->parent());
				obs::DataRef current = obs::settingsOf(f.get());
				const char *js = current ? obs_data_get_json(current.get()) : nullptr;
				const std::string json = js ? js : "";
				CommandSet c;
				c.choices.push_back(makeChoice("All settings of this filter (full payload)",
							       reqSetSourceFilterSettings(src, name, Json::raw(json), true),
							       reqGetSourceFilter(src, name)));
				c.resets.emplace_back("Reset this filter to defaults", [self, name] {
					if (!self)
						return;
					if (auto g = self->filterByName(name)) {
						obs::DataRef empty = obs::adopt(obs_data_create());
						obs_source_reset_settings(g.get(), empty.get());
						self->refreshEditorValues();
					}
				});
				c.resets.emplace_back("Reset all filters to defaults", [self] {
					if (self)
						self->resetAllFilters();
				});
				return c;
			});
		}

		attachCommandMenu(box, [self, name, box, key = std::string(k.key), def = k.def]()
					       -> std::optional<CommandSet> {
			if (!self || !self->parent())
				return std::nullopt;
			const std::string src = sourceName(self->parent());
			Json settings = Json::object();
			settings.set(key, box->value());
			const ObsRequest set = reqSetSourceFilterSettings(src, name, std::move(settings), true);
			const ObsRequest get = reqGetSourceFilter(src, name);
			CommandSet c;
			c.streamerBot = [set] { return sbFrom(set); };
			c.wsSet = [set] { return wsFrom(set); };
			c.wsGet = [get] { return wsFrom(get); };
			c.reset = [box, def] { box->setValue(def); };
			return c;
		});
	}
	refreshEditorValues();
}

void FiltersPanel::refreshEditorValues()
{
	if (editorFields_.empty())
		return;
	auto filter = filterByName(selectedName());
	if (!filter)
		return;
	obs::DataRef current = obs::settingsOf(filter.get());
	if (!current)
		return;

	updating_ = true;
	for (const auto &f : editorFields_) {
		if (f.box->hasFocus())
			continue; // don't fight the user's typing
		QSignalBlocker b(f.box);
		f.box->setValue(obs_data_get_double(current.get(), f.spec->key)); // falls back to filter defaults
	}
	updating_ = false;
}

} // namespace se
