// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "core/obs-raii.hpp"
#include "core/payload-generator.hpp"
#include "ui/widgets/command-menu.hpp"

#include <QPointer>
#include <QWidget>

#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;

namespace se {

// Volume fader, mute, sync offset and monitoring for one audio source.
// GUI thread only; OBS notifications are queued in from whatever thread emits them.
class AudioPanel final : public QWidget {
	Q_OBJECT
public:
	explicit AudioPanel(QWidget *parent = nullptr);
	~AudioPanel() override;

	void setSource(obs_source_t *source); // nullptr or a source without audio disables the panel
	obs_source_t *source() const { return source_.get(); }

private slots:
	void reload();

private:
	void buildUi();
	void connectSignals();
	void disconnectSignals();
	void attachRequestMenu(QWidget *w, std::function<ObsRequest()> setReq, std::function<ObsRequest()> getReq,
			       std::function<void()> reset);

	void setVolumeDb(double db);  // user edit (slider, or the box in dB mode) -> OBS
	void setVolumeMul(double mul); // the one place that writes volume and mirrors both controls
	void configureVolumeBox();     // range/suffix for dB or % mode
	AudioTracks tracksFromUi() const;
	void applyTracks();
	void attachChoiceMenu(QWidget *label, std::function<std::vector<CommandChoice>()> make,
			      std::function<void()> reset);
	std::string inputName() const;

	obs::SourceRef source_;
	bool updating_ = false;
	bool signalsConnected_ = false;
	QPointer<AudioPanel> *slot_ = nullptr; // libobs callback data; freed in disconnectSignals()

	QLabel *status_ = nullptr;
	QSlider *slider_ = nullptr;
	QDoubleSpinBox *dbBox_ = nullptr;
	QPushButton *mute_ = nullptr;
	QSpinBox *offset_ = nullptr;
	QComboBox *monitor_ = nullptr;
	QPushButton *unit_ = nullptr; // dB <-> %
	bool percent_ = false;
	QSlider *balance_ = nullptr; // -100 (left) .. +100 (right); OBS stores 0..1
	QCheckBox *mono_ = nullptr;
	QCheckBox *tracks_[6] = {};
};

} // namespace se
