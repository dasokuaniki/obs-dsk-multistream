#pragma once

#include "core/layout-manager.hpp"

#include <QPointF>
#include <QSizeF>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace dsk {

enum class VerticalLayoutChange {
	None,
	TransformOnly,
	Rebuild,
};

inline bool verticalLayoutItemTransformMatches(const VerticalLayoutItem &current, const VerticalLayoutItem &next)
{
	return current.rect == next.rect && current.crop == next.crop && current.fitMode == next.fitMode;
}

inline VerticalLayoutChange verticalLayoutChange(const VerticalLayout &current, const VerticalLayout &next)
{
	if (current.width != next.width || current.height != next.height || current.items.size() != next.items.size())
		return VerticalLayoutChange::Rebuild;

	bool transformChanged = false;
	for (int index = 0; index < current.items.size(); ++index) {
		const auto &before = current.items[index];
		const auto &after = next.items[index];
		if (before.id.isEmpty() || after.id.isEmpty() || before.id != after.id ||
		    before.sourceName != after.sourceName || before.visible != after.visible)
			return VerticalLayoutChange::Rebuild;
		for (int previous = 0; previous < index; ++previous) {
			if (next.items[previous].id == after.id)
				return VerticalLayoutChange::Rebuild;
		}
		if (!verticalLayoutItemTransformMatches(before, after))
			transformChanged = true;
	}
	return transformChanged ? VerticalLayoutChange::TransformOnly : VerticalLayoutChange::None;
}

enum VerticalResizeEdge : int {
	ResizeLeft = 1 << 0,
	ResizeRight = 1 << 1,
	ResizeTop = 1 << 2,
	ResizeBottom = 1 << 3,
};

inline void normalizeVerticalLayoutGeometry(VerticalLayout &layout)
{
	constexpr int DefaultWidth = 1080;
	constexpr int DefaultHeight = 1920;
	constexpr int MinimumCanvasDimension = 64;
	constexpr int MaximumCanvasDimension = 16384;
	constexpr int MaximumLayoutItems = 512;

	if (layout.width < MinimumCanvasDimension || layout.width > MaximumCanvasDimension)
		layout.width = DefaultWidth;
	if (layout.height < MinimumCanvasDimension || layout.height > MaximumCanvasDimension)
		layout.height = DefaultHeight;
	if (layout.items.size() > MaximumLayoutItems)
		layout.items.resize(MaximumLayoutItems);

	const double maximumExtent = 16.0 * std::max(layout.width, layout.height);
	for (auto &item : layout.items) {
		double x = item.rect.x();
		double y = item.rect.y();
		double width = item.rect.width();
		double height = item.rect.height();
		if (!std::isfinite(x))
			x = 0.0;
		if (!std::isfinite(y))
			y = 0.0;
		if (!std::isfinite(width) || width <= 0.0)
			width = layout.width;
		if (!std::isfinite(height) || height <= 0.0)
			height = layout.height;
		item.rect = QRectF(std::clamp(x, -maximumExtent, maximumExtent),
				   std::clamp(y, -maximumExtent, maximumExtent),
				   std::clamp(width, 1.0, maximumExtent),
				   std::clamp(height, 1.0, maximumExtent));

		double cropLeft = item.crop.x();
		double cropTop = item.crop.y();
		double cropRight = item.crop.width();
		double cropBottom = item.crop.height();
		if (!std::isfinite(cropLeft))
			cropLeft = 0.0;
		if (!std::isfinite(cropTop))
			cropTop = 0.0;
		if (!std::isfinite(cropRight))
			cropRight = 0.0;
		if (!std::isfinite(cropBottom))
			cropBottom = 0.0;
		item.crop = QRectF(std::clamp(cropLeft, 0.0, maximumExtent),
				   std::clamp(cropTop, 0.0, maximumExtent),
				   std::clamp(cropRight, 0.0, maximumExtent),
				   std::clamp(cropBottom, 0.0, maximumExtent));
	}
}

inline QSizeF croppedSourceSize(const VerticalLayoutItem &item, const QSizeF &sourceSize)
{
	if (sourceSize.width() <= 0.0 || sourceSize.height() <= 0.0)
		return {};

	const double width = sourceSize.width() - item.crop.x() - item.crop.width();
	const double height = sourceSize.height() - item.crop.y() - item.crop.height();
	if (width <= 0.0 || height <= 0.0)
		return {};
	return QSizeF(width, height);
}

