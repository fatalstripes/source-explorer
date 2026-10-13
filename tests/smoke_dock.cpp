// SPDX-License-Identifier: GPL-2.0-or-later
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
#include "ui/filter-clipboard.hpp"
#include "ui/filter-popouts.hpp"
#include "ui/panels/audio-panel.hpp"
#include "ui/panels/filters-panel.hpp"
#include "ui/panels/misc-panel.hpp"
#include "ui/panels/overview-panel.hpp"
#include "ui/panels/properties-panel.hpp"
#include "ui/panels/source-panel.hpp"
#include "ui/panels/transform-panel.hpp"
#include "ui/quick-access-bar.hpp"
#include "ui/source-picker.hpp"
#include "ui/source-tree-drawer.hpp"
#include "util/plugin-config.hpp"

#include <obs-frontend-api.h>
#include <obs.h>
#include <util/bmem.h>

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QSettings>
#include <QTabBar>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include <QPointer>

#include <cstdio>
#include <map>

namespace {

int g_menusOpened = 0;
int g_buttonsClicked = 0;
int g_failures = 0;

void expect(bool ok, const char *what)
{
	if (!ok) {
		++g_failures;
		std::fprintf(stderr, "[smoke] FAILED: %s\n", what);
	}
}

// Test-only source types, so the filter UI has real filters to work on without loading OBS's plugins.
// "smoke_fx" is an effect filter, "smoke_av" an audio filter (libobs marks those async), "smoke_mic"
// an audio input to put audio filters on.
void registerTestSources()
{
	auto props = [](void *) -> obs_properties_t * {
		obs_properties_t *p = obs_properties_create();
		obs_properties_add_bool(p, "on", "Smoke On");
		obs_properties_add_int_slider(p, "amount", "Smoke Amount", 0, 100, 1);
		obs_properties_add_text(p, "label", "Smoke Label", OBS_TEXT_DEFAULT);
		return p;
	};
	auto create = [](obs_data_t *, obs_source_t *) -> void * { return bzalloc(1); };
	auto destroy = [](void *p) { bfree(p); };

	obs_source_info fx = {};
	fx.id = "smoke_fx";
	fx.type = OBS_SOURCE_TYPE_FILTER;
	fx.output_flags = OBS_SOURCE_VIDEO;
	fx.get_name = [](void *) { return "Smoke Effect"; };
	fx.create = create;
	fx.destroy = destroy;
	fx.get_properties = props;
	obs_register_source(&fx);

	obs_source_info av = fx;
	av.id = "smoke_av";
	av.output_flags = OBS_SOURCE_AUDIO;
	av.get_name = [](void *) { return "Smoke Audio"; };
	obs_register_source(&av);

	obs_source_info mic = {};
	mic.id = "smoke_mic";
	mic.type = OBS_SOURCE_TYPE_INPUT;
	mic.output_flags = OBS_SOURCE_AUDIO;
	mic.get_name = [](void *) { return "Smoke Mic"; };
	mic.create = create;
	mic.destroy = destroy;
	obs_register_source(&mic);
}

bool addTestFilter(obs_source_t *parent, const char *kind, const char *name)
{
	obs_source_t *f = obs_source_create_private(kind, name, nullptr);
	if (!f)
		return false;
	obs_source_filter_add(parent, f);
	obs_source_release(f);
	return true;
}

bool hasFilter(obs_source_t *parent, const char *name)
{
	obs_source_t *f = obs_source_get_filter_by_name(parent, name);
	obs_source_release(f);
	return f != nullptr;
}

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
		// There is no running event loop here, so deleteLater() would otherwise never be honoured.
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
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
		if (QWidget *modal = QApplication::activeModalWidget())
			modal->close();
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

	// Start from an empty plugin config folder so a previous run cannot change what is shown.
	if (const QString dir = qEnvironmentVariable("SE_CONFIG_DIR"); !dir.isEmpty())
		QDir(dir).removeRecursively();

	// Some buttons open popup menus (the Transform popup, right-click menus). Close any that appear.
	QTimer popupCloser;
	popupCloser.setInterval(15);
	QObject::connect(&popupCloser, &QTimer::timeout, [] {
		if (QWidget *popup = QApplication::activePopupWidget())
			popup->close();
		// "Reset all" asks first; closing the dialog answers "no".
		if (QWidget *modal = QApplication::activeModalWidget())
			modal->close();
	});
	popupCloser.start();

