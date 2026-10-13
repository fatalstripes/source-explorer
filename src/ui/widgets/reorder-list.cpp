// SPDX-License-Identifier: GPL-2.0-or-later
#include "ui/widgets/reorder-list.hpp"

#include <algorithm>

#include <QApplication>
#include <QCursor>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>

namespace se {

ReorderListWidget::ReorderListWidget(QWidget *parent) : QListWidget(parent)
{
	setSelectionMode(QAbstractItemView::SingleSelection);
	setDragEnabled(true);
	setAcceptDrops(true);
	setDropIndicatorShown(true);
	setDragDropMode(QAbstractItemView::InternalMove);
	setDefaultDropAction(Qt::MoveAction);

	autoScroll_ = new QTimer(this);
	autoScroll_->setInterval(30);
	connect(autoScroll_, &QTimer::timeout, this, [this] {
		if (QApplication::mouseButtons() == Qt::NoButton) { // the drag ended without a drop here
			autoScroll_->stop();
			return;
		}
		scrollParentTowards(QCursor::pos());
	});
}

void ReorderListWidget::scrollParentTowards(const QPoint &globalPos)
{
	for (QWidget *w = parentWidget(); w; w = w->parentWidget()) {
		auto *area = qobject_cast<QScrollArea *>(w);
		if (!area)
			continue;
		constexpr int kMargin = 36;
		const QPoint p = area->viewport()->mapFromGlobal(globalPos);
		const int h = area->viewport()->height();
		int step = 0;
		if (p.y() < kMargin)
			step = -(6 + (kMargin - std::max(p.y(), 0)) / 2);
		else if (p.y() > h - kMargin)
			step = 6 + (std::min(p.y(), h) - (h - kMargin)) / 2;
		if (step)
			area->verticalScrollBar()->setValue(area->verticalScrollBar()->value() + step);
		return;
	}
}

void ReorderListWidget::dragEnterEvent(QDragEnterEvent *event)
{
	QListWidget::dragEnterEvent(event);
	autoScroll_->start();
}

void ReorderListWidget::dragLeaveEvent(QDragLeaveEvent *event)
{
	QListWidget::dragLeaveEvent(event);
	// Keep scrolling: the cursor is still dragging, it just left this list for another part of the panel.
}

void ReorderListWidget::dropEvent(QDropEvent *event)
{
	autoScroll_->stop();
	if (event->source() != this) {
		event->ignore();
		return;
	}

	const int from = currentRow();
	const QModelIndex over = indexAt(event->position().toPoint());
	int dest = count(); // dropped in empty space: append
	if (over.isValid()) {
		dest = over.row();
		if (dropIndicatorPosition() == QAbstractItemView::BelowItem)
			++dest;
	}

	// Accept with IgnoreAction so the view does not remove the source row itself;
	// the owner rebuilds the list from the authoritative data.
	event->setDropAction(Qt::IgnoreAction);
	event->accept();
	if (from >= 0)
		emit rowMoveRequested(from, dest);
}

} // namespace se
