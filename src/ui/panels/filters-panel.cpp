// SPDX-License-Identifier: GPL-2.0-or-later
#include "ui/panels/filters-panel.hpp"

#include "core/list-reorder.hpp"
#include "core/payload-generator.hpp"
#include "ui/filter-clipboard.hpp"
#include "ui/filter-popouts.hpp"
#include "ui/panels/properties-panel.hpp"
#include "ui/widgets/command-menu.hpp"
#include "ui/widgets/reorder-list.hpp"
#include "util/plugin-config.hpp"

#include <obs.h>

#include <QApplication>
#include <QCheckBox>
#include <QCursor>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace se {

// The flag constants in core/filter-rules.hpp are copies so that code stays libobs-free; make sure
// they cannot drift from libobs.
static_assert(kSourceFlagVideo == OBS_SOURCE_VIDEO);
static_assert(kSourceFlagAudio == OBS_SOURCE_AUDIO);
static_assert(kSourceFlagAsync == OBS_SOURCE_ASYNC);

namespace {

std::string str(const char *s)
{
	return s ? s : "";
}

std::string sourceName(obs_source_t *s)
{
	return s ? str(obs_source_get_name(s)) : std::string();
}

constexpr int kNameRole = Qt::UserRole;

} // namespace

// One row of the filter list: [expand] [enabled] name  (type)  [pop out], with the inline editor
// underneath when expanded. The labels let mouse events through so a press on them selects / drags
// the row (the list handles that); the buttons and checkbox work as buttons.
class FilterRow final : public QWidget {
	// No Q_OBJECT: it declares no signals or slots of its own (callbacks instead), so no moc step is needed.
public:
	struct Callbacks {
		std::function<void(bool)> enabled;
		std::function<void(bool)> expanded;
		std::function<void()> popout;
		std::function<void()> resized;
	};

	FilterRow(const QString &name, const QString &kind, bool enabled, bool expanded, Callbacks cb,
		  QWidget *parent)
		: QWidget(parent), cb_(std::move(cb))
	{
		layout_ = new QVBoxLayout(this);
		layout_->setContentsMargins(2, 2, 2, 2);
		layout_->setSpacing(2);

		auto *head = new QHBoxLayout;
		head->setSpacing(4);
		expand_ = new QToolButton(this);
		expand_->setAutoRaise(true);
		expand_->setCheckable(true);
		expand_->setToolTip(QObject::tr("Show or hide this filter's settings"));
		setExpandedSilently(expanded);
		enabled_ = new QCheckBox(this);
		enabled_->setChecked(enabled);
		enabled_->setToolTip(QObject::tr("Turn this filter on or off"));
		auto *nameLabel = new QLabel(name, this);
		nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
		auto *kindLabel = new QLabel(kind, this);
		kindLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
		kindLabel->setEnabled(false); // dimmed
		auto *pop = new QToolButton(this);
		pop->setAutoRaise(true);
		pop->setText(QObject::tr("Pop out"));
		pop->setToolTip(QObject::tr("Edit this filter in its own window"));
		head->addWidget(expand_);
		head->addWidget(enabled_);
		head->addWidget(nameLabel, 1);
		head->addWidget(kindLabel);
		head->addWidget(pop);
		layout_->addLayout(head);

		QObject::connect(expand_, &QToolButton::toggled, this, [this](bool on) {
			setExpandedSilently(on);
			if (cb_.expanded)
				cb_.expanded(on);
		});
		QObject::connect(enabled_, &QCheckBox::toggled, this, [this](bool on) {
			if (cb_.enabled)
				cb_.enabled(on);
		});
		QObject::connect(pop, &QToolButton::clicked, this, [this] {
			if (cb_.popout)
				cb_.popout();
		});
	}

	void setEnabledSilently(bool on)
	{
		QSignalBlocker b(enabled_);
		enabled_->setChecked(on);
	}

	bool isEnabledChecked() const { return enabled_->isChecked(); }
	bool isExpanded() const { return expand_->isChecked(); }

