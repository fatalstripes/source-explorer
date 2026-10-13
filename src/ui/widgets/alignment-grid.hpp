// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QWidget>

class QButtonGroup;

namespace se {

// 3x3 anchor picker. Value is an OBS alignment bitmask
// (Left=1, Right=2, Top=4, Bottom=8; 0 on an axis means centered).
class AlignmentGrid final : public QWidget {
	Q_OBJECT
public:
	explicit AlignmentGrid(QWidget *parent = nullptr);

	int alignment() const;
	void setAlignment(int alignment); // does not emit

signals:
	void alignmentChanged(int alignment); // user-initiated only

private:
	QButtonGroup *group_ = nullptr;
};

} // namespace se
