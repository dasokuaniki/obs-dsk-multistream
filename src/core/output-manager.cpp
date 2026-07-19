#include "core/output-manager.hpp"

#include "core/diagnostics.hpp"
#include "core/output-signal-policy.hpp"
#include "core/oauth-provider.hpp"
#include "core/secret-store.hpp"
#include "core/youtube-api-warning.hpp"
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
#include <QUrlQuery>

#include <cstring>
#include <initializer_list>
#include <utility>


namespace dsk {

namespace {

constexpr int PlatformApiTimeoutMs = 20 * 1000;
constexpr int YouTubeApiPageSize = 50;
constexpr int YouTubeMaxBroadcastPagesPerStatus = 10;
constexpr int YouTubeMaxTransientRetries = 5;
constexpr qint64 YouTubeSignalWaitTimeoutMs = 2 * 60 * 1000;
constexpr qint64 YouTubeAutoStartWaitTimeoutMs = 5 * 60 * 1000;
constexpr uint32_t DskVideoCanvasFlags = ACTIVATE | SCENE_REF | EPHEMERAL;

} // namespace

struct OutputManager::Session {
	quint64 serial = 0;
	QString targetId;
	obs_output_t *output = nullptr;
	obs_service_t *service = nullptr;
	qint64 startedAtMs = 0;
	QString sharedEncoderKey;
	QString youtubeAccessToken;
	qint64 youtubeAccessTokenExpiresAtMs = 0;
	qint64 youtubeSignalActiveAtMs = 0;
	qint64 youtubeAutoStartWaitingSinceMs = 0;
	quint64 youtubeOperationGeneration = 0;
	quint64 youtubePollGeneration = 0;
	int youtubeTransientRetryCount = 0;
	bool youtubeOperationInFlight = false;
	bool youtubeAuthRefreshRetried = false;
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

	QString detail = stripHtml(QString::fromUtf8(response.body).left(500).trimmed());
	const QStringList reasons = youtubeApiErrorReasons(response);
	const QJsonDocument document = QJsonDocument::fromJson(response.body);
	if (document.isObject()) {
		const QJsonObject error = document.object().value("error").toObject();
		const QString message = error.value("message").toString();
		if (!message.isEmpty())
			detail = stripHtml(message);
	}

