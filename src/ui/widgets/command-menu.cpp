// SPDX-License-Identifier: GPL-2.0-or-later
#include "ui/widgets/command-menu.hpp"

#include "util/clipboard.hpp"

#include <QAbstractSpinBox>
#include <QLineEdit>
#include <QMenu>

namespace se {

static void showMenu(QWidget *anchor, const QPoint &pos, const std::function<std::optional<CommandSet>()> &provider)
{
	auto cmds = provider();
	if (!cmds)
		return;

	QMenu menu(anchor);
	auto add = [&](const QString &text, const std::function<std::string()> &gen) {
		if (!gen)
			return;
		menu.addAction(text, [gen] { util::copyToClipboard(gen()); });
	};
	add(QObject::tr("Copy Streamer.Bot C# Command"), cmds->streamerBot);
	add(QObject::tr("Copy OBS WebSocket v5 (Set)"), cmds->wsSet);
	add(QObject::tr("Copy OBS WebSocket v5 (Get)"), cmds->wsGet);
	for (const auto &[label, gen] : cmds->extras)
		add(QString::fromStdString(label), gen);

	// Each choice is a submenu with the same three copies.
	for (const CommandChoice &choice : cmds->choices) {
		QMenu *sub = menu.addMenu(QString::fromStdString(choice.label));
		auto addTo = [&](const QString &text, const std::function<std::string()> &gen) {
			if (gen)
				sub->addAction(text, [gen] { util::copyToClipboard(gen()); });
		};
		addTo(QObject::tr("Copy Streamer.Bot C# Command"), choice.streamerBot);
		addTo(QObject::tr("Copy OBS WebSocket v5 (Set)"), choice.wsSet);
		addTo(QObject::tr("Copy OBS WebSocket v5 (Get)"), choice.wsGet);
	}

	if (cmds->reset || !cmds->resets.empty())
		menu.addSeparator();
	if (cmds->reset)
		menu.addAction(QObject::tr("Reset to Default"), [reset = cmds->reset] { reset(); });
	for (const auto &[label, action] : cmds->resets)
		menu.addAction(QString::fromStdString(label), [action] { action(); });
	if (menu.isEmpty())
		return;
	menu.exec(anchor->mapToGlobal(pos));
}

void attachCommandMenu(QWidget *w, std::function<std::optional<CommandSet>()> provider)
{
	auto hook = [provider](QWidget *target) {
		target->setContextMenuPolicy(Qt::CustomContextMenu);
		QObject::connect(target, &QWidget::customContextMenuRequested, target,
				 [target, provider](const QPoint &p) { showMenu(target, p, provider); });
	};
	hook(w);
	if (qobject_cast<QAbstractSpinBox *>(w)) {
		for (auto *le : w->findChildren<QLineEdit *>())
			hook(le);
	}
}

} // namespace se
