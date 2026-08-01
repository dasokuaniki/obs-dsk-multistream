#pragma once

#include "core/http-client.hpp"
#include "core/output-target.hpp"
#include "core/youtube-stream-options.hpp"

#include <QObject>
#include <QString>

class QTcpServer;
class QTcpSocket;
class QTimer;
class QUrl;

namespace dsk {

struct OAuthConnectionResult {
	TargetAuthMode authMode = TargetAuthMode::ManualRtmp;
	QString accountName;
	QString serverUrl;
	QString streamKey;
	QString refreshToken;
	QVector<YouTubeStreamOption> youtubeStreams;
	QString youtubeStreamLookupWarning;
	QString errorMessage;
};

class OAuthConnector : public QObject {
	Q_OBJECT

public:
	explicit OAuthConnector(QObject *parent = nullptr);
	~OAuthConnector() override;

	bool begin(TargetAuthMode authMode, const QString &clientId, const QString &clientSecret);
	bool isRunning() const;

signals:
	void finished(const dsk::OAuthConnectionResult &result);

private slots:
	void acceptCallbackConnection();

private:
	void fail(const QString &message);
	void handleCallbackSocket(QTcpSocket *socket);
	void processCallbackRequest(QTcpSocket *socket);
	void sendBrowserResponse(QTcpSocket *socket, const QString &body);
	void exchangeAuthorizationCode(const QString &code);
	void handleTokenReply(HttpResponse response);
	void fetchTwitchUser(const QString &accessToken);
	void handleTwitchUserReply(HttpResponse response, const QString &accessToken);
	void fetchTwitchStreamKey(const QString &accessToken, const QString &broadcasterId, const QString &accountName);
	void handleTwitchStreamKeyReply(HttpResponse response, const QString &accountName);
	void fetchKickChannel(const QString &accessToken, const QString &accountName);
	void handleKickChannelReply(HttpResponse response, const QString &accountName);
	void fetchYouTubeStreams(const QString &accessToken, const QString &pageToken = {});
	void handleYouTubeStreamsReply(HttpResponse response, const QString &accessToken);
	void completeYouTubeLogin(const QString &streamLookupWarning = {});
	void reset();
	QUrl redirectUri() const;

	TargetAuthMode authMode_ = TargetAuthMode::ManualRtmp;
	QString clientId_;
	QString clientSecret_;
	QString refreshToken_;
	QString state_;
	QString codeVerifier_;
	QVector<YouTubeStreamOption> youtubeStreams_;
	int youtubeStreamPageCount_ = 0;
	QTcpServer *server_ = nullptr;
	HttpClient *http_ = nullptr;
	QTimer *timeout_ = nullptr;
	bool running_ = false;
	bool callbackHandled_ = false;
};

} // namespace dsk

Q_DECLARE_METATYPE(dsk::OAuthConnectionResult)
