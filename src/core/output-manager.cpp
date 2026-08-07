#include "core/output-manager.hpp"

#include "core/comment-viewer-contract.hpp"
#include "core/diagnostics.hpp"
#include "core/output-signal-policy.hpp"
#include "core/oauth-provider.hpp"
#include "core/secret-store.hpp"
#include "core/youtube-api-warning.hpp"
#include "core/youtube-archive-rotation.hpp"
#include "core/youtube-broadcast-selector.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <media-io/video-io.h>
#include <util/config-file.h>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QUuid>
#include <QUrlQuery>

#include <cstring>
#include <initializer_list>
#include <limits>
#include <utility>


namespace dsk {

namespace {

constexpr int PlatformApiTimeoutMs = 20 * 1000;
constexpr int CommentViewerSelectionTimeoutMs = 750;
constexpr int YouTubeApiPageSize = 50;
constexpr int YouTubeMaxBroadcastPagesPerStatus = 10;
constexpr int YouTubeMaxTransientRetries = 5;
constexpr qint64 YouTubeSignalWaitTimeoutMs = 2 * 60 * 1000;
constexpr qint64 YouTubeAutoStartWaitTimeoutMs = 5 * 60 * 1000;
constexpr int YouTubeArchiveRotationRetryMs = 5 * 60 * 1000;
constexpr int YouTubeArchiveTransitionPollMs = 2000;
constexpr int YouTubeArchiveTransitionMaxPolls = 30;
constexpr uint32_t DskVideoCanvasFlags = ACTIVATE | SCENE_REF | EPHEMERAL;

} // namespace

struct OutputManager::Session {
	quint64 serial = 0;
	QString targetId;
	obs_output_t *output = nullptr;
	obs_service_t *service = nullptr;
	qint64 startedAtMs = 0;
	QString sharedEncoderKey;
	QString youtubeResolvedStreamKey;
	QString youtubeAccessToken;
	qint64 youtubeAccessTokenExpiresAtMs = 0;
	qint64 youtubeSignalActiveAtMs = 0;
	qint64 youtubeAutoStartWaitingSinceMs = 0;
	quint64 youtubeOperationGeneration = 0;
	quint64 youtubePollGeneration = 0;
	int youtubeTransientRetryCount = 0;
	bool youtubeOperationInFlight = false;
	bool youtubeAuthRefreshRetried = false;
	bool youtubePreflight = false;
	bool youtubeCommentViewerSelectionChecked = false;
	bool youtubeAwaitingSelection = false;
	bool youtubeBroadcastSelectionConfirmed = false;
	bool youtubeCompletedReuseLookup = false;
	QJsonObject youtubeRotationCurrentBroadcast;
	QString youtubeRotationCurrentBroadcastId;
	QString youtubeRotationStreamId;
	QString youtubeRotationNextBroadcastId;
	int youtubeRotationNextPart = 2;
	int youtubeRotationPolls = 0;
	quint64 youtubeRotationTimerGeneration = 0;
	bool youtubeRotationCurrentCompleted = false;
	bool pendingRelease = false;
	int releasePolls = 0;
};

struct OutputManager::SharedEncoderSet {
	QString key;
	obs_encoder_t *videoEncoder = nullptr;
	obs_encoder_t *audioEncoder = nullptr;
	int refs = 0;
};

static void copyTargetFields(OutputTarget &to, const OutputTarget &from, bool copySceneRoutes = true)
{
	to.id = from.id;
	to.name = from.name;
	to.platformId = from.platformId;
	to.authMode = from.authMode;
	to.youtubeBroadcastMode = from.youtubeBroadcastMode;
	to.authAccountName = from.authAccountName;
	to.authCredentialRef = from.authCredentialRef;
	to.oauthClientId = from.oauthClientId;
	to.oauthClientSecret = from.oauthClientSecret;
	to.oauthClientSecretRef = from.oauthClientSecretRef;
	to.oauthRefreshToken = from.oauthRefreshToken;
	to.oauthRefreshTokenRef = from.oauthRefreshTokenRef;
	to.serverUrl = from.serverUrl;
	to.streamKey = from.streamKey;
	to.encoderGroup = from.encoderGroup;
	to.useSharedEncoder = from.useSharedEncoder;
	to.autoStartWithObs = from.autoStartWithObs;
	to.autoStopWithObs = from.autoStopWithObs;
	to.reconnectEnabled = from.reconnectEnabled;
	to.reconnectMaxRetries = from.reconnectMaxRetries;
	to.reconnectDelaySeconds = from.reconnectDelaySeconds;
	to.videoBitrateKbps = from.videoBitrateKbps;
	to.audioBitrateKbps = from.audioBitrateKbps;
	to.keyframeSeconds = from.keyframeSeconds;
	to.videoEncoderId = from.videoEncoderId;
	to.audioEncoderId = from.audioEncoderId;
	to.sceneMode = from.sceneMode;
	to.sceneName = from.sceneName;
	to.sceneUuid = from.sceneUuid;
	to.sceneRoutes.clear();
	if (copySceneRoutes) {
		for (const auto &route : from.sceneRoutes)
			to.sceneRoutes.push_back(route);
	}
	to.enabled = from.enabled;
	to.startWithAll = from.startWithAll;
	to.state = from.state;
	to.lastError = from.lastError;
}

static QString videoOutputSummary(video_t *video)
{
	if (!video)
		return QStringLiteral("unavailable");

	const video_output_info *info = video_output_get_info(video);
	if (!info)
		return QStringLiteral("unavailable");

	const double fps = info->fps_den ? double(info->fps_num) / double(info->fps_den) : 0.0;
	return QStringLiteral("%1x%2 %3 fps %4")
		.arg(info->width)
		.arg(info->height)
		.arg(fps, 0, 'f', 2)
		.arg(QString::fromUtf8(get_video_format_name(info->format)));
}

static bool videoEncoderAvailable(const QString &encoderId)
{
	if (encoderId.trimmed().isEmpty())
		return false;
	const QByteArray id = encoderId.trimmed().toUtf8();
	return obs_encoder_get_display_name(id.constData()) && obs_get_encoder_type(id.constData()) == OBS_ENCODER_VIDEO;
}

static QString normalizedSettingsPath(const QString &path)
{
	return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

static QString firstAvailableVideoEncoder(std::initializer_list<const char *> encoderIds, const QString &fallback = QStringLiteral("obs_x264"))
{
	for (const char *encoderId : encoderIds) {
		const QString candidate = QString::fromUtf8(encoderId);
		if (videoEncoderAvailable(candidate))
			return candidate;
	}
	return fallback;
}

static QString obsSelectionToH264Encoder(const QString &selectedEncoder)
{
	const QString encoder = selectedEncoder.trimmed();
	if (encoder.isEmpty())
		return QStringLiteral("obs_x264");

	const QString lower = encoder.toLower();
	if (lower.contains(QStringLiteral("nvenc")))
		return firstAvailableVideoEncoder({"obs_nvenc_h264_tex", "ffmpeg_nvenc"});
	if (lower.contains(QStringLiteral("amf")) || lower.startsWith(QStringLiteral("amd")))
		return firstAvailableVideoEncoder({"h264_texture_amf", "amd_amf_h264", "obs_x264"});
	if (lower.contains(QStringLiteral("qsv")))
		return firstAvailableVideoEncoder({"obs_qsv11_v2", "obs_qsv11", "obs_x264"});
	if (lower.contains(QStringLiteral("videotoolbox")) || lower.startsWith(QStringLiteral("apple_")))
		return firstAvailableVideoEncoder({"com.apple.videotoolbox.videoencoder.ave.avc", "obs_x264"});
	if (lower == QStringLiteral("x264") || lower == QStringLiteral("x264_lowcpu"))
		return QStringLiteral("obs_x264");

	if (videoEncoderAvailable(encoder)) {
		const QByteArray id = encoder.toUtf8();
		const char *codec = obs_get_encoder_codec(id.constData());
		if (codec && QString::fromUtf8(codec).compare(QStringLiteral("h264"), Qt::CaseInsensitive) == 0)
			return encoder;
	}

	return firstAvailableVideoEncoder({"obs_x264"});
}

static bool isNativeNvencEncoder(const QString &encoderId)
{
	return encoderId.startsWith(QStringLiteral("obs_nvenc_"));
}

static bool isFfmpegNvencEncoder(const QString &encoderId)
{
	return encoderId == QStringLiteral("ffmpeg_nvenc") || encoderId == QStringLiteral("ffmpeg_hevc_nvenc");
}

static void ensureCredentialRefs(OutputTarget &target)
{
	if (!target.streamKey.isEmpty() && target.authCredentialRef.isEmpty())
		target.authCredentialRef = SecretStore::streamKeyCredentialRef(target.id);
	if (!target.oauthClientSecret.isEmpty() && target.oauthClientSecretRef.isEmpty())
		target.oauthClientSecretRef = SecretStore::oauthClientSecretCredentialRef(target.id);
	if (!target.oauthRefreshToken.isEmpty() && target.oauthRefreshTokenRef.isEmpty())
		target.oauthRefreshTokenRef = SecretStore::oauthRefreshTokenCredentialRef(target.id);
}

static bool stageCredentialForReassignedTarget(QString &secret, QString &credentialRef,
					       const QString &replacementRef, QString *warning)
{
	if (!secret.isEmpty()) {
		credentialRef = replacementRef;
		return true;
	}
	if (credentialRef.trimmed().isEmpty())
		return true;
	if (!SecretStore::isOwnedCredentialRef(credentialRef)) {
		if (warning)
			*warning = QStringLiteral("A saved credential reference outside the DSK namespace was not copied.");
		return false;
	}

	SecretStore secrets;
	QString loadedSecret;
	QString error;
	const SecretReadResult result = secrets.readSecretResult(credentialRef, &loadedSecret, &error);
	if (result == SecretReadResult::Found && !loadedSecret.isEmpty()) {
		secret = loadedSecret;
		credentialRef = replacementRef;
		return true;
	}

	if (warning) {
		*warning = result == SecretReadResult::NotFound
			? QStringLiteral("Credential %1 was not found; its old reference was preserved.").arg(credentialRef)
			: QStringLiteral("Credential %1 could not be copied: %2").arg(credentialRef, error);
	}
	return false;
}

static QSet<QString> credentialRefsForTarget(const OutputTarget &target)
{
	QSet<QString> refs;
	for (const QString &ref : {target.authCredentialRef, target.oauthClientSecretRef, target.oauthRefreshTokenRef}) {
		if (!ref.trimmed().isEmpty())
			refs.insert(ref.trimmed());
	}
	return refs;
}

static QStringList deleteUnreferencedTargetSecrets(const OutputTarget &target,
						    const QVector<OutputTarget> &remainingTargets)
{
	QStringList failures;
	QSet<QString> retainedRefs;
	for (const auto &remaining : remainingTargets)
		retainedRefs.unite(credentialRefsForTarget(remaining));

	SecretStore secrets;
	for (const QString &ref : credentialRefsForTarget(target)) {
		if (retainedRefs.contains(ref))
			continue;
		if (!SecretStore::isOwnedCredentialRef(ref)) {
			logWarning(QStringLiteral("Refusing to delete a credential outside the DSK namespace."));
			continue;
		}
		QString error;
		if (!secrets.deleteSecret(ref, &error)) {
			logWarning(QString("Failed to delete DSK credential %1: %2").arg(ref, error));
			failures.push_back(QStringLiteral("%1 (%2)").arg(ref, error));
		}
	}
	return failures;
}

static void releaseOutputAndService(obs_output_t *output, obs_service_t *service)
{
	// obs_output_set_service keeps a non-owning pointer to the creator-owned
	// service. Destroy the output while that pointer is still valid, then drop
	// the service reference returned by obs_service_create.
	if (output)
		obs_output_release(output);
	if (service)
		obs_service_release(service);
}

static quint64 outputSessionSerial(obs_output_t *output)
{
	const char *rawName = output ? obs_output_get_name(output) : nullptr;
	const QString name = rawName ? QString::fromUtf8(rawName) : QString();
	const QString marker = QStringLiteral("_session_");
	const int markerIndex = name.lastIndexOf(marker);
	if (markerIndex < 0)
		return 0;
	bool ok = false;
	const quint64 serial = name.mid(markerIndex + marker.size()).toULongLong(&ok);
	return ok ? serial : 0;
}

static QString outputStopCodeText(int code)
{
	switch (code) {
	case OBS_OUTPUT_SUCCESS:
		return "Stopped.";
	case OBS_OUTPUT_BAD_PATH:
		return "Bad output path or server URL.";
	case OBS_OUTPUT_CONNECT_FAILED:
		return "Connection failed.";
	case OBS_OUTPUT_INVALID_STREAM:
		return "Invalid stream. Check the stream key and platform ingest server.";
	case OBS_OUTPUT_ERROR:
		return "Output error.";
	case OBS_OUTPUT_DISCONNECTED:
		return "Disconnected.";
	case OBS_OUTPUT_UNSUPPORTED:
		return "Unsupported output.";
	case OBS_OUTPUT_NO_SPACE:
		return "No space left.";
	case OBS_OUTPUT_ENCODE_ERROR:
		return "Encoder error.";
	case OBS_OUTPUT_HDR_DISABLED:
		return "HDR output is disabled.";
	default:
		return QString("Output stopped with code %1.").arg(code);
	}
}

static QString outputStopMessage(int code, const QString &lastError)
{
	QString message = outputStopCodeText(code);
	if (!lastError.trimmed().isEmpty())
		message = QString("%1 %2").arg(message, lastError.trimmed());
	return message;
}

static QByteArray formBody(const QUrlQuery &query)
{
	return query.toString(QUrl::FullyEncoded).toUtf8();
}

static QString stripHtml(const QString &value)
{
	QString cleaned = value;
	cleaned.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
	return cleaned.simplified();
}

static bool isQuotaExceededText(const QString &value)
{
	const QString lower = value.toLower();
	return lower.contains(QStringLiteral("quota")) || lower.contains(QStringLiteral("dailylimitexceeded"));
}

static QStringList youtubeApiErrorReasons(const HttpResponse &response)
{
	QStringList reasons;
	const QJsonDocument document = QJsonDocument::fromJson(response.body);
	if (!document.isObject())
		return reasons;

	const QJsonObject error = document.object().value(QStringLiteral("error")).toObject();
	const auto appendReason = [&reasons](const QJsonValue &value) {
		const QString reason = value.toString().trimmed();
		if (!reason.isEmpty() && !reasons.contains(reason))
			reasons.push_back(reason);
	};
	appendReason(error.value(QStringLiteral("reason")));
	for (const QJsonValue &value : error.value(QStringLiteral("errors")).toArray())
		appendReason(value.toObject().value(QStringLiteral("reason")));
	return reasons;
}

static bool youtubeApiErrorHasAnyReason(const HttpResponse &response,
					std::initializer_list<const char *> expectedReasons)
{
	const QStringList reasons = youtubeApiErrorReasons(response);
	for (const char *expected : expectedReasons) {
		if (reasons.contains(QString::fromLatin1(expected)))
			return true;
	}
	return false;
}

static bool isRetryableYouTubeResponse(const HttpResponse &response, const QString &error)
{
	if (!response.transportError.isEmpty())
		return true;
	if (isQuotaExceededText(error) ||
	    youtubeApiErrorHasAnyReason(response, {"quotaExceeded", "dailyLimitExceeded", "dailyLimitExceededUnreg"}))
		return false;
	if (response.statusCode == 403 &&
	    youtubeApiErrorHasAnyReason(response,
					{"userRequestsExceedRateLimit", "rateLimitExceeded", "userRateLimitExceeded",
					 "userRateLimitExceededUnreg"}))
		return true;
	return response.statusCode == 408 || response.statusCode == 429 || response.statusCode >= 500;
}

static bool canContinuePlatformStart(const OutputTarget &target, const TargetRuntimeStatus &runtime)
{
	return target.state == TargetState::Starting || target.state == TargetState::Live || runtimeTransportIsRunning(runtime);
}

static QString youtubeSelectionStateName(YouTubeBroadcastSelectionState state)
{
	switch (state) {
	case YouTubeBroadcastSelectionState::NoActiveBroadcast:
		return QStringLiteral("no-active-broadcast");
	case YouTubeBroadcastSelectionState::Selected:
		return QStringLiteral("selected");
	case YouTubeBroadcastSelectionState::MultipleActiveBroadcasts:
		return QStringLiteral("multiple-active-broadcasts");
	case YouTubeBroadcastSelectionState::NoStreamKeyMatch:
		return QStringLiteral("no-stream-key-match");
	case YouTubeBroadcastSelectionState::MultipleStreamKeyMatches:
		return QStringLiteral("multiple-stream-key-matches");
	case YouTubeBroadcastSelectionState::PreferredBroadcastUnavailable:
		return QStringLiteral("preferred-broadcast-unavailable");
	}
	return QStringLiteral("unknown");
}

static QString broadcastIdForLog(const QString &broadcastId)
{
	const QString cleanId = broadcastId.trimmed();
	return cleanId.isEmpty() ? QStringLiteral("none") : QStringLiteral("...%1").arg(cleanId.right(6));
}

static QString platformHttpError(const HttpResponse &response)
{
	if (!response.transportError.isEmpty())
		return response.transportError;
	if (response.statusCode >= 200 && response.statusCode < 300)
		return {};

	// Provider error bodies are untrusted and may echo OAuth credentials. Reuse
	// the bounded redaction path used by the login flow before the detail can be
	// shown in the UI or written to the OBS log.
	QString detail = stripHtml(oauthSafeErrorDetail(response.body));
	const QStringList reasons = youtubeApiErrorReasons(response);

	if (isQuotaExceededText(detail) ||
	    youtubeApiErrorHasAnyReason(response, {"quotaExceeded", "dailyLimitExceeded", "dailyLimitExceededUnreg"})) {
		return QStringLiteral(
			"YouTube API quota exceeded. RTMP is connected, but DSK cannot switch the YouTube broadcast from preparing to live until quota resets. Enable Auto-start in YouTube Studio or retry with a Google Cloud project that still has quota.");
	}
	if (!reasons.isEmpty()) {
		const QString reasonDetail = reasons.join(QStringLiteral(", "));
		detail = detail.isEmpty() ? reasonDetail : QStringLiteral("%1 [%2]").arg(detail, reasonDetail);
	}

	const QString status = response.statusCode > 0 ? QStringLiteral("HTTP %1").arg(response.statusCode)
						    : QStringLiteral("HTTP request failed");
	return detail.isEmpty() ? status : QStringLiteral("%1: %2").arg(status, detail);
}

static void dskOutputStopCallback(void *data, calldata_t *params)
{
	auto *manager = static_cast<OutputManager *>(data);
	if (!manager)
		return;

	auto *output = static_cast<obs_output_t *>(calldata_ptr(params, "output"));
	const quint64 sessionSerial = outputSessionSerial(output);
	const int code = static_cast<int>(calldata_int(params, "code"));
	const char *lastError = calldata_string(params, "last_error");
	const QString errorText = lastError ? QString::fromUtf8(lastError) : QString();

	QPointer<OutputManager> guard(manager);
	QMetaObject::invokeMethod(manager, [guard, output, sessionSerial, code, errorText]() {
		if (!guard)
			return;
		guard->handleOutputStopped(output, sessionSerial, code, errorText);
	}, Qt::QueuedConnection);
}

static void dispatchOutputSignal(OutputManager *manager, obs_output_t *output, const QString &signalName,
				 int reconnectDelaySeconds = 0)
{
	if (!manager || !output)
		return;

	const quint64 sessionSerial = outputSessionSerial(output);
	QPointer<OutputManager> guard(manager);
	QMetaObject::invokeMethod(manager, [guard, output, sessionSerial, signalName, reconnectDelaySeconds]() {
		if (!guard)
			return;
		guard->handleOutputSignal(output, sessionSerial, signalName, reconnectDelaySeconds);
	}, Qt::QueuedConnection);
}

static void dskOutputStartingCallback(void *data, calldata_t *params)
{
	dispatchOutputSignal(static_cast<OutputManager *>(data), static_cast<obs_output_t *>(calldata_ptr(params, "output")),
			     QStringLiteral("starting"));
}

static void dskOutputStartCallback(void *data, calldata_t *params)
{
	dispatchOutputSignal(static_cast<OutputManager *>(data), static_cast<obs_output_t *>(calldata_ptr(params, "output")),
			     QStringLiteral("start"));
}

static void dskOutputActivateCallback(void *data, calldata_t *params)
{
	dispatchOutputSignal(static_cast<OutputManager *>(data), static_cast<obs_output_t *>(calldata_ptr(params, "output")),
			     QStringLiteral("activate"));
}

static void dskOutputReconnectCallback(void *data, calldata_t *params)
{
	dispatchOutputSignal(static_cast<OutputManager *>(data), static_cast<obs_output_t *>(calldata_ptr(params, "output")),
			     QStringLiteral("reconnect"), static_cast<int>(calldata_int(params, "timeout_sec")));
}

static void dskOutputReconnectSuccessCallback(void *data, calldata_t *params)
{
	dispatchOutputSignal(static_cast<OutputManager *>(data), static_cast<obs_output_t *>(calldata_ptr(params, "output")),
			     QStringLiteral("reconnect_success"));
}

static void dskOutputStoppingCallback(void *data, calldata_t *params)
{
	dispatchOutputSignal(static_cast<OutputManager *>(data), static_cast<obs_output_t *>(calldata_ptr(params, "output")),
			     QStringLiteral("stopping"));
}

static void dskOutputDeactivateCallback(void *data, calldata_t *params)
{
	dispatchOutputSignal(static_cast<OutputManager *>(data), static_cast<obs_output_t *>(calldata_ptr(params, "output")),
			     QStringLiteral("deactivate"));
}

static void connectOutputSignals(obs_output_t *output, OutputManager *manager)
{
	if (!output || !manager)
		return;
	signal_handler_t *handler = obs_output_get_signal_handler(output);
	signal_handler_connect(handler, "starting", dskOutputStartingCallback, manager);
	signal_handler_connect(handler, "start", dskOutputStartCallback, manager);
	signal_handler_connect(handler, "activate", dskOutputActivateCallback, manager);
	signal_handler_connect(handler, "reconnect", dskOutputReconnectCallback, manager);
	signal_handler_connect(handler, "reconnect_success", dskOutputReconnectSuccessCallback, manager);
	signal_handler_connect(handler, "stopping", dskOutputStoppingCallback, manager);
	signal_handler_connect(handler, "deactivate", dskOutputDeactivateCallback, manager);
	signal_handler_connect(handler, "stop", dskOutputStopCallback, manager);
}

static void disconnectOutputSignals(obs_output_t *output, OutputManager *manager)
{
	if (!output || !manager)
		return;
	signal_handler_t *handler = obs_output_get_signal_handler(output);
	signal_handler_disconnect(handler, "starting", dskOutputStartingCallback, manager);
	signal_handler_disconnect(handler, "start", dskOutputStartCallback, manager);
	signal_handler_disconnect(handler, "activate", dskOutputActivateCallback, manager);
	signal_handler_disconnect(handler, "reconnect", dskOutputReconnectCallback, manager);
	signal_handler_disconnect(handler, "reconnect_success", dskOutputReconnectSuccessCallback, manager);
	signal_handler_disconnect(handler, "stopping", dskOutputStoppingCallback, manager);
	signal_handler_disconnect(handler, "deactivate", dskOutputDeactivateCallback, manager);
	signal_handler_disconnect(handler, "stop", dskOutputStopCallback, manager);
}

OutputManager::OutputManager(QObject *parent)
	: QObject(parent),
	  http_(new HttpClient(this))
{
	const qint64 archiveRotationIntervalMs = youtubeArchiveRotationIntervalMs();
	if (archiveRotationIntervalMs != YouTubeArchiveRotationIntervalMs) {
		logWarning(QStringLiteral("YouTube archive rotation development override enabled: %1 minute(s).")
				   .arg(archiveRotationIntervalMs / (60 * 1000)));
	}
	loadSettingsFromCurrentProfile();
}

void OutputManager::loadSettingsFromCurrentProfile()
{
	loadedSettingsPath_ = normalizedSettingsPath(store_.settingsPath());
	PluginSettings settings = store_.load();
	const QString settingsLoadWarning = store_.lastLoadWarning();
	if (!settingsLoadWarning.isEmpty()) {
		QTimer::singleShot(0, this, [this, settingsLoadWarning]() { emit statusMessage(settingsLoadWarning); });
	}
	targets_.reserve(settings.targets.size());
	for (const auto &target : settings.targets) {
		targets_.resize(targets_.size() + 1);
		copyTargetFields(targets_.last(), target);
	}
	bool migratedTargets = false;
	QSet<QString> loadedTargetIds;
	struct DuplicateCredentialMigration {
		QString newId;
		OutputTarget previous;
	};
	QVector<DuplicateCredentialMigration> duplicateCredentialMigrations;
	QVector<OutputTarget> publisherManagedCredentialMigrations;
	QStringList credentialMigrationWarnings;
	for (auto &target : targets_) {
		if (target.id.trimmed().isEmpty() || loadedTargetIds.contains(target.id)) {
			OutputTarget previous;
			copyTargetFields(previous, target);
			const QString previousId = target.id;
			do {
				target.id = newTargetId();
			} while (loadedTargetIds.contains(target.id));
			duplicateCredentialMigrations.push_back({target.id, previous});

			QString warning;
			if (!stageCredentialForReassignedTarget(target.streamKey,
							       target.authCredentialRef,
							       SecretStore::streamKeyCredentialRef(target.id),
							       &warning))
				credentialMigrationWarnings.push_back(warning);
			warning.clear();
			if (!stageCredentialForReassignedTarget(target.oauthClientSecret,
							       target.oauthClientSecretRef,
							       SecretStore::oauthClientSecretCredentialRef(target.id),
							       &warning))
				credentialMigrationWarnings.push_back(warning);
			warning.clear();
			if (!stageCredentialForReassignedTarget(target.oauthRefreshToken,
							       target.oauthRefreshTokenRef,
							       SecretStore::oauthRefreshTokenCredentialRef(target.id),
							       &warning))
				credentialMigrationWarnings.push_back(warning);
			logWarning(QStringLiteral("Reassigned duplicate or empty target id '%1' to '%2'.")
					   .arg(previousId, target.id));
			migratedTargets = true;
		}
		OutputTarget publisherManagedPrevious;
		copyTargetFields(publisherManagedPrevious, target);
		if (migratePublisherManagedOAuthCredentials(target)) {
			publisherManagedCredentialMigrations.push_back(std::move(publisherManagedPrevious));
			migratedTargets = true;
		}
		loadedTargetIds.insert(target.id);
		if (isYouTubeApiWarningText(target.lastError)) {
			target.lastError.clear();
			migratedTargets = true;
		}
		const QString cleanSceneName = target.sceneName.trimmed();
		const QString cleanSceneUuid = target.sceneUuid.trimmed();
		if (target.sceneName != cleanSceneName || target.sceneUuid != cleanSceneUuid)
			migratedTargets = true;
		target.sceneName = cleanSceneName;
		target.sceneUuid = cleanSceneUuid;

		QVector<TargetSceneRoute> cleanRoutes;
		cleanRoutes.reserve(target.sceneRoutes.size());
		QSet<QString> routeKeys;
		for (auto route : target.sceneRoutes) {
			const TargetSceneRoute original = route;
			route.obsSceneName = route.obsSceneName.trimmed();
			route.obsSceneUuid = route.obsSceneUuid.trimmed();
			route.outputSceneName = route.outputSceneName.trimmed();
			route.outputSceneUuid = route.outputSceneUuid.trimmed();
			if (route.obsSceneName != original.obsSceneName || route.obsSceneUuid != original.obsSceneUuid ||
			    route.outputSceneName != original.outputSceneName || route.outputSceneUuid != original.outputSceneUuid)
				migratedTargets = true;
			if (route.obsSceneName.isEmpty() || route.outputSceneName.isEmpty()) {
				migratedTargets = true;
				continue;
			}
			const QString routeKey = route.obsSceneUuid.isEmpty()
						 ? QStringLiteral("name:%1").arg(route.obsSceneName)
						 : QStringLiteral("uuid:%1").arg(route.obsSceneUuid);
			if (routeKeys.contains(routeKey)) {
				migratedTargets = true;
				continue;
			}
			routeKeys.insert(routeKey);
			cleanRoutes.push_back(std::move(route));
		}
		if (cleanRoutes.size() != target.sceneRoutes.size())
			migratedTargets = true;
		target.sceneRoutes = std::move(cleanRoutes);
		// Any legacy plaintext secret must be rewritten even when an older file
		// already contains a credential reference.
		if (!target.streamKey.isEmpty() || !target.oauthClientSecret.isEmpty() ||
		    !target.oauthRefreshToken.isEmpty())
			migratedTargets = true;
	}
	layouts_.initializeVerticalScenes(settings.verticalScenes, settings.activeVerticalSceneId, settings.verticalLayout);
	persistedVerticalLayout_ = layouts_.verticalLayout();
	persistedVerticalScenes_ = layouts_.verticalScenes();
	persistedActiveVerticalSceneId_ = layouts_.activeVerticalSceneId();
	followObsScene_ = settings.followObsScene;
	sceneLinks_ = std::move(settings.sceneLinks);
	bool migratedLinks = false;
	QSet<QString> sceneLinkKeys;
	for (int i = sceneLinks_.size() - 1; i >= 0; --i) {
		auto &link = sceneLinks_[i];
		const QString cleanSceneName = link.sceneName.trimmed();
		const QString cleanSceneUuid = link.sceneUuid.trimmed();
		const QString cleanVerticalSceneId = link.verticalSceneId.trimmed();
		const QString cleanLegacyTemplateId = link.legacyTemplateId.trimmed();
		if (link.sceneName != cleanSceneName || link.sceneUuid != cleanSceneUuid ||
		    link.verticalSceneId != cleanVerticalSceneId || link.legacyTemplateId != cleanLegacyTemplateId)
			migratedLinks = true;
		link.sceneName = cleanSceneName;
		link.sceneUuid = cleanSceneUuid;
		link.verticalSceneId = cleanVerticalSceneId;
		link.legacyTemplateId = cleanLegacyTemplateId;
		if (link.sceneName.isEmpty()) {
			sceneLinks_.removeAt(i);
			migratedLinks = true;
			continue;
		}
		if (link.verticalSceneId.isEmpty()) {
			if (link.legacyTemplateId.isEmpty()) {
				sceneLinks_.removeAt(i);
				migratedLinks = true;
				continue;
			} else {
				link.verticalSceneId = layouts_.activeVerticalSceneId();
				link.legacyTemplateId.clear();
			}
			migratedLinks = true;
		}
		const QString linkKey = link.sceneUuid.isEmpty()
					? QStringLiteral("name:%1").arg(link.sceneName)
					: QStringLiteral("uuid:%1").arg(link.sceneUuid);
		if (sceneLinkKeys.contains(linkKey)) {
			sceneLinks_.removeAt(i);
			migratedLinks = true;
			continue;
		}
		sceneLinkKeys.insert(linkKey);
	}
	if (migratedTargets || migratedLinks) {
		if (save()) {
			for (const auto &migration : duplicateCredentialMigrations) {
				if (OutputTarget *target = findTarget(migration.newId)) {
					if (migration.previous.streamKey.isEmpty())
						target->streamKey.clear();
					if (migration.previous.oauthClientSecret.isEmpty())
						target->oauthClientSecret.clear();
					if (migration.previous.oauthRefreshToken.isEmpty())
						target->oauthRefreshToken.clear();
				}
			}
			for (const auto &previous : publisherManagedCredentialMigrations) {
				const QStringList cleanupFailures = deleteUnreferencedTargetSecrets(previous, targets_);
				if (!cleanupFailures.isEmpty())
					credentialMigrationWarnings.push_back(
						QStringLiteral("Obsolete publisher OAuth credentials could not be removed: %1")
							.arg(cleanupFailures.join(QStringLiteral(", "))));
			}
		} else {
			for (const auto &migration : duplicateCredentialMigrations) {
				if (OutputTarget *target = findTarget(migration.newId)) {
					target->authCredentialRef = migration.previous.authCredentialRef;
					target->oauthClientSecretRef = migration.previous.oauthClientSecretRef;
					target->oauthRefreshTokenRef = migration.previous.oauthRefreshTokenRef;
					target->streamKey = migration.previous.streamKey;
					target->oauthClientSecret = migration.previous.oauthClientSecret;
					target->oauthRefreshToken = migration.previous.oauthRefreshToken;
				}
			}
			for (const auto &previous : publisherManagedCredentialMigrations) {
				if (OutputTarget *target = findTarget(previous.id)) {
					target->oauthClientId = previous.oauthClientId;
					target->oauthClientSecret = previous.oauthClientSecret;
					target->oauthClientSecretRef = previous.oauthClientSecretRef;
					target->oauthRefreshToken = previous.oauthRefreshToken;
					target->oauthRefreshTokenRef = previous.oauthRefreshTokenRef;
				}
			}
		}
	}
	if (!credentialMigrationWarnings.isEmpty()) {
		const QString warning = QStringLiteral("DSK migrated target credentials, but some items need attention: %1")
					.arg(credentialMigrationWarnings.join(QStringLiteral(" ")));
		logWarning(warning);
		QTimer::singleShot(0, this, [this, warning]() { emit statusMessage(warning); });
	}
}

OutputManager::~OutputManager()
{
	prepareForUnload();
}

void OutputManager::abortPlatformRequests()
{
	if (http_)
		http_->abortAll();
}

void OutputManager::releaseAllSharedEncoders()
{
	for (auto *set : sharedEncoders_) {
		if (set->videoEncoder)
			obs_encoder_release(set->videoEncoder);
		if (set->audioEncoder)
			obs_encoder_release(set->audioEncoder);
		delete set;
	}
	sharedEncoders_.clear();
}

void OutputManager::reloadForCurrentProfile()
{
	if (shuttingDown_ || unloadPrepared_)
		return;

	SettingsStore nextStore;
	const QString nextSettingsPath = normalizedSettingsPath(nextStore.settingsPath());
	if (nextSettingsPath.compare(loadedSettingsPath_, Qt::CaseInsensitive) == 0) {
		refreshSceneIdentities();
		emit targetsChanged();
		emit verticalLayoutChanged();
		return;
	}

	logInfo(QStringLiteral("Reloading DSK settings for the current OBS profile."));
	abortPlatformRequests();
	releaseAllSessionsNow();
	releaseObsSceneReferences();
	releaseAllSharedEncoders();

	targets_.clear();
	runtimeStatuses_.clear();
	runtimeTargetIds_.clear();
	pendingPersistentRemovalIds_.clear();
	pendingRuntimeRemovalIds_.clear();
	sceneLinks_.clear();
	layouts_ = LayoutManager();
	persistedVerticalLayout_ = VerticalLayout();
	persistedVerticalScenes_.clear();
	persistedActiveVerticalSceneId_.clear();
	followObsScene_ = false;
	suppressNextObsAutoStart_ = false;
	suppressNextObsAutoStop_ = false;
	suppressObsAutoStartUntilMs_ = 0;
	suppressObsAutoStopUntilMs_ = 0;
	store_ = std::move(nextStore);
	loadedSettingsPath_.clear();

	loadSettingsFromCurrentProfile();
	refreshSceneIdentities();
	emit targetsChanged();
	emit verticalLayoutChanged();
	emit statusMessage(QStringLiteral("Loaded DSK settings for the current OBS profile."));
}

void OutputManager::prepareForSceneCollectionChange()
{
	if (shuttingDown_ || unloadPrepared_)
		return;

	QVector<QString> targetIds;
	for (const Session *session : sessions_) {
		if (!session)
			continue;
		const OutputTarget *target = findTarget(session->targetId);
		if (target && (target->encoderGroup == EncoderGroup::DskVertical || targetUsesSceneCanvas(*target)))
			targetIds.push_back(target->id);
	}

	for (const QString &targetId : targetIds) {
		Session *session = sessionForTarget(targetId);
		if (!session)
			continue;
		const int index = sessions_.indexOf(session);
		if (index < 0)
			continue;

		if (session->output) {
			disconnectOutputSignals(session->output, this);
			if (obs_output_active(session->output))
				obs_output_force_stop(session->output);
			releaseOutputAndService(session->output, session->service);
			releaseSharedEncoders(session->sharedEncoderKey);
		}
		delete session;
		sessions_.removeAt(index);
		releaseTargetSceneCanvas(targetId);

		if (OutputTarget *target = findTarget(targetId)) {
			target->state = TargetState::Stopped;
			target->lastError.clear();
			resetRuntimeStatus(targetId);
		}
		finalizePendingRemoval(targetId);
		logInfo(QString("Stopped scene-dependent output %1 before OBS scene collection cleanup.").arg(targetId));
	}

	releaseObsSceneReferences();
	if (!targetIds.isEmpty()) {
		emit statusMessage(QStringLiteral("Stopped DSK vertical/separate-scene outputs before changing the OBS scene collection."));
		emit targetsChanged();
	}
}

void OutputManager::prepareForUnload()
{
	if (unloadPrepared_)
		return;
	unloadPrepared_ = true;
	shuttingDown_ = true;
	disconnect(this, nullptr, nullptr, nullptr);
	abortPlatformRequests();
	releaseAllSessionsNow();
	releaseObsSceneReferences();
	releaseAllSharedEncoders();
}

void OutputManager::releaseObsSceneReferences()
{
#ifdef DSK_ENABLE_OBS_CANVAS_API
	releaseAllSceneCanvases();
	if (!shuttingDown_ && sessionUsesCanvasKey(QStringLiteral("dsk-vertical"))) {
		logWarning(QStringLiteral("Deferred DSK Vertical canvas release while a vertical output is active."));
		return;
	}
	if (verticalCanvas_) {
		obs_canvas_set_channel(verticalCanvas_, 0, nullptr);
		verticalScene_.release();
		obs_canvas_release(verticalCanvas_);
		verticalCanvas_ = nullptr;
	}
#endif
	verticalScene_.release();
}

const QVector<OutputTarget> &OutputManager::targets() const
{
	return targets_;
}

const PlatformPresetRegistry &OutputManager::platforms() const
{
	return platforms_;
}

LayoutManager &OutputManager::layouts()
{
	return layouts_;
}

const LayoutManager &OutputManager::layouts() const
{
	return layouts_;
}

bool OutputManager::followObsScene() const
{
	return followObsScene_;
}

const QVector<SceneLayoutLink> &OutputManager::sceneLinks() const
{
	return sceneLinks_;
}

QStringList OutputManager::obsSceneNames() const
{
	QStringList names;
	obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; ++i) {
		obs_source_t *source = scenes.sources.array[i];
		const char *name = source ? obs_source_get_name(source) : nullptr;
		if (name && *name)
			names.push_back(QString::fromUtf8(name));
	}
	obs_frontend_source_list_free(&scenes);
	names.removeDuplicates();
	names.sort(Qt::CaseInsensitive);
	return names;
}

QString OutputManager::effectiveOutputSceneName(const OutputTarget &target) const
{
	if (target.sceneMode == TargetSceneMode::FixedScene)
		return resolvedObsSceneName(target.sceneUuid, target.sceneName);

	if (target.sceneMode == TargetSceneMode::LinkedScene) {
		const QString obsScene = currentObsSceneName();
		const QString obsSceneUuid = currentObsSceneUuid();
		for (const auto &route : target.sceneRoutes) {
			const bool matches = route.obsSceneUuid.trimmed().isEmpty()
						     ? route.obsSceneName.trimmed() == obsScene
						     : !obsSceneUuid.isEmpty() && route.obsSceneUuid.trimmed() == obsSceneUuid;
			if (matches)
				return resolvedObsSceneName(route.outputSceneUuid, route.outputSceneName);
		}
		return resolvedObsSceneName(target.sceneUuid, target.sceneName);
	}

	return {};
}

bool OutputManager::addTarget(const OutputTarget &target)
{
	OutputTarget copy;
	copyTargetFields(copy, target);
	if (!copy.sceneName.trimmed().isEmpty()) {
		const QString resolvedUuid = obsSceneUuidForName(copy.sceneName);
		if (!resolvedUuid.isEmpty())
			copy.sceneUuid = resolvedUuid;
		else
			copy.sceneUuid = copy.sceneUuid.trimmed();
	}
	for (auto &route : copy.sceneRoutes) {
		const QString obsUuid = obsSceneUuidForName(route.obsSceneName);
		const QString outputUuid = obsSceneUuidForName(route.outputSceneName);
		if (!obsUuid.isEmpty())
			route.obsSceneUuid = obsUuid;
		if (!outputUuid.isEmpty())
			route.outputSceneUuid = outputUuid;
	}
	if (copy.id.isEmpty() || findTarget(copy.id)) {
		do {
			copy.id = newTargetId();
		} while (findTarget(copy.id));
	}
	targets_.push_back(copy);
	if (!save()) {
		targets_.removeLast();
		emit statusMessage(QStringLiteral("Failed to add target. Settings were not changed."));
		emit targetsChanged();
		return false;
	}
	emit targetsChanged();
	return true;
}

bool OutputManager::mutateTargetForUi(const QString &id, const std::function<void(OutputTarget &)> &mutator)
{
	OutputTarget *target = findTarget(id);
	if (!target)
		return false;
	if (pendingPersistentRemovalIds_.contains(id) || pendingRuntimeRemovalIds_.contains(id)) {
		emit statusMessage(QStringLiteral("Target removal is pending."));
		return false;
	}
	const TargetRuntimeStatus runtime = runtimeStatusForTarget(id);
	if (sessionForTarget(id) || runtimeHasSession(runtime) || runtimeTransportIsBusy(runtime)) {
		emit statusMessage(QStringLiteral("Stop this target before editing its settings."));
		emit targetsChanged();
		return false;
	}

	OutputTarget previous;
	copyTargetFields(previous, *target);
	logInfo(QString("Applying target UI edit: %1").arg(id));
	mutator(*target);
	target->id = previous.id;
	target->state = previous.state;
	target->lastError = previous.lastError;
	if (target->sceneName.trimmed() != previous.sceneName.trimmed() || target->sceneUuid.trimmed().isEmpty())
		target->sceneUuid = obsSceneUuidForName(target->sceneName);
	if (!save()) {
		copyTargetFields(*target, previous);
		emit targetsChanged();
		logWarning(QString("Target UI edit could not be saved: %1").arg(id));
		return false;
	}
	const QStringList cleanupFailures = deleteUnreferencedTargetSecrets(previous, targets_);
	if (!cleanupFailures.isEmpty())
		emit statusMessage(QStringLiteral("Target was saved, but obsolete credentials need cleanup: %1")
					   .arg(cleanupFailures.join(QStringLiteral(", "))));

	logInfo(QString("Target UI edit saved: %1").arg(id));
	emit targetsChanged();
	return true;
}

void OutputManager::removeTarget(const QString &id)
{
	if (!findTarget(id) || pendingPersistentRemovalIds_.contains(id))
		return;
	if (runtimeTargetIds_.contains(id)) {
		removeRuntimeTarget(id);
		return;
	}

	if (sessionForTarget(id)) {
		pendingPersistentRemovalIds_.insert(id);
		emit statusMessage(QStringLiteral("Stopping target before removal."));
		stopTarget(id);
		return;
	}
	finalizeTargetRemoval(id, false);
}

void OutputManager::setTargetEnabled(const QString &id, bool enabled)
{
	OutputTarget *target = findTarget(id);
	if (!target || target->enabled == enabled)
		return;

	const bool previousEnabled = target->enabled;
	target->enabled = enabled;
	if (!runtimeTargetIds_.contains(id) && !save()) {
		target->enabled = previousEnabled;
		emit targetsChanged();
		return;
	}
	// This flag controls future bulk/automatic starts. Stopping or starting a
	// live route remains an explicit action in Stream Controls.
	emit targetsChanged();
}

QString OutputManager::addRuntimeTarget(const OutputTarget &target)
{
	OutputTarget copy;
	copyTargetFields(copy, target, false);
	if (copy.id.isEmpty() || findTarget(copy.id)) {
		do {
			copy.id = newTargetId();
		} while (findTarget(copy.id));
	}
	const QString id = copy.id;
	targets_.push_back(std::move(copy));
	runtimeTargetIds_.insert(id);
	logInfo(QString("Runtime target added: %1").arg(id));
	return id;
}

void OutputManager::removeRuntimeTarget(const QString &id)
{
	if (!runtimeTargetIds_.contains(id) || pendingRuntimeRemovalIds_.contains(id))
		return;
	if (sessionForTarget(id)) {
		pendingRuntimeRemovalIds_.insert(id);
		stopTarget(id);
		return;
	}
	finalizeTargetRemoval(id, true);
}

bool OutputManager::finalizePendingRemoval(const QString &id)
{
	if (pendingRuntimeRemovalIds_.contains(id))
		return finalizeTargetRemoval(id, true);
	if (pendingPersistentRemovalIds_.contains(id))
		return finalizeTargetRemoval(id, false);
	return false;
}

bool OutputManager::finalizeTargetRemoval(const QString &id, bool runtimeTarget)
{
	if (sessionForTarget(id))
		return false;

	int index = -1;
	for (int i = 0; i < targets_.size(); ++i) {
		if (targets_[i].id == id) {
			index = i;
			break;
		}
	}
	if (index < 0) {
		pendingPersistentRemovalIds_.remove(id);
		pendingRuntimeRemovalIds_.remove(id);
		return false;
	}

	OutputTarget removed;
	copyTargetFields(removed, targets_[index]);
	targets_.removeAt(index);
	if (!runtimeTarget && !save()) {
		targets_.insert(index, OutputTarget{});
		copyTargetFields(targets_[index], removed);
		pendingPersistentRemovalIds_.remove(id);
		emit statusMessage(QStringLiteral("Failed to remove target. Settings were not changed."));
		emit targetsChanged();
		return false;
	}

	pendingPersistentRemovalIds_.remove(id);
	pendingRuntimeRemovalIds_.remove(id);
	runtimeTargetIds_.remove(id);
	runtimeStatuses_.remove(id);
	releaseTargetSceneCanvas(id);
	if (!runtimeTarget) {
		const QStringList cleanupFailures = deleteUnreferencedTargetSecrets(removed, targets_);
		if (!cleanupFailures.isEmpty())
			emit statusMessage(QStringLiteral("Target was removed, but obsolete credentials need cleanup: %1")
						   .arg(cleanupFailures.join(QStringLiteral(", "))));
	}
	emit targetsChanged();
	return true;
}

bool OutputManager::startTarget(const QString &id)
{
	OutputTarget *target = findTarget(id);
	if (!target)
		return false;
	const TargetRuntimeStatus runtime = runtimeStatusForTarget(id);
	if (runtimeTransportIsRunning(runtime) || target->state == TargetState::Live || target->state == TargetState::Starting)
		return true;
	if (runtime.transport == TransportState::Stopping || sessionForTarget(id)) {
		target->state = TargetState::Stopping;
		target->lastError = QStringLiteral("Output is still stopping. Try again in a moment.");
		setRuntimeTransport(id, runtimeStatusForTarget(id).sessionSerial, TransportState::Stopping, target->lastError);
		logWarning(QString("%1: %2").arg(target->name, target->lastError));
		if (!runtimeTargetIds_.contains(id))
			emit targetsChanged();
		return false;
	}

	target->state = TargetState::Starting;
	target->lastError.clear();
	resetRuntimeStatus(id);
	ensureRuntimeStatus(id).platform = isYouTubeTarget(*target) ? PlatformLiveState::Unknown : PlatformLiveState::NotApplicable;
	setRuntimeTransport(id, 0, TransportState::Starting, QStringLiteral("Connecting"));
	if (!runtimeTargetIds_.contains(id))
		emit targetsChanged();

	if (!hydrateTargetSecrets(*target)) {
		if (!runtimeTargetIds_.contains(id))
			emit targetsChanged();
		return false;
	}
	if (!validateTarget(*target)) {
		if (!runtimeTargetIds_.contains(id))
			emit targetsChanged();
		return false;
	}

	const bool ok = isYouTubeTarget(*target) && target->authMode == TargetAuthMode::YouTubeOAuth
				? beginYouTubeStartPreflight(*target)
				: startIndependentTarget(*target);

	if (!runtimeTargetIds_.contains(id))
		emit targetsChanged();
	return ok;
}

void OutputManager::stopTarget(const QString &id)
{
	if (OutputTarget *target = findTarget(id)) {
		const TargetRuntimeStatus runtime = runtimeStatusForTarget(id);
		if (runtime.transport == TransportState::Idle && !sessionForTarget(id) && target->state == TargetState::Stopped) {
			return;
		}
		target->state = TargetState::Stopping;
		setRuntimeTransport(id, runtimeStatusForTarget(id).sessionSerial, TransportState::Stopping, QStringLiteral("Stopping"));
		if (!runtimeTargetIds_.contains(id))
			emit targetsChanged();
		releaseSession(id, true);
		if (sessionForTarget(id)) {
			target->lastError.clear();
			logInfo(QString("Stopping %1").arg(target->name));
			if (!runtimeTargetIds_.contains(id))
				emit targetsChanged();
			return;
		}
		target->state = TargetState::Stopped;
		target->lastError.clear();
		resetRuntimeStatus(id);
		if (!runtimeTargetIds_.contains(id))
			save();
		logInfo(QString("Stopped %1").arg(target->name));
		if (!runtimeTargetIds_.contains(id))
			emit targetsChanged();
	}
}

void OutputManager::startAll()
{
	const QVector<QString> ids = startAllTargetIds(targets_);
	int started = 0;
	int failed = 0;
	for (const auto &id : ids) {
		if (startTarget(id))
			++started;
		else
			++failed;
	}
	if (failed > 0)
		emit statusMessage(QStringLiteral("Start All: %1 started, %2 failed.").arg(started).arg(failed));
}

void OutputManager::stopAll()
{
	QVector<QString> ids;
	ids.reserve(targets_.size());
	for (const auto &target : targets_)
		ids.push_back(target.id);
	for (const auto &id : ids)
		stopTarget(id);
}

void OutputManager::suppressNextObsAutoStart()
{
	suppressNextObsAutoStart_ = true;
	suppressObsAutoStartUntilMs_ = QDateTime::currentMSecsSinceEpoch() + 60000;
}

void OutputManager::suppressNextObsAutoStop()
{
	suppressNextObsAutoStop_ = true;
	suppressObsAutoStopUntilMs_ = QDateTime::currentMSecsSinceEpoch() + 5000;
}

void OutputManager::clearNextObsAutoStartSuppression()
{
	suppressNextObsAutoStart_ = false;
	suppressObsAutoStartUntilMs_ = 0;
}

void OutputManager::clearNextObsAutoStopSuppression()
{
	suppressNextObsAutoStop_ = false;
	suppressObsAutoStopUntilMs_ = 0;
}

bool OutputManager::shouldSuppressObsAutoStart()
{
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	const bool suppress = suppressNextObsAutoStart_ || (suppressObsAutoStartUntilMs_ > now);
	clearNextObsAutoStartSuppression();
	return suppress;
}

bool OutputManager::shouldSuppressObsAutoStop()
{
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	const bool suppress = suppressNextObsAutoStop_ || (suppressObsAutoStopUntilMs_ > now);
	clearNextObsAutoStopSuppression();
	return suppress;
}

void OutputManager::handleObsStreamingStarted()
{
	if (shouldSuppressObsAutoStart()) {
		logInfo("OBS streaming started by DSK individual control; skipping auto-start targets.");
		return;
	}

	QVector<QString> ids;
	ids.reserve(targets_.size());
	for (const auto &target : targets_) {
		if (target.enabled && target.autoStartWithObs)
			ids.push_back(target.id);
	}
	for (const auto &id : ids)
		startTarget(id);
}

void OutputManager::handleObsStreamingStopped()
{
	if (shouldSuppressObsAutoStop()) {
		logInfo("OBS streaming stopped by DSK individual control; skipping auto-stop targets.");
		return;
	}

	QVector<QString> ids;
	ids.reserve(targets_.size());
	for (const auto &target : targets_) {
		if (target.autoStopWithObs)
			ids.push_back(target.id);
	}
	for (const auto &id : ids)
		stopTarget(id);
}

void OutputManager::handleObsSceneChanged()
{
	bool applied = false;
	if (followObsScene_)
		applied = applyLinkedScene(currentObsSceneName());
	refreshLinkedSceneCanvases();
	if (!applied)
		emit verticalLayoutChanged();
}

void OutputManager::refreshSceneIdentities()
{
	QVector<OutputTarget> previousTargets;
	previousTargets.reserve(targets_.size());
	for (const auto &target : targets_) {
		previousTargets.resize(previousTargets.size() + 1);
		copyTargetFields(previousTargets.last(), target);
	}
	const QVector<SceneLayoutLink> previousLinks = sceneLinks_;
	bool changed = false;

	const auto syncSceneIdentity = [this, &changed](QString &name, QString &uuid) {
		const QString cleanName = name.trimmed();
		const QString cleanUuid = uuid.trimmed();
		if (name != cleanName || uuid != cleanUuid)
			changed = true;
		name = cleanName;
		uuid = cleanUuid;
		if (name.isEmpty()) {
			if (!uuid.isEmpty()) {
				const QString resolvedName = resolvedObsSceneName(uuid, {});
				if (!resolvedName.isEmpty()) {
					name = resolvedName;
					changed = true;
				}
			}
			return;
		}
		if (uuid.isEmpty()) {
			const QString resolvedUuid = obsSceneUuidForName(name);
			if (!resolvedUuid.isEmpty()) {
				uuid = resolvedUuid;
				changed = true;
			}
			return;
		}

		const QString resolvedName = resolvedObsSceneName(uuid, {});
		if (!resolvedName.isEmpty() && resolvedName != name) {
			name = resolvedName;
			changed = true;
		}
	};

	for (auto &target : targets_) {
		syncSceneIdentity(target.sceneName, target.sceneUuid);
		for (auto &route : target.sceneRoutes) {
			syncSceneIdentity(route.obsSceneName, route.obsSceneUuid);
			syncSceneIdentity(route.outputSceneName, route.outputSceneUuid);
		}
	}
	for (auto &link : sceneLinks_)
		syncSceneIdentity(link.sceneName, link.sceneUuid);

	if (!changed)
		return;
	if (!save()) {
		targets_.clear();
		targets_.reserve(previousTargets.size());
		for (const auto &target : previousTargets) {
			targets_.resize(targets_.size() + 1);
			copyTargetFields(targets_.last(), target);
		}
		sceneLinks_ = previousLinks;
		emit statusMessage(QStringLiteral("Failed to migrate OBS scene identities. Existing scene settings were kept."));
		return;
	}

	emit targetsChanged();
	emit verticalLayoutChanged();
}

void OutputManager::setFollowObsScene(bool follow)
{
	if (followObsScene_ == follow)
		return;
	const bool previous = followObsScene_;
	followObsScene_ = follow;
	if (!save()) {
		followObsScene_ = previous;
		emit statusMessage(QStringLiteral("Failed to save OBS scene link setting."));
		return;
	}
	if (followObsScene_)
		handleObsSceneChanged();
}

void OutputManager::upsertSceneLink(const SceneLayoutLink &link)
{
	SceneLayoutLink clean = link;
	clean.sceneName = clean.sceneName.trimmed();
	const QString resolvedUuid = obsSceneUuidForName(clean.sceneName);
	if (!resolvedUuid.isEmpty())
		clean.sceneUuid = resolvedUuid;
	else
		clean.sceneUuid = clean.sceneUuid.trimmed();
	if (clean.sceneName.isEmpty() || (clean.verticalSceneId.isEmpty() && clean.legacyTemplateId.isEmpty()))
		return;
	const QVector<SceneLayoutLink> previous = sceneLinks_;

	for (auto &existing : sceneLinks_) {
		const bool matches = !clean.sceneUuid.isEmpty() && !existing.sceneUuid.trimmed().isEmpty()
					     ? existing.sceneUuid.trimmed() == clean.sceneUuid
					     : existing.sceneName.trimmed() == clean.sceneName;
		if (matches) {
			existing.sceneName = clean.sceneName;
			existing.sceneUuid = clean.sceneUuid;
			existing.verticalSceneId = clean.verticalSceneId;
			existing.legacyTemplateId = clean.legacyTemplateId;
			if (!save()) {
				sceneLinks_ = previous;
				emit statusMessage(QStringLiteral("Failed to save vertical scene link."));
			}
			return;
		}
	}
	sceneLinks_.push_back(clean);
	if (!save()) {
		sceneLinks_ = previous;
		emit statusMessage(QStringLiteral("Failed to save vertical scene link."));
	}
}

void OutputManager::removeSceneLink(const QString &sceneName, const QString &sceneUuid)
{
	const QString cleanName = sceneName.trimmed();
	const QString cleanUuid = sceneUuid.trimmed().isEmpty() ? obsSceneUuidForName(cleanName) : sceneUuid.trimmed();
	for (int i = 0; i < sceneLinks_.size(); ++i) {
		const bool matches = !cleanUuid.isEmpty() && !sceneLinks_[i].sceneUuid.trimmed().isEmpty()
					     ? sceneLinks_[i].sceneUuid.trimmed() == cleanUuid
					     : sceneLinks_[i].sceneName.trimmed() == cleanName;
		if (matches) {
			const QVector<SceneLayoutLink> previous = sceneLinks_;
			sceneLinks_.removeAt(i);
			if (!save()) {
				sceneLinks_ = previous;
				emit statusMessage(QStringLiteral("Failed to remove vertical scene link."));
			}
			return;
		}
	}
}

bool OutputManager::setTargetSceneMode(const QString &id, TargetSceneMode mode, const QString &fallbackSceneName)
{
	OutputTarget *target = findTarget(id);
	if (!target)
		return false;
	if (sessionForTarget(id)) {
		emit statusMessage(QStringLiteral("Stop this target before changing its output scene mode."));
		emit targetsChanged();
		return false;
	}

	const TargetSceneMode previousMode = target->sceneMode;
	const QString previousName = target->sceneName;
	const QString previousUuid = target->sceneUuid;
	target->sceneMode = mode;
	target->sceneName = fallbackSceneName.trimmed();
	const QString resolvedUuid = obsSceneUuidForName(target->sceneName);
	target->sceneUuid = !resolvedUuid.isEmpty() ? resolvedUuid
						 : target->sceneName == previousName ? previousUuid : QString();

	const bool saved = save();
	if (!saved) {
		target->sceneMode = previousMode;
		target->sceneName = previousName;
		target->sceneUuid = previousUuid;
		emit statusMessage(QStringLiteral("Failed to save output scene settings."));
	} else if (target->sceneMode == TargetSceneMode::FollowObs) {
		releaseTargetSceneCanvas(id);
	}
	refreshLinkedSceneCanvases();
	emit targetsChanged();
	return saved;
}

bool OutputManager::upsertTargetSceneRoute(const QString &id, const QString &obsSceneName, const QString &outputSceneName)
{
	const QString cleanObsScene = obsSceneName.trimmed();
	const QString cleanOutputScene = outputSceneName.trimmed();
	const QString cleanObsSceneUuid = obsSceneUuidForName(cleanObsScene);
	const QString cleanOutputSceneUuid = obsSceneUuidForName(cleanOutputScene);
	if (cleanObsScene.isEmpty())
		return false;

	OutputTarget *target = findTarget(id);
	if (!target)
		return false;
	if (sessionForTarget(id)) {
		emit statusMessage(QStringLiteral("Stop this target before changing its scene routes."));
		emit targetsChanged();
		return false;
	}
	const QVector<TargetSceneRoute> previous = target->sceneRoutes;

	for (int i = target->sceneRoutes.size() - 1; i >= 0; --i) {
		if (target->sceneRoutes[i].obsSceneName.trimmed().isEmpty() ||
		    target->sceneRoutes[i].outputSceneName.trimmed().isEmpty())
			target->sceneRoutes.removeAt(i);
	}
	for (int i = 0; i < target->sceneRoutes.size(); ++i) {
		auto &route = target->sceneRoutes[i];
		const bool matches = !cleanObsSceneUuid.isEmpty() && !route.obsSceneUuid.trimmed().isEmpty()
					     ? route.obsSceneUuid.trimmed() == cleanObsSceneUuid
					     : route.obsSceneName.trimmed() == cleanObsScene;
		if (!matches)
			continue;
		if (cleanOutputScene.isEmpty()) {
			target->sceneRoutes.removeAt(i);
		} else {
			route.obsSceneName = cleanObsScene;
			route.obsSceneUuid = cleanObsSceneUuid;
			route.outputSceneName = cleanOutputScene;
			route.outputSceneUuid = cleanOutputSceneUuid;
		}
		const bool saved = save();
		if (!saved) {
			target->sceneRoutes = previous;
			emit statusMessage(QStringLiteral("Failed to save output scene route."));
		}
		refreshLinkedSceneCanvases();
		emit targetsChanged();
		return saved;
	}

	if (!cleanOutputScene.isEmpty())
		target->sceneRoutes.push_back({cleanObsScene, cleanObsSceneUuid, cleanOutputScene, cleanOutputSceneUuid});

	const bool saved = save();
	if (!saved) {
		target->sceneRoutes = previous;
		emit statusMessage(QStringLiteral("Failed to save output scene route."));
	}
	refreshLinkedSceneCanvases();
	emit targetsChanged();
	return saved;
}

OutputStats OutputManager::statsForTarget(const QString &id) const
{
	OutputStats stats;
	for (const auto *session : sessions_) {
		if (!session || session->targetId != id || !session->output)
			continue;

		stats.active = obs_output_active(session->output);
		stats.durationMs = session->startedAtMs > 0 ? QDateTime::currentMSecsSinceEpoch() - session->startedAtMs : 0;
		stats.totalBytes = obs_output_get_total_bytes(session->output);
		stats.totalFrames = obs_output_get_total_frames(session->output);
		stats.droppedFrames = obs_output_get_frames_dropped(session->output);
		stats.congestion = obs_output_get_congestion(session->output);
		break;
	}
	return stats;
}

TargetRuntimeStatus OutputManager::runtimeStatusForTarget(const QString &id) const
{
	TargetRuntimeStatus status = runtimeStatuses_.value(id);
	if (status.targetId.isEmpty())
		status.targetId = id;
	return status;
}

bool OutputManager::sessionMatches(const QString &targetId, quint64 sessionSerial) const
{
	if (sessionSerial == 0)
		return true;
	const Session *session = sessionForTarget(targetId);
	return session && session->serial == sessionSerial;
}

TargetRuntimeStatus &OutputManager::ensureRuntimeStatus(const QString &targetId)
{
	TargetRuntimeStatus &status = runtimeStatuses_[targetId];
	if (status.targetId.isEmpty())
		status.targetId = targetId;
	return status;
}

void OutputManager::resetRuntimeStatus(const QString &targetId)
{
	TargetRuntimeStatus &status = ensureRuntimeStatus(targetId);
	status = {};
	status.targetId = targetId;
	status.lastChangedAtMs = QDateTime::currentMSecsSinceEpoch();
	emit targetRuntimeChanged(targetId);
}

void OutputManager::updateRuntimeStats(const QString &targetId)
{
	TargetRuntimeStatus &status = ensureRuntimeStatus(targetId);
	const OutputStats stats = statsForTarget(targetId);
	status.totalBytes = stats.totalBytes;
	status.totalFrames = stats.totalFrames;
	status.droppedFrames = stats.droppedFrames;
	status.congestion = stats.congestion;
}

void OutputManager::setRuntimeTransport(const QString &targetId, quint64 sessionSerial, TransportState state,
					const QString &message, int reconnectDelaySeconds)
{
	if (!sessionMatches(targetId, sessionSerial))
		return;
	TargetRuntimeStatus &status = ensureRuntimeStatus(targetId);
	status.sessionSerial = sessionSerial;
	status.transport = state;
	status.reconnectDelaySeconds = reconnectDelaySeconds;
	status.lastChangedAtMs = QDateTime::currentMSecsSinceEpoch();
	if (!message.trimmed().isEmpty()) {
		status.transportMessage = message.trimmed();
		status.lastUserMessage = message.trimmed();
	}
	updateRuntimeStats(targetId);
	emit targetRuntimeChanged(targetId);
}

void OutputManager::setRuntimePlatform(const QString &targetId, quint64 sessionSerial, PlatformLiveState state,
				       const QString &message, const QString &technicalError)
{
	if (!sessionMatches(targetId, sessionSerial))
		return;
	TargetRuntimeStatus &status = ensureRuntimeStatus(targetId);
	status.sessionSerial = sessionSerial;
	status.platform = state;
	status.lastChangedAtMs = QDateTime::currentMSecsSinceEpoch();
	if (!message.trimmed().isEmpty()) {
		status.platformMessage = message.trimmed();
		status.lastUserMessage = message.trimmed();
	}
	if (!technicalError.trimmed().isEmpty())
		status.lastTechnicalError = technicalError.trimmed();
	updateRuntimeStats(targetId);
	emit targetRuntimeChanged(targetId);
}

void OutputManager::notifyCommentViewerYouTubeStarted(const QString &targetId, quint64 sessionSerial,
						      const QString &broadcastId)
{
	const OutputTarget *target = findTarget(targetId);
	if (!target || !isYouTubeTarget(*target) || !sessionMatches(targetId, sessionSerial) || !http_)
		return;

	const QString eventId =
		QStringLiteral("youtube:%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
	const QByteArray body = commentViewerYouTubeLiveStartPayload(eventId, broadcastId);
	if (body.isEmpty())
		return;

	HttpRequest request;
	request.url = commentViewerYouTubeLiveStartUrl();
	request.method = QByteArrayLiteral("POST");
	request.headers.push_back({QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json")});
	request.body = body;
	request.timeoutMs = 3000;
	request.maxResponseBytes = 64 * 1024;
	http_->send(std::move(request), [this](HttpResponse response) {
		if (response.isSuccess())
			return;
		if (response.statusCode != 404 && response.statusCode != 405) {
			logInfo(QStringLiteral("Comment Viewer was not available for the YouTube start notification."));
			return;
		}

		// Older Viewer builds only expose the manual recheck endpoint. Keep
		// that path as a compatibility fallback while v2 rolls out.
		HttpRequest fallback;
		fallback.url = commentViewerYouTubeRecheckUrl();
		fallback.method = QByteArrayLiteral("POST");
		fallback.timeoutMs = 3000;
		fallback.maxResponseBytes = 64 * 1024;
		http_->send(std::move(fallback), [](HttpResponse fallbackResponse) {
			if (!fallbackResponse.isSuccess())
				logInfo(QStringLiteral("Comment Viewer was not available for the YouTube start fallback."));
		});
	});
}

bool OutputManager::save()
{
	const QString currentSettingsPath = normalizedSettingsPath(store_.settingsPath());
	if (!loadedSettingsPath_.isEmpty() &&
	    currentSettingsPath.compare(loadedSettingsPath_, Qt::CaseInsensitive) != 0) {
		const QString error = QStringLiteral("OBS profile changed before DSK settings were reloaded. The save was blocked to protect both profiles.");
		logError(error);
		emit statusMessage(error);
		return false;
	}

	QVector<OutputTarget> persistentTargets;
	persistentTargets.reserve(targets_.size() - runtimeTargetIds_.size());
	for (const auto &target : targets_) {
		if (runtimeTargetIds_.contains(target.id))
			continue;
		persistentTargets.resize(persistentTargets.size() + 1);
		copyTargetFields(persistentTargets.last(), target);
		ensureCredentialRefs(persistentTargets.last());
	}

	QString error;
	if (!store_.save(persistentTargets,
			 layouts_.verticalLayout(),
			 layouts_.verticalScenes(),
			 layouts_.activeVerticalSceneId(),
			 followObsScene_,
			 sceneLinks_,
			 &error)) {
		logError(QString("Failed to save settings: %1").arg(error));
		emit statusMessage(QString("Failed to save DSK settings: %1").arg(error));
		return false;
	}

	for (const auto &saved : persistentTargets) {
		OutputTarget *target = findTarget(saved.id);
		if (!target)
			continue;
		target->authCredentialRef = saved.authCredentialRef;
		target->oauthClientSecretRef = saved.oauthClientSecretRef;
		target->oauthRefreshTokenRef = saved.oauthRefreshTokenRef;
	}
	persistedVerticalLayout_ = layouts_.verticalLayout();
	persistedVerticalScenes_ = layouts_.verticalScenes();
	persistedActiveVerticalSceneId_ = layouts_.activeVerticalSceneId();
	return true;
}

bool OutputManager::saveVerticalLayout()
{
	if (!save()) {
		layouts_.initializeVerticalScenes(persistedVerticalScenes_,
					  persistedActiveVerticalSceneId_,
					  persistedVerticalLayout_);
		QString refreshError;
		if (!refreshVerticalCanvasScene(&refreshError) && !refreshError.isEmpty())
			logWarning(QStringLiteral("Failed to restore the saved DSK Vertical scene: %1").arg(refreshError));
		emit verticalLayoutChanged();
		return false;
	}

	QString refreshError;
	if (!refreshVerticalCanvasScene(&refreshError) && !refreshError.isEmpty()) {
		logWarning(QStringLiteral("Saved DSK Vertical layout but could not refresh its live canvas: %1").arg(refreshError));
		emit statusMessage(QStringLiteral("Vertical layout saved, but live preview refresh failed: %1").arg(refreshError));
	}
	emit verticalLayoutChanged();
	return true;
}

QString OutputManager::createVerticalScene(const QString &name)
{
	const QString id = layouts_.createVerticalScene(name);
	if (id.isEmpty() || !saveVerticalLayout())
		return {};
	return id;
}

bool OutputManager::removeVerticalScene(const QString &id)
{
	const QVector<SceneLayoutLink> previousLinks = sceneLinks_;
	if (!layouts_.removeVerticalScene(id))
		return false;
	for (int i = sceneLinks_.size() - 1; i >= 0; --i) {
		if (sceneLinks_[i].verticalSceneId == id)
			sceneLinks_.removeAt(i);
	}
	if (saveVerticalLayout())
		return true;
	sceneLinks_ = previousLinks;
	emit verticalLayoutChanged();
	return false;
}

bool OutputManager::renameVerticalScene(const QString &id, const QString &name)
{
	return layouts_.renameVerticalScene(id, name) && saveVerticalLayout();
}

bool OutputManager::moveVerticalScene(const QString &id, int offset)
{
	return layouts_.moveVerticalScene(id, offset) && saveVerticalLayout();
}

bool OutputManager::reorderVerticalScenes(const QVector<QString> &orderedIds)
{
	return layouts_.reorderVerticalScenes(orderedIds) && saveVerticalLayout();
}

EncoderProfile OutputManager::effectiveProfileForTarget(const OutputTarget &target) const
{
	EncoderProfile profile = encoders_.profileFor(target.encoderGroup);
	if (target.videoBitrateKbps > 0)
		profile.videoBitrateKbps = target.videoBitrateKbps;
	if (target.audioBitrateKbps > 0)
		profile.audioBitrateKbps = target.audioBitrateKbps;
	if (target.keyframeSeconds > 0)
		profile.keyframeSeconds = target.keyframeSeconds;
	if (!target.videoEncoderId.trimmed().isEmpty())
		profile.videoEncoderId = target.videoEncoderId.trimmed();
	else
		profile.videoEncoderId = defaultVideoEncoderId();
	if (!target.audioEncoderId.trimmed().isEmpty())
		profile.audioEncoderId = target.audioEncoderId.trimmed();
	return profile;
}

QString OutputManager::defaultVideoEncoderId() const
{
	char *profilePath = obs_frontend_get_current_profile_path();
	if (profilePath) {
		const QString basicIniPath = QDir(QString::fromUtf8(profilePath)).filePath(QStringLiteral("basic.ini"));
		bfree(profilePath);

		QSettings basic(basicIniPath, QSettings::IniFormat);
		const QString outputMode = basic.value(QStringLiteral("Output/Mode"), QStringLiteral("Simple")).toString();
		if (outputMode.compare(QStringLiteral("Advanced"), Qt::CaseInsensitive) == 0) {
			const QString advancedEncoder = basic.value(QStringLiteral("AdvOut/Encoder"), QStringLiteral("obs_x264")).toString();
			return obsSelectionToH264Encoder(advancedEncoder);
		}

		return obsSelectionToH264Encoder(
			basic.value(QStringLiteral("SimpleOutput/StreamEncoder"), QStringLiteral("x264")).toString());
	}

	return firstAvailableVideoEncoder({"obs_nvenc_h264_tex", "ffmpeg_nvenc", "h264_texture_amf", "obs_qsv11_v2", "obs_x264"});
}

QString OutputManager::sharedEncoderKey(const OutputTarget &target, const EncoderProfile &profile) const
{
	return QString("%1|%2|%3x%4|v%5|a%6|kf%7|%8|%9")
		.arg(encoderGroupToString(profile.group))
		.arg(sceneCanvasKeyForTarget(target))
		.arg(profile.width)
		.arg(profile.height)
		.arg(profile.videoBitrateKbps)
		.arg(profile.audioBitrateKbps)
		.arg(profile.keyframeSeconds)
		.arg(profile.videoEncoderId.isEmpty() ? QStringLiteral("obs_x264") : profile.videoEncoderId)
		.arg(profile.audioEncoderId.isEmpty() ? QStringLiteral("ffmpeg_aac") : profile.audioEncoderId);
}

OutputManager::SharedEncoderSet *OutputManager::acquireSharedEncoders(const OutputTarget &target, const EncoderProfile &profile, video_t *video, QString *errorMessage)
{
	const QString key = sharedEncoderKey(target, profile);
	if (auto *existing = sharedEncoders_.value(key, nullptr)) {
		++existing->refs;
		return existing;
	}

	auto *set = new SharedEncoderSet;
	set->key = key;
	set->videoEncoder = createVideoEncoder(target, profile);
	if (!set->videoEncoder) {
		if (errorMessage)
			*errorMessage = "Failed to create shared video encoder.";
		delete set;
		return nullptr;
	}
	set->audioEncoder = createAudioEncoder(target, profile);
	if (!set->audioEncoder) {
		obs_encoder_release(set->videoEncoder);
		if (errorMessage)
			*errorMessage = "Failed to create shared audio encoder.";
		delete set;
		return nullptr;
	}

	obs_encoder_set_video(set->videoEncoder, video);
	obs_encoder_set_audio(set->audioEncoder, obs_get_audio());
	set->refs = 1;
	sharedEncoders_.insert(key, set);
	return set;
}

void OutputManager::releaseSharedEncoders(const QString &key)
{
	if (key.isEmpty())
		return;
	auto *set = sharedEncoders_.value(key, nullptr);
	if (!set)
		return;
	--set->refs;
	if (set->refs > 0)
		return;

	sharedEncoders_.remove(key);
	if (set->videoEncoder)
		obs_encoder_release(set->videoEncoder);
	if (set->audioEncoder)
		obs_encoder_release(set->audioEncoder);
	delete set;
}

OutputTarget *OutputManager::findTarget(const QString &id)
{
	for (auto &target : targets_) {
		if (target.id == id)
			return &target;
	}
	return nullptr;
}

const OutputTarget *OutputManager::findTarget(const QString &id) const
{
	for (const auto &target : targets_) {
		if (target.id == id)
			return &target;
	}
	return nullptr;
}

bool OutputManager::validateTarget(OutputTarget &target)
{
	QString error;
	if (!validateOutputTargetConfig(target, &error, false))
		return setTargetError(target, error);
	return true;
}

bool OutputManager::hydrateTargetSecrets(OutputTarget &target)
{
	SecretStore secrets;
	QString error;
	const bool resolvesYouTubeStreamKeyAtStart =
		isYouTubeTarget(target) && target.authMode == TargetAuthMode::YouTubeOAuth;
	if (!resolvesYouTubeStreamKeyAtStart && target.streamKey.trimmed().isEmpty() &&
	    !target.authCredentialRef.trimmed().isEmpty()) {
		if (!SecretStore::isOwnedCredentialRef(target.authCredentialRef))
			return setTargetError(target, QStringLiteral("Saved stream key reference is outside the DSK credential namespace."));
		QString secret;
		if (!secrets.readSecret(target.authCredentialRef, &secret, &error))
			return setTargetError(target, QString("Failed to read saved stream key: %1").arg(error));
		target.streamKey = secret;
	}
	if (target.authMode == TargetAuthMode::YouTubeOAuth && target.oauthClientSecret.trimmed().isEmpty() &&
	    !target.oauthClientSecretRef.trimmed().isEmpty()) {
		if (!SecretStore::isOwnedCredentialRef(target.oauthClientSecretRef))
			return setTargetError(target, QStringLiteral("Saved OAuth client secret reference is outside the DSK credential namespace."));
		QString secret;
		if (!secrets.readSecret(target.oauthClientSecretRef, &secret, &error))
			return setTargetError(target, QString("Failed to read saved OAuth client secret: %1").arg(error));
		target.oauthClientSecret = secret;
	}
	if (target.authMode == TargetAuthMode::YouTubeOAuth && target.oauthRefreshToken.trimmed().isEmpty() &&
	    !target.oauthRefreshTokenRef.trimmed().isEmpty()) {
		if (!SecretStore::isOwnedCredentialRef(target.oauthRefreshTokenRef))
			return setTargetError(target, QStringLiteral("Saved OAuth refresh token reference is outside the DSK credential namespace."));
		QString secret;
		if (!secrets.readSecret(target.oauthRefreshTokenRef, &secret, &error))
			return setTargetError(target, QString("Failed to read saved OAuth refresh token: %1").arg(error));
		target.oauthRefreshToken = secret;
	}
	return true;
}

bool OutputManager::beginYouTubeStartPreflight(OutputTarget &target)
{
	if (sessionForTarget(target.id))
		return false;

	auto *session = new Session;
	session->serial = nextSessionSerial_++;
	session->targetId = target.id;
	session->youtubePreflight = true;
	sessions_.push_back(session);

	TargetRuntimeStatus &runtime = ensureRuntimeStatus(target.id);
	runtime.sessionSerial = session->serial;
	runtime.startedAtMs = 0;
	runtime.platform = PlatformLiveState::Unknown;
	setRuntimeTransport(target.id,
			    session->serial,
			    TransportState::Starting,
			    QStringLiteral("Checking YouTube broadcast before RTMP"));
	setRuntimePlatform(target.id,
			   session->serial,
			   PlatformLiveState::Unknown,
			   QStringLiteral("Confirming the selected YouTube broadcast"));
	logInfo(QStringLiteral("%1: YouTube start preflight began before RTMP output (session %2).")
			.arg(target.name)
			.arg(session->serial));
	emit statusMessage(QStringLiteral("%1: Confirming the YouTube broadcast before sending video.")
				   .arg(target.name));
	resolveCommentViewerYouTubeBroadcastSelection(target.id, session->serial);
	return true;
}

void OutputManager::resolveCommentViewerYouTubeBroadcastSelection(const QString &targetId,
							  quint64 sessionSerial)
{
	Session *session = sessionForTarget(targetId);
	if (!session || session->serial != sessionSerial || !session->youtubePreflight)
		return;
	TargetRuntimeStatus &runtime = ensureRuntimeStatus(targetId);
	if (session->youtubeCommentViewerSelectionChecked || !runtime.broadcastId.trimmed().isEmpty()) {
		maybeStartYouTubeBroadcast(targetId, sessionSerial);
		return;
	}
	session->youtubeCommentViewerSelectionChecked = true;

	HttpRequest request;
	request.url = commentViewerYouTubeBroadcastSelectionUrl();
	request.timeoutMs = CommentViewerSelectionTimeoutMs;
	request.maxResponseBytes = 4096;
	const quint64 requestId = http_->send(std::move(request), [this, targetId, sessionSerial](HttpResponse response) {
		Session *currentSession = sessionForTarget(targetId);
		if (!currentSession || currentSession->serial != sessionSerial || !currentSession->youtubePreflight)
			return;
		if (response.isSuccess()) {
			const auto selection = parseCommentViewerYouTubeBroadcastSelection(response.body);
			TargetRuntimeStatus &currentRuntime = ensureRuntimeStatus(targetId);
			if (selection && currentRuntime.broadcastId.trimmed().isEmpty()) {
				currentRuntime.broadcastId = selection->broadcastId;
				logInfo(QStringLiteral("Using the YouTube broadcast prepared by DSK Comment Viewer for session %1.")
						.arg(sessionSerial));
			}
		}
		maybeStartYouTubeBroadcast(targetId, sessionSerial);
	});
	if (requestId == 0)
		maybeStartYouTubeBroadcast(targetId, sessionSerial);
}

bool OutputManager::startIndependentTarget(OutputTarget &target, Session *existingSession)
{
	const quint64 sessionSerial = existingSession ? existingSession->serial : nextSessionSerial_++;
	const QString effectiveStreamKey =
		existingSession && !existingSession->youtubeResolvedStreamKey.isEmpty()
			? existingSession->youtubeResolvedStreamKey
			: target.streamKey;
	const EncoderProfile profile = effectiveProfileForTarget(target);
	QString videoError;
	video_t *video = videoForTarget(target, profile, &videoError);
	if (!video)
		return setTargetError(target, videoError.isEmpty() ? "Video output is unavailable." : videoError);
	logInfo(QString("Video output for %1: profile=%2x%3 group=%4 actual=%5")
			.arg(target.name,
			     QString::number(profile.width),
			     QString::number(profile.height),
			     encoderGroupToString(profile.group),
			     videoOutputSummary(video)));
	if (isYouTubeTarget(target) && profile.group == EncoderGroup::DskVertical) {
		const video_output_info *info = video_output_get_info(video);
		if (!info || info->height <= info->width) {
			logWarning(QString("YouTube vertical preflight failed for %1: actual video output is %2.")
					   .arg(target.name, videoOutputSummary(video)));
		} else {
			logInfo(QString("YouTube vertical preflight OK for %1: sending portrait video %2.")
					.arg(target.name, videoOutputSummary(video)));
		}
	}

	obs_service_t *service = createService(target, effectiveStreamKey);
	if (!service)
		return setTargetError(target, "Failed to create RTMP service.");

	obs_output_t *output = createOutput(target, service, sessionSerial);
	if (!output) {
		obs_service_release(service);
		return setTargetError(target, "Failed to create output.");
	}

	QString sharedKey;
	if (target.useSharedEncoder) {
		QString sharedError;
		SharedEncoderSet *shared = acquireSharedEncoders(target, profile, video, &sharedError);
		if (!shared) {
			releaseOutputAndService(output, service);
			return setTargetError(target, sharedError.isEmpty() ? "Failed to create shared encoders." : sharedError);
		}
		sharedKey = shared->key;
		obs_output_set_video_encoder(output, shared->videoEncoder);
		obs_output_set_audio_encoder(output, shared->audioEncoder, 0);
	} else {
		obs_encoder_t *videoEncoder = createVideoEncoder(target, profile);
		if (!videoEncoder) {
			releaseOutputAndService(output, service);
			return setTargetError(target, "Failed to create video encoder.");
		}

		obs_encoder_t *audioEncoder = createAudioEncoder(target, profile);
		if (!audioEncoder) {
			obs_encoder_release(videoEncoder);
			releaseOutputAndService(output, service);
			return setTargetError(target, "Failed to create audio encoder.");
		}

		obs_encoder_set_video(videoEncoder, video);
		obs_encoder_set_audio(audioEncoder, obs_get_audio());
		obs_output_set_video_encoder(output, videoEncoder);
		obs_output_set_audio_encoder(output, audioEncoder, 0);
		obs_encoder_release(videoEncoder);
		obs_encoder_release(audioEncoder);
	}

	connectOutputSignals(output, this);
	if (!obs_output_start(output)) {
		disconnectOutputSignals(output, this);
		releaseOutputAndService(output, service);
		releaseSharedEncoders(sharedKey);
		return setTargetError(target, "OBS rejected the independent output start request.");
	}

	auto *session = existingSession ? existingSession : new Session;
	session->serial = sessionSerial;
	session->targetId = target.id;
	session->output = output;
	session->service = service;
	session->startedAtMs = QDateTime::currentMSecsSinceEpoch();
	session->sharedEncoderKey = sharedKey;
	if (!existingSession)
		sessions_.push_back(session);

	target.state = TargetState::Starting;
	target.lastError.clear();
	TargetRuntimeStatus &runtime = ensureRuntimeStatus(target.id);
	runtime.sessionSerial = session->serial;
	runtime.startedAtMs = session->startedAtMs;
	runtime.platform = isYouTubeTarget(target) ? PlatformLiveState::Unknown : PlatformLiveState::NotApplicable;
	setRuntimeTransport(target.id, session->serial, TransportState::Starting, QStringLiteral("Connecting"));
	logInfo(QString("Started %1 with %2 at %3x%4 using %5, key %6")
			.arg(target.name, encoderGroupToString(target.encoderGroup))
			.arg(profile.width)
			.arg(profile.height)
			.arg(profile.videoEncoderId.isEmpty() ? QStringLiteral("obs_x264") : profile.videoEncoderId)
			.arg(maskedKey(effectiveStreamKey)));
	if (!runtimeTargetIds_.contains(target.id))
		emit statusMessage(QString("Starting %1").arg(target.name));
	return true;
}

void OutputManager::maybeStartYouTubeBroadcast(const QString &targetId, quint64 sessionSerial, int attempt)
{
	OutputTarget *target = findTarget(targetId);
	if (!target || !canContinuePlatformStart(*target, runtimeStatusForTarget(targetId)) ||
	    !sessionMatches(targetId, sessionSerial))
		return;
	if (target->platformId.compare(QStringLiteral("youtube"), Qt::CaseInsensitive) != 0)
		return;
	if (runtimeStatusForTarget(targetId).platform == PlatformLiveState::Live)
		return;

	logInfo(QStringLiteral("%1: YouTube API start check (session %2, attempt %3).")
			.arg(target->name)
			.arg(sessionSerial)
			.arg(attempt));
	if (target->authMode != TargetAuthMode::YouTubeOAuth) {
		setRuntimePlatform(targetId, sessionSerial, PlatformLiveState::NeedsManualStart,
				   QStringLiteral("Start in YouTube Studio after RTMP signal is active"));
		emit statusMessage(QString("%1: YouTube signal is active. Connect YouTube login to auto-start the broadcast.").arg(target->name));
		return;
	}

	Session *session = sessionForTarget(targetId);
	if (!session || session->serial != sessionSerial)
		return;
	if (session->youtubeOperationInFlight) {
		logInfo(QStringLiteral("%1: YouTube API operation is already in flight for session %2.")
				.arg(target->name)
				.arg(sessionSerial));
		return;
	}

	const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
	const bool autoStartWaiting = session->youtubeAutoStartWaitingSinceMs > 0;
	const bool signalTimedOut = session->youtubeSignalActiveAtMs > 0 &&
				    nowMs - session->youtubeSignalActiveAtMs > YouTubeSignalWaitTimeoutMs;
	if (!autoStartWaiting && (attempt > 20 || signalTimedOut)) {
		setTargetApiWarning(targetId, QStringLiteral("YouTube API start failed: YouTube stream did not become active."), sessionSerial);
		return;
	}
	if (autoStartWaiting &&
	    nowMs - session->youtubeAutoStartWaitingSinceMs > YouTubeAutoStartWaitTimeoutMs) {
		setRuntimePlatform(targetId, sessionSerial, PlatformLiveState::NeedsManualStart,
				   QStringLiteral("YouTube Auto-start is still pending - check YouTube Studio"));
		emit statusMessage(QStringLiteral("%1: YouTube Auto-start is still pending. Check the broadcast in YouTube Studio.")
				   .arg(target->name));
		return;
	}

	const OAuthClientCredentials credentials = oauthEffectiveClientCredentials(
		target->authMode, target->oauthClientId, target->oauthClientSecret);
	if (!credentials.isComplete()) {
		setTargetApiWarning(targetId, QStringLiteral("YouTube API start unavailable: Google OAuth application credentials are missing."), sessionSerial);
		return;
	}

	if (target->oauthRefreshToken.trimmed().isEmpty()) {
		setTargetApiWarning(targetId, QStringLiteral("YouTube API start unavailable: reconnect YouTube login to save an OAuth refresh token."), sessionSerial);
		return;
	}

	++session->youtubePollGeneration;
	session->youtubeOperationInFlight = true;
	const quint64 operationGeneration = ++session->youtubeOperationGeneration;
	setRuntimePlatform(
		targetId,
		sessionSerial,
		session->youtubePreflight ? PlatformLiveState::Unknown : PlatformLiveState::RtmpSignalOnly,
		session->youtubePreflight ? QStringLiteral("Confirming the selected YouTube broadcast")
					  : QStringLiteral("Checking YouTube Live start"));
	if (!session->youtubeAccessToken.isEmpty() &&
	    session->youtubeAccessTokenExpiresAtMs > nowMs + 30000) {
		logInfo(QStringLiteral("%1: Reusing the current YouTube access token for this output session.")
				.arg(target->name));
		listYouTubeBroadcasts(targetId, sessionSerial, session->youtubeAccessToken, attempt, operationGeneration);
		return;
	}
	refreshYouTubeAccessToken(targetId, sessionSerial, attempt, operationGeneration);
}

bool OutputManager::youtubeOperationMatches(const QString &targetId, quint64 sessionSerial,
					    quint64 operationGeneration) const
{
	const Session *session = sessionForTarget(targetId);
	return session && session->serial == sessionSerial && session->youtubeOperationInFlight &&
	       session->youtubeOperationGeneration == operationGeneration;
}

void OutputManager::completeYouTubeOperation(const QString &targetId, quint64 sessionSerial,
					     quint64 operationGeneration)
{
	Session *session = sessionForTarget(targetId);
	if (!session || session->serial != sessionSerial ||
	    session->youtubeOperationGeneration != operationGeneration)
		return;
	session->youtubeOperationInFlight = false;
}

void OutputManager::scheduleYouTubePoll(const QString &targetId, quint64 sessionSerial,
					quint64 operationGeneration, int attempt, int delayMs)
{
	Session *session = sessionForTarget(targetId);
	if (!session || session->serial != sessionSerial || session->youtubeOperationInFlight ||
	    session->youtubeOperationGeneration != operationGeneration)
		return;
	const quint64 pollGeneration = ++session->youtubePollGeneration;
	QTimer::singleShot(qMax(0, delayMs), this,
			   [this, targetId, sessionSerial, operationGeneration, pollGeneration, attempt]() {
		Session *current = sessionForTarget(targetId);
		if (!current || current->serial != sessionSerial || current->youtubeOperationInFlight ||
		    current->youtubeOperationGeneration != operationGeneration ||
		    current->youtubePollGeneration != pollGeneration)
			return;
		maybeStartYouTubeBroadcast(targetId, sessionSerial, attempt);
	});
}

bool OutputManager::stopYouTubeAutoStartIfTimedOut(const QString &targetId, quint64 sessionSerial,
						   quint64 operationGeneration)
{
	Session *session = sessionForTarget(targetId);
	OutputTarget *target = findTarget(targetId);
	if (!session || !target || !youtubeOperationMatches(targetId, sessionSerial, operationGeneration) ||
	    session->youtubeAutoStartWaitingSinceMs <= 0 ||
	    QDateTime::currentMSecsSinceEpoch() - session->youtubeAutoStartWaitingSinceMs <=
		    YouTubeAutoStartWaitTimeoutMs)
		return false;

	completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
	setRuntimePlatform(targetId, sessionSerial, PlatformLiveState::NeedsManualStart,
			   QStringLiteral("YouTube Auto-start is still pending - check YouTube Studio"));
	emit statusMessage(QStringLiteral("%1: YouTube Auto-start is still pending. Check the broadcast in YouTube Studio.")
			   .arg(target->name));
	return true;
}

bool OutputManager::scheduleYouTubeRequestRetry(const QString &targetId, quint64 sessionSerial,
						quint64 operationGeneration, int attempt,
						const HttpResponse &response, const QString &stage)
{
	Session *session = sessionForTarget(targetId);
	OutputTarget *target = findTarget(targetId);
	if (!session || !target || !youtubeOperationMatches(targetId, sessionSerial, operationGeneration))
		return false;

	const QString error = platformHttpError(response);
	bool retry = isRetryableYouTubeResponse(response, error);
	if (response.statusCode == 401 && !session->youtubeAuthRefreshRetried) {
		session->youtubeAuthRefreshRetried = true;
		session->youtubeAccessToken.clear();
		session->youtubeAccessTokenExpiresAtMs = 0;
		retry = true;
	}
	if (!retry || session->youtubeTransientRetryCount >= YouTubeMaxTransientRetries)
		return false;

	const int retryNumber = ++session->youtubeTransientRetryCount;
	const int delayMs = qMin(10000, 1000 << qMin(retryNumber - 1, 3));
	completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
	setRuntimePlatform(targetId, sessionSerial, PlatformLiveState::RtmpSignalOnly,
			   QStringLiteral("YouTube %1 temporarily unavailable - retrying").arg(stage));
	logWarning(QStringLiteral("%1: YouTube %2 request will retry in %3 ms (HTTP %4, retry %5/%6).")
			   .arg(target->name, stage)
			   .arg(delayMs)
			   .arg(response.statusCode)
			   .arg(retryNumber)
			   .arg(YouTubeMaxTransientRetries));
	scheduleYouTubePoll(targetId, sessionSerial, operationGeneration, attempt, delayMs);
	return true;
}

void OutputManager::applyYouTubeBroadcastSelection(const QString &targetId, quint64 sessionSerial,
					   quint64 selectionGeneration, const QString &broadcastId)
{
	OutputTarget *target = findTarget(targetId);
	const QString cleanBroadcastId = broadcastId.trimmed();
	if (!target || cleanBroadcastId.isEmpty() || !sessionMatches(targetId, sessionSerial) ||
	    !canContinuePlatformStart(*target, runtimeStatusForTarget(targetId)))
		return;
	Session *session = sessionForTarget(targetId);
	if (!session || session->serial != sessionSerial || session->youtubeOperationInFlight ||
	    session->youtubeOperationGeneration != selectionGeneration)
		return;

	TargetRuntimeStatus &runtime = ensureRuntimeStatus(targetId);
	runtime.broadcastId = cleanBroadcastId;
	session->youtubeAwaitingSelection = false;
	session->youtubeBroadcastSelectionConfirmed = true;
	++session->youtubeOperationGeneration;
	session->youtubeAutoStartWaitingSinceMs = 0;
	if (!session->youtubePreflight)
		session->youtubeSignalActiveAtMs = QDateTime::currentMSecsSinceEpoch();
	clearTargetApiWarning(targetId);
	setRuntimePlatform(
		targetId,
		sessionSerial,
		session->youtubePreflight ? PlatformLiveState::Unknown : PlatformLiveState::RtmpSignalOnly,
		session->youtubePreflight ? QStringLiteral("Confirming the selected YouTube broadcast")
					  : QStringLiteral("Checking selected YouTube broadcast"));
	emit statusMessage(QStringLiteral("%1: Checking the selected YouTube broadcast.").arg(target->name));
	maybeStartYouTubeBroadcast(targetId, sessionSerial);
}

void OutputManager::cancelYouTubeBroadcastSelection(const QString &targetId, quint64 sessionSerial,
						    quint64 selectionGeneration)
{
	Session *session = sessionForTarget(targetId);
	if (!session || session->serial != sessionSerial || !session->youtubePreflight ||
	    !session->youtubeAwaitingSelection ||
	    session->youtubeOperationGeneration != selectionGeneration)
		return;

	logInfo(QStringLiteral("YouTube start preflight cancelled before RTMP output (session %1).")
			.arg(sessionSerial));
	stopTarget(targetId);
}

void OutputManager::refreshYouTubeAccessToken(const QString &targetId, quint64 sessionSerial, int attempt,
					      quint64 operationGeneration)
{
	OutputTarget *target = findTarget(targetId);
	if (!target || !canContinuePlatformStart(*target, runtimeStatusForTarget(targetId)) ||
	    !youtubeOperationMatches(targetId, sessionSerial, operationGeneration))
		return;
	const OAuthClientCredentials credentials = oauthEffectiveClientCredentials(
		target->authMode, target->oauthClientId, target->oauthClientSecret);
	if (!credentials.isComplete()) {
		completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
		setTargetApiWarning(targetId, QStringLiteral("YouTube token refresh failed: Google OAuth application credentials are missing."), sessionSerial);
		return;
	}

	QUrlQuery body;
	body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
	body.addQueryItem(QStringLiteral("client_id"), credentials.clientId);
	body.addQueryItem(QStringLiteral("client_secret"), credentials.clientSecret);
	body.addQueryItem(QStringLiteral("refresh_token"), target->oauthRefreshToken);

	HttpRequest request;
	request.url = QUrl(QStringLiteral("https://oauth2.googleapis.com/token"));
	request.method = QByteArrayLiteral("POST");
	request.timeoutMs = PlatformApiTimeoutMs;
	request.headers.push_back({QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/x-www-form-urlencoded")});
	request.body = formBody(body);
	logInfo(QStringLiteral("%1: Requesting a YouTube OAuth access token (session %2).")
			.arg(target->name)
			.arg(sessionSerial));
	http_->send(std::move(request), [this, targetId, sessionSerial, attempt, operationGeneration](HttpResponse response) {
		const QString error = platformHttpError(response);
		OutputTarget *target = findTarget(targetId);
		if (!target || !youtubeOperationMatches(targetId, sessionSerial, operationGeneration) ||
		    !canContinuePlatformStart(*target, runtimeStatusForTarget(targetId)))
			return;
		if (!error.isEmpty()) {
			if (scheduleYouTubeRequestRetry(targetId, sessionSerial, operationGeneration, attempt,
						       response, QStringLiteral("token refresh")))
				return;
			logWarning(QStringLiteral("%1: YouTube OAuth access-token request failed (HTTP %2).")
					   .arg(target->name)
					   .arg(response.statusCode));
			completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
			setTargetApiWarning(targetId, QString("YouTube token refresh failed: %1").arg(error), sessionSerial);
			return;
		}

		const QJsonDocument document = QJsonDocument::fromJson(response.body);
		const QJsonObject tokenResponse = document.object();
		const QString accessToken = tokenResponse.value(QStringLiteral("access_token")).toString();
		if (accessToken.isEmpty()) {
			completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
			setTargetApiWarning(targetId, QStringLiteral("YouTube token refresh failed: access token missing."), sessionSerial);
			return;
		}
		Session *session = sessionForTarget(targetId);
		if (!session || session->serial != sessionSerial)
			return;
		session->youtubeTransientRetryCount = 0;
		const int expiresInSeconds = qMax(60, tokenResponse.value(QStringLiteral("expires_in")).toInt(3600));
		session->youtubeAccessToken = accessToken;
		session->youtubeAccessTokenExpiresAtMs =
			QDateTime::currentMSecsSinceEpoch() + static_cast<qint64>(expiresInSeconds) * 1000;
		logInfo(QStringLiteral("%1: YouTube OAuth access token refreshed (HTTP %2, expires in %3 sec).")
				.arg(target->name)
				.arg(response.statusCode)
				.arg(expiresInSeconds));
		listYouTubeBroadcasts(targetId, sessionSerial, accessToken, attempt, operationGeneration);
	});
}

void OutputManager::listYouTubeBroadcasts(const QString &targetId, quint64 sessionSerial,
					  const QString &accessToken, int attempt,
					  quint64 operationGeneration)
{
	OutputTarget *target = findTarget(targetId);
	if (!target || !canContinuePlatformStart(*target, runtimeStatusForTarget(targetId)) ||
	    !youtubeOperationMatches(targetId, sessionSerial, operationGeneration))
		return;
	if (stopYouTubeAutoStartIfTimedOut(targetId, sessionSerial, operationGeneration))
		return;
	Session *session = sessionForTarget(targetId);
	setRuntimePlatform(
		targetId,
		sessionSerial,
		session && session->youtubePreflight ? PlatformLiveState::Unknown
						    : PlatformLiveState::RtmpSignalOnly,
		session && session->youtubePreflight ? QStringLiteral("Finding YouTube broadcast before RTMP")
						    : QStringLiteral("Finding YouTube broadcast"));
	listYouTubeBroadcastPage(targetId,
				 sessionSerial,
				 accessToken,
				 attempt,
				 operationGeneration,
				 QStringLiteral("upcoming"),
				 QString(),
				 0,
				 QJsonArray());
}

void OutputManager::listYouTubeBroadcastPage(const QString &targetId, quint64 sessionSerial,
					     const QString &accessToken, int attempt,
					     quint64 operationGeneration, const QString &broadcastStatus,
					     const QString &pageToken,
					     int pageNumber, const QJsonArray &broadcasts)
{
	OutputTarget *target = findTarget(targetId);
	if (!target || !canContinuePlatformStart(*target, runtimeStatusForTarget(targetId)) ||
	    !youtubeOperationMatches(targetId, sessionSerial, operationGeneration))
		return;
	if (stopYouTubeAutoStartIfTimedOut(targetId, sessionSerial, operationGeneration))
		return;

	QUrl url(QStringLiteral("https://www.googleapis.com/youtube/v3/liveBroadcasts"));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("part"), QStringLiteral("id,snippet,contentDetails,status"));
	query.addQueryItem(QStringLiteral("broadcastStatus"), broadcastStatus);
	query.addQueryItem(QStringLiteral("broadcastType"), QStringLiteral("all"));
	query.addQueryItem(QStringLiteral("maxResults"), QString::number(YouTubeApiPageSize));
	if (!pageToken.isEmpty())
		query.addQueryItem(QStringLiteral("pageToken"), pageToken);
	url.setQuery(query);

	HttpRequest request;
	request.url = url;
	request.timeoutMs = PlatformApiTimeoutMs;
	request.headers.push_back({QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + accessToken.toUtf8()});
	logInfo(QStringLiteral("%1: Listing %2 YouTube broadcasts (page %3, session %4, attempt %5).")
			.arg(target->name)
			.arg(broadcastStatus)
			.arg(pageNumber + 1)
			.arg(sessionSerial)
			.arg(attempt));
	http_->send(std::move(request),
		    [this, targetId, sessionSerial, accessToken, attempt, operationGeneration, broadcastStatus, pageNumber,
		     broadcasts](HttpResponse response) {
		const QString error = platformHttpError(response);
		OutputTarget *target = findTarget(targetId);
		if (!target || !youtubeOperationMatches(targetId, sessionSerial, operationGeneration) ||
		    !canContinuePlatformStart(*target, runtimeStatusForTarget(targetId)))
			return;
		if (!error.isEmpty()) {
			if (scheduleYouTubeRequestRetry(targetId, sessionSerial, operationGeneration, attempt,
						       response, QStringLiteral("broadcast lookup")))
				return;
			completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
			setTargetApiWarning(targetId, QString("YouTube broadcast lookup failed: %1").arg(error), sessionSerial);
			return;
		}
		const QJsonObject responseObject = QJsonDocument::fromJson(response.body).object();
		const QJsonArray items = responseObject.value(QStringLiteral("items")).toArray();
		QJsonArray accumulated = broadcasts;
		for (const QJsonValue &item : items)
			accumulated.push_back(item);
		logInfo(QStringLiteral("%1: %2 YouTube broadcast page returned %3 item(s), %4 accumulated (HTTP %5).")
				.arg(target->name)
				.arg(broadcastStatus)
				.arg(items.size())
				.arg(accumulated.size())
				.arg(response.statusCode));

		const QString nextPageToken = responseObject.value(QStringLiteral("nextPageToken")).toString();
		if (!nextPageToken.isEmpty() && pageNumber + 1 < YouTubeMaxBroadcastPagesPerStatus) {
			listYouTubeBroadcastPage(targetId,
						 sessionSerial,
						 accessToken,
						 attempt,
						 operationGeneration,
						 broadcastStatus,
						 nextPageToken,
						 pageNumber + 1,
						 accumulated);
			return;
		}
		if (!nextPageToken.isEmpty()) {
			completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
			setTargetApiWarning(
				targetId,
				QStringLiteral("YouTube broadcast lookup blocked: the %1 broadcast list exceeded %2 pages. Narrow or remove old scheduled broadcasts, then retry.")
					.arg(broadcastStatus)
					.arg(YouTubeMaxBroadcastPagesPerStatus),
				sessionSerial);
			return;
		}

		if (broadcastStatus == QStringLiteral("upcoming")) {
			listYouTubeBroadcastPage(targetId,
						 sessionSerial,
						 accessToken,
						 attempt,
						 operationGeneration,
						 QStringLiteral("active"),
						 QString(),
						 0,
						 accumulated);
			return;
		}
		if (broadcastStatus == QStringLiteral("active") && accumulated.isEmpty()) {
			logInfo(QStringLiteral("%1: No ready YouTube broadcast was found; checking completed broadcasts for a reusable stream.")
					.arg(target->name));
			listYouTubeBroadcastPage(targetId,
						 sessionSerial,
						 accessToken,
						 attempt,
						 operationGeneration,
						 QStringLiteral("completed"),
						 QString(),
						 0,
						 QJsonArray());
			return;
		}

		if (accumulated.isEmpty()) {
			completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
			setTargetApiWarning(targetId, QStringLiteral("YouTube broadcast lookup found no reusable completed broadcast."), sessionSerial);
			return;
		}
		if (broadcastStatus == QStringLiteral("completed")) {
			if (Session *session = sessionForTarget(targetId))
				session->youtubeCompletedReuseLookup = true;
		}
		listYouTubeStreams(targetId, sessionSerial, accessToken, attempt, operationGeneration, accumulated);
	});
}

void OutputManager::listYouTubeStreams(const QString &targetId, quint64 sessionSerial,
				       const QString &accessToken, int attempt,
				       quint64 operationGeneration, const QJsonArray &broadcasts)
{
	OutputTarget *target = findTarget(targetId);
	if (!target || !canContinuePlatformStart(*target, runtimeStatusForTarget(targetId)) ||
	    !youtubeOperationMatches(targetId, sessionSerial, operationGeneration))
		return;
	if (stopYouTubeAutoStartIfTimedOut(targetId, sessionSerial, operationGeneration))
		return;

	QStringList streamIds;
	for (const QJsonValue &value : broadcasts) {
		const QJsonObject broadcast = value.toObject();
		const QString streamId = broadcast.value(QStringLiteral("contentDetails")).toObject().value(QStringLiteral("boundStreamId")).toString();
		if (!streamId.isEmpty() && !streamIds.contains(streamId))
			streamIds.push_back(streamId);
	}

	if (streamIds.isEmpty()) {
		completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
		setTargetApiWarning(targetId, QStringLiteral("YouTube broadcast lookup found no bound stream."), sessionSerial);
		return;
	}
	listYouTubeStreamBatch(targetId,
			       sessionSerial,
			       accessToken,
			       attempt,
			       operationGeneration,
			       broadcasts,
			       streamIds,
			       0,
			       QJsonArray());
}

void OutputManager::listYouTubeStreamBatch(const QString &targetId, quint64 sessionSerial,
					   const QString &accessToken, int attempt,
					   quint64 operationGeneration, const QJsonArray &broadcasts,
					   const QStringList &streamIds,
					   int offset, const QJsonArray &streams)
{
	OutputTarget *target = findTarget(targetId);
	if (!target || !canContinuePlatformStart(*target, runtimeStatusForTarget(targetId)) ||
	    !youtubeOperationMatches(targetId, sessionSerial, operationGeneration))
		return;
	if (stopYouTubeAutoStartIfTimedOut(targetId, sessionSerial, operationGeneration))
		return;

	const QStringList batchIds = streamIds.mid(offset, YouTubeApiPageSize);
	if (batchIds.isEmpty()) {
		Session *session = sessionForTarget(targetId);
		if (session && session->youtubeCompletedReuseLookup) {
			processYouTubeCompletedBroadcastReuse(targetId, sessionSerial, accessToken,
							 operationGeneration, broadcasts, streams);
			return;
		}
		processYouTubeBroadcastSelection(targetId, sessionSerial, accessToken, attempt,
						 operationGeneration, broadcasts, streams);
		return;
	}

	QUrl url(QStringLiteral("https://www.googleapis.com/youtube/v3/liveStreams"));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("part"), QStringLiteral("id,cdn,status,contentDetails"));
	query.addQueryItem(QStringLiteral("id"), batchIds.join(','));
	query.addQueryItem(QStringLiteral("maxResults"), QString::number(YouTubeApiPageSize));
	url.setQuery(query);

	HttpRequest request;
	request.url = url;
	request.timeoutMs = PlatformApiTimeoutMs;
	request.headers.push_back({QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + accessToken.toUtf8()});
	logInfo(QStringLiteral("%1: Checking YouTube stream batch %2-%3 of %4 (session %5).")
			.arg(target->name)
			.arg(offset + 1)
			.arg(offset + batchIds.size())
			.arg(streamIds.size())
			.arg(sessionSerial));
	http_->send(std::move(request),
		    [this, targetId, sessionSerial, accessToken, attempt, operationGeneration, broadcasts, streamIds, offset, streams,
		     batchSize = batchIds.size()](HttpResponse response) {
		const QString error = platformHttpError(response);
		OutputTarget *target = findTarget(targetId);
		if (!target || !canContinuePlatformStart(*target, runtimeStatusForTarget(targetId)) ||
		    !youtubeOperationMatches(targetId, sessionSerial, operationGeneration))
			return;
		if (!error.isEmpty()) {
			if (scheduleYouTubeRequestRetry(targetId, sessionSerial, operationGeneration, attempt,
						       response, QStringLiteral("stream status")))
				return;
			completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
			setTargetApiWarning(targetId, QString("YouTube stream status lookup failed: %1").arg(error), sessionSerial);
			return;
		}
		QJsonArray accumulatedStreams = streams;
		const QJsonArray batchStreams =
			QJsonDocument::fromJson(response.body).object().value(QStringLiteral("items")).toArray();
		for (const QJsonValue &value : batchStreams)
			accumulatedStreams.push_back(value);
		const int nextOffset = offset + batchSize;
		if (nextOffset < streamIds.size()) {
			listYouTubeStreamBatch(targetId,
					       sessionSerial,
					       accessToken,
					       attempt,
					       operationGeneration,
					       broadcasts,
					       streamIds,
					       nextOffset,
					       accumulatedStreams);
			return;
		}
		Session *session = sessionForTarget(targetId);
		if (session && session->youtubeCompletedReuseLookup) {
			processYouTubeCompletedBroadcastReuse(targetId, sessionSerial, accessToken,
							 operationGeneration, broadcasts,
							 accumulatedStreams);
			return;
		}
		processYouTubeBroadcastSelection(targetId,
						 sessionSerial,
						 accessToken,
						 attempt,
						 operationGeneration,
						 broadcasts,
						 accumulatedStreams);
	});
}

void OutputManager::processYouTubeCompletedBroadcastReuse(const QString &targetId,
						   quint64 sessionSerial,
						   const QString &accessToken,
						   quint64 operationGeneration,
						   const QJsonArray &broadcasts,
						   const QJsonArray &streams)
{
	OutputTarget *target = findTarget(targetId);
	Session *session = sessionForTarget(targetId);
	if (!target || !session || session->serial != sessionSerial || !session->youtubePreflight ||
	    !youtubeOperationMatches(targetId, sessionSerial, operationGeneration) ||
	    !canContinuePlatformStart(*target, runtimeStatusForTarget(targetId)))
		return;
	session->youtubeCompletedReuseLookup = false;

	QHash<QString, QJsonObject> streamsById;
	for (const QJsonValue &value : streams) {
		const QJsonObject stream = value.toObject();
		const QString streamId = stream.value(QStringLiteral("id")).toString().trimmed();
		if (!streamId.isEmpty())
			streamsById.insert(streamId, stream);
	}
	const QString savedStreamKey = session->youtubeResolvedStreamKey.isEmpty()
					       ? target->streamKey
					       : session->youtubeResolvedStreamKey;
	const QJsonObject completed = youtubeMostRecentReusableCompletedBroadcast(
		broadcasts, streamsById, savedStreamKey);
	if (completed.isEmpty()) {
		completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
		setTargetApiWarning(
			targetId,
			QStringLiteral("YouTube broadcast lookup found no reusable completed broadcast matching this target's stream key."),
			sessionSerial);
		return;
	}
	const QString streamId = completed.value(QStringLiteral("contentDetails"))
					 .toObject()
					 .value(QStringLiteral("boundStreamId"))
					 .toString()
					 .trimmed();
	const QJsonObject stream = streamsById.value(streamId);
	const QString streamKey = stream.value(QStringLiteral("cdn"))
					.toObject()
					.value(QStringLiteral("ingestionInfo"))
					.toObject()
					.value(QStringLiteral("streamName"))
					.toString()
					.trimmed();
	if (streamId.isEmpty() || streamKey.isEmpty()) {
		completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
		setTargetApiWarning(targetId,
				    QStringLiteral("YouTube previous broadcast reuse failed: the reusable stream was incomplete."),
				    sessionSerial);
		return;
	}
	session->youtubeResolvedStreamKey = streamKey;
	setRuntimePlatform(targetId, sessionSerial, PlatformLiveState::Unknown,
			   QStringLiteral("Creating a new YouTube broadcast from the previous settings"));
	logInfo(QStringLiteral("%1: Creating a new YouTube broadcast from the most recent reusable completed broadcast before RTMP starts.")
			.arg(target->name));
	createYouTubeBroadcastFromCompleted(targetId, sessionSerial, operationGeneration,
					    accessToken, completed, stream);
}

void OutputManager::createYouTubeBroadcastFromCompleted(const QString &targetId,
						 quint64 sessionSerial,
						 quint64 operationGeneration,
						 const QString &accessToken,
						 const QJsonObject &completedBroadcast,
						 const QJsonObject &stream)
{
	OutputTarget *target = findTarget(targetId);
	if (!target || !youtubeOperationMatches(targetId, sessionSerial, operationGeneration))
		return;
	QUrl url(QStringLiteral("https://www.googleapis.com/youtube/v3/liveBroadcasts"));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("part"), QStringLiteral("snippet,status,contentDetails"));
	url.setQuery(query);
	const QString scheduledStart = QDateTime::currentDateTimeUtc().addSecs(5).toString(Qt::ISODate);
	HttpRequest request;
	request.url = url;
	request.method = QByteArrayLiteral("POST");
	request.timeoutMs = PlatformApiTimeoutMs;
	request.headers.push_back({QByteArrayLiteral("Authorization"),
				   QByteArrayLiteral("Bearer ") + accessToken.toUtf8()});
	request.headers.push_back({QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json")});
	request.body = QJsonDocument(youtubeReusedBroadcastInsertBody(completedBroadcast, scheduledStart))
			       .toJson(QJsonDocument::Compact);
	http_->send(std::move(request),
		    [this, targetId, sessionSerial, operationGeneration, accessToken,
		     stream](HttpResponse response) {
		if (!youtubeOperationMatches(targetId, sessionSerial, operationGeneration))
			return;
		const QString error = platformHttpError(response);
		if (!error.isEmpty()) {
			completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
			setTargetApiWarning(targetId,
					    QStringLiteral("YouTube previous broadcast reuse failed while creating a new broadcast: %1")
						    .arg(error),
					    sessionSerial);
			return;
		}
		const QJsonObject created = QJsonDocument::fromJson(response.body).object();
		if (created.value(QStringLiteral("id")).toString().trimmed().isEmpty()) {
			completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
			setTargetApiWarning(targetId,
					    QStringLiteral("YouTube previous broadcast reuse failed: broadcast creation returned no ID."),
					    sessionSerial);
			return;
		}
		bindYouTubeBroadcastFromCompleted(targetId, sessionSerial, operationGeneration,
						 accessToken, created, stream);
	});
}

void OutputManager::bindYouTubeBroadcastFromCompleted(const QString &targetId,
					      quint64 sessionSerial,
					      quint64 operationGeneration,
					      const QString &accessToken,
					      const QJsonObject &createdBroadcast,
					      const QJsonObject &stream)
{
	const QString broadcastId = createdBroadcast.value(QStringLiteral("id")).toString().trimmed();
	const QString streamId = stream.value(QStringLiteral("id")).toString().trimmed();
	if (!youtubeOperationMatches(targetId, sessionSerial, operationGeneration) ||
	    broadcastId.isEmpty() || streamId.isEmpty())
		return;
	QUrl url(QStringLiteral("https://www.googleapis.com/youtube/v3/liveBroadcasts/bind"));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("part"), QStringLiteral("id,snippet,contentDetails,status"));
	query.addQueryItem(QStringLiteral("id"), broadcastId);
	query.addQueryItem(QStringLiteral("streamId"), streamId);
	url.setQuery(query);
	HttpRequest request;
	request.url = url;
	request.method = QByteArrayLiteral("POST");
	request.timeoutMs = PlatformApiTimeoutMs;
	request.headers.push_back({QByteArrayLiteral("Authorization"),
				   QByteArrayLiteral("Bearer ") + accessToken.toUtf8()});
	http_->send(std::move(request),
		    [this, targetId, sessionSerial, operationGeneration, accessToken,
		     createdBroadcast, stream](HttpResponse response) {
		if (!youtubeOperationMatches(targetId, sessionSerial, operationGeneration))
			return;
		const QString error = platformHttpError(response);
		if (!error.isEmpty()) {
			failYouTubeCompletedBroadcastReuseAfterCreation(
				targetId, sessionSerial, operationGeneration, accessToken,
				createdBroadcast.value(QStringLiteral("id")).toString(),
				QStringLiteral("YouTube previous broadcast reuse failed while binding the reusable stream: %1")
					.arg(error));
			return;
		}
		QJsonObject bound = QJsonDocument::fromJson(response.body).object();
		if (bound.isEmpty())
			bound = createdBroadcast;
		const QString boundStreamId = bound.value(QStringLiteral("contentDetails"))
						      .toObject()
						      .value(QStringLiteral("boundStreamId"))
						      .toString()
						      .trimmed();
		if (bound.value(QStringLiteral("id")).toString().trimmed().isEmpty() ||
		    boundStreamId != stream.value(QStringLiteral("id")).toString().trimmed()) {
			failYouTubeCompletedBroadcastReuseAfterCreation(
				targetId, sessionSerial, operationGeneration, accessToken,
				createdBroadcast.value(QStringLiteral("id")).toString(),
				QStringLiteral("YouTube previous broadcast reuse failed: the new broadcast binding could not be confirmed."));
			return;
		}
		QJsonArray broadcasts;
		broadcasts.push_back(bound);
		QJsonArray streams;
		streams.push_back(stream);
		processYouTubeBroadcastSelection(targetId, sessionSerial, accessToken, 0,
						 operationGeneration, broadcasts, streams);
	});
}

void OutputManager::failYouTubeCompletedBroadcastReuseAfterCreation(
	const QString &targetId, quint64 sessionSerial, quint64 operationGeneration,
	const QString &accessToken, const QString &createdBroadcastId, const QString &message)
{
	const QString cleanBroadcastId = createdBroadcastId.trimmed();
	if (!youtubeOperationMatches(targetId, sessionSerial, operationGeneration))
		return;
	if (cleanBroadcastId.isEmpty()) {
		completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
		setTargetApiWarning(targetId, message, sessionSerial);
		return;
	}
	QUrl url(QStringLiteral("https://www.googleapis.com/youtube/v3/liveBroadcasts"));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("id"), cleanBroadcastId);
	url.setQuery(query);
	HttpRequest request;
	request.url = url;
	request.method = QByteArrayLiteral("DELETE");
	request.timeoutMs = PlatformApiTimeoutMs;
	request.headers.push_back({QByteArrayLiteral("Authorization"),
				   QByteArrayLiteral("Bearer ") + accessToken.toUtf8()});
	logWarning(QStringLiteral("YouTube previous broadcast reuse failed after creating a broadcast; removing the unused broadcast before reporting the error."));
	http_->send(std::move(request),
		    [this, targetId, sessionSerial, operationGeneration, message](HttpResponse response) {
		if (!youtubeOperationMatches(targetId, sessionSerial, operationGeneration))
			return;
		const QString cleanupError = platformHttpError(response);
		completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
		if (cleanupError.isEmpty()) {
			setTargetApiWarning(targetId, message, sessionSerial);
			return;
		}
		setTargetApiWarning(
			targetId,
			QStringLiteral("%1 Cleanup of the unused broadcast also failed: %2")
				.arg(message, cleanupError),
			sessionSerial);
	});
}

void OutputManager::processYouTubeBroadcastSelection(const QString &targetId, quint64 sessionSerial,
						     const QString &accessToken, int attempt,
						     quint64 operationGeneration, const QJsonArray &broadcasts,
						     const QJsonArray &streams)
{
	OutputTarget *target = findTarget(targetId);
	if (!target || !canContinuePlatformStart(*target, runtimeStatusForTarget(targetId)) ||
	    !youtubeOperationMatches(targetId, sessionSerial, operationGeneration))
		return;
	if (stopYouTubeAutoStartIfTimedOut(targetId, sessionSerial, operationGeneration))
		return;
	Session *session = sessionForTarget(targetId);
	if (!session || session->serial != sessionSerial)
		return;
	const auto resetTransientRetries = [this, &targetId]() {
		if (Session *session = sessionForTarget(targetId))
			session->youtubeTransientRetryCount = 0;
	};

	QHash<QString, QJsonObject> streamsById;
	for (const QJsonValue &value : streams) {
		const QJsonObject stream = value.toObject();
		streamsById.insert(stream.value(QStringLiteral("id")).toString(), stream);
	}

	const QString selectionStreamKey = session->youtubeResolvedStreamKey.isEmpty()
					       ? target->streamKey
					       : session->youtubeResolvedStreamKey;
	QString preferredBroadcastId = runtimeStatusForTarget(targetId).broadcastId;
	if (session->youtubePreflight && !session->youtubeBroadcastSelectionConfirmed)
		preferredBroadcastId.clear();
	const YouTubeBroadcastSelection selection =
		selectYouTubeBroadcast(broadcasts, streamsById, selectionStreamKey,
				       preferredBroadcastId,
				       session->youtubePreflight ? YouTubeBroadcastSelectionMode::Preflight
								 : YouTubeBroadcastSelectionMode::ActiveSignal);
	logInfo(QStringLiteral("%1: YouTube selection result=%2, candidates=%3, preferred=%4.")
				.arg(target->name,
				     youtubeSelectionStateName(selection.state),
				     QString::number(selection.candidates.size()),
				     preferredBroadcastId.isEmpty() ? QStringLiteral("no") : QStringLiteral("yes")));
	if (selection.state == YouTubeBroadcastSelectionState::MultipleActiveBroadcasts) {
			resetTransientRetries();
			completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
			session->youtubeAwaitingSelection = true;
			setTargetApiWarning(targetId, QStringLiteral("YouTube broadcast start blocked: multiple active broadcasts were found. Choose the broadcast in DSK Streaming."), sessionSerial);
			QJsonArray choices;
			for (const QJsonObject &candidate : selection.candidates)
				choices.push_back(candidate);
			emit youtubeBroadcastSelectionRequired(targetId, sessionSerial, operationGeneration, choices);
			return;
		}
		if (selection.state == YouTubeBroadcastSelectionState::MultipleStreamKeyMatches) {
			resetTransientRetries();
			completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
			session->youtubeAwaitingSelection = true;
			setTargetApiWarning(targetId, QStringLiteral("YouTube broadcast start blocked: multiple active broadcasts matched this target's stream key. Choose the broadcast in DSK Streaming."), sessionSerial);
			QJsonArray choices;
			for (const QJsonObject &candidate : selection.candidates)
				choices.push_back(candidate);
			emit youtubeBroadcastSelectionRequired(targetId, sessionSerial, operationGeneration, choices);
			return;
		}
		if (selection.state == YouTubeBroadcastSelectionState::NoActiveBroadcast &&
		    session->youtubePreflight) {
			resetTransientRetries();
			completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
			setTargetApiWarning(
				targetId,
				QStringLiteral("YouTube broadcast lookup found no broadcasts ready for preflight."),
				sessionSerial);
			return;
		}
		if (selection.state == YouTubeBroadcastSelectionState::NoStreamKeyMatch) {
			resetTransientRetries();
			completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
			setTargetApiWarning(targetId, QStringLiteral("YouTube broadcast start blocked: no active broadcast matched this target's stream key."), sessionSerial);
			return;
		}
		if (selection.state == YouTubeBroadcastSelectionState::PreferredBroadcastUnavailable) {
			if (shouldRetryYouTubePreferredBroadcastAfterRtmp(
				    session->youtubePreflight, session->youtubeSignalActiveAtMs > 0, attempt)) {
				completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
				setRuntimePlatform(targetId, sessionSerial, PlatformLiveState::LiveStarting,
						   QStringLiteral("YouTube is updating the selected broadcast - retrying"));
				logWarning(QStringLiteral("%1: Selected YouTube broadcast is temporarily unavailable after RTMP connected; retrying in 2 seconds (%2/%3).")
						   .arg(target->name)
						   .arg(attempt + 1)
						   .arg(YouTubePreferredBroadcastPropagationMaxRetries));
				scheduleYouTubePoll(targetId, sessionSerial, operationGeneration, attempt + 1, 2000);
				return;
			}
			resetTransientRetries();
			completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
			session->youtubeAwaitingSelection = !selection.candidates.isEmpty();
			setTargetApiWarning(targetId, QStringLiteral("YouTube broadcast start blocked: selected broadcast unavailable. Choose another broadcast in DSK Streaming."), sessionSerial);
			QJsonArray choices;
			for (const QJsonObject &candidate : selection.candidates)
				choices.push_back(candidate);
			if (!choices.isEmpty())
				emit youtubeBroadcastSelectionRequired(targetId, sessionSerial, operationGeneration, choices);
			return;
		}
		const QJsonObject selectedBroadcast = selection.broadcast;

		if (!selectedBroadcast.isEmpty()) {
			const QString lifecycle = selectedBroadcast.value(QStringLiteral("status")).toObject().value(QStringLiteral("lifeCycleStatus")).toString();
			const QString broadcastId = selectedBroadcast.value(QStringLiteral("id")).toString().trimmed();
			if (broadcastId.isEmpty()) {
				resetTransientRetries();
				completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
				setTargetApiWarning(targetId, QStringLiteral("YouTube broadcast start blocked: selected broadcast unavailable because its ID was missing."), sessionSerial);
				return;
			}
			ensureRuntimeStatus(targetId).broadcastId = broadcastId;
			if (session->youtubePreflight) {
				if (selection.streamKey.isEmpty()) {
					resetTransientRetries();
					completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
					setTargetApiWarning(
						targetId,
						QStringLiteral("YouTube broadcast start blocked: the selected broadcast has no usable RTMP stream key."),
						sessionSerial);
					return;
				}
				if (youtubeHasConflictingAutoStart(selection.candidates, broadcastId)) {
					resetTransientRetries();
					completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
					setTargetApiWarning(
						targetId,
						QStringLiteral("YouTube broadcast start blocked: another broadcast using this stream key has Auto-start enabled. Disable Auto-start on the other broadcast, then retry."),
						sessionSerial);
					return;
				}

				resetTransientRetries();
				completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
				session->youtubeResolvedStreamKey = selection.streamKey;
				session->youtubePreflight = false;
				session->youtubeAwaitingSelection = false;
				clearTargetApiWarning(targetId);
				setRuntimePlatform(targetId,
						   sessionSerial,
						   PlatformLiveState::Unknown,
						   QStringLiteral("Selected YouTube broadcast confirmed - connecting RTMP"));
				logInfo(QStringLiteral("%1: YouTube preflight selected broadcast %2; starting RTMP output.")
						.arg(target->name, broadcastIdForLog(broadcastId)));
				if (!startIndependentTarget(*target, session))
					releaseSession(targetId, false, sessionSerial);
				return;
			}
			if (lifecycle == QStringLiteral("live")) {
				resetTransientRetries();
				if (Session *session = sessionForTarget(targetId))
					session->youtubeAutoStartWaitingSinceMs = 0;
				completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
				clearTargetApiWarning(targetId);
				setRuntimePlatform(targetId, sessionSerial, PlatformLiveState::Live, QStringLiteral("YouTube broadcast is live"));
				emit statusMessage(QString("%1: YouTube broadcast is already live.").arg(target->name));
				notifyCommentViewerYouTubeStarted(targetId, sessionSerial, broadcastId);
				armYouTubeArchiveRotation(targetId, sessionSerial, selectedBroadcast);
				return;
			}

			if (lifecycle == QStringLiteral("testStarting") || lifecycle == QStringLiteral("liveStarting")) {
				resetTransientRetries();
				setRuntimePlatform(targetId, sessionSerial, PlatformLiveState::LiveStarting,
						   QStringLiteral("YouTube is switching to live"));
				emit statusMessage(QString("%1: YouTube broadcast is %2. Waiting...").arg(target->name, lifecycle));
				completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
				scheduleYouTubePoll(targetId, sessionSerial, operationGeneration, attempt + 1, 3000);
				return;
			}

			const QJsonObject contentDetails = selectedBroadcast.value(QStringLiteral("contentDetails")).toObject();
			if (contentDetails.value(QStringLiteral("enableAutoStart")).toBool(false)) {
				resetTransientRetries();
				Session *session = sessionForTarget(targetId);
				if (session && session->youtubeAutoStartWaitingSinceMs == 0)
					session->youtubeAutoStartWaitingSinceMs = QDateTime::currentMSecsSinceEpoch();
				setRuntimePlatform(targetId, sessionSerial, PlatformLiveState::LiveStarting,
						   QStringLiteral("YouTube Auto-start is enabled - waiting for live"));
				logInfo(QStringLiteral("%1: YouTube Auto-start is enabled for broadcast %2; waiting for YouTube to switch it live.")
						.arg(target->name, broadcastIdForLog(broadcastId)));
				emit statusMessage(QString("%1: YouTube Auto-start is enabled. Waiting for live...").arg(target->name));
				completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
				scheduleYouTubePoll(targetId, sessionSerial, operationGeneration, attempt + 1, 3000);
				return;
			}
			int transitionAttempt = attempt;
			if (Session *session = sessionForTarget(targetId)) {
				if (session->youtubeAutoStartWaitingSinceMs > 0) {
					transitionAttempt = 0;
					session->youtubeSignalActiveAtMs = QDateTime::currentMSecsSinceEpoch();
				}
				session->youtubeAutoStartWaitingSinceMs = 0;
			}
			const QJsonObject monitorStream = contentDetails.value(QStringLiteral("monitorStream")).toObject();
			const bool monitorStreamEnabled =
				!monitorStream.contains(QStringLiteral("enableMonitorStream")) ||
				monitorStream.value(QStringLiteral("enableMonitorStream")).toBool(true);
			const QString nextStatus =
				monitorStreamEnabled && lifecycle != QStringLiteral("testing") ? QStringLiteral("testing") : QStringLiteral("live");
			setRuntimePlatform(targetId,
					   sessionSerial,
					   nextStatus == QStringLiteral("testing") ? PlatformLiveState::Testing : PlatformLiveState::LiveStarting,
					   nextStatus == QStringLiteral("testing") ? QStringLiteral("YouTube monitor is testing")
									      : QStringLiteral("YouTube is switching to live"));
			transitionYouTubeBroadcast(targetId, accessToken, broadcastId, nextStatus, sessionSerial,
						   transitionAttempt, operationGeneration);
			return;
		}

		setRuntimePlatform(targetId, sessionSerial, PlatformLiveState::WaitingForSignal,
				   QStringLiteral("RTMP connected - waiting for YouTube signal"));
		resetTransientRetries();
		emit statusMessage(QString("%1: Waiting for YouTube stream signal...").arg(target->name));
		completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
		scheduleYouTubePoll(targetId, sessionSerial, operationGeneration, attempt + 1, 5000);
}

void OutputManager::transitionYouTubeBroadcast(const QString &targetId, const QString &accessToken, const QString &broadcastId,
					       const QString &broadcastStatus, quint64 sessionSerial, int attempt,
					       quint64 operationGeneration)
{
	OutputTarget *target = findTarget(targetId);
	if (!target || !canContinuePlatformStart(*target, runtimeStatusForTarget(targetId)) ||
	    !youtubeOperationMatches(targetId, sessionSerial, operationGeneration) || broadcastId.isEmpty() ||
	    broadcastStatus.isEmpty())
		return;

	QUrl url(QStringLiteral("https://www.googleapis.com/youtube/v3/liveBroadcasts/transition"));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("part"), QStringLiteral("id,snippet,contentDetails,status"));
	query.addQueryItem(QStringLiteral("id"), broadcastId);
	query.addQueryItem(QStringLiteral("broadcastStatus"), broadcastStatus);
	url.setQuery(query);

	HttpRequest request;
	request.url = url;
	request.method = QByteArrayLiteral("POST");
	request.timeoutMs = PlatformApiTimeoutMs;
	request.headers.push_back({QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + accessToken.toUtf8()});
	logInfo(QStringLiteral("%1: Requesting YouTube transition to %2 for broadcast %3 (session %4).")
			.arg(target->name, broadcastStatus, broadcastIdForLog(broadcastId))
			.arg(sessionSerial));
	http_->send(std::move(request), [this, targetId, sessionSerial, broadcastStatus, broadcastId, attempt,
					 operationGeneration](HttpResponse response) {
		const QString error = platformHttpError(response);
		OutputTarget *target = findTarget(targetId);
		if (!target || !youtubeOperationMatches(targetId, sessionSerial, operationGeneration) ||
		    !canContinuePlatformStart(*target, runtimeStatusForTarget(targetId)))
			return;
		if (!error.isEmpty()) {
			if (youtubeApiErrorHasAnyReason(response, {"errorStreamInactive"})) {
				completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
				setRuntimePlatform(targetId, sessionSerial, PlatformLiveState::WaitingForSignal,
						   QStringLiteral("YouTube stream signal changed - checking again"));
				logWarning(QStringLiteral("%1: YouTube transition found the bound stream inactive; rechecking signal status.")
						   .arg(target->name));
				scheduleYouTubePoll(targetId, sessionSerial, operationGeneration, attempt + 1, 3000);
				return;
			}
			if (youtubeApiErrorHasAnyReason(response, {"redundantTransition"})) {
				completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
				setRuntimePlatform(targetId, sessionSerial, PlatformLiveState::LiveStarting,
						   QStringLiteral("YouTube transition is already processing - confirming status"));
				logInfo(QStringLiteral("%1: YouTube reports the %2 transition is already active; confirming status.")
						.arg(target->name, broadcastStatus));
				scheduleYouTubePoll(targetId, sessionSerial, operationGeneration, attempt + 1, 2000);
				return;
			}
			if (scheduleYouTubeRequestRetry(targetId, sessionSerial, operationGeneration, attempt,
						       response, QStringLiteral("broadcast transition")))
				return;
			logWarning(QStringLiteral("%1: YouTube transition to %2 failed for broadcast %3 (HTTP %4).")
					   .arg(target->name, broadcastStatus, broadcastIdForLog(broadcastId))
					   .arg(response.statusCode));
			completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
			setTargetApiWarning(targetId, QString("YouTube broadcast %1 failed: %2").arg(broadcastStatus, error), sessionSerial);
			return;
		}
		logInfo(QStringLiteral("%1: YouTube transition to %2 accepted for broadcast %3 (HTTP %4).")
				.arg(target->name, broadcastStatus, broadcastIdForLog(broadcastId))
				.arg(response.statusCode));
		if (Session *session = sessionForTarget(targetId))
			session->youtubeTransientRetryCount = 0;

		if (broadcastStatus == QStringLiteral("testing")) {
			setRuntimePlatform(targetId, sessionSerial, PlatformLiveState::Testing, QStringLiteral("YouTube monitor is testing"));
			emit statusMessage(QString("%1: YouTube monitor is testing. Switching to live...").arg(target->name));
			completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
			scheduleYouTubePoll(targetId, sessionSerial, operationGeneration, attempt + 1, 3000);
			return;
		}

		setRuntimePlatform(targetId, sessionSerial, PlatformLiveState::LiveStarting,
				   QStringLiteral("YouTube accepted the live transition - confirming status"));
		emit statusMessage(QString("%1: YouTube accepted the live transition. Confirming...").arg(target->name));
		completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
		scheduleYouTubePoll(targetId, sessionSerial, operationGeneration, attempt + 1, 3000);
	});
}

void OutputManager::armYouTubeArchiveRotation(const QString &targetId, quint64 sessionSerial,
					      const QJsonObject &currentBroadcast)
{
	OutputTarget *target = findTarget(targetId);
	Session *session = sessionForTarget(targetId);
	if (!target || !session || session->serial != sessionSerial ||
	    target->youtubeBroadcastMode != YouTubeBroadcastMode::ArchiveRotation ||
	    target->authMode != TargetAuthMode::YouTubeOAuth)
		return;

	const QString currentBroadcastId = currentBroadcast.value(QStringLiteral("id")).toString().trimmed();
	const QString streamId = currentBroadcast.value(QStringLiteral("contentDetails"))
					 .toObject()
					 .value(QStringLiteral("boundStreamId"))
					 .toString()
					 .trimmed();
	if (currentBroadcastId.isEmpty() || streamId.isEmpty()) {
		logWarning(QStringLiteral("%1: YouTube archive rotation was not armed because the live broadcast or bound stream ID was missing.")
				   .arg(target->name));
		return;
	}

	const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
	const qint64 fallbackStartMs = youtubeArchiveRotationFallbackStartMs(
		session->youtubeRotationCurrentCompleted, session->startedAtMs, nowMs);
	const qint64 actualStartMs = youtubeBroadcastActualStartMs(
		currentBroadcast, fallbackStartMs);
	const qint64 delayMs = youtubeArchiveRotationDelayMs(actualStartMs, nowMs);
	session->youtubeRotationCurrentBroadcast = currentBroadcast;
	session->youtubeRotationCurrentBroadcastId = currentBroadcastId;
	session->youtubeRotationStreamId = streamId;
	session->youtubeRotationNextBroadcastId.clear();
	session->youtubeRotationNextPart = youtubeNextArchivePart(
		currentBroadcast.value(QStringLiteral("snippet")).toObject().value(QStringLiteral("title")).toString());
	session->youtubeRotationCurrentCompleted = false;
	session->youtubeRotationPolls = 0;
	const quint64 timerGeneration = ++session->youtubeRotationTimerGeneration;
	const int safeDelayMs = static_cast<int>(qMin<qint64>(delayMs, std::numeric_limits<int>::max()));
	logInfo(QStringLiteral("%1: YouTube archive rotation armed for broadcast %2 in %3 minute(s).")
			.arg(target->name, broadcastIdForLog(currentBroadcastId))
			.arg((delayMs + 59999) / 60000));
	QTimer::singleShot(safeDelayMs, this, [this, targetId, sessionSerial, timerGeneration]() {
		beginYouTubeArchiveRotation(targetId, sessionSerial, timerGeneration);
	});
}

void OutputManager::beginYouTubeArchiveRotation(const QString &targetId, quint64 sessionSerial,
						quint64 timerGeneration)
{
	OutputTarget *target = findTarget(targetId);
	Session *session = sessionForTarget(targetId);
	if (!target || !session || session->serial != sessionSerial ||
	    session->youtubeRotationTimerGeneration != timerGeneration || session->youtubeOperationInFlight ||
	    target->youtubeBroadcastMode != YouTubeBroadcastMode::ArchiveRotation ||
	    target->authMode != TargetAuthMode::YouTubeOAuth ||
	    !canContinuePlatformStart(*target, runtimeStatusForTarget(targetId)))
		return;
	if (session->youtubeRotationCurrentBroadcastId.isEmpty() || session->youtubeRotationStreamId.isEmpty())
		return;

	++session->youtubePollGeneration;
	session->youtubeOperationInFlight = true;
	const quint64 operationGeneration = ++session->youtubeOperationGeneration;
	session->youtubeRotationCurrentCompleted = false;
	session->youtubeRotationPolls = 0;
	setRuntimePlatform(targetId, sessionSerial, PlatformLiveState::Live,
			   QStringLiteral("YouTube Live - preparing the next archive"));
	emit statusMessage(QStringLiteral("%1: Preparing the next YouTube archive while the current broadcast stays live.")
				   .arg(target->name));

	const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
	if (!session->youtubeAccessToken.isEmpty() && session->youtubeAccessTokenExpiresAtMs > nowMs + 30000) {
		createYouTubeRotationBroadcast(targetId, sessionSerial, operationGeneration,
					       session->youtubeAccessToken);
		return;
	}
	refreshYouTubeRotationAccessToken(targetId, sessionSerial, operationGeneration);
}

void OutputManager::refreshYouTubeRotationAccessToken(const QString &targetId, quint64 sessionSerial,
						      quint64 operationGeneration)
{
	OutputTarget *target = findTarget(targetId);
	if (!target || !youtubeOperationMatches(targetId, sessionSerial, operationGeneration))
		return;
	const OAuthClientCredentials credentials = oauthEffectiveClientCredentials(
		target->authMode, target->oauthClientId, target->oauthClientSecret);
	if (!credentials.isComplete() || target->oauthRefreshToken.trimmed().isEmpty()) {
		failYouTubeArchiveRotation(targetId, sessionSerial, operationGeneration,
					   QStringLiteral("YouTube login credentials are unavailable."));
		return;
	}

	QUrlQuery body;
	body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
	body.addQueryItem(QStringLiteral("client_id"), credentials.clientId);
	body.addQueryItem(QStringLiteral("client_secret"), credentials.clientSecret);
	body.addQueryItem(QStringLiteral("refresh_token"), target->oauthRefreshToken);
	HttpRequest request;
	request.url = QUrl(QStringLiteral("https://oauth2.googleapis.com/token"));
	request.method = QByteArrayLiteral("POST");
	request.timeoutMs = PlatformApiTimeoutMs;
	request.headers.push_back({QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/x-www-form-urlencoded")});
	request.body = formBody(body);
	http_->send(std::move(request), [this, targetId, sessionSerial, operationGeneration](HttpResponse response) {
		if (!youtubeOperationMatches(targetId, sessionSerial, operationGeneration))
			return;
		const QString error = platformHttpError(response);
		if (!error.isEmpty()) {
			failYouTubeArchiveRotation(targetId, sessionSerial, operationGeneration,
						   QStringLiteral("access-token refresh failed: %1").arg(error));
			return;
		}
		const QJsonObject tokenResponse = QJsonDocument::fromJson(response.body).object();
		const QString accessToken = tokenResponse.value(QStringLiteral("access_token")).toString();
		Session *session = sessionForTarget(targetId);
		if (!session || session->serial != sessionSerial || accessToken.isEmpty()) {
			failYouTubeArchiveRotation(targetId, sessionSerial, operationGeneration,
						   QStringLiteral("access-token refresh returned no token."));
			return;
		}
		const int expiresInSeconds = qMax(60, tokenResponse.value(QStringLiteral("expires_in")).toInt(3600));
		session->youtubeAccessToken = accessToken;
		session->youtubeAccessTokenExpiresAtMs =
			QDateTime::currentMSecsSinceEpoch() + static_cast<qint64>(expiresInSeconds) * 1000;
		createYouTubeRotationBroadcast(targetId, sessionSerial, operationGeneration, accessToken);
	});
}

void OutputManager::createYouTubeRotationBroadcast(const QString &targetId, quint64 sessionSerial,
						    quint64 operationGeneration,
						    const QString &accessToken)
{
	OutputTarget *target = findTarget(targetId);
	Session *session = sessionForTarget(targetId);
	if (!target || !session || !youtubeOperationMatches(targetId, sessionSerial, operationGeneration))
		return;

	QUrl url(QStringLiteral("https://www.googleapis.com/youtube/v3/liveBroadcasts"));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("part"), QStringLiteral("snippet,status,contentDetails"));
	url.setQuery(query);
	const QString scheduledStart = QDateTime::currentDateTimeUtc().addSecs(5).toString(Qt::ISODate);
	const QJsonObject body = youtubeArchiveBroadcastInsertBody(
		session->youtubeRotationCurrentBroadcast, session->youtubeRotationNextPart, scheduledStart);
	HttpRequest request;
	request.url = url;
	request.method = QByteArrayLiteral("POST");
	request.timeoutMs = PlatformApiTimeoutMs;
	request.headers.push_back({QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + accessToken.toUtf8()});
	request.headers.push_back({QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json")});
	request.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
	logInfo(QStringLiteral("%1: Creating YouTube archive part %2 while the current broadcast remains live.")
			.arg(target->name)
			.arg(session->youtubeRotationNextPart));
	http_->send(std::move(request), [this, targetId, sessionSerial, operationGeneration, accessToken](HttpResponse response) {
		if (!youtubeOperationMatches(targetId, sessionSerial, operationGeneration))
			return;
		const QString error = platformHttpError(response);
		if (!error.isEmpty()) {
			failYouTubeArchiveRotation(targetId, sessionSerial, operationGeneration,
						   QStringLiteral("next broadcast creation failed: %1").arg(error));
			return;
		}
		const QString nextBroadcastId = QJsonDocument::fromJson(response.body)
						 .object()
						 .value(QStringLiteral("id"))
						 .toString()
						 .trimmed();
		if (nextBroadcastId.isEmpty()) {
			failYouTubeArchiveRotation(targetId, sessionSerial, operationGeneration,
						   QStringLiteral("next broadcast creation returned no ID."));
			return;
		}
		if (Session *session = sessionForTarget(targetId))
			session->youtubeRotationNextBroadcastId = nextBroadcastId;
		bindYouTubeRotationBroadcast(targetId, sessionSerial, operationGeneration, accessToken,
					     nextBroadcastId);
	});
}

void OutputManager::bindYouTubeRotationBroadcast(const QString &targetId, quint64 sessionSerial,
						  quint64 operationGeneration,
						  const QString &accessToken,
						  const QString &nextBroadcastId)
{
	Session *session = sessionForTarget(targetId);
	if (!session || !youtubeOperationMatches(targetId, sessionSerial, operationGeneration))
		return;
	QUrl url(QStringLiteral("https://www.googleapis.com/youtube/v3/liveBroadcasts/bind"));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("part"), QStringLiteral("id,snippet,contentDetails,status"));
	query.addQueryItem(QStringLiteral("id"), nextBroadcastId);
	query.addQueryItem(QStringLiteral("streamId"), session->youtubeRotationStreamId);
	url.setQuery(query);
	HttpRequest request;
	request.url = url;
	request.method = QByteArrayLiteral("POST");
	request.timeoutMs = PlatformApiTimeoutMs;
	request.headers.push_back({QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + accessToken.toUtf8()});
	http_->send(std::move(request), [this, targetId, sessionSerial, operationGeneration, accessToken](HttpResponse response) {
		if (!youtubeOperationMatches(targetId, sessionSerial, operationGeneration))
			return;
		const QString error = platformHttpError(response);
		if (!error.isEmpty()) {
			failYouTubeArchiveRotation(targetId, sessionSerial, operationGeneration,
						   QStringLiteral("next broadcast binding failed: %1").arg(error));
			return;
		}
		completeCurrentYouTubeRotationBroadcast(targetId, sessionSerial, operationGeneration, accessToken);
	});
}

void OutputManager::completeCurrentYouTubeRotationBroadcast(const QString &targetId, quint64 sessionSerial,
							     quint64 operationGeneration,
							     const QString &accessToken)
{
	Session *session = sessionForTarget(targetId);
	if (!session || !youtubeOperationMatches(targetId, sessionSerial, operationGeneration))
		return;
	QUrl url(QStringLiteral("https://www.googleapis.com/youtube/v3/liveBroadcasts/transition"));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("part"), QStringLiteral("id,status"));
	query.addQueryItem(QStringLiteral("id"), session->youtubeRotationCurrentBroadcastId);
	query.addQueryItem(QStringLiteral("broadcastStatus"), QStringLiteral("complete"));
	url.setQuery(query);
	HttpRequest request;
	request.url = url;
	request.method = QByteArrayLiteral("POST");
	request.timeoutMs = PlatformApiTimeoutMs;
	request.headers.push_back({QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + accessToken.toUtf8()});
	http_->send(std::move(request), [this, targetId, sessionSerial, operationGeneration, accessToken](HttpResponse response) {
		if (!youtubeOperationMatches(targetId, sessionSerial, operationGeneration))
			return;
		const QString error = platformHttpError(response);
		if (!error.isEmpty() && !youtubeApiErrorHasAnyReason(response, {"redundantTransition"})) {
			if (isRetryableYouTubeResponse(response, error)) {
				failYouTubeArchiveRotation(targetId, sessionSerial, operationGeneration,
							   QStringLiteral("current broadcast completion is being confirmed."), true);
				return;
			}
			failYouTubeArchiveRotation(targetId, sessionSerial, operationGeneration,
						   QStringLiteral("current broadcast completion failed: %1").arg(error));
			return;
		}
		Session *session = sessionForTarget(targetId);
		if (!session)
			return;
		session->youtubeRotationPolls = 0;
		pollYouTubeRotationBroadcast(targetId, sessionSerial, operationGeneration, accessToken,
					     session->youtubeRotationCurrentBroadcastId, true);
	});
}

void OutputManager::pollYouTubeRotationBroadcast(const QString &targetId, quint64 sessionSerial,
						 quint64 operationGeneration,
						 const QString &accessToken,
						 const QString &broadcastId,
						 bool waitingForCurrentComplete)
{
	if (!youtubeOperationMatches(targetId, sessionSerial, operationGeneration) || broadcastId.isEmpty())
		return;
	QUrl url(QStringLiteral("https://www.googleapis.com/youtube/v3/liveBroadcasts"));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("part"), QStringLiteral("id,snippet,contentDetails,status"));
	query.addQueryItem(QStringLiteral("id"), broadcastId);
	url.setQuery(query);
	HttpRequest request;
	request.url = url;
	request.timeoutMs = PlatformApiTimeoutMs;
	request.headers.push_back({QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + accessToken.toUtf8()});
	http_->send(std::move(request), [this, targetId, sessionSerial, operationGeneration, accessToken, broadcastId,
					 waitingForCurrentComplete](HttpResponse response) {
		Session *session = sessionForTarget(targetId);
		if (!session || !youtubeOperationMatches(targetId, sessionSerial, operationGeneration))
			return;
		const QString error = platformHttpError(response);
		const QJsonArray items = QJsonDocument::fromJson(response.body).object().value(QStringLiteral("items")).toArray();
		const QJsonObject broadcast = items.isEmpty() ? QJsonObject() : items.first().toObject();
		const QString lifecycle = broadcast.value(QStringLiteral("status"))
						  .toObject()
						  .value(QStringLiteral("lifeCycleStatus"))
						  .toString();
		if (!error.isEmpty() || broadcast.isEmpty()) {
			if (++session->youtubeRotationPolls >= YouTubeArchiveTransitionMaxPolls) {
				failYouTubeArchiveRotation(targetId, sessionSerial, operationGeneration,
							   QStringLiteral("YouTube did not confirm the archive transition."),
							   waitingForCurrentComplete);
				return;
			}
			QTimer::singleShot(YouTubeArchiveTransitionPollMs, this,
				[this, targetId, sessionSerial, operationGeneration, accessToken, broadcastId,
				 waitingForCurrentComplete]() {
					pollYouTubeRotationBroadcast(targetId, sessionSerial, operationGeneration,
								     accessToken, broadcastId,
								     waitingForCurrentComplete);
				});
			return;
		}

		if (waitingForCurrentComplete) {
			if (lifecycle == QStringLiteral("complete")) {
				session->youtubeRotationCurrentCompleted = true;
				session->youtubeRotationPolls = 0;
				startNextYouTubeRotationBroadcast(targetId, sessionSerial, operationGeneration, accessToken);
				return;
			}
			if (++session->youtubeRotationPolls >= YouTubeArchiveTransitionMaxPolls) {
				failYouTubeArchiveRotation(targetId, sessionSerial, operationGeneration,
							   QStringLiteral("the current broadcast did not finish in time."),
							   lifecycle != QStringLiteral("live"));
				return;
			}
		} else if (lifecycle == QStringLiteral("live")) {
			ensureRuntimeStatus(targetId).broadcastId = broadcastId;
			completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
			if (OutputTarget *target = findTarget(targetId)) {
				if (target->lastError.startsWith(QStringLiteral("YouTube archive rotation")))
					target->lastError.clear();
				setRuntimePlatform(targetId, sessionSerial, PlatformLiveState::Live,
						   QStringLiteral("YouTube broadcast is live - archive split complete"));
				emit statusMessage(QStringLiteral("%1: YouTube archive switched successfully; RTMP stayed connected.")
							   .arg(target->name));
			}
			notifyCommentViewerYouTubeStarted(targetId, sessionSerial, broadcastId);
			armYouTubeArchiveRotation(targetId, sessionSerial, broadcast);
			return;
		} else if (++session->youtubeRotationPolls >= YouTubeArchiveTransitionMaxPolls) {
			failYouTubeArchiveRotation(targetId, sessionSerial, operationGeneration,
						   QStringLiteral("the next broadcast did not become live."), true);
			return;
		}

		QTimer::singleShot(YouTubeArchiveTransitionPollMs, this,
			[this, targetId, sessionSerial, operationGeneration, accessToken, broadcastId,
			 waitingForCurrentComplete]() {
				pollYouTubeRotationBroadcast(targetId, sessionSerial, operationGeneration, accessToken,
							     broadcastId, waitingForCurrentComplete);
			});
	});
}

void OutputManager::startNextYouTubeRotationBroadcast(const QString &targetId, quint64 sessionSerial,
						       quint64 operationGeneration,
						       const QString &accessToken)
{
	Session *session = sessionForTarget(targetId);
	if (!session || !youtubeOperationMatches(targetId, sessionSerial, operationGeneration) ||
	    session->youtubeRotationNextBroadcastId.isEmpty())
		return;
	setRuntimePlatform(targetId, sessionSerial, PlatformLiveState::LiveStarting,
			   QStringLiteral("Current archive saved - starting the next YouTube broadcast"));
	QUrl url(QStringLiteral("https://www.googleapis.com/youtube/v3/liveBroadcasts/transition"));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("part"), QStringLiteral("id,snippet,contentDetails,status"));
	query.addQueryItem(QStringLiteral("id"), session->youtubeRotationNextBroadcastId);
	query.addQueryItem(QStringLiteral("broadcastStatus"), QStringLiteral("live"));
	url.setQuery(query);
	HttpRequest request;
	request.url = url;
	request.method = QByteArrayLiteral("POST");
	request.timeoutMs = PlatformApiTimeoutMs;
	request.headers.push_back({QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + accessToken.toUtf8()});
	http_->send(std::move(request), [this, targetId, sessionSerial, operationGeneration, accessToken](HttpResponse response) {
		Session *session = sessionForTarget(targetId);
		if (!session || !youtubeOperationMatches(targetId, sessionSerial, operationGeneration))
			return;
		const QString error = platformHttpError(response);
		if (!error.isEmpty() && !youtubeApiErrorHasAnyReason(response, {"redundantTransition"})) {
			if ((isRetryableYouTubeResponse(response, error) ||
			     youtubeApiErrorHasAnyReason(response, {"errorStreamInactive", "invalidTransition"})) &&
			    ++session->youtubeRotationPolls < YouTubeArchiveTransitionMaxPolls) {
				QTimer::singleShot(YouTubeArchiveTransitionPollMs, this,
					[this, targetId, sessionSerial, operationGeneration, accessToken]() {
						startNextYouTubeRotationBroadcast(targetId, sessionSerial, operationGeneration,
									      accessToken);
					});
				return;
			}
			failYouTubeArchiveRotation(targetId, sessionSerial, operationGeneration,
						   QStringLiteral("next broadcast start failed: %1").arg(error), true);
			return;
		}
		session->youtubeRotationPolls = 0;
		pollYouTubeRotationBroadcast(targetId, sessionSerial, operationGeneration, accessToken,
					     session->youtubeRotationNextBroadcastId, false);
	});
}

void OutputManager::failYouTubeArchiveRotation(const QString &targetId, quint64 sessionSerial,
						quint64 operationGeneration,
						const QString &message,
						bool currentBroadcastMayBeComplete,
						bool discardPreparedBroadcast)
{
	OutputTarget *target = findTarget(targetId);
	Session *session = sessionForTarget(targetId);
	if (!target || !session || !youtubeOperationMatches(targetId, sessionSerial, operationGeneration))
		return;
	if (!currentBroadcastMayBeComplete && discardPreparedBroadcast &&
	    !session->youtubeRotationNextBroadcastId.isEmpty()) {
		const QString preparedBroadcastId = session->youtubeRotationNextBroadcastId;
		session->youtubeRotationNextBroadcastId.clear();
		QUrl url(QStringLiteral("https://www.googleapis.com/youtube/v3/liveBroadcasts"));
		QUrlQuery query;
		query.addQueryItem(QStringLiteral("id"), preparedBroadcastId);
		url.setQuery(query);
		HttpRequest request;
		request.url = url;
		request.method = QByteArrayLiteral("DELETE");
		request.timeoutMs = PlatformApiTimeoutMs;
		request.headers.push_back({QByteArrayLiteral("Authorization"),
					   QByteArrayLiteral("Bearer ") + session->youtubeAccessToken.toUtf8()});
		http_->send(std::move(request),
			    [this, targetId, sessionSerial, operationGeneration, message](HttpResponse response) {
			if (!response.isSuccess())
				logWarning(QStringLiteral("DSK could not remove an unused YouTube archive frame after a safe rotation failure."));
			failYouTubeArchiveRotation(targetId, sessionSerial, operationGeneration, message, false, false);
		});
		return;
	}
	const QString technical = QStringLiteral("YouTube archive rotation failed: %1").arg(message);
	target->lastError = technical;
	logWarning(QStringLiteral("%1: %2").arg(target->name, technical));
	const YouTubeArchiveFailureAction failureAction = youtubeArchiveFailureAction(
		currentBroadcastMayBeComplete, session->youtubeRotationCurrentCompleted,
		session->youtubeRotationPolls, YouTubeArchiveTransitionMaxPolls);
	if (failureAction != YouTubeArchiveFailureAction::RetryLaterCurrentLive) {
		if (failureAction == YouTubeArchiveFailureAction::NeedsAttention) {
			completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
			setRuntimePlatform(targetId, sessionSerial, PlatformLiveState::Failed,
					   QStringLiteral("YouTube archive change needs attention"), technical);
			emit statusMessage(QStringLiteral("%1: The current archive may be closed, but the next frame could not be confirmed. Check YouTube Studio.")
					   .arg(target->name));
			return;
		}
		setRuntimePlatform(targetId, sessionSerial, PlatformLiveState::LiveStarting,
				   QStringLiteral("Confirming the archive change before retrying"), technical);
		QTimer::singleShot(YouTubeArchiveTransitionPollMs, this,
			[this, targetId, sessionSerial, operationGeneration]() {
				Session *current = sessionForTarget(targetId);
				if (!current || !youtubeOperationMatches(targetId, sessionSerial, operationGeneration))
					return;
				pollYouTubeRotationBroadcast(targetId, sessionSerial, operationGeneration,
							     current->youtubeAccessToken,
							     current->youtubeRotationCurrentBroadcastId, true);
			});
		return;
	}

	completeYouTubeOperation(targetId, sessionSerial, operationGeneration);
	setRuntimePlatform(targetId, sessionSerial, PlatformLiveState::Live,
			   QStringLiteral("YouTube Live - archive split will retry in 5 minutes"), technical);
	emit statusMessage(QStringLiteral("%1: Archive splitting failed safely; the current YouTube broadcast remains live. Retrying in 5 minutes.")
				   .arg(target->name));
	const quint64 timerGeneration = ++session->youtubeRotationTimerGeneration;
	QTimer::singleShot(YouTubeArchiveRotationRetryMs, this,
			   [this, targetId, sessionSerial, timerGeneration]() {
				   beginYouTubeArchiveRotation(targetId, sessionSerial, timerGeneration);
			   });
	if (!runtimeTargetIds_.contains(targetId))
		emit targetsChanged();
}

void OutputManager::setTargetApiWarning(const QString &targetId, const QString &message, quint64 sessionSerial)
{
	OutputTarget *target = findTarget(targetId);
	if (!target || !sessionMatches(targetId, sessionSerial))
		return;
	Session *session = sessionForTarget(targetId);
	const bool terminatePreflight =
		session && session->serial == sessionSerial && session->youtubePreflight &&
		!session->youtubeAwaitingSelection;
	const QString userMessage =
		session && session->youtubePreflight ? userFacingYouTubePreflightWarningText(message)
						    : userFacingYouTubeApiWarningText(message);
	const bool changed = target->lastError != message;
	target->lastError = message;
	const TargetRuntimeStatus runtime = runtimeStatusForTarget(targetId);
	setRuntimePlatform(targetId,
			   sessionSerial,
			   platformStateForYouTubeApiWarningText(message),
			   runtimeTransportIsRunning(runtime) || target->state == TargetState::Live ? userMessage
												    : QString(),
			   message);
	logWarning(QString("%1: %2").arg(target->name, message));
	if (!runtimeTargetIds_.contains(targetId))
		emit statusMessage(QString("%1: %2").arg(target->name, userMessage));
	if (terminatePreflight) {
		target->state = TargetState::Error;
		setRuntimeTransport(targetId,
				    sessionSerial,
				    TransportState::Failed,
				    QStringLiteral("YouTube start blocked before RTMP"));
		releaseSession(targetId, false, sessionSerial);
	}
	if (changed && !runtimeTargetIds_.contains(targetId))
		emit targetsChanged();
}

void OutputManager::clearTargetApiWarning(const QString &targetId)
{
	OutputTarget *target = findTarget(targetId);
	if (!target)
		return;
	if (!isYouTubeApiWarningText(target->lastError))
		return;
	target->lastError.clear();
	const quint64 serial = runtimeStatusForTarget(targetId).sessionSerial;
	if (runtimeTransportIsRunning(runtimeStatusForTarget(targetId)) || target->state == TargetState::Live)
		setRuntimePlatform(targetId, serial, PlatformLiveState::RtmpSignalOnly,
				   QStringLiteral("RTMP connected - checking YouTube Live"));
	if (!runtimeTargetIds_.contains(targetId))
		emit targetsChanged();
}

bool OutputManager::setTargetError(OutputTarget &target, const QString &message)
{
	target.state = TargetState::Error;
	target.lastError = message;
	TargetRuntimeStatus &status = ensureRuntimeStatus(target.id);
	status.sessionSerial = 0;
	status.transport = TransportState::Failed;
	status.platform = isYouTubeTarget(target) ? PlatformLiveState::Failed : PlatformLiveState::NotApplicable;
	status.lastChangedAtMs = QDateTime::currentMSecsSinceEpoch();
	status.lastUserMessage = message;
	status.lastTechnicalError = message;
	logWarning(QString("%1: %2").arg(target.name, message));
	if (!runtimeTargetIds_.contains(target.id))
		emit statusMessage(QString("%1: %2").arg(target.name, message));
	emit targetRuntimeChanged(target.id);
	return false;
}

void OutputManager::handleOutputSignal(obs_output_t *output, quint64 expectedSerial, const QString &signalName,
				       int reconnectDelaySeconds)
{
	if (!output || expectedSerial == 0)
		return;

	Session *matched = nullptr;
	for (Session *session : sessions_) {
		if (session && session->output == output && session->serial == expectedSerial) {
			matched = session;
			break;
		}
	}
	if (!matched)
		return;

	OutputTarget *target = findTarget(matched->targetId);
	if (!target)
		return;
	if (matched->pendingRelease && shouldIgnoreOutputSignalDuringPendingRelease(signalName))
		return;

	const quint64 serial = matched->serial;
	if (signalName == QStringLiteral("starting")) {
		target->state = TargetState::Starting;
		setRuntimeTransport(target->id, serial, TransportState::Starting, QStringLiteral("Connecting"));
	} else if (signalName == QStringLiteral("start")) {
		target->state = TargetState::Live;
		matched->youtubeSignalActiveAtMs = QDateTime::currentMSecsSinceEpoch();
		setRuntimeTransport(target->id, serial, TransportState::Connected, QStringLiteral("RTMP sending"));
		const PlatformLiveState platform = runtimeStatusForTarget(target->id).platform;
		if (isYouTubeTarget(*target) &&
		    (platform == PlatformLiveState::Unknown || platform == PlatformLiveState::RtmpSignalOnly))
			setRuntimePlatform(target->id, serial, PlatformLiveState::RtmpSignalOnly,
					   QStringLiteral("RTMP connected - checking YouTube Live"));
		if (isYouTubeTarget(*target))
			maybeStartYouTubeBroadcast(target->id, serial);
	} else if (signalName == QStringLiteral("activate")) {
		target->state = TargetState::Live;
		setRuntimeTransport(target->id, serial, TransportState::Active, QStringLiteral("RTMP sending"));
		const PlatformLiveState platform = runtimeStatusForTarget(target->id).platform;
		if (isYouTubeTarget(*target) &&
		    (platform == PlatformLiveState::Unknown || platform == PlatformLiveState::RtmpSignalOnly))
			setRuntimePlatform(target->id, serial, PlatformLiveState::RtmpSignalOnly,
					   QStringLiteral("RTMP connected - checking YouTube Live"));
	} else if (signalName == QStringLiteral("reconnect")) {
		target->state = TargetState::Live;
		const bool keepArchiveRotationOperation = isYouTubeTarget(*target) &&
			target->youtubeBroadcastMode == YouTubeBroadcastMode::ArchiveRotation &&
			matched->youtubeOperationInFlight;
		if (isYouTubeTarget(*target) && !keepArchiveRotationOperation) {
			++matched->youtubeOperationGeneration;
			++matched->youtubePollGeneration;
			matched->youtubeOperationInFlight = false;
		}
		setRuntimeTransport(target->id,
				    serial,
				    TransportState::Reconnecting,
				    reconnectDelaySeconds > 0 ? QStringLiteral("Reconnecting in %1 sec").arg(reconnectDelaySeconds)
							      : QStringLiteral("Reconnecting"),
				    reconnectDelaySeconds);
	} else if (signalName == QStringLiteral("reconnect_success")) {
		target->state = TargetState::Live;
		matched->youtubeSignalActiveAtMs = QDateTime::currentMSecsSinceEpoch();
		setRuntimeTransport(target->id, serial, TransportState::Connected, QStringLiteral("RTMP reconnected"));
		if (isYouTubeTarget(*target))
			maybeStartYouTubeBroadcast(target->id, serial);
	} else if (signalName == QStringLiteral("stopping")) {
		target->state = TargetState::Stopping;
		setRuntimeTransport(target->id, serial, TransportState::Stopping, QStringLiteral("Stopping"));
	} else if (signalName == QStringLiteral("deactivate")) {
		updateRuntimeStats(target->id);
	}

	if (!runtimeTargetIds_.contains(target->id))
		emit targetsChanged();
}

void OutputManager::handleOutputStopped(obs_output_t *output, quint64 expectedSerial, int code,
					const QString &lastError)
{
	if (!output || expectedSerial == 0)
		return;

	QString targetId;
	Session *stoppedSession = nullptr;
	for (Session *session : sessions_) {
		if (session && session->output == output && session->serial == expectedSerial) {
			targetId = session->targetId;
			stoppedSession = session;
			break;
		}
	}
	if (targetId.isEmpty())
		return;

	OutputTarget *target = findTarget(targetId);
	if (target) {
		const quint64 serial = stoppedSession ? stoppedSession->serial : runtimeStatusForTarget(targetId).sessionSerial;
		if (code == OBS_OUTPUT_SUCCESS) {
			target->state = TargetState::Stopped;
			target->lastError.clear();
			resetRuntimeStatus(targetId);
			logInfo(QString("Stopped %1").arg(target->name));
			if (!runtimeTargetIds_.contains(target->id))
				emit statusMessage(QString("Stopped %1").arg(target->name));
		} else {
			const QString message = outputStopMessage(code, lastError);
			target->state = TargetState::Stopped;
			target->lastError = message;
			setRuntimeTransport(targetId, serial, TransportState::Failed, message);
			setRuntimePlatform(targetId, serial, isYouTubeTarget(*target) ? PlatformLiveState::Failed : PlatformLiveState::NotApplicable,
					   message, message);
			logWarning(QString("%1 failed: %2").arg(target->name, message));
			if (!runtimeTargetIds_.contains(target->id))
				emit statusMessage(QString("%1 failed: %2").arg(target->name, message));
		}
	}

	releaseSession(stoppedSession, false);
	if (!runtimeTargetIds_.contains(targetId))
		emit targetsChanged();
}

OutputManager::Session *OutputManager::sessionForTarget(const QString &id) const
{
	for (Session *session : sessions_) {
		if (session && session->targetId == id)
			return session;
	}
	return nullptr;
}

void OutputManager::releaseSession(const QString &id, bool requestStop)
{
	releaseSession(id, requestStop, 0);
}

void OutputManager::releaseSession(const QString &id, bool requestStop, quint64 expectedSerial)
{
	for (int i = sessions_.size() - 1; i >= 0; --i) {
		Session *session = sessions_[i];
		if (!session || session->targetId != id)
			continue;
		if (expectedSerial != 0 && session->serial != expectedSerial)
			continue;

		releaseSession(session, requestStop);
		break;
	}
}

void OutputManager::releaseSession(Session *session, bool requestStop)
{
	const int i = sessions_.indexOf(session);
	if (i < 0 || !session)
		return;

	const QString targetId = session->targetId;
	const quint64 sessionSerial = session->serial;
	if (session->output) {
		const bool active = obs_output_active(session->output);
		if (shuttingDown_) {
			disconnectOutputSignals(session->output, this);
			if (active)
				obs_output_force_stop(session->output);
			logInfo(QString("Releasing output resources for %1 during shutdown").arg(targetId));
			releaseOutputAndService(session->output, session->service);
			releaseSharedEncoders(session->sharedEncoderKey);
			session->output = nullptr;
			session->service = nullptr;
			delete session;
			sessions_.removeAt(i);
			return;
		}
		if (requestStop) {
			if (session->pendingRelease)
				return;

			// A start request can still be connecting while obs_output_active()
			// reports false. Request cancellation immediately instead of waiting
			// for it to become active and relying on the delayed force-stop path.
			session->pendingRelease = true;
			session->releasePolls = 0;
			logInfo(QString("Stopping runtime output for %1").arg(targetId));
			obs_output_stop(session->output);
			QTimer::singleShot(1000, this, [this, targetId, sessionSerial]() { releaseSession(targetId, false, sessionSerial); });
			return;
		}
		if (session->output) {
			if (obs_output_active(session->output)) {
				if (++session->releasePolls >= 5) {
					logWarning(QString("Force stopping output for %1 after delayed release wait.").arg(targetId));
					obs_output_force_stop(session->output);
					session->releasePolls = 0;
				}
				QTimer::singleShot(1000, this, [this, targetId, sessionSerial]() { releaseSession(targetId, false, sessionSerial); });
				return;
			}
			logInfo(QString("Releasing output resources for %1").arg(targetId));
			disconnectOutputSignals(session->output, this);
			releaseOutputAndService(session->output, session->service);
			releaseSharedEncoders(session->sharedEncoderKey);
			session->output = nullptr;
			session->service = nullptr;
		}
	}

	delete session;
	sessions_.removeAt(i);
	releaseTargetSceneCanvas(targetId);

	if (OutputTarget *target = findTarget(targetId)) {
		if (finalizePendingRemoval(targetId))
			return;
		if (target->state == TargetState::Stopping && !sessionForTarget(targetId)) {
			target->state = TargetState::Stopped;
			target->lastError.clear();
			resetRuntimeStatus(targetId);
			if (!runtimeTargetIds_.contains(targetId))
				emit targetsChanged();
		}
	}
}

void OutputManager::releaseAllSessionsNow()
{
	for (int i = sessions_.size() - 1; i >= 0; --i) {
		Session *session = sessions_[i];
		if (!session) {
			sessions_.removeAt(i);
			continue;
		}

		if (session->output) {
			disconnectOutputSignals(session->output, this);
			if (obs_output_active(session->output))
				obs_output_force_stop(session->output);
			logInfo(QString("Releasing output resources for %1 during reset or shutdown").arg(session->targetId));
			releaseOutputAndService(session->output, session->service);
			releaseSharedEncoders(session->sharedEncoderKey);
			session->output = nullptr;
			session->service = nullptr;
		}
		delete session;
		sessions_.removeAt(i);
	}
}

obs_service_t *OutputManager::createService(const OutputTarget &target, const QString &streamKey)
{
	obs_data_t *serviceSettings = obs_data_create();
	obs_data_set_string(serviceSettings, "server", target.serverUrl.toUtf8().constData());
	obs_data_set_string(serviceSettings, "key", streamKey.toUtf8().constData());

	const QByteArray serviceName = QString("dsk_service_%1").arg(target.id).toUtf8();
	obs_service_t *service = obs_service_create("rtmp_custom", serviceName.constData(), serviceSettings, nullptr);
	obs_data_release(serviceSettings);
	return service;
}

obs_output_t *OutputManager::createOutput(const OutputTarget &target, obs_service_t *service, quint64 sessionSerial)
{
	const char *outputType = obs_service_get_preferred_output_type(service);
	if (!outputType || !*outputType)
		outputType = "rtmp_output";

	const QByteArray outputName = QString("dsk_output_%1_session_%2").arg(target.id).arg(sessionSerial).toUtf8();
	obs_output_t *output = obs_output_create(outputType, outputName.constData(), nullptr, nullptr);
	if (!output)
		return nullptr;

	obs_output_set_service(output, service);
	obs_output_set_reconnect_settings(output, target.reconnectEnabled ? target.reconnectMaxRetries : 0, target.reconnectDelaySeconds);
	applyProfileDelay(output);
	return output;
}

obs_encoder_t *OutputManager::createVideoEncoder(const OutputTarget &target, const EncoderProfile &profile)
{
	const QString encoderId = profile.videoEncoderId.isEmpty() ? QStringLiteral("obs_x264") : profile.videoEncoderId;
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "rate_control", "CBR");
	obs_data_set_int(settings, "bitrate", profile.videoBitrateKbps);
	obs_data_set_int(settings, "max_bitrate", profile.videoBitrateKbps);
	obs_data_set_int(settings, "keyint_sec", profile.keyframeSeconds);
	obs_data_set_string(settings, "profile", "high");

