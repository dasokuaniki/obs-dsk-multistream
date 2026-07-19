#include "core/encoder-profile-manager.hpp"
#include "core/experimental-features.hpp"
#include "core/comment-viewer-contract.hpp"
#include "core/comment-viewer-integration-policy.hpp"
#include "core/layout-manager.hpp"
#include "core/oauth-provider.hpp"
#include "core/output-signal-policy.hpp"
#include "core/output-runtime-status.hpp"
#include "core/output-target.hpp"
#include "core/platform-preset-registry.hpp"
#include "core/secret-store.hpp"
#include "core/settings-codec.hpp"
#include "core/vertical-layout-geometry.hpp"
#include "core/youtube-api-warning.hpp"
#include "core/youtube-broadcast-selector.hpp"
#include "ui/stream-controls-state.hpp"
#include "ui/visible-refresh-gate.hpp"
#include "ui/vertical-layout-metrics.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtGlobal>
#include <QUuid>

#include <iostream>
#include <limits>

namespace {

int failures = 0;

static_assert(dsk::VerticalPreviewMinimumWidth == 110);
static_assert(dsk::VerticalPreviewMinimumHeight == 195);
static_assert(dsk::VerticalPreviewMinimumHeight * 9 == dsk::VerticalPreviewMinimumWidth * 16 - 5,
	      "vertical preview minimum should remain approximately 9:16");

void check(bool condition, const char *message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
}

QSet<QString> localeKeys(const QString &path)
{
	QFile file(path);
	check(file.open(QIODevice::ReadOnly), qPrintable(QString("open locale %1").arg(path)));

	QSet<QString> keys;
	while (!file.atEnd()) {
		const QString line = QString::fromUtf8(file.readLine()).trimmed();
		if (line.isEmpty() || line.startsWith('#'))
			continue;

		const int equals = line.indexOf('=');
		check(equals > 0, qPrintable(QString("valid locale line in %1").arg(path)));
		if (equals <= 0)
			continue;

		const QString key = line.left(equals);
		check(!keys.contains(key), qPrintable(QString("duplicate locale key %1").arg(key)));
		keys.insert(key);
	}
	return keys;
}

void testOutputTargetHelpers()
{
	using namespace dsk;

	check(encoderGroupToString(EncoderGroup::DskHorizontal) == "dsk-horizontal", "horizontal encoder group string");
	check(encoderGroupToString(EncoderGroup::DskVertical) == "dsk-vertical", "vertical encoder group string");
	check(encoderGroupFromString("obs-main") == EncoderGroup::DskHorizontal,
	      "legacy OBS Main encoder group migrates to horizontal");
	check(encoderGroupFromString("dsk-vertical") == EncoderGroup::DskVertical, "vertical encoder group parse");
	check(encoderGroupFromString("unknown") == EncoderGroup::DskHorizontal, "unknown encoder group defaults horizontal");
	check(targetAuthModeToString(TargetAuthMode::ManualRtmp) == "manual-rtmp", "manual auth mode string");
	check(targetAuthModeToString(TargetAuthMode::TwitchOAuth) == "twitch-oauth", "Twitch auth mode string");
	check(targetAuthModeToString(TargetAuthMode::YouTubeOAuth) == "youtube-oauth", "YouTube auth mode string");
	check(targetAuthModeToString(TargetAuthMode::KickOAuth) == "kick-oauth", "Kick auth mode string");
	check(targetAuthModeFromString("twitch-oauth") == TargetAuthMode::TwitchOAuth, "Twitch auth mode parse");
	check(targetAuthModeFromString("youtube-oauth") == TargetAuthMode::YouTubeOAuth, "YouTube auth mode parse");
	check(targetAuthModeFromString("kick-oauth") == TargetAuthMode::KickOAuth, "Kick auth mode parse");
	check(targetAuthModeFromString("bad") == TargetAuthMode::ManualRtmp, "unknown auth mode defaults manual");
	check(platformSupportsAuthMode("twitch", TargetAuthMode::TwitchOAuth), "Twitch supports Twitch OAuth");
	check(platformSupportsAuthMode("youtube", TargetAuthMode::YouTubeOAuth), "YouTube supports YouTube OAuth");
	check(platformSupportsAuthMode("kick", TargetAuthMode::KickOAuth), "Kick supports Kick OAuth");
	check(!platformSupportsAuthMode("kick", TargetAuthMode::TwitchOAuth), "Kick does not support Twitch OAuth");

	OutputTarget legacyTwitch;
	legacyTwitch.authMode = TargetAuthMode::TwitchOAuth;
	legacyTwitch.authAccountName = QStringLiteral("saved-channel");
	legacyTwitch.authCredentialRef = QStringLiteral("DSK Multistream/stream-key/saved");
	legacyTwitch.oauthClientId = QStringLiteral("legacy-client-id");
	legacyTwitch.oauthClientSecret = QStringLiteral("legacy-client-secret");
	legacyTwitch.oauthClientSecretRef = QStringLiteral("DSK Multistream/oauth-client-secret/saved");
	legacyTwitch.oauthRefreshToken = QStringLiteral("legacy-refresh-token");
	legacyTwitch.oauthRefreshTokenRef = QStringLiteral("DSK Multistream/oauth-refresh-token/saved");
	check(migratePublisherManagedOAuthCredentials(legacyTwitch),
	      "publisher-managed Twitch login reports legacy credential migration");
	check(legacyTwitch.oauthClientId.isEmpty() && legacyTwitch.oauthClientSecret.isEmpty() &&
		      legacyTwitch.oauthClientSecretRef.isEmpty() && legacyTwitch.oauthRefreshToken.isEmpty() &&
		      legacyTwitch.oauthRefreshTokenRef.isEmpty(),
	      "publisher-managed Twitch login clears obsolete OAuth client credentials");
	check(legacyTwitch.authAccountName == "saved-channel" &&
		      legacyTwitch.authCredentialRef == "DSK Multistream/stream-key/saved",
	      "publisher-managed Twitch migration preserves account and stream key reference");
	check(!migratePublisherManagedOAuthCredentials(legacyTwitch),
	      "publisher-managed Twitch credential migration is idempotent");

	OutputTarget legacyKick;
	legacyKick.authMode = TargetAuthMode::KickOAuth;
	legacyKick.authAccountName = QStringLiteral("kick-channel");
	legacyKick.authCredentialRef = QStringLiteral("DSK Multistream/stream-key/kick");
	legacyKick.oauthClientId = QStringLiteral("legacy-kick-client");
	legacyKick.oauthClientSecret = QStringLiteral("legacy-kick-secret");
	legacyKick.oauthRefreshToken = QStringLiteral("legacy-kick-refresh");
	check(migratePublisherManagedOAuthCredentials(legacyKick),
	      "publisher-managed Kick login reports legacy credential migration");
	check(legacyKick.oauthClientId.isEmpty() && legacyKick.oauthClientSecret.isEmpty() &&
		      legacyKick.oauthRefreshToken.isEmpty(),
	      "publisher-managed Kick login clears obsolete OAuth credentials");
	check(legacyKick.authAccountName == "kick-channel" &&
		      legacyKick.authCredentialRef == "DSK Multistream/stream-key/kick",
	      "publisher-managed Kick migration preserves account and stream key reference");

	OutputTarget youtubeCredentials;
	youtubeCredentials.authMode = TargetAuthMode::YouTubeOAuth;
	youtubeCredentials.oauthClientId = QStringLiteral("youtube-client");
	check(!migratePublisherManagedOAuthCredentials(youtubeCredentials) &&
		      youtubeCredentials.oauthClientId == "youtube-client",
	      "publisher-managed Twitch migration leaves YouTube credentials unchanged");

	check(maskedKey("").isEmpty(), "empty stream key stays empty");
	check(maskedKey("abcdefgh") == "********", "short stream key is fully masked");
	check(maskedKey("abcd1234wxyz") == "********", "long stream key is fully masked");
	check(!newTargetId().isEmpty(), "new target id is populated");

	OutputTarget valid;
	valid.id = "valid";
	valid.serverUrl = "rtmps://example.test/live";
	valid.streamKey = "secret-key";
	QString error;
	check(validateOutputTargetConfig(valid, &error), "valid RTMPS target passes validation");
	check(error.isEmpty(), "valid target has no validation error");

	OutputTarget http = valid;
	http.serverUrl = "https://example.test/live";
	check(!validateOutputTargetConfig(http, &error), "HTTP target fails validation");
	check(error.contains("rtmp://"), "HTTP validation error names RTMP requirement");

	OutputTarget missingHost = valid;
	missingHost.serverUrl = "rtmp:///live";
	check(!validateOutputTargetConfig(missingHost, &error), "RTMP target without a host fails validation");
	check(error.contains("host"), "missing host validation error names host requirement");

	OutputTarget legacyTikTok = valid;
	legacyTikTok.platformId = "tiktok";
	legacyTikTok.serverUrl = "rtmp://push.tiktokcdn.com/live";
	check(!validateOutputTargetConfig(legacyTikTok, &error), "legacy generic TikTok URL fails validation");
	check(error == "TikTok needs the server URL shown in TikTok LIVE setup.",
	      "legacy generic TikTok URL explains how to get the correct server");
	legacyTikTok.serverUrl = "rtmps://live.tiktok.example/live";
	check(validateOutputTargetConfig(legacyTikTok, &error), "TikTok accepts a server supplied by LIVE setup");
	check(error.isEmpty(), "valid TikTok server has no validation error");

	OutputTarget emptyKey = valid;
	emptyKey.streamKey = " ";
	check(!validateOutputTargetConfig(emptyKey, &error), "empty stream key fails validation");
	check(error == "Stream key is empty.", "empty stream key error");

	OutputTarget savedKey = emptyKey;
	savedKey.authCredentialRef = "DSK Multistream/stream-key/saved";
	check(validateOutputTargetConfig(savedKey, &error), "saved stream key reference passes validation");
	check(error.isEmpty(), "saved stream key reference has no validation error");

	OutputTarget disabledTarget = valid;
	disabledTarget.enabled = false;
	check(!disabledTarget.enabled, "disabled target flag can be set");
	check(!validateOutputTargetConfig(disabledTarget, &error), "disabled target fails validation");
	check(error == "Target is disabled.", "disabled target error");
	check(validateOutputTargetConfig(disabledTarget, &error, false),
	      "disabled target can be validated for an explicit individual start");

	OutputTarget mismatchedLogin = valid;
	mismatchedLogin.platformId = "youtube";
	mismatchedLogin.authMode = TargetAuthMode::TwitchOAuth;
	check(!validateOutputTargetConfig(mismatchedLogin, &error), "mismatched login mode fails validation");
	check(error == "Login mode does not match the selected platform.", "mismatched login error");

	OutputTarget noStart = valid;
	noStart.id = "no-start";
	noStart.startWithAll = false;
	OutputTarget disabledStart = valid;
	disabledStart.id = "disabled-start";
	disabledStart.enabled = false;
	const QVector<QString> startIds = startAllTargetIds({valid, noStart, disabledStart});
	check(startIds.size() == 1, "start all selects one eligible target");
	check(startIds[0] == "valid", "start all selects enabled include target");

	check(valid.useSharedEncoder, "targets default to shared encoder");
	check(!valid.autoStartWithObs, "targets default to manual start instead of OBS stream auto-start");
	check(valid.autoStopWithObs, "targets default to OBS stream auto-stop");
	check(valid.reconnectEnabled, "targets default to reconnect enabled");
	check(valid.reconnectMaxRetries == 20, "targets default reconnect retries");
	check(valid.reconnectDelaySeconds == 2, "targets default reconnect delay");
	check(valid.keyframeSeconds == 2, "targets default keyframe interval");

	OutputTarget invalidReconnect = valid;
	invalidReconnect.reconnectDelaySeconds = 0;
	check(!validateOutputTargetConfig(invalidReconnect, &error), "zero reconnect delay fails validation");
	check(error == "Reconnect settings are invalid.", "zero reconnect delay error");

	invalidReconnect = valid;
	invalidReconnect.reconnectMaxRetries = -1;
	check(!validateOutputTargetConfig(invalidReconnect, &error), "negative reconnect retries fail validation");

	invalidReconnect.reconnectEnabled = false;
	check(validateOutputTargetConfig(invalidReconnect, &error), "disabled reconnect ignores retry limits");

	OutputTarget invalidEncoder = valid;
	invalidEncoder.videoBitrateKbps = 250000;
	check(!validateOutputTargetConfig(invalidEncoder, &error), "excessive video bitrate fails validation");

	QString secretError;
	QString secretValue;
	SecretStore secretStore;
	check(secretStore.readSecretResult(QString(), &secretValue, &secretError) == SecretReadResult::Error,
	      "empty credential reference reports a read error");
	check(SecretStore::isOwnedCredentialRef(SecretStore::streamKeyCredentialRef("target-1")),
	      "generated stream key credential belongs to DSK");
	check(SecretStore::isOwnedCredentialRef(SecretStore::oauthClientSecretCredentialRef("target-1")),
	      "generated OAuth client credential belongs to DSK");
	check(!SecretStore::isOwnedCredentialRef(QStringLiteral("DSK Multistream/stream-key/")),
	      "empty DSK credential suffix is rejected");
	check(!SecretStore::isOwnedCredentialRef(QStringLiteral("Unrelated App/credential")),
	      "foreign generic credential does not belong to DSK");
}

void testLayoutsAndProfiles()
{
	using namespace dsk;

	check(fitModeToString(FitMode::Fit) == "fit", "fit mode string");
	check(fitModeToString(FitMode::Fill) == "fill", "fill mode string");
	check(fitModeToString(FitMode::Stretch) == "stretch", "stretch mode string");
	check(fitModeFromString("fit") == FitMode::Fit, "fit mode parse");
	check(fitModeFromString("stretch") == FitMode::Stretch, "stretch mode parse");
	check(fitModeFromString("bad") == FitMode::Fill, "bad fit mode defaults fill");

	LayoutManager layouts;
	check(layouts.verticalLayout().items.isEmpty(), "new vertical layout starts empty");
	layouts.applyTemplate("full-screen");
	check(layouts.verticalLayout().items.isEmpty(), "template does not create placeholder items");

	VerticalLayout customLayout;
	customLayout.items.push_back({
		"main-source",
		"Game Capture",
		QRectF(100, 200, 300, 400),
		QRectF(),
		FitMode::Fill,
		true,
	});
	customLayout.items.push_back({
		"camera-source",
		"Camera",
		QRectF(10, 20, 100, 100),
		QRectF(),
		FitMode::Fill,
		true,
	});
	layouts.setVerticalLayout(customLayout);
	layouts.applyTemplate("full-screen");
	check(layouts.verticalLayout().items.size() == 2, "full-screen preserves existing items");
	check(layouts.verticalLayout().items[0].rect.width() == 1080, "full-screen width");
	check(layouts.verticalLayout().items[0].rect.height() == 1920, "full-screen height");
	check(layouts.verticalLayout().items[0].fitMode == FitMode::Fit, "full-screen uses fit");
	check(layouts.verticalLayout().items[0].sourceName == "Game Capture", "template preserves source name");

	layouts.applyTemplate("game-camera");
	check(layouts.verticalLayout().items.size() == 2, "game-camera preserves two items");
	check(layouts.verticalLayout().items[1].rect.x() == 670, "game-camera camera x");
	check(layouts.verticalLayout().items[1].rect.y() == 1240, "game-camera camera y");

	layouts.applyTemplate("camera-first");
	check(layouts.verticalLayout().items.size() == 2, "camera-first has two items");
	check(layouts.verticalLayout().items[0].rect.height() == 1420, "camera-first main item height");

	VerticalLayoutScene sanitizedScene;
	sanitizedScene.id = "sanitize-scene";
	sanitizedScene.name = "Sanitize";
	sanitizedScene.layout.items = customLayout.items;
	sanitizedScene.layout.items.push_back({"internal-program", "DSK Vertical Program", QRectF(0, 0, 1080, 1920)});
	sanitizedScene.layout.items.push_back({"internal-preview", "DSK Vertical Preview", QRectF(0, 0, 1080, 1920)});
	layouts.initializeVerticalScenes({sanitizedScene}, sanitizedScene.id, {});
	check(layouts.verticalLayout().items.size() == customLayout.items.size(),
	      "internal vertical render scenes cannot become recursive layout sources");
	const QString secondSceneId = layouts.createVerticalScene("Gameplay");
	const QString thirdSceneId = layouts.createVerticalScene("Gameplay");
	check(layouts.verticalSceneName(secondSceneId) == "Gameplay", "vertical scene keeps requested unique name");
	check(layouts.verticalSceneName(thirdSceneId) == "Gameplay 2", "vertical scene duplicate name is disambiguated");
	check(layouts.renameVerticalScene(thirdSceneId, "Camera"), "vertical scene can be renamed");
	check(layouts.verticalSceneName(thirdSceneId) == "Camera", "vertical scene rename is applied");
	check(layouts.moveVerticalScene(thirdSceneId, -1), "vertical scene can move up");
	check(layouts.verticalScenes()[1].id == thirdSceneId, "vertical scene move updates display order");
	check(layouts.removeVerticalScene(thirdSceneId), "vertical scene can be removed");
	check(layouts.activeVerticalSceneId() == secondSceneId, "removing active scene selects a neighboring scene");
	check(layouts.removeVerticalScene(secondSceneId), "second vertical scene can be removed");
	check(!layouts.removeVerticalScene(sanitizedScene.id), "last vertical scene cannot be removed");

	VerticalLayoutItem fitItem;
	fitItem.rect = QRectF(0, 0, 1080, 1920);
	fitItem.fitMode = FitMode::Fit;
	const QRectF fittedContent = displayedContentRect(fitItem, QSizeF(1920, 1080));
	check(qAbs(fittedContent.width() - 1080.0) < 0.001, "fit geometry preserves full visible width");
	check(qAbs(fittedContent.height() - 607.5) < 0.001, "fit geometry preserves source aspect ratio");
	check(qAbs(fittedContent.y() - 656.25) < 0.001, "fit geometry centers visible content");
	const QRectF centeredWideSource = centeredAspectFitRect(QSizeF(1920, 1080), QSizeF(1080, 1920));
	check(qAbs(centeredWideSource.x()) < 0.001 && qAbs(centeredWideSource.y() - 656.25) < 0.001,
	      "16:9 source is centered in a 9:16 canvas");
	check(qAbs(centeredWideSource.width() - 1080.0) < 0.001 &&
		      qAbs(centeredWideSource.height() - 607.5) < 0.001,
	      "16:9 source keeps its aspect ratio when fitted to a 9:16 canvas");
	check(centeredAspectFitRect(QSizeF(), QSizeF(1080, 1920)) == QRectF(0, 0, 1080, 1920),
	      "unknown source size safely falls back to the full canvas");
	fitItem.fitMode = FitMode::Fill;
	check(displayedContentRect(fitItem, QSizeF(1920, 1080)) == fitItem.rect,
	      "fill geometry uses the complete visible bounds");

	const QRectF reference(0, 0, 160, 90);
	const QRectF rightResize = aspectConstrainedResize(QRectF(0, 0, 320, 90), reference, ResizeRight);
	check(qAbs(rightResize.width() - 320.0) < 0.001 && qAbs(rightResize.height() - 180.0) < 0.001,
	      "fit side resize keeps the visible source aspect ratio");
	check(qAbs(rightResize.center().y() - reference.center().y()) < 0.001,
	      "fit side resize stays centered on the untouched axis");
	const QRectF cornerResize =
		aspectConstrainedResize(QRectF(0, 0, 320, 100), reference, ResizeRight | ResizeBottom);
	check(qAbs(cornerResize.width() / cornerResize.height() - 16.0 / 9.0) < 0.001,
	      "fit corner resize keeps the visible source aspect ratio");
	check(cornerResize.topLeft() == reference.topLeft(), "fit corner resize keeps the opposite corner anchored");

	VerticalLayout overlappingLayout;
	overlappingLayout.items.push_back({"front", "Front", QRectF(0, 0, 400, 400)});
	overlappingLayout.items.push_back({"back", "Back", QRectF(0, 0, 200, 200)});
	const QVector<QRectF> overlappingDisplayRects{QRectF(0, 0, 400, 400), QRectF(0, 0, 200, 200)};
	check(verticalPreviewHitItem(overlappingLayout, overlappingDisplayRects, QPointF(100, 100)) == 0,
	      "vertical preview selects the front-most source when no source is selected");
	check(verticalPreviewHitItem(overlappingLayout, overlappingDisplayRects, QPointF(100, 100), 1) == 1,
	      "vertical preview keeps an overlapping selected source interactive");
	check(verticalPreviewHitItem(overlappingLayout, overlappingDisplayRects, QPointF(300, 300), 1) == -1,
	      "clicking outside the selected vertical source clears selection instead of selecting a background source");
	overlappingLayout.items[1].visible = false;
	check(verticalPreviewHitItem(overlappingLayout, overlappingDisplayRects, QPointF(100, 100), 1) == 0,
	      "vertical preview does not prioritize a hidden selected source");
	check(verticalPreviewHitItem(overlappingLayout, overlappingDisplayRects, QPointF(500, 500), 0) == -1,
	      "vertical preview background is not treated as the selected source");

	VerticalLayout oversizedPreviewLayout;
	oversizedPreviewLayout.width = 1080;
	oversizedPreviewLayout.height = 1920;
	oversizedPreviewLayout.items.push_back(
		{"oversized", "Oversized", QRectF(-540, -960, 2160, 3840)});
	const QVector<QRectF> oversizedDisplayRects{oversizedPreviewLayout.items[0].rect};
	const QRectF previewCanvas(187.5, 0.0, 225.0, 400.0);
	check(verticalPreviewHitItemAtWidgetPoint(oversizedPreviewLayout, oversizedDisplayRects, previewCanvas,
					      QPointF(100.0, 200.0), 0) == -1,
	      "space outside the rendered vertical canvas never hits an oversized selected source");
	check(verticalPreviewHitItemAtWidgetPoint(oversizedPreviewLayout, oversizedDisplayRects, previewCanvas,
					      QPointF(300.0, 200.0), 0) == 0,
	      "a point inside the rendered vertical canvas still hits the selected source");

	VerticalLayout unsafeLayout;
	unsafeLayout.width = 0;
	unsafeLayout.height = std::numeric_limits<int>::max();
	unsafeLayout.items.push_back({"unsafe",
				      "Camera",
				      QRectF(std::numeric_limits<double>::infinity(),
					     std::numeric_limits<double>::quiet_NaN(),
					     -1.0,
					     std::numeric_limits<double>::infinity()),
				      QRectF(-1.0,
					     std::numeric_limits<double>::infinity(),
					     -2.0,
					     std::numeric_limits<double>::quiet_NaN()),
				      FitMode::Fit,
				      true});
	normalizeVerticalLayoutGeometry(unsafeLayout);
	check(unsafeLayout.width == 1080 && unsafeLayout.height == 1920,
	      "unsafe canvas dimensions normalize to portrait defaults");
	check(std::isfinite(unsafeLayout.items[0].rect.x()) && std::isfinite(unsafeLayout.items[0].rect.y()) &&
		      unsafeLayout.items[0].rect.width() > 0.0 && unsafeLayout.items[0].rect.height() > 0.0,
	      "unsafe layout rectangle normalizes to finite positive geometry");
	check(unsafeLayout.items[0].crop.x() >= 0.0 && unsafeLayout.items[0].crop.y() >= 0.0 &&
		      unsafeLayout.items[0].crop.width() >= 0.0 && unsafeLayout.items[0].crop.height() >= 0.0,
	      "unsafe crop geometry normalizes to non-negative values");

	EncoderProfileManager profiles;
	check(profiles.profileFor(EncoderGroup::DskHorizontal).width == 1920, "horizontal profile width");
	check(profiles.profileFor(EncoderGroup::DskHorizontal).height == 1080, "horizontal profile height");
	check(profiles.profileFor(EncoderGroup::DskVertical).width == 1080, "vertical profile width");
	check(profiles.profileFor(EncoderGroup::DskVertical).height == 1920, "vertical profile height");
}

void testSettingsCodec()
{
	using namespace dsk;

	OutputTarget target;
	target.id = "target-1";
	target.name = "YouTube vertical";
	target.platformId = "youtube";
	target.authMode = TargetAuthMode::YouTubeOAuth;
	target.authAccountName = "channel@example.test";
	target.authCredentialRef = "dsk/youtube/target-1";
	target.oauthClientId = "oauth-client-id";
	target.oauthClientSecret = "oauth-client-secret";
	target.oauthClientSecretRef = "DSK Multistream/oauth-client-secret/target-1";
	target.oauthRefreshToken = "oauth-refresh-token";
	target.oauthRefreshTokenRef = "DSK Multistream/oauth-refresh-token/target-1";
	target.serverUrl = "rtmps://example.test/live";
	target.streamKey = "stream-secret";
	target.encoderGroup = EncoderGroup::DskVertical;
	target.useSharedEncoder = false;
	target.autoStartWithObs = false;
	target.autoStopWithObs = true;
	target.reconnectEnabled = true;
	target.reconnectMaxRetries = 42;
	target.reconnectDelaySeconds = 7;
	target.videoBitrateKbps = 9000;
	target.audioBitrateKbps = 192;
	target.keyframeSeconds = 4;
	target.videoEncoderId = "obs_x264";
	target.audioEncoderId = "ffmpeg_aac";
	target.enabled = false;
	target.startWithAll = false;

	const OutputTarget decodedTarget = outputTargetFromJson(outputTargetToJson(target));
	check(decodedTarget.id == target.id, "target codec preserves id");
	check(decodedTarget.name == target.name, "target codec preserves name");
	check(decodedTarget.platformId == target.platformId, "target codec preserves platform");
	check(decodedTarget.authMode == target.authMode, "target codec preserves auth mode");
	check(decodedTarget.authAccountName == target.authAccountName, "target codec preserves auth account");
	check(decodedTarget.authCredentialRef == target.authCredentialRef, "target codec preserves auth credential ref");
	check(decodedTarget.oauthClientId == target.oauthClientId, "target codec preserves OAuth client id");
	check(decodedTarget.oauthClientSecret == target.oauthClientSecret, "target codec preserves OAuth client secret");
	check(decodedTarget.oauthClientSecretRef == target.oauthClientSecretRef, "target codec preserves OAuth client secret ref");
	check(decodedTarget.oauthRefreshToken == target.oauthRefreshToken, "target codec preserves OAuth refresh token");
	check(decodedTarget.oauthRefreshTokenRef == target.oauthRefreshTokenRef, "target codec preserves OAuth refresh token ref");
	check(decodedTarget.serverUrl == target.serverUrl, "target codec preserves server");
	check(decodedTarget.streamKey == target.streamKey, "target codec preserves stream key");
	check(decodedTarget.encoderGroup == target.encoderGroup, "target codec preserves encoder group");
	check(decodedTarget.useSharedEncoder == target.useSharedEncoder, "target codec preserves shared encoder flag");
	check(decodedTarget.autoStartWithObs == target.autoStartWithObs, "target codec preserves OBS auto-start flag");
	check(decodedTarget.autoStopWithObs == target.autoStopWithObs, "target codec preserves OBS auto-stop flag");
	check(decodedTarget.reconnectEnabled == target.reconnectEnabled, "target codec preserves reconnect enabled");
	check(decodedTarget.reconnectMaxRetries == target.reconnectMaxRetries, "target codec preserves reconnect retries");
	check(decodedTarget.reconnectDelaySeconds == target.reconnectDelaySeconds, "target codec preserves reconnect delay");
	check(decodedTarget.videoBitrateKbps == target.videoBitrateKbps, "target codec preserves video bitrate");
	check(decodedTarget.audioBitrateKbps == target.audioBitrateKbps, "target codec preserves audio bitrate");
	check(decodedTarget.keyframeSeconds == target.keyframeSeconds, "target codec preserves keyframe interval");
	check(decodedTarget.videoEncoderId == target.videoEncoderId, "target codec preserves video encoder id");
	check(decodedTarget.audioEncoderId == target.audioEncoderId, "target codec preserves audio encoder id");
	check(decodedTarget.enabled == target.enabled, "target codec preserves enabled flag");
	check(decodedTarget.startWithAll == target.startWithAll, "target codec preserves start-all flag");

	QJsonObject minimalTargetJson;
	minimalTargetJson.insert("encoderGroup", "unknown");
	const OutputTarget minimalTarget = outputTargetFromJson(minimalTargetJson);
	check(!minimalTarget.id.isEmpty(), "target codec generates id for missing id");
	check(minimalTarget.name == "Untitled", "target codec defaults missing name");
	check(minimalTarget.platformId == "custom", "target codec defaults missing platform");
	check(minimalTarget.authMode == TargetAuthMode::ManualRtmp, "target codec defaults manual auth");
	check(minimalTarget.encoderGroup == EncoderGroup::DskHorizontal, "target codec defaults unknown encoder group");
	check(minimalTarget.useSharedEncoder, "target codec defaults shared encoder");
	check(!minimalTarget.autoStartWithObs, "target codec defaults OBS auto-start off");
	check(minimalTarget.autoStopWithObs, "target codec defaults OBS auto-stop");
	check(minimalTarget.reconnectEnabled, "target codec defaults reconnect enabled");
	check(minimalTarget.reconnectMaxRetries == 20, "target codec defaults reconnect retries");
	check(minimalTarget.reconnectDelaySeconds == 2, "target codec defaults reconnect delay");
	check(minimalTarget.keyframeSeconds == 2, "target codec defaults keyframe interval");
	check(minimalTarget.enabled, "target codec defaults enabled");
	check(minimalTarget.startWithAll, "target codec defaults start-all enabled");

	QJsonObject legacyObsMainTargetJson;
	legacyObsMainTargetJson.insert("encoderGroup", "obs-main");
	const OutputTarget legacyObsMainTarget = outputTargetFromJson(legacyObsMainTargetJson);
	check(legacyObsMainTarget.encoderGroup == EncoderGroup::DskHorizontal,
	      "target codec migrates legacy OBS Main targets to DSK horizontal");

	VerticalLayout layout;
	layout.width = 1080;
	layout.height = 1920;
	layout.templateId = "camera-first";
	layout.items.push_back({
		"item-1",
		"Game Capture",
		QRectF(10, 20, 900, 1200),
		QRectF(1, 2, 3, 4),
		FitMode::Fit,
		true,
	});
	layout.items.push_back({
		"item-2",
		"Camera",
		QRectF(100, 1300, 400, 500),
		QRectF(0, 0, 0, 0),
		FitMode::Stretch,
		false,
	});

	const VerticalLayout decodedLayout = verticalLayoutFromJson(verticalLayoutToJson(layout));
	check(decodedLayout.width == layout.width, "layout codec preserves width");
	check(decodedLayout.height == layout.height, "layout codec preserves height");
	check(decodedLayout.templateId == layout.templateId, "layout codec preserves template");
	check(decodedLayout.items.size() == 2, "layout codec preserves item count");
	check(decodedLayout.items[0].id == "item-1", "layout codec preserves item id");
	check(decodedLayout.items[0].sourceName == "Game Capture", "layout codec preserves source name");
	check(decodedLayout.items[0].rect == QRectF(10, 20, 900, 1200), "layout codec preserves rect");
	check(decodedLayout.items[0].crop == QRectF(1, 2, 3, 4), "layout codec preserves crop");
	check(decodedLayout.items[0].fitMode == FitMode::Fit, "layout codec preserves fit mode");
	check(decodedLayout.items[1].fitMode == FitMode::Stretch, "layout codec preserves stretch mode");
	check(!decodedLayout.items[1].visible, "layout codec preserves visibility");

	VerticalLayout legacyEmptyDefault;
	legacyEmptyDefault.items.push_back({
		"legacy-placeholder",
		QString(),
		QRectF(0, 0, 1080, 1920),
		QRectF(0, 0, 0, 0),
		FitMode::Fit,
		true,
	});
	normalizeLoadedVerticalLayout(legacyEmptyDefault);
	check(legacyEmptyDefault.items.isEmpty(), "layout normalize removes legacy empty default layer");

	VerticalLayout emptyNamedSource;
	emptyNamedSource.items.push_back({
		"blank-source",
		QString(),
		QRectF(123, 456, 789, 111),
		QRectF(0, 0, 0, 0),
		FitMode::Fill,
		true,
	});
	normalizeLoadedVerticalLayout(emptyNamedSource);
	check(emptyNamedSource.items.isEmpty(), "layout normalize removes any blank source item");

	VerticalLayout realFullScreenLayer;
	realFullScreenLayer.items.push_back({
		"real-layer",
		"STREAM PC",
		QRectF(0, 0, 1080, 1920),
		QRectF(0, 0, 0, 0),
		FitMode::Fit,
		true,
	});
	normalizeLoadedVerticalLayout(realFullScreenLayer);
	check(realFullScreenLayer.items.size() == 1, "layout normalize keeps real full-screen source layer");

	QJsonArray mixedItems;
	mixedItems.push_back(QStringLiteral("not-an-object"));
	QJsonObject sparseItem;
	sparseItem.insert("fitMode", "invalid");
	mixedItems.push_back(sparseItem);
	QJsonObject sparseLayoutJson;
	sparseLayoutJson.insert("items", mixedItems);
	const VerticalLayout sparseLayout = verticalLayoutFromJson(sparseLayoutJson);
	check(sparseLayout.width == 1080, "layout codec defaults missing width");
	check(sparseLayout.height == 1920, "layout codec defaults missing height");
	check(sparseLayout.templateId == "full-screen", "layout codec defaults template");
	check(sparseLayout.items.size() == 1, "layout codec ignores non-object items");
	check(!sparseLayout.items[0].id.isEmpty(), "layout codec generates missing item id");
	check(sparseLayout.items[0].rect == QRectF(0, 0, 1080, 1920), "layout codec defaults sparse rect");
	check(sparseLayout.items[0].fitMode == FitMode::Fill, "layout codec defaults invalid fit mode");
	check(sparseLayout.items[0].visible, "layout codec defaults item visibility");

	const SceneLayoutLink link{"OBS Scene", "obs-scene-uuid", "vertical-scene-1", {}};
	const SceneLayoutLink decodedLink = sceneLayoutLinkFromJson(sceneLayoutLinkToJson(link));
	check(decodedLink.sceneName == link.sceneName, "scene link codec preserves scene name");
	check(decodedLink.sceneUuid == link.sceneUuid, "scene link codec preserves OBS scene UUID");
	check(decodedLink.verticalSceneId == link.verticalSceneId, "scene link codec preserves DSK vertical scene id");
	check(decodedLink.legacyTemplateId.isEmpty(), "scene link codec omits legacy template when unused");

	const SceneLayoutLink defaultLink = sceneLayoutLinkFromJson(QJsonObject{});
	check(defaultLink.sceneName.isEmpty(), "scene link codec defaults missing scene name");
	check(defaultLink.verticalSceneId.isEmpty(), "scene link codec defaults missing DSK vertical scene");
	QJsonObject legacyLinkJson;
	legacyLinkJson.insert("templateId", "full-screen");
	const SceneLayoutLink legacyLink = sceneLayoutLinkFromJson(legacyLinkJson);
	check(legacyLink.legacyTemplateId == "full-screen", "scene link codec reads legacy template id");

	OutputTarget sceneTarget;
	sceneTarget.id = "scene-target";
	sceneTarget.name = "Scene Target";
	sceneTarget.platformId = "youtube";
	sceneTarget.serverUrl = "rtmp://a.rtmp.youtube.com/live2";
	sceneTarget.streamKey = "secret";
	sceneTarget.sceneMode = TargetSceneMode::LinkedScene;
	sceneTarget.sceneName = "Fallback YouTube";
	sceneTarget.sceneUuid = "fallback-youtube-uuid";
	sceneTarget.sceneRoutes.push_back({"Game", "game-uuid", "Game YouTube", "game-youtube-uuid"});
	sceneTarget.sceneRoutes.push_back({"Talk", "talk-uuid", "Talk YouTube", "talk-youtube-uuid"});
	const OutputTarget decodedSceneTarget = outputTargetFromJson(outputTargetToJson(sceneTarget));
	check(decodedSceneTarget.sceneMode == TargetSceneMode::LinkedScene, "target codec preserves linked scene mode");
	check(decodedSceneTarget.sceneName == sceneTarget.sceneName, "target codec preserves fallback scene name");
	check(decodedSceneTarget.sceneUuid == sceneTarget.sceneUuid, "target codec preserves fallback scene UUID");
	check(decodedSceneTarget.sceneRoutes.size() == 2, "target codec preserves scene routes");
	check(decodedSceneTarget.sceneRoutes[0].obsSceneName == "Game", "target codec preserves route obs scene");
	check(decodedSceneTarget.sceneRoutes[0].obsSceneUuid == "game-uuid", "target codec preserves route OBS UUID");
	check(decodedSceneTarget.sceneRoutes[0].outputSceneName == "Game YouTube", "target codec preserves route output scene");
	check(decodedSceneTarget.sceneRoutes[0].outputSceneUuid == "game-youtube-uuid", "target codec preserves route output UUID");
}

void testPlatformRegistry()
{
	dsk::PlatformPresetRegistry registry;
	check(registry.presets().size() == 5, "registry has five presets");
	check(registry.presetById("twitch").defaultServer == "rtmp://live.twitch.tv/app", "Twitch default server");
	check(registry.presetById("youtube").defaultServer == "rtmp://a.rtmp.youtube.com/live2", "YouTube default server");
	check(registry.presetById("kick").defaultServer.startsWith("rtmps://"), "Kick uses RTMPS");
	check(registry.presetById("tiktok").verticalCommon, "TikTok marked vertical common");
	check(registry.presetById("tiktok").recommendedOutput == "dsk-vertical", "TikTok recommends vertical output");
	check(registry.presetById("youtube").horizontalBitrateKbps >= 6000, "YouTube horizontal bitrate present");
	check(registry.presetById("missing").id == "custom", "missing preset falls back to custom");
}

void testOAuthProviders()
{
	using namespace dsk;

	const OAuthProvider twitch = oauthProviderForAuthMode(TargetAuthMode::TwitchOAuth);
	check(twitch.id == "twitch", "Twitch OAuth provider id");
	check(twitch.authorizeUrl.host() == "id.twitch.tv", "Twitch OAuth authorize host");
	check(twitch.scopes.contains("channel:read:stream_key"), "Twitch OAuth stream key scope");
	check(oauthUsesPublisherRelay(TargetAuthMode::TwitchOAuth), "Twitch OAuth uses the publisher relay");
	check(oauthPublisherRelayProfile(TargetAuthMode::TwitchOAuth) == "multistream",
	      "Twitch OAuth uses the multistream relay profile");
	const QUrl twitchRelayUrl = oauthPublisherRelayAuthorizeUrl(
		twitch, QUrl("http://localhost:17371/callback"), "state-value", "challenge-value");
	const QUrlQuery twitchRelayQuery(twitchRelayUrl);
	check(twitchRelayUrl.scheme() == "https", "Twitch publisher relay uses HTTPS");
	check(twitchRelayUrl.path() == "/v1/twitch/authorize", "Twitch publisher relay authorize path");
	check(twitchRelayQuery.queryItemValue("profile") == "multistream", "Twitch relay URL pins the multistream profile");
	check(twitchRelayQuery.queryItemValue("redirect_uri") == "http://localhost:17371/callback",
	      "Twitch relay URL pins the OBS callback");
	check(!twitchRelayQuery.hasQueryItem("client_id"), "Twitch relay URL does not require a user Client ID");
	check(oauthPublisherRelayTokenUrl(twitch).path() == "/v1/twitch/token", "Twitch publisher relay token path");
	QJsonObject relayResponse{
		{QStringLiteral("profile"), QStringLiteral("multistream")},
		{QStringLiteral("client_id"), QStringLiteral("public-client-id")},
		{QStringLiteral("scope"), QJsonArray{QStringLiteral("channel:read:stream_key")}},
	};
	QString publicClientId;
	check(oauthValidatePublisherRelayTokenMetadata(twitch, relayResponse, &publicClientId).isEmpty(),
	      "Twitch relay metadata accepts the expected profile and scope");
	check(publicClientId == "public-client-id", "Twitch relay metadata returns the public Client ID");
	relayResponse.insert(QStringLiteral("scope"), QStringLiteral("chat:read channel:read:stream_key"));
	check(oauthValidatePublisherRelayTokenMetadata(twitch, relayResponse).isEmpty(),
	      "Twitch relay metadata accepts required permissions with additional granted scopes");
	relayResponse.insert(QStringLiteral("scope"), QStringLiteral("channel:read:stream_key"));
	check(oauthValidatePublisherRelayTokenMetadata(twitch, relayResponse).isEmpty(),
	      "Twitch relay metadata accepts an exact space-delimited scope response");
	relayResponse.insert(QStringLiteral("scope"), QJsonArray{QStringLiteral("chat:read")});
	check(oauthValidatePublisherRelayTokenMetadata(twitch, relayResponse).contains("required permission"),
	      "Twitch relay metadata rejects a missing stream-key scope");
	relayResponse.insert(QStringLiteral("scope"), QJsonArray{QStringLiteral("channel:read:stream_key")});
	relayResponse.insert(QStringLiteral("profile"), QStringLiteral("comment-viewer"));
	check(oauthValidatePublisherRelayTokenMetadata(twitch, relayResponse).contains("wrong application profile"),
	      "Twitch relay metadata rejects a cross-profile response");

	const OAuthProvider kick = oauthProviderForAuthMode(TargetAuthMode::KickOAuth);
	check(kick.id == "kick", "Kick OAuth provider id");
	check(kick.authorizeUrl.host() == "id.kick.com", "Kick OAuth authorize host");
	check(kick.scopes.contains("streamkey:read"), "Kick OAuth stream-key scope");
	check(oauthUsesPublisherRelay(TargetAuthMode::KickOAuth), "Kick OAuth uses the publisher relay");
	check(oauthPublisherRelayProfile(TargetAuthMode::KickOAuth) == "multistream",
	      "Kick OAuth uses the multistream relay profile");
	const QUrl kickRelayUrl = oauthPublisherRelayAuthorizeUrl(
		kick, QUrl("http://localhost:17371/callback"), "state-value", "challenge-value");
	const QUrlQuery kickRelayQuery(kickRelayUrl);
	check(kickRelayUrl.path() == "/v1/kick/authorize", "Kick publisher relay authorize path");
	check(kickRelayQuery.queryItemValue("profile") == "multistream", "Kick relay URL pins the multistream profile");
	QJsonObject kickRelayResponse{
		{QStringLiteral("profile"), QStringLiteral("multistream")},
		{QStringLiteral("client_id"), QStringLiteral("kick-public-client-id")},
		{QStringLiteral("scope"),
		 QJsonArray{QStringLiteral("user:read"), QStringLiteral("channel:read"),
			    QStringLiteral("streamkey:read")}},
	};
	check(oauthValidatePublisherRelayTokenMetadata(kick, kickRelayResponse).isEmpty(),
	      "Kick relay metadata accepts the exact multistream permissions");
	kickRelayResponse.insert(QStringLiteral("scope"),
				 QJsonArray{QStringLiteral("user:read"), QStringLiteral("channel:read"),
					    QStringLiteral("streamkey:read"), QStringLiteral("chat:write"),
					    QStringLiteral("events:subscribe")});
	check(oauthValidatePublisherRelayTokenMetadata(kick, kickRelayResponse).isEmpty(),
	      "Kick relay metadata accepts required permissions with additional granted scopes");
	kickRelayResponse.insert(QStringLiteral("scope"),
				 QJsonArray{QStringLiteral("user:read"), QStringLiteral("channel:read")});
	check(oauthValidatePublisherRelayTokenMetadata(kick, kickRelayResponse).contains("required permission"),
	      "Kick relay metadata rejects a missing stream-key permission");

	const QJsonObject kickChannelResponse{
		{QStringLiteral("data"),
		 QJsonArray{QJsonObject{
			 {QStringLiteral("slug"), QStringLiteral("kick-user")},
			 {QStringLiteral("stream"),
			  QJsonObject{{QStringLiteral("url"), QStringLiteral("rtmps://stream.kick.com/1234567890")},
				      {QStringLiteral("key"), QStringLiteral("kick-secret-key")}}},
		 }}},
	};
	const KickChannelConnection kickConnection = oauthParseKickChannelResponse(kickChannelResponse);
	check(kickConnection.isComplete(), "Kick channel response yields a complete stream connection");
	check(kickConnection.accountName == "kick-user", "Kick channel response yields the account slug");
	check(kickConnection.serverUrl == "rtmps://stream.kick.com/1234567890",
	      "Kick channel response yields the official stream URL");
	check(kickConnection.streamKey == "kick-secret-key", "Kick channel response yields the stream key");
	QJsonObject kickMissingKey = kickChannelResponse;
	QJsonObject missingKeyChannel = kickMissingKey.value(QStringLiteral("data")).toArray().first().toObject();
	QJsonObject missingKeyStream = missingKeyChannel.value(QStringLiteral("stream")).toObject();
	missingKeyStream.remove(QStringLiteral("key"));
	missingKeyChannel.insert(QStringLiteral("stream"), missingKeyStream);
	kickMissingKey.insert(QStringLiteral("data"), QJsonArray{missingKeyChannel});
	check(oauthParseKickChannelResponse(kickMissingKey).errorMessage.contains("stream key"),
	      "Kick channel response without a key returns an actionable error");

	const QString sanitizedJsonError = oauthSafeErrorDetail(
		QByteArrayLiteral("{\"error\":\"invalid_client\",\"error_description\":\"client_secret=TOPSECRET access_token=TOKEN\"}"));
	check(!sanitizedJsonError.contains("TOPSECRET") && !sanitizedJsonError.contains("TOKEN"),
	      "OAuth JSON errors redact credentials and access tokens");
	const QString sanitizedTextError = oauthSafeErrorDetail(
		QByteArrayLiteral("provider failed refresh_token=REFRESHSECRET code=CODESECRET"));
	check(!sanitizedTextError.contains("REFRESHSECRET") && !sanitizedTextError.contains("CODESECRET"),
	      "OAuth text errors redact refresh tokens and authorization codes");

	const OAuthProvider youtube = oauthProviderForAuthMode(TargetAuthMode::YouTubeOAuth);
	check(youtube.id == "youtube", "YouTube OAuth provider id");
	check(youtube.authorizeUrl.host() == "accounts.google.com", "YouTube OAuth authorize host");
	check(youtube.tokenUrl.host() == "oauth2.googleapis.com", "YouTube OAuth token host");
	check(youtube.scopes.contains("https://www.googleapis.com/auth/youtube.force-ssl"), "YouTube OAuth live scope");
	check(!oauthUsesPublisherRelay(TargetAuthMode::YouTubeOAuth), "YouTube keeps its current direct OAuth path");
	const OAuthClientCredentials bundledYouTube = oauthBundledClientCredentials(TargetAuthMode::YouTubeOAuth);
	check(oauthHasBundledClientCredentials(TargetAuthMode::YouTubeOAuth) == bundledYouTube.isComplete(),
	      "YouTube bundled OAuth availability matches credential completeness");
	check(!oauthBundledClientCredentials(TargetAuthMode::TwitchOAuth).hasAny(),
	      "Twitch never reads desktop bundled credentials");
	const OAuthClientCredentials defaultYouTube = oauthEffectiveClientCredentials(
		TargetAuthMode::YouTubeOAuth, QString(), QString());
	check(defaultYouTube.clientId == bundledYouTube.clientId &&
	      defaultYouTube.clientSecret == bundledYouTube.clientSecret,
	      "empty target credentials use the bundled YouTube OAuth application");
	const OAuthClientCredentials partialCustom = oauthEffectiveClientCredentials(
		TargetAuthMode::YouTubeOAuth, QStringLiteral("custom-client"), QString());
	check(partialCustom.clientId == "custom-client" && partialCustom.clientSecret.isEmpty() &&
	      !partialCustom.isComplete(),
	      "partial custom credentials never mix with bundled publisher credentials");
	const OAuthClientCredentials fullCustom = oauthEffectiveClientCredentials(
		TargetAuthMode::YouTubeOAuth, QStringLiteral("custom-client"), QStringLiteral("custom-secret"));
	check(fullCustom.clientId == "custom-client" && fullCustom.clientSecret == "custom-secret" &&
	      fullCustom.isComplete(),
	      "complete custom credentials override bundled publisher credentials");
	check(oauthHasUsableClientCredentials(TargetAuthMode::YouTubeOAuth, QStringLiteral("custom-client"),
					       QString(), QStringLiteral("DSK Multistream/oauth-client-secret/target")),
	      "a custom Client ID plus a saved DSK Client Secret reference is usable in dock state");
	check(!oauthHasUsableClientCredentials(TargetAuthMode::YouTubeOAuth, QStringLiteral("custom-client"),
						QString(), QString()),
	      "a partial custom OAuth application never falls back to bundled credentials");
	check(!oauthHasUsableClientCredentials(TargetAuthMode::YouTubeOAuth, QString(), QString(),
						QStringLiteral("DSK Multistream/oauth-client-secret/target")),
	      "a Client Secret reference without its custom Client ID is unusable");

	const QUrl url = oauthAuthorizeUrl(youtube, "client-id", QUrl("http://localhost:17371/callback"), "state-value", "challenge-value");
	const QString query = url.query(QUrl::FullyDecoded);
	check(query.contains("client_id=client-id"), "OAuth URL includes client id");
	check(query.contains("scope=https://www.googleapis.com/auth/youtube.force-ssl"), "OAuth URL includes scope");
	check(query.contains("code_challenge=challenge-value"), "OAuth URL includes PKCE challenge");
	check(query.contains("access_type=offline"), "YouTube OAuth URL requests offline access");
}

void testYouTubeApiWarningHelpers()
{
	using namespace dsk;

	OutputTarget youtube;
	youtube.platformId = "youtube";
	youtube.state = TargetState::Live;
	youtube.lastError = "YouTube token refresh failed: invalid_grant";
	check(isYouTubeApiWarningText(youtube.lastError), "YouTube invalid_grant is API warning text");
	check(targetHasLiveYouTubeApiWarning(youtube), "live YouTube invalid_grant is live warning");
	check(targetHasExpiredYouTubeLoginWarning(youtube), "invalid_grant is expired login warning");
	check(userFacingYouTubeApiWarningText(youtube.lastError).contains("Reconnect YouTube login"),
	      "invalid_grant user text asks for reconnect");
	check(userFacingYouTubeApiWarningText(youtube.lastError).contains("remains in preparation"),
	      "invalid_grant user text explains YouTube preparation state");
	check(liveYouTubeApiWarningRowText(youtube.lastError) == "RTMP only - login expired",
	      "invalid_grant row text distinguishes RTMP from YouTube Live");

	youtube.lastError = "YouTube broadcast start blocked: multiple active broadcasts";
	check(targetHasYouTubeApiWarning(youtube), "multiple broadcasts is API warning");
	check(userFacingYouTubeApiWarningText(youtube.lastError).contains("Multiple YouTube broadcasts"),
	      "multiple broadcasts user text");
	check(userFacingYouTubeApiWarningText(youtube.lastError).contains("DSK Streaming"),
	      "multiple broadcasts are selected in DSK Streaming");
	check(!userFacingYouTubeApiWarningText(youtube.lastError).contains("YouTube Studio"),
	      "multiple broadcasts do not send users to YouTube Studio for selection");

	youtube.lastError = "YouTube broadcast start blocked: selected broadcast is no longer available";
	check(targetHasYouTubeApiWarning(youtube), "unavailable selected broadcast is API warning");
	check(userFacingYouTubeApiWarningText(youtube.lastError).contains("selected broadcast is no longer available"),
	      "unavailable selected broadcast is explained");
	check(userFacingYouTubeApiWarningText(youtube.lastError).contains("DSK Streaming"),
	      "unavailable selected broadcast asks for another DSK Streaming choice");
	check(liveYouTubeApiWarningRowText(youtube.lastError) == "Live signal - selected broadcast unavailable",
	      "unavailable selected broadcast has a focused live row label");

	youtube.lastError = "YouTube broadcast lookup blocked: the upcoming broadcast list exceeded 10 pages";
	check(targetHasYouTubeApiWarning(youtube), "truncated broadcast lookup is API warning");
	check(userFacingYouTubeApiWarningText(youtube.lastError).contains("Too many scheduled broadcasts"),
	      "truncated broadcast lookup fails closed with corrective guidance");
	check(liveYouTubeApiWarningRowText(youtube.lastError) == "Live signal - broadcast list too large",
	      "truncated broadcast lookup has a focused live row label");

	OutputTarget twitch = youtube;
	twitch.platformId = "twitch";
	check(!targetHasYouTubeApiWarning(twitch), "non-YouTube target ignores YouTube warning text");
}

void testRuntimeStatusHelpers()
{
	using namespace dsk;

	OutputTarget youtube;
	youtube.id = "yt-runtime";
	youtube.platformId = "youtube";
	youtube.authMode = TargetAuthMode::YouTubeOAuth;
	youtube.state = TargetState::Live;
	youtube.enabled = true;

	TargetRuntimeStatus runtime;
	runtime.targetId = youtube.id;
	runtime.sessionSerial = 7;
	runtime.transport = TransportState::Active;
	runtime.platform = PlatformLiveState::RtmpSignalOnly;
	check(runtimeTransportIsRunning(runtime), "active runtime is running");
	check(runtimeStatusLabel(youtube, runtime) == "RTMP only", "YouTube RTMP-only runtime is not labeled YouTube Live");
	check(runtimeStatusDetail(youtube, runtime).contains("checking YouTube"), "YouTube RTMP-only detail explains platform check");

	runtime.platform = PlatformLiveState::Live;
	check(runtimeStatusLabel(youtube, runtime) == "YouTube Live", "YouTube platform live is labeled explicitly");
	check(runtimeStatusDetail(youtube, runtime) == "YouTube broadcast is live", "YouTube platform live detail");

	runtime.platform = PlatformLiveState::AuthExpired;
	runtime.transportMessage = "RTMP sending";
	runtime.platformMessage = "RTMP connected - reconnect YouTube login";
	check(runtimeStatusLabel(youtube, runtime) == "RTMP only", "YouTube auth warning distinguishes platform state");
	check(runtimeStatusDetail(youtube, runtime).contains("reconnect YouTube login"), "YouTube auth warning detail");

	youtube.lastError = "YouTube token refresh failed: invalid_grant";
	check(runtimeStatusLabel(youtube, runtime) == "RTMP only", "live YouTube auth error keeps RTMP-only label");
	check(runtimeStatusDetail(youtube, runtime).contains("reconnect YouTube login"),
	      "live YouTube auth error keeps runtime platform detail");
	youtube.lastError.clear();

	OutputTarget twitch = youtube;
	twitch.platformId = "twitch";
	runtime.platform = PlatformLiveState::NotApplicable;
	runtime.platformMessage.clear();
	runtime.lastUserMessage.clear();
	check(runtimeStatusLabel(twitch, runtime) == "RTMP sending", "Twitch active runtime uses transport label");
}

void testOutputSignalPolicy()
{
	for (const QString &signal : {QStringLiteral("starting"), QStringLiteral("start"),
				      QStringLiteral("activate"), QStringLiteral("reconnect"),
				      QStringLiteral("reconnect_success")}) {
		check(dsk::shouldIgnoreOutputSignalDuringPendingRelease(signal),
		      qPrintable(QStringLiteral("pending output release ignores stale %1 signals").arg(signal)));
	}

	for (const QString &signal : {QStringLiteral("stopping"), QStringLiteral("deactivate"),
				      QStringLiteral("stop")}) {
		check(!dsk::shouldIgnoreOutputSignalDuringPendingRelease(signal),
		      qPrintable(QStringLiteral("pending output release preserves %1 cleanup signals").arg(signal)));
	}
}

void testStreamControlsState()
{
	dsk::OutputTarget target;
	target.enabled = true;
	target.startWithAll = true;
	target.state = dsk::TargetState::Stopped;
	dsk::TargetRuntimeStatus runtime;
	check(dsk::targetCanStartWithAll(target, runtime), "stopped included target enables Start All");

	target.state = dsk::TargetState::Stopping;
	check(!dsk::targetCanStartWithAll(target, runtime), "stopping target does not enable Start All");
	check(dsk::targetBlocksStartAll(target, runtime), "stopping included target blocks Start All");
	target.state = dsk::TargetState::Starting;
	check(!dsk::targetCanStartWithAll(target, runtime), "starting target does not enable Start All");
	check(dsk::targetBlocksStartAll(target, runtime), "starting included target blocks Start All");
	target.state = dsk::TargetState::Live;
	check(!dsk::targetCanStartWithAll(target, runtime), "live target does not enable Start All");
	check(!dsk::targetBlocksStartAll(target, runtime), "stable live target does not block other Start All targets");

	target.state = dsk::TargetState::Stopped;
	runtime.transport = dsk::TransportState::Stopping;
	check(!dsk::targetCanStartWithAll(target, runtime), "stopping runtime does not enable Start All");
	check(dsk::targetBlocksStartAll(target, runtime), "stopping included runtime blocks Start All");
	runtime.transport = dsk::TransportState::Connected;
	check(!dsk::targetCanStartWithAll(target, runtime), "connected runtime does not enable Start All");
	runtime.transport = dsk::TransportState::Idle;
	target.enabled = false;
	check(!dsk::targetCanStartWithAll(target, runtime), "disabled target does not enable Start All");
	target.enabled = true;
	target.startWithAll = false;
	check(!dsk::targetCanStartWithAll(target, runtime), "excluded target does not enable Start All");

	check(dsk::obsNativeCanStartWithAll(true, false, false), "idle OBS native stream enables Start All");
	check(!dsk::obsNativeCanStartWithAll(true, false, true),
	      "transitioning OBS native stream does not enable Start All");
	check(!dsk::obsNativeCanStartWithAll(true, true, false), "active OBS native stream does not enable Start All");
	check(!dsk::obsNativeCanStartWithAll(false, false, false),
	      "unavailable OBS native stream does not enable Start All");
}

void testVisibleRefreshGate()
{
	dsk::VisibleRefreshGate gate;
	check(gate.isDirty(), "a newly constructed page needs its first refresh");
	check(!gate.takeIfVisible(false), "a hidden page defers its pending refresh");
	check(gate.isDirty(), "a hidden page keeps the pending refresh dirty");
	check(gate.takeIfVisible(true), "showing a dirty page consumes exactly one refresh");
	check(!gate.isDirty(), "a visible refresh clears the dirty state");
	check(!gate.takeIfVisible(true), "an unchanged visible page does not refresh again");
	gate.markDirty();
	gate.markDirty();
	check(gate.takeIfVisible(true), "a burst of changes coalesces into one visible refresh");
	check(!gate.takeIfVisible(true), "a coalesced refresh is consumed only once");
}

void testSecretStoreHelpers()
{
	using namespace dsk;

	check(SecretStore::streamKeyCredentialRef("target-1") == "DSK Multistream/stream-key/target-1", "stream key credential ref");
	check(SecretStore::oauthClientSecretCredentialRef("target-1") == "DSK Multistream/oauth-client-secret/target-1", "OAuth client secret credential ref");
#ifdef _WIN32
	check(SecretStore::isAvailable(), "Windows secret store is available");

	SecretStore secrets;
	QString error;
	QString loaded;
	const QString oversized = QString(4096, QLatin1Char('x'));
	check(!secrets.writeSecret(QStringLiteral("DSK Multistream/test/oversized"), oversized, &error),
	      "Windows secret store rejects oversized secrets before CredWrite");
	check(error.contains("too large"), "oversized secret returns a readable error");

	if (!qEnvironmentVariableIsSet("DSK_TEST_LIVE_SECRET_STORE"))
		return;

	const QString ref = QStringLiteral("DSK Multistream/test/%1")
				    .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
	const QString value = QStringLiteral("test-stream-key-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
	check(secrets.writeSecret(ref, value, &error), "Windows secret store writes a test secret");
	check(error.isEmpty(), "secret write has no error");
	check(secrets.readSecret(ref, &loaded, &error), "Windows secret store reads a test secret");
	check(loaded == value, "Windows secret store preserves secret value");
	check(secrets.deleteSecret(ref, &error), "Windows secret store deletes a test secret");
	loaded.clear();
	check(!secrets.readSecret(ref, &loaded, &error), "deleted test secret is not readable");
#else
	check(!SecretStore::isAvailable(), "non-Windows secret store is unavailable");
#endif
}

void testDataFiles()
{
	QFile presetsFile("data/presets/platforms.json");
	check(presetsFile.open(QIODevice::ReadOnly), "open platform preset json");
	const QJsonDocument document = QJsonDocument::fromJson(presetsFile.readAll());
	check(document.isObject(), "platform preset json object");

	const QJsonObject root = document.object();
	check(root.value("schemaVersion").toInt() == 1, "platform preset schema version");
	const QJsonArray platforms = root.value("platforms").toArray();
	check(platforms.size() == 5, "platform preset json has five platforms");
	dsk::PlatformPresetRegistry registry;
	check(registry.presets().size() == platforms.size(), "runtime registry and preset json have the same size");

	QSet<QString> ids;
	for (const QJsonValue &value : platforms) {
		check(value.isObject(), "platform item is object");
		const QJsonObject object = value.toObject();
		const QString id = object.value("id").toString();
		check(!id.isEmpty(), "platform id exists");
		check(!ids.contains(id), "platform id unique");
		ids.insert(id);
		check(!object.value("name").toString().isEmpty(), "platform name exists");
		check(object.contains("defaultServer"), "platform defaultServer exists");
		check(object.contains("helpUrl"), "platform helpUrl exists");
		check(object.contains("recommendedOutput"), "platform recommendedOutput exists");
		check(object.contains("horizontalBitrateKbps"), "platform horizontalBitrateKbps exists");
		check(object.contains("verticalBitrateKbps"), "platform verticalBitrateKbps exists");
		check(object.contains("note"), "platform note exists");
		check(object.contains("verticalCommon"), "platform verticalCommon exists");

		const dsk::PlatformPreset preset = registry.presetById(id);
		check(preset.id == id, qPrintable(QString("runtime preset id matches json for %1").arg(id)));
		check(preset.displayName == object.value("name").toString(),
		      qPrintable(QString("runtime preset name matches json for %1").arg(id)));
		check(preset.defaultServer == object.value("defaultServer").toString(),
		      qPrintable(QString("runtime preset server matches json for %1").arg(id)));
		check(preset.helpUrl == object.value("helpUrl").toString(),
		      qPrintable(QString("runtime preset help URL matches json for %1").arg(id)));
		check(preset.recommendedOutput == object.value("recommendedOutput").toString(),
		      qPrintable(QString("runtime preset output matches json for %1").arg(id)));
		check(preset.horizontalBitrateKbps == object.value("horizontalBitrateKbps").toInt(),
		      qPrintable(QString("runtime horizontal bitrate matches json for %1").arg(id)));
		check(preset.verticalBitrateKbps == object.value("verticalBitrateKbps").toInt(),
		      qPrintable(QString("runtime vertical bitrate matches json for %1").arg(id)));
		check(preset.note == object.value("note").toString(),
		      qPrintable(QString("runtime preset note matches json for %1").arg(id)));
		check(preset.verticalCommon == object.value("verticalCommon").toBool(),
		      qPrintable(QString("runtime vertical flag matches json for %1").arg(id)));
	}

	for (const QString &required : {"twitch", "youtube", "kick", "tiktok", "custom"})
		check(ids.contains(required), qPrintable(QString("required platform %1 exists").arg(required)));

	const QSet<QString> en = localeKeys("data/locale/en-US.ini");
	const QSet<QString> ja = localeKeys("data/locale/ja-JP.ini");
	check(!en.isEmpty(), "en-US locale has keys");
	check(en == ja, "locale key sets match");
}

QJsonObject youtubeTestBroadcast(const QString &id, const QString &streamId,
				 const QString &lifeCycleStatus = QStringLiteral("ready"))
{
	return QJsonObject{
		{QStringLiteral("id"), id},
		{QStringLiteral("contentDetails"), QJsonObject{{QStringLiteral("boundStreamId"), streamId}}},
		{QStringLiteral("status"), QJsonObject{{QStringLiteral("lifeCycleStatus"), lifeCycleStatus}}},
	};
}

QJsonObject youtubeTestStream(const QString &id, const QString &streamName,
			      const QString &streamStatus = QStringLiteral("active"))
{
	return QJsonObject{
		{QStringLiteral("id"), id},
		{QStringLiteral("status"), QJsonObject{{QStringLiteral("streamStatus"), streamStatus}}},
		{QStringLiteral("cdn"),
		 QJsonObject{{QStringLiteral("ingestionInfo"),
			      QJsonObject{{QStringLiteral("streamName"), streamName}}}}},
	};
}

void testYouTubeBroadcastSelection()
{
	using namespace dsk;

	QHash<QString, QJsonObject> streams;
	streams.insert(QStringLiteral("stream-a"), youtubeTestStream(QStringLiteral("stream-a"), QStringLiteral("key-a")));
	streams.insert(QStringLiteral("stream-b"), youtubeTestStream(QStringLiteral("stream-b"), QStringLiteral("key-b")));

	QJsonArray one{youtubeTestBroadcast(QStringLiteral("broadcast-a"), QStringLiteral("stream-a"))};
	YouTubeBroadcastSelection selection = selectYouTubeBroadcast(one, streams, QString());
	check(selection.state == YouTubeBroadcastSelectionState::Selected,
	      "one active YouTube broadcast is selected without a stream key");
	check(selection.broadcast.value(QStringLiteral("id")).toString() == QStringLiteral("broadcast-a"),
	      "YouTube selection returns the matching broadcast object");

	QJsonArray two = one;
	two.push_back(youtubeTestBroadcast(QStringLiteral("broadcast-b"), QStringLiteral("stream-b")));
	selection = selectYouTubeBroadcast(two, streams, QString());
	check(selection.state == YouTubeBroadcastSelectionState::MultipleActiveBroadcasts,
	      "multiple active YouTube broadcasts require an explicit stream key");
	selection = selectYouTubeBroadcast(two, streams, QStringLiteral("key-b"));
	check(selection.state == YouTubeBroadcastSelectionState::Selected &&
		      selection.broadcast.value(QStringLiteral("id")).toString() == QStringLiteral("broadcast-b"),
	      "stream key selects exactly one active YouTube broadcast");
	check(selection.candidates.size() == 1,
	      "YouTube selection exposes the matching broadcast candidate");
	selection = selectYouTubeBroadcast(two, streams, QStringLiteral("missing-key"));
	check(selection.state == YouTubeBroadcastSelectionState::NoStreamKeyMatch,
	      "active YouTube broadcasts with a different key are rejected");

	QHash<QString, QJsonObject> duplicateKeyStreams = streams;
	duplicateKeyStreams[QStringLiteral("stream-b")] =
		youtubeTestStream(QStringLiteral("stream-b"), QStringLiteral("key-a"));
	selection = selectYouTubeBroadcast(two, duplicateKeyStreams, QStringLiteral("key-a"));
	check(selection.state == YouTubeBroadcastSelectionState::MultipleStreamKeyMatches,
	      "duplicate YouTube stream-key bindings are never auto-transitioned");
	check(selection.candidates.size() == 2,
	      "duplicate YouTube stream-key bindings expose both choices");
	selection = selectYouTubeBroadcast(two, duplicateKeyStreams, QStringLiteral("key-a"),
				   QStringLiteral("broadcast-b"));
	check(selection.state == YouTubeBroadcastSelectionState::Selected &&
		      selection.broadcast.value(QStringLiteral("id")).toString() == QStringLiteral("broadcast-b"),
	      "an explicit YouTube broadcast choice resolves duplicate stream-key bindings");
	selection = selectYouTubeBroadcast(two, duplicateKeyStreams, QStringLiteral("key-a"),
				   QStringLiteral("missing-broadcast"));
	check(selection.state == YouTubeBroadcastSelectionState::PreferredBroadcastUnavailable &&
		      selection.broadcast.isEmpty() && selection.candidates.size() == 2,
	      "a missing explicit YouTube broadcast choice never falls back to another broadcast");

	QJsonArray completedChoice{
		youtubeTestBroadcast(QStringLiteral("broadcast-a"), QStringLiteral("stream-a")),
		youtubeTestBroadcast(QStringLiteral("broadcast-b"), QStringLiteral("stream-b"),
				     QStringLiteral("complete")),
	};
	selection = selectYouTubeBroadcast(completedChoice, duplicateKeyStreams, QStringLiteral("key-a"),
				   QStringLiteral("broadcast-b"));
	check(selection.state == YouTubeBroadcastSelectionState::PreferredBroadcastUnavailable &&
		      selection.broadcast.isEmpty() && selection.candidates.size() == 1,
	      "a completed explicit YouTube broadcast choice never falls back to a ready broadcast");

	QJsonArray completed{youtubeTestBroadcast(QStringLiteral("done"), QStringLiteral("stream-a"),
					   QStringLiteral("complete"))};
	selection = selectYouTubeBroadcast(completed, streams, QStringLiteral("key-a"));
	check(selection.state == YouTubeBroadcastSelectionState::NoActiveBroadcast,
	      "completed YouTube broadcasts are ignored");

	QJsonArray created{youtubeTestBroadcast(QStringLiteral("created"), QStringLiteral("stream-a"),
					 QStringLiteral("created"))};
	selection = selectYouTubeBroadcast(created, streams, QStringLiteral("key-a"));
	check(selection.state == YouTubeBroadcastSelectionState::NoActiveBroadcast,
	      "incomplete created YouTube broadcasts are not selected for transition");

	for (const QString &lifecycle : {QStringLiteral("testing"), QStringLiteral("testStarting"),
					 QStringLiteral("liveStarting"), QStringLiteral("live")}) {
		QJsonArray actionable{youtubeTestBroadcast(QStringLiteral("actionable"), QStringLiteral("stream-a"),
						       lifecycle)};
		selection = selectYouTubeBroadcast(actionable, streams, QStringLiteral("key-a"));
		check(selection.state == YouTubeBroadcastSelectionState::Selected,
		      qPrintable(QStringLiteral("YouTube lifecycle %1 remains actionable").arg(lifecycle)));
	}
}

void testCommentViewerObsIntegrationContract()
{
	using namespace dsk;

	const QByteArray valid = R"json({
		"ok": true,
		"service": "dsk-comment-viewer",
		"schemaVersion": 1,
		"appVersion": "0.2.0-beta.24",
		"integration": "obs-browser-dock",
		"obsDock": {
			"viewerPath": "/viewer?dock=chat&send=1",
			"capabilities": ["comments.read", "comments.send"]
		}
	})json";
	const auto integration = parseCommentViewerObsIntegration(valid);
	check(integration.has_value(), "valid Comment Viewer OBS integration contract is accepted");
	check(integration && integration->appVersion == QStringLiteral("0.2.0-beta.24"),
	      "Comment Viewer integration exposes its app version");
	check(integration && integration->viewerUrl ==
				     QUrl(QStringLiteral("http://127.0.0.1:17321/viewer?dock=chat&send=1")),
	      "Comment Viewer integration is pinned to the loopback viewer URL");
	check(integration && integration->canSendComments,
	      "Comment Viewer integration advertises comment sending support");

