// SPDX-License-Identifier: GPL-2.0-or-later
#include "ui/explorer-dock.hpp"

#include "core/obs-event-bridge.hpp"
#include "core/obs-raii.hpp"
#include "core/quick-access.hpp"
#include "core/tree-snapshot.hpp"
#include "ui/dock-placement.hpp"
#include "ui/dock-style.hpp"
#include "util/plugin-config.hpp"
#include "ui/quick-access-bar.hpp"
#include "ui/panels/audio-panel.hpp"
#include "ui/panels/filters-panel.hpp"
#include "ui/panels/misc-panel.hpp"
#include "ui/panels/source-panel.hpp"
#include "ui/panels/overview-panel.hpp"
#include "ui/panels/properties-panel.hpp"
#include "ui/panels/transform-panel.hpp"
#include "ui/source-picker.hpp"
#include "ui/source-tree-drawer.hpp"

#include <obs-frontend-api.h>
#include <obs.h>

#include <algorithm>

#include <QBoxLayout>
#include <QDockWidget>
#include <QEasingCurve>
#include <QEvent>
#include <QShowEvent>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>

namespace se {

// `kind` selects the accent colour in dock-style.hpp (program / preview / muted / filters).
static QLabel *makeBadge(const QString &text, const char *kind, QWidget *parent)
{
	auto *l = new QLabel(text, parent);
	l->setObjectName("seBadge");
	l->setProperty("kind", kind);
	l->setVisible(false);
	return l;
}

ExplorerDock::ExplorerDock(QWidget *parent) : QWidget(parent)
{
	setObjectName("sourceExplorerDock");
	// Explicit minimum: the layout would otherwise derive it from the tallest page and stop the dock
	// from fitting into (or snapping to) a dock area. Content scrolls instead of being clipped.
	setMinimumSize(300, 260);
	setStyleSheet(dockStyleSheet());
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(6, 6, 6, 6);

	buildIdentityBar(root);

	context_ = new ContextBar(this);
	root->addWidget(context_);
	quick_ = new QuickAccessBar(this);
	root->addWidget(quick_);

	auto *body = new QHBoxLayout;
	root->addLayout(body, 1);

	// Slide-out source tree: collapsed to zero width until the identity-bar button opens it.
	drawer_ = new SourceTreeDrawer(this);
	drawer_->setFixedWidth(0);
	drawer_->hide();
	body->addWidget(drawer_);
	drawerAnim_ = new QPropertyAnimation(this, "drawerWidth", this);
	drawerAnim_->setDuration(160);
	drawerAnim_->setEasingCurve(QEasingCurve::OutCubic);
	connect(drawer_, &SourceTreeDrawer::globalSourceChosen, this, &ExplorerDock::setPinnedSource);
	connect(drawer_, &SourceTreeDrawer::itemChosen, this, &ExplorerDock::loadItem);
	// A pick in the source tree is deliberate, so it may leave a pinned source.
	connect(drawer_, &SourceTreeDrawer::selectionApplied, this, [this] {
		allowOverride_ = true;
		syncSelection();
	});

	connect(quick_, &QuickAccessBar::pinToggled, this, [this](bool on) { hold_ = on; });
	connect(quick_, &QuickAccessBar::resetUiRequested, this, &ExplorerDock::resetUi);
	// Breadcrumbs: Scene / Group load that scene or group as the source being worked on.
	connect(context_, &ContextBar::sceneClicked, this, [this] {
		if (auto s = obs::sourceByName(ctx_.scene.toUtf8().constData()))
			setPinnedSource(s.get());
	});
	connect(context_, &ContextBar::groupClicked, this, [this] {
		if (auto s = obs::sourceByName(ctx_.group.toUtf8().constData()))
			setPinnedSource(s.get());
	});
	// Source: back to the selected item if a scene or group was loaded, otherwise open the tree.
	connect(context_, &ContextBar::sourceClicked, this, [this] {
		if (pinned_ && lastItem_) {
			pinned_.reset();
			obs::SourceRef edit = obs::adopt(obs_frontend_preview_program_mode_active()
								 ? obs_frontend_get_current_preview_scene()
								 : obs_frontend_get_current_scene());
			applySelection(lastItem_.get(), edit.get());
			updateTabVisibility();
			updateBars();
		} else {
			openPicker();
		}
	});

	rail_ = new QListWidget(this);
	rail_->setObjectName("seRail");
	// Wide enough for the longest label plus the item padding, whatever the font or DPI.
	rail_->setFixedWidth(rail_->fontMetrics().horizontalAdvance(QStringLiteral("Properties")) + 44); // first guess; refined in updateRailWidth()
	rail_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	rail_->setTextElideMode(Qt::ElideNone);
	const char *names[PageCount] = {"Overview", "Source", "Transform", "Audio", "Filters", "Properties", "Misc"};
	stack_ = new QStackedWidget(this);

	// Pages sit in scroll areas so the dock's minimum size is small. A QDockWidget whose minimum
	// size does not fit cannot be dropped into a dock area, and the Transform page alone is
	// about a thousand pixels tall. (Properties already scrolls internally.)
	auto addPage = [this](QWidget *page, bool scrolls) {
		if (!scrolls) {
			stack_->addWidget(page);
			pages_[stack_->count() - 1] = page;
			return;
		}
		auto *area = new QScrollArea(stack_);
		area->setWidgetResizable(true);
		area->setFrameShape(QFrame::NoFrame);
		area->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
		area->setWidget(page); // the scroll area owns the page
		stack_->addWidget(area);
		pages_[stack_->count() - 1] = area;
	};

	for (int i = 0; i < PageCount; ++i) {
		rail_->addItem(names[i]);
		switch (i) {
		case Overview:
			overview_ = new OverviewPanel;
			connect(overview_, &OverviewPanel::pickSourceRequested, this, &ExplorerDock::openPicker);
			addPage(overview_, true);
			break;
		case Source:
			sourcePanel_ = new SourcePanel;
			addPage(sourcePanel_, true);
			break;
		case Misc:
			misc_ = new MiscPanel;
			addPage(misc_, true);
			break;
		case Transform:
			transform_ = new TransformPanel;
			addPage(transform_, true);
			break;
		case Filters:
			filters_ = new FiltersPanel;
			addPage(filters_, true);
			break;
		case Audio:
			audio_ = new AudioPanel;
			addPage(audio_, true);
			break;
		case Properties:
			properties_ = new PropertiesPanel(stack_);
			addPage(properties_, false);
			break;
		default:
			break;
		}
	}
	rail_->setCurrentRow(Overview);
	connect(rail_, &QListWidget::currentRowChanged, stack_, &QStackedWidget::setCurrentIndex);
	rail_->viewport()->installEventFilter(this);
	rail_->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(rail_, &QWidget::customContextMenuRequested, this, &ExplorerDock::showRailMenu);

	body->addWidget(rail_);
	body->addWidget(stack_, 1);

	bridge_ = new ObsEventBridge(this);
	connect(bridge_, &ObsEventBridge::frontendEvent, this, &ExplorerDock::onFrontendEvent);

	// No frontend signal for scene-item selection exists, so poll the edit scene
	// (cheap: one enum over the current scene's items).
	selectionPoll_ = new QTimer(this);
	selectionPoll_->setInterval(250);
	connect(selectionPoll_, &QTimer::timeout, this, &ExplorerDock::syncSelection);
	selectionPoll_->start();

	refreshIdentity();
}

ExplorerDock::~ExplorerDock() = default;

namespace {
struct SelectedItemFinder {
	obs::SceneItemRef found;
	static bool cb(obs_scene_t *, obs_sceneitem_t *item, void *p)
	{
		auto *self = static_cast<SelectedItemFinder *>(p);
		if (obs_sceneitem_selected(item)) {
			self->found = obs::retain(item); // enum gives a borrowed pointer
			return false;
		}
		if (obs_sceneitem_is_group(item)) { // children of a group can be selected too
			obs_scene_enum_items(obs_sceneitem_group_get_scene(item), &SelectedItemFinder::cb, p);
			return !self->found;
		}
		return true;
	}
};
} // namespace

// Source tab: the item's source. Group tab: the group containing the item, if any.
// Scene tab: the scene being edited (for a grouped item, the group's parent scene).
void ExplorerDock::updateFilterContext(obs_sceneitem_t *item, obs_source_t *editScene)
{
	if (!item) {
		filters_->setContext(nullptr, nullptr, nullptr);
		return;
	}
	obs_source_t *group = nullptr;
	obs_source_t *scene = editScene;
	if (obs_scene_t *owner = obs_sceneitem_get_scene(item)) {
		if (obs_scene_is_group(owner))
			group = obs_scene_get_source(owner); // borrowed; setContext retains
		else
			scene = obs_scene_get_source(owner);
	}
	filters_->setContext(obs_sceneitem_get_source(item), group, scene);
}

void ExplorerDock::applySelection(obs_sceneitem_t *item, obs_source_t *editScene)
{
	if (panelsGone_)
		return;
	transform_->setItem(item);
	overview_->setItem(item);
	sourcePanel_->setItem(item);
	misc_->setItem(item);
	obs_source_t *src = item ? obs_sceneitem_get_source(item) : nullptr;
	audio_->setSource(src);
	updateFilterContext(item, editScene);
	properties_->setSource(src);
}

QString ExplorerDock::editSceneName() const
{
	obs::SourceRef s = obs::adopt(obs_frontend_preview_program_mode_active() ? obs_frontend_get_current_preview_scene()
										 : obs_frontend_get_current_scene());
	return s ? QString::fromUtf8(obs_source_get_name(s.get())) : QString();
}

void ExplorerDock::syncSelection()
{
	if (panelsGone_)
		return;
	// Studio mode edits the preview scene; otherwise the program scene.
	obs::SourceRef scene = obs::adopt(obs_frontend_preview_program_mode_active()
						  ? obs_frontend_get_current_preview_scene()
						  : obs_frontend_get_current_scene());
	SelectedItemFinder finder;
	if (scene)
		obs_scene_enum_items(obs_scene_from_source(scene.get()), &SelectedItemFinder::cb, &finder);

	// Pin holds what is loaded: OBS's selection moving elsewhere is ignored until it is released,
	// unless the user made a deliberate pick in the source tree (allowOverride_, one-shot).
	const bool holding = hold_ && !allowOverride_;
	allowOverride_ = false;

	// Compared against OBS's last-seen selection, not the transform panel's item:
	// pinning a global device clears the panel while OBS's selection stays as it was.
	if (!holding && finder.found.get() != lastItem_.get()) {
		lastItem_ = obs::retain(finder.found.get());
		rootSceneName_ = (finder.found && scene) ? obs_source_get_name(scene.get()) : "";
		// A real selection takes over from a pinned global audio device; losing the
		// selection leaves a pinned device alone (the tree has no scene item to keep).
		if (finder.found)
			pinned_.reset();
		if (finder.found || !pinned_) {
			applySelection(finder.found.get(), scene.get());
			if (!finder.found) {
				refreshIdentity();
				updateBars();
				return;
			}
		}
	}

	updateTabVisibility();
	updateBars();

	if (pinned_) {
		showSourceIdentity(pinned_.get());
		return;
	}
	if (!lastItem_)
		return;
	showSourceIdentity(obs_sceneitem_get_source(lastItem_.get()));
}

// Status bar breadcrumbs and quick-access buttons for whatever is loaded.
void ExplorerDock::updateBars()
{
	obs_source_t *src = currentSource();
	drawer_->setCurrent(src ? QString::fromUtf8(obs_source_get_name(src)) : QString());
	quick_->setTarget(pinned_ ? nullptr : lastItem_.get(), src);
	quick_->refresh();

	ContextInfo info;
	if (pinned_) {
		const QString n = QString::fromUtf8(obs_source_get_name(pinned_.get()));
		if (obs_source_get_type(pinned_.get()) == OBS_SOURCE_TYPE_SCENE) {
			(obs_source_is_group(pinned_.get()) ? info.group : info.scene) = n;
		} else {
			info.source = n;
		}
	} else if (lastItem_) {
		info.source = QString::fromUtf8(obs_source_get_name(obs_sceneitem_get_source(lastItem_.get())));
		if (obs_scene_t *owner = obs_sceneitem_get_scene(lastItem_.get());
		    owner && obs_scene_is_group(owner))
			info.group = QString::fromUtf8(obs_source_get_name(obs_scene_get_source(owner)));
		info.scene = QString::fromStdString(rootSceneName_);
		const QString edit = editSceneName();
		if (contextMismatch(edit.toStdString(), rootSceneName_)) {
			info.warn = true;
			info.warnText = tr("This source is in the scene \"%1\", not the scene you are editing (\"%2\").")
						.arg(info.scene, edit);
		}
	}
	ctx_ = info;
	context_->setContext(info);
}

// The "Select a source" dialog, opened on the loaded source. Scenes and global devices are loaded as
// the pinned source; scene items are selected in OBS and the dock follows as it does for any selection.
void ExplorerDock::openPicker()
{
	if (panelsGone_)
		return;
	SourcePicker picker(this);
	if (obs_source_t *src = currentSource())
		picker.setCurrent(QString::fromUtf8(obs_source_get_name(src)));
	connect(&picker, &SourcePicker::sourceChosen, this, &ExplorerDock::setPinnedSource);
	connect(&picker, &SourcePicker::selectionApplied, this, [this] {
		allowOverride_ = true; // a deliberate pick may leave a pinned source
		syncSelection();
	});
	connect(&picker, &SourcePicker::itemChosen, this, &ExplorerDock::loadItem);
	picker.exec();
}

void ExplorerDock::loadItem(obs_sceneitem_t *item, const QString &rootScene)
{
	if (!item || panelsGone_)
		return;
	pinned_.reset();
	lastItem_ = obs::retain(item);
	rootSceneName_ = rootScene.toStdString();
	hold_ = true; // stay on it: OBS's selection is somewhere else
	quick_->setPinned(true);
	obs::SourceRef edit = editScene();
	applySelection(item, edit.get());
	updateTabVisibility();
	updateBars();
	showSourceIdentity(obs_sceneitem_get_source(item));
}

// Close the tree, go back to Overview and release a pin. Selection and any loaded device stay.
void ExplorerDock::resetUi()
{
	reattachAll();
	filters_->resetLayout();
	hold_ = false;
	quick_->setPinned(false);
	drawerButton_->setChecked(false);
	rail_->setCurrentRow(Overview);
	if (auto *wrapper = qobject_cast<QDockWidget *>(parentWidget()); wrapper && wrapper->isFloating())
		wrapper->resize(sizeHint());
}

void ExplorerDock::setPinnedSource(obs_source_t *source)
{
	if (!source || panelsGone_)
		return;
	pinned_ = obs::retain(source);
	transform_->setItem(nullptr); // global devices have no transform
	overview_->setSource(source);
	sourcePanel_->setSource(source);
	misc_->setSource(source);
	audio_->setSource(source);
	filters_->setContext(source, nullptr, nullptr);
	properties_->setSource(source);
	showSourceIdentity(source);
	updateTabVisibility();
	updateBars();
}

obs_source_t *ExplorerDock::currentSource() const
{
	if (pinned_)
		return pinned_.get();
	return lastItem_ ? obs_sceneitem_get_source(lastItem_.get()) : nullptr;
}

// Tabs that do not apply to what is loaded are hidden (an audio-only source has no Transform tab,
// nothing selected leaves just the Overview). If the tab being viewed disappears, fall back to Overview.
void ExplorerDock::updateTabVisibility()
{
	obs_source_t *src = currentSource();
	const bool hasSource = src != nullptr;
	const bool hasItem = !pinned_ && static_cast<bool>(lastItem_);
	const bool hasAudio = hasSource && (obs_source_get_output_flags(src) & OBS_SOURCE_AUDIO);

	const bool visible[PageCount] = {
		/* Overview */ true,    /* Source */ hasSource, /* Transform */ hasItem, /* Audio */ hasAudio,
		/* Filters */ hasSource, /* Properties */ hasSource, /* Misc */ hasSource,
	};
	for (int i = 0; i < PageCount; ++i) {
		const bool show = visible[i] && !isDetached(Page(i)); // a detached tab lives in its own dock
		if (rail_->item(i)->isHidden() == show)
			rail_->item(i)->setHidden(!show);
	}

	if (rail_->currentItem() == nullptr || rail_->currentItem()->isHidden())
		rail_->setCurrentRow(Overview);
}

void ExplorerDock::setDrawerWidth(int width)
{
	drawerWidth_ = width;
	drawer_->setFixedWidth(width); // min == max: the layout must give the tree exactly this much
}

// A floating dock gets wider by the drawer's width while it is open, so the panels beside it keep
// the room they had; a docked one just shares the space it already has.
void ExplorerDock::setDrawerOpen(bool open)
{
	constexpr int kDrawerWidth = 260;
	auto *wrapper = qobject_cast<QDockWidget *>(parentWidget());
	const bool floating = wrapper && wrapper->isFloating();

	drawerAnim_->stop();
	disconnect(drawerAnim_, &QPropertyAnimation::finished, nullptr, nullptr);
	drawerAnim_->setStartValue(drawerWidth_);

	if (open) {
		drawer_->show();
		if (floating && windowGrowth_ == 0) {
			windowGrowth_ = kDrawerWidth;
			wrapper->resize(wrapper->width() + windowGrowth_, wrapper->height());
		}
		drawerAnim_->setEndValue(kDrawerWidth);
	} else {
		drawerAnim_->setEndValue(0);
		connect(drawerAnim_, &QPropertyAnimation::finished, this, [this] {
			if (drawerButton_->isChecked())
				return; // reopened meanwhile
			drawer_->hide(); // hidden drawers stop polling
			if (windowGrowth_ > 0) {
				if (auto *w = qobject_cast<QDockWidget *>(parentWidget());
				    w && w->isFloating())
					w->resize(std::max(200, w->width() - windowGrowth_), w->height());
				windowGrowth_ = 0;
			}
		});
	}
	drawerAnim_->start();
}

// Identity bar for one source. Muted/filter state can change at any time, so this
// runs on every selection tick, not just when the selection changes.
// A glyph for the kind of source, from libobs's own icon classification.
static QString iconGlyph(obs_source_t *src)
{
	if (obs_source_get_type(src) == OBS_SOURCE_TYPE_SCENE)
		return QStringLiteral("\u25A6"); // scene or group
	switch (obs_source_get_icon_type(obs_source_get_id(src))) {
	case OBS_ICON_TYPE_IMAGE: return QStringLiteral("\u25A8");
	case OBS_ICON_TYPE_COLOR: return QStringLiteral("\u25A0");
	case OBS_ICON_TYPE_SLIDESHOW: return QStringLiteral("\u25A4");
	case OBS_ICON_TYPE_AUDIO_INPUT: return QStringLiteral("\u266A");
	case OBS_ICON_TYPE_AUDIO_OUTPUT:
	case OBS_ICON_TYPE_PROCESS_AUDIO_OUTPUT: return QStringLiteral("\u266B");
	case OBS_ICON_TYPE_DESKTOP_CAPTURE: return QStringLiteral("\u25AD");
	case OBS_ICON_TYPE_WINDOW_CAPTURE: return QStringLiteral("\u25A2");
	case OBS_ICON_TYPE_GAME_CAPTURE: return QStringLiteral("\u25C8");
	case OBS_ICON_TYPE_CAMERA: return QStringLiteral("\u25C9");
	case OBS_ICON_TYPE_TEXT: return QStringLiteral("T");
	case OBS_ICON_TYPE_MEDIA: return QStringLiteral("\u25B6");
	case OBS_ICON_TYPE_BROWSER: return QStringLiteral("\u25CD");
	default: return QStringLiteral("\u25C7");
	}
}

void ExplorerDock::showSourceIdentity(obs_source_t *src)
{
	iconLabel_->setText(iconGlyph(src));
	nameLabel_->setText(QString::fromUtf8(obs_source_get_name(src)));
	typeLabel_->setText(QString::fromUtf8(obs_source_get_display_name(obs_source_get_id(src))));
	const bool live = obs_source_active(src);
	badgeProgram_->setVisible(live);
	badgePreview_->setVisible(!live && obs_source_showing(src));
	badgeMuted_->setVisible(obs_source_muted(src));
	const size_t filters = obs_source_filter_count(src);
	badgeFilters_->setText(tr("Filters: %1").arg(filters));
	badgeFilters_->setVisible(filters > 0);
}

void ExplorerDock::buildIdentityBar(QBoxLayout *root)
{
	auto *bar = new QHBoxLayout;
	drawerButton_ = new QToolButton(this);
	drawerButton_->setObjectName("seDrawerButton");
	drawerButton_->setText(tr("Sources"));
	drawerButton_->setCheckable(true);
	drawerButton_->setToolTip(tr("Show the scene and source tree"));
	connect(drawerButton_, &QToolButton::toggled, this, &ExplorerDock::setDrawerOpen);
	bar->addWidget(drawerButton_);

	iconLabel_ = new QLabel(this);
	iconLabel_->setObjectName("seIcon");
	bar->addWidget(iconLabel_);

	nameLabel_ = new QToolButton(this);
	nameLabel_->setObjectName("seIdentityName");
	nameLabel_->setText(tr("No source selected"));
	nameLabel_->setToolTip(tr("Select a source"));
	nameLabel_->setAutoRaise(true);
	connect(nameLabel_, &QToolButton::clicked, this, &ExplorerDock::openPicker);
	typeLabel_ = new QLabel(this);
	typeLabel_->setForegroundRole(QPalette::PlaceholderText); // muted, follows the OBS theme
	badgeProgram_ = makeBadge(tr("Live on Program"), "program", this);
	badgePreview_ = makeBadge(tr("In Preview"), "preview", this);
	badgeMuted_ = makeBadge(tr("Muted"), "muted", this);
	badgeFilters_ = makeBadge(QString(), "filters", this);

	bar->addWidget(nameLabel_);
	bar->addWidget(typeLabel_);
	bar->addStretch(1);
	for (auto *b : {badgeProgram_, badgePreview_, badgeMuted_, badgeFilters_})
		bar->addWidget(b);
	root->addLayout(bar);
}

void ExplorerDock::onFrontendEvent(int event)
{
	// Always on the GUI thread (ObsEventBridge queues anything from other threads).
	switch (event) {
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
	case OBS_FRONTEND_EVENT_EXIT:
		if (panelsGone_)
			break;
		// Let go of every scene item / source before OBS tears the collection down.
		pinned_.reset();
		lastItem_.reset();
		hold_ = false;
		rootSceneName_.clear();
		quick_->setPinned(false);
		quick_->setTarget(nullptr, nullptr);
		context_->setContext({});
		transform_->setItem(nullptr);
		overview_->setItem(nullptr);
		sourcePanel_->setItem(nullptr);
		misc_->setItem(nullptr);
		audio_->setSource(nullptr);
		filters_->setContext(nullptr, nullptr, nullptr);
		properties_->setSource(nullptr);
		break;
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		refreshIdentity();
		break;
	default:
		break;
	}
}

// Identity when nothing is selected: say so, and show which scene is being edited as context.
// A selected source (or pinned device) owns the bar instead; syncSelection() keeps that fresh.
void ExplorerDock::refreshIdentity()
{
	if (lastItem_ || pinned_)
		return;

	iconLabel_->clear();
	nameLabel_->setText(tr("No source selected"));
	obs::SourceRef edit = obs::adopt(obs_frontend_preview_program_mode_active()
						 ? obs_frontend_get_current_preview_scene()
						 : obs_frontend_get_current_scene());
	typeLabel_->setText(edit ? tr("Editing scene: %1").arg(QString::fromUtf8(obs_source_get_name(edit.get())))
				 : QString());
	for (auto *b : {badgeProgram_, badgePreview_, badgeMuted_, badgeFilters_})
		b->setVisible(false);
}

// The font and padding that decide the rail's width are only final once OBS's theme has been
// applied to the shown widget, so it is fitted then (and again if the style or font changes).
void ExplorerDock::updateRailWidth()
{
	// Style and font change events arrive while the constructor is still running (setStyleSheet
	// is its first call), before the rail exists. Ignore those. (tests/smoke_dock.cpp covers this.)
	if (!rail_)
		return;
	rail_->ensurePolished();
	const int width = rail_->sizeHintForColumn(0) + 2 * rail_->frameWidth() + 8;
	if (width > 8 && rail_->width() != width)
		rail_->setFixedWidth(width);
}

void ExplorerDock::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	updateRailWidth();
}

