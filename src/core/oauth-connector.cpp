#include "core/oauth-connector.hpp"
#include "core/diagnostics.hpp"
#include "core/oauth-provider.hpp"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHostAddress>
#include <QPointer>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace dsk {

namespace {

constexpr quint16 OAuthCallbackPort = 17371;
constexpr int NetworkTimeoutMs = 20 * 1000;
constexpr int CallbackSocketTimeoutMs = 15 * 1000;
constexpr int MaxCallbackRequestBytes = 16 * 1024;
constexpr int MaxYouTubeStreamPages = 20;

QString base64Url(const QByteArray &data)
{
	QByteArray encoded = data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
	return QString::fromLatin1(encoded);
}

QString randomToken(int bytes)
{
	QByteArray data;
	data.resize(bytes);
	for (int i = 0; i < bytes; ++i)
		data[i] = char(QRandomGenerator::global()->bounded(256));
	return base64Url(data);
}

QByteArray formBody(const QUrlQuery &query)
{
	return query.toString(QUrl::FullyEncoded).toUtf8();
}

QJsonObject responseObject(const HttpResponse &response, QString *errorMessage)
{
	if (!response.transportError.isEmpty()) {
		if (errorMessage)
			*errorMessage = response.transportError;
		return {};
	}
	if (response.statusCode < 200 || response.statusCode >= 300) {
		if (errorMessage)
			*errorMessage = QStringLiteral("HTTP %1: %2")
						.arg(response.statusCode)
						.arg(oauthSafeErrorDetail(response.body));
		return {};
	}

	const QJsonDocument document = QJsonDocument::fromJson(response.body);
	if (!document.isObject()) {
		if (errorMessage)
			*errorMessage = QStringLiteral("OAuth response was not a JSON object.");
		return {};
	}
	return document.object();
}

} // namespace

OAuthConnector::OAuthConnector(QObject *parent)
	: QObject(parent),
	  http_(new HttpClient(this)),
	  timeout_(new QTimer(this))
{
	timeout_->setSingleShot(true);
	connect(timeout_, &QTimer::timeout, this, [this]() {
		if (running_)
			fail(QStringLiteral("OAuth login timed out. Start the login again."));
	});
}

OAuthConnector::~OAuthConnector()
{
	reset();
}

bool OAuthConnector::begin(TargetAuthMode authMode, const QString &clientId, const QString &clientSecret)
{
	if (running_)
		return false;
	authMode_ = authMode;
	if (authMode != TargetAuthMode::TwitchOAuth && authMode != TargetAuthMode::YouTubeOAuth &&
	    authMode != TargetAuthMode::KickOAuth) {
		fail(QStringLiteral("Unsupported OAuth login mode."));
		return false;
	}
	const bool publisherRelay = oauthUsesPublisherRelay(authMode);
	if (!publisherRelay && clientId.trimmed().isEmpty()) {
		fail(QStringLiteral("OAuth Client ID is empty. Register a Twitch/Google OAuth app and paste its Client ID."));
		return false;
	}
	if (authMode == TargetAuthMode::YouTubeOAuth && clientSecret.trimmed().isEmpty()) {
		fail(QStringLiteral("Google OAuth Client Secret is empty. Paste the Client Secret from the same OAuth client before login."));
		return false;
	}

	clientId_ = publisherRelay ? QString() : clientId.trimmed();
	clientSecret_ = publisherRelay ? QString() : clientSecret;
	refreshToken_.clear();
	callbackHandled_ = false;
	state_ = randomToken(24);
	codeVerifier_ = randomToken(48);
	logInfo(QString("OAuth begin: mode=%1 redirect=http://localhost:%2/callback")
			.arg(targetAuthModeToString(authMode_))
			.arg(OAuthCallbackPort));

	server_ = new QTcpServer(this);
	connect(server_, &QTcpServer::newConnection, this, &OAuthConnector::acceptCallbackConnection);
	if (!server_->listen(QHostAddress::LocalHost, OAuthCallbackPort)) {
		const QString message = QString("Could not listen on %1: %2").arg(redirectUri().toString(), server_->errorString());
		reset();
		fail(message);
		return false;
	}

	running_ = true;
	timeout_->start(oauthInteractiveTimeoutMs());
	const QByteArray challengeBytes = QCryptographicHash::hash(codeVerifier_.toUtf8(), QCryptographicHash::Sha256);
	const OAuthProvider provider = oauthProviderForAuthMode(authMode_);
	const QString challenge = base64Url(challengeBytes);
	const QUrl loginUrl = publisherRelay
				      ? oauthPublisherRelayAuthorizeUrl(provider, redirectUri(), state_, challenge)
				      : oauthAuthorizeUrl(provider, clientId_, redirectUri(), state_, challenge);
	if (!QDesktopServices::openUrl(loginUrl)) {
		reset();
		fail(QStringLiteral("Could not open the system browser for OAuth login."));
		return false;
	}

	return true;
}