	void setExpandedSilently(bool on)
	{
		QSignalBlocker b(expand_);
		expand_->setChecked(on);
		expand_->setText(on ? QStringLiteral("▾") : QStringLiteral("▸"));
	}

	// Takes ownership. nullptr removes the current body.
	void setBody(QWidget *body)
	{
		if (body_) {
			body_->hide();
			body_->deleteLater();
			layout_->removeWidget(body_);
		}
		body_ = body;
		if (body_) {
			body_->setParent(this);
			layout_->addWidget(body_);
			body_->show();
		}
	}

protected:
	bool event(QEvent *e) override
	{
		const bool r = QWidget::event(e);
		if (e->type() == QEvent::LayoutRequest && cb_.resized)
			cb_.resized();
		return r;
	}

private:
	Callbacks cb_;
	QVBoxLayout *layout_ = nullptr;
	QToolButton *expand_ = nullptr;
	QCheckBox *enabled_ = nullptr;
	QWidget *body_ = nullptr;
};

namespace {

QString arrow(bool open)
{
	return open ? QStringLiteral("▾ ") : QStringLiteral("▸ ");
}

} // namespace

// ---------------------------------------------------------------------------
FiltersPanel::FiltersPanel(QWidget *parent) : QWidget(parent)
{
	auto *root = new QVBoxLayout(this);

	tabs_ = new QTabBar(this);
	tabs_->addTab(tr("Source"));
	tabs_->addTab(tr("Group"));
	tabs_->addTab(tr("Scene"));
	root->addWidget(tabs_);

	search_ = new QLineEdit(this);
	search_->setPlaceholderText(tr("Search filters"));
	search_->setClearButtonEnabled(true);
	root->addWidget(search_);

	// Everything below the search box scrolls as one, so expanded editors never squeeze the lists.
	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	content_ = new QWidget;
	auto *contentLayout = new QVBoxLayout(content_);
	contentLayout->setContentsMargins(0, 0, 0, 0);
	for (int i = 0; i < 2; ++i) {
		SectionUi &s = sections_[i];
		s.section = i == 0 ? FilterSection::AudioVideo : FilterSection::Effect;
		s.header = new QToolButton(content_);
		s.header->setCheckable(true);
		s.header->setChecked(uiSettings().value(QString("filters/open/%1").arg(i), true).toBool());
		s.header->setToolButtonStyle(Qt::ToolButtonTextOnly);
		s.header->setAutoRaise(true);
		s.header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		s.header->setStyleSheet("QToolButton { text-align: left; font-weight: bold; }");
		s.list = new ReorderListWidget(content_);
		s.list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		s.list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		contentLayout->addWidget(s.header);
		contentLayout->addWidget(s.list);

		connect(s.header, &QToolButton::toggled, this, [this, i](bool open) {
			uiSettings().setValue(QString("filters/open/%1").arg(i), open);
			updateHeaders();
		});
		connect(s.list, &ReorderListWidget::rowMoveRequested, this,
			[this, sec = s.section](int from, int dest) { onRowMoveRequested(sec, from, dest); });
		connect(s.list, &QListWidget::currentRowChanged, this, [this, sec = s.section](int) { onRowSelected(sec); });
	}
	contentLayout->addStretch(1);
	scroll->setWidget(content_);
	root->addWidget(scroll, 1);

	message_ = new QLabel(this);
	message_->setWordWrap(true);
	message_->hide();
	root->addWidget(message_);

	// Buttons wrap onto a second row rather than overflowing a narrow dock.
	auto makeRow = [&] {
		auto *r = new QHBoxLayout;
		r->setSpacing(4);
		root->addLayout(r);
		return r;
	};
	auto *row1 = makeRow();
	addButton_ = new QPushButton(this);
	addButton_->setText(tr("+ Add"));
	auto *addMenu = new QMenu(addButton_);
	addButton_->setMenu(addMenu);
	connect(addMenu, &QMenu::aboutToShow, this, &FiltersPanel::rebuildAddMenu);
	removeButton_ = new QPushButton(tr("Remove"), this);
	upButton_ = new QPushButton(tr("Move up"), this);
	downButton_ = new QPushButton(tr("Move down"), this);
	row1->addWidget(addButton_);
	row1->addWidget(removeButton_);
	row1->addWidget(upButton_);
	row1->addWidget(downButton_);
	row1->addStretch(1);

	auto *row2 = makeRow();
	copyButton_ = new QPushButton(this);
	copyButton_->setText(tr("Copy"));
	auto *copyMenu = new QMenu(copyButton_);
	copyMenu->addAction(tr("Copy selected filter"), this, [this] { copyFilters(false); });
	copyMenu->addAction(tr("Copy all filters on this tab"), this, [this] { copyFilters(true); });
	copyButton_->setMenu(copyMenu);
	copyButton_->setToolTip(tr("Copy filters so they can be pasted onto another source, even after a restart"));
	pasteButton_ = new QPushButton(tr("Paste"), this);
	pasteButton_->setToolTip(tr("Add the copied filters to this source"));
	expandButton_ = new QPushButton(tr("Expand all"), this);
	collapseButton_ = new QPushButton(tr("Collapse all"), this);
	row2->addWidget(copyButton_);
	row2->addWidget(pasteButton_);
	row2->addWidget(expandButton_);
	row2->addWidget(collapseButton_);
	row2->addStretch(1);

	auto *row3 = makeRow();
	resetAllButton_ = new QPushButton(tr("Reset all"), this);
	resetAllButton_->setToolTip(tr("Reset every filter in this list to its default settings"));
	closeWindowsButton_ = new QPushButton(this);
	closeWindowsButton_->setToolTip(tr("Close every filter window that was popped out"));
	row3->addWidget(resetAllButton_);
	row3->addWidget(closeWindowsButton_);
	row3->addStretch(1);

	auto updateWindowsButton = [this](int n) {
		closeWindowsButton_->setText(tr("Close all dialogs (%1)").arg(n));
		closeWindowsButton_->setVisible(n > 0);
	};
	updateWindowsButton(FilterPopouts::instance().count());
	connect(&FilterPopouts::instance(), &FilterPopouts::countChanged, this, updateWindowsButton);
	connect(closeWindowsButton_, &QPushButton::clicked, this, [] { FilterPopouts::instance().closeAll(); });

	connect(tabs_, &QTabBar::currentChanged, this, &FiltersPanel::onTabChanged);
	connect(search_, &QLineEdit::textChanged, this, [this] { rebuildList(true); });
	connect(removeButton_, &QPushButton::clicked, this, &FiltersPanel::removeSelected);
	connect(upButton_, &QPushButton::clicked, this, [this] { moveSelected(-1); });
	connect(downButton_, &QPushButton::clicked, this, [this] { moveSelected(1); });
	connect(pasteButton_, &QPushButton::clicked, this, &FiltersPanel::pasteFromClipboard);
	connect(expandButton_, &QPushButton::clicked, this, [this] { expandAll(true); });
	connect(collapseButton_, &QPushButton::clicked, this, [this] { expandAll(false); });
	connect(resetAllButton_, &QPushButton::clicked, this, &FiltersPanel::resetAllFilters);

	attachMenus();

	poll_ = new QTimer(this);
	poll_->setInterval(500);
	connect(poll_, &QTimer::timeout, this, &FiltersPanel::poll);
	poll_->start();

	const int saved = uiSettings().value("filters/lastTab", 0).toInt();
	wantedTab_ = saved >= 0 && saved < TabCount ? saved : 0;

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

	// Qt moves the current tab around by itself as tabs are disabled, so pick it deliberately: the
	// tab the user last chose if it applies to this source, otherwise the first one that does.
	{
		QSignalBlocker b(tabs_);
		for (int i = 0; i < TabCount; ++i)
			tabs_->setTabVisible(i, static_cast<bool>(sources_[i])); // Group / Scene only when they exist
		int pick = sources_[wantedTab_] ? wantedTab_ : -1;
		for (int i = 0; pick < 0 && i < TabCount; ++i)
			if (sources_[i])
				pick = i;
		if (pick >= 0)
			tabs_->setCurrentIndex(pick);
	}
	selected_.clear();
	loadExpanded();
	updateCounts();
	rebuildList(true);
}

