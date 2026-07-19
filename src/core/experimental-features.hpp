#pragma once

#include <QByteArray>
#include <QtGlobal>

namespace dsk {

inline bool experimentalFeatureEnabled(const QByteArray &rawValue)
{
	const QByteArray value = rawValue.trimmed().toLower();
	return value == QByteArrayLiteral("1") || value == QByteArrayLiteral("true") ||
	       value == QByteArrayLiteral("on");
}

inline bool experimentalSceneRoutingEnabled()
{
	return experimentalFeatureEnabled(qgetenv("DSK_EXPERIMENTAL_SCENE_ROUTING"));
}

} // namespace dsk

