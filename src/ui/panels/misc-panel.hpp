#pragma once
#include "core/obs-raii.hpp"

#include <QWidget>

#include <string>
#include <vector>

class QCheckBox;
class QFormLayout;
class QLabel;
class QTimer;
class QVBoxLayout;

namespace se {

// The "Misc" tab: identifiers, flags and dimensions, status toggles, and a button for every scene
// that uses the source (click one to jump to it). GUI thread only; polls while visible.
class MiscPanel final : public QWidget {
	Q_OBJECT
public:
	explicit MiscPanel(QWidget *parent = nullptr);
	~MiscPanel() override;

	void setItem(obs_sceneitem_t *item); // nullptr clears
	void setSource(obs_source_t *source); // a source with no scene item

private slots:
	void refresh();

private:
	void buildUi();
	obs_source_t *shown() const;
	void rebuildSceneButtons(const std::vector<std::string> &names);

	obs::SceneItemRef item_;
	obs::SourceRef source_;
	bool updating_ = false;

	QLabel *hint_ = nullptr;
	QLabel *id_ = nullptr;
	QLabel *unversionedId_ = nullptr;
	QLabel *kind_ = nullptr;
	QLabel *uuid_ = nullptr;
	QLabel *flags_ = nullptr;
	QLabel *dimensions_ = nullptr;
	QFormLayout *toggleForm_ = nullptr;
	QCheckBox *muted_ = nullptr;
	QCheckBox *visible_ = nullptr;
	QCheckBox *locked_ = nullptr;
	QLabel *active_ = nullptr;
	QLabel *showing_ = nullptr;
	QLabel *enabled_ = nullptr;
	QLabel *filters_ = nullptr;
	QVBoxLayout *scenes_ = nullptr;
	QLabel *scenesEmpty_ = nullptr;
	std::vector<std::string> sceneNames_; // what the buttons currently show
	QTimer *poll_ = nullptr;
};

} // namespace se