	if (isQuotaExceededText(detail) ||
	    youtubeApiErrorHasAnyReason(response, {"quotaExceedß¼òÚ$z{-®éÜj×B'—FT'&’Væ6öFW$æÖRÒ7G&–ær‚&G6µöVF–õòSòS""’æ&r†Væ6öFW$w&÷WFõ7G&–ær‡&öf–ÆRæw&÷W’ÂF&vWBæ–B’çFõWFc‚‚“° –ö'5öVæ6öFW%÷B¦Væ6öFW"Òö'5öVF–õöVæ6öFW%ö7&VFR†Væ6öFW$–BçFõWFc‚‚’æ6öç7DFF‚’ÂVæ6öFW$æÖRæ6öç7DFF‚’Â6WGF–æw2ÂÂçVÆÇG"“° –ö'5öFF÷&VÆV6R‡6WGF–æw2“° —&WGW&âVæ6öFW#°§Ð ¦&ööÂ÷WGWDÖævW#£¦Vç7W&UfW'F–6Ä6çf5f–FVò…7G&–ær¦W'&÷$ÖW76vR§°¢6–fFVbE4µôTä$ÄUôô%5ô4åd5ô –ö'5÷f–FVõö–æfò–æfòÒ·Ó° ––b‚ö'5övWE÷f–FVõö–æfò‚f–æfò’’° ––b†W'&÷$ÖW76vR ’¦W'&÷$ÖW76vRÒ7G&–ætÆ—FW&Â‚$ô%2f–FVò–æf÷&ÖF–öâ—2Væf–Æ&ÆRâ"“° —&WGW&âfÇ6S° —Ð ––b†Æ–÷WG5òçfW'F–6ÄÆ–÷WB‚’çv–GF‚ÃÒÇÂÆ–÷WG5òçfW'F–6ÄÆ–÷WB‚’æ†V–v‡BÃÒ’° ––b†W'&÷$ÖW76vR ’¦W'&÷$ÖW76vRÒ7G&–ætÆ—FW&Â‚$E4²fW'F–6Â6çf2F–ÖVç6–öç2&R–çfÆ–Bâ"“° —&WGW&âfÇ6S° —Ð ––æfòæ&6U÷v–GF‚ÒV–çC3%÷B†Æ–÷WG5òçfW'F–6ÄÆ–÷WB‚’çv–GF‚“° ––æfòæ&6Uö†V–v‡BÒV–çC3%÷B†Æ–÷WG5òçfW'F–6ÄÆ–÷WB‚’æ†V–v‡B“° ––æfòæ÷WGWE÷v–GF‚ÒV–çC3%÷B†Æ–÷WG5òçfW'F–6ÄÆ–÷WB‚’çv–GF‚“° ––æfòæ÷WGWEö†V–v‡BÒV–çC3%÷B†Æ–÷WG5òçfW'F–6ÄÆ–÷WB‚’æ†V–v‡B“°  ––b‡fW'F–6Ä6çf5òbbö'5ö6çf5÷&VÖ÷fVB‡fW'F–6Ä6çf5ò’’° –ö'5ö6çf5÷&VÆV6R‡fW'F–6Ä6çf5ò“° —fW'F–6Ä6çf5òÒçVÆÇG#° —Ð ––b‚fW'F–6Ä6çf5ò’° –ö'5ög&öçFVæEö6çf5öÆ—7B6çf6W2Ò·Ó° –ö'5ög&öçFVæEövWEö6çf6W2‚f6çf6W2“° –f÷"‡6—¦U÷B’Ò²’Â6çf6W2æ6çf6W2æçVÓ²²¶’’° –ö'5ö6çf5÷B¦6æF–FFRÒ6çf6W2æ6çf6W2æ'&•¶•Ó° –6öç7B6†"¦æÖRÒö'5ö6çf5övWEöæÖR†6æF–FFR“° ––b†æÖRbb7G&6×†æÖRÂ$E4²fW'F–6Â"’ÓÒbbö'5ö6çf5÷&VÖ÷fVB†6æF–FFR’’° —fW'F–6Ä6çf5òÒö'5ö6çf5övWE÷&Vb†6æF–FFR“° –'&V³° —Ð —Ð –ö'5ög&öçFVæEö6çf5öÆ—7Eög&VR‚f6çf6W2“° —Ð  –ö'5÷f–FVõö–æfò7W'&VçD–æfòÒ·Ó° –6öç7B&ööÂ†5f–FVòÒfW'F–6Ä6çf5òbbö'5ö6çf5ö†5÷f–FVò‡fW'F–6Ä6çf5ò“° –6öç7B&ööÂ†4ÖF6†–æt–æfòÒfW'F–6Ä6çf5òbbö'5ö6çf5övWE÷f–FVõö–æfò‡fW'F–6Ä6çf5òÂf7W'&VçD–æfò’b` ’7W'&VçD–æfòæ&6U÷v–GF‚ÓÒ–æfòæ&6U÷v–GF‚b` ’7W'&VçD–æfòæ&6Uö†V–v‡BÓÒ–æfòæ&6Uö†V–v‡Bb` ’7W'&VçD–æfòæ÷WGWE÷v–GF‚ÓÒ–æfòæ÷WGWE÷v–GF‚b` ’7W'&VçD–æfòæ÷WGWEö†V–v‡BÓÒ–æfòæ÷WGWEö†V–v‡C° –6öç7B&ööÂ†4ÖF6†–ætfÆw2ÒfW'F–6Ä6çf5òbbö'5ö6çf5övWEöfÆw2‡fW'F–6Ä6çf5ò’ÓÒG6µf–FVô6çf4fÆw3°  ––b‡fW'F–6Ä6çf5òbb‚†5f–FVòÇÂ†4ÖF6†–æt–æfòÇÂ†4ÖF6†–ætfÆw2’’° ––b‡6W76–öåW6W46çf4¶W’…7G&–ætÆ—FW&Â‚&G6²×fW'F–6Â"’’’° ––b†W'&÷$ÖW76vR ’¦W'&÷$ÖW76vRÒ7G&–ætÆ—FW&Â‚$E4²fW'F–6Â6çf2—2–âW6RæB6ææ÷B6†ævR—G26öæf–wW&F–öââ"“° —&WGW&âfÇ6S° —Ð ––b††4ÖF6†–ætfÆw2bbö'5÷f–FVõö7F—fR‚’bbö'5ö6çf5÷&W6WE÷f–FVò‡fW'F–6Ä6çf5òÂf–æfò’ —&WGW&âG'VS°  —fW'F–6Å66VæUòç&VÆV6R‚“° –ö'5ö6çf5÷6WEö6†ææVÂ‡fW'F–6Ä6çf5òÂÂçVÆÇG"“° ––b‚ö'5ög&öçFVæE÷&VÖ÷fUö6çf2‡fW'F–6Ä6çf5ò’’° ––b†W'&÷$ÖW76vR ’¦W'&÷$ÖW76vRÒ7G&–ætÆ—FW&Â‚$f–ÆVBFò&WÆ6RF†RE4²fW'F–6Â6çf2â"“° —&WGW&âfÇ6S° —Ð –ö'5ö6çf5÷&VÆV6R‡fW'F–6Ä6çf5ò“° —fW'F–6Ä6çf5òÒçVÆÇG#° —Ð  ––b‚fW'F–6Ä6çf5ò —fW'F–6Ä6çf5òÒö'5ög&öçFVæEöFEö6çf2‚$E4²fW'F–6Â"Âf–æfòÂG6µf–FVô6çf4fÆw2“°  ––b‚fW'F–6Ä6çf5òÇÂö'5ö6çf5ö†5÷f–FVò‡fW'F–6Ä6çf5ò’’° ––b‡fW'F–6Ä6çf5ò’° –ö'5ög&öçFVæE÷&VÖ÷fUö6çf2‡fW'F–6Ä6çf5ò“° –ö'5ö6çf5÷&VÆV6R‡fW'F–6Ä6çf5ò“° —fW'F–6Ä6çf5òÒçVÆÇG#° —Ð ––b†W'&÷$ÖW76vR ’¦W'&÷$ÖW76vRÒ7G&–ætÆ—FW&Â‚$f–ÆVBFò7&VFRE4²fW'F–6Â6çf2f–FVòâ"“° —&WGW&âfÇ6S° —Ð  ––b†W'&÷$ÖW76vR –W'&÷$ÖW76vRÓæ6ÆV"‚“° —&WGW&âG'VS°¢6VÇ6P ––b†W'&÷$ÖW76vR ’¦W'&÷$ÖW76vRÒ$E4²fW'F–6Â66VæRv2'V–ÇBÂ'WB&VÂ“£b÷WGWBæVVG2ô%26çf2’v—&–ærâ#° —&WGW&âfÇ6S°¢6VæF–`§Ð ¦&ööÂ÷WGWDÖævW#£§&Vg&W6…fW'F–6Ä6çf566VæR…7G&–ær¦W'&÷$ÖW76vR§°¢6–fFVbE4µôTä$ÄUôô%5ô4åd5ô ––b‚fW'F–6Ä6çf5ò’° ––b†W'&÷$ÖW76vR –W'&÷$ÖW76vRÓæ6ÆV"‚“° —&WGW&âG'VS° —Ð ––b‚Vç7W&UfW'F–6Ä6çf5f–FVò†W'&÷$ÖW76vR’ —&WGW&âfÇ6S°  •7G&–ær66VæTW'&÷#° –ö'5÷6÷W&6U÷B§6÷W&6RÒfW'F–6Å66VæUòç&V'V–ÆB†Æ–÷WG5òçfW'F–6ÄÆ–÷WB‚’ÂfW'F–6Ä6çf5òÂg66VæTW'&÷"“° ––b‚6÷W&6R’° ––b†W'&÷$ÖW76vR ’¦W'&÷$ÖW76vRÒ66VæTW'&÷"æ—4V×G’‚’ò$f–ÆVBFò'V–ÆBE4²fW'F–6Â66VæRâ"¢66VæTW'&÷#° —&WGW&âfÇ6S° —Ð  ––b‡fW'F–6Ä6çf5ò –ö'5ö6çf5÷6WEö6†ææVÂ‡fW'F–6Ä6çf5òÂÂ6÷W&6R“° —&WGW&âG'VS°¢6VÇ6P ’‡fö–B–W'&÷$ÖW76vS° —&WGW&âG'VS°¢6VæF–`§Ð ¦&ööÂ÷WGWDÖævW#£§F&vWEW6W566VæT6çf2†6öç7B÷WGWEF&vWBgF&vWB’6öç7@§° —&WGW&âF&vWBæVæ6öFW$w&÷WÓÒVæ6öFW$w&÷W£¤G6´†÷&—¦öçFÂbbF&vWBç66VæTÖöFRÒF&vWE66VæTÖöFS£¤föÆÆ÷tö'3°§Ð ¥7G&–ær÷WGWDÖævW#£§66VæT6çf4¶W”f÷%F&vWB†6öç7B÷WGWEF&vWBgF&vWB’6öç7@§° ––b‡F&vWEW6W566VæT6çf2‡F&vWB’ —&WGW&â7G&–ætÆ—FW&Â‚'66VæR×F&vWC¢S"’æ&r‡F&vWBæ–B“° ––b‡F&vWBæVæ6öFW$w&÷WÓÒVæ6öFW$w&÷W£¤G6µfW'F–6Â —&WGW&â7G&–ætÆ—FW&Â‚&G6²×fW'F–6Â"“° —&WGW&â7G&–ætÆ—FW&Â‚&ö'2×&öw&Ò"“°§Ð ¦&ööÂ÷WGWDÖævW#£§6W76–öåW6W46çf4¶W’†6öç7B7G&–ærf¶W’’6öç7@§° –f÷"†6öç7B6W76–öâ§6W76–öâ¢6W76–öç5ò’° ––b‚6W76–öâÇÂ6W76–öâÓæ÷WGWB –6öçF–çVS° –6öç7B÷WGWEF&vWB§F&vWBÒf–æEF&vWB‡6W76–öâÓçF&vWD–B“° ––b‡F&vWBbb66VæT6çf4¶W”f÷%F&vWB‚§F&vWB’ÓÒ¶W’ —&WGW&âG'VS° —Ð —&WGW&âfÇ6S°§Ð ¦&ööÂ÷WGWDÖævW#£¦Vç7W&U66VæT6çf4f÷%F&vWB†6öç7B÷WGWEF&vWBgF&vWBÂ6öç7BVæ6öFW%&öf–ÆRg&öf–ÆRÂ7G&–ær¦W'&÷$ÖW76vR§°¢6–fFVbE4µôTä$ÄUôô%5ô4åd5ô ––b‚F&vWEW6W566VæT6çf2‡F&vWB’ —&WGW&âG'VS°  –6öç7B7G&–ær66VæTæÖRÒVffV7F—fT÷WGWE66VæTæÖR‡F&vWB“° ––b‡66VæTæÖRçG&–ÖÖVB‚’æ—4V×G’‚’’° ––b†W'&÷$ÖW76vR ’¦W'&÷$ÖW76vRÒF&vWBç66VæTÖöFRÓÒF&vWE66VæTÖöFS£¤Æ–æ¶VE66VæP “ò7G&–ætÆ—FW&Â‚$Æ–æ¶VB66VæRÖöFR†2æò&÷WFRf÷"F†R7W'&VçBô%266VæRæBæòfÆÆ&6²66VæRâ" “¢7G&–ætÆ—FW&Â‚$f—†VB66VæRÖöFRæVVG2âô%266VæRâ"“° —&WGW&âfÇ6S° —Ð  –ö'5÷6÷W&6U÷B§66VæU6÷W&6RÒö'5övWE÷6÷W&6Uö'•öæÖR‡66VæTæÖRçFõWFc‚‚’æ6öç7DFF‚’“° ––b‚66VæU6÷W&6R’° ––b†W'&÷$ÖW76vR ’¦W'&÷$ÖW76vRÒ7G&–ætÆ—FW&Â‚$ô%266VæRæ÷Bf÷VæC¢S"’æ&r‡66VæTæÖR“° —&WGW&âfÇ6S° —Ð ––b‚ö'5÷66VæUög&öÕ÷6÷W&6R‡66VæU6÷W&6R’’° ––b†W'&÷$ÖW76vR ’¦W'&÷$ÖW76vRÒ7G&–ætÆ—FW&Â‚%6VÆV7FVB6÷W&6R—2æ÷Bâô%266VæS¢S"’æ&r‡66VæTæÖR“° –ö'5÷6÷W&6U÷&VÆV6R‡66VæU6÷W&6R“° —&WGW&âfÇ6S° —Ð  –ö'5÷f–FVõö–æfò–æfòÒ·Ó° ––b‚ö'5övWE÷f–FVõö–æfò‚f–æfò’ÇÂ&öf–ÆRçv–GF‚ÃÒÇÂ&öf–ÆRæ†V–v‡BÃÒ’° –ö'5÷6÷W&6U÷&VÆV6R‡66VæU6÷W&6R“° ––b†W'&÷$ÖW76vR ’¦W'&÷$ÖW76vRÒ7G&–ætÆ—FW&Â‚$E4²66VæR6çf2f–FVò6WGF–æw2&R–çfÆ–Bâ"“° —&WGW&âfÇ6S° —Ð ––æfòæ&6U÷v–GF‚ÒV–çC3%÷B‡&öf–ÆRçv–GF‚“° ––æfòæ&6Uö†V–v‡BÒV–çC3%÷B‡&öf–ÆRæ†V–v‡B“° ––æfòæ÷WGWE÷v–GF‚ÒV–çC3%÷B‡&öf–ÆRçv–GF‚“° ––æfòæ÷WGWEö†V–v‡BÒV–çC3%÷B‡&öf–ÆRæ†V–v‡B“°  –ö'5ö6çf5÷B¦6çf2Ò66VæT6çf6W5òçfÇVR‡F&vWBæ–BÂçVÆÇG"“° ––b†6çf2bbö'5ö6çf5÷&VÖ÷fVB†6çf2’’° –ö'5ö6çf5÷&VÆV6R†6çf2“° —66VæT6çf6W5òç&VÖ÷fR‡F&vWBæ–B“° –6çf2ÒçVÆÇG#° —Ð  –ö'5÷f–FVõö–æfò7W'&VçD–æfòÒ·Ó° –6öç7B&ööÂ†5f–FVòÒ6çf2bbö'5ö6çf5ö†5÷f–FVò†6çf2“° –6öç7B&ööÂ†4ÖF6†–æt–æfòÒ6çf2bbö'5ö6çf5övWE÷f–FVõö–æfò†6çf2Âf7W'&VçD–æfò’b` ’7W'&VçD–æfòæ&6U÷v–GF‚ÓÒ–æfòæ&6U÷v–GF‚b` ’7W'&VçD–æfòæ&6Uö†V–v‡BÓÒ–æfòæ&6Uö†V–v‡Bb` ’7W'&VçD–æfòæ÷WGWE÷v–GF‚ÓÒ–æfòæ÷WGWE÷v–GF‚b` ’7W'&VçD–æfòæ÷WGWEö†V–v‡BÓÒ–æfòæ÷WGWEö†V–v‡C° –6öç7B&ööÂ†4ÖF6†–ætfÆw2Ò6çf2bbö'5ö6çf5övWEöfÆw2†6çf2’ÓÒG6µf–FVô6çf4fÆw3° ––b†6çf2bb‚†5f–FVòÇÂ†4ÖF6†–æt–æfòÇÂ†4ÖF6†–ætfÆw2’’° ––b‡6W76–öåW6W46çf4¶W’‡66VæT6çf4¶W”f÷%F&vWB‡F&vWB’’’° –ö'5÷6÷W&6U÷&VÆV6R‡66VæU6÷W&6R“° ––b†W'&÷$ÖW76vR ’¦W'&÷$ÖW76vRÒ7G&–ætÆ—FW&Â‚%F†—2E4²66VæR6çf2—2–âW6RæB6ææ÷B6†ævR—G26öæf–wW&F–öââ"“° —&WGW&âfÇ6S° —Ð ––b‚†4ÖF6†–ætfÆw2ÇÂö'5÷f–FVõö7F—fR‚’ÇÂö'5ö6çf5÷&W6WE÷f–FVò†6çf2Âf–æfò’’° –ö'5ö6çf5÷6WEö6†ææVÂ†6çf2ÂÂçVÆÇG"“° ––b‚ö'5ög&öçFVæE÷&VÖ÷fUö6çf2†6çf2’’° –ö'5÷6÷W&6U÷&VÆV6R‡66VæU6÷W&6R“° ––b†W'&÷$ÖW76vR ’¦W'&÷$ÖW76vRÒ7G&–ætÆ—FW&Â‚$f–ÆVBFò&WÆ6RF†RE4²66VæR6çf2â"“° —&WGW&âfÇ6S° —Ð –ö'5ö6çf5÷&VÆV6R†6çf2“° —66VæT6çf6W5òç&VÖ÷fR‡F&vWBæ–B“° –6çf2ÒçVÆÇG#° —Ð —Ð  ––b‚6çf2’° –6öç7B7G&–ær6çf4æÖRÒ7G&–ætÆ—FW&Â‚$E4²66VæRÒS"’æ&r‡F&vWBææÖRæ—4V×G’‚’òF&vWBæ–B¢F&vWBææÖR“° –6çf2Òö'5ög&öçFVæEöFEö6çf2†6çf4æÖRçFõWFc‚‚’æ6öç7DFF‚’Âf–æfòÂG6µf–FVô6çf4fÆw2“° ––b‚6çf2ÇÂö'5ö6çf5ö†5÷f–FVò†6çf2’’° ––b†6çf2’° –ö'5ög&öçFVæE÷&VÖ÷fUö6çf2†6çf2“° –ö'5ö6çf5÷&VÆV6R†6çf2“° —Ð –ö'5÷6÷W&6U÷&VÆV6R‡66VæU6÷W&6R“° ––b†W'&÷$ÖW76vR ’¦W'&÷$ÖW76vRÒ7G&–ætÆ—FW&Â‚$f–ÆVBFò7&VFRE4²66VæR6çf2f–FVòâ"“° —&WGW&âfÇ6S° —Ð —66VæT6çf6W5òæ–ç6W'B‡F&vWBæ–BÂ6çf2“° —Ð  –ö'5ö6çf5÷6WEö6†ææVÂ†6çf2ÂÂ66VæU6÷W&6R“° –ö'5÷6÷W&6U÷&VÆV6R‡66VæU6÷W&6R“° ––b†W'&÷$ÖW76vR –W'&÷$ÖW76vRÓæ6ÆV"‚“° —&WGW&âG'VS°¢6VÇ6P ’‡fö–B—F&vWC° ’‡fö–B—&öf–ÆS° ––b†W'&÷$ÖW76vR ’¦W'&÷$ÖW76vRÒ7G&–ætÆ—FW&Â‚%6W&FR66VæR÷WGWBæVVG2ô%26çf2’v—&–ærâ"“° —&WGW&âfÇ6S°¢6VæF–`§Ð §fö–B÷WGWDÖævW#£§&VÆV6UF&vWE66VæT6çf2†6öç7B7G&–ærgF&vWD–B§°¢6–fFVbE4µôTä$ÄUôô%5ô4åd5ô ––b‚6‡WGF–ætF÷våòbb6W76–öåW6W46çf4¶W’…7G&–ætÆ—FW&Â‚'66VæR×F&vWC¢S"’æ&r‡F&vWD–B’’’° –Æöuv&æ–ær…7G&–ætÆ—FW&Â‚$FVfW'&VB&VÆV6Röbâ–â×W6RE4²66VæR6çf2f÷"Sâ"’æ&r‡F&vWD–B’“° —&WGW&ã° —Ð –ö'5ö6çf5÷B¦6çf2Ò66VæT6çf6W5òçF¶R‡F&vWD–B“° ––b‚6çf2 —&WGW&ã° –ö'5ö6çf5÷6WEö6†ææVÂ†6çf2ÂÂçVÆÇG"“° –ö'5ög&öçFVæE÷&VÖ÷fUö6çf2†6çf2“° –ö'5ö6çf5÷&VÆV6R†6çf2“°¢6VÇ6P ’‡fö–B—F&vWD–C°¢6VæF–`§Ð §fö–B÷WGWDÖævW#£§&VÆV6TÆÅ66VæT6çf6W2‚§°¢6–fFVbE4µôTä$ÄUôô%5ô4åd5ô –6öç7BWFò–G2Ò66VæT6çf6W5òæ¶W—2‚“° –f÷"†6öç7B7G&–ærf–B¢–G2 —&VÆV6UF&vWE66VæT6çf2†–B“°¢6VæF–`§Ð §fö–B÷WGWDÖævW#£§&Vg&W6„Æ–æ¶VE66VæT6çf6W2‚§°¢6–fFVbE4µôTä$ÄUôô%5ô4åd5ô –f÷"…6W76–öâ§6W76–öâ¢6W76–öç5ò’° ––b‚6W76–öâÇÂ6W76–öâÓæ÷WGWBÇÂö'5ö÷WGWEö7F—fR‡6W76–öâÓæ÷WGWB’ –6öçF–çVS° ”÷WGWEF&vWB§F&vWBÒf–æEF&vWB‡6W76–öâÓçF&vWD–B“° ––b‚F&vWBÇÂF&vWBÓç66VæTÖöFRÒF&vWE66VæTÖöFS£¤Æ–æ¶VE66VæRÇÂF&vWEW6W566VæT6çf2‚§F&vWB’ –6öçF–çVS° •7G&–ærW'&÷#° ––b‚Vç7W&U66VæT6çf4f÷%F&vWB‚§F&vWBÂVffV7F—fU&öf–ÆTf÷%F&vWB‚§F&vWB’ÂfW'&÷"’bbW'&÷"æ—4V×G’‚’ –Æöuv&æ–ær…7G&–ær‚$f–ÆVBFò&Vg&W6‚Æ–æ¶VB66VæRf÷"S¢S""’æ&r‡F&vWBÓææÖRÂW'&÷"’“° —Ð¢6VæF–`§Ð §f–FVõ÷B¤÷WGWDÖævW#£§f–FVôf÷%F&vWB†6öç7B÷WGWEF&vWBgF&vWBÂ6öç7BVæ6öFW%&öf–ÆRg&öf–ÆRÂ7G&–ær¦W'&÷$ÖW76vR§° ––b‚F&vWEW6W566VæT6çf2‡F&vWB’ —&WGW&âf–FVôf÷$Væ6öFW$w&÷W‡F&vWBæVæ6öFW$w&÷WÂW'&÷$ÖW76vR“°  ––b‚Vç7W&U66VæT6çf4f÷%F&vWB‡F&vWBÂ&öf–ÆRÂW'&÷$ÖW76vR’ —&WGW&âçVÆÇG#° ¢6–fFVbE4µôTä$ÄUôô%5ô4åd5ô –ö'5ö6çf5÷B¦6çf2Ò66VæT6çf6W5òçfÇVR‡F&vWBæ–BÂçVÆÇG"“° ––b‚6çf2’° ––b†W'&÷$ÖW76vR ’¦W'&÷$ÖW76vRÒ7G&–ætÆ—FW&Â‚$E4²66VæR6çf2—2Væf–Æ&ÆRâ"“° —&WGW&âçVÆÇG#° —Ð —&WGW&âö'5ö6çf5övWE÷f–FVò†6çf2“°¢6VÇ6P ’‡fö–B—F&vWC° ’‡fö–B—&öf–ÆS° ––b†W'&÷$ÖW76vR ’¦W'&÷$ÖW76vRÒ7G&–ætÆ—FW&Â‚%6W&FR66VæR÷WGWBæVVG2ô%26çf2’v—&–ærâ"“° —&WGW&âçVÆÇG#°¢6VæF–`§Ð §f–FVõ÷B¤÷WGWDÖævW#£§f–FVôf÷$Væ6öFW$w&÷W„Væ6öFW$w&÷Ww&÷WÂ7G&–ær¦W'&÷$ÖW76vR§° ––b†w&÷WÓÒVæ6öFW$w&÷W£¤G6´†÷&—¦öçFÂ —&WGW&âö'5övWE÷f–FVò‚“°  ––b†w&÷WÓÒVæ6öFW$w&÷W£¤G6µfW'F–6Â’°¢6–fFVbE4µôTä$ÄUôô%5ô4åd5ô ––b‚Vç7W&UfW'F–6Ä6çf5f–FVò†W'&÷$ÖW76vR’ —&WGW&âçVÆÇG#°  •7G&–ær66VæTW'&÷#° –ö'5÷6÷W&6U÷B§6÷W&6RÒfW'F–6Å66VæUòç&V'V–ÆB†Æ–÷WG5òçfW'F–6ÄÆ–÷WB‚’ÂfW'F–6Ä6çf5òÂg66VæTW'&÷"“° ––b‚6÷W&6R’° ––b†W'&÷$ÖW76vR ’¦W'&÷$ÖW76vRÒ66VæTW'&÷"æ—4V×G’‚’ò$f–ÆVBFò'V–ÆBE4²fW'F–6Â66VæRâ"¢66VæTW'&÷#° —&WGW&âçVÆÇG#° —Ð  –ö'5ö6çf5÷6WEö6†ææVÂ‡fW'F–6Ä6çf5òÂÂ6÷W&6R“° —&WGW&âö'5ö6çf5övWE÷f–FVò‡fW'F–6Ä6çf5ò“°¢6VÇ6P ––b†W'&÷$ÖW76vR ’¦W'&÷$ÖW76vRÒ$E4²fW'F–6Â66VæRv2'V–ÇBÂ'WB&VÂ“£b÷WGWBæVVG2ô%26çf2’v—&–ærâ#° —&WGW&âçVÆÇG#°¢6VæF–` —Ð  —&WGW&âö'5övWE÷f–FVò‚“°§Ð ¥7G&–ær÷WGWDÖævW#£¦7W'&VçDö'566VæTæÖR‚’6öç7@§° –ö'5÷6÷W&6U÷B§66VæRÒö'5ög&öçFVæEövWEö7W'&VçE÷66VæR‚“° ––b‚66VæR —&WGW&â·Ó°  –6öç7B6†"¦æÖRÒö'5÷6÷W&6UövWEöæÖR‡66VæR“° –6öç7B7G&–ær&W7VÇBÒæÖRò7G&–æs£¦g&öÕWFc‚†æÖR’¢7G&–ær‚“° –ö'5÷6÷W&6U÷&VÆV6R‡66VæR“° —&WGW&â&W7VÇC°§Ð ¥7G&–ær÷WGWDÖævW#£¦7W'&VçDö'566VæUWV–B‚’6öç7@§° –ö'5÷6÷W&6U÷B§66VæRÒö'5ög&öçFVæEövWEö7W'&VçE÷66VæR‚“° ––b‚66VæR —&WGW&â·Ó°  –6öç7B6†"§WV–BÒö'5÷6÷W&6UövWE÷WV–B‡66VæR“° –6öç7B7G&–ær&W7VÇBÒWV–Bò7G&–æs£¦g&öÕWFc‚‡WV–B’¢7G&–ær‚“° –ö'5÷6÷W&6U÷&VÆV6R‡66VæR“° —&WGW&â&W7VÇC°§Ð ¥7G&–ær÷WGWDÖævW#£§&W6öÇfVDö'566VæTæÖR†6öç7B7G&–ærg66VæUWV–BÂ6öç7B7G&–ærffÆÆ&6µ66VæTæÖR’6öç7@§° –6öç7B7G&–ær6ÆVåWV–BÒ66VæUWV–BçG&–ÖÖVB‚“° ––b†6ÆVåWV–Bæ—4V×G’‚’ —&WGW&âfÆÆ&6µ66VæTæÖRçG&–ÖÖVB‚“°  –ö'5÷6÷W&6U÷B§6÷W&6RÒö'5övWE÷6÷W&6Uö'•÷WV–B†6ÆVåWV–BçFõWFc‚‚’æ6öç7DFF‚’“° ––b‚6÷W&6R —&WGW&â·Ó° –6öç7B6†"¦æÖRÒö'5÷66VæUög&öÕ÷6÷W&6R‡6÷W&6R’òö'5÷6÷W&6UövWEöæÖR‡6÷W&6R’¢çVÆÇG#° –6öç7B7G&–ær&W7VÇBÒæÖRò7G&–æs£¦g&öÕWFc‚†æÖR’¢7G&–ær‚“° –ö'5÷6÷W&6U÷&VÆV6R‡6÷W&6R“° —&WGW&â&W7VÇC°§Ð ¥7G&–ær÷WGWDÖævW#£¦ö'566VæUWV–Df÷$æÖR†6öç7B7G&–ærg66VæTæÖR’6öç7@§° –6öç7B7G&–ær6ÆVäæÖRÒ66VæTæÖRçG&–ÖÖVB‚“° ––b†6ÆVäæÖRæ—4V×G’‚’ —&WGW&â·Ó°  –ö'5÷6÷W&6U÷B§6÷W&6RÒö'5övWE÷6÷W&6Uö'•öæÖR†6ÆVäæÖRçFõWFc‚‚’æ6öç7DFF‚’“° ––b‚6÷W&6R —&WGW&â·Ó° –6öç7B6†"§WV–BÒö'5÷66VæUög&öÕ÷6÷W&6R‡6÷W&6R’òö'5÷6÷W&6UövWE÷WV–B‡6÷W&6R’¢çVÆÇG#° –6öç7B7G&–ær&W7VÇBÒWV–Bò7G&–æs£¦g&öÕWFc‚‡WV–B’¢7G&–ær‚“° –ö'5÷6÷W&6U÷&VÆV6R‡6÷W&6R“° —&WGW&â&W7VÇC°§Ð ¦&ööÂ÷WGWDÖævW#£¦Ç”Æ–æ¶VE66VæR†6öç7B7G&–ærg66VæTæÖR§° ––b‡66VæTæÖRæ—4V×G’‚’ —&WGW&âfÇ6S° –6öç7B7G&–ær66VæUWV–BÒ7W'&VçDö'566VæUWV–B‚“°  –f÷"†6öç7BWFòfÆ–æ²¢66VæTÆ–æ·5ò’° –6öç7B&ööÂÖF6†W2ÒÆ–æ²ç66VæUWV–BçG&–ÖÖVB‚’æ—4V×G’‚ ’òÆ–æ²ç66VæTæÖRçG&–ÖÖVB‚’ÓÒ66VæTæÖRçG&–ÖÖVB‚ ’¢66VæUWV–Bæ—4V×G’‚’bbÆ–æ²ç66VæUWV–BçG&–ÖÖVB‚’ÓÒ66VæUWV–C° ––b‚ÖF6†W2 –6öçF–çVS°  ––b‚Æ–æ²çfW'F–6Å66VæT–Bæ—4V×G’‚’’° ––b‚Æ–÷WG5òç6VÆV7EfW'F–6Å66VæR†Æ–æ²çfW'F–6Å66VæT–B’’° –Æöuv&æ–ær…7G&–ær‚$f–ÆVBFòÇ’fW'F–6Â66VæRÆ–æ²f÷"S¢E4²66VæR—2Ö—76–ærâ" ’æ&r‡66VæTæÖR’“° —&WGW&âfÇ6S° —Ð —ÒVÇ6R–b‚Æ–æ²æÆVv7•FV×ÆFT–Bæ—4V×G’‚’’° –Æ–÷WG5òæÇ•FV×ÆFR†Æ–æ²æÆVv7•FV×ÆFT–B“° —ÒVÇ6R° —&WGW&âfÇ6S° —Ð ––b‚6fUfW'F–6ÄÆ–÷WB‚’ —&WGW&âfÇ6S° –VÖ—B7FGW4ÖW76vR…7G&–ær‚$E4²fW'F–6Â66VæRÆ–æ¶VBFòS"’æ&r‡66VæTæÖR’“° —&WGW&âG'VS° —Ð —&WGW&âfÇ6S°§Ð §fö–B÷WGWDÖævW#£¦Ç•&öf–ÆTFVÆ’†ö'5ö÷WGWE÷B¦÷WGWB§° –6öæf–u÷B¦6öæf–rÒö'5ög&öçFVæEövWE÷&öf–ÆUö6öæf–r‚“° ––b‚6öæf–r —&WGW&ã°  –6öç7B&ööÂW6TFVÆ’Ò6öæf–uövWEö&ööÂ†6öæf–rÂ$÷WGWB"Â$FVÆ”Væ&ÆR"“° –6öç7B&ööÂ&W6W'fTFVÆ’Ò6öæf–uövWEö&ööÂ†6öæf–rÂ$÷WGWB"Â$FVÆ•&W6W'fR"“° –6öç7BV–çC3%÷BFVÆ•6V2ÒV–çC3%÷B†6öæf–uövWEö–çB†6öæf–rÂ$÷WGWB"Â$FVÆ•6V2"’“° –ö'5ö÷WGWE÷6WEöFVÆ’†÷WGWBÂW6TFVÆ’òFVÆ•6V2¢Â&W6W'fTFVÆ’òô%5ôõUEUEôDTÄ•õ$U4U%dR¢“°§Ð §ÒòòæÖW76RG6°