obs_source_t *FiltersPanel::parent() const
{
	const int i = tabs_->currentIndex();
	return i >= 0 && i < TabCount ? sources_[i].get() : nullptr;
}

// Reset UI: reopen any section that was collapsed.
void FiltersPanel::resetLayout()
{
	for (SectionUi &s : sections_)
		s.header->setChecked(true);
}

void FiltersPanel::onTabChanged(int i)
{
	wantedTab_ = i;
	uiSettings().setValue("filters/lastTab", i);
	selected_.clear();
	loadExpanded();
	rebuildList(true);
}

std::string FiltersPanel::sourceKey() const
{
	obs_source_t *p = parent();
	return p ? str(obs_source_get_uuid(p)) : std::string();
}

void FiltersPanel::loadExpanded()
{
	expanded_.clear();
	const std::string key = sourceKey();
	if (key.empty())
		return;
	for (const QString &n : uiSettings().value(QString::fromStdString("filters/expanded/" + key)).toStringList())
		expanded_.insert(n.toStdString());
}

void FiltersPanel::saveExpanded() const
{
	const std::string key = sourceKey();
	if (key.empty())
		return;
	QStringList names;
	for (const std::string &n : expanded_)
		names << QString::fromStdString(n);
	const QString k = QString::fromStdString("filters/expanded/" + key);
	if (names.isEmpty())
		uiSettings().remove(k);
	else
		uiSettings().setValue(k, names);
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
			e.kind = str(obs_source_get_id(filter));
			e.display = str(obs_source_get_display_name(e.kind.c_str()));
			e.enabled = obs_source_enabled(filter);
			e.flags = obs_source_get_output_flags(filter);
			e.section = sectionFor(e.flags);
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

std::vector<FilterSection> FiltersPanel::chainSections() const
{
	std::vector<FilterSection> out;
	for (const Entry &e : entries(parent()))
		out.push_back(e.section);
	return out;
}

int FiltersPanel::rowCount(FilterSection section) const
{
	return int(ui(section).names.size());
}

// ---------------------------------------------------------------------------
// List
// ---------------------------------------------------------------------------
void FiltersPanel::poll()
{
	if (!isVisible())
		return;
	updateCounts();
	rebuildList(false);
	updateButtons();
}

void FiltersPanel::updateCounts()
{
	static const char *const names[] = {QT_TR_NOOP("Source"), QT_TR_NOOP("Group"), QT_TR_NOOP("Scene")};
	for (int i = 0; i < TabCount; ++i) {
		const size_t n = sources_[i] ? obs_source_filter_count(sources_[i].get()) : 0;
		tabs_->setTabText(i, n ? tr("%1 (%2)").arg(tr(names[i])).arg(n) : tr(names[i]));
	}
}

QListWidgetItem *FiltersPanel::itemFor(const std::string &name) const
{
	for (const SectionUi &s : sections_) {
		for (int r = 0; r < s.list->count(); ++r)
			if (s.list->item(r)->data(kNameRole).toString().toStdString() == name)
				return s.list->item(r);
	}
	return nullptr;
}

void FiltersPanel::fitList(SectionUi &s)
{
	int h = 2 * s.list->frameWidth();
	for (int r = 0; r < s.list->count(); ++r)
		h += s.list->item(r)->sizeHint().height();
	s.list->setFixedHeight(std::max(h, 4));
}

void FiltersPanel::syncRowHeight(const std::string &name)
{
	QListWidgetItem *item = itemFor(name);
	auto it = rows_.find(name);
	if (!item || it == rows_.end() || !it->second)
		return;
	const QSize want = it->second->sizeHint();
	QSize have = item->sizeHint();
	if (have.height() == want.height())
		return;
	item->setSizeHint(QSize(have.width() > 0 ? have.width() : want.width(), want.height()));
	for (SectionUi &s : sections_)
		if (std::find(s.names.begin(), s.names.end(), name) != s.names.end())
			fitList(s);
}

void FiltersPanel::setExpanded(const std::string &name, bool open)
{
	if (open)
		expanded_.insert(name);
	else
		expanded_.erase(name);
	saveExpanded();

	auto it = rows_.find(name);
	if (it == rows_.end() || !it->second)
		return;
	if (open) {
		auto *panel = new PropertiesPanel(PropertiesPanel::Mode::Embedded, it->second);
		panel->setFilter(parent(), name);
		it->second->setBody(panel);
	} else {
		it->second->setBody(nullptr);
	}
	it->second->setExpandedSilently(open);
	syncRowHeight(name);
}

void FiltersPanel::expandAll(bool open)
{
	std::vector<std::string> names;
	for (const SectionUi &s : sections_)
		names.insert(names.end(), s.names.begin(), s.names.end());
	for (const std::string &n : names)
		setExpanded(n, open);
}

void FiltersPanel::rebuildList(bool force)
{
	obs_source_t *p = parent();
	chain_ = entries(p);

	const std::string query = search_->text().toStdString();
	std::string sig = sourceName(p) + '\n' + query + '\n';
	for (const Entry &e : chain_)
		sig += e.name + '|' + e.kind + '\n';
	if (!force && sig == signature_) {
		syncEnabledStates();
		return;
	}
	// Don't pull rows out from under a drag in progress; the next tick catches up.
	if (!force && QApplication::mouseButtons() != Qt::NoButton)
		return;
	signature_ = std::move(sig);

	updating_ = true;
	rows_.clear();
	for (SectionUi &s : sections_) {
		QSignalBlocker b(s.list);
		s.list->clear();
		s.names.clear();
	}

	QPointer<FiltersPanel> self(this);
	for (const Entry &e : chain_) {
		if (!matchesFilterSearch(e.name, e.display, query))
			continue;
		SectionUi &s = ui(e.section);
		auto *item = new QListWidgetItem(s.list);
		item->setData(kNameRole, QString::fromStdString(e.name));
		item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);

		const std::string name = e.name;
		FilterRow::Callbacks cb;
		cb.enabled = [self, name](bool on) {
			if (!self || self->updating_)
				return;
			if (auto f = self->filterByName(name))
				obs_source_set_enabled(f.get(), on);
		};
		cb.expanded = [self, name](bool on) {
			if (self)
				self->setExpanded(name, on);
		};
		cb.popout = [self, name] {
			if (self && self->parent())
				FilterPopouts::instance().open(self->parent(), name);
		};
		cb.resized = [self, name] {
			if (self)
				self->syncRowHeight(name);
		};
		const bool open = expanded_.count(name) > 0;
		auto *row = new FilterRow(QString::fromStdString(e.name), QString::fromStdString(e.display), e.enabled,
					  open, std::move(cb), s.list);
		s.list->setItemWidget(item, row);
		item->setSizeHint(row->sizeHint());
		rows_[name] = row;
		s.names.push_back(name);
		attachCommandMenu(row, [self, name] { return self ? self->rowCommands(name) : std::nullopt; });
		if (open) {
			auto *panel = new PropertiesPanel(PropertiesPanel::Mode::Embedded, row);
			panel->setFilter(p, name);
			row->setBody(panel);
			item->setSizeHint(row->sizeHint());
		}
	}

	// Keep the selection if its row is still showing; otherwise fall back to the first row.
	auto visible = [&](const std::string &n) {
		return std::any_of(std::begin(sections_), std::end(sections_), [&](const SectionUi &s) {
			return std::find(s.names.begin(), s.names.end(), n) != s.names.end();
		});
	};
	if (!visible(selected_)) {
		selected_.clear();
		for (const SectionUi &s : sections_)
			if (selected_.empty() && !s.names.empty())
				selected_ = s.names.front();
	}
	for (SectionUi &s : sections_) {
		QSignalBlocker b(s.list);
		const auto it = std::find(s.names.begin(), s.names.end(), selected_);
		s.list->setCurrentRow(it == s.names.end() ? -1 : int(it - s.names.begin()));
		fitList(s);
	}
	updating_ = false;

	updateHeaders();
	updateButtons();
}

