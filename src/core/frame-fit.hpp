#pragma once
// Qt-free geometry for keeping a floating window usable and grabbable.
#include <algorithm>

namespace se {

struct Rect {
	int x = 0, y = 0, w = 0, h = 0;
	int right() const { return x + w; }
	int bottom() const { return y + h; }
	bool operator==(const Rect &) const = default;
};

struct Size {
	int w = 0, h = 0;
};

// `frame` is the window's outer rectangle INCLUDING its title bar; `available` is the usable
// screen area (taskbar excluded). Returns a frame that:
//   - has a sensible size: a frame that is tiny, or covers nearly the whole screen (over 85%),
//     is replaced by `preferred` (never larger than 90% of the screen) and centred. Anything
//     in between is the user's choice and is kept;
//   - lies entirely inside `available`, so the title bar can always be reached.
// A frame that is already usable is returned unchanged.
inline Rect fitFloatingFrame(Rect frame, const Rect &available, Size preferred)
{
	const int maxW = std::max(1, available.w * 9 / 10);
	const int maxH = std::max(1, available.h * 9 / 10);

	const bool unusableSize = frame.w < 200 || frame.h < 150 || frame.w > available.w * 85 / 100 ||
				  frame.h > available.h * 85 / 100;
	if (unusableSize) {
		frame.w = std::min(preferred.w, maxW);
		frame.h = std::min(preferred.h, maxH);
		frame.x = available.x + (available.w - frame.w) / 2;
		frame.y = available.y + (available.h - frame.h) / 2;
		return frame;
	}

	frame.w = std::min(frame.w, maxW);
	frame.h = std::min(frame.h, maxH);
	frame.x = std::clamp(frame.x, available.x, available.right() - frame.w);
	frame.y = std::clamp(frame.y, available.y, available.bottom() - frame.h);
	return frame;
}

} // namespace se