	QJsonObject wrongSchema = QJsonDocument::fromJson(valid).object();
	wrongSchema.insert(QStringLiteral("schemaVersion"), 2);
	check(!parseCommentViewerObsIntegration(QJsonDocument(wrongSchema).toJson(QJsonDocument::Compact)),
	      "unsupported Comment Viewer integration schemas are rejected");

	QJsonObject externalViewer = QJsonDocument::fromJson(valid).object();
	QJsonObject externalDock = externalViewer.value(QStringLiteral("obsDock")).toObject();
	externalDock.insert(QStringLiteral("viewerPath"), QStringLiteral("https://example.com/viewer"));
	externalViewer.insert(QStringLiteral("obsDock"), externalDock);
	check(!parseCommentViewerObsIntegration(QJsonDocument(externalViewer).toJson(QJsonDocument::Compact)),
	      "Comment Viewer integration cannot redirect the OBS dock to an external URL");

	check(!parseCommentViewerObsIntegration(QByteArrayLiteral("not-json")),
	      "malformed Comment Viewer integration responses are rejected");
}

void testCommentViewerInstallDetection()
{
	QTemporaryDir temporary;
	check(temporary.isValid(), "Comment Viewer install detection has a temporary directory");
	check(!dsk::isCommentViewerInstallDirectory(temporary.path()),
	      "an empty directory is not a Comment Viewer installation");

	QFile metadata(temporary.filePath(QStringLiteral("package.json")));
	check(metadata.open(QIODevice::WriteOnly), "create Comment Viewer package metadata fixture");
	metadata.write(R"json({"name":"dsk-comment-viewer","version":"0.2.0-beta.24"})json");
	metadata.close();
	QFile serverLauncher(temporary.filePath(QStringLiteral("start-server-hidden.vbs")));
	check(serverLauncher.open(QIODevice::WriteOnly), "create Comment Viewer server launcher fixture");
	serverLauncher.write("fixture");
	serverLauncher.close();
	check(!dsk::isCommentViewerInstallDirectory(temporary.path()),
	      "a partial Comment Viewer installation is rejected");

	QFile appLauncher(temporary.filePath(QStringLiteral("start-hidden.vbs")));
	check(appLauncher.open(QIODevice::WriteOnly), "create Comment Viewer app launcher fixture");
	appLauncher.write("fixture");
	appLauncher.close();
	check(dsk::isCommentViewerInstallDirectory(temporary.path()),
	      "a complete independent Comment Viewer installation is detected");
}