bool OAuthConnector::isRunning() const
{
	return running_;
}

void OAuthConnector::acceptCallbackConnection()
{
	logInfo(QString("OAuth callback connection received: mode=%1").arg(targetAuthModeToString(authMode_)));
	while (server_ && server_->hasPendingConnections()) {
		QTcpSocket *socket = server_->nextPendingConnection();
		if (socket)
			handleCallbackSocket(socket);
	}
}

void OAuthConnector::handleCallbackSocket(QTcpSocket *socket)
{
	socket->setParent(this);
	connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { processCallbackRequest(socket); });
	connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);

	QPointer<QTcpSocket> socketGuard(socket);
	QTimer::singleShot(0, this, [this, socketGuard]() {
		if (socketGuard && socketGuard->bytesAvailable() > 0)
			processCallbackRequest(socketGuard);
	});
	QTimer::singleShot(CallbackSocketTimeoutMs, socket, [socket]() {
		if (socket->state() != QAbstractSocket::UnconnectedState)
			socket->disconnectFromHost();
	});
}

void OAuthConnector::processCallbackRequest(QTcpSocket *socket)
{
	if (!socket || socket->property("dskProcessed").toBool())
		return;
	if (socket->bytesAvailable() <= 0)
		return;

	QByteArray request = socket->property("dskRequestBuffer").toByteArray();
	request += socket->readAll();
	if (request.size() > MaxCallbackRequestBytes) {
		socket->setProperty("dskProcessed", true);
		sendBrowserResponse(socket, QStringLiteral("DSK rejected an oversized OAuth callback."));
		if (!callbackHandled_)
			fail(QStringLiteral("OAuth callback request was too large."));
		return;
	}
	socket->setProperty("dskRequestBuffer", request);
	if (!request.contains("\r\n\r\n") && !request.contains("\n\n"))
		return;

	socket->setProperty("dskProcessed", true);
	const QList<QByteArray> lines = request.split('\n');
	const QList<QByteArray> parts = lines.value(0).trimmed().split(' ');
	if (parts.size() < 2) {
		sendBrowserResponse(socket, QStringLiteral("DSK did not receive a valid OAuth callback."));
		if (!callbackHandled_)
			fail(QStringLiteral("Invalid OAuth callback request."));
		return;
	}

	const QString path = QString::fromUtf8(parts[1]);
	const QUrl callbackUrl(QStringLiteral("http://localhost") + path);
	if (callbackUrl.path() != QStringLiteral("/callback")) {
		sendBrowserResponse(socket, QStringLiteral("DSK login callback is waiting. You can close this tab."));
		return;
	}

	const QUrlQuery query(callbackUrl);
	const QString callbackState = query.queryItemValue("state");
	const QString code = query.queryItemValue("code");
	const QString error = query.queryItemValue("error");
	const QString errorDescription = query.queryItemValue("error_description");

	if (!error.isEmpty()) {
		sendBrowserResponse(socket, QStringLiteral("Login was cancelled or rejected. You can close this tab."));
		if (!callbackHandled_)
			fail(errorDescription.isEmpty() ? QString("OAuth rejected the login: %1").arg(error)
						       : QString("OAuth rejected the login: %1 (%2)").arg(error, errorDescription));
		return;
	}
	if (callbackState != state_ || code.isEmpty()) {
		logError(QString("OAuth callback rejected: stateMatch=%1 codePresent=%2")
				 .arg(callbackState == state_ ? QStringLiteral("true") : QStringLiteral("false"),
				      code.isEmpty() ? QStringLiteral("false") : QStringLiteral("true")));
		sendBrowserResponse(socket, QStringLiteral("DSK rejected this OAuth callback. You can close this tab."));
		if (!callbackHandled_)
			fail(QStringLiteral("OAuth callback state did not match."));
		return;
	}

	callbackHandled_ = true;
	logInfo(QString("OAuth callback accepted: mode=%1").arg(targetAuthModeToString(authMode_)));
	sendBrowserResponse(socket, QStringLiteral("DSK received the login. You can close this tab and return to OBS."));
	if (server_)
		server_->close();
	exchangeAuthorizationCode(code);
}