	if (isNativeNvencEncoder(encoderId)) {
		obs_data_set_string(settings, "preset", "p5");
		obs_data_set_string(settings, "tune", "hq");
		obs_data_set_string(settings, "multipass", "qres");
		obs_data_set_bool(settings, "adaptive_quantization", true);
		obs_data_set_bool(settings, "lookahead", false);
		obs_data_set_int(settings, "bf", 2);
	} else if (isFfmpegNvencEncoder(encoderId)) {
		obs_data_set_string(settings, "preset2", "p5");
		obs_data_set_string(settings, "tune", "hq");
		obs_data_set_string(settings, "multipass", "qres");
		obs_data_set_bool(settings, "psycho_aq", true);
		obs_data_set_int(settings, "gpu", 0);
		obs_data_set_int(settings, "bf", 2);
	} else if (encoderId == QStringLiteral("obs_x264")) {
		obs_data_set_string(settings, "preset", "veryfast");
	}

	const QByteArray encoderName = QString("dsk_video_%1_%2").arg(encoderGroupToString(profile.group), target.id).toUtf8();
	obs_encoder_t *encoder = obs_video_encoder_create(encoderId.toUtf8().constData(), encoderName.constData(), settings, nullptr);
	obs_data_release(settings);

	if (encoder) {
		obs_encoder_set_scaled_size(encoder, uint32_t(profile.width), uint32_t(profile.height));
		obs_encoder_set_gpu_scale_type(encoder, OBS_SCALE_BICUBIC);
	}

