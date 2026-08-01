#include "core/http-client.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

#include <functional>
#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char *message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
}

bool waitUntil(const std::function<bool()> &predicate, int timeoutMs = 5000)
{
	QElapsedTimer timer;
	timer.start();
	while (!predicate() && timer.elapsed() < timeoutMs) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
		QThread::msleep(2);
	}
	QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
	return predicate();
}

struct CapturedRequest {
	QByteArray method;
	QByteArray path;
	QByteArray headers;
	QByteArray body;
};

class LocalHttpServer {
public:
	using Responder = std::function<void(QTcpSocket *, const CapturedRequest &)>;

	explicit LocalHttpServer(Responder responder)
		: responder_(std::move(responder))
	{
		QObject::connect(&server_, &QTcpServer::newConnection, &server_, [this]() {
			while (server_.hasPendingConnections()) {
				QTcpSocket *socket = server_.nextPendingConnection();
				if (!socket)
					continue;
				buffers_.insert(socket, {});
				QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
					buffers_[socket].append(socket->readAll());
					process(socket);
				});
				QObject::connect(socket, &QTcpSocket::disconnected, socket, [this, socket]() {
					buffers_.remove(socket);
					socket->deleteLater();
				});
			}
		});
		check(server_.listen(QHostAddress::LocalHost), "local HTTP server listens");
	}

	QUrl url(const QString &path) const
	{
		return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(server_.serverPort()).arg(path));
	}

	static void reply(QTcpSocket *socket, int status, const QByteArray &body,
			  const QByteArray &contentType = QByteArrayLiteral("application/json"))
	{
		const QByteArray reason = status >= 200 && status < 300 ? QByteArrayLiteral("OK") : QByteArrayLiteral("Error");
		QByteArray response = QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(status) + ' ' + reason +
				      QByteArrayLiteral("\r\nContent-Type: ") + contentType +
				      QByteArrayLiteral("\r\nConnection: close\r\nContent-Length: ") +
				      QByteArray::number(body.size()) + QByteArrayLiteral("\r\n\r\n") + body;
		socket->write(response);
		socket->disconnectFromHost();
	}

private:
	void process(QTcpSocket *socket)
	{
		const QByteArray request = buffers_.value(socket);
		const qsizetype headerEnd = request.indexOf("\r\n\r\n");
		if (headerEnd < 0)
			return;

		const QByteArray headers = request.left(headerEnd);
		qint64 contentLength = 0;
		for (const QByteArray &line : headers.split('\n')) {
			const QByteArray cleanLine = line.trimmed();
			if (cleanLine.toLower().startsWith("content-length:"))
				contentLength = cleanLine.mid(sizeof("content-length:") - 1).trimmed().toLongLong();
		}
		const qsizetype bodyStart = headerEnd + 4;
		if (request.size() - bodyStart < contentLength)
			return;

		const QList<QByteArray> requestLine = headers.split('\n').value(0).trimmed().split(' ');
		CapturedRequest captured;
		captured.method = requestLine.value(0);
		captured.path = requestLine.value(1);
		captured.headers = headers;
		captured.body = request.mid(bodyStart, contentLength);
		buffers_.remove(socket);
		responder_(socket, captured);
	}

	QTcpServer server_;
	QHash<QTcpSocket *, QByteArray> buffers_;
	Responder responder_;
};

