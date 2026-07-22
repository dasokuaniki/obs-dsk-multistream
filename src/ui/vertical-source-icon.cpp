#include "ui/vertical-source-icon-loader.hpp"

#include "ui/vertical-source-icon.hpp"

#include <obs-frontend-api.h>
#include <obs.h>

#include <QApplication>
#include <QByteArray>
#include <QStyle>
#include <QVariant>
#include <QWidget>

namespace dsk {

QIcon obsSourceTypeIcon(obs_source_t *source)
{
	QStyle *applicationStyle = QApplication::style();
	const QIcon fallback = applicationStyle ? applicationStyle->standardIcon(QStyle::SP_FileIcon) : QIcon();
	if (!source)
		return fallback;

	const char *sourceIdRaw = obs_source_get_id(source);
	const QString sourceId = sourceIdRaw ? QString::fromUtf8(sourceIdRaw) : QString();
	const int iconType = sourceIdRaw ? int(obs_source_get_icon_type(sourceIdRaw)) : int(OBS_ICON_TYPE_UNKNOWN);
	const QString propertyName = obsSourceIconPropertyName(sourceId, iconType);
	auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
	if (mainWindow) {
		const QByteArray propertyUtf8 = propertyName.toUtf8();
		const QVariant value = mainWindow->property(propertyUtf8.constData());
		if (value.canConvert<QIcon>()) {
			const QIcon icon = value.value<QIcon>();
			if (!icon.isNull())
				return icon;
		}
	}

	if (!applicationStyle)
		return fallback;
	switch (iconType) {
	case OBS_ICON_TYPE_AUDIO_INPUT:
	case OBS_ICON_TYPE_AUDIO_OUTPUT:
	case OBS_ICON_TYPE_PROCESS_AUDIO_OUTPUT:
		return applicationStyle->standardIcon(QStyle::SP_MediaVolume);
	case OBS_ICON_TYPE_DESKTOP_CAPTURE:
	case OBS_ICON_TYPE_WINDOW_CAPTURE:
	case OBS_ICON_TYPE_GAME_CAPTURE:
	case OBS_ICON_TYPE_CAMERA:
		return applicationStyle->standardIcon(QStyle::SP_ComputerIcon);
	case OBS_ICON_TYPE_MEDIA:
		return applicationStyle->standardIcon(QStyle::SP_MediaPlay);
	case OBS_ICON_TYPE_BROWSER:
		return applicationStyle->standardIcon(QStyle::SP_DriveNetIcon);
	default:
		return fallback;
	}
}

QIcon obsSourceTypeIcon(const QString &sourceName)
{
	if (sourceName.isEmpty())
		return obsSourceTypeIcon(static_cast<obs_source_t *>(nullptr));
	obs_source_t *source = obs_get_source_by_name(sourceName.toUtf8().constData());
	const QIcon icon = obsSourceTypeIcon(source);
	if (source)
		obs_source_release(source);
	return icon;
}

static_assert(int(OBS_ICON_TYPE_IMAGE) == 1 && int(OBS_ICON_TYPE_BROWSER) == 12 &&
		      int(OBS_ICON_TYPE_PROCESS_AUDIO_OUTPUT) == 14,
	      "Update obsSourceIconPropertyName when libobs changes obs_icon_type values.");

} // namespace dsk