	return encoder;
}

obs_encoder_t *OutputManager::createAudioEncoder(const OutputTarget &target, const EncoderProfile &profile)
{
	const QString encoderId = profile.audioEncoderId.isEmpty() ? QStringLiteral("ffmpeg_aac") : profile.audioEncoderId;
	obs_data_t *settings = obs_data_create();
	obs_data_set_int(settings, "bitrate", profile.audioBitrateKbps);

	const QByteArray encoderName = QString("dsk_audio_%1_%2").arg(encoderGroupToString(profile.group), target.id).toUtf8();
	obs_encoder_t *encoder = obs_audio_encoder_create(encoderId.toUtf8().constData(), encoderName.constData(), settings, 0, nullptr);
	obs_data_release(settings);
	return encoder;
}

bool OutputManager::ensureVerticalCanvasVideo(QString *errorMessage)
{
#ifdef DSK_ENABLE_OBS_CANVAS_API
	obs_video_info info = {};
	if (!obs_get_video_info(&info)) {
		if (errorMessage)
			*errorMessage = QStringLiteral("OBS video information is unavailable.");
		return false;
	}
	if (layouts_.verticalLayout().width <= 0 || layouts_.verticalLayout().height <= 0) {
		if (errorMessage)
			*errorMessage = QStringLiteral("DSK Vertical canvas dimensions are invalid.");
		return false;
	}
	info.base_width = uint32_t(layouts_.verticalLayout().width);
	info.base_height = uint32_t(layouts_.verticalLayout().height);
	info.output_width = uint32_t(layouts_.verticalLayout().width);
	info.output_height = uint32_t(layouts_.verticalLayout().height);

	if (verticalCanvas_ && obs_canvas_removed(verticalCanvas_)) {
		obs_canvas_release(verticalCanvas_);
		verticalCanvas_ = nullptr;
	}
	if (!verticalCanvas_) {
		obs_frontend_canvas_list canvases = {};
		obs_frontend_get_canvases(&canvases);
		for (size_t i = 0; i < canvases.canvases.num; ++i) {
			obs_canvas_t *candidate = canvases.canvases.array[i];
			const char *name = obs_canvas_get_name(candidate);
			if (name && strcmp(name, "DSK Vertical") == 0 && !obs_canvas_removed(candidate)) {
				verticalCanvas_ = obs_canvas_get_ref(candidate);
				break;
			}
		}
		obs_frontend_canvas_list_free(&canvases);
	}

	obs_video_info currentInfo = {};
	const bool hasVideo = verticalCanvas_ && obs_canvas_has_video(verticalCanvas_);
	const bool hasMatchingInfo = verticalCanvas_ && obs_canvas_get_video_info(verticalCanvas_, &currentInfo) &&
				     currentInfo.base_width == info.base_width &&
				     currentInfo.base_height == info.base_height &&
				     currentInfo.output_width == info.output_width &&
				     currentInfo.output_height == info.output_height;
	const bool hasMatchingFlags = verticalCanvas_ && obs_canvas_get_flags(verticalCanvas_) == DskVideoCanvasFlags;

	if (verticalCanvas_ && (!hasVideo || !hasMatchingInfo || !hasMatchingFlags)) {
		if (sessionUsesCanvasKey(QStringLiteral("dsk-vertical"))) {
			if (errorMessage)
				*errorMessage = QStringLiteral("DSK Vertical canvas is in use and cannot change its configuration.");
			return false;
		}
		if (hasMatchingFlags && !obs_video_active() && obs_canvas_reset_video(verticalCanvas_, &info))
			return true;

		verticalScene_.release();
		obs_canvas_set_channel(verticalCanvas_, 0, nullptr);
		if (!obs_frontend_remove_canvas(verticalCanvas_)) {
			if (errorMessage)
				*errorMessage = QStringLiteral("Failed to replace the DSK Vertical canvas.");
			return false;
		}
		obs_canvas_release(verticalCanvas_);
		verticalCanvas_ = nullptr;
	}

	if (!verticalCanvas_)
		verticalCanvas_ = obs_frontend_add_canvas("DSK Vertical", &info, DskVideoCanvasFlags);

	if (!verticalCanvas_ || !obs_canvas_has_video(verticalCanvas_)) {
		if (verticalCanvas_) {
			obs_frontend_remove_canvas(verticalCanvas_);
			obs_canvas_release(verticalCanvas_);
			verticalCanvas_ = nullptr;
		}
		if (errorMessage)
			*errorMessage = QStringLiteral("Failed to create DSK Vertical canvas video.");
		return false;
	}

	if (errorMessage)
		errorMessage->clear();
	return true;
#else
	if (errorMessage)
		*errorMessage = "DSK Vertical scene was built, but real 9:16 output needs OBS canvas API wiring.";
	return false;
#endif
}