void ExplorerDock::changeEvent(QEvent *event)
{
	QWidget::changeEvent(event);
	if (event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange)
		updateRailWidth();
}

QSize ExplorerDock::sizeHint() const
{
	return QSize(420, 620); // first-show size for a floating dock
}

// ---------------------------------------------------------------------------
// Detachable tabs
// ---------------------------------------------------------------------------
namespace {

const char *const kPageIds[ExplorerDock::PageCount] = {"overview", "source", "transform", "audio",
							"filters", "properties", "misc"};
const char *const kPageTitles[ExplorerDock::PageCount] = {"Overview", "Source", "Transform", "Audio",
							   "Filters", "Properties", "Misc"};

QString dockIdFor(int page)
{
	return QStringLiteral("source-explorer-tab-") + kPageIds[page];
}

// What goes into the new OBS dock: a Reattach button over the page. The page keeps being fed by the
// main dock, so it follows the selection exactly as it did inside it.
class DetachedTab final : public QWidget {
public:
	DetachedTab(QWidget *page, const std::function<void()> &onReattach) : QWidget(nullptr)
	{
		setStyleSheet(dockStyleSheet()); // the main dock's styling does not reach a separate dock
		auto *layout = new QVBoxLayout(this);
		layout->setContentsMargins(6, 4, 6, 6);
		auto *bar = new QHBoxLayout;
		bar->addStretch(1);
		auto *button = new QPushButton(QObject::tr("Reattach"), this);
		button->setToolTip(QObject::tr("Put this tab back into the Source Explorer dock"));
		QObject::connect(button, &QPushButton::clicked, this, [onReattach] { onReattach(); });
		bar->addWidget(button);
		layout->addLayout(bar);
		layout->addWidget(page, 1);
		page->show(); // taking a page out of the QStackedWidget hid it
	}
};

ExplorerDock::DockHost &dockHost()
{
	static ExplorerDock::DockHost host = [] {
		ExplorerDock::DockHost h;
		h.add = [](const QString &id, const QString &title, QWidget *content) {
			if (!obs_frontend_add_dock_by_id(id.toUtf8().constData(), title.toUtf8().constData(), content))
				return false;
			if (auto *wrapper = qobject_cast<QDockWidget *>(content->parentWidget())) {
				new FloatingDockRescue(wrapper); // keep a floating dock's title bar reachable
				// OBS adds a plugin's dock hidden (the user normally ticks it in the Docks menu), but
				// the user just asked for this one, so show it.
				wrapper->resize(std::max(wrapper->width(), 420), std::max(wrapper->height(), 520));
				wrapper->show();
				wrapper->raise();
			}
			return true;
		};
		h.remove = [](const QString &id) { obs_frontend_remove_dock(id.toUtf8().constData()); };
		return h;
	}();
	return host;
}

} // namespace

