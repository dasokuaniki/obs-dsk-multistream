#pragma once

#include <QString>

#ifdef DSK_DIAGNOSTICS_QT_FALLBACK
#include <QDebug>
#endif

namespace dsk {

#ifdef DSK_DIAGNOSTICS_QT_FALLBACK
inline void logInfo(const QString &message)
{
	qInfo().noquote() << "[DSK Multistream]" << message;
}

inline void logWarning(const QString &message)
{
	qWarning().noquote() << "[DSK Multistream]" << message;
}

inline void logError(const QString &message)
{
	qCritical().noquote() << "[DSK Multistream]" << message;
}
#else
void logInfo(const QString &message);
void logWarning(const QString &message);
void logError(const QString &message);
#endif

} // namespace dsk
