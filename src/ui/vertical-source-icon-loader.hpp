#pragma once

#include <QIcon>
#include <QString>

struct obs_source;
using obs_source_t = struct obs_source;

namespace dsk {

QIcon obsSourceTypeIcon(obs_source_t *source);
QIcon obsSourceTypeIcon(const QString &sourceName);

} // namespace dsk
