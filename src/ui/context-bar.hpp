// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
#include <QWidget>

class QLabel;
class QToolButton;

namespace se {

// What the dock is showing, as breadcrumbs: Scene > Group > Source.
struct ContextInfo {
	QString scene;  // empty = segment hidden
	QString group;
	QString source;
	bool warn = false; // the loaded item is not in the scene being edited
	QString warnText;
};

// The status bar under the identity bar. Each segment is a button: click it to jump to that context
// (load the scene or group itself, or return to the source). GUI thread only.
class ContextBar final : public QWidget {
	Q_OBJECT
public:
	explicit ContextBar(QWidget *parent = nullptr);

	void setContext(const ContextInfo &info);

signals:
	void sceneClicked();
	void groupClicked();
	void sourceClicked();

private:
	QToolButton *makeSegment(const QString &objectName);

	QToolButton *scene_ = nullptr;
	QToolButton *group_ = nullptr;
	QToolButton *source_ = nullptr;
	QLabel *sep1_ = nullptr;
	QLabel *sep2_ = nullptr;
	QLabel *warn_ = nullptr;
};

} // namespace se
