#include "ui/panels/audio-panel.hpp"

#include "ui/widgets/command-menu.hpp"

#include <obs.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QMetaObject>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>
#include <vector>

namespace se {

// Our MonitorType enum is index-compatible with libobs; enforce it.
static_assert(static_cast<int>(MonitorType::None) == OBS_MONITORING_TYPE_NONE);
static_assert(static_cast<int>(MonitorType::MonitorOnly) == OBS_MONITORING_TYPE_MONITOR_ONLY);
static_assert(static_cast<int>(MonitorType::MonitorAndOutput) == OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT);

namespace {
constexpr int kSliderScale = 10; // slider units per dB (0.1 dB resolution)
constexpr int64_t kNsPerMs = 1'000'000;

double sourceDb(obs_source_t *src)
{
	const float mul = obs_source_get_volume(src);
	if (mul <= 0.0f)
		return kMinVolumeDb;
	return clampVolumeDb(obs_mul_to_db(mul));
}

const char *kSignals[] = {"volume", "mute", "audio_sync", "audio_monitoring", "audio_balance",
			  "audio_mixers"};

// libobs creates sources with every mixer track enabled (obs-source.c: audio_mixers = 0xFF).
constexpr uint32_t kTrackMask = 0x3F; // tracks 1-6 are the ones OBS shows
constexpr AudioTracks kDefaultTracks = {true, true, true, true, true, true};
} // namespace

AudioPanel::AudioPanel(QWidget *parent) : QWidget(parent)
{
	buildUi();
	setSource(nullptr);
}

AudioPanel::~AudioPanel()
{
	disconnectSignals();
}

std::string AudioPanel::inputName() const
{
	return source_ ? obs_source_get_name(source_.get()) : std::string();
}

void AudioPanel::attachRequestMenu(QWidget *w, std::function<ObsRequest()> setReq, std::function<ObsRequest()> getReq,
				   std::function<void()> reset)
{
	QPointer<AudioPanel> self(this);
	attachCommandMenu(w, [self, setReq = std::move(setReq), getReq = std::move(getReq),
			      reset = std::move(reset)]() -> std::optional<CommandSet> {
		if (!self || !self->source_)
			return std::nullopt;
		// Snapshot now: the menu action may run after the UI has changed.
		const ObsRequest set = setReq(), get = getReq();
		CommandSet c;
		c.streamerBot = [set] { return sbFrom(set); };
		c.wsSet = [set] { return wsFrom(set); };
		c.wsGet = [get] { return wsFrom(get); };
		c.reset = reset;
		return c;
	});
}

void AudioPanel::attachChoiceMenu(QWidget *label, std::function<std::vector<CommandChoice>()> make,
				  std::function<void()> reset)
{
	if (!label)
		return;
	QPointer<AudioPanel> self(this);
	attachCommandMenu(label, [self, make = std::move(make), reset = std::move(reset)]() -> std::optional<CommandSet> {
		if (!self || !self->source_)
			return std::nullopt;
		CommandSet c;
		c.choices = make(); // snapshot now, while the source is in the state the user is looking at
		c.reset = reset;
		return c;
	});
}

AudioTracks AudioPanel::tracksFromUi() const
{
	AudioTracks t{};
	for (size_t i = 0; i < t.size(); ++i)
		t[i] = tracks_[i]->isChecked();
	return t;
}

void AudioPanel::applyTracks()
{
	if (!source_)
		return;
	uint32_t mask = obs_source_get_audio_mixers(source_.get()) & ~kTrackMask; // keep tracks 7-8 as they are
	for (size_t i = 0; i < 6; ++i)
		if (tracks_[i]->isChecked())
			mask |= (1u << i);
	obs_source_set_audio_mixers(source_.get(), mask);
}

void AudioPanel::configureVolumeBox()
{
	const bool was = updating_;
	updating_ = true; // changing range/suffix can emit valueChanged; never write that to OBS
	{
		QSignalBlocker block(dbBox_);
		if (percent_) {
			dbBox_->setRange(0.0, kMaxVolumeMul * 100.0);
			dbBox_->setDecimals(0);
			dbBox_->setSingleStep(5.0);
			dbBox_->setSuffix(" %");
		} else {
			dbBox_->setRange(kMinVolumeDb, kMaxVolumeDb);
			dbBox_->setDecimals(1);
			dbBox_->setSingleStep(0.5);
			dbBox_->setSuffix(" dB");
		}
	}
	updating_ = was;
}

void AudioPanel::buildUi()
{
	auto *root = new QVBoxLayout(this);
	status_ = new QLabel(tr("Select a source with audio."), this);
	root->addWidget(status_);

	auto *form = new QFormLayout;
	root->addLayout(form);

	// Volume: slider (always dB) + exact entry that can show dB or percent.
	slider_ = new QSlider(Qt::Horizontal, this);
	slider_->setRange(int(kMinVolumeDb * kSliderScale), int(kMaxVolumeDb * kSliderScale));
	slider_->setPageStep(10 * kSliderScale);
	dbBox_ = new QDoubleSpinBox(this);
	configureVolumeBox();
	unit_ = new QPushButton(tr("dB"), this);
	unit_->setCheckable(true);
	unit_->setToolTip(tr("Show volume in dB or as a percentage"));
	auto *volRow = new QHBoxLayout;
	volRow->addWidget(slider_, 1);
	volRow->addWidget(dbBox_);
	volRow->addWidget(unit_);
	form->addRow(tr("Volume"), volRow);

	// Balance: -100 (left) .. +100 (right), with a reset to centre.
	balance_ = new QSlider(Qt::Horizontal, this);
	balance_->setRange(-100, 100);
	balance_->setPageStep(10);
	auto *balanceReset = new QPushButton(tr("Center"), this);
	auto *balanceRow = new QHBoxLayout;
	balanceRow->addWidget(balance_, 1);
	balanceRow->addWidget(balanceReset);
	form->addRow(tr("Balance"), balanceRow);

	mute_ = new QPushButton(tr("Mute"), this); // reads "Muted" and lights up while muted
	mute_->setCheckable(true);
	form->addRow(tr("Mute"), mute_);

	mono_ = new QCheckBox(tr("Downmix to mono"), this);
	mono_->setToolTip(tr("Not available through obs-websocket, so there is no command to copy."));
	form->addRow(tr("Mono"), mono_);

	offset_ = new QSpinBox(this);
	offset_->setRange(kMinSyncOffsetMs, kMaxSyncOffsetMs);
	offset_->setSuffix(" ms");
	form->addRow(tr("Sync offset"), offset_);

	monitor_ = new QComboBox(this);
	monitor_->addItem(tr("Monitor Off"));
	monitor_->addItem(tr("Monitor Only (mute output)"));
	monitor_->addItem(tr("Monitor and Output"));
	form->addRow(tr("Monitoring"), monitor_);

	auto *trackRow = new QHBoxLayout;
	for (int i = 0; i < 6; ++i) {
		tracks_[i] = new QCheckBox(QString::number(i + 1), this);
		trackRow->addWidget(tracks_[i]);
	}
	trackRow->addStretch(1);
	form->addRow(tr("Tracks"), trackRow);
	root->addStretch(1);

	// Edits -> OBS -------------------------------------------------------------
	connect(slider_, &QSlider::valueChanged, this, [this](int v) {
		if (updating_)
			return;
		setVolumeDb(double(v) / kSliderScale);
	});
	connect(dbBox_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
		if (updating_)
			return;
		if (percent_)
			setVolumeMul(v / 100.0);
		else
			setVolumeDb(v);
	});
	connect(unit_, &QPushButton::toggled, this, [this](bool pct) {
		percent_ = pct;
		unit_->setText(pct ? tr("%") : tr("dB"));
		configureVolumeBox();
		reload();
	});
	connect(balance_, &QSlider::valueChanged, this, [this](int v) {
		if (!updating_ && source_)
			obs_source_set_balance_value(source_.get(), float(clampBalance((v + 100) / 200.0)));
	});
	connect(balanceReset, &QPushButton::clicked, this, [this] { balance_->setValue(0); });
	connect(mute_, &QPushButton::toggled, this, [this](bool on) {
		mute_->setText(on ? tr("Muted") : tr("Mute"));
		if (!updating_ && source_)
			obs_source_set_muted(source_.get(), on);
	});
	connect(mono_, &QCheckBox::toggled, this, [this](bool on) {
		if (updating_ || !source_)
			return;
		const uint32_t flags = obs_source_get_flags(source_.get());
		obs_source_set_flags(source_.get(), on ? (flags | OBS_SOURCE_FLAG_FORCE_MONO)
						       : (flags & ~uint32_t(OBS_SOURCE_FLAG_FORCE_MONO)));
	});
	connect(offset_, &QSpinBox::valueChanged, this, [this](int ms) {
		if (!updating_ && source_)
			obs_source_set_sync_offset(source_.get(), int64_t(clampSyncOffsetMs(ms)) * kNsPerMs);
	});
	connect(monitor_, &QComboBox::currentIndexChanged, this, [this](int idx) {
		if (!updating_ && source_)
			obs_source_set_monitoring_type(source_.get(), static_cast<obs_monitoring_type>(idx));
	});
	for (QCheckBox *t : tracks_)
		connect(t, &QCheckBox::toggled, this, [this] {
			if (!updating_)
				applyTracks();
		});

