// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "core/obs-raii.hpp"
#include "ui/context-bar.hpp"

#include <QWidget>

#include <QPointer>

#include <functional>
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
	bool eventFilter(QObject *watched, QEvent *event) override; // drag a rail tab out to detach it
	void changeEvent(QEvent *event) override;

public:

	int drawerWidth() const { return drawerWidth_; }
	void setDrawerWidth(int width);

	enum Page { Overview, Source, Transform, Audio, Filters, Properties, Misc, PageCount };

	// A tab can be moved out of the dock into its own OBS dock (right-click it in the rail). The panel
	// is the same object, so it keeps following the selection. Which tabs are detached is remembered.
	// Load a scene item the user picked in a scene that is not live. OBS's selection is untouched; the
	// dock holds the item (as if pinned) and the status bar warns that it is in another scene.
	void loadItem(obs_sceneitem_t *item, const QString &rootScene);
	void detachPage(Page page);
	void reattachPage(Page page);
	void reattachAll();
	bool isDetached(Page page) const { return detached_[page].host != nullptr; }
	void restoreDetached(); // call once the main dock is in OBS: re-detaches what was detached last time
	static QStringList detachedDockIds();

	// How a detached tab becomes an OBS dock. Defaults to obs_frontend_add_dock_by_id / remove_dock;
	// the headless smoke test substitutes plain windows.
	struct DockHost {
		std::function<bool(const QString &id, const QString &title, QWidget *content)> add;
		std::function<void(const QString &id)> remove;
	};
	static void setDockHost(DockHost host);

private slots:
	void onFrontendEvent(int event);
	void refreshIdentity();
	void syncSelection();
	void setDrawerOpen(bool open);
	// Show a source that has no scene item (global audio devices): everything except
	// Transform follows it, until the user selects a scene item again.
	void setPinnedSource(obs_source_t *source);
	void openPicker(); // the "Select a source" dialog

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

	struct Detached {
		QPointer<QWidget> host; // the widget OBS owns in the new dock
	};
	Detached detached_[PageCount];
	QWidget *pages_[PageCount] = {}; // what sits in stack_ for each page (its scroll area)
	bool reattaching_ = false;
	int railPressRow_ = -1; // rail row under the mouse button, while it is held
	bool panelsGone_ = false;        // a detached dock was destroyed under us (OBS shutting down)
	void showRailMenu(const QPoint &pos);
	void saveDetached() const;

	obs::SourceRef pinned_; // non-null while a global audio device is shown
	obs::SceneItemRef lastItem_; // OBS's selection as of the previous poll
};

} // namespace se
