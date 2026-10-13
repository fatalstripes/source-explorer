// SPDX-License-Identifier: GPL-2.0-or-later
#include "ui/widgets/reorder-list.hpp"

#include <QDropEvent>

namespace se {

ReorderListWidget::ReorderListWidget(QWidget *parent) : QListWidget(parent)
{
	setSelectionMode(QAbstractItemView::SingleSelection);
	setDragEnabled(true);
	setAcceptDrops(true);
	setDropIndicatorShown(true);
	setDragDropMode(QAbstractItemView::InternalMove);
	setDefaultDropAction(Qt::MoveAction);
}

void ReorderListWidget::dropEvent(QDropEvent *event)
{
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
