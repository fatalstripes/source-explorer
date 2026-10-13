// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "core/tree-model.hpp"

#include <QDialog>
#include <QString>

#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

struct obs_source;

namespace se {

// "Select a source" dialog: the whole scene / group / source hierarchy with a name search and
// filters (source type, filter type, visible only, live only, scenes-only). Pick a row and press OK
// (or double-click it) to load it in the dock. The source that is already loaded is shown in green.
// GUI thread only; the tree is read once and again on Refresh, not polled.
class SourcePicker final : public QDialog {
	Q_OBJECT
public:
	explicit SourcePicker(QWidget *parent = nullptr);
	~SourcePicker() override;

	// Highlights every row for this source and selects the first. Empty clears.
	void setCurrent(const QString &sourceName);
	void refresh();

	// What OK would do for the current row; false when the row cannot be loaded (a section header).
	bool canLoad() const;
	// Loads the current row exactly as OK does and returns true if the dialog should close.
	bool loadCurrent();

signals:
	// A scene or a global audio device was chosen: it has no scene item to select.
	void sourceChosen(obs_source *source);
	// A scene item was selected in OBS itself; callers may re-sync now.
	void selectionApplied();

private:
	void render();
	void updateFilterChoices();
	void updateButtons();
	TreeFilter currentFilter() const;

	QLineEdit *search_ = nullptr;
	QComboBox *type_ = nullptr;
	QComboBox *filterType_ = nullptr;
	QCheckBox *visibleOnly_ = nullptr;
	QCheckBox *activeOnly_ = nullptr;
	QCheckBox *showSources_ = nullptr;
	QTreeWidget *tree_ = nullptr;
	QLabel *status_ = nullptr;
	QPushButton *ok_ = nullptr;

	std::vector<TreeNode> model_;
	QString current_;
	bool rendering_ = false;
};

} // namespace se
