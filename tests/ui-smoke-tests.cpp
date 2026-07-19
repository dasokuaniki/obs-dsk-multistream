#include "core/oauth-connector.hpp"
#include "core/oauth-provider.hpp"
#include "core/output-target.hpp"
#include "core/platform-preset-registry.hpp"
#include "ui/layout-widget-utils.hpp"
#include "ui/target-edit-dialog.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

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

void testTargetEditDialogAddDefaults()
{
	dsk::PlatformPresetRegistry platforms;
	dsk::TargetEditDialog dialog(platforms);

	dialog.setNewTargetDefaults(dsk::newTargetId());
	dsk::OutputTarget accepted;
	dialog.fillTarget(accepted);

	check(!accepted.id.isEmpty(), "dialog preserves target id");
	check(accepted.name == "New Target", "dialog preserves target name");
	check(accepted.platformId == "twitch", "dialog preserves platform");
	check(accepted.serverUrl == "rtmp://live.twitch.tv/app", "dialog preserves preset server");
	check(accepted.authMode == dsk::TargetAuthMode::ManualRtmp, "dialog defaults manual auth");
	check(accepted.encoderGroup == dsk::EncoderGroup::DskHorizontal, "dialog defaults horizontal DSK output");
	check(accepted.sceneMode == dsk::TargetSceneMode::FollowObs, "dialog defaults following OBS program");
	check(accepted.sceneName.isEmpty(), "dialog defaults empty routed scene");
	check(accepted.enabled, "dialog defaults target enabled");
	check(accepted.startWithAll, "dialog defaults start-all enabled");

	auto *platform = dialog.findChild<QComboBox *>(QStringLiteral("dskTargetPlatform"));
	auto *presetServer = dialog.findChild<QPushButton *>(QStringLiteral("dskUsePresetServer"));
	auto *streamKey = dialog.findChild<QLineEdit *>(QStringLiteral("dskTargetStreamKey"));
	auto *platformHint = dialog.findChild<QLabel *>(QStringLiteral("dskPlatformHint"));
	check(platform != nullptr, "dialog exposes the platform selector for UI behavior tests");
	check(presetServer != nullptr, "dialog exposes the preset server button for UI behavior tests");
	check(streamKey != nullptr, "dialog exposes the stream key field for UI behavior tests");
	check(platformHint != nullptr, "dialog exposes the platform hint for UI behavior tests");
	if (presetServer)
		check(presetServer->isEnabled(), "Twitch enables its server preset");
	if (streamKey)
		check(streamKey->placeholderText() == "Enter stream key", "new Twitch target asks for a stream key");
	if (platform) {
		platform->setCurrentIndex(platform->findData(QStringLiteral("youtube")));
		dialog.fillTarget(accepted);
		check(accepted.platformId == "youtube", "new target switches to YouTube");
		check(accepted.serverUrl == "rtmp://a.rtmp.youtube.com/live2",
		      "new target follows the selected YouTube server preset");
		if (presetServer)
			check(presetServer->isEnabled(), "YouTube enables its server preset");

		platform->setCurrentIndex(platform->findData(QStringLiteral("kick")));
		dialog.fillTarget(accepted);
		check(accepted.platformId == "kick", "new target switches to Kick");
		check(accepted.serverUrl == "rtmps://fa-live-cf.kick.com/app",
		      "new target follows the selected Kick server preset");
		if (presetServer)
			check(presetServer->isEnabled(), "Kick enables its server preset");

		platform->setCurrentIndex(platform->findData(QStringLiteral("tiktok")));
		dialog.fillTarget(accepted);
		check(accepted.platformId == "tiktok", "new target switches to TikTok");
		check(accepted.serverUrl.isEmpty(), "TikTok manual RTMP does not invent an ingest server");
		check(accepted.authMode == dsk::TargetAuthMode::ManualRtmp, "TikTok uses manual RTMP authentication");
		check(accepted.encoderGroup == dsk::EncoderGroup::DskVertical, "TikTok defaults to DSK Vertical output");
		if (presetServer)
			check(!presetServer->isEnabled(), "TikTok disables an unavailable server preset");
		if (streamKey)
			check(streamKey->placeholderText() == "Paste TikTok stream key",
			      "TikTok stream key field names the required value");
		if (platformHint)
			check(platformHint->text().contains("Manual RTMP only"),
			      "TikTok hint explains that login is not available");

		platform->setCurrentIndex(platform->findData(QStringLiteral("custom")));
		dialog.fillTarget(accepted);
		check(accepted.serverUrl.isEmpty(), "custom target clears a previously followed platform preset");
		if (presetServer)
			check(!presetServer->isEnabled(), "custom target disables an unavailable server preset");
		if (streamKey)
			check(streamKey->placeholderText() == "Enter stream key", "custom target asks for a stream key");
	}
}

void testListSelectionCanBeClearedForPreviewBackgroundClick()
{
	QListWidget items;
	items.addItems({QStringLiteral("Camera"), QStringLiteral("Game")});
	items.setCurrentRow(0);
	check(items.currentRow() == 0 && !items.selectedItems().isEmpty(),
	      "vertical source list starts with a selected row");

	dsk::clearListWidgetSelection(&items);
	check(items.currentRow() == -1, "preview background click clears the current vertical source row");
	check(items.selectedItems().isEmpty(), "preview background click clears the vertical source selection");
}

void testSceneRoutingControlsAreHiddenByDefault()
{
	const QByteArray previous = qgetenv("DSK_EXPERIMENTAL_SCENE_ROUTING");
	qunsetenv("DSK_EXPERIMENTAL_SCENE_ROUTING");

	dsk::PlatformPresetRegistry platforms;
	dsk::TargetEditDialog dialog(platforms);

	QComboBox *sceneMode = nullptr;
	for (QComboBox *combo : dialog.findChildren<QComboBox *>()) {
		if (combo->findData(QStringLiteral("fixed-scene")) >= 0 &&
		    combo->findData(QStringLiteral("linked-scene")) >= 0) {
			sceneMode = combo;
			break;
		}
	}
	QLineEdit *sceneName = nullptr;
	for (QLineEdit *lineEdit : dialog.findChildren<QLineEdit *>()) {
		if (lineEdit->placeholderText().contains(QStringLiteral("Fixed mode"))) {
			sceneName = lineEdit;
			break;
		}
	}

	check(sceneMode != nullptr, "dialog retains the experimental scene mode control");
	check(sceneMode && sceneMode->isHidden(), "scene routing mode is hidden by default");
	check(sceneName != nullptr, "dialog retains the experimental scene name control");
	check(sceneName && sceneName->isHidden(), "scene routing scene is hidden by default");

	qputenv("DSK_EXPERIMENTAL_SCENE_ROUTING", QByteArrayLiteral("1"));
	dsk::TargetEditDialog experimentalDialog(platforms);
	QComboBox *experimentalSceneMode = nullptr;
	for (QComboBox *combo : experimentalDialog.findChildren<QComboBox *>()) {
		if (combo->findData(QStringLiteral("fixed-scene")) >= 0 &&
		    combo->findData(QStringLiteral("linked-scene")) >= 0) {
			experimentalSceneMode = combo;
			break;
		}
	}
	check(experimentalSceneMode && !experimentalSceneMode->isHidden(),
	      "explicit experimental flag reveals scene routing controls");

	if (previous.isNull())
		qunsetenv("DSK_EXPERIMENTAL_SCENE_ROUTING");
	else
		qputenv("DSK_EXPERIMENTAL_SCENE_ROUTING", previous);
}

void testTargetEditDialogPreservesCustomServerAcrossPlatformChange()
{
	dsk::PlatformPresetRegistry platforms;
	dsk::TargetEditDialog dialog(platforms);
	dsk::OutputTarget input;
	input.id = QStringLiteral("custom-server-target");
	input.name = QStringLiteral("Custom ingest");
	input.platformId = QStringLiteral("twitch");
	input.authMode = dsk::TargetAuthMode::ManualRtmp;
	input.serverUrl = QStringLiteral("rtmps://ingest.example.test/live");
	input.streamKey = QStringLiteral("key");
	dialog.setTarget(input);

	auto *platform = dialog.findChild<QComboBox *>(QStringLiteral("dskTargetPlatform"));
	check(platform != nullptr, "custom server test finds the platform selector");
	if (platform)
		platform->setCurrentIndex(platform->findData(QStringLiteral("youtube")));

	dsk::OutputTarget accepted;
	dialog.fillTarget(accepted);
	check(accepted.platformId == "youtube", "custom server target changes platform");
	check(accepted.serverUrl == "rtmps://ingest.example.test/live",
	      "platform change preserves an existing custom server URL");
}