void testCommentViewerIntegrationProbePolicy()
{
	using dsk::CommentViewerProbeAction;
	using dsk::commentViewerIntegrationEnabledAtStartup;
	using dsk::commentViewerProbeAction;

	check(commentViewerIntegrationEnabledAtStartup(true),
	      "an installed Comment Viewer enables integration at startup");
	check(!commentViewerIntegrationEnabledAtStartup(false),
	      "a missing Comment Viewer keeps integration off at startup");
	check(commentViewerProbeAction(false, 7, 7, true, false, 0, 10) == CommentViewerProbeAction::Ignore,
	      "a disabled Comment Viewer integration ignores a successful stale probe");
	check(commentViewerProbeAction(true, 8, 7, true, false, 0, 10) == CommentViewerProbeAction::Ignore,
	      "a superseded Comment Viewer probe cannot recreate the dock");
	check(commentViewerProbeAction(true, 7, 7, true, false, 0, 10) == CommentViewerProbeAction::Connect,
	      "a current valid probe connects the Comment Viewer dock");
	check(commentViewerProbeAction(true, 7, 7, false, false, 0, 10) == CommentViewerProbeAction::Launch,
	      "the first failed probe launches an installed but stopped Viewer");
	check(commentViewerProbeAction(true, 7, 7, false, true, 0, 10) == CommentViewerProbeAction::Retry,
	      "a failed post-launch probe retries while attempts remain");
	check(commentViewerProbeAction(true, 7, 7, false, true, 9, 10) == CommentViewerProbeAction::GiveUp,
	      "the final failed post-launch probe gives up without leaving a dead dock");
}

