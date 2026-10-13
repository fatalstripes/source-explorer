// SPDX-License-Identifier: GPL-2.0-or-later
#include "util/clipboard.hpp"

#include <QClipboard>
#include <QGuiApplication>
#include <QString>

namespace se::util {

void copyToClipboard(const std::string &utf8Text)
{
	if (auto *cb = QGuiApplication::clipboard())
		cb->setText(QString::fromStdString(utf8Text));
}

} // namespace se::util