void checkRoundTrip(const dsk::OutputTarget &input, const char *label)
{
	dsk::PlatformPresetRegistry platforms;
	dsk::TargetEditDialog dialog(platforms);

	dialog.setTarget(input);
	dsk::OutputTarget accepted;
	dialog.fillTarget(accepted);

	check(accepted.id == input.id, qPrintable(QString("%1 preserves id").arg(label)));
	check(accepted.name == input.name, qPrintable(QString("%1 preserves name").arg(label)));
	check(accepted.platformId == input.platformId, qPrintable(QString("%1 preserves platform").arg(label)));
	check(accepted.authMode == input.authMode, qPrintable(QString("%1 preserves auth mode").arg(label)));
	check(accepted.authAccountName == input.authAccountName, qPrintable(QString("%1 preserves auth account").arg(label)));
	check(accepted.authCredentialRef == input.authCredentialRef, qPrintable(QString("%1 preserves auth credential ref").arg(label)));
	if (input.authMode == dsk::TargetAuthMode::TwitchOAuth ||
	    input.authMode == dsk::TargetAuthMode::KickOAuth) {
		check(accepted.oauthClientId.isEmpty(), qPrintable(QString("%1 removes legacy user OAuth client id").arg(label)));
		check(accepted.oauthClientSecret.isEmpty(), qPrintable(QString("%1 removes legacy user OAuth client secret").arg(label)));
		check(accepted.oauthClientSecretRef.isEmpty(), qPrintable(QString("%1 removes legacy OAuth client secret ref").arg(label)));
	} else {
		check(accepted.oauthClientId == input.oauthClientId, qPrintable(QString("%1 preserves OAuth client id").arg(label)));
		check(accepted.oauthClientSecret == input.oauthClientSecret, qPrintable(QString("%1 preserves OAuth client secret").arg(label)));
		check(accepted.oauthClientSecretRef == input.oauthClientSecretRef, qPrintable(QString("%1 preserves OAuth client secret ref").arg(label)));
	}
	check(accepted.oauthRefreshToken == input.oauthRefreshToken, qPrintable(QString("%1 preserves OAuth refresh token").arg(label)));
	check(accepted.oauthRefreshTokenRef == input.oauthRefreshTokenRef, qPrintable(QString("%1 preserves OAuth refresh token ref").arg(label)));
	check(accepted.serverUrl == input.serverUrl.trimmed(), qPrintable(QString("%1 preserves trimmed server").arg(label)));
	check(accepted.streamKey == input.streamKey, qPrintable(QString("%1 preserves stream key").arg(label)));
	check(accepted.encoderGroup == input.encoderGroup, qPrintable(QString("%1 preserves encoder group").arg(label)));
	check(accepted.useSharedEncoder == input.useSharedEncoder, qPrintable(QString("%1 preserves shared encoder").arg(label)));
	check(accepted.autoStartWithObs == input.autoStartWithObs, qPrintable(QString("%1 preserves auto start").arg(label)));
	check(accepted.autoStopWithObs == input.autoStopWithObs, qPrintable(QString("%1 preserves auto stop").arg(label)));
	check(accepted.reconnectEnabled == input.reconnectEnabled, qPrintable(QString("%1 preserves reconnect enabled").arg(label)));
	check(accepted.reconnectMaxRetries == input.reconnectMaxRetries, qPrintable(QString("%1 preserves reconnect retries").arg(label)));
	check(accepted.reconnectDelaySeconds == input.reconnectDelaySeconds, qPrintable(QString("%1 preserves reconnect delay").arg(label)));
	check(accepted.videoBitrateKbps == input.videoBitrateKbps, qPrintable(QString("%1 preserves video bitrate").arg(label)));
	check(accepted.audioBitrateKbps == input.audioBitrateKbps, qPrintable(QString("%1 preserves audio bitrate").arg(label)));
	check(accepted.keyframeSeconds == input.keyframeSeconds, qPrintable(QString("%1 preserves keyframe interval").arg(label)));
	check(accepted.videoEncoderId == input.videoEncoderId, qPrintable(QString("%1 preserves video encoder id").arg(label)));
	check(accepted.audioEncoderId == input.audioEncoderId, qPrintable(QString("%1 preserves audio encoder id").arg(label)));
	check(accepted.sceneMode == input.sceneMode, qPrintable(QString("%1 preserves scene routing mode").arg(label)));
	check(accepted.sceneName == input.sceneName.trimmed(), qPrintable(QString("%1 preserves scene routing fallback").arg(label)));
	check(accepted.sceneUuid == input.sceneUuid.trimmed(), qPrintable(QString("%1 preserves scene routing fallback UUID").arg(label)));
	check(accepted.sceneRoutes.size() == input.sceneRoutes.size(), qPrintable(QString("%1 preserves scene route count").arg(label)));
	for (int i = 0; i < accepted.sceneRoutes.size() && i < input.sceneRoutes.size(); ++i) {
		check(accepted.sceneRoutes[i].obsSceneName == input.sceneRoutes[i].obsSceneName,
		      qPrintable(QString("%1 preserves scene route OBS name").arg(label)));
		check(accepted.sceneRoutes[i].obsSceneUuid == input.sceneRoutes[i].obsSceneUuid,
		      qPrintable(QString("%1 preserves scene route OBS UUID").arg(label)));
		check(accepted.sceneRoutes[i].outputSceneName == input.sceneRoutes[i].outputSceneName,
		      qPrintable(QString("%1 preserves scene route output name").arg(label)));
		check(accepted.sceneRoutes[i].outputSceneUuid == input.sceneRoutes[i].outputSceneUuid,
		      qPrintable(QString("%1 preserves scene route output UUID").arg(label)));
	}
	check(accepted.enabled == input.enabled, qPrintable(QString("%1 preserves enabled").arg(label)));
	check(accepted.startWithAll == input.startWithAll, qPrintable(QString("%1 preserves start with all").arg(label)));
	check(accepted.state == input.state, qPrintable(QString("%1 preserves state").arg(label)));
	check(accepted.lastError == input.lastError, qPrintable(QString("%1 preserves last error").arg(label)));
}

