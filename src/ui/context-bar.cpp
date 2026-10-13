#include "ui/context-bar.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

namespace se {

QToolButton *ContextBar::makeSegment(const QString &objectName)
{
	auto *b = new QToolButton(this);
	b->setObjectName(objectName);
	b->setAutoRaise(true);
	b->setToolButtonStyle(Qt::ToolButtonTextOnly);
	return b;
}

ContextBar::ContextBar(QWidget *parent) : QWidget(parent)
{
	setObjectName("seContextBar");
	auto *col = new QVBoxLayout(this);
	col->setContentsMargins(0, 0, 0, 0);
	col->setSpacing(2);

	auto *row = new QHBoxLayout;
	row->setContentsMargins(0, 0, 0, 0);
	row->setSpacing(2);
	scene_ = makeSegment("seCrumb");
	group_ = makeSegment("seCrumb");
	source_ = makeSegment("seCrumb");
	sep1_ = new QLabel(QStringLiteral(">"), this);
	sep2_ = new QLabel(QStringLiteral(">"), this);
	for (QLabel *s : {sep1_, sep2_})
		s->setForegroundRole(QPalette::PlaceholderText);
	row->addWidget(scene_);
	row->addWidget(sep1_);
	row->addWidget(group_);
	row->addWidget(sep2_);
	row->addWidget(source_);
	row->addStretch(1);
	col->addLayout(row);

	warn_ = new QLabel(this);
	warn_->setObjectName("seWarn");
	warn_->setWordWrap(true);
	warn_->setVisible(false);
	col->addWidget(warn_);

	connect(scene_, &QToolButton::clicked, this, &ContextBar::sceneClicked);
	connect(group_, &QToolButton::clicked, this, &ContextBar::groupClicked);
	connect(source_, &QToolButton::clicked, this, &ContextBar::sourceClicked);

	setContext({});
}

void ContextBar::setContext(const ContextInfo &info)
{
	auto show = [](QToolButton *b, const QString &label, const QString &name) {
		b->setVisible(!name.isEmpty());
		b->setText(label + name);
		b->setToolTip(name);
	};
	show(scene_, tr("Scene: "), info.scene);
	show(group_, tr("Group: "), info.group);
	show(source_, tr("Source: "), info.source);

	// A separator only shows between two visible segments.
	sep1_->setVisible(!info.scene.isEmpty() && (!info.group.isEmpty() || !info.source.isEmpty()));
	sep2_->setVisible(!info.group.isEmpty() && !info.source.isEmpty());

	warn_->setVisible(info.warn);
	warn_->setText(info.warnText);
	setVisible(!info.scene.isEmpty() || !info.group.isEmpty() || !info.source.isEmpty());
}

} // namespace se
