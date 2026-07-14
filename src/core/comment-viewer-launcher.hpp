#pragma once

#include <QString>
#include <QUrl>

namespace dsk {

QUrl commentViewerBaseUrl();
QUrl commentViewerPageUrl();
bool startCommentViewerServer();
bool openCommentViewerApp();

} // namespace dsk
