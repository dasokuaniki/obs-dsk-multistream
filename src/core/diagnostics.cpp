#include "core/diagnostics.hpp"

#include <obs-module.h>

namespace dsk {

void logInfo(const QString &message)
{
	blog(LOG_INFO, "[DSK Multistream] %s", qUtf8Printable(message));
}

void logWarning(const QString &message)
{
	blog(LOG_WARNING, "[DSK Multistream] %s", qUtf8Printable(message));
}

void logError(const QString &message)
{
	blog(LOG_ERROR, "[DSK Multistream] %s", qUtf8Printable(message));
}

} // namespace dsk
