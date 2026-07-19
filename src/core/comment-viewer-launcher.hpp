#pragma once

#include <QString>
#include <QUrl>

namespace dsk {

QUrl commentViewerBaseUrl();
QUrl commentViewerPageUrl();
QUrl commentViewerObsIntegrationUrl();
bool isCommentViewerInstalled();
bool startCommentViewerServer();
bool openCommentViewerApp();

} // namespace dsk