bool OutputManager::refreshVerticalCanvasScene(QString *errorMessage)
{
#ifdef DSK_ENABLE_OBS_CANVAS_API
	if (!verticalCanvas_) {
		if (errorMessage)
			errorMessage->clear();
		return true;
	}
	if (!ensureVerticalCanvasVideo(errorMessage))
		return false;

	QString sceneError;
	obs_source_t *source = verticalScene_.rebuild(layouts_.verticalLayout(), verticalCanvas_, &sceneError);
	if (!source) {
		if (errorMessage)
			*errorMessage = sceneError.isEmpty() ? "Failed to build DSK Vertical scene." : sceneError;
		return false;
	}

	if (verticalCanvas_)
		obs_canvas_set_channel(verticalCanvas_, 0, source);
	return true;
#else
	(void)errorMessage;
	return true;
#endif
}

bool OutputManager::targetUsesSceneCanvas(const OutputTarget &target) const
{
	return target.encoderGroup == EncoderGroup::DskHorizontal && target.sceneMode != TargetSceneMode::FollowObs;
}

QString OutputManager::sceneCanvasKeyForTarget(const OutputTarget &target) const
{
	if (targetUsesSceneCanvas(target))
		return QStringLiteral("scene-target:%1").arg(target.id);
	if (target.encoderGroup == EncoderGroup::DskVertical)
		return QStringLiteral("dsk-vertical");
	return QStringLiteral("obs-program");
}

