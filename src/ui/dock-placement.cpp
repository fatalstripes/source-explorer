// SPDX-License-Identifier: GPL-2.0-or-later
#include "ui/dock-placement.hpp"

#include "core/frame-fit.hpp"

#include <QDockWidget>
#include <QEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>

namespace se {

namespace {
constexpr Size kPreferredFrame{420, 640}; // logical pixels, title bar included
}

FloatingDockRescue::FloatingDockRescue(QDockWidget *dock) : QObject(dock), dock_(dock)
{
	dock_->installEventFilter(this);
	// Floating a docked dock, or docking it again, changes what "usable" means.
	connect(dock_, &QDockWidget::topLevelChanged, this, [this](bool) { schedule(); });
	schedule(); // covers a dock that OBS already restored as visible and floating
}

bool FloatingDockRescue::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == dock_ && event->type() == QEvent::Show)
		schedule();
	return QObject::eventFilter(watched, event);
}

// Defer: at Show time the native frame is not final yet.
void FloatingDockRescue::schedule()
{
	QTimer::singleShot(0, this, &FloatingDockRescue::rescue);
}

void FloatingDockRescue::rescue()
{
	if (busy_ || !dock_->isFloating() || !dock_->isVisible())
		return;

	const QRect frame = dock_->frameGeometry();
	// Use the screen the window is mostly on; fall back to the main window's screen
	// (also covers a window that is entirely off-screen).
	QScreen *screen = QGuiApplication::screenAt(frame.center());
	if (!screen && dock_->parentWidget())
		screen = dock_->parentWidget()->screen();
	if (!screen)
		screen = QGuiApplication::primaryScreen();
	if (!screen)
		return;

	const QRect avail = screen->availableGeometry();
	const Rect fit = fitFloatingFrame({frame.x(), frame.y(), frame.width(), frame.height()},
					  {avail.x(), avail.y(), avail.width(), avail.height()}, kPreferredFrame);
	if (fit == Rect{frame.x(), frame.y(), frame.width(), frame.height()})
		return;

	// resize() takes the client size, move() the frame position (decoration included).
	const QRect client = dock_->geometry();
	const int decoW = frame.width() - client.width();
	const int decoH = frame.height() - client.height();

	busy_ = true;
	dock_->resize(fit.w - decoW, fit.h - decoH);
	dock_->move(fit.x, fit.y);
	busy_ = false;
}

} // namespace se
