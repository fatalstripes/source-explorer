// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "core/filter-rules.hpp"
#include "core/obs-raii.hpp"

#include <QPointer>
#include <QWidget>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

class QLabel;
class QLineEdit;
class QListWidgetItem;
class QPushButton;
class QTabBar;
class QTimer;
class QToolButton;
class QVBoxLayout;

namespace se {

class FilterRow;
class ReorderListWidget;
struct CommandSet;

// Filter chain editor for a source, its parent group and its scene (one tab each). The chain is shown
// the way OBS's own dialog does: an "Audio/Video" list and an "Effect" list. Each row has an enable
// checkbox and can expand to edit the filter's properties inline or pop out into its own window.
// GUI thread only. The panel polls OBS (500 ms, only while visible) instead of subscribing to filter
// signals, so there is no cross-thread state to guard.
class FiltersPanel final : public QWidget {
	Q_OBJECT
public:
	explicit FiltersPanel(QWidget *parent = nullptr);
	~FiltersPanel() override;

	// Any argument may be null. The panel takes its own references.
	void setContext(obs_source_t *source, obs_source_t *group, obs_source_t *scene);

	// For the smoke test.
	int rowCount(FilterSection section) const;
	void expandAll(bool expanded);
	void resetLayout(); // Reset UI: reopens collapsed sections

private slots:
	void poll();
	void onTabChanged(int);
	void rebuildAddMenu();

private:
	enum Tab { SourceTab, GroupTab, SceneTab, TabCount };
	struct Entry {
		std::string name;
		std::string kind; // filter type id, e.g. "color_filter_v2"
		std::string display;
		bool enabled = true;
		uint32_t flags = 0;
		FilterSection section = FilterSection::Effect;
	};
	struct SectionUi {
		FilterSection section = FilterSection::Effect;
		QToolButton *header = nullptr;
		ReorderListWidget *list = nullptr;
		std::vector<std::string> names; // visible rows, top to bottom
	};

	obs_source_t *parent() const; // source for the active tab, may be null
	std::vector<Entry> entries(obs_source_t *parent) const;
	obs::SourceRef filterByName(const std::string &name) const;
	SectionUi &ui(FilterSection s) { return s == FilterSection::AudioVideo ? sections_[0] : sections_[1]; }
	const SectionUi &ui(FilterSection s) const { return s == FilterSection::AudioVideo ? sections_[0] : sections_[1]; }
	std::vector<FilterSection> chainSections() const;

	void rebuildList(bool force);
	void syncEnabledStates();
	void updateHeaders();
	void updateCounts();
	void updateButtons();
	void fitList(SectionUi &s);
	QListWidgetItem *itemFor(const std::string &name) const;
	void syncRowHeight(const std::string &name);
	void setExpanded(const std::string &name, bool expanded);
	void loadExpanded();
	void saveExpanded() const;
	std::string sourceKey() const;

	void select(const std::string &name);
	void onRowSelected(FilterSection section);
	void onRowMoveRequested(FilterSection section, int from, int destRow);
	void moveSelected(int delta);
	void moveFilterTo(const std::string &name, int target); // target = chain index
	void addFilter(const char *typeId);
	void removeSelected();
	void copyFilters(bool all);
	void pasteFromClipboard();
	void resetAllFilters(); // asks first: it discards every setting on the active tab's filters
	void attachMenus();
	std::optional<CommandSet> rowCommands(const std::string &name);
	std::optional<CommandSet> listCommands();
	void showMessage(const QString &text);

	obs::SourceRef sources_[TabCount];
	QTabBar *tabs_ = nullptr;
	QLineEdit *search_ = nullptr;
	SectionUi sections_[2]; // [0] Audio/Video, [1] Effect
	QPushButton *addButton_ = nullptr;
	QPushButton *copyButton_ = nullptr;
	QPushButton *pasteButton_ = nullptr;
	QPushButton *removeButton_ = nullptr;
	QPushButton *upButton_ = nullptr;
	QPushButton *downButton_ = nullptr;
	QPushButton *expandButton_ = nullptr;
	QPushButton *collapseButton_ = nullptr;
	QPushButton *resetAllButton_ = nullptr;
	QPushButton *closeWindowsButton_ = nullptr;
	QLabel *message_ = nullptr;
	QWidget *content_ = nullptr;
	QTimer *poll_ = nullptr;

	std::vector<Entry> chain_; // the active tab's filters, in OBS order
	std::map<std::string, QPointer<FilterRow>> rows_;
	std::set<std::string> expanded_; // filter names expanded on the active tab's source
	std::string selected_;
	int wantedTab_ = 0; // the tab the user last chose; used whenever it applies to the loaded source
	std::string signature_; // last rendered chain, to skip no-op rebuilds
	bool updating_ = false;
	bool upDecreasesIndex_ = true; // libobs MOVE_UP direction; verified on first move
	bool directionProbed_ = false;
};

} // namespace se