bool OutputManager::sessionUsesCanvasKey(const QString &key) const
{
	for (const Session *session : sessions_) {
		if (!session || !session->output)
			continue;
		const OutputTarget *target = findTarget(session->targetId);
		if (target && sceneCanvasKeyForTarget(*target) == key)
			return true;
	}
	return false;
}

bool OutputManager::ensureSceneCanvasForTarget(const OutputTarget &target, const EncoderProfile &profile, QString *errorMessage)
{
#ifdef DSK_ENABLE_OBS_CANVAS_API
	if (!targetUsesSceneCanvas(target))
		return true;

	const QString sceneName = effectiveOutputSceneName(target);
	if (sceneName.trimmed().isEmpty()) {
		if (errorMessage)
			*errorMessage = target.sceneMode == TargetSceneMode::LinkedScene
				? QStringLiteral("Linked scene mode has no route for the current OBS scene and no fallback scene.")
				: QStringLiteral("Fixed scene mode needs an OBS scene.");
		return false;
	}

	obs_source_t *sceneSource = obs_get_source_by_name(sceneName.toUtf8().constData());
	if (!sceneSource) {
		if (errorMessage)
			*errorMessage = QStringLiteral("OBS scene not found: %1").arg(sceneName);
		return false;
	}
	if (!obs_scene_from_source(sceneSource)) {
		if (errorMessage)
			*errorMessage = QStringLiteral("Selected source is not an OBS scene: %1").arg(sceneName);
		obs_source_release(sceneSource);
		return false;
	}

	obs_video_info info = {};
	if (!obs_get_video_info(&info) || profile.width <= 0 || profile.height <= 0) {
		obs_source_release(sceneSource);
		if (errorMessage)
			*errorMessage = QStringLiteral("DSK scene canvas video settings are invalid.");
		return false;
	}
	info.base_width = uint32_t(profile.width);
	info.base_height = uint32_t(profile.height);
	info.output_width = uint32_t(profile.width);
	info.output_height = uint32_t(profile.height);

	obs_canvas_t *canvas = sceneCanvases_.value(target.id, nullptr);
	if (canvas && obs_canvas_removed(canvas)) {
		obs_canvas_release(canvas);
		sceneCanvases_.remove(target.id);
		canvas = nullptr;
	}

	obs_video_info currentInfo = {};
	const bool hasVideo = canvas && obs_canvas_has_video(canvas);
	const bool hasMatchingInfo = canvas && obs_canvas_get_video_info(canvas, &currentInfo) &&
				     currentInfo.base_width == info.base_width &&
				     currentInfo.base_height == info.base_height &&
				     currentInfo.output_width == info.output_width &&
				     currentInfo.output_height == info.output_height;
	const bool hasMatchingFlags = canvas && obs_canvas_get_flags(canvas) == DskVideoCanvasFlags;
	if (canvas && (!hasVideo || !hasMatchingInfo || !hasMatchingFlags)) {
		if (sessionUsesCanvasKey(sceneCanvasKeyForTarget(target))) {
			obs_source_release(sceneSource);
			if (errorMessage)
				*errorMessage = QStringLiteral("This DSK scene canvas is in use and cannot change its configuration.");
			return false;
		}
		if (!hasMatchingFlags || obs_video_active() || !obs_canvas_reset_video(canvas, &info)) {
			obs_canvas_set_channel(canvas, 0, nullptr);
			if (!obs_frontend_remove_canvas(canvas)) {
				obs_source_release(sceneSource);
				if (errorMessage)
					*errorMessage = QStringLiteral("Failed to replace the DSK scene canvas.");
				return false;
			}
			obs_canvas_release(canvas);
			sceneCanvases_.remove(target.id);
			canvas = nullptr;
		}
	}

	if (!canvas) {
		const QString canvasName = QStringLiteral("DSK Scene - %1").arg(target.name.isEmpty() ? target.id : target.name);
		canvas = obs_frontend_add_canvas(canvasName.toUtf8().constData(), &info, DskVideoCanvasFlags);
		if (!canvas || !obs_canvas_has_video(canvas)) {
			if (canvas) {
				obs_frontend_remove_canvas(canvas);
				obs_canvas_release(canvas);
			}
			obs_source_release(sceneSource);
			if (errorMessage)
				*errorMessage = QStringLiteral("Failed to create DSK scene canvas video.");
			return false;
		}
		sceneCanvases_.insert(target.id, canvas);
	}

	obs_canvas_set_channel(canvas, 0, sceneSource);
	obs_source_release(sceneSource);
	if (errorMessage)
		errorMessage->clear();
	return true;
#else
	(void)target;
	(void)profile;
	if (errorMessage)
		*errorMessage = QStringLiteral("Separate scene output needs OBS canvas API wiring.");
	return false;
#endif
}