void OAuthConnector::sendBrowserResponse(QTcpSocket *socket, const QString &body)
{
	if (!socket)
		return;

	const QByteArray html = QString("<!doctype html><meta charset=\"utf-8\"><title>DSK Login</title><body>%1</body>").arg(body).toUtf8();
	socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: ");
	socket->write(QByteArray::number(html.size()));
	socket->write("\r\n\r\n");
	socket->write(html);
	socket->flush();

	connect(socket, &QTcpSocket::bytesWritten, socket, [socket](qint64) {
		if (socket->bytesToWrite() == 0)
			socket->disconnectFromHost();
	});
	QTimer::singleShot(1000, socket, [socket]() {
		if (socket->state() != QAbstractSocket::UnconnectedState)
			socket->disconnectFromHost();
	});
}

void OAuthConnector::exchangeAuthorizationCode(const QString &code)
{
	logInfo(QString("OAuth token exchange begin: mode=%1").arg(targetAuthModeToString(authMode_)));
	const OAuthProvider provider = oauthProviderForAuthMode(authMode_);
	const bool publisherRelay = oauthUsesPublisherRelay(authMode_);
	HttpRequest request;
	request.url = publisherRelay ? oauthPublisherRelayTokenUrl(provider) : provider.tokenUrl;
	request.method = QByteArrayLiteral("POST");
	request.timeoutMs = NetworkTimeoutMs;
	request.headers.push_back({QByteArrayLiteral("Content-Type"),
				  publisherRelay ? QByteArrayLiteral("application/json")
						 : QByteArrayLiteral("application/x-www-form-urlencoded")});

	if (publisherRelay) {
		const QJsonObject body{
			{QStringLiteral("grant_type"), QStringLiteral("authorization_code")},
			{QStringLiteral("code"), code},
			{QStringLiteral("redirect_uri"), redirectUri().toString()},
			{QStringLiteral("code_verifier"), codeVerifier_},
		};
		request.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
	} else {
		QUrlQuery body;
		body.addQueryItem("grant_type", "authorization_code");
		body.addQueryItem("client_id", clientId_);
		if (!clientSecret_.isEmpty())
			body.addQueryItem("client_secret", clientSecret_);
		body.addQueryItem("code", code);
		body.addQueryItem("redirect_uri", redirectUri().toString());
		body.addQueryItem("code_verifier", codeVerifier_);
		request.body = formBody(body);
	}

	http_->send(std::move(request), [this](HttpResponse response) { handleTokenReply(std::move(response)); });
}

void OAuthConnector::handleTokenReply(HttpResponse response)
{
	QString error;
	const QJsonObject object = responseObject(response, &error);
	if (!error.isEmpty()) {
		fail(QString("OAuth token exchange failed. %1").arg(error));
		return;
	}

	const QString accessToken = object.value("access_token").toString();
	if (accessToken.isEmpty()) {
		fail(QStringLiteral("OAuth token response did not include an access token."));
		return;
	}
	if (oauthUsesPublisherRelay(authMode_)) {
		const OAuthProvider provider = oauthProviderForAuthMode(authMode_);
		const QString metadataError = oauthValidatePublisherRelayTokenMetadata(provider, object, &clientId_);
		if (!metadataError.isEmpty()) {
			fail(metadataError);
			return;
		}
	}
	refreshToken_ = object.value("refresh_token").toString();
	if (authMode_ == TargetAuthMode::YouTubeOAuth && refreshToken_.isEmpty()) {
		fail(QStringLiteral("YouTube login did not return an offline refresh token. Revoke the DSK OBS grant in your Google Account, then connect again."));
		return;
	}
	logInfo(QString("OAuth token exchange succeeded: mode=%1 refreshToken=%2")
			.arg(targetAuthModeToString(authMode_),
			     refreshToken_.isEmpty() ? QStringLiteral("absent") : QStringLiteral("present")));

	if (authMode_ == TargetAuthMode::TwitchOAuth)
		fetchTwitchUser(accessToken);
	else if (authMode_ == TargetAuthMode::YouTubeOAuth)
		fetchYouTubeStreams(accessToken);
	else if (authMode_ == TargetAuthMode::KickOAuth) {
		const QJsonObject account = object.value(QStringLiteral("account")).toObject();
		QString accountName = account.value(QStringLiteral("display_name")).toString().trimmed();
		if (accountName.isEmpty())
			accountName = account.value(QStringLiteral("username")).toString().trimmed();
		fetchKickChannel(accessToken, accountName);
	}
	else
		fail(QStringLiteral("Unsupported OAuth login mode."));
}

