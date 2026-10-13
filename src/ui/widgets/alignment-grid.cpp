#include "ui/widgets/alignment-grid.hpp"

#include "core/payload-generator.hpp"

#include <QButtonGroup>
#include <QGridLayout>
#include <QToolButton>

namespace se {

namespace {
constexpr int kCell = 20; // px per cell
constexpr int kGap = 3;

int cellToAlignment(int row, int col)
{
	int a = 0;
	if (col == 0) a |= align::Left;
	if (col == 2) a |= align::Right;
	if (row == 0) a |= align::Top;
	if (row == 2) a |= align::Bottom;
	return a;
}
} // namespace

AlignmentGrid::AlignmentGrid(QWidget *parent) : QWidget(parent), group_(new QButtonGroup(this))
{
	group_->setExclusive(true);
	auto *grid = new QGridLayout(this);
	grid->setContentsMargins(0, 0, 0, 0);
	grid->setSpacing(kGap);

	// A compact, fixed 3x3 square. Left to stretch, the layout spreads the cells across the whole
	// panel width and they stop reading as a grid. Colours follow the OBS theme's palette.
	const int side = 3 * kCell + 2 * kGap;
	setFixedSize(side, side);
	// Style-sheet sizes override widget sizes, and OBS themes style QToolButton, so the cell size
	// is pinned here too (otherwise the cells render as thin slivers). The unselected border is a
	// mid grey with transparency: visible on dark and light themes alike.
	setStyleSheet(QStringLiteral(R"(
QToolButton {
	min-width: %1px; max-width: %1px; min-height: %1px; max-height: %1px; /* content size: the 1px border adds the other 2 */
	padding: 0; margin: 0;
	border: 1px solid rgba(128, 128, 128, 170);
	border-radius: 3px;
	background: transparent;
}
QToolButton:hover { background-color: rgba(128, 128, 128, 60); }
QToolButton:checked { background-color: palette(highlight); border-color: palette(highlight); }
QToolButton:disabled { border-color: rgba(128, 128, 128, 80); }
QToolButton:checked:disabled { background-color: rgba(128, 128, 128, 110); }
)").arg(kCell - 2)); // border (1px each side) brings it to kCell

	for (int row = 0; row < 3; ++row) {
		for (int col = 0; col < 3; ++col) {
			auto *b = new QToolButton(this);
			b->setCheckable(true);
			b->setFixedSize(kCell, kCell);
			b->setAutoRaise(false);
			const int a = cellToAlignment(row, col);
			group_->addButton(b, a); // button id == alignment value
			grid->addWidget(b, row, col);
		}
	}
	group_->button(align::TopLeft)->setChecked(true);
	connect(group_, &QButtonGroup::idClicked, this, &AlignmentGrid::alignmentChanged);
}

int AlignmentGrid::alignment() const
{
	return group_->checkedId();
}

void AlignmentGrid::setAlignment(int alignment)
{
	// Normalize contradictory flags (Left|Right, Top|Bottom) to centered on that axis.
	if ((alignment & align::Left) && (alignment & align::Right))
		alignment &= ~(align::Left | align::Right);
	if ((alignment & align::Top) && (alignment & align::Bottom))
		alignment &= ~(align::Top | align::Bottom);
	if (auto *b = group_->button(alignment))
		b->setChecked(true);
}

} // namespace se