	// Command copier: right-click a FIELD for that one value ------------------------------
	// (volume follows the unit shown; the label offers both units.)
	auto volSet = [this] {
		return percent_ ? reqSetInputVolumeMul(inputName(), dbBox_->value() / 100.0)
				: reqSetInputVolumeDb(inputName(), dbBox_->value());
	};
	auto volGet = [this] { return reqGetInputVolume(inputName()); };
	auto volReset = [this] { setVolumeDb(0.0); };
	attachRequestMenu(slider_, [this] { return reqSetInputVolumeDb(inputName(), double(slider_->value()) / kSliderScale); },
			  volGet, volReset);
	attachRequestMenu(dbBox_, volSet, volGet, volReset);
	attachRequestMenu(
		balance_, [this] { return reqSetInputAudioBalance(inputName(), (balance_->value() + 100) / 200.0); },
		[this] { return reqGetInputAudioBalance(inputName()); }, [this] { balance_->setValue(0); });
	attachRequestMenu(
		mute_, [this] { return reqSetInputMute(inputName(), mute_->isChecked()); },
		[this] { return reqGetInputMute(inputName()); }, [this] { mute_->setChecked(false); });
	attachRequestMenu(
		offset_, [this] { return reqSetInputAudioSyncOffset(inputName(), offset_->value()); },
		[this] { return reqGetInputAudioSyncOffset(inputName()); }, [this] { offset_->setValue(0); });
	attachRequestMenu(
		monitor_,
		[this] {
			return reqSetInputAudioMonitorType(inputName(), static_cast<MonitorType>(monitor_->currentIndex()));
		},
		[this] { return reqGetInputAudioMonitorType(inputName()); }, [this] { monitor_->setCurrentIndex(0); });
	// obs-websocket sets all six tracks at once, so each box copies the whole set as shown.
	auto tracksSet = [this] { return reqSetInputAudioTracks(inputName(), tracksFromUi()); };
	auto tracksGet = [this] { return reqGetInputAudioTracks(inputName()); };
	auto tracksReset = [this] {
		for (size_t i = 0; i < 6; ++i)
			tracks_[i]->setChecked(kDefaultTracks[i]);
	};
	for (QCheckBox *t : tracks_)
		attachRequestMenu(t, tracksSet, tracksGet, tracksReset);

