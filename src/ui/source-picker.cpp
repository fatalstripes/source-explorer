// SPDX-License-Identifier: GPL-2.0-or-later
#include "ui/source-picker.hpp"

#include "core/obs-raii.hpp"
#include "core/tree-snapshot.hpp"
#include "ui/dock-style.hpp"

#include <obs.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <set>

namespace se {

namespace {

enum Role { KindRole = Qt::UserRole, RootRole, SceneRole, IdRole };

const QColor kCurrentGreen(76, 175, 80);

NodeKind kindOf(const QTreeWidgetItem *it)
{
	return static_cast<NodeKind>(it->data(0, KindRole).toInt());
}

QString typeName(const std::string &typeId)
{
	if (typeId == "scene")
		return QObject::tr("Scene");
	if (typeId == "group")
		return QObject::tr("Group");
	const char *n = obs_source_get_display_name(typeId.c_str());
	return n ? QString::fromUtf8(n) : QString::fromStdString(typeId);
}

void collectTypes(const std::vector<TreeNode> &nodes, std::set<std::string> &types, std::set<std::string> &filters)
{
	for (const TreeNode &n : nodes) {
		if (!n.typeId.empty())
			types.insert(n.typeId);
		for (const std::string &f : n.filterIds)
			filters.insert(f);
		collectTypes(n.children, types, filters);
	}
}

// Keeps what was chosen in a combo box across a refill.
void refill(QComboBox *box, const QString &any, const std::set<std::string> &ids)
{
	const QString keep = box->currentData().toString();
	std::vector<std::pair<QString, QString>> entries; // display, id
	for (const std::string &id : ids)
		entries.emplace_back(typeName(id), QString::fromStdString(id));
	std::sort(entries.begin(), entries.end(),
		  [](const auto &a, const auto &b) { return a.first.localeAwareCompare(b.first) < 0; });

	QSignalBlocker b(box);
	box->clear();
	box->addItem(any, QString());
	for (const auto &e : entries)
		box->addItem(e.first, e.second);
	const int i = box->findData(keep);
	box->setCurrentIndex(i >= 0 ? i : 0);
}

} // namespace

SourcePicker::SourcePicker(QWidget *parent) : QDialog(parent)
{
	setObjectName("seSourcePicker");
	setWindowTitle(tr("Select a source"));
	setStyleSheet(dockStyleSheet());
	resize(560, 640);

	auto *root = new QVBoxLayout(this);

	search_ = new QLineEdit(this);
	search_->setPlaceholderText(tr("Search scenes and sources"));
	search_->setClearButtonEnabled(true);
	root->addWidget(search_);

	auto *filters = new QHBoxLayout;
	type_ = new QComboBox(this);
	type_->setToolTip(tr("Only show sources of this type"));
	filterType_ = new QComboBox(this);
	filterType_->setToolTip(tr("Only show sources that have this kind of filter"));
	filters->addWidget(type_, 1);
	filters->addWidget(filterType_, 1);
	root->addLayout(filters);

	auto *toggles = new QHBoxLayout;
	visibleOnly_ = new QCheckBox(tr("Visible only"), this);
	visibleOnly_->setToolTip(tr("Hide sources that are switched off in their scene"));
	activeOnly_ = new QCheckBox(tr("Live only"), this);
	activeOnly_->setToolTip(tr("Only sources that are on program or showing right now"));
	showSources_ = new QCheckBox(tr("Show sources"), this);
	showSources_->setChecked(true);
	showSources_->setToolTip(tr("Untick to list only scenes and groups"));
	toggles->addWidget(visibleOnly_);
	toggles->addWidget(activeOnly_);
	toggles->addWidget(showSources_);
	toggles->addStretch(1);
	auto *expand = new QPushButton(tr("Expand all"), this);
	auto *collapse = new QPushButton(tr("Collapse all"), this);
	auto *reload = new QPushButton(tr("Refresh"), this);
	toggles->addWidget(expand);
	toggles->addWidget(collapse);
	toggles->addWidget(reload);
	root->addLayout(toggles);

	tree_ = new QTreeWidget(this);
	tree_->setColumnCount(2);
	tree_->setHeaderLabels({tr("Name"), tr("Type")});
	tree_->header()->setStretchLastSection(false);
	tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	tree_->setSelectionMode(QAbstractItemView::SingleSelection);
	tree_->setExpandsOnDoubleClick(false); // double-click loads the row
	root->addWidget(tree_, 1);

	status_ = new QLabel(this);
	status_->setWordWrap(true);
	status_->setVisible(false);
	root->addWidget(status_);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	ok_ = buttons->button(QDialogButtonBox::Ok);
	root->addWidget(buttons);

	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(buttons, &QDialogButtonBox::accepted, this, [this] {
		if (loadCurrent())
			accept();
	});
	connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *, int) {
		if (loadCurrent())
			accept();
	});
	connect(tree_, &QTreeWidget::currentItemChanged, this, [this] { updateButtons(); });
	connect(search_, &QLineEdit::textChanged, this, [this] { render(); });
	connect(type_, &QComboBox::currentIndexChanged, this, [this] { render(); });
	connect(filterType_, &QComboBox::currentIndexChanged, this, [this] { render(); });
	connect(visibleOnly_, &QCheckBox::toggled, this, [this] { render(); });
	connect(activeOnly_, &QCheckBox::toggled, this, [this] { render(); });
	connect(showSources_, &QCheckBox::toggled, this, [this] { render(); });
	connect(expand, &QPushButton::clicked, tree_, &QTreeWidget::expandAll);
	connect(collapse, &QPushButton::clicked, tree_, &QTreeWidget::collapseAll);
	connect(reload, &QPushButton::clicked, this, [this] { refresh(); });

	refresh();
}

