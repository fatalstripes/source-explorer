#pragma once
#include <QListWidget>

namespace se {

// List that lets the user drag one row to a new position but never edits its own
// model: it reports the move and the owner reorders the real data, then repopulates.
// The reported destination uses Qt's "insert before row `destRow` in the pre-move
// list" convention (0..count); see core/list-reorder.hpp for converting it.
class ReorderListWidget final : public QListWidget {
	Q_OBJECT
public:
	explicit ReorderListWidget(QWidget *parent = nullptr);

signals:
	void rowMoveRequested(int from, int destRow);

protected:
	void dropEvent(QDropEvent *event) override;
};

} // namespace se