void FiltersPanel::syncEnabledStates()
{
	updating_ = true;
	for (const Entry &e : chain_) {
		auto it = rows_.find(e.name);
		if (it != rows_.end() && it->second && it->second->isEnabledChecked() != e.enabled)
			it->second->setEnabledSilently(e.enabled);
	}
	updating_ = false;
}

void FiltersPanel::updateHeaders()
{
	obs_source_t *p = parent();
	const uint32_t flags = p ? obs_source_get_output_flags(p) : 0;
	const bool video = (flags & OBS_SOURCE_VIDEO) != 0;
	const bool audio = (flags & OBS_SOURCE_AUDIO) != 0;
	const bool async = (flags & OBS_SOURCE_ASYNC) != 0;

	int total[2] = {0, 0};
	for (const Entry &e : chain_)
		++total[e.section == FilterSection::AudioVideo ? 0 : 1];

	// Same rules as OBS's own dialog: hide a section the source cannot use (unless it has leftovers).
	const bool showAv = p && (async || audio || total[0] > 0);
	const bool showFx = p && (video || total[1] > 0);
	const bool avIsAudioOnly = !video || (audio && !async);

	for (int i = 0; i < 2; ++i) {
		SectionUi &s = sections_[i];
		const bool show = i == 0 ? showAv : showFx;
		const QString title = i == 0 ? (avIsAudioOnly ? tr("Audio Filters") : tr("Audio/Video Filters"))
					     : tr("Effect Filters");
		const int shown = int(s.names.size());
		const QString count = shown == total[i] ? QString::number(total[i])
							: tr("%1 of %2").arg(shown).arg(total[i]);
		s.header->setText(arrow(s.header->isChecked()) + title + " (" + count + ")");
		s.header->setVisible(show);
		s.list->setVisible(show && s.header->isChecked() && shown > 0);
	}
}