	// Right-click a LABEL for the multi-value / alternative-unit commands ------------------
	attachChoiceMenu(
		form->labelForField(volRow),
		[this] {
			obs_source_t *src = source_.get();
			return std::vector<CommandChoice>{
				makeChoice("Volume (dB)", reqSetInputVolumeDb(inputName(), sourceDb(src)),
					   reqGetInputVolume(inputName())),
				makeChoice("Volume (%)", reqSetInputVolumeMul(inputName(), obs_source_get_volume(src)),
					   reqGetInputVolume(inputName()))};
		},
		volReset);
	attachChoiceMenu(
		form->labelForField(balanceRow),
		[this] {
			return std::vector<CommandChoice>{
				makeChoice("Balance", reqSetInputAudioBalance(inputName(), (balance_->value() + 100) / 200.0),
					   reqGetInputAudioBalance(inputName()))};
		},
		[this] { balance_->setValue(0); });
	attachChoiceMenu(
		form->labelForField(trackRow),
		[tracksSet, tracksGet] {
			return std::vector<CommandChoice>{makeChoice("Mixer tracks (1-6)", tracksSet(), tracksGet())};
		},
		tracksReset);
}

void AudioPanel::setVolumeDb(double db)
{
	db = clampVolumeDb(db);
	// At the floor OBS treats the source as silent (-inf dB).
	setVolumeMul(db <= kMinVolumeDb ? 0.0 : double(obs_db_to_mul(float(db))));
}

