#pragma once

#include <QLayout>
#include <QListWidget>
#include <QSignalBlocker>
#include <QWidget>

namespace dsk {

inline void clearLayoutWidgetsForRefresh(QLayout *layout, int preserveTrailingItems = 0)
{
	if (!layout)
		return;

	const int preserved = preserveTrailingItems > 0 ? preserveTrailingItems : 0;
	while (layout->count() > preserved) {
		QLayoutItem *item = layout->takeAt(0);
		if (!item)
			break;
		if (QWidget *widget = item->widget()) {
			// A widget removed from a layout stays parented and paintable until
			// DeferredDelete runs. Hide it before scheduling deletion so stale
			// geometry cannot overlap replacement controls during refresh bursts.
			widget->hide();
			widget->deleteLater();
		}
		delete item;
	}
}

inline void clearListWidgetSelection(QListWidget *list)
{
	if (!list)
		return;

	const QSignalBlocker blocker(list);
	list->setCurrentRow(-1);
	list->clearSelection();
}

} // namespace dsk
