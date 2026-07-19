#pragma once

namespace dsk {

// Keep the dock compact enough to give the OBS main preview priority. These
// dimensions are half of the original 220x390 minimum while preserving 9:16.
inline constexpr int VerticalPreviewMinimumWidth = 110;
inline constexpr int VerticalPreviewMinimumHeight = 195;

} // namespace dsk