	stage("obs_startup");
	if (!obs_startup("en-US", nullptr, nullptr)) {
		std::fprintf(stderr, "obs_startup failed (is OBS's bin directory on PATH?)\n");
		return 2;
	}

	registerTestSources();

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

		stage("load an item from a non-live scene");
		dock.loadItem(item, "Smoke A");
		pump(400);
		if (auto *qb = dock.findChild<se::QuickAccessBar *>())
			expect(qb->pinned(), "a directly loaded item is held (pinned)");

		stage("detach and reattach tabs");
		{
			// Stand-in for OBS's dock machinery: each detached tab becomes a plain window.
			std::map<QString, QPointer<QWidget>> hosts;
			se::ExplorerDock::setDockHost({[&](const QString &id, const QString &, QWidget *w) {
							       w->setWindowFlag(Qt::Window);
							       w->show();
							       hosts[id] = w;
							       return true;
						       },
						       [&](const QString &id) {
							       delete hosts[id].data();
							       hosts.erase(id);
						       }});
			auto *rail = dock.findChild<QListWidget *>("seRail");
			using P = se::ExplorerDock;

			dock.detachPage(P::Transform);
			dock.detachPage(P::Filters);
			pump(300);
			expect(dock.isDetached(P::Transform) && dock.isDetached(P::Filters), "tabs report detached");
			expect(hosts.size() == 2, "each detached tab got its own dock");
			expect(rail->item(P::Transform)->isHidden() && rail->item(P::Filters)->isHidden(),
			       "detached tabs leave the rail");
			se::uiSettings().sync();
			expect(se::uiSettings().value("tabs/detached").toStringList().size() == 2,
			       "detached tabs are remembered");
			expect(!hosts["source-explorer-tab-filters"].isNull() &&
				       hosts["source-explorer-tab-filters"]->findChild<se::FiltersPanel *>() != nullptr,
			       "the Filters panel moved into its dock");
			if (auto *fp = hosts["source-explorer-tab-filters"]->findChild<se::FiltersPanel *>())
				expect(fp->isVisible() && fp->height() > 100, "the detached panel is actually showing");

			dock.reattachPage(P::Transform);
			pump(200);
			expect(!dock.isDetached(P::Transform) && hosts.size() == 1, "reattach removes the dock");
			expect(dock.findChild<se::TransformPanel *>() != nullptr, "the panel is back in the main dock");

			// A new dock (as after a restart) restores what was left detached.
			{
				se::ExplorerDock again;
				again.restoreDetached();
				pump(200);
				expect(again.isDetached(P::Filters) && !again.isDetached(P::Transform),
				       "detached tabs are restored on the next start");
				again.reattachAll();
				pump(100);
			}

			dock.reattachAll();
			pump(100);
			expect(hosts.empty(), "reattach all closes every detached dock");

			// A refused dock (no OBS) leaves the tab where it was.
			se::ExplorerDock::setDockHost({[](const QString &, const QString &, QWidget *) { return false; },
						       [](const QString &) {}});
			dock.detachPage(P::Audio);
			expect(!dock.isDetached(P::Audio) && dock.findChild<se::AudioPanel *>() != nullptr,
			       "a failed detach changes nothing");
		}

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

