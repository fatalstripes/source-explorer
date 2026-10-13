// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>

namespace se {

// Style for the whole dock. Neutral colours come from the palette() of the active OBS theme so the
// dock follows Yami/Acri/Rachni/light alike; only the status badges use fixed accent colours.
inline QString dockStyleSheet()
{
	return QStringLiteral(R"(
QToolButton#seIdentityName {
	border: none;
	background: transparent;
	font-size: 13px;
	font-weight: 600;
	padding: 0 2px;
}
QToolButton#seIdentityName:hover { text-decoration: underline; }
QLabel#seIcon { font-size: 14px; }

QToolButton#seCrumb { border: none; padding: 1px 4px; border-radius: 3px; }
QToolButton#seCrumb:hover { background-color: rgba(128, 128, 128, 60); }
QLabel#seWarn { color: #e0a030; padding: 2px 4px; }

QToolButton#seQuickButton {
	padding: 3px 8px;
	border: 1px solid rgba(128, 128, 128, 120);
	border-radius: 4px;
}
QToolButton#seQuickButton:hover { background-color: rgba(128, 128, 128, 60); }
QToolButton#seQuickButton:checked {
	background-color: palette(highlight);
	color: palette(highlighted-text);
	border-color: palette(highlight);
}
QToolButton#seQuickButton:disabled { color: rgba(128, 128, 128, 160); }

QLabel#seBadge {
	border-radius: 8px;
	padding: 1px 8px;
	font-size: 11px;
	font-weight: 600;
	color: white;
	background-color: #5b6270;
}
QLabel#seBadge[kind="program"] { background-color: #c0392b; }
QLabel#seBadge[kind="preview"] { background-color: #2e8b57; }
QLabel#seBadge[kind="muted"]   { background-color: #b9770e; }
QLabel#seBadge[kind="filters"] { background-color: #2f6fb5; }

QToolButton#seDrawerButton {
	padding: 3px 10px;
	border: 1px solid palette(mid);
	border-radius: 4px;
}
QToolButton#seDrawerButton:checked {
	background-color: palette(highlight);
	color: palette(highlighted-text);
	border-color: palette(highlight);
}

QListWidget#seRail { border: none; background: transparent; outline: 0; }
QListWidget#seRail::item { padding: 7px 10px; border-radius: 4px; }
QListWidget#seRail::item:hover:!selected { background-color: palette(alternate-base); }
QListWidget#seRail::item:selected {
	background-color: palette(highlight);
	color: palette(highlighted-text);
}

QGroupBox {
	border: 1px solid palette(mid);
	border-radius: 4px;
	margin-top: 10px;
	padding: 10px 8px 6px 8px;
}
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 10px; padding: 0 4px; }

QPushButton:checked {
	background-color: palette(highlight);
	color: palette(highlighted-text);
}

QLabel#seHint { padding: 6px 2px; }
)");
}

} // namespace se
