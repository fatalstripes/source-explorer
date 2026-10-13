// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// libobs stores colors as a 32-bit integer in 0xAABBGGRR order (R is the lowest
// byte), held in an obs_data int (64-bit). Qt-free so it can be unit tested.
#include <cstdint>

namespace se {

struct Rgba {
	int r = 0, g = 0, b = 0, a = 255;
	bool operator==(const Rgba &) const = default;
};

inline std::int64_t toObsColor(Rgba c)
{
	auto ch = [](int v) { return static_cast<std::uint32_t>(v < 0 ? 0 : (v > 255 ? 255 : v)); };
	const std::uint32_t v = (ch(c.a) << 24) | (ch(c.b) << 16) | (ch(c.g) << 8) | ch(c.r);
	return static_cast<std::int64_t>(v); // always non-negative: no sign extension surprises
}

// Accepts values read back as signed 32-bit (negative) or unsigned 64-bit.
inline Rgba fromObsColor(std::int64_t value)
{
	const auto v = static_cast<std::uint32_t>(value & 0xFFFFFFFFLL);
	return {static_cast<int>(v & 0xFF), static_cast<int>((v >> 8) & 0xFF), static_cast<int>((v >> 16) & 0xFF),
		static_cast<int>((v >> 24) & 0xFF)};
}

} // namespace se
