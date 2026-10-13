// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "core/obs-raii.hpp"
#include "ui/context-bar.hpp"

#include <QWidget>

#include <string>

class QBoxLayout;
class QLabel;
class QListWidget;
class QPropertyAnimation;
class QStackedWidget;
class QTimer;
class QToolButton;

namespace se {

class ObsEventBridge;
class AudioPanel;
class FiltersPanel;
class MiscPanel;
class SourcePanel;
class OverviewPanel;
class PropertiesPanel;
class QuickAccessBar;
class SourceTreeDrawer;
class TransformPanel;

// Content widget for the dock. OBS wraps it in a QDockWidget via
// obs_frontend_add_dock_by_id(), which also gives it OBS's stylesheet.
class ExplorerDock final : public QWidget {
	Q_OBJECT
	// Animated width of the slide-out source tree (fixed width, so the layout cannot squeeze it).
	Q_PROPERTY(int drawerWidth READ drawerWidth WRITE setDrawerWidth)
public:
	explicit ExplorerDock(QWidget *parent = nullptr);
	~ExplorerDock() override;

	QSize sizeHint() const override;

protected:
	void showEvent(QShowEvent *event) override;
	void changeEvent(QEvent *event) override;

public:

	int drawerWidth() const { return drawerWidth_; }
	void setDrawerWidth(int width);

	enum Page { Overview, Source, Transform, Audio, Filters, Properties, Misc, PageCount };

private slots:
	void onFrontendEvent(int event);
	void refreshIdentity();
	void syncSelection();
	void setDrawerOpen(bool open);
	// Show a source that has no scene item (global audio devices): everything except
	// Transform follows it, until the user selects a scene item again.
	void setPinnedSource(obs_source_t *source);

private:
	void updateFilterContext(obs_sceneitem_t *item, obs_source_t *editScene);
	void showSourceIdentity(obs_source_t *src);
	void updateRailWidth(); // fits the rail to its labels once OBS's font and theme are applied
	obs_source_t *currentSource() const; // the pinned device, else the selected item's source
	void updateTabVisibility();          // tabs hide when they do not apply to the current source
	// Feed every panel one scene item (or nothing).
	void applySelection(obs_sceneitem_t *item, obs_source_t *editScene);
	void updateBars(); // status bar breadcrumbs and the quick-access buttons
	QString editSceneName() const; // preview scene in Studio Mode, else the program scene
	void resetUi();
	void buildIdentityBar(QBoxLayout *root);

	ObsEventBridge *bridge_ = nullptr;
	QToolButton *drawerButton_ = nullptr;
	QLabel *iconLabel_ = nullptr;
	QToolButton *nameLabel_ = nullptr; // a button: clicking the name opens the source tree
	QLabel *typeLabel_ = nullptr;
	QLabel *badgeProgram_ = nullptr;
	QLabel *badgePreview_ = nullptr;
	QLabel *badgeMuted_ = nullptr;
	QLabel *badgeFilters_ = nullptr;
	SourceTreeDrawer *drawer_ = nullptr;
	QPropertyAnimation *drawerAnim_ = nullptr;
	int drawerWidth_ = 0;
	int windowGrowth_ = 0; // extra width given to a floating dock while the drawer is open
	QListWidget *rail_ = nullptr;
	QStackedWidget *stack_ = nullptr;
	OverviewPanel *overview_ = nullptr;
	TransformPanel *transform_ = nullptr;
	AudioPanel *audio_ = nullptr;
	FiltersPanel *filters_ = nullptr;
	SourcePanel *sourcePanel_ = nullptr;
	MiscPanel *misc_ = nullptr;
	PropertiesPanel *properties_ = nullptr;
	QTimer *selectionPoll_ = nullptr;
	ContextBar *context_ = nullptr;
	QuickAccessBar *quick_ = nullptr;
	ContextInfo ctx_; // what the status bar shows, kept so a click knows where to jump
	bool hold_ = false;           // Pin: stay on the loaded source instead of following OBS's selection
	bool allowOverride_ = false;  // one-shot: a deliberate pick (source tree) may leave a held source
	std::string rootSceneName_;   // the scene being edited when the current item was picked

	obs::SourceRef pinned_; // non-null while a global audio device is shown
	obs::SceneItemRef lastItem_; // OBS's selection as of the previous poll
};

} // namespace se