void OAuthConnector::fetchTwitchUser(const QString &accessToken)
{
	HttpRequest request;
	request.url = QUrl(QStringLiteral("https://api.twitch.tv/helix/users"));
	request.timeoutMs = NetworkTimeoutMs;
	request.headers.push_back({QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + accessToken.toUtf8()});
	request.headers.push_back({QByteArrayLiteral("Client-Id"), clientId_.toUtf8()});
	http_->send(std::move(request), [this, accessToken](HttpResponse response) {
		handleTwitchUserReply(std::move(response), accessToken);
	});
}

void OAuthConnector::handleTwitchUserReply(HttpResponse response, const QString &accessToken)
{
	QString error;
	const QJsonObject object = responseObject(response, &error);
	if (!error.isEmpty()) {
		fail(QString("Twitch account lookup failed. %1").arg(error));
		return;
	}

	const QJsonArray data = object.value("data").toArray();
	if (data.isEmpty() || !data[0].isObject()) {
		fail(QStringLiteral("Twitch account lookup returned no user."));
		return;
	}
	const QJsonObject user = data[0].toObject();
	const QString broadcasterId = user.value("id").toString();
	const QString accountName = user.value("display_name").toString(user.value("login").toString());
	if (broadcasterId.isEmpty()) {
		fail(QStringLiteral("Twitch account lookup returned no broadcaster id."));
		return;
	}

	fetchTwitchStreamKey(accessToken, broadcasterId, accountName);
}

void OAuthConnector::fetchTwitchStreamKey(const QString &accessToken, const QString &broadcasterId, const QString &accountName)
{
	QUrl url("https://api.twitch.tv/helix/streams/key");
	QUrlQuery query;
	query.addQueryItem("broadcaster_id", broadcasterId);
	url.setQuery(query);
	HttpRequest request;
	request.url = url;
	request.timeoutMs = NetworkTimeoutMs;
	request.headers.push_back({QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + accessToken.toUtf8()});
	request.headers.push_back({QByteArrayLiteral("Client-Id"), clientId_.toUtf8()});
	http_->send(std::move(request), [this, accountName](HttpResponse response) {
		handleTwitchStreamKeyReply(std::move(response), accountName);
	});
}

void OAuthConnector::handleTwitchStreamKeyReply(HttpResponse response, const QString &accountName)
{
	QString error;
	const QJsonObject object = responseObject(response, &error);
	if (!error.isEmpty()) {
		fail(QString("Twitch stream key lookup failed. %1").arg(error));
		return;
	}
	const QJsonArray data = object.value("data").toArray();
	if (data.isEmpty() || !data[0].isObject()) {
		fail(QStringLiteral("Twitch stream key lookup returned no key."));
		return;
	}
	const QString streamKey = data[0].toObject().value("stream_key").toString();
	if (streamKey.isEmpty()) {
		fail(QStringLiteral("Twitch stream key lookup returned an empty key."));
		return;
	}

	OAuthConnectionResult result;
	result.authMode = authMode_;
	result.accountName = accountName;
	result.serverUrl = QStringLiteral("rtmp://live.twitch.tv/app");
	result.streamKey = streamKey;
	reset();
	emit finished(result);
}

