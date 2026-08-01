#pragma once

#include <QByteArray>
#include <QDir>
#include <QLocale>
#include <QSettings>
#include <QString>

#ifndef DSK_DIAGNOSTICS_QT_FALLBACK
#include <obs-module.h>
#endif

namespace dsk {

inline QString localizedText(const char *key, const char *englishFallback)
{
	const QString fallback = QString::fromUtf8(englishFallback);

#ifdef DSK_DIAGNOSTICS_QT_FALLBACK
	QString localeName = qEnvironmentVariable("DSK_UI_TEST_LOCALE").trimmed();
	if (localeName.isEmpty())
		localeName = QLocale::system().name();
	localeName.replace(QLatin1Char('_'), QLatin1Char('-'));
	const QString localeFile = localeName.startsWith(QStringLiteral("ja"), Qt::CaseInsensitive)
					   ? QStringLiteral("ja-JP.ini")
					   : QStringLiteral("en-US.ini");
	QSettings settings(QDir::current().filePath(QStringLiteral("data/locale/%1").arg(localeFile)),
			   QSettings::IniFormat);
	const QString value = settings.value(QString::fromUtf8(key)).toString().trimmed();
	return value.isEmpty() ? fallback : value;
#else
	const char *value = obs_module_text(key);
	if (!value || !*value || QByteArray(value) == QByteArray(key))
		return fallback;
	return QString::fromUtf8(value);
#endif
}

} // namespace dsk
