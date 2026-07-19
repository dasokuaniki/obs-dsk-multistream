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
	layouts.initializeVertic×tòÚ$z{-®éÜj×–öâ"“° –6öç7B§6öä'&’ÆFf÷&×2Ò&ö÷BçfÇVR‚'ÆFf÷&×2"’çFô'&’‚“° –6†V6²‡ÆFf÷&×2ç6—¦R‚’ÓÒRÂ'ÆFf÷&Ò&W6WB§6öâ†2f—fRÆFf÷&×2"“° –G6³£¥ÆFf÷&Õ&W6WE&Vv—7G'’&Vv—7G'“° –6†V6²‡&Vv—7G'’ç&W6WG2‚’ç6—¦R‚’ÓÒÆFf÷&×2ç6—¦R‚’Â''VçF–ÖR&Vv—7G'’æB&W6WB§6öâ†fRF†R6ÖR6—¦R"“°  •6WCÅ7G&–æsâ–G3° –f÷"†6öç7B§6öåfÇVRgfÇVR¢ÆFf÷&×2’° –6†V6²‡fÇVRæ—4ö&¦V7B‚’Â'ÆFf÷&Ò—FVÒ—2ö&¦V7B"“° –6öç7B§6öäö&¦V7Bö&¦V7BÒfÇVRçFôö&¦V7B‚“° –6öç7B7G&–ær–BÒö&¦V7BçfÇVR‚&–B"’çFõ7G&–ær‚“° –6†V6²‚–Bæ—4V×G’‚’Â'ÆFf÷&Ò–BW†—7G2"“° –6†V6²‚–G2æ6öçF–ç2†–B’Â'ÆFf÷&Ò–BVæ—VR"“° ––G2æ–ç6W'B†–B“° –6†V6²‚ö&¦V7BçfÇVR‚&æÖR"’çFõ7G&–ær‚’æ—4V×G’‚’Â'ÆFf÷&ÒæÖRW†—7G2"“° –6†V6²†ö&¦V7Bæ6öçF–ç2‚&FVfVÇE6W'fW""’Â'ÆFf÷&ÒFVfVÇE6W'fW"W†—7G2"“° –6†V6²†ö&¦V7Bæ6öçF–ç2‚&†VÇW&Â"’Â'ÆFf÷&Ò†VÇW&ÂW†—7G2"“° –6†V6²†ö&¦V7Bæ6öçF–ç2‚'&V6öÖÖVæFVD÷WGWB"’Â'ÆFf÷&Ò&V6öÖÖVæFVD÷WGWBW†—7G2"“° –6†V6²†ö&¦V7Bæ6öçF–ç2‚&†÷&—¦öçFÄ&—G&FT¶'2"’Â'ÆFf÷&Ò†÷&—¦öçFÄ&—G&FT¶'2W†—7G2"“° –6†V6²†ö&¦V7Bæ6öçF–ç2‚'fW'F–6Ä&—G&FT¶'2"’Â'ÆFf÷&ÒfW'F–6Ä&—G&FT¶'2W†—7G2"“° –6†V6²†ö&¦V7Bæ6öçF–ç2‚&æ÷FR"’Â'ÆFf÷&Òæ÷FRW†—7G2"“° –6†V6²†ö&¦V7Bæ6öçF–ç2‚'fW'F–6Ä6öÖÖöâ"’Â'ÆFf÷&ÒfW'F–6Ä6öÖÖöâW†—7G2"“°  –6öç7BG6³£¥ÆFf÷&Õ&W6WB&W6WBÒ&Vv—7G'’ç&W6WD'”–B†–B“° –6†V6²‡&W6WBæ–BÓÒ–BÂ&–çF&ÆR…7G&–ær‚''VçF–ÖR&W6WB–BÖF6†W2§6öâf÷"S"’æ&r†–B’’“° –6†V6²‡&W6WBæF—7Æ”æÖRÓÒö&¦V7BçfÇVR‚&æÖR"’çFõ7G&–ær‚’À ’&–çF&ÆR…7G&–ær‚''VçF–ÖR&W6WBæÖRÖF6†W2§6öâf÷"S"’æ&r†–B’’“° –6†V6²‡&W6WBæFVfVÇE6W'fW"ÓÒö&¦V7BçfÇVR‚&FVfVÇE6W'fW""’çFõ7G&–ær‚’À ’&–çF&ÆR…7G&–ær‚''VçF–ÖR&W6WB6W'fW"ÖF6†W2§6öâf÷"S"’æ&r†–B’’“° –6†V6²‡&W6WBæ†VÇW&ÂÓÒö&¦V7BçfÇVR‚&†VÇW&Â"’çFõ7G&–ær‚’À ’&–çF&ÆR…7G&–ær‚''VçF–ÖR&W6WB†VÇU$ÂÖF6†W2§6öâf÷"S"’æ&r†–B’’“° –6†V6²‡&W6WBç&V6öÖÖVæFVD÷WGWBÓÒö&¦V7BçfÇVR‚'&V6öÖÖVæFVD÷WGWB"’çFõ7G&–ær‚’À ’&–çF&ÆR…7G&–ær‚''VçF–ÖR&W6WB÷WGWBÖF6†W2§6öâf÷"S"’æ&r†–B’’“° –6†V6²‡&W6WBæ†÷&—¦öçFÄ&—G&FT¶'2ÓÒö&¦V7BçfÇVR‚&†÷&—¦öçFÄ&—G&FT¶'2"’çFô–çB‚’À ’&–çF&ÆR…7G&–ær‚''VçF–ÖR†÷&—¦öçFÂ&—G&FRÖF6†W2§6öâf÷"S"’æ&r†–B’’“° –6†V6²‡&W6WBçfW'F–6Ä&—G&FT¶'2ÓÒö&¦V7BçfÇVR‚'fW'F–6Ä&—G&FT¶'2"’çFô–çB‚’À ’&–çF&ÆR…7G&–ær‚''VçF–ÖRfW'F–6Â&—G&FRÖF6†W2§6öâf÷"S"’æ&r†–B’’“° –6†V6²‡&W6WBææ÷FRÓÒö&¦V7BçfÇVR‚&æ÷FR"’çFõ7G&–ær‚’À ’&–çF&ÆR…7G&–ær‚''VçF–ÖR&W6WBæ÷FRÖF6†W2§6öâf÷"S"’æ&r†–B’’“° –6†V6²‡&W6WBçfW'F–6Ä6öÖÖöâÓÒö&¦V7BçfÇVR‚'fW'F–6Ä6öÖÖöâ"’çFô&ööÂ‚’À ’&–çF&ÆR…7G&–ær‚''VçF–ÖRfW'F–6ÂfÆrÖF6†W2§6öâf÷"S"’æ&r†–B’’“° —Ğ  –f÷"†6öç7B7G&–ærg&WV—&VB¢²'Gv—F6‚"Â'–÷WGV&R"Â&¶–6²"Â'F–·Fö²"Â&7W7FöÒ'Ò –6†V6²†–G2æ6öçF–ç2‡&WV—&VB’Â&–çF&ÆR…7G&–ær‚'&WV—&VBÆFf÷&ÒSW†—7G2"’æ&r‡&WV—&VB’’“°  –6öç7B6WCÅ7G&–æsâVâÒÆö6ÆT¶W—2‚&FFöÆö6ÆRöVâÕU2æ–æ’"“° –6öç7B6WCÅ7G&–æsâ¦ÒÆö6ÆT¶W—2‚&FFöÆö6ÆRö¦Ô¥æ–æ’"“° –6†V6²‚Vâæ—4V×G’‚’Â&VâÕU2Æö6ÆR†2¶W—2"“° –6†V6²†VâÓÒ¦Â&Æö6ÆR¶W’6WG2ÖF6‚"“°§Ğ ¥§6öäö&¦V7B–÷WGV&UFW7D'&öF67B†6öç7B7G&–ærf–BÂ6öç7B7G&–ærg7G&VÔ–BÀ ’6öç7B7G&–ærfÆ–fT7–6ÆU7FGW2Ò7G&–ætÆ—FW&Â‚'&VG’"’§° —&WGW&â§6öäö&¦V7G° —µ7G&–ætÆ—FW&Â‚&–B"’Â–GÒÀ —µ7G&–ætÆ—FW&Â‚&6öçFVçDFWF–Ç2"’Â§6öäö&¦V7G·µ7G&–ætÆ—FW&Â‚&&÷VæE7G&VÔ–B"’Â7G&VÔ–G××ÒÀ —µ7G&–ætÆ—FW&Â‚'7FGW2"’Â§6öäö&¦V7G·µ7G&–ætÆ—FW&Â‚&Æ–fT7–6ÆU7FGW2"’ÂÆ–fT7–6ÆU7FGW7××ÒÀ —Ó°§Ğ ¥§6öäö&¦V7B–÷WGV&UFW7E7G&VÒ†6öç7B7G&–ærf–BÂ6öç7B7G&–ærg7G&VÔæÖRÀ ’6öç7B7G&–ærg7G&VÕ7FGW2Ò7G&–ætÆ—FW&Â‚&7F—fR"’§° —&WGW&â§6öäö&¦V7G° —µ7G&–ætÆ—FW&Â‚&–B"’Â–GÒÀ —µ7G&–ætÆ—FW&Â‚'7FGW2"’Â§6öäö&¦V7G·µ7G&–ætÆ—FW&Â‚'7G&VÕ7FGW2"’Â7G&VÕ7FGW7××ÒÀ —µ7G&–ætÆ—FW&Â‚&6Fâ"’À ’§6öäö&¦V7G·µ7G&–ætÆ—FW&Â‚&–ævW7F–öä–æfò"’À ’§6öäö&¦V7G·µ7G&–ætÆ—FW&Â‚'7G&VÔæÖR"’Â7G&VÔæÖW××××ÒÀ —Ó°§Ğ §fö–BFW7E–÷UGV&T'&öF67E6VÆV7F–öâ‚§° —W6–æræÖW76RG6³°  •†6ƒÅ7G&–ærÂ§6öäö&¦V7Câ7G&V×3° —7G&V×2æ–ç6W'B…7G&–ætÆ—FW&Â‚'7G&VÒÖ"’Â–÷WGV&UFW7E7G&VÒ…7G&–ætÆ—FW&Â‚'7G&VÒÖ"’Â7G&–ætÆ—FW&Â‚&¶W’Ö"’’“° —7G&V×2æ–ç6W'B…7G&–ætÆ—FW&Â‚'7G&VÒÖ""’Â–÷WGV&UFW7E7G&VÒ…7G&–ætÆ—FW&Â‚'7G&VÒÖ""’Â7G&–ætÆ—FW&Â‚&¶W’Ö""’’“°  •§6öä'&’öæW·–÷WGV&UFW7D'&öF67B…7G&–ætÆ—FW&Â‚&'&öF67BÖ"’Â7G&–ætÆ—FW&Â‚'7G&VÒÖ"’—Ó° •–÷UGV&T'&öF67E6VÆV7F–öâ6VÆV7F–öâÒ6VÆV7E–÷UGV&T'&öF67B†öæRÂ7G&V×2Â7G&–ær‚’“° –6†V6²‡6VÆV7F–öâç7FFRÓÒ–÷UGV&T'&öF67E6VÆV7F–öå7FFS£¥6VÆV7FVBÀ ’&öæR7F—fR–÷UGV&R'&öF67B—26VÆV7FVBv—F†÷WB7G&VÒ¶W’"“° –6†V6²‡6VÆV7F–öâæ'&öF67BçfÇVR…7G&–ætÆ—FW&Â‚&–B"’’çFõ7G&–ær‚’ÓÒ7G&–ætÆ—FW&Â‚&'&öF67BÖ"’À ’%–÷UGV&R6VÆV7F–öâ&WGW&ç2F†RÖF6†–ær'&öF67Bö&¦V7B"“°  •§6öä'&’GvòÒöæS° —GvòçW6…ö&6²‡–÷WGV&UFW7D'&öF67B…7G&–ætÆ—FW&Â‚&'&öF67BÖ""’Â7G&–ætÆ—FW&Â‚'7G&VÒÖ""’’“° —6VÆV7F–öâÒ6VÆV7E–÷UGV&T'&öF67B‡GvòÂ7G&V×2Â7G&–ær‚’“° –6†V6²‡6VÆV7F–öâç7FFRÓÒ–÷UGV&T'&öF67E6VÆV7F–öå7FFS£¤×VÇF—ÆT7F—fT'&öF67G2À ’&×VÇF—ÆR7F—fR–÷UGV&R'&öF67G2&WV—&RâW‡Æ–6—B7G&VÒ¶W’"“° —6VÆV7F–öâÒ6VÆV7E–÷UGV&T'&öF67B‡GvòÂ7G&V×2Â7G&–ætÆ—FW&Â‚&¶W’Ö""’“° –6†V6²‡6VÆV7F–öâç7FFRÓÒ–÷UGV&T'&öF67E6VÆV7F–öå7FFS£¥6VÆV7FVBb` ’6VÆV7F–öâæ'&öF67BçfÇVR…7G&–ætÆ—FW&Â‚&–B"’’çFõ7G&–ær‚’ÓÒ7G&–ætÆ—FW&Â‚&'&öF67BÖ""’À ’'7G&VÒ¶W’6VÆV7G2W†7FÇ’öæR7F—fR–÷UGV&R'&öF67B"“° –6†V6²‡6VÆV7F–öâæ6æF–FFW2ç6—¦R‚’ÓÒÀ ’%–÷UGV&R6VÆV7F–öâW‡÷6W2F†RÖF6†–ær'&öF67B6æF–FFR"“° —6VÆV7F–öâÒ6VÆV7E–÷UGV&T'&öF67B‡GvòÂ7G&V×2Â7G&–ætÆ—FW&Â‚&Ö—76–ærÖ¶W’"’“° –6†V6²‡6VÆV7F–öâç7FFRÓÒ–÷UGV&T'&öF67E6VÆV7F–öå7FFS£¤æõ7G&VÔ¶W”ÖF6‚À ’&7F—fR–÷UGV&R'&öF67G2v—F‚F–ffW&VçB¶W’&R&V¦V7FVB"“°  •†6ƒÅ7G&–ærÂ§6öäö&¦V7CâGWÆ–6FT¶W•7G&V×2Ò7G&V×3° –GWÆ–6FT¶W•7G&V×5µ7G&–ætÆ—FW&Â‚'7G&VÒÖ""•ÒĞ —–÷WGV&UFW7E7G&VÒ…7G&–ætÆ—FW&Â‚'7G&VÒÖ""’Â7G&–ætÆ—FW&Â‚&¶W’Ö"’“° —6VÆV7F–öâÒ6VÆV7E–÷UGV&T'&öF67B‡GvòÂGWÆ–6FT¶W•7G&V×2Â7G&–ætÆ—FW&Â‚&¶W’Ö"’“° –6†V6²‡6VÆV7F–öâç7FFRÓÒ–÷UGV&T'&öF67E6VÆV7F–öå7FFS£¤×VÇF—ÆU7G&VÔ¶W”ÖF6†W2À ’&GWÆ–6FR–÷UGV&R7G&VÒÖ¶W’&–æF–æw2&RæWfW"WFò×G&ç6—F–öæVB"“° –6†V6²‡6VÆV7F–öâæ6æF–FFW2ç6—¦R‚’ÓÒ"À ’&GWÆ–6FR–÷UGV&R7G&VÒÖ¶W’&–æF–æw2W‡÷6R&÷F‚6†ö–6W2"“° —6VÆV7F–öâÒ6VÆV7E–÷UGV&T'&öF67B‡GvòÂGWÆ–6FT¶W•7G&V×2Â7G&–ætÆ—FW&Â‚&¶W’Ö"’À ’7G&–ætÆ—FW&Â‚&'&öF67BÖ""’“° –6†V6²‡6VÆV7F–öâç7FFRÓÒ–÷UGV&T'&öF67E6VÆV7F–öå7FFS£¥6VÆV7FVBb` ’6VÆV7F–öâæ'&öF67BçfÇVR…7G&–ætÆ—FW&Â‚&–B"’’çFõ7G&–ær‚’ÓÒ7G&–ætÆ—FW&Â‚&'&öF67BÖ""’À ’&âW‡Æ–6—B–÷UGV&R'&öF67B6†ö–6R&W6öÇfW2GWÆ–6FR7G&VÒÖ¶W’&–æF–æw2"“° —6VÆV7F–öâÒ6VÆV7E–÷UGV&T'&öF67B‡GvòÂGWÆ–6FT¶W•7G&V×2Â7G&–ætÆ—FW&Â‚&¶W’Ö"’À ’7G&–ætÆ—FW&Â‚&Ö—76–ærÖ'&öF67B"’“° –6†V6²‡6VÆV7F–öâç7FFRÓÒ–÷UGV&T'&öF67E6VÆV7F–öå7FFS£¥&VfW'&VD'&öF67EVæf–Æ&ÆRb` ’6VÆV7F–öâæ'&öF67Bæ—4V×G’‚’bb6VÆV7F–öâæ6æF–FFW2ç6—¦R‚’ÓÒ"À ’&Ö—76–ærW‡Æ–6—B–÷UGV&R'&öF67B6†ö–6RæWfW"fÆÇ2&6²Fòæ÷F†W"'&öF67B"“°  •§6öä'&’6ö×ÆWFVD6†ö–6W° —–÷WGV&UFW7D'&öF67B…7G&–ætÆ—FW&Â‚&'&öF67BÖ"’Â7G&–ætÆ—FW&Â‚'7G&VÒÖ"’’À —–÷WGV&UFW7D'&öF67B…7G&–ætÆ—FW&Â‚&'&öF67BÖ""’Â7G&–ætÆ—FW&Â‚'7G&VÒÖ""’À ’7G&–ætÆ—FW&Â‚&6ö×ÆWFR"’’À —Ó° —6VÆV7F–öâÒ6VÆV7E–÷UGV&T'&öF67B†6ö×ÆWFVD6†ö–6RÂGWÆ–6FT¶W•7G&V×2Â7G&–ætÆ—FW&Â‚&¶W’Ö"’À ’7G&–ætÆ—FW&Â‚&'&öF67BÖ""’“° –6†V6²‡6VÆV7F–öâç7FFRÓÒ–÷UGV&T'&öF67E6VÆV7F–öå7FFS£¥&VfW'&VD'&öF67EVæf–Æ&ÆRb` ’6VÆV7F–öâæ'&öF67Bæ—4V×G’‚’bb6VÆV7F–öâæ6æF–FFW2ç6—¦R‚’ÓÒÀ ’&6ö×ÆWFVBW‡Æ–6—B–÷UGV&R'&öF67B6†ö–6RæWfW"fÆÇ2&6²Fò&VG’'&öF67B"“°  •§6öä'&’6ö×ÆWFVG·–÷WGV&UFW7D'&öF67B…7G&–ætÆ—FW&Â‚&FöæR"’Â7G&–ætÆ—FW&Â‚'7G&VÒÖ"’À ’7G&–ætÆ—FW&Â‚&6ö×ÆWFR"’—Ó° —6VÆV7F–öâÒ6VÆV7E–÷UGV&T'&öF67B†6ö×ÆWFVBÂ7G&V×2Â7G&–ætÆ—FW&Â‚&¶W’Ö"’“° –6†V6²‡6VÆV7F–öâç7FFRÓÒ–÷UGV&T'&öF67E6VÆV7F–öå7FFS£¤æô7F—fT'&öF67BÀ ’&6ö×ÆWFVB–÷UGV&R'&öF67G2&R–væ÷&VB"“°  •§6öä'&’7&VFVG·–÷WGV&UFW7D'&öF67B…7G&–ætÆ—FW&Â‚&7&VFVB"’Â7G&–ætÆ—FW&Â‚'7G&VÒÖ"’À ’7G&–ætÆ—FW&Â‚&7&VFVB"’—Ó° —6VÆV7F–öâÒ6VÆV7E–÷UGV&T'&öF67B†7&VFVBÂ7G&V×2Â7G&–ætÆ—FW&Â‚&¶W’Ö"’“° –6†V6²‡6VÆV7F–öâç7FFRÓÒ–÷UGV&T'&öF67E6VÆV7F–öå7FFS£¤æô7F—fT'&öF67BÀ ’&–æ6ö×ÆWFR7&VFVB–÷UGV&R'&öF67G2&Ræ÷B6VÆV7FVBf÷"G&ç6—F–öâ"“°  –f÷"†6öç7B7G&–ærfÆ–fV7–6ÆR¢µ7G&–ætÆ—FW&Â‚'FW7F–ær"’Â7G&–ætÆ—FW&Â‚'FW7E7F'F–ær"’À ’7G&–ætÆ—FW&Â‚&Æ—fU7F'F–ær"’Â7G&–ætÆ—FW&Â‚&Æ—fR"—Ò’° •§6öä'&’7F–öæ&ÆW·–÷WGV&UFW7D'&öF67B…7G&–ætÆ—FW&Â‚&7F–öæ&ÆR"’Â7G&–ætÆ—FW&Â‚'7G&VÒÖ"’À ’Æ–fV7–6ÆR—Ó° —6VÆV7F–öâÒ6VÆV7E–÷UGV&T'&öF67B†7F–öæ&ÆRÂ7G&V×2Â7G&–ætÆ—FW&Â‚&¶W’Ö"’“° –6†V6²‡6VÆV7F–öâç7FFRÓÒ–÷UGV&T'&öF67E6VÆV7F–öå7FFS£¥6VÆV7FVBÀ ’&–çF&ÆR…7G&–ætÆ—FW&Â‚%–÷UGV&RÆ–fV7–6ÆRS&VÖ–ç27F–öæ&ÆR"’æ&r†Æ–fV7–6ÆR’’“° —Ğ§Ğ §fö–BFW7D6öÖÖVçEf–WvW$ö'4–çFVw&F–öä6öçG&7B‚§° —W6–æræÖW76RG6³°  –6öç7B'—FT'&’fÆ–BÒ"&§6öâ‡° ’&ö²#¢G'VRÀ ’'6W'f–6R#¢&G6²Ö6öÖÖVçB×f–WvW""À ’'66†VÖfW'6–öâ#¢À ’&fW'6–öâ#¢#ã"ãÖ&WFã#B"À ’&–çFVw&F–öâ#¢&ö'2Ö'&÷w6W"ÖFö6²"À ’&ö'4Fö6²#¢° ’'f–WvW%F‚#¢"÷f–WvW#öFö6³Ö6†Bg6VæCÓ"À ’&6&–Æ—F–W2#¢²&6öÖÖVçG2ç&VB"Â&6öÖÖVçG2ç6VæB%Ğ —Ğ —Ò–§6öâ#° –6öç7BWFò–çFVw&F–öâÒ'6T6öÖÖVçEf–WvW$ö'4–çFVw&F–öâ‡fÆ–B“° –6†V6²†–çFVw&F–öâæ†5÷fÇVR‚’Â'fÆ–B6öÖÖVçBf–WvW"ô%2–çFVw&F–öâ6öçG&7B—266WFVB"“° –6†V6²†–çFVw&F–öâbb–çFVw&F–öâÓæfW'6–öâÓÒ7G&–ætÆ—FW&Â‚#ã"ãÖ&WFã#B"’À ’$6öÖÖVçBf–WvW"–çFVw&F–öâW‡÷6W2—G2fW'6–öâ"“° –6†V6²†–çFVw&F–öâbb–çFVw&F–öâÓçf–WvW%W&ÂÓĞ ’W&Â…7G&–ætÆ—FW&Â‚&‡GG¢òó#rããã£s3#÷f–WvW#öFö6³Ö6†Bg6VæCÓ"’’À ’$6öÖÖVçBf–WvW"–çFVw&F–öâ—2–ææVBFòF†RÆö÷&6²f–WvW"U$Â"“° –6†V6²†–çFVw&F–öâbb–çFVw&F–öâÓæ6å6VæD6öÖÖVçG2À ’$6öÖÖVçBf–WvW"–çFVw&F–öâGfW'F—6W26öÖÖVçB6VæF–ær7W÷'B"“°  •§6öäö&¦V7Bw&öæu66†VÖÒ§6öäFö7VÖVçC£¦g&öÔ§6öâ‡fÆ–B’æö&¦V7B‚“° —w&öæu66†VÖæ–ç6W'B…7G&–ætÆ—FW&Â‚'66†VÖfW'6–öâ"’Â"“° –6†V6²‚'6T6öÖÖVçEf–WvW$ö'4–çFVw&F–öâ…§6öäFö7VÖVçB‡w&öæu66†VÖ’çFô§6öâ…§6öäFö7VÖVçC£¤6ö×7B’’À ’'Vç7W÷'FVB6öÖÖVçBf–WvW"–çFVw&F–öâ66†VÖ2&R&V¦V7FVB"“°  •§6öäö&¦V7BW‡FW&æÅf–WvW"Ò§6öäFö7VÖVçC£¦g&öÔ§6öâ‡fÆ–B’æö&¦V7B‚“° •§6öäö&¦V7BW‡FW&æÄFö6²ÒW‡FW&æÅf–WvW"çfÇVR…7G&–ætÆ—FW&Â‚&ö'4Fö6²"’’çFôö&¦V7B‚“° –W‡FW&æÄFö6²æ–ç6W'B…7G&–ætÆ—FW&Â‚'f–WvW%F‚"’Â7G&–ætÆ—FW&Â‚&‡GG3¢òöW†×ÆRæ6öÒ÷f–WvW""’“° –W‡FW&æÅf–WvW"æ–ç6W'B…7G&–ætÆ—FW&Â‚&ö'4Fö6²"’ÂW‡FW&æÄFö6²“° –6†V6²‚'6T6öÖÖVçEf–WvW$ö'4–çFVw&F–öâ…§6öäFö7VÖVçB†W‡FW&æÅf–WvW"’çFô§6öâ…§6öäFö7VÖVçC£¤6ö×7B’’À ’$6öÖÖVçBf–WvW"–çFVw&F–öâ6ææ÷B&VF—&V7BF†Rô%2Fö6²FòâW‡FW&æÂU$Â"“°  –6†V6²‚'6T6öÖÖVçEf–WvW$ö'4–çFVw&F–öâ…'—FT'&”Æ—FW&Â‚&æ÷BÖ§6öâ"’’À ’&ÖÆf÷&ÖVB6öÖÖVçBf–WvW"–çFVw&F–öâ&W7öç6W2&R&V¦V7FVB"“°§Ğ §fö–BFW7D6öÖÖVçEf–WvW$–ç7FÆÄFWFV7F–öâ‚§° •FV×÷&'”F—"FV×÷&'“° –6†V6²‡FV×÷&'’æ—5fÆ–B‚’Â$6öÖÖVçBf–WvW"–ç7FÆÂFWFV7F–öâ†2FV×÷&'’F—&V7F÷'’"“° –6†V6²‚G6³£¦—46öÖÖVçEf–WvW$–ç7FÆÄF—&V7F÷'’‡FV×÷&'’çF‚‚’’À ’&âV×G’F—&V7F÷'’—2æ÷B6öÖÖVçBf–WvW"–ç7FÆÆF–öâ"“°  •f–ÆRÖWFFF‡FV×÷&'’æf–ÆUF‚…7G&–ætÆ—FW&Â‚'6¶vRæ§6öâ"’’“° –6†V6²†ÖWFFFæ÷Vâ…”ôFWf–6S£¥w&—FTöæÇ’’Â&7&VFR6öÖÖVçBf–WvW"6¶vRÖWFFFf—‡GW&R"“° –ÖWFFFçw&—FR…"&§6öâ‡²&æÖR#¢&G6²Ö6öÖÖVçB×f–WvW""Â'fW'6–öâ#¢#ã"ãÖ&WFã#B'Ò–§6öâ"“° –ÖWFFFæ6Æ÷6R‚“° •f–ÆR6W'fW$ÆVæ6†W"‡FV×÷&'’æf–ÆUF‚…7G&–ætÆ—FW&Â‚'7F'B×6W'fW"Ö†–FFVâçf'2"’’“° –6†V6²‡6W'fW$ÆVæ6†W"æ÷Vâ…”ôFWf–6S£¥w&—FTöæÇ’’Â&7&VFR6öÖÖVçBf–WvW"6W'fW"ÆVæ6†W"f—‡GW&R"“° —6W'fW$ÆVæ6†W"çw&—FR‚&f—‡GW&R"“° —6W'fW$ÆVæ6†W"æ6Æ÷6R‚“° –6†V6²‚G6³£¦—46öÖÖVçEf–WvW$–ç7FÆÄF—&V7F÷'’‡FV×÷&'’çF‚‚’’À ’&'F–Â6öÖÖVçBf–WvW"–ç7FÆÆF–öâ—2&V¦V7FVB"“°  •f–ÆRÆVæ6†W"‡FV×÷&'’æf–ÆUF‚…7G&–ætÆ—FW&Â‚'7F'BÖ†–FFVâçf'2"’’“° –6†V6²†ÆVæ6†W"æ÷Vâ…”ôFWf–6S£¥w&—FTöæÇ’’Â&7&VFR6öÖÖVçBf–WvW"ÆVæ6†W"f—‡GW&R"“° –ÆVæ6†W"çw&—FR‚&f—‡GW&R"“° –ÆVæ6†W"æ6Æ÷6R‚“° –6†V6²†G6³£¦—46öÖÖVçEf–WvW$–ç7FÆÄF—&V7F÷'’‡FV×÷&'’çF‚‚’’À ’&6ö×ÆWFR–æFWVæFVçB6öÖÖVçBf–WvW"–ç7FÆÆF–öâ—2FWFV7FVB"“°§Ğ §fö–BFW7D6öÖÖVçEf–WvW$–çFVw&F–öå&ö&UöÆ–7’‚§° —W6–ærG6³£¤6öÖÖVçEf–WvW%&ö&T7F–öã° —W6–ærG6³£¦6öÖÖVçEf–WvW$–çFVw&F–öäVæ&ÆVDE7F'GW° —W6–ærG6³£¦6öÖÖVçEf–WvW%&ö&T7F–öã°  –6†V6²†6öÖÖVçEf–WvW$–çFVw&F–öäVæ&ÆVDE7F'GW‡G'VR’À ’&â–ç7FÆÆVB6öÖÖVçBf–WvW"Væ&ÆW2–çFVw&F–öâB7F'GW"“° –6†V6²‚6öÖÖVçEf–WvW$–çFVw&F–öäVæ&ÆVDE7F'GW†fÇ6R’À ’&Ö—76–ær6öÖÖVçBf–WvW"¶VW2–çFVw&F–öâöfbB7F'GW"“° –6†V6²†6öÖÖVçEf–WvW%&ö&T7F–öâ†fÇ6RÂrÂrÂG'VRÂfÇ6RÂÂ’ÓÒ6öÖÖVçEf–WvW%&ö&T7F–öã£¤–væ÷&RÀ ’&F—6&ÆVB6öÖÖVçBf–WvW"–çFVw&F–öâ–væ÷&W27V66W76gVÂ7FÆR&ö&R"“° –6†V6²†6öÖÖVçEf–WvW%&ö&T7F–öâ‡G'VRÂ‚ÂrÂG'VRÂfÇ6RÂÂ’ÓÒ6öÖÖVçEf–WvW%&ö&T7F–öã£¤–væ÷&RÀ ’&7WW'6VFVB6öÖÖVçBf–WvW"&ö&R6ææ÷B&V7&VFRF†RFö6²"“° –6†V6²†6öÖÖVçEf–WvW%&ö&T7F–öâ‡G'VRÂrÂrÂG'VRÂfÇ6RÂÂ’ÓÒ6öÖÖVçEf–WvW%&ö&T7F–öã£¤6öææV7BÀ ’&7W'&VçBfÆ–B&ö&R6öææV7G2F†R6öÖÖVçBf–WvW"Fö6²"“° –6†V6²†6öÖÖVçEf–WvW%&ö&T7F–öâ‡G'VRÂrÂrÂfÇ6RÂfÇ6RÂÂ’ÓÒ6öÖÖVçEf–WvW%&ö&T7F–öã£¤ÆVæ6‚À ’'F†Rf—'7Bf–ÆVB&ö&RÆVæ6†W2â–ç7FÆÆVB'WB7F÷VBf–WvW""“° –6†V6²†6öÖÖVçEf–WvW%&ö&T7F–öâ‡G'VRÂrÂrÂfÇ6RÂG'VRÂÂ’ÓÒ6öÖÖVçEf–WvW%&ö&T7F–öã£¥&WG'’À ’&f–ÆVB÷7BÖÆVæ6‚&ö&R&WG&–W2v†–ÆRGFV×G2&VÖ–â"“° –6†V6²†6öÖÖVçEf–WvW%&ö&T7F–öâ‡G'VRÂrÂrÂfÇ6RÂG'VRÂ’Â’ÓÒ6öÖÖVçEf–WvW%&ö&T7F–öã£¤v—fUWÀ ’'F†Rf–æÂf–ÆVB÷7BÖÆVæ6‚&ö&Rv—fW2Wv—F†÷WBÆVf–ærFVBFö6²"“°§Ğ §fö–BFW7DW‡W&–ÖVçFÅ66VæU&÷WF–æuöÆ–7’‚§° –6†V6²‚G6³£¦W‡W&–ÖVçFÄfVGW&TVæ&ÆVB‡·Ò’Â&W‡W&–ÖVçFÂfVGW&W2FVfVÇBöfb"“° –6†V6²‚G6³£¦W‡W&–ÖVçFÄfVGW&TVæ&ÆVB…'—FT'&”Æ—FW&Â‚#"’’Â'¦W&ò¶VW2âW‡W&–ÖVçFÂfVGW&Röfb"“° –6†V6²‚G6³£¦W‡W&–ÖVçFÄfVGW&TVæ&ÆVB…'—FT'&”Æ—FW&Â‚&fÇ6R"’’Â&fÇ6R¶VW2âW‡W&–ÖVçFÂfVGW&Röfb"“° –6†V6²†G6³£¦W‡W&–ÖVçFÄfVGW&TVæ&ÆVB…'—FT'&”Æ—FW&Â‚#"’’Â&öæRVæ&ÆW2âW‡W&–ÖVçFÂfVGW&R"“° –6†V6²†G6³£¦W‡W&–ÖVçFÄfVGW&TVæ&ÆVB…'—FT'&”Æ—FW&Â‚"E%TR"’’Â'G'VRVæ&ÆW2âW‡W&–ÖVçFÂfVGW&R"“°§Ğ §ÒòòæÖW76P ¦–çBÖ–â†–çB&v2Â6†"¢¦&wb§° •6÷&TÆ–6F–öâ†&v2Â&wb“°  —FW7D÷WGWEF&vWD†VÇW'2‚“° —FW7DÆ–÷WG4æE&öf–ÆW2‚“° —FW7E6WGF–æw46öFV2‚“° —FW7EÆFf÷&Õ&Vv—7G'’‚“° —FW7DôWF…&÷f–FW'2‚“° —FW7E–÷UGV&T•v&æ–æt†VÇW'2‚“° —FW7E'VçF–ÖU7FGW4†VÇW'2‚“° —FW7D÷WGWE6–væÅöÆ–7’‚“° —FW7E7G&VÔ6öçG&öÇ57FFR‚“° —FW7Ef—6–&ÆU&Vg&W6„vFR‚“° —FW7E6V7&WE7F÷&T†VÇW'2‚“° —FW7E–÷UGV&T'&öF67E6VÆV7F–öâ‚“° —FW7D6öÖÖVçEf–WvW$ö'4–çFVw&F–öä6öçG&7B‚“° —FW7D6öÖÖVçEf–WvW$–ç7FÆÄFWFV7F–öâ‚“° —FW7D6öÖÖVçEf–WvW$–çFVw&F–öå&ö&UöÆ–7’‚“° —FW7DW‡W&–ÖVçFÅ66VæU&÷WF–æuöÆ–7’‚“° —FW7DFFf–ÆW2‚“°  ––b†f–ÇW&W2â’° —7FC£¦6W'"ÃÂf–ÇW&W2ÃÂ"6Öö¶RFW7B6†V6·2f–ÆVBåÆâ#° —&WGW&â° —Ğ  —7FC£¦6÷WBÃÂ$ÆÂ6Öö¶RFW7G276VBåÆâ#° —&WGW&â°§Ğ