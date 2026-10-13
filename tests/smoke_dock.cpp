// Start-up smoke test for the dock UI.
//
// Builds the real dock and every panel against the real libobs (obs.dll from an installed OBS) on
// Qt's headless "minimal" platform, with two throwaway scenes, then drives them hard:
//   - constructs and shows the dock, lets every poll timer fire, walks every rail page;
//   - opens and closes the drawer, pins a source, simulates a scene-collection change;
//   - gives each panel a real scene item, clicks every button, and opens every right-click menu.
// Any crash fails the test; so does a run that found nothing to exercise.
//
// Why it exists: the unit tests cover only the pure code, so a constructor-order bug in the Qt UI
// (a style-change event reaching a not-yet-built widget) got as far as crashing OBS on launch.
//
// Needs: QT_QPA_PLATFORM=minimal, Qt plugins on QT_PLUGIN_PATH, and OBS's bin directory on PATH.
// CMake's `smoke_dock` test sets all three. Nothing here touches the user's OBS configuration.
#include "core/obs-event-bridge.hpp"
#include "ui/context-bar.hpp"
#include "ui/explorer-dock.hpp"
#include "ui/panels/audio-panel.hpp"
#include "ui/panels/filters-panel.hpp"
#include "ui/panels/misc-panel.hpp"
#include "ui/panels/overview-panel.hpp"
#include "ui/panels/properties-panel.hpp"
#include "ui/panels/source-panel.hpp"
#include "ui/panels/transform-panel.hpp"
#include "ui/quick-access-bar.hpp"
#include "ui/source-tree-drawer.hpp"

#include <obs-frontend-api.h>
#include <obs.h>

#include <QAbstractButton>
#include <QApplication>
#include <QContextMenuEvent>
#include <QElapsedTimer>
#include <QListWidget>
#include <QThread>
#include <QTimer>

#include <cstdio>

namespace {

int g_menusOpened = 0;
int g_buttonsClicked = 0;

void stage(const char *what)
{
	std::printf("[smoke] %s\n", what);
	std::fflush(stdout); // a crash must not swallow the last line
}

void pump(int ms)
{
	QElapsedTimer t;
	t.start();
	while (t.elapsed() < ms) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		QThread::msleep(5);
	}
}

// Click every button-like widget under `root` (tool buttons, checkboxes, anchor cells, ...).
void clickEverything(QWidget *root)
{
	for (auto *b : root->findChildren<QAbstractButton *>()) {
		if (!b->isEnabled())
			continue;
		b->click();
		++g_buttonsClicked;
	}
	pump(60);
}

// Right-click every widget that has a custom context menu. menu.exec() blocks in a nested event
// loop, so a repeating timer closes whichever popup is open.
void openAllMenus(QWidget *root)
{
	QTimer closer;
	closer.setInterval(15);
	QObject::connect(&closer, &QTimer::timeout, [] {
		if (QWidget *popup = QApplication::activePopupWidget())
			popup->close();
	});
	closer.start();

	for (QWidget *w : root->findChildren<QWidget *>()) {
		if (w->contextMenuPolicy() != Qt::CustomContextMenu)
			continue;
		QContextMenuEvent ev(QContextMenuEvent::Mouse, QPoint(3, 3), w->mapToGlobal(QPoint(3, 3)));
		QCoreApplication::sendEvent(w, &ev); // runs the provider; blocks in exec() if it makes a menu
		++g_menusOpened;
	}
	closer.stop();
}

