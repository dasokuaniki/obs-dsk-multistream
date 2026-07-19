#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPair>
#include <QUrl>

#include <functional>
#include <memory>

class QThreadPool;

namespace dsk {

struct HttpRequest {
	QUrl url;
	QByteArray method = QByteArrayLiteral("GET");
	QList<QPair<QByteArray, QByteArray>> headers;
	QByteArray body;
	int timeoutMs = 20 * 1000;
	qint64 maxResponseBytes = 2 * 1024 * 1024;
};

struct HttpResponse {
	int statusCode = 0;
	QByteArray body;
	QString transportError;

	bool isSuccess() const;
};

class HttpClient final : public QObject {
public:
	using Completion = std::function<void(HttpResponse)>;

	explicit HttpClient(QObject *parent = nullptr);
	~HttpClient() override;

	quint64 send(HttpRequest request, Completion completion);
	void abortAll();

private:
	struct SharedState;

	std::shared_ptr<SharedState> state_;
	std::unique_ptr<QThreadPool> pool_;
};

} // namespace dsk