void OutputManager::releaseTargetSceneCanvas(const QString &targetId)
{
#ifdef DSK_ENABLE_OBS_CANVAS_API
	if (!shuttingDown_ && sessionUsesCanvasKey(QStringLiteral("scene-target:%1").arg(targetId))) {
		logWarning(QStringLiteral("Deferred release of an in-use DSK scene canvas for %1.").arg(targetId));
		return;
	}
	obs_canvas_t *canvas = sceneCanvases_.take(targetId);
	if (!canvas)
		return;
	obs_canvas_set_channel(canvas, 0, nullptr);
	obs_frontend_remove_canvas(canvas);
	obs_canvas_release(canvas);
#else
	(void)targetId;
#endif
}

void OutputManager::releaseAllSceneCanvases()
{
#ifdef DSK_ENABLE_OBS_CANVAS_API
	const auto ids = sceneCanvases_.keys();
	for (const QString &id : ids)
		releaseTargetSceneCanvas(id);
#endif
}

void OutputManager::refreshLinkedSceneCanvases()
{
#ifdef DSK_ENABLE_OBS_CANVAS_API
	for (Session *session : sessions_) {
		if (!session || !session->output || !obs_output_active(session->output))
			continue;
		OutputTarget *target = findTarget(session->targetId);
		if (!target || target->sceneMode != TargetSceneMode::LinkedScene || !targetUsesSceneCanvas(*target))
			continue;
		QString error;
		if (!ensureSceneCanvasForTarget(*target, effectiveProfileForTarget(*target), &error) && !error.isEmpty())
			logWarning(QString("Failed to refresh linked scene for %1: %2").arg(target->name, error));
	}
#endif
}