void ExplorerDock::setDockHost(DockHost host)
{
	dockHost() = std::move(host);
}

QStringList ExplorerDock::detachedDockIds()
{
	QStringList ids;
	for (int i = 0; i < PageCount; ++i)
		ids << dockIdFor(i);
	return ids;
}

void ExplorerDock::saveDetached() const
{
	QStringList names;
	for (int i = 0; i < PageCount; ++i)
		if (isDetached(Page(i)))
			names << kPageIds[i];
	if (names.isEmpty())
		uiSettings().remove("tabs/detached");
	else
		uiSettings().setValue("tabs/detached", names);
}

// Dragging a tab off the rail (a little way beyond it) detaches it, like right-click > Detach.
bool ExplorerDock::eventFilter(QObject *watched, QEvent *event)
{
	if (rail_ && watched == rail_->viewport()) {
		switch (event->type()) {
		case QEvent::MouseButtonPress: {
			auto *me = static_cast<QMouseEvent *>(event);
			QListWidgetItem *item = me->button() == Qt::LeftButton ? rail_->itemAt(me->position().toPoint()) : nullptr;
			railPressRow_ = item ? rail_->row(item) : -1;
			break;
		}
		case QEvent::MouseMove: {
			auto *me = static_cast<QMouseEvent *>(event);
			if (railPressRow_ >= 0 && (me->buttons() & Qt::LeftButton)) {
				const QRect away = rail_->viewport()->rect().adjusted(-24, -24, 24, 24);
				if (!away.contains(me->position().toPoint())) {
					const Page page = Page(railPressRow_);
					railPressRow_ = -1;
					QTimer::singleShot(0, this, [this, page] { detachPage(page); });
				}
			}
			break;
		}
		case QEvent::MouseButtonRelease:
			railPressRow_ = -1;
			break;
		default:
			break;
		}
	}
	return QWidget::eventFilter(watched, event);
}