void testGetAndPost()
{
	QList<CapturedRequest> captured;
	LocalHttpServer server([&captured](QTcpSocket *socket, const CapturedRequest &request) {
		captured.push_back(request);
		LocalHttpServer::reply(socket, 200, request.path == QByteArrayLiteral("/post") ? request.body
												  : QByteArrayLiteral("{\"ok\":true}"));
	});
	dsk::HttpClient client;

	bool getDone = false;
	dsk::HttpResponse getResponse;
	dsk::HttpRequest get;
	get.url = server.url(QStringLiteral("/ok"));
	client.send(get, [&getDone, &getResponse](dsk::HttpResponse response) {
		getResponse = std::move(response);
		getDone = true;
	});
	check(waitUntil([&getDone]() { return getDone; }), "GET callback completes");
	check(getResponse.isSuccess(), "GET reports success");
	check(getResponse.body == QByteArrayLiteral("{\"ok\":true}"), "GET returns response body");

	bool postDone = false;
	dsk::HttpResponse postResponse;
	dsk::HttpRequest post;
	post.url = server.url(QStringLiteral("/post"));
	post.method = QByteArrayLiteral("POST");
	post.headers.push_back({QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json")});
	post.body = QByteArrayLiteral("{\"value\":42}");
	client.send(post, [&postDone, &postResponse](dsk::HttpResponse response) {
		postResponse = std::move(response);
		postDone = true;
	});
	check(waitUntil([&postDone]() { return postDone; }), "POST callback completes");
	check(postResponse.isSuccess(), "POST reports success");
	check(postResponse.body == post.body, "POST response echoes request body");
	check(captured.size() == 2, "server captured two requests");
	if (captured.size() == 2) {
		check(captured[0].method == QByteArrayLiteral("GET"), "server captured GET method");
		check(captured[1].method == QByteArrayLiteral("POST"), "server captured POST method");
		check(captured[1].body == post.body, "server captured POST body");
	}
}

void testResponseLimit()
{
	LocalHttpServer server([](QTcpSocket *socket, const CapturedRequest &) {
		LocalHttpServer::reply(socket, 200, QByteArray(4096, 'x'), QByteArrayLiteral("text/plain"));
	});
	dsk::HttpClient client;
	bool done = false;
	dsk::HttpResponse response;
	dsk::HttpRequest request;
	request.url = server.url(QStringLiteral("/large"));
	request.maxResponseBytes = 128;
	client.send(request, [&done, &response](dsk::HttpResponse value) {
		response = std::move(value);
		done = true;
	});
	check(waitUntil([&done]() { return done; }), "oversized response callback completes");
	check(!response.isSuccess(), "oversized response is rejected");
	check(response.transportError.contains(QStringLiteral("size limit")), "oversized response explains limit");
}

void testAbortSuppressesCallback()
{
	bool requestArrived = false;
	LocalHttpServer server([&requestArrived](QTcpSocket *, const CapturedRequest &) { requestArrived = true; });
	bool callbackCalled = false;
	QElapsedTimer shutdownTimer;
	{
		dsk::HttpClient client;
		dsk::HttpRequest request;
		request.url = server.url(QStringLiteral("/slow"));
		request.timeoutMs = 10 * 1000;
		client.send(request, [&callbackCalled](dsk::HttpResponse) { callbackCalled = true; });
		check(waitUntil([&requestArrived]() { return requestArrived; }), "slow request reaches server");
		client.abortAll();
		shutdownTimer.start();
	}
	check(shutdownTimer.elapsed() < 2000, "cancelled HTTP worker shuts down promptly");
	QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
	check(!callbackCalled, "aborted request suppresses callback");
}

void testLiveRelay()
{
	dsk::HttpClient client;
	bool done = false;
	dsk::HttpResponse response;
	dsk::HttpRequest request;
	request.url = QUrl(QStringLiteral("https://auth.dasoku.org/v1/ready?platform=kick&profile=multistream"));
	client.send(request, [&done, &response](dsk::HttpResponse value) {
		response = std::move(value);
		done = true;
	});
	check(waitUntil([&done]() { return done; }, 15000), "live HTTPS relay callback completes");
	check(response.isSuccess(), "live HTTPS relay request succeeds");
	const QJsonObject object = QJsonDocument::fromJson(response.body).object();
	check(object.value(QStringLiteral("platform")).toString() == QStringLiteral("kick"), "live relay identifies Kick");
	check(object.value(QStringLiteral("profile")).toString() == QStringLiteral("multistream"), "live relay identifies multistream profile");
	check(object.value(QStringLiteral("ok")).toBool(), "live Kick relay is ready");
}

} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	testGetAndPost();
	testResponseLimit();
	testAbortSuppressesCallback();
	if (app.arguments().contains(QStringLiteral("--live-relay")))
		testLiveRelay();

	if (failures == 0)
		std::cout << "All HTTP client tests passed.\n";
	return failures == 0 ? 0 : 1;
}