void FiltersPanel::updateButtons()
{
	obs_source_t *p = parent();
	const bool searching = !search_->text().trimmed().isEmpty();
	const bool any = !chain_.empty();
	bool canUp = false, canDown = false;
	for (const SectionUi &s : sections_) {
		const auto it = std::find(s.names.begin(), s.names.end(), selected_);
		if (it != s.names.end() && !searching) {
			canUp = it != s.names.begin();
			canDown = it + 1 != s.names.end();
		}
	}
	addButton_->setEnabled(p != nullptr);
	removeButton_->setEnabled(!selected_.empty());
	upButton_->setEnabled(canUp);
	downButton_->setEnabled(canDown);
	copyButton_->setEnabled(any);
	pasteButton_->setEnabled(p && !filterClipboardEmpty());
	expandButton_->setEnabled(any);
	collapseButton_->setEnabled(any);
	resetAllButton_->setEnabled(any);
}

void FiltersPanel::showMessage(const QString &text)
{
	message_->setText(text);
	message_->setVisible(!text.isEmpty());
	QPointer<QLabel> label(message_);
	QTimer::singleShot(6000, this, [label, text] {
		if (label && label->text() == text)
			label->hide();
	});
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------
void FiltersPanel::onRowSelected(FilterSection section)
{
	if (updating_)
		return;
	SectionUi &s = ui(section);
	const int row = s.list->currentRow();
	if (row < 0 || row >= int(s.names.size()))
		return;
	selected_ = s.names[size_t(row)];
	// One selection across both lists.
	SectionUi &other = section == FilterSection::AudioVideo ? sections_[1] : sections_[0];
	QSignalBlocker b(other.list);
	other.list->setCurrentRow(-1);
	updateButtons();
}

void FiltersPanel::select(const std::string &name)
{
	selected_ = name;
	for (SectionUi &s : sections_) {
		QSignalBlocker b(s.list);
		const auto it = std::find(s.names.begin(), s.names.end(), name);
		s.list->setCurrentRow(it == s.names.end() ? -1 : int(it - s.names.begin()));
	}
	updateButtons();
}

// ---------------------------------------------------------------------------
// Reordering
// ---------------------------------------------------------------------------
void FiltersPanel::onRowMoveRequested(FilterSection section, int from, int destRow)
{
	if (!search_->text().trimmed().isEmpty()) {
		rebuildList(true); // a filtered list does not show the real order, so refuse to reorder it
		return;
	}
	SectionUi &s = ui(section);
	const int count = int(s.names.size());
	const int target = dropTargetIndex(count, from, destRow);
	if (target < 0 || from < 0 || from >= count || target == from)
		return;
	const std::string name = s.names[size_t(from)];
	moveFilterTo(name, chainIndexForRow(chainSections(), section, target));
	selected_ = name;
	rebuildList(true);
}

void FiltersPanel::moveSelected(int delta)
{
	for (SectionUi &s : sections_) {
		const auto it = std::find(s.names.begin(), s.names.end(), selected_);
		if (it == s.names.end())
			continue;
		const int row = int(it - s.names.begin());
		const int to = row + delta;
		if (to < 0 || to >= int(s.names.size()))
			return;
		moveFilterTo(selected_, chainIndexForRow(chainSections(), s.section, to));
		rebuildList(true);
		return;
	}
}

// libobs only offers relative moves. The meaning of MOVE_UP vs. our row order is
// verified on the first real move (and corrected if it was the other way round).
void FiltersPanel::moveFilterTo(const std::string &name, int target)
{
	obs_source_t *p = parent();
	auto filter = filterByName(name);
	if (!p || !filter || target < 0)
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
// Add / remove / copy / paste
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
		FilterSection section;
	};
	std::vector<Candidate> found;
	const char *id = nullptr;
	for (size_t i = 0; obs_enum_filter_types(i, &id); ++i) {
		const uint32_t f = obs_get_source_output_flags(id);
		if (f & (OBS_SOURCE_CAP_DISABLED | OBS_SOURCE_DEPRECATED))
			continue;
		if (!fitsSource(parentFlags, f))
			continue;
		found.push_back({QString::fromUtf8(obs_source_get_display_name(id)), id, sectionFor(f)});
	}
	std::sort(found.begin(), found.end(),
		  [](const Candidate &a, const Candidate &b) { return a.display.localeAwareCompare(b.display) < 0; });
	for (FilterSection sec : {FilterSection::AudioVideo, FilterSection::Effect}) {
		bool first = true;
		for (const auto &c : found) {
			if (c.section != sec)
				continue;
			if (first) {
				menu->addSection(sec == FilterSection::AudioVideo ? tr("Audio / Video") : tr("Effect"));
				first = false;
			}
			const std::string typeId = c.id;
			menu->addAction(c.display, this, [this, typeId] { addFilter(typeId.c_str()); });
		}
	}
}

