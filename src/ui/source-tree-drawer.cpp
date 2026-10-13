// SPDX-License-Identifier: GPL-2.0-or-later
#include "ui/source-tree-drawer.hpp"

#include "core/obs-raii.hpp"
#include "core/payload-generator.hpp"
#include "core/tree-snapshot.hpp"
#include "ui/scene-switch.hpp"
#include "ui/widgets/command-menu.hpp"

#include <obs-frontend-api.h>
#include <obs.h>

#include <QCursor>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QPointer>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>
#include <string>

namespace se {

namespace {

enum Role {
	KeyRole = Qt::UserRole,
	SceneRole,
	IdRole,
	KindRole,
	RootRole,
};

std::string nameOf(obs_source_t *s)
{
	const char *n = s ? obs_source_get_name(s) : nullptr;
	return n ? n : "";
}

QTreeWidgetItem *addNode(QTreeWidget *tree, QTreeWidgetItem *parent, const TreeNode &n, const QSet<QString> &expanded,
			 bool expandAll)
{
	auto *it = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
	it->setText(0, QString::fromStdString(n.label));
	it->setData(0, KeyRole, QString::fromStdString(n.key));
	it->setData(0, SceneRole, QString::fromStdString(n.scene));
	it->setData(0, IdRole, n.itemId);
	it->setData(0, KindRole, int(n.kind));
	it->setData(0, RootRole, QString::fromStdString(n.rootScene));

	if (n.kind == NodeKind::Item || n.kind == NodeKind::Group) {
		it->setCheckState(1, n.visible ? Qt::Checked : Qt::Unchecked);
		it->setCheckState(2, n.locked ? Qt::Checked : Qt::Unchecked);
		it->setToolTip(1, QObject::tr("Visible"));
		it->setToolTip(2, QObject::tr("Locked"));
	}
	for (const TreeNode &c : n.children)
		addNode(tree, it, c, expanded, expandAll);
	if (!n.children.empty())
		it->setExpanded(expandAll || expanded.contains(QString::fromStdString(n.key)));
	return it;
}

NodeKind kindOf(const QTreeWidgetItem *it)
{
	return static_cast<NodeKind>(it->data(0, KindRole).toInt());
}

} // namespace

// ---------------------------------------------------------------------------
SourceTreeDrawer::SourceTreeDrawer(QWidget *parent) : QWidget(parent)
{
	setObjectName("seSourceTree");
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 0);

	search_ = new QLineEdit(this);
	search_->setPlaceholderText(tr("Search scenes and sources"));
	search_->setClearButtonEnabled(true);
	root->addWidget(search_);

	auto *tools = new QHBoxLayout;
	// Short labels: the drawer is narrow. The tooltips carry the full wording.
	auto *expand = new QPushButton(tr("Expand"), this);
	expand->setToolTip(tr("Expand all"));
	auto *collapse = new QPushButton(tr("Collapse"), this);
	collapse->setToolTip(tr("Collapse all"));
	auto *reload = new QPushButton(tr("Refresh"), this);
	reload->setToolTip(tr("Read the scenes and sources again"));
	tools->addWidget(expand);
	tools->addWidget(collapse);
	tools->addWidget(reload);
	tools->addStretch(1);
	root->addLayout(tools);

	tree_ = new QTreeWidget(this);
	tree_->setColumnCount(3);
	tree_->setHeaderLabels({tr("Name"), tr("Show"), tr("Lock")});
	tree_->header()->setStretchLastSection(false);
	tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	tree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	tree_->setSelectionMode(QAbstractItemView::SingleSelection);
	tree_->setExpandsOnDoubleClick(false); // double-click switches scene instead
	root->addWidget(tree_, 1);

	status_ = new QLabel(this);
	status_->setWordWrap(true);
	status_->setVisible(false);
	root->addWidget(status_);

	connect(search_, &QLineEdit::textChanged, this, &SourceTreeDrawer::render);
	connect(expand, &QPushButton::clicked, tree_, &QTreeWidget::expandAll);
	connect(collapse, &QPushButton::clicked, tree_, &QTreeWidget::collapseAll);
	connect(reload, &QPushButton::clicked, this, [this] { refreshModel(true); });
	connect(tree_, &QTreeWidget::itemClicked, this, &SourceTreeDrawer::onItemClicked);
	connect(tree_, &QTreeWidget::itemDoubleClicked, this, &SourceTreeDrawer::onItemDoubleClicked);
	connect(tree_, &QTreeWidget::itemChanged, this, &SourceTreeDrawer::onItemChanged);

