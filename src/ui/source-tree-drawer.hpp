// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "core/tree-model.hpp"

#include <QSet>
#include <QWidget>

#include <string>
#include <vector>

class QLabel;
class QLineEdit;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

struct obs_source;
struct obs_scene_item;

namespace se {

// Searchable scene -> group -> item hierarchy with live show/lock toggles, plus the
// global audio devices. Selecting a row selects that scene item in OBS itself, so
// the rest of the dock follows through its normal selection tracking.
//
// GUI thread only. Like the panels it polls (500 ms, only while visible), so there
// is no cross-thread state.
class SourceTreeDrawer final : public QWidget {
	Q_OBJECT
public:
	explicit SourceTreeDrawer(QWidget *parent = nullptr);
	~SourceTreeDrawer() override;

signals:
	// A global audio device was chosen (it has no scene item behind it).
	void globalSourceChosen(obs_source *source);
	// OBS's selection was changed on the user's behalf; callers may re-sync now.
	void selectionApplied();
	// A scene item in a scene that is not live: OBS is left alone, the caller loads the item itself.
	void itemChosen(obs_scene_item *item, const QString &rootScene);

private slots:
	void poll();
	void render();
	void onItemClicked(QTreeWidgetItem *item, int column);
	void onItemDoubleClicked(QTreeWidgetItem *item, int column);
	void onItemChanged(QTreeWidgetItem *item, int column);

private:
	void refreshModel(bool force);
	void updateSceneHighlight();
	void attachRowMenu();
	void say(const QString &message); // transient status line under the tree

	QLineEdit *search_ = nullptr;
	QTreeWidget *tree_ = nullptr;
	QLabel *status_ = nullptr;
	QTimer *poll_ = nullptr;

	std::vector<TreeNode> model_;
	std::string signature_;
	QSet<QString> expanded_; // keys of expanded rows, survives rebuilds
	bool firstRender_ = true;
	bool rendering_ = false;
};

} // namespace se
