#pragma once

#include <QString>
#include <QVector>

#include <utility>

namespace dsk {

template<typename T>
bool reorderValuesByStableIds(const QVector<T> &values, const QVector<QString> &orderedIds, QVector<T> *reordered)
{
	if (!reordered || values.size() != orderedIds.size())
		return false;

	QVector<T> result;
	QVector<bool> used(values.size(), false);
	result.reserve(values.size());
	for (const QString &id : orderedIds) {
		int match = -1;
		for (int index = 0; index < values.size(); ++index) {
			if (!used[index] && values[index].id == id) {
				match = index;
				break;
			}
		}
		if (match < 0)
			return false;
		used[match] = true;
		result.push_back(values[match]);
	}

	*reordered = std::move(result);
	return true;
}

} // namespace dsk