	// Remember what the user expanded; ignore programmatic changes and search mode.
	auto remember = [this](QTreeWidgetItem *it, bool open) {
		if (rendering_ || !search_->text().trimmed().isEmpty())
			return;
		const QString key = it->data(0, KeyRole).toString();
		if (open)
			expanded_.insert(key);
		else
			expanded_.remove(key);
	};
	connect(tree_, &QTreeWidget::itemExpanded, this, [remember](QTreeWidgetItem *it) { remember(it, true); });
	connect(tree_, &QTreeWidget::itemCollapsed, this, [remember](QTreeWidgetItem *it) { remember(it, false); });

	attachRowMenu();

	poll_ = new QTimer(this);
	poll_->setInterval(500);
	connect(poll_, &QTimer::timeout, this, &SourceTreeDrawer::poll);
	poll_->start();
}

SourceTreeDrawer::~SourceTreeDrawer() = default;

// ---------------------------------------------------------------------------
// Model refresh + rendering
// ---------------------------------------------------------------------------
void SourceTreeDrawer::poll()
{
	if (!isVisible())
		return;
	refreshModel(false);
	updateSceneHighlight();
}

void SourceTreeDrawer::refreshModel(bool force)
{
	model_ = snapshotTree(false);
	std::string sig = treeSignature(model_);
	if (!force && sig == signature_)
		return;
	signature_ = std::move(sig);
	render();
}

void SourceTreeDrawer::render()
{
	const QString keep = tree_->currentItem() ? tree_->currentItem()->data(0, KeyRole).toString() : QString();
	const bool searching = !search_->text().trimmed().isEmpty();

	if (firstRender_) {
		// Start with the scene being edited open; everything else collapsed.
		if (obs::SourceRef s = editScene())
			expanded_.insert(QString::fromStdString("s:" + nameOf(s.get())));
		firstRender_ = false;
	}

	rendering_ = true;
	tree_->setUpdatesEnabled(false);
	tree_->clear();
	const auto shown = filterTree(model_, search_->text().toStdString());
	QTreeWidgetItem *toSelect = nullptr;
	for (const TreeNode &n : shown)
		addNode(tree_, nullptr, n, expanded_, searching);
	if (!keep.isEmpty()) {
		QTreeWidgetItemIterator it(tree_);
		for (; *it; ++it) {
			if ((*it)->data(0, KeyRole).toString() == keep) {
				toSelect = *it;
				break;
			}
		}
	}
	if (toSelect)
		tree_->setCurrentItem(toSelect);
	tree_->setUpdatesEnabled(true);
	rendering_ = false;
	updateSceneHighlight();
	applyCurrentHighlight();
}

void SourceTreeDrawer::setCurrent(const QString &sourceName)
{
	if (sourceName == current_)
		return;
	current_ = sourceName;
	applyCurrentHighlight();
}

void SourceTreeDrawer::applyCurrentHighlight()
{
	rendering_ = true; // colour changes emit itemChanged
	for (QTreeWidgetItemIterator it(tree_); *it; ++it) {
		const bool on = !current_.isEmpty() && kindOf(*it) != NodeKind::Section && (*it)->text(0) == current_;
		const QBrush want = on ? QBrush(QColor(76, 175, 80)) : QBrush();
		if ((*it)->foreground(0) != want)
			(*it)->setForeground(0, want);
	}
	rendering_ = false;
}

void SourceTreeDrawer::updateSceneHighlight()
{
	obs::SourceRef edit = editScene();
	const QString name = edit ? QString::fromStdString(nameOf(edit.get())) : QString();
	rendering_ = true; // font changes emit itemChanged
	for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
		auto *it = tree_->topLevelItem(i);
		if (kindOf(it) != NodeKind::Scene)
			continue;
		const bool on = it->text(0) == name;
		QFont f = it->font(0);
		if (f.bold() != on) {
			f.setBold(on);
			it->setFont(0, f);
		}
	}
	rendering_ = false;
}

