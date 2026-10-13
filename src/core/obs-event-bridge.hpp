// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <obs-frontend-api.h>

#include <QObject>

namespace se {

// Converts OBS frontend events (delivered on whatever thread OBS chooses) into
// Qt signals that are always emitted on the bridge's own (GUI) thread.
// Construct and destroy on the GUI thread.
class ObsEventBridge final : public QObject {
	Q_OBJECT
public:
	explicit ObsEventBridge(QObject *parent = nullptr);
	~ObsEventBridge() override;

signals:
	// `event` is an obs_frontend_event value.
	void frontendEvent(int event);

private:
	static void onFrontendEvent(enum obs_frontend_event event, void *self);
};

} // namespace se
