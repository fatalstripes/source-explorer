// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/obs-event-bridge.hpp"

#include <obs-frontend-api.h>

#include <QMetaObject>
#include <QPointer>
#include <QThread>

namespace se {

ObsEventBridge::ObsEventBridge(QObject *parent) : QObject(parent)
{
	obs_frontend_add_event_callback(&ObsEventBridge::onFrontendEvent, this);
}

ObsEventBridge::~ObsEventBridge()
{
	// Unregister first so no new callbacks can reference `this`. Events already
	// queued hold a QPointer and are dropped if we are gone.
	obs_frontend_remove_event_callback(&ObsEventBridge::onFrontendEvent, this);
}

void ObsEventBridge::onFrontendEvent(enum obs_frontend_event event, void *self)
{
	QPointer<ObsEventBridge> guard(static_cast<ObsEventBridge *>(self));
	// OBS normally delivers frontend events on the GUI thread. Emit those right away:
	// teardown events (scene collection changing, exit) must be handled before OBS
	// frees what the listeners still reference, which a queued delivery would miss.
	if (guard && guard->thread() == QThread::currentThread()) {
		emit guard->frontendEvent(int(event));
		return;
	}
	// Any other thread: never touch widgets here, only queue.
	QMetaObject::invokeMethod(
		guard.data(), [guard, event] {
			if (guard)
				emit guard->frontendEvent(int(event));
		},
		Qt::QueuedConnection);
}

} // namespace se