	// ---- the Filters tab, with real filters ------------------------------------------------
	{
		stage("filters: sections, search, inline editors, persistence");
		obs_source_t *mic = obs_source_create_private("smoke_mic", "Smoke Mic", nullptr);
		expect(mic != nullptr, "test audio input could be created");
		expect(addTestFilter(srcB, "smoke_fx", "Look A") && addTestFilter(srcB, "smoke_fx", "Look B"),
		       "effect filters added to the scene");
		expect(addTestFilter(mic, "smoke_av", "Gain") && addTestFilter(mic, "smoke_av", "Gate"),
		       "audio filters added to the input");
		expect((obs_get_source_output_flags("smoke_av") & OBS_SOURCE_ASYNC) != 0,
		       "libobs marks audio filters async (the section rule depends on it)");

		{
			// The panel reopens on the last tab used; make that the Source tab for these checks.
			se::uiSettings().setValue("filters/lastTab", 0);
			se::FiltersPanel p;
			p.resize(500, 700);
			p.show();
			p.setContext(srcB, nullptr, srcA);
			pump(700);
			expect(p.rowCount(se::FilterSection::Effect) == 2, "scene shows 2 effect rows");
			expect(p.rowCount(se::FilterSection::AudioVideo) == 0, "scene shows no audio/video rows");

			auto *search = p.findChild<QLineEdit *>();
			expect(search != nullptr, "search box exists");
			if (search) {
				search->setText("zzz");
				expect(p.rowCount(se::FilterSection::Effect) == 0, "search with no match hides every row");
				search->setText("look a");
				expect(p.rowCount(se::FilterSection::Effect) == 1, "search narrows to one row");
				search->setText("smoke effect"); // matches the filter TYPE name too
				expect(p.rowCount(se::FilterSection::Effect) == 2, "search matches the type name");
				search->clear();
				expect(p.rowCount(se::FilterSection::Effect) == 2, "clearing search restores rows");
			}

			p.expandAll(true);
			pump(900);
			for (auto *l : p.findChildren<QListWidget *>())
				if (l->count() > 0)
					expect(l->item(0)->sizeHint().height() > 120 && l->height() > 240,
					       "an expanded row (and its list) grows to fit the editor");
			expect(p.findChildren<se::PropertiesPanel *>().size() == 2, "each expanded row hosts an inline editor");
			expect(p.findChildren<QCheckBox *>().size() >= 4, "inline editors render their properties");
			se::uiSettings().sync();
			const QString key = QString("filters/expanded/%1").arg(obs_source_get_uuid(srcB));
			expect(se::uiSettings().value(key).toStringList().size() == 2, "expanded rows are remembered");
			p.expandAll(false);
			pump(300);
			expect(p.findChildren<se::PropertiesPanel *>().isEmpty(), "collapsing removes the inline editors");
			expect(se::uiSettings().value(key).toStringList().isEmpty(), "collapsed state is remembered");

			p.setContext(mic, nullptr, nullptr);
			pump(700);
			expect(p.rowCount(se::FilterSection::AudioVideo) == 2, "audio input shows 2 audio rows");
			expect(p.rowCount(se::FilterSection::Effect) == 0, "audio input shows no effect rows");
		}

		stage("filters: pop-out windows and the quick-access close button");
		{
			auto &pops = se::FilterPopouts::instance();
			pops.open(srcB, "Look A");
			pops.open(srcB, "Look A"); // second open raises the same window
			pops.open(srcB, "Look B");
			pump(200);
			expect(pops.count() == 2, "two distinct pop-out windows");

			se::QuickAccessBar bar;
			bar.resize(500, 80);
			bar.show();
			bar.setTarget(item, srcB);
			pump(150);
			QToolButton *close = nullptr;
			for (auto *b : bar.findChildren<QToolButton *>())
				if (b->text().startsWith("Close filter windows"))
					close = b;
			expect(close != nullptr, "quick-access bar offers 'close filter windows'");
			if (close)
				close->click();
			pump(200);
			expect(pops.count() == 0, "the close button closes every pop-out window");

			pops.open(srcB, "Look A");
			pump(100);
			obs_source_t *gone = obs_source_get_filter_by_name(srcB, "Look A");
			obs_source_filter_remove(srcB, gone);
			obs_source_release(gone);
			pump(1500);
			expect(pops.count() == 0, "a pop-out closes itself when its filter is removed");
			expect(addTestFilter(srcB, "smoke_fx", "Look A"), "filter re-added");

			pops.open(srcB, "Look B");
			pump(100);
			if (auto *bridge = pops.findChild<se::ObsEventBridge *>())
				bridge->frontendEvent(int(OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING));
			pump(100);
			expect(pops.count() == 0, "pop-outs close when the scene collection changes");
		}

		stage("filters: copy and paste");
		{
			obs_source_t *look = obs_source_get_filter_by_name(srcB, "Look A");
			se::writeFilterClipboard({se::snapshotFilter(look)});
			obs_source_release(look);
			expect(QFile::exists(se::configFile("filter-clipboard.json")), "the clipboard is saved to disk");
			expect(!se::filterClipboardEmpty(), "clipboard has the copied filter");

			auto r = se::pasteFilters(srcA, se::readFilterClipboard());
			expect(r.pasted == 1 && hasFilter(srcA, "Look A"), "effect filter pasted onto another scene");
			r = se::pasteFilters(srcA, se::readFilterClipboard());
			expect(r.pasted == 1 && hasFilter(srcA, "Look A 2"), "pasting again picks a unique name");

			obs_source_t *gain = obs_source_get_filter_by_name(mic, "Gain");
			r = se::pasteFilters(srcA, {se::snapshotFilter(gain)});
			obs_source_release(gain);
			expect(r.pasted == 0 && r.skipped == 1, "an audio filter is refused on a scene (no audio)");
		}

		// With real filters in place, run the generic click-everything / right-click-everything pass too.
		exercise<se::FiltersPanel>("FiltersPanel (scene with filters)",
					   [&](se::FiltersPanel &p) { p.setContext(srcB, nullptr, srcA); });
		exercise<se::FiltersPanel>("FiltersPanel (audio input)",
					   [&](se::FiltersPanel &p) { p.setContext(mic, nullptr, nullptr); });
		se::FilterPopouts::instance().closeAll();
		pump(100);
		obs_source_release(mic);
	}

