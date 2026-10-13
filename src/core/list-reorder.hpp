#pragma once
// Index math for drag-and-drop reordering in a Qt item view.
#include <algorithm>

namespace se {

// Qt reports a move as "take row `from`, insert it before row `destRow`" where
// destRow is expressed in the list *before* the move (0..count; count == append).
// Returns the index the item ends up at, clamped to [0, count-1]; -1 if the list is empty.
inline int dropTargetIndex(int count, int from, int destRow)
{
	if (count <= 0)
		return -1;
	const int to = destRow > from ? destRow - 1 : destRow;
	return std::clamp(to, 0, count - 1);
}

} // namespace se
