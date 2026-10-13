// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "core/obs-raii.hpp"

#include <QWidget>

#include <string>
#include <vector>

class QFormLayout;
class QPushButton;
class QTabBar;
class QTimer;
class QToolButton;

namespace se {

class ReorderListWidget;

// Filter chain editor for a source, its parent group and its scene (one tab each).
// GUI thread only. The panel polls OBS (500 ms, only while visible) instead of
// subscribing to filter signals, so there is no cross-thread state to guard.
class FiltersPanel final : public QWidget {
	Q_OBJECT
public:
	explicit FiltersPanel(QWidget *parent = nullptr);
	~FiltersPanel() override;

	// Any argument may be null. The panel takes its own references.
	void setContext(obs_source_t *source, obs_source_t *group, obs_source_t *scene);

private slots:
	void poll();
	void onTabChanged(int);
	void onRowMoveRequested(int from, int destRow);
	void onSelectionChanged();
	void rebuildAddMenu();

private:
	enum Tab { SourceTab, GroupTab, SceneTab, TabCount };
	struct Entry {
		std::string name;
		std::string kind; // filter type id, e.g. "color_filter_v2"
		bool enabled = true;
	};

	obs_source_t *parent() const; // source for the active tab, may be null
	std::vector<Entry> entries(obs_source_t *parent) const;
	std::string selectedName() const;
	obs::SourceRef filterByName(const std::string &name) const;

	void rebuildList(bool force);
	void rebuildEditor();
	void refreshEditorValues();
	void moveFilterTo(const std::string &name, int target);
	void addFilter(const char *typeId);
	void removeSelected();
	void resetAllFilters(); // asks first: it discards every setting on the active tab's filters
	void attachRowMenu();

	obs::SourceRef sources_[TabCount];
	QTabBar *tabs_ = nullptr;
	ReorderListWidget *list_ = nullptr;
	QToolButton *addButton_ = nullptr;
	QPushButton *removeButton_ = nullptr;
	QPushButton *resetAllButton_ = nullptr;
	QWidget *editorHost_ = nullptr;
	QFormLayout *editorForm_ = nullptr;
	QTimer *poll_ = nullptr;

	std::string signature_;       // last rendered chain, to skip no-op rebuilds
	bool updating_ = false;
	bool upDecreasesIndex_ = true; // libobs MOVE_UP direction; verified on first move
	bool directionProbed_ = false;

	struct EditorField;
	std::vector<EditorField> editorFields_; // complete type in the .cpp
};

} // namespace se
