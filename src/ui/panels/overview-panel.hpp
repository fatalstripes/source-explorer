#pragma once
#include "core/obs-raii.hpp"
#include "core/payload-generator.hpp"
#include "ui/widgets/command-menu.hpp"

#include <QWidget>

#include <deque>
#include <functional>
#include <optional>

class QGridLayout;
class QLabel;
class QPushButton;
class QTimer;

namespace se {

// Summary of the selection plus one-click actions: Center, Fit to Canvas, Rotate,
// Flip and Snapshot. Every action button has the command copier, which copies the
// exact request the action would send right now. GUI thread only; polls while visible.
class OverviewPanel final : public QWidget {
	Q_OBJECT
public:
	explicit OverviewPanel(QWidget *parent = nullptr);
	~OverviewPanel() override;

	void setItem(obs_sceneitem_t *item); // nullptr clears
	// A source with no scene item (a global audio device): summary only, no actions.
	void setSource(obs_source_t *source);

private slots:
	void refresh();

private:
	struct Action {
		QPushButton *button = nullptr;
		std::function<TransformPatch()> plan; // empty patch => not applicable right now
		QString unavailable;                  // message shown for an empty patch
	};

	void buildUi();
	Action &addAction(QGridLayout *grid, int row, int col, const QString &text,
			  std::function<TransformPatch()> plan, const QString &unavailable);
	void runAction(const Action &a);
	void snapshot();
	void say(const QString &message);
	obs_source_t *shownSource() const;
	SceneItemTarget target() const;
	void attachActionMenu(QPushButton *b, std::function<std::optional<CommandSet>()> provider);

	obs::SceneItemRef item_;
	obs::SourceRef source_; // used when there is no item
	std::deque<Action> actions_;
	QPushButton *snapshotButton_ = nullptr;

	QLabel *name_ = nullptr;
	QLabel *type_ = nullptr;
	QLabel *sourceSize_ = nullptr;
	QLabel *onCanvas_ = nullptr;
	QLabel *position_ = nullptr;
	QLabel *rotation_ = nullptr;
	QLabel *canvasSize_ = nullptr;
	QLabel *status_ = nullptr;
	QLabel *hint_ = nullptr; // shown while nothing is selected
	QTimer *poll_ = nullptr;
};

} // namespace se
