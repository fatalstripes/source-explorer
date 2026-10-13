#pragma once
#include <QLayout>
#include <QList>

namespace se {

// Lays widgets out left to right and wraps onto new rows when the width runs out, so a row of
// buttons reflows in a narrow dock instead of overflowing it.
class FlowLayout final : public QLayout {
public:
	explicit FlowLayout(QWidget *parent = nullptr, int spacing = 4);
	~FlowLayout() override;

	void addItem(QLayoutItem *item) override;
	int count() const override;
	QLayoutItem *itemAt(int index) const override;
	QLayoutItem *takeAt(int index) override;

	Qt::Orientations expandingDirections() const override { return {}; }
	bool hasHeightForWidth() const override { return true; }
	int heightForWidth(int width) const override;
	void setGeometry(const QRect &rect) override;
	QSize sizeHint() const override;
	QSize minimumSize() const override;

private:
	int doLayout(const QRect &rect, bool testOnly) const;

	QList<QLayoutItem *> items_;
	int spacing_;
};

} // namespace se
