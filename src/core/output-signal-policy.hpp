#pragma once

#include <QString>

namespace dsk {

inline bool shouldIgnoreOutputSignalDuringPendingRelease(const QString &signalName)
{
	return signalName != QStringLiteral("stopping") && signalName != QStringLiteral("deactivate") &&
	       signalName != QStringLiteral("stop");
}

} // namespace dsk