void FiltersPanel::addFilter(const char *typeId)
{
	obs_source_t *p = parent();
	if (!p)
		return;
	const std::string name = uniqueFilterName(str(obs_source_get_display_name(typeId)), [&](const std::string &n) {
		return static_cast<bool>(filterByName(n));
	});
	obs::SourceRef filter = obs::adopt(obs_source_create(typeId, name.c_str(), nullptr, nullptr));
	if (!filter)
		return;
	obs_source_filter_add(p, filter.get()); // parent takes its own reference

	// Show the new filter, open, so its settings can be edited straight away.
	if (!search_->text().isEmpty()) {
		QSignalBlocker b(search_);
		search_->clear();
	}
	expanded_.insert(name);
	saveExpanded();
	selected_ = name;
	rebuildList(true);
}

void FiltersPanel::removeSelected()
{
	obs_source_t *p = parent();
	auto f = filterByName(selected_);
	if (!p || !f)
		return;
	expanded_.erase(selected_);
	saveExpanded();
	obs_source_filter_remove(p, f.get());
	selected_.clear();
	rebuildList(true);
}

void FiltersPanel::copyFilters(bool all)
{
	std::vector<ClipFilter> clip;
	for (const Entry &e : entries(parent())) {
		if (!all && e.name != selected_)
			continue;
		if (auto f = filterByName(e.name))
			clip.push_back(snapshotFilter(f.get()));
	}
	if (clip.empty())
		return;
	writeFilterClipboard(clip);
	showMessage(tr("Copied %n filter(s). Select another source and press Paste.", "", int(clip.size())));
	updateButtons();
}