void SourceTreeDrawer::say(const QString &message)
{
	status_->setText(message);
	status_->setVisible(true);
	QPointer<SourceTreeDrawer> self(this);
	QTimer::singleShot(5000, this, [self, message] {
		if (self && self->status_->text() == message)
			self->status_->setVisible(false);
	});
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------
void SourceTreeDrawer::onItemClicked(QTreeWidgetItem *item, int column)
{
	if (column != 0 || !item)
		return;

	switch (kindOf(item)) {
	case NodeKind::GlobalAudio: {
		obs::SourceRef src = obs::sourceByName(item->text(0).toUtf8().constData());
		if (src)
			emit globalSourceChosen(src.get()); // receiver retains what it keeps
		return;
	}
	case NodeKind::Item:
	case NodeKind::Group:
		break;
	default:
		return; // scene and section rows: single click does nothing (double-click switches)
	}

	const SelectResult r = selectSceneItem(item->data(0, RootRole).toString().toStdString(),
					       item->data(0, SceneRole).toString().toStdString(),
					       item->data(0, IdRole).toLongLong());
	if (r.loadOnly)
		emit itemChosen(r.item.get(), QString::fromStdString(r.rootScene));
	else if (!r.message.empty())
		say(tr("%1").arg(QString::fromStdString(r.message)));
	else if (r.selected)
		emit selectionApplied();
}

void SourceTreeDrawer::onItemDoubleClicked(QTreeWidgetItem *item, int column)
{
	if (!item || column != 0 || kindOf(item) != NodeKind::Scene)
		return;
	obs::SourceRef scene = obs::sourceByName(item->text(0).toUtf8().constData());
	if (!scene)
		return;

	if (requestSceneSwitch(this, scene.get()))
		emit selectionApplied();
}

void SourceTreeDrawer::onItemChanged(QTreeWidgetItem *item, int column)
{
	if (rendering_ || !item || (column != 1 && column != 2))
		return;
	const NodeKind k = kindOf(item);
	if (k != NodeKind::Item && k != NodeKind::Group)
		return;

	auto target = findSceneItem(item->data(0, SceneRole).toString().toStdString(), item->data(0, IdRole).toLongLong());
	if (!target)
		return;
	const bool on = item->checkState(column) == Qt::Checked;
	if (column == 1)
		obs_sceneitem_set_visible(target.get(), on);
	else
		obs_sceneitem_set_locked(target.get(), on);

	// The tree already shows the new state; don't let the next poll rebuild it.
	model_ = snapshotTree(false);
	signature_ = treeSignature(model_);
}

// ---------------------------------------------------------------------------
// Command copier: right-click on a row
// ---------------------------------------------------------------------------
void SourceTreeDrawer::attachRowMenu()
{
	QPointer<SourceTreeDrawer> self(this);
	attachCommandMenu(tree_, [self]() -> std::optional<CommandSet> {
		if (!self)
			return std::nullopt;
		QTreeWidgetItem *item = self->tree_->itemAt(self->tree_->viewport()->mapFromGlobal(QCursor::pos()));
		if (!item)
			return std::nullopt;

		CommandSet c;
		switch (kindOf(item)) {
		case NodeKind::Scene: {
			const std::string name = item->text(0).toStdString();
			const ObsRequest set = reqSetCurrentProgramScene(name);
			const ObsRequest get = reqGetCurrentProgramScene();
			c.streamerBot = [set] { return sbFrom(set); };
			c.wsSet = [set] { return wsFrom(set); };
			c.wsGet = [get] { return wsFrom(get); };
			return c;
		}
		case NodeKind::Item:
		case NodeKind::Group: {
			// Group children are addressed through the group's name, as obs-websocket expects.
			const SceneItemTarget t{item->data(0, SceneRole).toString().toStdString(),
						item->data(0, IdRole).toLongLong()};
			const bool visible = item->checkState(1) == Qt::Checked;
			const bool locked = item->checkState(2) == Qt::Checked;
			const ObsRequest setVis = reqSetSceneItemEnabled(t, visible);
			const ObsRequest getVis = reqGetSceneItemEnabled(t);
			const ObsRequest setLock = reqSetSceneItemLocked(t, locked);
			const ObsRequest getLock = reqGetSceneItemLocked(t);
			c.streamerBot = [setVis] { return sbFrom(setVis); };
			c.wsSet = [setVis] { return wsFrom(setVis); };
			c.wsGet = [getVis] { return wsFrom(getVis); };
			c.extras = {
				{"Copy Streamer.Bot C# (Set Locked)", [setLock] { return sbFrom(setLock); }},
				{"Copy OBS WebSocket v5 (Set Locked)", [setLock] { return wsFrom(setLock); }},
				{"Copy OBS WebSocket v5 (Get Locked)", [getLock] { return wsFrom(getLock); }},
			};
			// Default state: shown and unlocked. The menu runs a nested event loop during
			// which the tree may be rebuilt, so resolve the item from OBS, never from `item`.
			c.reset = [self, t] {
				if (!self)
					return;
				if (auto it = findSceneItem(t.sceneName, t.sceneItemId)) {
					obs_sceneitem_set_visible(it.get(), true);
					obs_sceneitem_set_locked(it.get(), false);
				}
				self->refreshModel(true);
			};
			return c;
		}
		default:
			return std::nullopt;
		}
	});
}

} // namespace se
