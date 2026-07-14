#include "core/http-client.hpp"

#include <QMetaObject>
#include <QThreadPool>

#include <curl/curl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace dsk {

namespace {

struct TransferContext {
	QByteArray response;
	qint64 maxResponseBytes = 0;
	std::shared_ptr<std::atomic_bool> cancelled;
	bool responseTooLarge = false;
};

int curlInitializationResult()
{
	static std::once_flag once;
	static CURLcode result = CURLE_FAILED_INIT;
	std::call_once(once, []() { result = curl_global_init(CURL_GLOBAL_DEFAULT); });
	return static_cast<int>(result);
}

size_t writeResponse(char *data, size_t size, size_t count, void *userData)
{
	auto *context = static_cast<TransferContext *>(userData);
	if (!context || !data)
		return 0;
	if (size != 0 && count > std::numeric_limits<size_t>::max() / size)
		return 0;

	const size_t bytes = size * count;
	const qint64 currentSize = context->response.size();
	if (bytes > static_cast<size_t>(std::numeric_limits<qint64>::max()) ||
	    currentSize > context->maxResponseBytes - static_cast<qint64>(bytes)) {
		context->responseTooLarge = true;
		return 0;
	}

	context->response.append(data, static_cast<qsizetype>(bytes));
	return bytes;
}

int transferProgress(void *userData, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	auto *context = static_cast<TransferContext *>(userData);
	return context && context->cancelled && context->cancelled->load(std::memory_order_relaxed) ? 1 : 0;
}

bool hasHeaderInjection(const QByteArray &value)
{
	return value.contains('\r') || value.contains('\n');
}

bool isValidMethod(const QByteArray &method)
{
	if (method.isEmpty())
		return false;
	for (const char value : method) {
		if (value < 'A' || value > 'Z')
			return false;
	}
	return true;
}

HttpResponse performRequest(const HttpRequest &request, const std::shared_ptr<std::atomic_bool> &cancelled)
{
	HttpResponse response;
	if (curlInitializationResult() != CURLE_OK) {
		response.transportError = QStringLiteral("Could not initialize the signed OBS HTTP runtime.");
		return response;
	}

	const QString scheme = request.url.scheme().toLower();
	if (!request.url.isValid() || (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) ||
	    request.url.host().trimmed().isEmpty()) {
		response.transportError = QStringLiteral("HTTP request URL is invalid or unsupported.");
		return response;
	}
	if (request.timeoutMs <= 0 || request.maxResponseBytes <= 0) {
		response.transportError = QStringLiteral("HTTP timeout or response limit is invalid.");
		return response;
	}

	QByteArray method = request.method.trimmed().toUpper();
	if (!isValidMethod(method)) {
		response.transportError = QStringLiteral("HTTP method is invalid.");
		return response;
	}
	if (method == QByteArrayLiteral("GET") && !request.body.isEmpty()) {
		response.transportError = QStringLiteral("GET requests cannot include a request body.");
		return response;
	}

	for (const auto &header : request.headers) {
		const QByteArray name = header.first.trimmed();
		if (name.isEmpty() || name.contains(':') || hasHeaderInjection(name) || hasHeaderInjection(header.second)) {
			response.transportError = QStringLiteral("HTTP header contains invalid characters.");
			return response;
		}
	}

	CURL *curl = curl_easy_init();
	if (!curl) {
		response.transportError = QStringLiteral("Could not create an HTTP request.");
		return response;
	}

	curl_slist *headers = nullptr;
	bool headerAllocationFailed = false;
	for (const auto &header : request.headers) {
		const QByteArray line = header.first.trimmed() + QByteArrayLiteral(": ") + header.second;
		curl_slist *next = curl_slist_append(headers, line.constData());
		if (!next) {
			headerAllocationFailed = true;
			break;
		}
		headers = next;
	}
	if (headerAllocationFailed) {
		curl_slist_free_all(headers);
		curl_easy_cleanup(curl);
		response.transportError = QStringLiteral("Could not allocate HTTP request headers.");
		return response;
	}

	TransferContext context;
	context.maxResponseBytes = request.maxResponseBytes;
	context.cancelled = cancelled;
	std::array<char, CURL_ERROR_SIZE> errorBuffer{};
	const QByteArray encodedUrl = request.url.toEncoded(QUrl::FullyEncoded);
	const long connectTimeoutMs = std::min(request.timeoutMs, 10 * 1000);

	curl_easy_setopt(curl, CURLOPT_URL, encodedUrl.constData());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "DSK-OBS-Multistream/0.1");
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, connectTimeoutMs);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(request.timeoutMs));
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 0L);
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
	curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeResponse);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, transferProgress);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &context);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer.data());

	if (method == QByteArrayLiteral("GET")) {
		curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
	} else if (method == QByteArrayLiteral("POST")) {
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.constData());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
	} else {
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.constData());
		if (!request.body.isEmpty()) {
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.constData());
			curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
		}
	}

	const CURLcode result = cancelled->load(std::memory_order_relaxed) ? CURLE_ABORTED_BY_CALLBACK : curl_easy_perform(curl);
	long statusCode = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
	response.statusCode = statusCode > 0 && statusCode <= std::numeric_limits<int>::max() ? static_cast<int>(statusCode) : 0;
	response.body = std::move(context.response);

	if (result != CURLE_OK && !cancelled->load(std::memory_order_relaxed)) {
		if (context.responseTooLarge) {
			response.transportError = QStringLiteral("HTTP response exceeded the configured size limit.");
		} else {
			const QString detail = errorBuffer[0] != '\0' ? QString::fromUtf8(errorBuffer.data())
							       : QString::fromUtf8(curl_easy_strerror(result));
			response.transportError = detail.isEmpty() ? QStringLiteral("HTTP request failed.") : detail;
		}
	}

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	return response;
}

} // namespace