void ExplorerDock::detachPage(Page page)
{
	if (isDetached(page) || !pages_[page] || panelsGone_)
		return;
	QWidget *area = pages_[page];

	// A placeholder keeps every other page at the same stack index as its rail row.
	auto *placeholder = new QLabel(tr("This tab is open in its own dock.\nUse its Reattach button, or Reset UI."));
	placeholder->setAlignment(Qt::AlignCenter);
	placeholder->setWordWrap(true);
	stack_->removeWidget(area);
	stack_->insertWidget(page, placeholder);

	auto *host = new DetachedTab(area, [this, page] {
		QTimer::singleShot(0, this, [this, page] { reattachPage(page); }); // not from inside its own button
	});
	if (!dockHost().add(dockIdFor(page), tr("Source Explorer: %1").arg(tr(kPageTitles[page])), host)) {
		stack_->removeWidget(placeholder);
		delete placeholder;
		stack_->insertWidget(page, area); // takes it back out of the host
		delete host;
		return;
	}
	detached_[page].host = host;
	// The host disappearing for any reason other than our own reattach means OBS is tearing down.
	connect(host, &QObject::destroyed, this, [this] {
		if (!reattaching_)
			panelsGone_ = true;
	});
	saveDetached();
	updateTabVisibility();
}

void ExplorerDock::reattachPage(Page page)
{
	if (!isDetached(page))
		return;
	QPointer<QWidget> host = detached_[page].host;
	if (host)
		host->disconnect(this); // a deliberate reattach is not OBS tearing the dock down, however late the host dies
	reattaching_ = true;
	QWidget *placeholder = stack_->widget(page);
	stack_->removeWidget(placeholder);
	placeholder->deleteLater();
	stack_->insertWidget(page, pages_[page]); // out of the host, back in place
	pages_[page]->show();
	dockHost().remove(dockIdFor(page));
	if (host)
		host->deleteLater(); // the host did not go with its dock
	detached_[page].host = nullptr;
	reattaching_ = false;

	saveDetached();
	updateTabVisibility();
	if (!rail_->item(page)->isHidden())
		rail_->setCurrentRow(page);
}

void ExplorerDock::reattachAll()
{
	for (int i = 0; i < PageCount; ++i)
		reattachPage(Page(i));
}

void ExplorerDock::restoreDetached()
{
	const QStringList names = uiSettings().value("tabs/detached").toStringList();
	for (int i = 0; i < PageCount; ++i)
		if (names.contains(kPageIds[i]))
			detachPage(Page(i));
}

void ExplorerDock::showRailMenu(const QPoint &pos)
{
	QMenu menu(rail_);
	if (QListWidgetItem *item = rail_->itemAt(pos)) {
		const Page page = Page(rail_->row(item));
		menu.addAction(tr("Detach \"%1\" into its own dock").arg(item->text()), this,
			       [this, page] { QTimer::singleShot(0, this, [this, page] { detachPage(page); }); });
	}
	for (int i = 0; i < PageCount; ++i) {
		if (isDetached(Page(i))) {
			menu.addAction(tr("Reattach all tabs"), this, [this] { reattachAll(); });
			break;
		}
	}
	if (!menu.isEmpty())
		menu.exec(rail_->mapToGlobal(pos));
}

} // namespace se