void FiltersPanel::pasteFromClipboard()
{
	obs_source_t *p = parent();
	if (!p)
		return;
	const PasteResult r = pasteFilters(p, readFilterClipboard());
	QString msg = tr("Pasted %n filter(s).", "", r.pasted);
	if (r.skipped > 0)
		msg += " " + tr("Skipped %n that this source cannot use.", "", r.skipped);
	showMessage(msg);
	if (!r.lastName.empty())
		selected_ = r.lastName;
	rebuildList(true);
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
}

// ---------------------------------------------------------------------------
// Command copier: right-click on a row, or on the list's empty space
// ---------------------------------------------------------------------------
std::optional<CommandSet> FiltersPanel::listCommands()
{
	obs_source_t *p = parent();
	if (!p)
		return std::nullopt;
	QPointer<FiltersPanel> self(this);
	const ObsRequest all = reqGetSourceFilterList(sourceName(p));
	CommandSet c;
	c.streamerBot = [all] { return sbFrom(all); };
	c.wsGet = [all] { return wsFrom(all); };
	c.actions.emplace_back("Paste copied filters here", [self] {
		if (self)
			self->pasteFromClipboard();
	});
	c.resets.emplace_back("Reset all filters to defaults", [self] {
		if (self)
			self->resetAllFilters();
	});
	return c;
}

