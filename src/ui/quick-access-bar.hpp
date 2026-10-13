#pragma once
#include "core/obs-raii.hpp"
#include "core/quick-access.hpp"

#include <QWidget>

#include <map>
#include <vector>

class QToolButton;

namespace se {

class FlowLayout;

// The row of icon-style buttons under the identity bar. Which buttons appear depends on what is
// loaded (core/quick-access.hpp): audio sources get Mute and Monitoring, browser sources get
// Refresh, media sources get Play/Pause/Stop/Restart, scene items get Visibility, Lock and a
// Transform popup, and so on. Right-click a button for the command it sends.
// GUI thread only; the dock calls refresh() on its poll tick to keep the button states current.
class QuickAccessBar final : public QWidget {
	Q_OBJECT
public:
	explicit QuickAccessBar(QWidget *parent = nullptr);
	~QuickAccessBar() override;

	// The loaded source. `item` is null for a scene, group or device with no scene item.
	// Cheap to call repeatedly: it only rebuilds when the source or its traits change.
	void setTarget(obs_sceneitem_t *item, obs_source_t *source);
	void refresh();

	bool pinned() const { return pinned_; }
	void setPinned(bool pinned); // does not emit

signals:
	void pinToggled(bool pinned);
	void resetUiRequested();

private:
	void rebuild();
	QToolButton *addButton(QuickButton kind, const QString &text, const QString &tip, bool checkable);
	void attachMenus();
	bool pressProperty(const char *propertyName); // click a button property of the source

	obs::SceneItemRef item_;
	obs::SourceRef source_;
	SourceTraits traits_;
	obs_source_t *traitsFor_ = nullptr; // the source the traits (incl. the property probe) were computed for
	bool pinned_ = false;

	FlowLayout *flow_ = nullptr;
	std::map<QuickButton, QToolButton *> buttons_;
};

} // namespace se
