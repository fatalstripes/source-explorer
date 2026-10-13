#pragma once
#include <string>

namespace se::util {
// UI-thread only.
void copyToClipboard(const std::string &utf8Text);
} // namespace se::util