std::optional<CommandSet> FiltersPanel::rowCommands(const std::string &name)
{
	const std::string src = sourceName(parent());
	auto filter = filterByName(name);
	if (!filter)
		return std::nullopt;
	QPointer<FiltersPanel> self(this);

	// Snapshot now; menu actions may run after the chain has changed.
	// obs_data_get_json yields only user-set values, which is what a Set should carry.
	obs::DataRef settings = obs::settingsOf(filter.get());
	const char *js = settings ? obs_data_get_json(settings.get()) : nullptr;
	const std::string json = js ? js : "";
	const bool enabled = obs_source_enabled(filter.get());
	int index = 0;
	const auto chain = entries(parent());
	for (size_t i = 0; i < chain.size(); ++i)
		if (chain[i].name == name)
			index = int(i);

	const ObsRequest set = reqSetSourceFilterSettings(src, name, Json::raw(json));
	const ObsRequest get = reqGetSourceFilter(src, name);
	const ObsRequest en = reqSetSourceFilterEnabled(src, name, enabled);
	const ObsRequest idx = reqSetSourceFilterIndex(src, name, index);
	const char *kindId = obs_source_get_id(filter.get());
	// Add: recreates this filter (kind and settings) on whichever source the command is aimed at.
	const ObsRequest add = reqCreateSourceFilter(src, name, kindId ? kindId : "", Json::raw(json));
	const ObsRequest remove = reqRemoveSourceFilter(src, name);
	const ObsRequest all = reqGetSourceFilterList(src);

	CommandSet c;
	c.actions.emplace_back("Pop out into a window", [self, name] {
		if (self && self->parent())
			FilterPopouts::instance().open(self->parent(), name);
	});
	c.actions.emplace_back("Copy this filter (for Paste)", [self, name] {
		if (!self)
			return;
		self->select(name);
		self->copyFilters(false);
	});
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
}

void FiltersPanel::attachMenus()
{
	QPointer<FiltersPanel> self(this);
	for (SectionUi &s : sections_)
		attachCommandMenu(s.list, [self] { return self ? self->listCommands() : std::nullopt; });
	attachCommandMenu(content_, [self] { return self ? self->listCommands() : std::nullopt; });
}

} // namespace se
