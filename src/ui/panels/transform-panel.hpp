#pragma once
#include "core/obs-raii.hpp"
#include "core/payload-generator.hpp"
#include "core/transform-math.hpp"

#include <QPointer>
#include <QWidget>

#include <deque>
#include <string>
#include <utility>
#include <vector>
#include <functional>

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QGridLayout;
class QPushButton;

struct obs_transform_info;
struct obs_sceneitem_crop;

namespace se {

class AlignmentGrid;

// Edits one scene item's transform. All methods run on the Qt GUI thread; OBS
// notifications are marshalled in via queued invocations.
class TransformPanel final : public QWidget {
	Q_OBJECT
public:
	explicit TransformPanel(QWidget *parent = nullptr);
	~TransformPanel() override;

	void setItem(obs_sceneitem_t *item); // nullptr clears; takes its own reference
	obs_sceneitem_t *item() const { return item_.get(); }

private slots:
	void reload();

private:
	struct Field {
		QDoubleSpinBox *box = nullptr;
		double defaultValue = 0.0;
		std::function<double(const obs_transform_info &, const obs_sceneitem_crop &)> read;
		std::function<void(obs_transform_info &, obs_sceneitem_crop &, double)> write;
		std::function<void(TransformPatch &, double)> patch;
		std::function<void()> reset; // optional: overrides "set the box to defaultValue"
	};

	void buildUi();
	Field &addField(QFormLayout *form, const QString &label, double min, double max, double step,
			int decimals, double def);
	void connectSignals();
	void disconnectSignals();

	bool readItem(obs_transform_info &ti, obs_sceneitem_crop &crop) const;
	void applyPatch(const TransformPatch &p); // writes to OBS, then reloads UI
	SceneItemTarget target() const;
	void attachMenu(QWidget *w, std::function<void(TransformPatch &)> fill, std::function<void()> reset);
	// Right-click on a field's LABEL: submenus for the combined commands it belongs to, plus the
	// full transform. `groups` are (menu text, how to fill a patch for that group).
	using GroupFill = std::pair<std::string, std::function<void(TransformPatch &)>>;
	void attachLabelMenu(QWidget *label, std::vector<GroupFill> groups);
	ItemGeometry currentGeometry() const;
	// One quick-action button: click applies `plan()`; right-click copies the request it would send.
	void addQuickAction(QGridLayout *grid, int row, int col, const QString &text,
			    std::function<TransformPatch()> plan);

	obs::SceneItemRef item_;
	obs::SourceRef sceneSource_; // keeps the signal handler's owner alive
	bool updating_ = false;
	bool signalsConnected_ = false;
	QPointer<TransformPanel> *slot_ = nullptr; // libobs callback data; freed in disconnectSignals()

	std::deque<Field> fields_; // deque: stable references
	AlignmentGrid *alignGrid_ = nullptr;
	AlignmentGrid *boundsAlignGrid_ = nullptr;
	QPushButton *pasteButton_ = nullptr; // enabled once something has been copied
	QDoubleSpinBox *sizeWidthBox_ = nullptr;   // greyed out while a bounding box drives the size
	QDoubleSpinBox *sizeHeightBox_ = nullptr;
	QDoubleSpinBox *boundsWidthBox_ = nullptr;  // only meaningful while a bounds type is set
	QDoubleSpinBox *boundsHeightBox_ = nullptr;
	QComboBox *boundsType_ = nullptr;
};

} // namespace se
