// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "core/obs-raii.hpp"
#include "core/payload-generator.hpp"

#include <QPointer>
#include <QWidget>

class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QToolButton;
class QTimer;

namespace se {

// The "Source" tab: name, scene item ID and stacking index, size, how many scenes use the source,
// show/hide transitions, blend mode and method, scale filtering and deinterlacing.
// Rows that need a scene item (everything except name/type/size/scene count) hide when the source
// is shown without one (a global audio device); deinterlacing hides for non-video-capture sources.
// GUI thread only; polls while visible, so edits made elsewhere show up.
class SourcePanel final : public QWidget {
	Q_OBJECT
public:
	explicit SourcePanel(QWidget *parent = nullptr);
	~SourcePanel() override;

	void setItem(obs_sceneitem_t *item); // nullptr clears
	void setSource(obs_source_t *source); // a source with no scene item

private slots:
	void refresh();

private:
	void buildUi();
	obs_source_t *shown() const;
	SceneItemTarget target() const;
	QComboBox *makeTransitionBox(bool show);
	void applyTransition(bool show, const QString &typeId);
	void say(const QString &message);

	obs::SceneItemRef item_;
	obs::SourceRef source_; // used when there is no item
	bool updating_ = false;

	QFormLayout *form_ = nullptr;
	QLineEdit *name_ = nullptr;
	QLabel *type_ = nullptr;
	QLabel *itemId_ = nullptr;
	QSpinBox *index_ = nullptr;
	QLabel *size_ = nullptr;
	QLabel *sceneCount_ = nullptr;
	QPushButton *screenshot_ = nullptr;
	QWidget *showRow_ = nullptr;
	QComboBox *showType_ = nullptr;
	QToolButton *showProps_ = nullptr;
	QToolButton *hideProps_ = nullptr;
	QPointer<QWidget> transitionWindow_[2]; // [0] hide, [1] show
	void openTransitionSettings(bool show);
	QSpinBox *showMs_ = nullptr;
	QWidget *hideRow_ = nullptr;
	QComboBox *hideType_ = nullptr;
	QSpinBox *hideMs_ = nullptr;
	QComboBox *blendMode_ = nullptr;
	QComboBox *blendMethod_ = nullptr;
	QComboBox *scaleFilter_ = nullptr;
	QComboBox *deinterlace_ = nullptr;
	QComboBox *fieldOrder_ = nullptr;
	QLabel *status_ = nullptr;
	QLabel *hint_ = nullptr;
	QTimer *poll_ = nullptr;
};

} // namespace se
