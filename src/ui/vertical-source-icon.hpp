#pragma once

#include <QString>

namespace dsk {

inline QString obsSourceIconPropertyName(const QString &sourceId, int iconType)
{
	if (sourceId == QStringLiteral("scene"))
		return QStringLiteral("sceneIcon");
	if (sourceId == QStringLiteral("group"))
		return QStringLiteral("groupIcon");

	switch (iconType) {
	case 1:
		return QStringLiteral("imageIcon");
	case 2:
		return QStringLiteral("colorIcon");
	case 3:
		return QStringLiteral("slideshowIcon");
	case 4:
		return QStringLiteral("audioInputIcon");
	case 5:
		return QStringLiteral("audioOutputIcon");
	case 6:
		return QStringLiteral("desktopCapIcon");
	case 7:
		return QStringLiteral("windowCapIcon");
	case 8:
		return QStringLiteral("gameCapIcon");
	case 9:
		return QStringLiteral("cameraIcon");
	case 10:
		return QStringLiteral("textIcon");
	case 11:
		return QStringLiteral("mediaIcon");
	case 12:
		return QStringLiteral("browserIcon");
	case 14:
		return QStringLiteral("audioProcessOutputIcon");
	default:
		return QStringLiteral("defaultIcon");
	}
}

} // namespace dsk