video_t *OutputManager::videoForTarget(const OutputTarget &target, const EncoderProfile &profile, QString *errorMessage)
{
	if (!targetUsesSceneCanvas(target))
		return videoForEncoderGroup(target.encoderGroup, errorMessage);

	if (!ensureSceneCanvasForTarget(target, profile, errorMessage))
		return nullptr;

#ifdef DSK_ENABLE_OBS_CANVAS_API
	obs_canvas_t *canvas = sceneCanvases_.value(target.id, nullptr);
	if (!canvas) {
		if (errorMessage)
			*errorMessage = QStringLiteral("DSK scene canvas is unavailable.");
		return nullptr;
	}
	return obs_canvas_get_video(canvas);
#else
	(void)target;
	(void)profile;
	if (errorMessage)
		*errorMessage = QStringLiteral("Separate scene output needs OBS canvas API wiring.");
	return nullptr;
#endif
}

video_t *OutputManager::videoForEncoderGroup(EncoderGroup group, QString *errorMessage)
{
	if (group == EncoderGroup::DskHorizontal)
		return obs_get_video();

	if (group == EncoderGroup::DskVertical) {
#ifdef DSK_ENABLE_OBS_CANVAS_API
		if (!ensureVerticalCanvasVideo(errorMessage))
			return nullptr;

		QString sceneError;
		obs_source_t *source = verticalScene_.rebuild(layouts_.verticalLayout(), verticalCanvas_, &sceneError);
		if (!source) {
			if (errorMessage)
				*errorMessage = sceneError.isEmpty() ? "Failed to build DSK Vertical scene." : sceneError;
			return nullptr;
		}

		obs_canvas_set_channel(verticalCanvas_, 0, source);
		return obs_canvas_get_video(verticalCanvas_);
#else
		if (errorMessage)
			*errorMessage = "DSK Vertical scene was built, but real 9:16 output needs OBS canvas API wiring.";
		return nullptr;
#endif
	}

	return obs_get_video();
}

