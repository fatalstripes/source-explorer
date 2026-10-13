// SPDX-License-Identifier: GPL-2.0-or-later
#include "ui/widgets/flow-layout.hpp"

#include <QWidget>

#include <algorithm>

namespace se {

FlowLayout::FlowLayout(QWidget *parent, int spacing) : QLayout(parent), spacing_(spacing)
{
	setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout()
{
	while (QLayoutItem *item = takeAt(0))
		delete item;
}

void FlowLayout::addItem(QLayoutItem *item)
{
	items_.append(item);
}

int FlowLayout::count() const
{
	return int(items_.size());
}

QLayoutItem *FlowLayout::itemAt(int index) const
{
	return index >= 0 && index < items_.size() ? items_.at(index) : nullptr;
}

QLayoutItem *FlowLayout::takeAt(int index)
{
	return index >= 0 && index < items_.size() ? items_.takeAt(index) : nullptr;
}

int FlowLayout::heightForWidth(int width) const
{
	return doLayout(QRect(0, 0, width, 0), true);
}

void FlowLayout::setGeometry(const QRect &rect)
{
	QLayout::setGeometry(rect);
	doLayout(rect, false);
}

QSize FlowLayout::sizeHint() const
{
	return minimumSize();
}

QSize FlowLayout::minimumSize() const
{
	QSize size;
	for (const QLayoutItem *item : items_)
		size = size.expandedTo(item->minimumSize()); // the widest single item must always fit
	const QMargins m = contentsMargins();
	return size + QSize(m.left() + m.right(), m.top() + m.bottom());
}

// Returns the height used. With testOnly the items are measured but not moved.
int FlowLayout::doLayout(const QRect &rect, bool testOnly) const
{
	const QMargins m = contentsMargins();
	const QRect area = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
	int x = area.x(), y = area.y(), rowHeight = 0;

	for (QLayoutItem *item : items_) {
		const QSize hint = item->sizeHint();
		if (x > area.x() && x + hint.width() > area.right() + 1) { // does not fit: next row
			x = area.x();
			y += rowHeight + spacing_;
			rowHeight = 0;
		}
		if (!testOnly)
			item->setGeometry(QRect(QPoint(x, y), hint));
		x += hint.width() + spacing_;
		rowHeight = std::max(rowHeight, hint.height());
	}
	return y + rowHeight - rect.y() + m.bottom();
}

} // namespace se