inline QRectF centeredAspectFitRect(const QSizeF &contentSize, const QSizeF &canvasSize)
{
	if (canvasSize.width() <= 0.0 || canvasSize.height() <= 0.0)
		return {};

	const QRectF canvasRect(QPointF(0.0, 0.0), canvasSize);
	if (contentSize.width() <= 0.0 || contentSize.height() <= 0.0)
		return canvasRect;

	const double scale = std::min(canvasSize.width() / contentSize.width(),
				      canvasSize.height() / contentSize.height());
	const QSizeF fittedSize(contentSize.width() * scale, contentSize.height() * scale);
	return QRectF((canvasSize.width() - fittedSize.width()) / 2.0,
		      (canvasSize.height() - fittedSize.height()) / 2.0,
		      fittedSize.width(),
		      fittedSize.height());
}

inline QRectF displayedContentRect(const VerticalLayoutItem &item, const QSizeF &sourceSize)
{
	const QSizeF cropped = croppedSourceSize(item, sourceSize);
	if (cropped.width() <= 0.0 || cropped.height() <= 0.0)
		return item.rect;

	if (item.fitMode == FitMode::Stretch || item.fitMode == FitMode::Fill)
		return item.rect;

	const double scale = std::min(item.rect.width() / cropped.width(), item.rect.height() / cropped.height());
	const QSizeF content(cropped.width() * scale, cropped.height() * scale);
	return QRectF(item.rect.x() + (item.rect.width() - content.width()) / 2.0,
		      item.rect.y() + (item.rect.height() - content.height()) / 2.0,
		      content.width(),
		      content.height());
}

inline int verticalPreviewHitItem(const VerticalLayout &layout,
				  const QVector<QRectF> &displayRects,
				  const QPointF &layoutPoint,
				  int selectedIndex = -1)
{
	const auto contains = [&](int index) {
		if (index < 0 || index >= layout.items.size() || !layout.items[index].visible)
			return false;
		const QRectF rect = index < displayRects.size() ? displayRects[index] : layout.items[index].rect;
		return rect.contains(layoutPoint);
	};

	// Once a visible source is selected, clicking outside its displayed bounds
	// means "clear selection". Do not immediately select a full-canvas source
	// behind it, otherwise the white edit outline can never be dismissed from
	// layouts that have a background layer.
	if (selectedIndex >= 0 && selectedIndex < layout.items.size() && layout.items[selectedIndex].visible)
		return contains(selectedIndex) ? selectedIndex : -1;

	// DSK stores front-most sources first.
	for (int index = 0; index < layout.items.size(); ++index) {
		if (index != selectedIndex && contains(index))
			return index;
	}
	return -1;
}

inline int verticalPreviewHitItemAtWidgetPoint(const VerticalLayout &layout,
					       const QVector<QRectF> &displayRects,
					       const QRectF &canvasRect,
					       const QPointF &widgetPoint,
					       int selectedIndex = -1)
{
	if (layout.width <= 0 || layout.height <= 0 || !canvasRect.isValid() || !canvasRect.contains(widgetPoint))
		return -1;

	const QPointF layoutPoint((widgetPoint.x() - canvasRect.x()) * double(layout.width) / canvasRect.width(),
				  (widgetPoint.y() - canvasRect.y()) * double(layout.height) / canvasRect.height());
	return verticalPreviewHitItem(layout, displayRects, layoutPoint, selectedIndex);
}

inline QRectF aspectConstrainedResize(const QRectF &candidate, const QRectF &reference, int edges, double minimumSize = 32.0)
{
	if (reference.width() <= 0.0 || reference.height() <= 0.0)
		return candidate.normalized();

	const bool horizontal = edges & (ResizeLeft | ResizeRight);
	const bool vertical = edges & (ResizeTop | ResizeBottom);
	if (!horizontal && !vertical)
		return candidate.normalized();

	const QRectF clean = candidate.normalized();
	const double aspect = reference.width() / reference.height();
	double width = std::max(minimumSize, clean.width());
	double height = std::max(minimumSize, clean.height());

	if (horizontal && vertical) {
		if (width / height > aspect)
			height = width / aspect;
		else
			width = height * aspect;
	} else if (horizontal) {
		height = width / aspect;
	} else {
		width = height * aspect;
	}

	double left = clean.left();
	double top = clean.top();
	if ((edges & ResizeLeft) && !(edges & ResizeRight))
		left = clean.right() - width;
	else if (!horizontal)
		left = reference.center().x() - width / 2.0;

	if ((edges & ResizeTop) && !(edges & ResizeBottom))
		top = clean.bottom() - height;
	else if (!vertical)
		top = reference.center().y() - height / 2.0;

	return QRectF(left, top, width, height);
}

} // namespace dsk
