#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace dsk {

struct YouTubeStreamOption {
	QString id;
	QString title;
	QString serverUrl;
	QString streamKey;
	QString status;
};

struct YouTubeStreamPage {
	QVector<YouTubeStreamOption> streams;
	QString nextPageToken;
};

YouTubeStreamPage parseYouTubeStreamPage(const QJsonObject &response);
QString youtubeStreamOptionLabel(const YouTubeStreamOption &stream);

} // namespace dsk
