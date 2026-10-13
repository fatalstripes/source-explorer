// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "core/obs-raii.hpp"
#include "core/payload-generator.hpp"

#include <QWidget>

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class QComboBox;
class QFormLayout;
class QLabel;
class QScrollArea;
class QTimer;

namespace se {

struct CommandSet;

// Renders the selected source's (or one of its filters') obs_properties as Qt
// widgets, exactly as OBS's own properties window would, plus the command copier
// on every field. GUI thread only; no OBS callbacks are registered (the panel
// polls while visible), so there is no cross-thread state.
class PropertiesPanel final : public QWidget {
	Q_OBJECT
public:
	explicit PropertiesPanel(QWidget *parent = nullptr);
	~PropertiesPanel() override;

	void setSource(obs_source_t *source); // nullptr clears

private slots:
	void poll();
	void onTargetChanged(int);
	void rebuildForm();

private:
	using ValueFn = std::function<Json()>;

	std::vector<std::string> filterNames() const;
	bool rebuildTargets(bool keepSelection); // true if the target list changed
	void buildProperties(obs_properties_t *props, QWidget *parent, QFormLayout *form);
	QWidget *makeEditor(obs_property_t *prop, QWidget *parent, ValueFn &valueOut);
	void addRow(obs_property_t *prop, QWidget *parent, QFormLayout *form);

	void commit(obs_property_t *prop); // after writing settings_: notify, apply, refresh
	void scheduleRebuild();
	void resetProperty(const std::string &name);
	std::optional<CommandSet> commandsFor(const std::string &name, Json value);
	// Label right-click: this property, and every property as one full payload.
	std::optional<CommandSet> labelCommandsFor(const std::string &name, const std::string &title, Json value);
	// (Set, Get) requests for the loaded target: SetInputSettings for an input, the filter
	// equivalents for a filter. `settings` is what the Set carries.
	std::pair<ObsRequest, ObsRequest> requestsFor(Json settings) const;
	bool userIsEditing() const;
	std::string settingsJson() const;

	obs::SourceRef source_;
	obs::SourceRef target_; // the source itself or the chosen filter
	obs::PropertiesRef props_;
	obs::DataRef settings_;
	std::string lastJson_;
	std::string targetsSignature_;
	unsigned gen_ = 0; // bumped on every rebuild; stale widget callbacks compare against it
	bool building_ = false;

	QComboBox *targets_ = nullptr;
	QLabel *status_ = nullptr;
	QScrollArea *scroll_ = nullptr;
	QWidget *content_ = nullptr;
	QTimer *poll_ = nullptr;
};

} // namespace se