SourcePicker::~SourcePicker() = default;

void SourcePicker::setCurrent(const QString &sourceName)
{
	current_ = sourceName;
	render();
}

void SourcePicker::refresh()
{
	model_ = snapshotTree(true);
	updateFilterChoices();
	render();
}

void SourcePicker::updateFilterChoices()
{
	std::set<std::string> types, filters;
	collectTypes(model_, types, filters);
	refill(type_, tr("All source types"), types);
	refill(filterType_, tr("Any filter"), filters);
}

TreeFilter SourcePicker::currentFilter() const
{
	TreeFilter f;
	f.query = search_->text().toStdString();
	f.typeId = type_->currentData().toString().toStdString();
	f.filterId = filterType_->currentData().toString().toStdString();
	f.visibleOnly = visibleOnly_->isChecked();
	f.activeOnly = activeOnly_->isChecked();
	f.showSources = showSources_->isChecked();
	return f;
}

void SourcePicker::render()
{
	if (rendering_)
		return;
	rendering_ = true;

	const TreeFilter filter = currentFilter();
	const bool narrowed = filter.restrictsItems() || !filter.showSources || !filter.query.empty();
	// Without a filter, open only the scene being edited and any scene holding the current source.
	QString editName;
	if (obs::SourceRef edit = editScene())
		editName = QString::fromUtf8(obs_source_get_name(edit.get()));

	tree_->setUpdatesEnabled(false);
	tree_->clear();

	QTreeWidgetItem *toSelect = nullptr;
	std::function<bool(const TreeNode &, QTreeWidgetItem *)> add = [&](const TreeNode &n, QTreeWidgetItem *parent) {
		auto *it = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree_);
		const QString label = QString::fromStdString(n.label);
		it->setText(0, label);
		if (!n.typeId.empty())
			it->setText(1, typeName(n.typeId));
		it->setData(0, KindRole, int(n.kind));
		it->setData(0, RootRole, QString::fromStdString(n.rootScene));
		it->setData(0, SceneRole, QString::fromStdString(n.scene));
		it->setData(0, IdRole, n.itemId);

		const bool isCurrent = !current_.isEmpty() && n.kind != NodeKind::Section && label == current_;
		if (isCurrent) {
			it->setForeground(0, kCurrentGreen);
			QFont f = it->font(0);
			f.setBold(true);
			it->setFont(0, f);
			if (!toSelect)
				toSelect = it;
		}
		bool holdsCurrent = isCurrent;
		for (const TreeNode &c : n.children)
			holdsCurrent = add(c, it) || holdsCurrent;
		if (!n.children.empty()) {
			const bool open = narrowed || holdsCurrent || label == editName;
			it->setExpanded(open);
		}
		return holdsCurrent;
	};
	for (const TreeNode &n : applyTreeFilter(model_, filter))
		add(n, nullptr);

	if (toSelect) {
		tree_->setCurrentItem(toSelect);
		tree_->scrollToItem(toSelect);
	}
	tree_->setUpdatesEnabled(true);
	rendering_ = false;
	updateButtons();
}

bool SourcePicker::canLoad() const
{
	const QTreeWidgetItem *it = tree_->currentItem();
	return it && kindOf(it) != NodeKind::Section;
}

void SourcePicker::updateButtons()
{
	ok_->setEnabled(canLoad());
}

bool SourcePicker::loadCurrent()
{
	QTreeWidgetItem *it = tree_->currentItem();
	if (!it)
		return false;

	switch (kindOf(it)) {
	case NodeKind::Scene:
	case NodeKind::GlobalAudio: {
		obs::SourceRef src = obs::sourceByName(it->text(0).toUtf8().constData());
		if (!src)
			return false;
		emit sourceChosen(src.get()); // the receiver retains what it keeps
		return true;
	}
	case NodeKind::Item:
	case NodeKind::Group: {
		const SelectResult r = selectSceneItem(it->data(0, RootRole).toString().toStdString(),
						       it->data(0, SceneRole).toString().toStdString(),
						       it->data(0, IdRole).toLongLong());
		if (!r.message.empty()) {
			status_->setText(QString::fromStdString(r.message));
			status_->setVisible(true);
			return false; // stay open: the user can pick something else
		}
		if (r.loadOnly) {
			emit itemChosen(r.item.get(), QString::fromStdString(r.rootScene)); // receiver retains
			return true;
		}
		if (!r.selected)
			return false;
		emit selectionApplied();
		return true;
	}
	default:
		return false;
	}
}

} // namespace se