void testExperimentalSceneRoutingPolicy()
{
	check(!dsk::experimentalFeatureEnabled({}), "experimental features default off");
	check(!dsk::experimentalFeatureEnabled(QByteArrayLiteral("0")), "zero keeps an experimental feature off");
	check(!dsk::experimentalFeatureEnabled(QByteArrayLiteral("false")), "false keeps an experimental feature off");
	check(dsk::experimentalFeatureEnabled(QByteArrayLiteral("1")), "one enables an experimental feature");
	check(dsk::experimentalFeatureEnabled(QByteArrayLiteral(" TRUE ")), "true enables an experimental feature");
}

} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);

	testOutputTargetHelpers();
	testLayoutsAndProfiles();
	testSettingsCodec();
	testPlatformRegistry();
	testOAuthProviders();
	testYouTubeApiWarningHelpers();
	testRuntimeStatusHelpers();
	testOutputSignalPolicy();
	testStreamControlsState();
	testVisibleRefreshGate();
	testSecretStoreHelpers();
	testYouTubeBroadcastSelection();
	testCommentViewerObsIntegrationContract();
	testCommentViewerInstallDetection();
	testCommentViewerIntegrationProbePolicy();
	testExperimentalSceneRoutingPolicy();
	testDataFiles();

	if (failures > 0) {
		std::cerr << failures << " smoke test checks failed.\n";
		return 1;
	}

	std::cout << "All smoke tests passed.\n";
	return 0;
}