void AudioPanel::setVolumeMul(double mul)
{
	if (!source_)
		return;
	mul = clampVolumeMul(mul);
	obs_source_set_volume(source_.get(), float(mul));
	const double db = mul <= 0.0 ? kMinVolumeDb : clampVolumeDb(double(obs_mul_to_db(float(mul))));
	// Mirror into the sibling control without re-entering.
	updating_ = true;
	{
		QSignalBlocker a(slider_), b(dbBox_);
		slider_->setValue(int(std::lround(db * kSliderScale)));
		dbBox_->setValue(percent_ ? mul * 100.0 : db);
	}
	updating_ = false;
}

// ---------------------------------------------------------------------------
// Source binding
// ---------------------------------------------------------------------------
void AudioPanel::setSource(obs_source_t *source)
{
	if (source && source == source_.get())
		return;
	disconnectSignals();
	const bool hasAudio = source && (obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO);
	source_ = hasAudio ? obs::retain(source) : obs::SourceRef();
	connectSignals();

	const bool on = static_cast<bool>(source_);
	std::vector<QWidget *> controls = {slider_, dbBox_, unit_, balance_, mono_, mute_, offset_, monitor_};
	for (QCheckBox *t : tracks_)
		controls.push_back(t);
	for (QWidget *w : controls)
		w->setEnabled(on);
	status_->setText(on ? QString::fromUtf8(obs_source_get_name(source_.get()))
			    : (source ? tr("This source has no audio.") : tr("Select a source with audio.")));
	reload();
}

void AudioPanel::reload()
{
	if (!source_)
		return;
	obs_source_t *src = source_.get();
	const double db = sourceDb(src);
	const float mul = obs_source_get_volume(src);
	const float balance = obs_source_get_balance_value(src);
	const uint32_t mixers = obs_source_get_audio_mixers(src);

	updating_ = true;
	{
		QSignalBlocker a(slider_), b(dbBox_), c(mute_), d(offset_), e(monitor_), f(balance_), g(mono_);
		if (!slider_->isSliderDown()) // don't fight an in-progress drag
			slider_->setValue(int(std::lround(db * kSliderScale)));
		if (!dbBox_->hasFocus())
			dbBox_->setValue(percent_ ? double(mul) * 100.0 : db);
		if (!balance_->isSliderDown())
			balance_->setValue(int(std::lround((double(balance) - 0.5) * 200.0)));
		mono_->setChecked((obs_source_get_flags(src) & OBS_SOURCE_FLAG_FORCE_MONO) != 0);
		mute_->setChecked(obs_source_muted(src));
		mute_->setText(mute_->isChecked() ? tr("Muted") : tr("Mute")); // signals are blocked here
		if (!offset_->hasFocus())
			offset_->setValue(int(obs_source_get_sync_offset(src) / kNsPerMs));
		monitor_->setCurrentIndex(static_cast<int>(obs_source_get_monitoring_type(src)));
		for (size_t i = 0; i < 6; ++i) {
			QSignalBlocker t(tracks_[i]);
			tracks_[i]->setChecked((mixers & (1u << i)) != 0);
		}
	}
	updating_ = false;
}

// Source signals can fire on any thread (UI, audio, scripting). Only queue.
// libobs serializes disconnect against in-flight callbacks, so `data` is valid
// for the duration of this call.
static void onAudioSignal(void *data, calldata_t *)
{
	QPointer<AudioPanel> guard = *static_cast<QPointer<AudioPanel> *>(data);
	if (!guard)
		return;
	QMetaObject::invokeMethod(guard.data(), "reload", Qt::QueuedConnection);
}

void AudioPanel::connectSignals()
{
	if (!source_)
		return;
	if (signal_handler_t *sh = obs_source_get_signal_handler(source_.get())) {
		slot_ = new QPointer<AudioPanel>(this);
		for (const char *sig : kSignals)
			signal_handler_connect(sh, sig, &onAudioSignal, slot_);
		signalsConnected_ = true;
	}
}

void AudioPanel::disconnectSignals()
{
	if (!signalsConnected_)
		return;
	if (source_) {
		if (signal_handler_t *sh = obs_source_get_signal_handler(source_.get())) {
			for (const char *sig : kSignals)
				signal_handler_disconnect(sh, sig, &onAudioSignal, slot_);
		}
	}
	delete slot_;
	slot_ = nullptr;
	signalsConnected_ = false;
}

} // namespace se
