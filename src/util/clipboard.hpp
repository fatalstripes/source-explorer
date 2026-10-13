// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>

namespace se::util {
// UI-thread only.
void copyToClipboard(const std::string &utf8Text);
} // namespace se::util