template<class Panel, class Feed> void exercise(const char *name, Feed feed)
{
	stage(name);
	Panel p;
	p.resize(600, 700);
	p.show();
	pump(120);
	feed(p);
	pump(700); // at least one poll tick (they run every 250-500 ms)
	clickEverything(&p);
	openAllMenus(&p);
	pump(60);
}

} // namespace

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", qgetenv("QT_QPA_PLATFORM").isEmpty() ? QByteArray("minimal") : qgetenv("QT_QPA_PLATFORM"));
	QApplication app(argc, argv);

	// Some buttons open popup menus (the Transform popup, right-click menus). Close any that appear.
	QTimer popupCloser;
	popupCloser.setInterval(15);
	QObject::connect(&popupCloser, &QTimer::timeout, [] {
		if (QWidget *popup = QApplication::activePopupWidget())
			popup->close();
	});
	popupCloser.start();

	stage("obs_startup");
	if (!obs_startup("en-US", nullptr, nullptr)) {
		std::fprintf(stderr, "obs_startup failed (is OBS's bin directory on PATH?)\n");
		return 2;
	}

	// Two throwaway scenes; B is an item inside A, which gives every panel a real scene item.
	obs_scene_t *sceneA = obs_scene_create("Smoke A");
	obs_scene_t *sceneB = obs_scene_create("Smoke B");
	obs_source_t *srcA = obs_scene_get_source(sceneA);
	obs_source_t *srcB = obs_scene_get_source(sceneB);
	obs_sceneitem_t *item = obs_scene_add(sceneA, srcB);
	if (!item) {
		std::fprintf(stderr, "could not add a scene item\n");
		return 2;
	}

	// ---- the whole dock ------------------------------------------------------------------
	{
		stage("construct and show the dock");
		se::ExplorerDock dock;
		dock.resize(500, 700);
		dock.show();
		pump(1200); // every poll timer fires several times with nothing selected

		stage("walk every rail page");
		if (auto *rail = dock.findChild<QListWidget *>("seRail")) {
			for (int i = 0; i < rail->count(); ++i) {
				rail->setCurrentRow(i);
				pump(150);
			}
		} else {
			std::fprintf(stderr, "rail not found: object name changed?\n");
			return 3;
		}

		stage("drawer open / close");
		QMetaObject::invokeMethod(&dock, "setDrawerOpen", Q_ARG(bool, true));
		pump(400);
		QMetaObject::invokeMethod(&dock, "setDrawerOpen", Q_ARG(bool, false));
		pump(400);

		// Both paths are driven through the dock's own public signals, exactly as OBS and the
		// drawer drive them (Qt cannot invoke a slot by name with a raw libobs pointer).
		stage("pin a source (a global audio device has no scene item)");
		auto *drawer = dock.findChild<se::SourceTreeDrawer *>();
		if (!drawer) {
			std::fprintf(stderr, "drawer not found\n");
			return 3;
		}
		drawer->globalSourceChosen(srcB);
		pump(700);

		stage("scene collection changing");
		auto *bridge = dock.findChild<se::ObsEventBridge *>();
		if (!bridge) {
			std::fprintf(stderr, "event bridge not found\n");
			return 3;
		}
		bridge->frontendEvent(int(OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING));
		pump(300);

		stage("click everything and open every menu on the dock");
		clickEverything(&dock);
		openAllMenus(&dock);
	}

	// ---- each panel on its own, fed a real item, a source, then nothing -------------------
	exercise<se::TransformPanel>("TransformPanel", [&](se::TransformPanel &p) { p.setItem(item); });
	exercise<se::OverviewPanel>("OverviewPanel (item)", [&](se::OverviewPanel &p) { p.setItem(item); });
	exercise<se::OverviewPanel>("OverviewPanel (source only)", [&](se::OverviewPanel &p) { p.setSource(srcB); });
	exercise<se::SourcePanel>("SourcePanel (item)", [&](se::SourcePanel &p) { p.setItem(item); });
	exercise<se::SourcePanel>("SourcePanel (source only)", [&](se::SourcePanel &p) { p.setSource(srcB); });
	exercise<se::MiscPanel>("MiscPanel", [&](se::MiscPanel &p) { p.setItem(item); });
	exercise<se::AudioPanel>("AudioPanel (a scene has no audio)", [&](se::AudioPanel &p) { p.setSource(srcB); });
	exercise<se::FiltersPanel>("FiltersPanel", [&](se::FiltersPanel &p) { p.setContext(srcB, nullptr, srcA); });
	exercise<se::PropertiesPanel>("PropertiesPanel", [&](se::PropertiesPanel &p) { p.setSource(srcB); });
	exercise<se::SourceTreeDrawer>("SourceTreeDrawer", [](se::SourceTreeDrawer &) {});
	exercise<se::QuickAccessBar>("QuickAccessBar (item)", [&](se::QuickAccessBar &p) { p.setTarget(item, srcB); });
	exercise<se::QuickAccessBar>("QuickAccessBar (scene, no item)", [&](se::QuickAccessBar &p) { p.setTarget(nullptr, srcB); });
	exercise<se::QuickAccessBar>("QuickAccessBar (nothing)", [&](se::QuickAccessBar &p) { p.setTarget(nullptr, nullptr); });
	exercise<se::ContextBar>("ContextBar", [](se::ContextBar &p) {
		se::ContextInfo info;
		info.scene = "Smoke A";
		info.group = "Smoke Group";
		info.source = "Smoke B";
		info.warn = true;
		info.warnText = "This source is in another scene.";
		p.setContext(info);
	});

	// Releasing a scene that sits inside another scene makes libobs itself log "Double destroy just
	// occurred". A control program using libobs alone, with none of this plugin's code, prints it
	// too, so it is not a reference-count bug here and can be ignored.
	stage("tear down");
	obs_scene_release(sceneB);
	obs_scene_release(sceneA);
	pump(50);
	obs_shutdown();

	std::printf("[smoke] clicked %d buttons, opened %d context menus\n", g_buttonsClicked, g_menusOpened);
	// Guard against a vacuous pass: a restructure that leaves nothing to click should fail loudly.
	if (g_buttonsClicked < 30 || g_menusOpened < 30) {
		std::fprintf(stderr, "too little was exercised (buttons %d, menus %d)\n", g_buttonsClicked, g_menusOpened);
		return 4;
	}
	std::puts("[smoke] OK");
	return 0;
}