void OAuthConnector::fetchKickChannel(const QString &accessToken, const QString &accountName)
{
	HttpRequest request;
	request.url = QUrl(QStringLiteral("https://api.kick.com/public/v1/channels"));
	request.timeoutMs = NetworkTimeoutMs;
	request.headers.push_back({QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + accessToken.toUtf8()});
	request.headers.push_back({QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json")});
	http_->send(std::move(request), [this, accountName](HttpResponse response) {
		handleKickChannelReply(std::move(response), accountName);
	});
}

void OAuthConnector::handleKickChannelReply(HttpResponse response, const QString &accountName)
{
	QString error;
	const QJsonObject object = responseObject(response, &error);
	if (!error.isEmpty()) {
		fail(QString("Kick channel lookup failed. %1").arg(error));
		return;
	}

	const KickChannelConnection connection = oauthParseKickChannelResponse(object);
	if (!connection.errorMessage.isEmpty()) {
		fail(connection.errorMessage);
		return;
	}

	OAuthConnectionResult result;
	result.authMode = authMode_;
	result.accountName = connection.accountName.isEmpty() ? accountName : connection.accountName;
	if (result.accountName.isEmpty())
		result.accountName = QStringLiteral("Kick");
	result.serverUrl = connection.serverUrl;
	result.streamKey = connection.streamKey;
	reset();
	emit finished(result);
}

void OAuthConnector::fetchYouTubeStreams(const QString &accessToken, const QString &pageToken)
{
	QUrl url(QStringLiteral("https://www.googleapis.com/youtube/v3/liveStreams"));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("part"), QStringLiteral("id,snippet,cdn,status,contentDetails"));
	query.addQueryItem(QStringLiteral("mine"), QStringLiteral("true"));
	query.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("50"));
	if (!pageToken.isEmpty())
		query.addQueryItem(QStringLiteral("pageToken"), pageToken);
	url.setQuery(query);

	HttpRequest request;
	request.url = url;
	request.timeoutMs = NetworkTimeoutMs;
	request.headers.push_back({QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + accessToken.toUtf8()});
	request.headers.push_back({QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json")});
	http_->send(std::move(request), [this, accessToken](HttpResponse response) {
		handleYouTubeStreamsReply(std::move(response), accessToken);
	});
}

void OAuthConnector::handleYouTubeStreamsReply(HttpResponse response, const QString &accessToken)
{
	QString error;
	const QJsonObject object = responseObject(response, &error);
	if (!error.isEmpty()) {
		completeYouTubeLogin(QStringLiteral("YouTube stream list could not be loaded. %1 You can still enter a stream key manually.")
					     .arg(error));
		return;
	}

	const YouTubeStreamPage page = parseYouTubeStreamPage(object);
	for (const YouTubeStreamOption &stream : page.streams) {
		bool duplicate = false;
		for (const YouTubeStreamOption &existing : youtubeStreams_) {
			if (existing.streamKey == stream.streamKey) {
				duplicate = true;
				break;
			}
		}
		if (!duplicate)
			youtubeStreams_.push_back(stream);
	}

	++youtubeStreamPageCount_;
	if (!page.nextPageToken.isEmpty() && youtubeStreamPageCount_ < MaxYouTubeStreamPages) {
		fetchYouTubeStreams(accessToken, page.nextPageToken);
		return;
	}
	if (!page.nextPageToken.isEmpty()) {
		completeYouTubeLogin(QStringLiteral("Only the first %1 pages of YouTube streams were loaded. You can still select a loaded stream or enter a key manually.")
					     .arg(MaxYouTubeStreamPages));
		return;
	}
	completeYouTubeLogin();
}

void OAuthConnector::completeYouTubeLogin(const QString &streamLookupWarning)
{
	OAuthConnectionResult result;
	result.authMode = authMode_;
	result.accountName = QStringLiteral("YouTube");
	result.serverUrl = QStringLiteral("rtmp://a.rtmp.youtube.com/live2");
	result.refreshToken = refreshToken_;
	result.youtubeStreams = youtubeStreams_;
	result.youtubeStreamLookupWarning = streamLookupWarning;
	logInfo(QStringLiteral("OAuth YouTube login completed: reusableStreams=%1 lookupWarning=%2")
			.arg(result.youtubeStreams.size())
			.arg(streamLookupWarning.isEmpty() ? QStringLiteral("absent") : QStringLiteral("present")));
	reset();
	emit finished(result);
}

void OAuthConnector::fail(const QString &message)
{
	logError(QString("OAuth failed: %1").arg(message));
	OAuthConnectionResult result;
	result.authMode = authMode_;
	result.errorMessage = message;
	reset();
	emit finished(result);
}

void OAuthConnector::reset()
{
	running_ = false;
	if (timeout_)
		timeout_->stop();
	if (http_)
		http_->abortAll();
	if (server_) {
		server_->close();
		server_->deleteLater();
		server_ = nullptr;
	}
	clientId_.clear();
	clientSecret_.clear();
	refreshToken_.clear();
	state_.clear();
	codeVerifier_.clear();
	youtubeStreams_.clear();
	youtubeStreamPageCount_ = 0;
	callbackHandled_ = false;
}

QUrl OAuthConnector::redirectUri() const
{
	return QUrl(QString("http://localhost:%1/callback").arg(OAuthCallbackPort));
}

} // namespace dsk
