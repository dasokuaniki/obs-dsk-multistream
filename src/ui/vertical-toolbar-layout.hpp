#pragma once

namespace dsk {

inline constexpr int VerticalToolbarCompactWidth = 280;
inline constexpr int VerticalToolbarControlMinimumWidth = 28;
inline constexpr int VerticalToolbarMaximumSpacing = 6;

constexpr bool verticalToolbarUsesCompactMode(int width)
{
	return width < VerticalToolbarCompactWidth;
}

} // namespace dsk