	// ---- the source picker dialog ------------------------------------------------------------
	{
		stage("source picker: filters, current source, loading");
		se::SourcePicker pk;
		pk.show();
		pump(200);
		auto *tree = pk.findChild<QTreeWidget *>();
		auto combos = pk.findChildren<QComboBox *>(); // [0] source type, [1] filter type
		auto *search = pk.findChild<QLineEdit *>();
		auto *showSources = pk.findChildren<QCheckBox *>().last(); // "Show sources"
		expect(tree && combos.size() == 2 && search, "picker has its tree, two filters and a search box");
		if (tree && combos.size() == 2 && search) {
			auto top = [&] { return tree->topLevelItemCount(); };
			const int all = top();
			expect(all >= 2, "picker lists the scenes");
			expect(combos[0]->findData("scene") >= 0, "source types offered include those present");
			expect(combos[1]->findData("smoke_fx") >= 0, "filter types offered include those present");

			search->setText("smoke a");
			expect(top() == 1, "search narrows the picker");
			search->clear();

			combos[1]->setCurrentIndex(combos[1]->findData("smoke_fx"));
			expect(top() >= 1, "filtering by filter type keeps scenes that have one");
			combos[0]->setCurrentIndex(combos[0]->findData("scene"));
			expect(top() >= 1, "filtering by source type keeps the scenes");
			combos[0]->setCurrentIndex(0);
			combos[1]->setCurrentIndex(0);

			showSources->setChecked(false);
			bool onlyScenes = true;
			for (QTreeWidgetItemIterator it(tree); *it; ++it)
				if ((*it)->parent() && (*it)->text(1) != "Group")
					onlyScenes = false;
			expect(onlyScenes, "'Show sources' off leaves scenes and groups only");
			showSources->setChecked(true);

			int chosen = 0;
			QObject::connect(&pk, &se::SourcePicker::sourceChosen, [&](obs_source *) { ++chosen; });
			pk.setCurrent("Smoke A");
			expect(tree->currentItem() && tree->currentItem()->text(0) == "Smoke A", "the current source is selected");
			expect(pk.canLoad(), "a scene row can be loaded");
			expect(pk.loadCurrent() && chosen == 1, "loading a scene reports it as chosen");

			// An item in a scene that is not live (there is no live scene here): OBS must be left
			// alone and the dock told to load the item itself.
			int loaded = 0;
			QObject::connect(&pk, &se::SourcePicker::itemChosen, [&](obs_sceneitem_t *, const QString &root) {
				if (root == "Smoke A")
					++loaded;
			});
			pk.setCurrent("Smoke B");
			for (QTreeWidgetItemIterator it(tree); *it; ++it)
				if ((*it)->parent() && (*it)->text(0) == "Smoke B")
					tree->setCurrentItem(*it);
			expect(pk.loadCurrent() && loaded == 1, "an item in a non-live scene is loaded directly");
		}
	}

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
	if (g_failures) {
		std::fprintf(stderr, "[smoke] %d check(s) failed\n", g_failures);
		return 5;
	}
	std::puts("[smoke] OK");
	return 0;
}