void testTargetEditDialogRoundTrips()
{
	dsk::OutputTarget youtube;
	youtube.id = "target-youtube";
	youtube.name = "YouTube Vertical";
	youtube.platformId = "youtube";
	youtube.authMode = dsk::TargetAuthMode::YouTubeOAuth;
	youtube.authAccountName = "channel@example.test";
	youtube.authCredentialRef = "DSK Multistream/oauth/youtube";
	youtube.oauthClientId = "youtube-client-id";
	youtube.oauthClientSecret = "youtube-client-secret";
	youtube.oauthClientSecretRef = "DSK Multistream/oauth-client-secret/youtube";
	youtube.oauthRefreshToken = "youtube-refresh-token";
	youtube.oauthRefreshTokenRef = "DSK Multistream/oauth-refresh-token/youtube";
	youtube.serverUrl = " rtmps://youtube.example/live ";
	youtube.streamKey = "yt-secret";
	youtube.encoderGroup = dsk::EncoderGroup::DskVertical;
	youtube.useSharedEncoder = false;
	youtube.autoStartWithObs = false;
	youtube.autoStopWithObs = false;
	youtube.reconnectEnabled = true;
	youtube.reconnectMaxRetries = 33;
	youtube.reconnectDelaySeconds = 9;
	youtube.videoBitrateKbps = 8000;
	youtube.audioBitrateKbps = 192;
	youtube.keyframeSeconds = 4;
	youtube.videoEncoderId = "obs_x264";
	youtube.audioEncoderId = "ffmpeg_aac";
	youtube.sceneMode = dsk::TargetSceneMode::LinkedScene;
	youtube.sceneName = "YouTube fallback";
	youtube.sceneUuid = "youtube-fallback-uuid";
	youtube.sceneRoutes.push_back({"Game", "gamß¯7¶‰žËkºwµçQMÑÉ•…µ-•åI•™•É•¹” ¤)ì(%‘Í¬èéA±…Ñ™½ÉµAÉ•Í•ÑI•¥ÍÑÉäÁ±…Ñ™½ÉµÌì(%‘Í¬èéQ…É•Ñ‘¥Ñ¥…±½œ‘¥…±½œ¡Á±…Ñ™½ÉµÌ¤ì((%‘Í¬èé=ÕÑÁÕÑQ…É•Ð¥¹ÁÕÐì(%¥¹ÁÕÐ¹¥€ô€‰Ñ…É•ÐµÍ…Ù•µ­•äˆì(%¥¹ÁÕÐ¹¹…µ”€ô€‰M…Ù•-•äQ…É•Ðˆì(%¥¹ÁÕÐ¹Á±…Ñ™½Éµ%€ô€‰ÑÝ¥Ñ ˆì(%¥¹ÁÕÐ¹…ÕÑ¡5½‘”€ô‘Í¬èéQ…É•ÑÕÑ¡5½‘”èé5…¹Õ…±IÑµÀì(%¥¹ÁÕÐ¹…ÕÑ¡É•‘•¹Ñ¥…±I•˜€ô€‰M,5Õ±Ñ¥ÍÑÉ•…´½ÍÑÉ•…´µ­•ä½Ñ…É•ÐµÍ…Ù•µ­•äˆì(%¥¹ÁÕÐ¹Í•ÉÙ•ÉUÉ°€ô€‰ÉÑµÀè¼½±¥Ù”¹ÑÝ¥Ñ ¹ÑØ½…ÁÀˆì(%¥¹ÁÕÐ¹ÍÑÉ•…µ-•ä¹±•…È ¤ì((%‘¥…±½œ¹Í•ÑQ…É•Ð¡¥¹ÁÕÐ¤ì(%‘Í¬èé=ÕÑÁÕÑQ…É•Ð…•ÁÑ•ì(%‘¥…±½œ¹™¥±±Q…É•Ð¡…•ÁÑ•¤ì((%EMÑÉ¥¹œ•ÉÉ½Èì(%¡•¬¡…•ÁÑ•¹ÍÑÉ•…µ-•ä¹¥ÍµÁÑä ¤°€‰‰±…¹¬ÍÑÉ•…´­•ä•‘¥Ð­••ÁÌ­•ä½ÕÐ½˜‘¥…±½œÑ…É•Ðˆ¤ì(%¡•¬¡…•ÁÑ•¹…ÕÑ¡É•‘•¹Ñ¥…±I•˜€ôô¥¹ÁÕÐ¹…ÕÑ¡É•‘•¹Ñ¥…±I•˜°€‰‰±…¹¬ÍÑÉ•…´­•ä•‘¥ÐÁÉ•Í•ÉÙ•ÌÍ…Ù•É•‘•¹Ñ¥…°É•˜ˆ¤ì(%¡•¬¡‘Í¬èéÙ…±¥‘…Ñ•=ÕÑÁÕÑQ…É•Ñ½¹™¥œ¡…•ÁÑ•°€™•ÉÉ½È¤°€‰Í…Ù•ÍÑÉ•…´­•äÉ•˜É•µ…¥¹Ì„Ù…±¥Ñ…É•Ðˆ¤ì)ô()Ù½¥Ñ•ÍÑQ…É•Ñ‘¥Ñ¥…±½UÍ•ÍMÉ½±±…‰±•½É´ ¤)ì(%‘Í¬èéA±…Ñ™½ÉµAÉ•Í•ÑI•¥ÍÑÉäÁ±…Ñ™½ÉµÌì(%‘Í¬èéQ…É•Ñ‘¥Ñ¥…±½œ‘¥…±½œ¡Á±…Ñ™½ÉµÌ¤ì((%…ÕÑ¼€©ÍÉ½±±É•„€ô‘¥…±½œ¹™¥¹‘¡¥±ñEMÉ½±±É•„€¨ø ¤ì(%¡•¬¡ÍÉ½±±É•„€„ô¹Õ±±ÁÑÈ°€‰Ñ…É•Ð•‘¥Ð‘¥…±½œ¡…Ì„ÍÉ½±±…‰±”™½É´…É•„ˆ¤ì(%¡•¬¡ÍÉ½±±É•„€˜˜ÍÉ½±±É•„´ùÝ¥‘•ÑI•Í¥é…‰±” ¤°€‰Ñ…É•Ð•‘¥Ð‘¥…±½œÍÉ½±°…É•„É•Í¥é•Ì¥ÑÌ™½É´ˆ¤ì(%¡•¬¡ÍÉ½±±É•„€˜˜ÍÉ½±±É•„´ùµ…á¥µÕµ!•¥¡Ð ¤€øô€ÈàÀ°€‰Ñ…É•Ð•‘¥Ð‘¥…±½œ­••ÁÌ„ÕÍ…‰±”ÍÉ½±°¡•¥¡Ðˆ¤ì)ô()Ù½¥Ñ•ÍÑI•™É•Í¡I½ÝÍ!¥‘•	•™½É••™•ÉÉ•‘•±•Ñ” ¤)ì(%E]¥‘•ÐÁ…É•¹Ðì(%…ÕÑ¼€©±…å½ÕÐ€ô¹•ÜEY	½á1…å½ÕÐ ™Á…É•¹Ð¤ì(%Á…É•¹Ð¹Í¡½Ü ¤ì((%EY•Ñ½ÈñEA½¥¹Ñ•ÈñE]¥‘•ÐøøÉ•Ñ¥É•‘I½ÝÌì(%™½È€¡¥¹Ð¤€ô€Àì¤€ð€Ðì€¬­¤¤ì($%…ÕÑ¼€©É½Ü€ô¹•ÜE]¥‘•Ð ™Á…É•¹Ð¤ì($%É½Ü´ùÍ•Ñ=‰©•Ñ9…µ”¡EMÑÉ¥¹1¥Ñ•É…° ‰Ñ…É•ÑI½Üˆ¤¤ì($%É½Ü´ùÍ•Ñ5¥¹¥µÕµ!•¥¡Ð ÈÐ¤ì($%±…å½ÕÐ´ù…‘‘]¥‘•Ð¡É½Ü¤ì($%É•Ñ¥É•‘I½ÝÌ¹ÁÕÍ¡}‰…¬¡É½Ü¤ì(%ô(%±…å½ÕÐ´ù…‘‘MÑÉ•Ñ  Ä¤ì(%E½É•ÁÁ±¥…Ñ¥½¸èéÁÉ½•ÍÍÙ•¹ÑÌ ¤ì(%™½È€¡½¹ÍÐEA½¥¹Ñ•ÈñE]¥‘•Ðø€™É½Ü€èÉ•Ñ¥É•‘I½ÝÌ¤($%¡•¬¡É½Ü€˜˜É½Ü´ù¥ÍY¥Í¥‰±” ¤°€‰ÍÑÉ•…´½¹ÑÉ½°É½ÝÌÍÑ…ÉÐÙ¥Í¥‰±”‰•™½É”„É•™É•Í ˆ¤ì((%‘Í¬èé±•…É1…å½ÕÑ]¥‘•ÑÍ½ÉI•™É•Í ¡±…å½ÕÐ°€Ä¤ì(%¡•¬¡±…å½ÕÐ´ù½Õ¹Ð ¤€ôô€Ä€˜˜±…å½ÕÐ´ù¥Ñ•µÐ À¤´ùÍÁ…•É%Ñ•´ ¤°($€€€€€€‰É•™É•Í É•µ½Ù•ÌÉ•Ñ¥É•É½ÝÌÝ¡¥±”ÁÉ•Í•ÉÙ¥¹œÑ¡”ÑÉ…¥±¥¹œ±…å½ÕÐÍÑÉ•Ñ ˆ¤ì(%™½È€¡½¹ÍÐEA½¥¹Ñ•ÈñE]¥‘•Ðø€™É½Ü€èÉ•Ñ¥É•‘I½ÝÌ¤ì($%¡•¬¡É½Ü°€‰É•Ñ¥É•ÍÑÉ•…´½¹ÑÉ½°É½ÝÌÉ•µ…¥¸Ù…±¥Õ¹Ñ¥°•™•ÉÉ•‘•±•Ñ”ÉÕ¹Ìˆ¤ì($%¡•¬¡É½Ü€˜˜É½Ü´ù¥Í!¥‘‘•¸ ¤°€‰É•Ñ¥É•ÍÑÉ•…´½¹ÑÉ½°É½ÝÌ¡¥‘”‰•™½É”•™•ÉÉ•‘•±•Ñ”…¸ÉÕ¸ˆ¤ì(%ô(($¼¼MÑ½À±°…¸ÅÕ•Õ”Í•Ù•É…°Ñ…É•ÐÍÑ…Ñ”¡…¹•Ì‰•™½É”EÐÁÉ½•ÍÍ•Ì($¼¼•™•ÉÉ•‘•±•Ñ”¸I•Á±…•µ•¹ÐÉ½ÝÌµÕÍÐ‰”Ñ¡”½¹±äÙ¥Í¥‰±”•¹•É…Ñ¥½¸¸(%EY•Ñ½ÈñEA½¥¹Ñ•ÈñE]¥‘•ÐøøÉ•Á±…•µ•¹ÑI½ÝÌì(%™½È€¡¥¹Ð¤€ô€Àì¤€ð€Ðì€¬­¤¤ì($%…ÕÑ¼€©É½Ü€ô¹•ÜE]¥‘•Ð ™Á…É•¹Ð¤ì($%É½Ü´ùÍ•Ñ=‰©•Ñ9…µ”¡EMÑÉ¥¹1¥Ñ•É…° ‰Ñ…É•ÑI½Üˆ¤¤ì($%É½Ü´ùÍ•Ñ5¥¹¥µÕµ!•¥¡Ð ÈÐ¤ì($%±…å½ÕÐ´ù¥¹Í•ÉÑ]¥‘•Ð¡±…å½ÕÐ´ù½Õ¹Ð ¤€´€Ä°É½Ü¤ì($%É•Á±…•µ•¹ÑI½ÝÌ¹ÁÕÍ¡}‰…¬¡É½Ü¤ì(%ô(%E½É•ÁÁ±¥…Ñ¥½¸èéÁÉ½•ÍÍÙ•¹ÑÌ ¤ì(%¥¹ÐÙ¥Í¥‰±•Q…É•ÑI½ÝÌ€ô€Àì(%™½È€¡E]¥‘•Ð€©É½Ü€èÁ…É•¹Ð¹™¥¹‘¡¥±‘É•¸ñE]¥‘•Ð€¨ø¡EMÑÉ¥¹1¥Ñ•É…° ‰Ñ…É•ÑI½Üˆ¤¤¤ì($%¥˜€¡É½Ü´ù¥ÍY¥Í¥‰±” ¤¤($$$¬­Ù¥Í¥‰±•Q…É•ÑI½ÝÌì(%ô(%¡•¬¡Ù¥Í¥‰±•Q…É•ÑI½ÝÌ€ôôÉ•Á±…•µ•¹ÑI½ÝÌ¹Í¥é” ¤°($€€€€€€‰„É•™É•Í ‰ÕÉÍÐ±•…Ù•Ì½¹±äÑ¡”™¥¹…°ÍÑÉ•…´½¹ÑÉ½°É½Ü•¹•É…Ñ¥½¸Ù¥Í¥‰±”ˆ¤ì((%E½É•ÁÁ±¥…Ñ¥½¸èéÍ•¹‘A½ÍÑ•‘Ù•¹ÑÌ¡¹Õ±±ÁÑÈ°EÙ•¹Ðèé•™•ÉÉ•‘•±•Ñ”¤ì(%™½È€¡½¹ÍÐEA½¥¹Ñ•ÈñE]¥‘•Ðø€™É½Ü€èÉ•Ñ¥É•‘I½ÝÌ¤($%¡•¬¡É½Ü¹¥Í9Õ±° ¤°€‰É•Ñ¥É•ÍÑÉ•…´½¹ÑÉ½°É½ÝÌ…É”‘•±•Ñ•…™Ñ•È•™•ÉÉ•‘•±•Ñ”ˆ¤ì)ô()Ù½¥Ñ•ÍÑ=ÕÑ¡½¹¹•Ñ½ÉI•©•ÑÍU¹ÍÕÁÁ½ÉÑ•‘5½‘” ¤)ì(%‘Í¬èé=ÕÑ¡½¹¹•Ñ½È½¹¹•Ñ½Èì(%¥¹Ð™¥¹¥Í¡½Õ¹Ð€ô€Àì(%‘Í¬èé=ÕÑ¡½¹¹•Ñ¥½¹I•ÍÕ±ÐÉ•ÍÕ±Ðì(%E=‰©•Ðèé½¹¹•Ð ™½¹¹•Ñ½È°€™‘Í¬èé=ÕÑ¡½¹¹•Ñ½Èèé™¥¹¥Í¡•°€™½¹¹•Ñ½È°($$$l™™¥¹¥Í¡½Õ¹Ð°€™É•ÍÕ±Ñt¡½¹ÍÐ‘Í¬èé=ÕÑ¡½¹¹•Ñ¥½¹I•ÍÕ±Ð€™Ù…±Õ”¤ì($$$$€¬­™¥¹¥Í¡½Õ¹Ðì($$$$É•ÍÕ±Ð€ôÙ…±Õ”ì($$$ô¤ì((%¡•¬ …½¹¹•Ñ½È¹‰•¥¸¡‘Í¬èéQ…É•ÑÕÑ¡5½‘”èé5…¹Õ…±IÑµÀ°EMÑÉ¥¹1¥Ñ•É…° ‰±¥•¹Ðˆ¤°EMÑÉ¥¹œ ¤¤°($€€€€€€‰=ÕÑ ½¹¹•Ñ½ÈÉ•©•ÑÌµ…¹Õ…°IQ5@µ½‘”ˆ¤ì(%¡•¬¡™¥¹¥Í¡½Õ¹Ð€ôô€Ä°€‰Õ¹ÍÕÁÁ½ÉÑ•=ÕÑ µ½‘”™¥¹¥Í¡•Ì•á…Ñ±ä½¹”ˆ¤ì(%¡•¬ …É•ÍÕ±Ð¹•ÉÉ½É5•ÍÍ…”¹¥ÍµÁÑä ¤°€‰Õ¹ÍÕÁÁ½ÉÑ•=ÕÑ µ½‘”É•ÑÕÉ¹Ì…¸•áÁ±…¹…Ñ¥½¸ˆ¤ì(%¡•¬ …½¹¹•Ñ½È¹¥ÍIÕ¹¹¥¹œ ¤°€‰Õ¹ÍÕÁÁ½ÉÑ•=ÕÑ µ½‘”±•…Ù•Ì¹¼ÉÕ¹¹¥¹œ…±±‰…¬Í•ÉÙ•Èˆ¤ì)ô()Ù½¥Ñ•ÍÑQÝ¥Ñ¡AÕ‰±¥Í¡•É=ÕÑ¡U¥¹‘5¥É…Ñ¥½¸ ¤)ì(%‘Í¬èéA±…Ñ™½ÉµAÉ•Í•ÑI•¥ÍÑÉäÁ±…Ñ™½ÉµÌì(%‘Í¬èéQ…É•Ñ‘¥Ñ¥…±½œ‘¥…±½œ¡Á±…Ñ™½ÉµÌ¤ì(%‘Í¬èé=ÕÑÁÕÑQ…É•Ð¥¹ÁÕÐì(%¥¹ÁÕÐ¹¥€ôEMÑÉ¥¹1¥Ñ•É…° ‰ÁÕ‰±¥Í¡•ÈµÑÝ¥Ñ ˆ¤ì(%¥¹ÁÕÐ¹¹…µ”€ôEMÑÉ¥¹1¥Ñ•É…° ‰QÝ¥Ñ ˆ¤ì(%¥¹ÁÕÐ¹Á±…Ñ™½Éµ%€ôEMÑÉ¥¹1¥Ñ•É…° ‰ÑÝ¥Ñ ˆ¤ì(%¥¹ÁÕÐ¹…ÕÑ¡5½‘”€ô‘Í¬èéQ…É•ÑÕÑ¡5½‘”èéQÝ¥Ñ¡=ÕÑ ì(%¥¹ÁÕÐ¹…ÕÑ¡½Õ¹Ñ9…µ”€ôEMÑÉ¥¹1¥Ñ•É…° ‰±•…äµ…½Õ¹Ðˆ¤ì(%¥¹ÁÕÐ¹…ÕÑ¡É•‘•¹Ñ¥…±I•˜€ôEMÑÉ¥¹1¥Ñ•É…° ‰M,5Õ±Ñ¥ÍÑÉ•…´½ÍÑÉ•…´µ­•ä½ÁÕ‰±¥Í¡•ÈµÑÝ¥Ñ ˆ¤ì(%¥¹ÁÕÐ¹½…ÕÑ¡±¥•¹Ñ%€ôEMÑÉ¥¹1¥Ñ•É…° ‰±•…äµÕÍ•Èµ±¥•¹Ðµ¥ˆ¤ì(%¥¹ÁÕÐ¹½…ÕÑ¡±¥•¹ÑM•É•Ð€ôEMÑÉ¥¹1¥Ñ•É…° ‰±•…äµÕÍ•ÈµÍ•É•Ðˆ¤ì(%¥¹ÁÕÐ¹½…ÕÑ¡±¥•¹ÑM•É•ÑI•˜€ôEMÑÉ¥¹1¥Ñ•É…° ‰M,5Õ±Ñ¥ÍÑÉ•…´½½…ÕÑ µ±¥•¹ÐµÍ•É•Ð½±•…äˆ¤ì(%¥¹ÁÕÐ¹Í•ÉÙ•ÉUÉ°€ôEMÑÉ¥¹1¥Ñ•É…° ‰ÉÑµÀè¼½±¥Ù”¹ÑÝ¥Ñ ¹ÑØ½…ÁÀˆ¤ì(%¥¹ÁÕÐ¹ÍÑÉ•…µ-•ä€ôEMÑÉ¥¹1¥Ñ•É…° ‰Í…Ù•µÍÑÉ•…´µ­•äˆ¤ì((%‘¥…±½œ¹Í•ÑQ…É•Ð¡¥¹ÁÕÐ¤ì(%…ÕÑ¼€©±¥•¹Ñ%€ô‘¥…±½œ¹™¥¹‘¡¥±ñE1¥¹•‘¥Ð€¨ø¡EMÑÉ¥¹1¥Ñ•É…° ‰‘Í­=ÕÑ¡±¥•¹Ñ%ˆ¤¤ì(%…ÕÑ¼€©±¥•¹ÑM•É•Ð€ô‘¥…±½œ¹™¥¹‘¡¥±ñE1¥¹•‘¥Ð€¨ø¡EMÑÉ¥¹1¥Ñ•É…° ‰‘Í­=ÕÑ¡±¥•¹ÑM•É•Ðˆ¤¤ì(%…ÕÑ¼€©½¹¹•Ñ	ÕÑÑ½¸€ô‘¥…±½œ¹™¥¹‘¡¥±ñEAÕÍ¡	ÕÑÑ½¸€¨ø¡EMÑÉ¥¹1¥Ñ•É…° ‰‘Í­½¹¹•Ñ=ÕÑ ˆ¤¤ì(%…ÕÑ¼€©‘¥Í½¹¹•Ñ	ÕÑÑ½¸€ô‘¥…±½œ¹™¥¹‘¡¥±ñEAÕÍ¡	ÕÑÑ½¸€¨ø¡EMÑÉ¥¹1¥Ñ•É…° ‰‘Í­¥Í½¹¹•Ñ=ÕÑ ˆ¤¤ì(%¡•¬¡±¥•¹Ñ%€˜˜±¥•¹Ñ%´ù¥Í!¥‘‘•¸ ¤°€‰QÝ¥Ñ ÁÕ‰±¥Í¡•È±½¥¸¡¥‘•ÌÑ¡”ÕÍ•È±¥•¹Ð%™¥•±ˆ¤ì(%¡•¬¡±¥•¹ÑM•É•Ð€˜˜±¥•¹ÑM•É•Ð´ù¥Í!¥‘‘•¸ ¤°€‰QÝ¥Ñ ÁÕ‰±¥Í¡•È±½¥¸¡¥‘•ÌÑ¡”ÕÍ•È±¥•¹ÐM•É•Ð™¥•±ˆ¤ì(%¡•¬¡½¹¹•Ñ	ÕÑÑ½¸€˜˜½¹¹•Ñ	ÕÑÑ½¸´ù¥Í¹…‰±• ¤€˜˜½¹¹•Ñ	ÕÑÑ½¸´ùÑ•áÐ ¤€ôô€‰I•½¹¹•ÐQÝ¥Ñ ˆ°($€€€€€€‰½¹¹•Ñ•QÝ¥Ñ ÁÕ‰±¥Í¡•È±½¥¸•áÁ½Í•Ì½¹”±•…ÈI•½¹¹•ÐQÝ¥Ñ …Ñ¥½¸ˆ¤ì(%¡•¬¡‘¥Í½¹¹•Ñ	ÕÑÑ½¸€˜˜€…‘¥Í½¹¹•Ñ	ÕÑÑ½¸´ù¥Í!¥‘‘•¸ ¤€˜˜‘¥Í½¹¹•Ñ	ÕÑÑ½¸´ù¥Í¹…‰±• ¤°($€€€€€€‰½¹¹•Ñ•QÝ¥Ñ ÁÕ‰±¥Í¡•È±½¥¸•áÁ½Í•Ì„¥Í½¹¹•Ð…Ñ¥½¸ˆ¤ì((%‘Í¬èé=ÕÑÁÕÑQ…É•Ð…•ÁÑ•ì(%‘¥…±½œ¹™¥±±Q…É•Ð¡…•ÁÑ•¤ì(%¡•¬¡…•ÁÑ•¹…ÕÑ¡5½‘”€ôô‘Í¬èéQ…É•ÑÕÑ¡5½‘”èéQÝ¥Ñ¡=ÕÑ °€‰QÝ¥Ñ ÁÕ‰±¥Í¡•È±½¥¸ÁÉ•Í•ÉÙ•Ì…ÕÑ µ½‘”ˆ¤ì(%¡•¬¡…•ÁÑ•¹…ÕÑ¡½Õ¹Ñ9…µ”€ôô¥¹ÁÕÐ¹…ÕÑ¡½Õ¹Ñ9…µ”°€‰QÝ¥Ñ ÁÕ‰±¥Í¡•È±½¥¸ÁÉ•Í•ÉÙ•ÌÑ¡”½¹¹•Ñ•…½Õ¹Ð±…‰•°ˆ¤ì(%¡•¬¡…•ÁÑ•¹½…ÕÑ¡±¥•¹Ñ%¹¥ÍµÁÑä ¤°€‰QÝ¥Ñ ÁÕ‰±¥Í¡•È±½¥¸É•µ½Ù•Ì±•…äÕÍ•È±¥•¹Ð%Ìˆ¤ì(%¡•¬¡…•ÁÑ•¹½…ÕÑ¡±¥•¹ÑM•É•Ð¹¥ÍµÁÑä ¤°€‰QÝ¥Ñ ÁÕ‰±¥Í¡•È±½¥¸É•µ½Ù•Ì±•…äÕÍ•È±¥•¹ÐM•É•ÑÌˆ¤ì(%¡•¬¡…•ÁÑ•¹½…ÕÑ¡±¥•¹ÑM•É•ÑI•˜¹¥ÍµÁÑä ¤°€‰QÝ¥Ñ ÁÕ‰±¥Í¡•È±½¥¸É•µ½Ù•Ì±•…ä±¥•¹ÐM•É•ÐÉ•™•É•¹•Ìˆ¤ì(%¡•¬¡…•ÁÑ•¹ÍÑÉ•…µ-•ä€ôô¥¹ÁÕÐ¹ÍÑÉ•…µ-•ä°€‰QÝ¥Ñ ÁÕ‰±¥Í¡•È±½¥¸ÁÉ•Í•ÉÙ•ÌÑ¡”…ÕÑ½µ…Ñ¥…±±äÉ•ÑÉ¥•Ù•ÍÑÉ•…´­•äˆ¤ì((%¡•¬¡E5•Ñ…=‰©•Ðèé¥¹Ù½­•5•Ñ¡½ ™‘¥…±½œ°€‰‘¥Í½¹¹•Ñ=ÕÑ¡½Õ¹Ðˆ°EÐèé¥É•Ñ½¹¹•Ñ¥½¸¤°($€€€€€€‰QÝ¥Ñ ¥Í½¹¹•Ð…Ñ¥½¸¥Ì¥¹Ù½­…‰±”ˆ¤ì(%‘¥…±½œ¹™¥±±Q…É•Ð¡…•ÁÑ•¤ì(%¡•¬¡…•ÁÑ•¹…ÕÑ¡½Õ¹Ñ9…µ”¹¥ÍµÁÑä ¤°€‰QÝ¥Ñ ¥Í½¹¹•Ð±•…ÉÌÑ¡”Í…Ù•…½Õ¹Ð±…‰•°¥¸Ñ¡”•‘¥Ñ•Ñ…É•Ðˆ¤ì(%¡•¬¡…•ÁÑ•¹…ÕÑ¡É•‘•¹Ñ¥…±I•˜¹¥ÍµÁÑä ¤°€‰QÝ¥Ñ ¥Í½¹¹•Ð±•…ÉÌÑ¡”Í…Ù•ÍÑÉ•…´­•äÉ•™•É•¹”¥¸Ñ¡”•‘¥Ñ•Ñ…É•Ðˆ¤ì(%¡•¬¡…•ÁÑ•¹ÍÑÉ•…µ-•ä¹¥ÍµÁÑä ¤°€‰QÝ¥Ñ ¥Í½¹¹•Ð±•…ÉÌÑ¡”ÍÑÉ•…´­•ä¥¸Ñ¡”•‘¥Ñ•Ñ…É•Ðˆ¤ì(%¡•¬¡‘¥Í½¹¹•Ñ	ÕÑÑ½¸´ù¥Í!¥‘‘•¸ ¤°€‰QÝ¥Ñ ¥Í½¹¹•Ð…Ñ¥½¸¡¥‘•Ì…™Ñ•È±½…°‘¥Í½¹¹•Ñ¥½¸ˆ¤ì(%¡•¬¡½¹¹•Ñ	ÕÑÑ½¸´ùÑ•áÐ ¤€ôô€‰½¹¹•ÐQÝ¥Ñ ˆ°€‰QÝ¥Ñ ¥Í½¹¹•ÐÉ•ÑÕÉ¹ÌÑ¡”ÁÉ¥µ…Éä…Ñ¥½¸Ñ¼½¹¹•ÐQÝ¥Ñ ˆ¤ì)ô()Ù½¥Ñ•ÍÑe½ÕQÕ‰•	Õ¹‘±•‘=ÕÑ¡U¤ ¤)ì(%‘Í¬èéA±…Ñ™½ÉµAÉ•Í•ÑI•¥ÍÑÉäÁ±…Ñ™½ÉµÌì(%‘Í¬èéQ…É•Ñ‘¥Ñ¥…±½œ‘¥…±½œ¡Á±…Ñ™½ÉµÌ¤ì(%‘¥…±½œ¹Í•Ñ9•ÝQ…É•Ñ•™…Õ±ÑÌ¡EMÑÉ¥¹1¥Ñ•É…° ‰‰Õ¹‘±•µå½ÕÑÕ‰”ˆ¤¤ì((%E½µ‰½	½à€©Á±…Ñ™½É´€ô¹Õ±±ÁÑÈì(%™½È€¡E½µ‰½	½à€©½µ‰¼€è‘¥…±½œ¹™¥¹‘¡¥±‘É•¸ñE½µ‰½	½à€¨ø ¤¤ì($%¥˜€¡½µ‰¼´ù™¥¹‘…Ñ„¡EMÑÉ¥¹1¥Ñ•É…° ‰å½ÕÑÕ‰”ˆ¤¤€øô€À¤ì($$%Á±…Ñ™½É´€ô½µ‰¼ì($$%‰É•…¬ì($%ô(%ô(%¡•¬¡Á±…Ñ™½É´€„ô¹Õ±±ÁÑÈ°€‰e½ÕQÕ‰”‰Õ¹‘±•=ÕÑ Ñ•ÍÐ™¥¹‘ÌÑ¡”Á±…Ñ™½É´Í•±•Ñ½Èˆ¤ì(%¥˜€ …Á±…Ñ™½É´¤($%É•ÑÕÉ¸ì(%Á±…Ñ™½É´´ùÍ•ÑÕÉÉ•¹Ñ%¹‘•à¡Á±…Ñ™½É´´ù™¥¹‘…Ñ„¡EMÑÉ¥¹1¥Ñ•É…° ‰å½ÕÑÕ‰”ˆ¤¤¤ì((%E½µ‰½	½à€©…ÕÑ¡5½‘”€ô¹Õ±±ÁÑÈì(%™½È€¡E½µ‰½	½à€©½µ‰¼€è‘¥…±½œ¹™¥¹‘¡¥±‘É•¸ñE½µ‰½	½à€¨ø ¤¤ì($%¥˜€¡½µ‰¼´ù™¥¹‘…Ñ„¡EMÑÉ¥¹1¥Ñ•É…° ‰å½ÕÑÕ‰”µ½…ÕÑ ˆ¤¤€øô€À¤ì($$%…ÕÑ¡5½‘”€ô½µ‰¼ì($$%‰É•…¬ì($%ô(%ô(%¡•¬¡…ÕÑ¡5½‘”€„ô¹Õ±±ÁÑÈ°€‰e½ÕQÕ‰”‰Õ¹‘±•=ÕÑ Ñ•ÍÐ™¥¹‘ÌÑ¡”½¹¹•Ñ¥½¸Í•±•Ñ½Èˆ¤ì(%¥˜€ ……ÕÑ¡5½‘”¤($%É•ÑÕÉ¸ì(%…ÕÑ¡5½‘”´ùÍ•ÑÕÉÉ•¹Ñ%¹‘•à¡…ÕÑ¡5½‘”´ù™¥¹‘…Ñ„¡EMÑÉ¥¹1¥Ñ•É…° ‰å½ÕÑÕ‰”µ½…ÕÑ ˆ¤¤¤ì((%…ÕÑ¼€©ÕÍÑ½µÁÀ€ô‘¥…±½œ¹™¥¹‘¡¥±ñE¡•­	½à€¨ø¡EMÑÉ¥¹1¥Ñ•É…° ‰‘Í­UÍ•ÕÍÑ½µ=ÕÑ¡ÁÀˆ¤¤ì(%…ÕÑ¼€©±¥•¹Ñ%€ô‘¥…±½œ¹™¥¹‘¡¥±ñE1¥¹•‘¥Ð€¨ø¡EMÑÉ¥¹1¥Ñ•É…° ‰‘Í­=ÕÑ¡±¥•¹Ñ%ˆ¤¤ì(%…ÕÑ¼€©±¥•¹ÑM•É•Ð€ô‘¥…±½œ¹™¥¹‘¡¥±ñE1¥¹•‘¥Ð€¨ø¡EMÑÉ¥¹1¥Ñ•É…° ‰‘Í­=ÕÑ¡±¥•¹ÑM•É•Ðˆ¤¤ì(%¥˜€ …‘Í¬èé½…ÕÑ¡!…Í	Õ¹‘±•‘±¥•¹ÑÉ•‘•¹Ñ¥…±Ì¡‘Í¬èéQ…É•ÑÕÑ¡5½‘”èée½ÕQÕ‰•=ÕÑ ¤¤ì($%¡•¬¡ÕÍÑ½µÁÀ€˜˜ÕÍÑ½µÁÀ´ù¥Í!¥‘‘•¸ ¤°($$€€€€€€‰Õ¹‰Õ¹‘±•‰Õ¥±‘Ì¡¥‘”Ñ¡”Õ¹…Ù…¥±…‰±”ÁÕ‰±¥Í¡•Èµ…ÁÀÍ•±•Ñ½Èˆ¤ì($%¡•¬¡±¥•¹Ñ%€˜˜€…±¥•¹Ñ%´ù¥Í!¥‘‘•¸ ¤°($$€€€€€€‰Õ¹‰Õ¹‘±•‰Õ¥±‘ÌÍ¡½ÜÑ¡”ÕÍÑ½´e½ÕQÕ‰”±¥•¹Ð%™¥•±ˆ¤ì($%¡•¬¡±¥•¹ÑM•É•Ð€˜˜€…±¥•¹ÑM•É•Ð´ù¥Í!¥‘‘•¸ ¤°($$€€€€€€‰Õ¹‰Õ¹‘±•‰Õ¥±‘ÌÍ¡½ÜÑ¡”ÕÍÑ½´e½ÕQÕ‰”±¥•¹ÐM•É•Ð™¥•±ˆ¤ì($%É•ÑÕÉ¸ì(%ô(%¡•¬¡ÕÍÑ½µÁÀ€˜˜€…ÕÍÑ½µÁÀ´ù¥Í!¥‘‘•¸ ¤€˜˜€…ÕÍÑ½µÁÀ´ù¥Í¡•­• ¤°($€€€€€€‰¹•Üe½ÕQÕ‰”Ñ…É•ÑÌ‘•™…Õ±ÐÑ¼Ñ¡”‰Õ¹‘±•=ÕÑ …ÁÁ±¥…Ñ¥½¸ˆ¤ì(%¡•¬¡±¥•¹Ñ%€˜˜±¥•¹Ñ%´ù¥Í!¥‘‘•¸ ¤°€‰‰Õ¹‘±•e½ÕQÕ‰”±½¥¸¡¥‘•ÌÑ¡”±¥•¹Ð%™¥•±ˆ¤ì(%¡•¬¡±¥•¹ÑM•É•Ð€˜˜±¥•¹ÑM•É•Ð´ù¥Í!¥‘‘•¸ ¤°€‰‰Õ¹‘±•e½ÕQÕ‰”±½¥¸¡¥‘•ÌÑ¡”±¥•¹ÐM•É•Ð™¥•±ˆ¤ì((%‘Í¬èé=ÕÑÁÕÑQ…É•Ð‰Õ¹‘±•ì(%‘¥…±½œ¹™¥±±Q…É•Ð¡‰Õ¹‘±•¤ì(%¡•¬¡‰Õ¹‘±•¹…ÕÑ¡5½‘”€ôô‘Í¬èéQ…É•ÑÕÑ¡5½‘”èée½ÕQÕ‰•=ÕÑ °($€€€€€€‰‰Õ¹‘±•e½ÕQÕ‰”±½¥¸ÁÉ•Í•ÉÙ•ÌÑ¡”e½ÕQÕ‰”…ÕÑ µ½‘”ˆ¤ì(%¡•¬¡‰Õ¹‘±•¹½…ÕÑ¡±¥•¹Ñ%¹¥ÍµÁÑä ¤€˜˜‰Õ¹‘±•¹½…ÕÑ¡±¥•¹ÑM•É•Ð¹¥ÍµÁÑä ¤€˜˜($€€€€€‰Õ¹‘±•¹½…ÕÑ¡±¥•¹ÑM•É•ÑI•˜¹¥ÍµÁÑä ¤°($€€€€€€‰‰Õ¹‘±•ÁÕ‰±¥Í¡•ÈÉ•‘•¹Ñ¥…±Ì…É”¹•Ù•È½Á¥•¥¹Ñ¼Ñ…É•ÐÍ•ÑÑ¥¹Ìˆ¤ì((%ÕÍÑ½µÁÀ´ùÍ•Ñ¡•­•¡ÑÉÕ”¤ì(%¡•¬ …±¥•¹Ñ%´ù¥Í!¥‘‘•¸ ¤€˜˜€…±¥•¹ÑM•É•Ð´ù¥Í!¥‘‘•¸ ¤°($€€€€€€‰ÕÍÑ½´½½±”=ÕÑ µ½‘”É•Ù•…±Ì‰½Ñ É•‘•¹Ñ¥…°™¥•±‘Ìˆ¤ì(%±¥•¹Ñ%´ùÍ•ÑQ•áÐ¡EMÑÉ¥¹1¥Ñ•É…° ‰ÕÍÑ½´µ±¥•¹Ð¹…ÁÁÌ¹½½±•ÕÍ•É½¹Ñ•¹Ð¹½´ˆ¤¤ì(%±¥•¹ÑM•É•Ð´ùÍ•ÑQ•áÐ¡EMÑÉ¥¹1¥Ñ•É…° ‰ÕÍÑ½´µÍ•É•Ðˆ¤¤ì(%‘Í¬èé=ÕÑÁÕÑQ…É•ÐÕÍÑ½´ì(%‘¥…±½œ¹™¥±±Q…É•Ð¡ÕÍÑ½´¤ì(%¡•¬¡ÕÍÑ½´¹½…ÕÑ¡±¥•¹Ñ%€ôô€‰ÕÍÑ½´µ±¥•¹Ð¹…ÁÁÌ¹½½±•ÕÍ•É½¹Ñ•¹Ð¹½´ˆ€˜˜($€€€€€ÕÍÑ½´¹½…ÕÑ¡±¥•¹ÑM•É•Ð€ôô€‰ÕÍÑ½´µÍ•É•Ðˆ°($€€€€€€‰ÕÍÑ½´½½±”=ÕÑ µ½‘”Á•ÉÍ¥ÍÑÌ•áÁ±¥¥Ñ±ä•¹Ñ•É•É•‘•¹Ñ¥…±Ìˆ¤ì)ô()Ù½¥Ñ•ÍÑ-¥­AÕ‰±¥Í¡•É=ÕÑ¡U¥¹‘5¥É…Ñ¥½¸ ¤)ì(%‘Í¬èéA±…Ñ™½ÉµAÉ•Í•ÑI•¥ÍÑÉäÁ±…Ñ™½ÉµÌì(%‘Í¬èéQ…É•Ñ‘¥Ñ¥…±½œ‘¥…±½œ¡Á±…Ñ™½ÉµÌ¤ì(%‘Í¬èé=ÕÑÁÕÑQ…É•Ð¥¹ÁÕÐì(%¥¹ÁÕÐ¹¥€ôEMÑÉ¥¹1¥Ñ•É…° ‰ÁÕ‰±¥Í¡•Èµ­¥¬ˆ¤ì(%¥¹ÁÕÐ¹¹…µ”€ôEMÑÉ¥¹1¥Ñ•É…° ‰-¥¬ˆ¤ì(%¥¹ÁÕÐ¹Á±…Ñ™½Éµ%€ôEMÑÉ¥¹1¥Ñ•É…° ‰­¥¬ˆ¤ì(%¥¹ÁÕÐ¹…ÕÑ¡5½‘”€ô‘Í¬èéQ…É•ÑÕÑ¡5½‘”èé-¥­=ÕÑ ì(%¥¹ÁÕÐ¹…ÕÑ¡½Õ¹Ñ9…µ”€ôEMÑÉ¥¹1¥Ñ•É…° ‰­¥¬µÕÍ•Èˆ¤ì(%¥¹ÁÕÐ¹…ÕÑ¡É•‘•¹Ñ¥…±I•˜€ôEMÑÉ¥¹1¥Ñ•É…° ‰M,5Õ±Ñ¥ÍÑÉ•…´½ÍÑÉ•…´µ­•ä½ÁÕ‰±¥Í¡•Èµ­¥¬ˆ¤ì(%¥¹ÁÕÐ¹½…ÕÑ¡±¥•¹Ñ%€ôEMÑÉ¥¹1¥Ñ•É…° ‰±•…äµÕÍ•Èµ±¥•¹Ðµ¥ˆ¤ì(%¥¹ÁÕÐ¹½…ÕÑ¡±¥•¹ÑM•É•Ð€ôEMÑÉ¥¹1¥Ñ•É…° ‰±•…äµÕÍ•ÈµÍ•É•Ðˆ¤ì(%¥¹ÁÕÐ¹½…ÕÑ¡I•™É•Í¡Q½­•¸€ôEMÑÉ¥¹1¥Ñ•É…° ‰±•…äµÉ•™É•Í µÑ½­•¸ˆ¤ì(%¥¹ÁÕÐ¹Í•ÉÙ•ÉUÉ°€ôEMÑÉ¥¹1¥Ñ•É…° ‰ÉÑµÁÌè¼½ÍÑÉ•…´¹­¥¬¹½´¼ÄÈÌÐÔØÜàäÀˆ¤ì(%¥¹ÁÕÐ¹ÍÑÉ•…µ-•ä€ôEMÑÉ¥¹1¥Ñ•É…° ‰Í…Ù•µ­¥¬µÍÑÉ•…´µ­•äˆ¤ì((%‘¥…±½œ¹Í•ÑQ…É•Ð¡¥¹ÁÕÐ¤ì(%…ÕÑ¼€©±¥•¹Ñ%€ô‘¥…±½œ¹™¥¹‘¡¥±ñE1¥¹•‘¥Ð€¨ø¡EMÑÉ¥¹1¥Ñ•É…° ‰‘Í­=ÕÑ¡±¥•¹Ñ%ˆ¤¤ì(%…ÕÑ¼€©±¥•¹ÑM•É•Ð€ô‘¥…±½œ¹™¥¹‘¡¥±ñE1¥¹•‘¥Ð€¨ø¡EMÑÉ¥¹1¥Ñ•É…° ‰‘Í­=ÕÑ¡±¥•¹ÑM•É•Ðˆ¤¤ì(%…ÕÑ¼€©½¹¹•Ñ	ÕÑÑ½¸€ô‘¥…±½œ¹™¥¹‘¡¥±ñEAÕÍ¡	ÕÑÑ½¸€¨ø¡EMÑÉ¥¹1¥Ñ•É…° ‰‘Í­½¹¹•Ñ=ÕÑ ˆ¤¤ì(%…ÕÑ¼€©‘¥Í½¹¹•Ñ	ÕÑÑ½¸€ô‘¥…±½œ¹™¥¹‘¡¥±ñEAÕÍ¡	ÕÑÑ½¸€¨ø¡EMÑÉ¥¹1¥Ñ•É…° ‰‘Í­¥Í½¹¹•Ñ=ÕÑ ˆ¤¤ì(%¡•¬¡±¥•¹Ñ%€˜˜±¥•¹Ñ%´ù¥Í!¥‘‘•¸ ¤°€‰-¥¬ÁÕ‰±¥Í¡•È±½¥¸¡¥‘•ÌÑ¡”ÕÍ•È±¥•¹Ð%™¥•±ˆ¤ì(%¡•¬¡±¥•¹ÑM•É•Ð€˜˜±¥•¹ÑM•É•Ð´ù¥Í!¥‘‘•¸ ¤°€‰-¥¬ÁÕ‰±¥Í¡•È±½¥¸¡¥‘•ÌÑ¡”ÕÍ•È±¥•¹ÐM•É•Ð™¥•±ˆ¤ì(%¡•¬¡½¹¹•Ñ	ÕÑÑ½¸€˜˜½¹¹•Ñ	ÕÑÑ½¸´ù¥Í¹…‰±• ¤€˜˜½¹¹•Ñ	ÕÑÑ½¸´ùÑ•áÐ ¤€ôô€‰I•½¹¹•Ð-¥¬ˆ°($€€€€€€‰½¹¹•Ñ•-¥¬ÁÕ‰±¥Í¡•È±½¥¸•áÁ½Í•Ì½¹”±•…ÈI•½¹¹•Ð-¥¬…Ñ¥½¸ˆ¤ì(%¡•¬¡‘¥Í½¹¹•Ñ	ÕÑÑ½¸€˜˜€…‘¥Í½¹¹•Ñ	ÕÑÑ½¸´ù¥Í!¥‘‘•¸ ¤€˜˜‘¥Í½¹¹•Ñ	ÕÑÑ½¸´ù¥Í¹…‰±• ¤°($€€€€€€‰½¹¹•Ñ•-¥¬ÁÕ‰±¥Í¡•È±½¥¸•áÁ½Í•Ì„¥Í½¹¹•Ð…Ñ¥½¸ˆ¤ì((%‘Í¬èé=ÕÑÁÕÑQ…É•Ð…•ÁÑ•ì(%‘¥…±½œ¹™¥±±Q…É•Ð¡…•ÁÑ•¤ì(%¡•¬¡…•ÁÑ•¹…ÕÑ¡5½‘”€ôô‘Í¬èéQ…É•ÑÕÑ¡5½‘”èé-¥­=ÕÑ °€‰-¥¬ÁÕ‰±¥Í¡•È±½¥¸ÁÉ•Í•ÉÙ•Ì…ÕÑ µ½‘”ˆ¤ì(%¡•¬¡…•ÁÑ•¹…ÕÑ¡½Õ¹Ñ9…µ”€ôô¥¹ÁÕÐ¹…ÕÑ¡½Õ¹Ñ9…µ”°€‰-¥¬ÁÕ‰±¥Í¡•È±½¥¸ÁÉ•Í•ÉÙ•ÌÑ¡”…½Õ¹Ð±…‰•°ˆ¤ì(%¡•¬¡…•ÁÑ•¹½…ÕÑ¡±¥•¹Ñ%¹¥ÍµÁÑä ¤€˜˜…•ÁÑ•¹½…ÕÑ¡±¥•¹ÑM•É•Ð¹¥ÍµÁÑä ¤€˜˜($$€€€€€…•ÁÑ•¹½…ÕÑ¡I•™É•Í¡Q½­•¸¹¥ÍµÁÑä ¤°($€€€€€€‰-¥¬ÁÕ‰±¥Í¡•È±½¥¸É•µ½Ù•Ì½‰Í½±•Ñ”‘•Í­Ñ½À=ÕÑ É•‘•¹Ñ¥…±Ìˆ¤ì(%¡•¬¡…•ÁÑ•¹ÍÑÉ•…µ-•ä€ôô¥¹ÁÕÐ¹ÍÑÉ•…µ-•ä°€‰-¥¬ÁÕ‰±¥Í¡•È±½¥¸ÁÉ•Í•ÉÙ•ÌÑ¡”É•ÑÉ¥•Ù•ÍÑÉ•…´­•äˆ¤ì((%¡•¬¡E5•Ñ…=‰©•Ðèé¥¹Ù½­•5•Ñ¡½ ™‘¥…±½œ°€‰‘¥Í½¹¹•Ñ=ÕÑ¡½Õ¹Ðˆ°EÐèé¥É•Ñ½¹¹•Ñ¥½¸¤°($€€€€€€‰-¥¬¥Í½¹¹•Ð…Ñ¥½¸¥Ì¥¹Ù½­…‰±”ˆ¤ì(%‘¥…±½œ¹™¥±±Q…É•Ð¡…•ÁÑ•¤ì(%¡•¬¡…•ÁÑ•¹…ÕÑ¡½Õ¹Ñ9…µ”¹¥ÍµÁÑä ¤°€‰-¥¬¥Í½¹¹•Ð±•…ÉÌÑ¡”Í…Ù•…½Õ¹Ð±…‰•°ˆ¤ì(%¡•¬¡…•ÁÑ•¹…ÕÑ¡É•‘•¹Ñ¥…±I•˜¹¥ÍµÁÑä ¤°€‰-¥¬¥Í½¹¹•Ð±•…ÉÌÑ¡”ÍÑÉ•…´­•äÉ•™•É•¹”ˆ¤ì(%¡•¬¡…•ÁÑ•¹ÍÑÉ•…µ-•ä¹¥ÍµÁÑä ¤°€‰-¥¬¥Í½¹¹•Ð±•…ÉÌÑ¡”ÍÑÉ•…´­•äˆ¤ì(%¡•¬¡‘¥Í½¹¹•Ñ	ÕÑÑ½¸´ù¥Í!¥‘‘•¸ ¤°€‰-¥¬¥Í½¹¹•Ð…Ñ¥½¸¡¥‘•Ì…™Ñ•È±½…°‘¥Í½¹¹•Ñ¥½¸ˆ¤ì(%¡•¬¡½¹¹•Ñ	ÕÑÑ½¸´ùÑ•áÐ ¤€ôô€‰½¹¹•Ð-¥¬ˆ°€‰-¥¬¥Í½¹¹•ÐÉ•ÑÕÉ¹ÌÑ¡”…Ñ¥½¸Ñ¼½¹¹•Ð-¥¬ˆ¤ì)ô()ô€¼¼¹…µ•ÍÁ…”()¥¹Ðµ…¥¸¡¥¹Ð…ÉŒ°¡…È€¨©…ÉØ¤)ì(%EÁÁ±¥…Ñ¥½¸…ÁÀ¡…ÉŒ°…ÉØ¤ì((%½¹ÍÐ…ÕÑ¼ÉÕ¸€ômt¡½¹ÍÐ¡…È€©¹…µ”°…ÕÑ¼Ñ•ÍÐ¤ì($%ÍÑèé•ÉÈ€ðð€‰IU8è€ˆ€ðð¹…µ”€ðð€q¸œì($%Ñ•ÍÐ ¤ì($%ÍÑèé•ÉÈ€ðð€‰=9è€ˆ€ðð¹…µ”€ðð€q¸œì(%ôì(%ÉÕ¸ ‰Ñ…É•Ð…‘‘•™…Õ±ÑÌˆ°Ñ•ÍÑQ…É•Ñ‘¥Ñ¥…±½‘‘•™…Õ±ÑÌ¤ì(%ÉÕ¸ ‰±•…ÈÙ•ÉÑ¥…°ÁÉ•Ù¥•ÜÍ•±•Ñ¥½¸ˆ°Ñ•ÍÑ1¥ÍÑM•±•Ñ¥½¹…¹	•±•…É•‘½ÉAÉ•Ù¥•Ý	…­É½Õ¹‘±¥¬¤ì(%ÉÕ¸ ‰Í•¹”É½ÕÑ¥¹œ¡¥‘‘•¸‰ä‘•™…Õ±Ðˆ°Ñ•ÍÑM•¹•I½ÕÑ¥¹½¹ÑÉ½±ÍÉ•!¥‘‘•¹	å•™…Õ±Ð¤ì(%ÉÕ¸ ‰ÕÍÑ½´Í•ÉÙ•ÈÁÉ•Í•ÉÙ…Ñ¥½¸ˆ°Ñ•ÍÑQ…É•Ñ‘¥Ñ¥…±½AÉ•Í•ÉÙ•ÍÕÍÑ½µM•ÉÙ•ÉÉ½ÍÍA±…Ñ™½Éµ¡…¹”¤ì(%ÉÕ¸ ‰Ñ…É•ÐÉ½Õ¹ÑÉ¥ÁÌˆ°Ñ•ÍÑQ…É•Ñ‘¥Ñ¥…±½I½Õ¹‘QÉ¥ÁÌ¤ì(%ÉÕ¸ ‰±•…ÈÕ¹ÕÍ•=ÕÑ Í•É•ÑÌˆ°Ñ•ÍÑQ…É•Ñ‘¥Ñ¥…±½±•…ÉÍU¹ÕÍ•‘=ÕÑ¡M•É•ÑÌ¤ì(%ÉÕ¸ ‰É•Á•…Ñ•‘¥…±½œÕÍ”ˆ°Ñ•ÍÑQ…É•Ñ‘¥Ñ¥…±½I•Á•…Ñ•‘UÍ”¤ì(%ÉÕ¸ ‰…•ÁÐ…¡•ÌÉ•ÍÕ±Ðˆ°Ñ•ÍÑQ…É•Ñ‘¥Ñ¥…±½•ÁÑ…¡•ÍI•ÍÕ±Ð¤ì(%ÉÕ¸ ‰Í…Ù”Ù…±¥‘…Ñ¥½¸ˆ°Ñ•ÍÑQ…É•Ñ‘¥Ñ¥…±½Y…±¥‘…Ñ•Í	•™½É•M…Ù”¤ì(%ÉÕ¸ ‰=ÕÑ )M=8•áÑÉ…Ñ¥½¸ˆ°Ñ•ÍÑQ…É•Ñ‘¥Ñ¥…±½=ÕÑ¡)Í½¹áÑÉ…Ñ¥½¸¤ì(%ÉÕ¸ ‰±¥•¹Ð¡…¹”¥¹Ù…±¥‘…Ñ•Ì±½¥¸ˆ°Ñ•ÍÑQ…É•Ñ‘¥Ñ¥…±½±¥•¹Ñ¡…¹•%¹Ù…±¥‘…Ñ•Í=±‘1½¥¸¤ì(%ÉÕ¸ ‰Í…Ù•ÍÑÉ•…´­•äÉ•™•É•¹”ˆ°Ñ•ÍÑQ…É•Ñ‘¥Ñ¥…±½-••ÁÍM…Ù•‘MÑÉ•…µ-•åI•™•É•¹”¤ì(%ÉÕ¸ ‰ÍÉ½±±…‰±”™½É´ˆ°Ñ•ÍÑQ…É•Ñ‘¥Ñ¥…±½UÍ•ÍMÉ½±±…‰±•½É´¤ì(%ÉÕ¸ ‰É•™É•Í É½ÝÌ¡¥‘”‰•™½É”‘•™•ÉÉ•‘•±•Ñ”ˆ°Ñ•ÍÑI•™É•Í¡I½ÝÍ!¥‘•	•™½É••™•ÉÉ•‘•±•Ñ”¤ì(%ÉÕ¸ ‰Õ¹ÍÕÁÁ½ÉÑ•=ÕÑ µ½‘”ˆ°Ñ•ÍÑ=ÕÑ¡½¹¹•Ñ½ÉI•©•ÑÍU¹ÍÕÁÁ½ÉÑ•‘5½‘”¤ì(%ÉÕ¸ ‰QÝ¥Ñ ÁÕ‰±¥Í¡•È=ÕÑ U$ˆ°Ñ•ÍÑQÝ¥Ñ¡AÕ‰±¥Í¡•É=ÕÑ¡U¥¹‘5¥É…Ñ¥½¸¤ì(%ÉÕ¸ ‰-¥¬ÁÕ‰±¥Í¡•È=ÕÑ U$ˆ°Ñ•ÍÑ-¥­AÕ‰±¥Í¡•É=ÕÑ¡U¥¹‘5¥É…Ñ¥½¸¤ì(%ÉÕ¸ ‰e½ÕQÕ‰”‰Õ¹‘±•=ÕÑ U$ˆ°Ñ•ÍÑe½ÕQÕ‰•	Õ¹‘±•‘=ÕÑ¡U¤¤ì((%¥˜€¡™…¥±ÕÉ•Ì€ø€À¤ì($%ÍÑèé•ÉÈ€ðð™…¥±ÕÉ•Ì€ðð€ˆU$Íµ½­”Ñ•ÍÐ¡•­Ì™…¥±•¹q¸ˆì($%É•ÑÕÉ¸€Äì(%ô((%ÍÑèé½ÕÐ€ðð€‰±°U$Íµ½­”Ñ•ÍÑÌÁ…ÍÍ•¹q¸ˆì(%É•ÑÕÉ¸€Àì)ô(