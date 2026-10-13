// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QListWidget>

class QTimer;

namespace se {

// List that lets the user drag one row to a new position but never edits its own
// model: it reports the move and the owner reorders the real data, then repopulates.
// The reported destination uses Qt's "insert before row `destRow` in the pre-move
// list" convention (0..count); see core/list-reorder.hpp for converting it.
class ReorderListWidget final : public QListWidget {
	Q_OBJECT
public:
	explicit ReorderListWidget(QWidget *parent = nullptr);

	// While a row is being dragged, scrolls the enclosing scroll area when the cursor is near its top or
	// bottom edge (the list itself is as tall as its rows, so it never scrolls on its own).
	// Called on a timer during a drag; public so it can be tested.
	void scrollParentTowards(const QPoint &globalPos);

signals:
	void rowMoveRequested(int from, int destRow);

protected:
	void dragEnterEvent(QDragEnterEvent *event) override;
	void dragLeaveEvent(QDragLeaveEvent *event) override;
	void dropEvent(QDropEvent *event) override;

private:
	QTimer *autoScroll_ = nullptr;
};

} // namespace se
