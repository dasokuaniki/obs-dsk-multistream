#include "core/youtube-stream-options.hpp"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>
#include <QUrl>

namespace dsk {

namespace {

QString boundedText(const QJsonValue &value, int maximumLength)
{
	QString text = value.toString().trimmed();
	text.replace(QLatin1Char('\r'), QLatin1Char(' '));
	text.replace(QLatin1Char('\n'), QLatin1Char(' '));
	text.replace(QLatin1Char('\t'), QLatin1Char(' '));
	while (text.contains(QStringLiteral("  ")))
		text.replace(QStringLiteral("  "), QStringLiteral(" "));
	return text.left(maximumLength);
}

QString validRtmpUrl(const QJsonValue &value)
{
	const QString candidate = boundedText(value, 2048);
	const QUrl url(candidate, QUrl::StrictMode);
	const QString scheme = url.scheme().toLower();
	const QString host = url.host().toLower();
	if (!url.isValid() || !host.endsWith(QStringLiteral(".youtube.com")) || !url.userName().isEmpty() ||
	    !url.password().isEmpty() || (scheme != QStringLiteral("rtmp") && scheme != QStringLiteral("rtmps")))
		return {};
	return candidate;
}

} // namespace

YouTubeStreamPage parseYouTubeStreamPage(const QJsonObject &response)
{
	YouTubeStreamPage page;
	page.nextPageToken = boundedText(response.value(QStringLiteral("nextPageToken")), 2048);

	QSet<QString> seenKeys;
	const QJsonArray items = response.value(QStringLiteral("items")).toArray();
	for (const QJsonValue &value : items) {
		if (!value.isObject())
			continue;
		const QJsonObject item = value.toObject();
		const QJsonObject cdn = item.value(QStringLiteral("cdn")).toObject();
		if (boundedText(cdn.value(QStringLiteral("ingestionType")), 16).toLower() != QStringLiteral("rtmp"))
			continue;
		const QJsonObject contentDetails = item.value(QStringLiteral("contentDetails")).toObject();
		if (contentDetails.contains(QStringLiteral("isReusable")) &&
		    !contentDetails.value(QStringLiteral("isReusable")).toBool())
			continue;

		const QJsonObject ingestion = cdn.value(QStringLiteral("ingestionInfo")).toObject();
		const QString streamKey = boundedText(ingestion.value(QStringLiteral("streamName")), 512);
		if (streamKey.isEmpty() || seenKeys.contains(streamKey))
			continue;
		QString serverUrl = validRtmpUrl(ingestion.value(QStringLiteral("rtmpsIngestionAddress")));
		if (serverUrl.isEmpty())
			serverUrl = validRtmpUrl(ingestion.value(QStringLiteral("ingestionAddress")));
		if (serverUrl.isEmpty())
			continue;

		YouTubeStreamOption stream;
		stream.id = boundedText(item.value(QStringLiteral("id")), 256);
		stream.title = boundedText(item.value(QStringLiteral("snippet")).toObject().value(QStringLiteral("title")), 128);
		if (stream.title.isEmpty())
			stream.title = QStringLiteral("YouTube stream");
		stream.serverUrl = serverUrl;
		stream.streamKey = streamKey;
		stream.status = boundedText(item.value(QStringLiteral("status")).toObject()
						.value(QStringLiteral("streamStatus")), 32);
		seenKeys.insert(streamKey);
		page.streams.push_back(std::move(stream));
	}
	return page;
}

QString youtubeStreamOptionLabel(const YouTubeStreamOption &stream)
{
	QString title = stream.title.trimmed();
	QString status = stream.status.trimmed();
	if (title.isEmpty() || (!stream.streamKey.isEmpty() && title.contains(stream.streamKey)))
		title = QStringLiteral("YouTube stream");
	if (!stream.streamKey.isEmpty() && status.contains(stream.streamKey))
		status.clear();
	return status.isEmpty() ? title : QStringLiteral("%1 (%2)").arg(title, status);
}

} // namespace dsk