struct HttpClient::SharedState {
	std::mutex mutex;
	bool shuttingDown = false;
	quint64 nextRequestId = 1;
	std::unordered_map<quint64, std::shared_ptr<std::atomic_bool>> pending;
};

bool HttpResponse::isSuccess() const
{
	return transportError.isEmpty() && statusCode >= 200 && statusCode < 300;
}

HttpClient::HttpClient(QObject *parent)
	: QObject(parent),
	  state_(std::make_shared<SharedState>()),
	  pool_(std::make_unique<QThreadPool>())
{
	pool_->setMaxThreadCount(4);
	pool_->setExpiryTimeout(30 * 1000);
}

HttpClient::~HttpClient()
{
	{
		std::lock_guard<std::mutex> lock(state_->mutex);
		state_->shuttingDown = true;
		for (const auto &entry : state_->pending)
			entry.second->store(true, std::memory_order_relaxed);
		state_->pending.clear();
	}
	pool_->waitForDone();
}

quint64 HttpClient::send(HttpRequest request, Completion completion)
{
	quint64 requestId = 0;
	auto cancelled = std::make_shared<std::atomic_bool>(false);
	{
		std::lock_guard<std::mutex> lock(state_->mutex);
		if (state_->shuttingDown)
			return 0;
		requestId = state_->nextRequestId++;
		if (requestId == 0)
			requestId = state_->nextRequestId++;
		state_->pending.emplace(requestId, cancelled);
	}

	const std::shared_ptr<SharedState> state = state_;
	HttpClient *receiver = this;
	pool_->start([state, receiver, requestId, cancelled, request = std::move(request), completion = std::move(completion)]() mutable {
		HttpResponse response = performRequest(request, cancelled);
		bool shouldQueue = false;
		{
			std::lock_guard<std::mutex> lock(state->mutex);
			shouldQueue = !state->shuttingDown && !cancelled->load(std::memory_order_relaxed) &&
				      state->pending.find(requestId) != state->pending.end();
			if (!shouldQueue)
				state->pending.erase(requestId);
		}
		if (!shouldQueue)
			return;

		const bool queued = QMetaObject::invokeMethod(
			receiver,
			[state, requestId, cancelled, completion = std::move(completion), response = std::move(response)]() mutable {
				bool shouldComplete = false;
				{
					std::lock_guard<std::mutex> lock(state->mutex);
					const auto pending = state->pending.find(requestId);
					shouldComplete = !state->shuttingDown && !cancelled->load(std::memory_order_relaxed) &&
							 pending != state->pending.end();
					if (pending != state->pending.end())
						state->pending.erase(pending);
				}
				if (shouldComplete && completion)
					completion(std::move(response));
			},
			Qt::QueuedConnection);
		if (!queued) {
			std::lock_guard<std::mutex> lock(state->mutex);
			state->pending.erase(requestId);
		}
	});

	return requestId;
}

void HttpClient::abortAll()
{
	std::lock_guard<std::mutex> lock(state_->mutex);
	for (const auto &entry : state_->pending)
		entry.second->store(true, std::memory_order_relaxed);
	state_->pending.clear();
}

} // namespace dsk