QString OutputManager::currentObsSceneName() const
{
	obs_source_t *scene = obs_frontend_get_current_scene();
	if (!scene)
		return {};

	const char *name = obs_source_get_name(scene);
	const QString result = name ? QString::fromUtf8(name) : QString();
	obs_source_release(scene);
	return result;
}

QString OutputManager::currentObsSceneUuid() const
{
	obs_source_t *scene = obs_frontend_get_current_scene();
	if (!scene)
		return {};

	const char *uuid = obs_source_get_uuid(scene);
	const QString result = uuid ? QString::fromUtf8(uuid) : QString();
	obs_source_release(scene);
	return result;
}

QString OutputManager::resolvedObsSceneName(const QString &sceneUuid, const QString &fallbackSceneName) const
{
	const QString cleanUuid = sceneUuid.trimmed();
	if (cleanUuid.isEmpty())
		return fallbackSceneName.trimmed();

	obs_source_t *source = obs_get_source_by_uuid(cleanUuid.toUtf8().constData());
	if (!source)
		return {};
	const char *name = obs_scene_from_source(source) ? obs_source_get_name(source) : nullptr;
	const QString result = name ? QString::fromUtf8(name) : QString();
	obs_source_release(source);
	return result;
}

QString OutputManager::obsSceneUuidForName(const QString &sceneName) const
{
	const QString cleanName = sceneName.trimmed();
	if (cleanName.isEmpty())
		return {};

	obs_source_t *source = obs_get_source_by_name(cleanName.toUtf8().constData());
	if (!source)
		return {};
	const char *uuid = obs_scene_from_source(source) ? obs_source_get_uuid(source) : nullptr;
	const QString result = uuid ? QString::fromUtf8(uuid) : QString();
	obs_source_release(source);
	return result;
}

bool OutputManager::applyLinkedScene(const QString &sceneName)
{
	if (sceneName.isEmpty())
		return false;
	const QString sceneUuid = currentObsSceneUuid();

	for (const auto &link : sceneLinks_) {
		const bool matches = link.sceneUuid.trimmed().isEmpty()
					     ? link.sceneName.trimmed() == sceneName.trimmed()
					     : !sceneUuid.isEmpty() && link.sceneUuid.trimmed() == sceneUuid;
		if (!matches)
			continue;

		if (!link.verticalSceneId.isEmpty()) {
			if (!layouts_.selectVerticalScene(link.verticalSceneId)) {
				logWarning(QString("Failed to apply vertical scene link for %1: DSK scene is missing.")
						   .arg(sceneName));
				return false;
			}
		} else if (!link.legacyTemplateId.isEmpty()) {
			layouts_.applyTemplate(link.legacyTemplateId);
		} else {
			return false;
		}
		if (!saveVerticalLayout())
			return false;
		emit statusMessage(QString("DSK vertical scene linked to %1").arg(sceneName));
		return true;
	}
	return false;
}

void OutputManager::applyProfileDelay(obs_output_t *output)
{
	config_t *config = obs_frontend_get_profile_config();
	if (!config)
		return;

	const bool useDelay = config_get_bool(config, "Output", "DelayEnable");
	const bool preserveDelay = config_get_bool(config, "Output", "DelayPreserve");
	const uint32_t delaySec = uint32_t(config_get_int(config, "Output", "DelaySec"));
	obs_output_set_delay(output, useDelay ? delaySec : 0, preserveDelay ? OBS_OUTPUT_DELAY_PRESERVE : 0);
}

} // namespace dsk
