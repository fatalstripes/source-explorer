// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>

class QDockWidget;

namespace se {

// While a dock is floating, makes sure that every time it is shown it is a usable size and that
// its title bar is fully on screen, so it can always be grabbed and moved. Docked docks are left
// alone. Also repairs a bad position that OBS restored from its saved layout.
// The geometry rules live in core/frame-fit.hpp (unit tested).
class FloatingDockRescue final : public QObject {
	Q_OBJECT
public:
	explicit FloatingDockRescue(QDockWidget *dock); // parented to `dock`

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	void schedule();
	void rescue();

	QDockWidget *dock_;
	bool busy_ = false;
};

} // namespace se
