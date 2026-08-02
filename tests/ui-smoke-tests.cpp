#include "core/oauth-connector.hpp"
#include "core/oauth-provider.hpp"
#include "core/output-target.hpp"
#include "core/platform-preset-registry.hpp"
#include "ui/layout-widget-utils.hpp"
#include "ui/neutral-platform-badge.hpp"
#include "ui/target-edit-dialog.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QFormLayout>
#include <QLabel>
#include <QImage>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QPainter>
#include <QScrollArea>
#include <QString>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
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

		platform->setCurrentIndex(platform->findData(QStringLiteral("custom")));
		dialog.fillTarget(accepted);
		check(accepted.serverUrl.isEmpty(), "custom target clears a previously followed platform preset");
		check(accepted.authMode == dsk::TargetAuthMode::ManualRtmp, "custom target uses Manual RTMP authentication");
		if (presetServer)
			check(!presetServer->isEnabled(), "custom target disables an unavailable server preset");
		if (streamKey)
			check(streamKey->placeholderText() == "Enter stream key", "custom target asks for a stream key");
		if (platformHint)
			check(platformHint->text().contains("supplied by the platform"),
			      "custom target explains where to get its RTMP URL");
	}
}

bool dialogHasLabel(const dsk::TargetEditDialog &dialog, const QString &text)
{
	for (const QLabel *label : dialog.findChildren<QLabel *>()) {
		if (label->text() == text)
			return true;
	}
	return false;
}

void testTargetEditDialogLocalizationAndAdvancedSettings()
{
	dsk::PlatformPresetRegistry platforms;

	qputenv("DSK_UI_TEST_LOCALE", QByteArrayLiteral("ja-JP"));
	dsk::TargetEditDialog japaneseDialog(platforms);
	auto *japaneseIntro = japaneseDialog.findChild<QLabel *>(QStringLiteral("dskTargetIntro"));
	auto *japaneseAdvanced =
		japaneseDialog.findChild<QToolButton *>(QStringLiteral("dskAdvancedSettingsToggle"));
	auto *japaneseAdvancedPanel =
		japaneseDialog.findChild<QWidget *>(QStringLiteral("dskAdvancedSettingsPanel"));
	auto *japaneseVideoEncoder =
		japaneseDialog.findChild<QComboBox *>(QStringLiteral("dskTargetVideoEncoder"));
	auto *japaneseContent =
		japaneseDialog.findChild<QWidget *>(QStringLiteral("dskTargetContent"));
	auto *japaneseScrollArea =
		japaneseDialog.findChild<QScrollArea *>(QStringLiteral("dskTargetScrollArea"));
	auto *japaneseBasicForm =
		japaneseDialog.findChild<QFormLayout *>(QStringLiteral("dskTargetBasicForm"));
	auto *japaneseAdvancedForm =
		japaneseDialog.findChild<QFormLayout *>(QStringLiteral("dskTargetAdvancedForm"));
	auto *japaneseAuthStatus =
		japaneseDialog.findChild<QLabel *>(QStringLiteral("dskTargetAuthStatus"));
	auto *japanesePlatformHint =
		japaneseDialog.findChild<QLabel *>(QStringLiteral("dskPlatformHint"));

	check(japaneseDialog.windowTitle() == QStringLiteral("配信先を編集"),
	      "Japanese environment localizes the target editor title");
	check(japaneseIntro && japaneseIntro->text().contains(QStringLiteral("配信先を1つ設定")),
	      "Japanese environment localizes the target editor introduction");
	check(dialogHasLabel(japaneseDialog, QStringLiteral("配信先名")),
	      "Japanese environment localizes the basic form labels");
	check(dialogHasLabel(japaneseDialog, QStringLiteral("出力方向")),
	      "Japanese environment localizes the output mode label");
	check(japaneseAdvanced && japaneseAdvanced->text() == QStringLiteral("高度な設定"),
	      "Japanese environment localizes the advanced settings toggle");
	check(japaneseAdvancedPanel && japaneseAdvancedPanel->isHidden(),
	      "advanced settings are collapsed by default");
	check(japaneseContent && japaneseContent->layout() &&
		      japaneseContent->layout()->alignment().testFlag(Qt::AlignTop),
	      "target editor content stays top-aligned instead of stretching into empty space");
	check(japaneseScrollArea && japaneseScrollArea->alignment().testFlag(Qt::AlignTop),
	      "target editor scroll area pins compact content to the top");
	check(japaneseIntro &&
		      japaneseIntro->sizePolicy().verticalPolicy() == QSizePolicy::Fixed,
	      "target editor introduction cannot absorb unused vertical space");
	check(japaneseBasicForm && japaneseAdvancedForm &&
		      japaneseBasicForm->horizontalSpacing() == japaneseAdvancedForm->horizontalSpacing() &&
		      japaneseBasicForm->verticalSpacing() == japaneseAdvancedForm->verticalSpacing(),
	      "basic and advanced forms use the same spacing rules");
	check(japaneseBasicForm && japaneseAdvancedForm &&
		      japaneseBasicForm->labelAlignment() == japaneseAdvancedForm->labelAlignment() &&
		      japaneseBasicForm->fieldGrowthPolicy() == japaneseAdvancedForm->fieldGrowthPolicy(),
	      "basic and advanced forms align labels and fields consistently");
	if (japaneseBasicForm && japaneseAuthStatus) {
		int row = -1;
		QFormLayout::ItemRole role = QFormLayout::LabelRole;
		japaneseBasicForm->getWidgetPosition(japaneseAuthStatus, &row, &role);
		check(row >= 0 && role == QFormLayout::SpanningRole,
		      "authentication guidance spans the full form width without an empty label column");
	}
	if (japaneseBasicForm && japanesePlatformHint) {
		int row = -1;
		QFormLayout::ItemRole role = QFormLayout::LabelRole;
		japaneseBasicForm->getWidgetPosition(japanesePlatformHint, &row, &role);
		check(row >= 0 && role == QFormLayout::SpanningRole,
		      "platform guidance spans the full form width without an empty label column");
	}
	japaneseDialog.resize(620, 780);
	japaneseDialog.show();
	QCoreApplication::processEvents();
	check(japaneseContent && japaneseIntro && japaneseContent->layout() &&
		      japaneseIntro->geometry().top() <=
			      japaneseContent->layout()->contentsMargins().top() + 1,
	      "target editor does not leave a blank band above the introduction");
	check(japaneseVideoEncoder &&
		      japaneseVideoEncoder->itemText(0) == QStringLiteral("自動（推奨・H.264）"),
	      "Japanese environment uses an unambiguous automatic encoder label");
	check(japaneseVideoEncoder &&
		      japaneseVideoEncoder->toolTip().contains(QStringLiteral("DSK専用の別エンコーダー")),
	      "Japanese encoder help explains that DSK creates a separate encoder");
	if (japaneseAdvanced)
		japaneseAdvanced->setChecked(true);
	check(japaneseAdvancedPanel && !japaneseAdvancedPanel->isHidden(),
	      "advanced settings can be expanded");

	qputenv("DSK_UI_TEST_LOCALE", QByteArrayLiteral("en-US"));
	dsk::TargetEditDialog englishDialog(platforms);
	auto *englishAdvanced =
		englishDialog.findChild<QToolButton *>(QStringLiteral("dskAdvancedSettingsToggle"));
	auto *englishVideoEncoder =
		englishDialog.findChild<QComboBox *>(QStringLiteral("dskTargetVideoEncoder"));

	check(englishDialog.windowTitle() == QStringLiteral("Edit Destination"),
	      "English environment keeps the target editor in English");
	check(dialogHasLabel(englishDialog, QStringLiteral("Destination name")),
	      "English environment uses English basic form labels");
	check(englishAdvanced && englishAdvanced->text() == QStringLiteral("Advanced settings"),
	      "English environment localizes the advanced settings toggle");
	check(englishVideoEncoder &&
		      englishVideoEncoder->itemText(0) == QStringLiteral("Auto (recommended - H.264)"),
	      "English automatic encoder label does not imply sharing OBS output");
	check(englishVideoEncoder &&
		      !englishVideoEncoder->itemText(0).contains(QStringLiteral("OBS"), Qt::CaseInsensitive),
	      "automatic encoder label removes the misleading OBS wording");
	check(englishVideoEncoder &&
		      englishVideoEncoder->toolTip().contains(QStringLiteral("separate DSK encoder")),
	      "English encoder help explains that DSK creates a separate encoder");
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
	youtube.sceneRoutes.push_back({"Game", "game-uuid", "Game YouTube", "game-youtube-uuid"});
	youtube.sceneRoutes.push_back({"Talk", "talk-uuid", "Talk YouTube", "talk-youtube-uuid"});
	youtube.enabled = false;
	youtube.startWithAll = false;
	youtube.state = dsk::TargetState::Error;
	youtube.lastError = "previous error";
	checkRoundTrip(youtube, "YouTube OAuth target");

	dsk::OutputTarget twitch = youtube;
	twitch.id = "target-twitch";
	twitch.name = "Twitch Horizontal";
	twitch.platformId = "twitch";
	twitch.authMode = dsk::TargetAuthMode::TwitchOAuth;
	twitch.authAccountName = "twitch-user";
	twitch.authCredentialRef = "DSK Multistream/oauth/twitch";
	twitch.oauthRefreshToken.clear();
	twitch.oauthRefreshTokenRef.clear();
	twitch.serverUrl = "rtmp://live.twitch.tv/app";
	twitch.encoderGroup = dsk::EncoderGroup::DskHorizontal;
	twitch.enabled = true;
	twitch.startWithAll = true;
	twitch.state = dsk::TargetState::Live;
	twitch.lastError.clear();
	checkRoundTrip(twitch, "Twitch OAuth target");

	dsk::OutputTarget kick = twitch;
	kick.id = "target-kick";
	kick.name = "Kick Horizontal";
	kick.platformId = "kick";
	kick.authMode = dsk::TargetAuthMode::KickOAuth;
	kick.authAccountName = "kick-user";
	kick.authCredentialRef = "DSK Multistream/oauth/kick";
	kick.serverUrl = "rtmps://stream.kick.com/1234567890";
	checkRoundTrip(kick, "Kick OAuth target");

	dsk::OutputTarget legacy = youtube;
	legacy.id = "target-legacy-manual";
	legacy.name = "Legacy Manual RTMP";
	legacy.platformId = "tiktok";
	legacy.authMode = dsk::TargetAuthMode::ManualRtmp;
	legacy.serverUrl = "rtmps://legacy.example/live";
	dsk::PlatformPresetRegistry platforms;
	dsk::TargetEditDialog dialog(platforms);
	dialog.setTarget(legacy);
	dsk::OutputTarget migrated;
	dialog.fillTarget(migrated);
	check(migrated.platformId == "custom", "removed legacy preset migrates to Manual RTMP");
	check(migrated.serverUrl == legacy.serverUrl, "legacy Manual RTMP migration preserves the server URL");
	check(migrated.streamKey == legacy.streamKey, "legacy Manual RTMP migration preserves the stream key");
}

void testTargetEditDialogClearsUnusedOAuthSecrets()
{
	dsk::PlatformPresetRegistry platforms;
	dsk::TargetEditDialog dialog(platforms);

	dsk::OutputTarget input;
	input.id = QStringLiteral("target-stale-oauth");
	input.name = QStringLiteral("Manual target");
	input.platformId = QStringLiteral("custom");
	input.authMode = dsk::TargetAuthMode::ManualRtmp;
	input.oauthClientId = QStringLiteral("stale-client");
	input.oauthClientSecret = QStringLiteral("stale-secret");
	input.oauthClientSecretRef = QStringLiteral("DSK Multistream/oauth-client-secret/stale");
	input.oauthRefreshToken = QStringLiteral("stale-token");
	input.oauthRefreshTokenRef = QStringLiteral("DSK Multistream/oauth-refresh-token/stale");
	input.serverUrl = QStringLiteral("rtmp://example.test/live");
	input.streamKey = QStringLiteral("stream-key");

	dialog.setTarget(input);
	dsk::OutputTarget accepted;
	dialog.fillTarget(accepted);

	check(accepted.oauthClientId.isEmpty(), "manual target clears stale OAuth client id");
	check(accepted.oauthClientSecret.isEmpty(), "manual target clears stale OAuth client secret");
	check(accepted.oauthClientSecretRef.isEmpty(), "manual target clears stale OAuth client secret ref");
	check(accepted.oauthRefreshToken.isEmpty(), "manual target clears stale OAuth refresh token");
	check(accepted.oauthRefreshTokenRef.isEmpty(), "manual target clears stale OAuth refresh token ref");
}

void testTargetEditDialogRepeatedUse()
{
	dsk::PlatformPresetRegistry platforms;
	dsk::TargetEditDialog dialog(platforms);

	for (int i = 0; i < 50; ++i) {
		dialog.setNewTargetDefaults(QString("new-target-%1").arg(i));
		dsk::OutputTarget accepted;
		dialog.fillTarget(accepted);
		check(accepted.id == QString("new-target-%1").arg(i), "repeated new target preserves id");
		check(accepted.platformId == "twitch", "repeated new target defaults Twitch");
		check(accepted.authCredentialRef.isEmpty(), "repeated new target clears credential ref");
		check(accepted.oauthClientId.isEmpty(), "repeated new target clears OAuth client id");
		check(accepted.oauthClientSecret.isEmpty(), "repeated new target clears OAuth client secret");
		check(accepted.oauthClientSecretRef.isEmpty(), "repeated new target clears OAuth client secret ref");
		check(accepted.oauthRefreshToken.isEmpty(), "repeated new target clears OAuth refresh token");
		check(accepted.oauthRefreshTokenRef.isEmpty(), "repeated new target clears OAuth refresh token ref");
		check(accepted.videoEncoderId.isEmpty(), "repeated new target clears video encoder id");
		check(accepted.lastError.isEmpty(), "repeated new target clears last error");
	}
}

void testTargetEditDialogAcceptCachesResult()
{
	dsk::PlatformPresetRegistry platforms;
	dsk::TargetEditDialog dialog(platforms);

	dsk::OutputTarget input;
	input.id = "target-save-path";
	input.name = "Save Path YouTube";
	input.platformId = "youtube";
	input.authMode = dsk::TargetAuthMode::YouTubeOAuth;
	input.authAccountName = "youtube-user";
	input.authCredentialRef = "DSK Multistream/oauth/youtube";
	input.oauthClientId = "client-id";
	input.oauthClientSecret = "client-secret";
	input.oauthClientSecretRef = "DSK Multistream/oauth-client-secret/youtube";
	input.oauthRefreshToken = "refresh-token";
	input.oauthRefreshTokenRef = "DSK Multistream/oauth-refresh-token/youtube";
	input.serverUrl = " rtmps://a.rtmps.youtube.com/live2 ";
	input.streamKey = "saved-key";
	input.encoderGroup = dsk::EncoderGroup::DskVertical;
	input.useSharedEncoder = false;
	input.autoStartWithObs = false;
	input.autoStopWithObs = false;
	input.reconnectEnabled = true;
	input.reconnectMaxRetries = 12;
	input.reconnectDelaySeconds = 4;
	input.videoBitrateKbps = 6500;
	input.audioBitrateKbps = 160;
	input.keyframeSeconds = 2;
	input.videoEncoderId = "obs_x264";
	input.audioEncoderId = "ffmpeg_aac";
	input.enabled = true;
	input.startWithAll = false;

	dialog.setTarget(input);
	dialog.accept();

	const dsk::OutputTarget cached = dialog.acceptedTarget();
	check(cached.id == input.id, "accepted dialog exposes cached target id");
	check(cached.platformId == input.platformId, "cached accepted target preserves platform");

	dsk::OutputTarget accepted;
	dialog.fillTarget(accepted);
	check(accepted.id == input.id, "accepted dialog preserves id after Save");
	check(accepted.platformId == input.platformId, "accepted dialog preserves platform after Save");
	check(accepted.authMode == input.authMode, "accepted dialog preserves auth mode after Save");
	check(accepted.oauthClientSecret == input.oauthClientSecret, "accepted dialog preserves OAuth client secret after Save");
	check(accepted.oauthRefreshToken == input.oauthRefreshToken, "accepted dialog preserves OAuth refresh token after Save");
	check(accepted.serverUrl == input.serverUrl.trimmed(), "accepted dialog trims server after Save");
	check(accepted.streamKey == input.streamKey, "accepted dialog preserves stream key after Save");
	check(accepted.encoderGroup == input.encoderGroup, "accepted dialog preserves output mode after Save");
	check(accepted.enabled == input.enabled, "accepted dialog preserves enabled after Save");
	check(accepted.startWithAll == input.startWithAll, "accepted dialog preserves Start All flag after Save");
}

void testTargetEditDialogValidatesBeforeSave()
{
	dsk::PlatformPresetRegistry platforms;
	dsk::TargetEditDialog dialog(platforms);
	dsk::OutputTarget input;
	input.id = "invalid-save-target";
	input.name = "Invalid target";
	input.platformId = "custom";
	input.authMode = dsk::TargetAuthMode::ManualRtmp;
	input.serverUrl = "not-an-rtmp-url";
	input.streamKey = "key";
	dialog.setTarget(input);

	QString error;
	check(!dialog.validateForSave(&error), "dialog rejects an invalid server before Save");
	check(error == "Server URL must start with rtmp:// or rtmps://.", "dialog reports the invalid server reason");

	input.serverUrl = "rtmps://ingest.example.test/live";
	input.streamKey.clear();
	dialog.setTarget(input);
	check(!dialog.validateForSave(&error), "dialog rejects a missing stream key before Save");
	check(error == "Stream key is empty.", "dialog reports the missing stream key reason");

	input.platformId = "tiktok";
	input.serverUrl = "rtmp://push.tiktokcdn.com/live";
	input.streamKey = "key";
	dialog.setTarget(input);
	dsk::OutputTarget migrated;
	dialog.fillTarget(migrated);
	check(migrated.platformId == "custom", "dialog migrates a removed legacy preset to Manual RTMP");

	input.platformId = "custom";
	input.serverUrl = "rtmps://ingest.example.test/live";
	input.streamKey = "key";
	dialog.setTarget(input);
	check(dialog.validateForSave(&error), "dialog accepts a complete manual RTMP target");
}

void testTargetEditDialogOAuthJsonExtraction()
{
	dsk::PlatformPresetRegistry platforms;
	dsk::TargetEditDialog dialog(platforms);

	dsk::OutputTarget input;
	input.id = "target-google-json";
	input.name = "YouTube JSON";
	input.platformId = "youtube";
	input.authMode = dsk::TargetAuthMode::YouTubeOAuth;
	input.oauthClientId = "placeholder-client-id";
	input.oauthClientSecret =
		R"({"installed":{"client_id":"json-client-id.apps.googleusercontent.com","client_secret":"json-client-secret"}})";
	input.serverUrl = "rtmp://a.rtmp.youtube.com/live2";
	input.streamKey = "stream-key";

	dialog.setTarget(input);
	dsk::OutputTarget accepted;
	dialog.fillTarget(accepted);

	check(accepted.oauthClientId == "json-client-id.apps.googleusercontent.com", "dialog extracts Google OAuth client id from JSON");
	check(accepted.oauthClientSecret == "json-client-secret", "dialog extracts Google OAuth client secret from JSON");
}

void testTargetEditDialogClientChangeInvalidatesOldLogin()
{
	dsk::PlatformPresetRegistry platforms;
	dsk::TargetEditDialog dialog(platforms);

	dsk::OutputTarget input;
	input.id = "target-google-client-change";
	input.name = "YouTube client change";
	input.platformId = "youtube";
	input.authMode = dsk::TargetAuthMode::YouTubeOAuth;
	input.authAccountName = "old-channel";
	input.oauthClientId = "old-client-id.apps.googleusercontent.com";
	input.oauthClientSecret =
		R"({"installed":{"client_id":"new-client-id.apps.googleusercontent.com","client_secret":"new-client-secret"}})";
	input.oauthRefreshToken = "old-refresh-token";
	input.oauthRefreshTokenRef = "DSK Multistream/oauth-refresh-token/client-change";
	input.serverUrl = "rtmp://a.rtmp.youtube.com/live2";
	input.streamKey = "stream-key";

	dialog.setTarget(input);
	dsk::OutputTarget accepted;
	dialog.fillTarget(accepted);

	check(accepted.oauthClientId == "new-client-id.apps.googleusercontent.com", "client change extracts new Google client id");
	check(accepted.authAccountName.isEmpty(), "client change clears the old OAuth account label");
	check(accepted.oauthRefreshToken.isEmpty(), "client change clears the old OAuth refresh token");
	check(accepted.oauthRefreshTokenRef.isEmpty(), "client change clears the old OAuth refresh token reference");
}

void testTargetEditDialogKeepsSavedStreamKeyReference()
{
	dsk::PlatformPresetRegistry platforms;
	dsk::TargetEditDialog dialog(platforms);

	dsk::OutputTarget input;
	input.id = "target-saved-key";
	input.name = "Saved Key Target";
	input.platformId = "twitch";
	input.authMode = dsk::TargetAuthMode::ManualRtmp;
	input.authCredentialRef = "DSK Multistream/stream-key/target-saved-key";
	input.serverUrl = "rtmp://live.twitch.tv/app";
	input.streamKey.clear();

	dialog.setTarget(input);
	dsk::OutputTarget accepted;
	dialog.fillTarget(accepted);

	QString error;
	check(accepted.streamKey.isEmpty(), "blank stream key edit keeps key out of dialog target");
	check(accepted.authCredentialRef == input.authCredentialRef, "blank stream key edit preserves saved credential ref");
	check(dsk::validateOutputTargetConfig(accepted, &error), "saved stream key ref remains a valid target");
}

void testTargetEditDialogUsesScrollableForm()
{
	dsk::PlatformPresetRegistry platforms;
	dsk::TargetEditDialog dialog(platforms);

	auto *scrollArea = dialog.findChild<QScrollArea *>();
	check(scrollArea != nullptr, "target edit dialog has a scrollable form area");
	check(scrollArea && scrollArea->widgetResizable(), "target edit dialog scroll area resizes its form");
	check(scrollArea && scrollArea->maximumHeight() >= 280, "target edit dialog keeps a usable scroll height");
}

void testRefreshRowsHideBeforeDeferredDelete()
{
	QWidget parent;
	auto *layout = new QVBoxLayout(&parent);
	parent.show();

	QVector<QPointer<QWidget>> retiredRows;
	for (int i = 0; i < 4; ++i) {
		auto *row = new QWidget(&parent);
		row->setObjectName(QStringLiteral("targetRow"));
		row->setMinimumHeight(24);
		layout->addWidget(row);
		retiredRows.push_back(row);
	}
	layout->addStretch(1);
	QCoreApplication::processEvents();
	for (const QPointer<QWidget> &row : retiredRows)
		check(row && row->isVisible(), "stream control rows start visible before a refresh");

	dsk::clearLayoutWidgetsForRefresh(layout, 1);
	check(layout->count() == 1 && layout->itemAt(0)->spacerItem(),
	      "refresh removes retired rows while preserving the trailing layout stretch");
	for (const QPointer<QWidget> &row : retiredRows) {
		check(row, "retired stream control rows remain valid until DeferredDelete runs");
		check(row && row->isHidden(), "retired stream control rows hide before DeferredDelete can run");
	}

	// Stop All can queue several target state changes before Qt processes
	// DeferredDelete. Replacement rows must be the only visible generation.
	QVector<QPointer<QWidget>> replacementRows;
	for (int i = 0; i < 4; ++i) {
		auto *row = new QWidget(&parent);
		row->setObjectName(QStringLiteral("targetRow"));
		row->setMinimumHeight(24);
		layout->insertWidget(layout->count() - 1, row);
		replacementRows.push_back(row);
	}
	QCoreApplication::processEvents();
	int visibleTargetRows = 0;
	for (QWidget *row : parent.findChildren<QWidget *>(QStringLiteral("targetRow"))) {
		if (row->isVisible())
			++visibleTargetRows;
	}
	check(visibleTargetRows == replacementRows.size(),
	      "a refresh burst leaves only the final stream control row generation visible");

	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	for (const QPointer<QWidget> &row : retiredRows)
		check(row.isNull(), "retired stream control rows are deleted after DeferredDelete");
}

void testOAuthConnectorRejectsUnsupportedMode()
{
	dsk::OAuthConnector connector;
	int finishCount = 0;
	dsk::OAuthConnectionResult result;
	QObject::connect(&connector, &dsk::OAuthConnector::finished, &connector,
			 [&finishCount, &result](const dsk::OAuthConnectionResult &value) {
				 ++finishCount;
				 result = value;
			 });

	check(!connector.begin(dsk::TargetAuthMode::ManualRtmp, QStringLiteral("client"), QString()),
	      "OAuth connector rejects manual RTMP mode");
	check(finishCount == 1, "unsupported OAuth mode finishes exactly once");
	check(!result.errorMessage.isEmpty(), "unsupported OAuth mode returns an explanation");
	check(!connector.isRunning(), "unsupported OAuth mode leaves no running callback server");
}

void testOAuthConnectorAllowsInteractiveVerificationTime()
{
	check(dsk::oauthInteractiveTimeoutMs() >= 15 * 60 * 1000,
	      "OAuth callback listener stays available long enough for an unverified-app consent flow");
}

void testTwitchPublisherOAuthUiAndMigration()
{
	dsk::PlatformPresetRegistry platforms;
	dsk::TargetEditDialog dialog(platforms);
	dsk::OutputTarget input;
	input.id = QStringLiteral("publisher-twitch");
	input.name = QStringLiteral("Twitch");
	input.platformId = QStringLiteral("twitch");
	input.authMode = dsk::TargetAuthMode::TwitchOAuth;
	input.authAccountName = QStringLiteral("legacy-account");
	input.authCredentialRef = QStringLiteral("DSK Multistream/stream-key/publisher-twitch");
	input.oauthClientId = QStringLiteral("legacy-user-client-id");
	input.oauthClientSecret = QStringLiteral("legacy-user-secret");
	input.oauthClientSecretRef = QStringLiteral("DSK Multistream/oauth-client-secret/legacy");
	input.serverUrl = QStringLiteral("rtmp://live.twitch.tv/app");
	input.streamKey = QStringLiteral("saved-stream-key");

	dialog.setTarget(input);
	auto *clientId = dialog.findChild<QLineEdit *>(QStringLiteral("dskOAuthClientId"));
	auto *clientSecret = dialog.findChild<QLineEdit *>(QStringLiteral("dskOAuthClientSecret"));
	auto *connectButton = dialog.findChild<QPushButton *>(QStringLiteral("dskConnectOAuth"));
	auto *disconnectButton = dialog.findChild<QPushButton *>(QStringLiteral("dskDisconnectOAuth"));
	check(clientId && clientId->isHidden(), "Twitch publisher login hides the user Client ID field");
	check(clientSecret && clientSecret->isHidden(), "Twitch publisher login hides the user Client Secret field");
	check(connectButton && connectButton->isEnabled() && connectButton->text() == "Reconnect Twitch",
	      "connected Twitch publisher login exposes one clear Reconnect Twitch action");
	check(disconnectButton && !disconnectButton->isHidden() && disconnectButton->isEnabled(),
	      "connected Twitch publisher login exposes a Disconnect action");

	dsk::OutputTarget accepted;
	dialog.fillTarget(accepted);
	check(accepted.authMode == dsk::TargetAuthMode::TwitchOAuth, "Twitch publisher login preserves auth mode");
	check(accepted.authAccountName == input.authAccountName, "Twitch publisher login preserves the connected account label");
	check(accepted.oauthClientId.isEmpty(), "Twitch publisher login removes legacy user Client IDs");
	check(accepted.oauthClientSecret.isEmpty(), "Twitch publisher login removes legacy user Client Secrets");
	check(accepted.oauthClientSecretRef.isEmpty(), "Twitch publisher login removes legacy Client Secret references");
	check(accepted.streamKey == input.streamKey, "Twitch publisher login preserves the automatically retrieved stream key");

	check(QMetaObject::invokeMethod(&dialog, "disconnectOAuthAccount", Qt::DirectConnection),
	      "Twitch Disconnect action is invokable");
	dialog.fillTarget(accepted);
	check(accepted.authAccountName.isEmpty(), "Twitch Disconnect clears the saved account label in the edited target");
	check(accepted.authCredentialRef.isEmpty(), "Twitch Disconnect clears the saved stream key reference in the edited target");
	check(accepted.streamKey.isEmpty(), "Twitch Disconnect clears the stream key in the edited target");
	check(disconnectButton->isHidden(), "Twitch Disconnect action hides after local disconnection");
	check(connectButton->text() == "Connect Twitch", "Twitch Disconnect returns the primary action to Connect Twitch");
}

void testYouTubeBundledOAuthUi()
{
	dsk::PlatformPresetRegistry platforms;
	dsk::TargetEditDialog dialog(platforms);
	dialog.setNewTargetDefaults(QStringLiteral("bundled-youtube"));

	QComboBox *platform = nullptr;
	for (QComboBox *combo : dialog.findChildren<QComboBox *>()) {
		if (combo->findData(QStringLiteral("youtube")) >= 0) {
			platform = combo;
			break;
		}
	}
	check(platform != nullptr, "YouTube bundled OAuth test finds the platform selector");
	if (!platform)
		return;
	platform->setCurrentIndex(platform->findData(QStringLiteral("youtube")));

	QComboBox *authMode = nullptr;
	for (QComboBox *combo : dialog.findChildren<QComboBox *>()) {
		if (combo->findData(QStringLiteral("youtube-oauth")) >= 0) {
			authMode = combo;
			break;
		}
	}
	check(authMode != nullptr, "YouTube bundled OAuth test finds the connection selector");
	if (!authMode)
		return;
	authMode->setCurrentIndex(authMode->findData(QStringLiteral("youtube-oauth")));
	auto *broadcastMode = dialog.findChild<QComboBox *>(QStringLiteral("dskYouTubeBroadcastMode"));
	check(broadcastMode != nullptr, "YouTube target exposes the broadcast mode selector");
	check(broadcastMode && !broadcastMode->isHidden(),
	      "YouTube OAuth shows the broadcast mode selector");
	check(broadcastMode && broadcastMode->currentData() == QStringLiteral("normal"),
	      "YouTube target defaults to normal broadcast mode");
	if (broadcastMode)
		broadcastMode->setCurrentIndex(broadcastMode->findData(QStringLiteral("archive-rotation")));

	auto *customApp = dialog.findChild<QCheckBox *>(QStringLiteral("dskUseCustomOAuthApp"));
	auto *clientId = dialog.findChild<QLineEdit *>(QStringLiteral("dskOAuthClientId"));
	auto *clientSecret = dialog.findChild<QLineEdit *>(QStringLiteral("dskOAuthClientSecret"));
	if (!dsk::oauthHasBundledClientCredentials(dsk::TargetAuthMode::YouTubeOAuth)) {
		check(customApp && customApp->isHidden(),
		      "unbundled builds hide the unavailable publisher-app selector");
		check(clientId && !clientId->isHidden(),
		      "unbundled builds show the custom YouTube Client ID field");
		check(clientSecret && !clientSecret->isHidden(),
		      "unbundled builds show the custom YouTube Client Secret field");
		return;
	}
	check(customApp && !customApp->isHidden() && !customApp->isChecked(),
	      "new YouTube targets default to the bundled OAuth application");
	check(clientId && clientId->isHidden(), "bundled YouTube login hides the Client ID field");
	check(clientSecret && clientSecret->isHidden(), "bundled YouTube login hides the Client Secret field");

	dsk::OutputTarget bundled;
	dialog.fillTarget(bundled);
	check(bundled.authMode == dsk::TargetAuthMode::YouTubeOAuth,
	      "bundled YouTube login preserves the YouTube auth mode");
	check(bundled.youtubeBroadcastMode == dsk::YouTubeBroadcastMode::ArchiveRotation,
	      "target editor preserves archive rotation mode");
	check(bundled.oauthClientId.isEmpty() && bundled.oauthClientSecret.isEmpty() &&
	      bundled.oauthClientSecretRef.isEmpty(),
	      "bundled publisher credentials are never copied into target settings");

	customApp->setChecked(true);
	check(!clientId->isHidden() && !clientSecret->isHidden(),
	      "custom Google OAuth mode reveals both credential fields");
	clientId->setText(QStringLiteral("custom-client.apps.googleusercontent.com"));
	clientSecret->setText(QStringLiteral("custom-secret"));
	dsk::OutputTarget custom;
	dialog.fillTarget(custom);
	check(custom.oauthClientId == "custom-client.apps.googleusercontent.com" &&
	      custom.oauthClientSecret == "custom-secret",
	      "custom Google OAuth mode persists explicitly entered credentials");
}

void testYouTubeOAuthRequiresLegalConsent()
{
	dsk::PlatformPresetRegistry platforms;
	dsk::TargetEditDialog dialog(platforms);
	dialog.setNewTargetDefaults(QStringLiteral("youtube-legal-consent"));

	QComboBox *platform = nullptr;
	for (QComboBox *combo : dialog.findChildren<QComboBox *>()) {
		if (!platform && combo->findData(QStringLiteral("youtube")) >= 0)
			platform = combo;
	}
	check(platform != nullptr,
	      "YouTube legal consent test finds the platform selector");
	if (!platform)
		return;

	platform->setCurrentIndex(platform->findData(QStringLiteral("youtube")));

	QComboBox *authMode = nullptr;
	for (QComboBox *combo : dialog.findChildren<QComboBox *>()) {
		if (combo->findData(QStringLiteral("youtube-oauth")) >= 0) {
			authMode = combo;
			break;
		}
	}
	check(authMode != nullptr,
	      "YouTube legal consent test finds the login selector after selecting YouTube");
	if (!authMode)
		return;

	authMode->setCurrentIndex(authMode->findData(QStringLiteral("youtube-oauth")));

	auto *consent = dialog.findChild<QCheckBox *>(QStringLiteral("dskYouTubeDataConsent"));
	auto *links = dialog.findChild<QLabel *>(QStringLiteral("dskYouTubeLegalLinks"));
	auto *connectButton = dialog.findChild<QPushButton *>(QStringLiteral("dskConnectOAuth"));
	check(consent != nullptr && !consent->isHidden(),
	      "YouTube OAuth shows an explicit data-use consent control");
	check(consent && !consent->isChecked(),
	      "YouTube data-use consent is opt-in rather than preselected");
	check(links != nullptr && links->openExternalLinks(),
	      "YouTube OAuth shows externally openable policy links");
	check(links && links->text().contains(QStringLiteral("https://dsk.dasoku.org/privacy")) &&
		      links->text().contains(QStringLiteral("https://www.youtube.com/t/terms")) &&
		      links->text().contains(QStringLiteral("https://policies.google.com/privacy")) &&
		      links->text().contains(QStringLiteral("https://security.google.com/settings/security/permissions")),
	      "YouTube OAuth exposes DSK privacy, YouTube terms, Google privacy, and permission controls");
	check(connectButton && !connectButton->isEnabled(),
	      "YouTube OAuth cannot start before informed consent");

	if (consent)
		consent->setChecked(true);
	check(connectButton && connectButton->isEnabled(),
	      "accepting the data-use terms enables YouTube OAuth");

	authMode->setCurrentIndex(authMode->findData(QStringLiteral("manual-rtmp")));
	check(consent && consent->isHidden(),
	      "YouTube-specific consent stays out of Manual RTMP mode");
}

void testYouTubeStreamSelectionAfterLogin()
{
	dsk::PlatformPresetRegistry platforms;
	dsk::TargetEditDialog dialog(platforms);
	dsk::OutputTarget input;
	input.id = QStringLiteral("youtube-stream-selection");
	input.name = QStringLiteral("New Target");
	input.platformId = QStringLiteral("youtube");
	input.authMode = dsk::TargetAuthMode::YouTubeOAuth;
	input.serverUrl = QStringLiteral("rtmp://a.rtmp.youtube.com/live2");
	dialog.setTarget(input);

	auto *connector = dialog.findChild<dsk::OAuthConnector *>();
	check(connector != nullptr, "YouTube stream selection test finds the OAuth connector");
	if (!connector)
		return;

	dsk::OAuthConnectionResult result;
	result.authMode = dsk::TargetAuthMode::YouTubeOAuth;
	result.accountName = QStringLiteral("YouTube");
	result.refreshToken = QStringLiteral("refresh-token");
	result.youtubeStreams = {
		{QStringLiteral("first-id"), QStringLiteral("Main stream"),
		 QStringLiteral("rtmps://a.rtmps.youtube.com/live2"), QStringLiteral("secret-first-key"),
		 QStringLiteral("inactive")},
		{QStringLiteral("second-id"), QStringLiteral("Backup stream"),
		 QStringLiteral("rtmp://b.rtmp.youtube.com/live2"), QStringLiteral("secret-second-key"),
		 QStringLiteral("active")},
	};
	check(QMetaObject::invokeMethod(connector, "finished", Qt::DirectConnection,
					Q_ARG(dsk::OAuthConnectionResult, result)),
	      "YouTube OAuth result reaches the target editor");

	auto *streamSelector = dialog.findChild<QComboBox *>(QStringLiteral("dskYouTubeStream"));
	check(streamSelector != nullptr, "YouTube login exposes a named stream selector");
	check(streamSelector && !streamSelector->isHidden(), "YouTube OAuth shows the stream selector");
	check(streamSelector && streamSelector->count() == 3,
	      "YouTube stream selector contains manual entry and all reusable streams");
	if (!streamSelector)
		return;
	for (int i = 0; i < streamSelector->count(); ++i) {
		check(!streamSelector->itemText(i).contains(QStringLiteral("secret-first-key")) &&
			      !streamSelector->itemText(i).contains(QStringLiteral("secret-second-key")),
		      "YouTube stream selector never displays stream keys");
	}
	check(streamSelector->currentIndex() == 1, "a fresh YouTube login selects the first reusable stream");

	streamSelector->setCurrentIndex(2);
	dsk::OutputTarget selected;
	dialog.fillTarget(selected);
	check(selected.serverUrl == QStringLiteral("rtmp://b.rtmp.youtube.com/live2"),
	      "selecting a YouTube stream applies its ingestion server");
	check(selected.streamKey == QStringLiteral("secret-second-key"),
	      "selecting a YouTube stream applies its stream key");

	input.streamKey = QStringLiteral("existing-manual-key");
	dialog.setTarget(input);
	check(QMetaObject::invokeMethod(connector, "finished", Qt::DirectConnection,
					Q_ARG(dsk::OAuthConnectionResult, result)),
	      "a second YouTube OAuth result reaches the target editor");
	dialog.fillTarget(selected);
	check(streamSelector->currentIndex() == 0,
	      "a non-matching saved YouTube key keeps manual selection after reconnect");
	check(selected.streamKey == QStringLiteral("existing-manual-key"),
	      "YouTube stream lookup never overwrites a non-matching saved key");
}

void testKickPublisherOAuthUiAndMigration()
{
	dsk::PlatformPresetRegistry platforms;
	dsk::TargetEditDialog dialog(platforms);
	dsk::OutputTarget input;
	input.id = QStringLiteral("publisher-kick");
	input.name = QStringLiteral("Kick");
	input.platformId = QStringLiteral("kick");
	input.authMode = dsk::TargetAuthMode::KickOAuth;
	input.authAccountName = QStringLiteral("kick-user");
	input.authCredentialRef = QStringLiteral("DSK Multistream/stream-key/publisher-kick");
	input.oauthClientId = QStringLiteral("legacy-user-client-id");
	input.oauthClientSecret = QStringLiteral("legacy-user-secret");
	input.oauthRefreshToken = QStringLiteral("legacy-refresh-token");
	input.serverUrl = QStringLiteral("rtmps://stream.kick.com/1234567890");
	input.streamKey = QStringLiteral("saved-kick-stream-key");

	dialog.setTarget(input);
	auto *clientId = dialog.findChild<QLineEdit *>(QStringLiteral("dskOAuthClientId"));
	auto *clientSecret = dialog.findChild<QLineEdit *>(QStringLiteral("dskOAuthClientSecret"));
	auto *connectButton = dialog.findChild<QPushButton *>(QStringLiteral("dskConnectOAuth"));
	auto *disconnectButton = dialog.findChild<QPushButton *>(QStringLiteral("dskDisconnectOAuth"));
	check(clientId && clientId->isHidden(), "Kick publisher login hides the user Client ID field");
	check(clientSecret && clientSecret->isHidden(), "Kick publisher login hides the user Client Secret field");
	check(connectButton && connectButton->isEnabled() && connectButton->text() == "Reconnect Kick",
	      "connected Kick publisher login exposes one clear Reconnect Kick action");
	check(disconnectButton && !disconnectButton->isHidden() && disconnectButton->isEnabled(),
	      "connected Kick publisher login exposes a Disconnect action");

	dsk::OutputTarget accepted;
	dialog.fillTarget(accepted);
	check(accepted.authMode == dsk::TargetAuthMode::KickOAuth, "Kick publisher login preserves auth mode");
	check(accepted.authAccountName == input.authAccountName, "Kick publisher login preserves the account label");
	check(accepted.oauthClientId.isEmpty() && accepted.oauthClientSecret.isEmpty() &&
		      accepted.oauthRefreshToken.isEmpty(),
	      "Kick publisher login removes obsolete desktop OAuth credentials");
	check(accepted.streamKey == input.streamKey, "Kick publisher login preserves the retrieved stream key");

	check(QMetaObject::invokeMethod(&dialog, "disconnectOAuthAccount", Qt::DirectConnection),
	      "Kick Disconnect action is invokable");
	dialog.fillTarget(accepted);
	check(accepted.authAccountName.isEmpty(), "Kick Disconnect clears the saved account label");
	check(accepted.authCredentialRef.isEmpty(), "Kick Disconnect clears the stream key reference");
	check(accepted.streamKey.isEmpty(), "Kick Disconnect clears the stream key");
	check(disconnectButton->isHidden(), "Kick Disconnect action hides after local disconnection");
	check(connectButton->text() == "Connect Kick", "Kick Disconnect returns the action to Connect Kick");
}

void testNeutralKickBadgeRendering()
{
	QImage image(QSize(30, 30), QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	QPainter painter(&image);
	dsk::drawNeutralBadgeFrame(painter);
	dsk::drawNeutralMonogram(painter, QStringLiteral("K"));
	painter.end();

	int brightPixelCount = 0;
	int maximumSaturation = 0;
	qint64 brightPixelXTotal = 0;
	for (int y = 0; y < image.height(); ++y) {
		for (int x = 0; x < image.width(); ++x) {
			const QColor pixel = image.pixelColor(x, y);
			if (pixel.alpha() == 0)
				continue;
			maximumSaturation = std::max(maximumSaturation, pixel.hsvSaturation());
			if (qGray(pixel.rgb()) >= 180) {
				++brightPixelCount;
				brightPixelXTotal += x;
			}
		}
	}

	check(brightPixelCount >= 18, "neutral Kick badge renders a legible foreground monogram");
	if (brightPixelCount > 0) {
		const double brightCenterX = static_cast<double>(brightPixelXTotal) / brightPixelCount;
		check(brightCenterX >= 12.0 && brightCenterX <= 17.0,
		      "neutral Kick monogram remains visually centered in the shared badge frame");
	}
	check(maximumSaturation < 90,
	      "neutral Kick badge avoids official brand colors and remains monochrome");

	const QByteArray previewPath = qgetenv("DSK_UI_PREVIEW_PATH");
	if (!previewPath.isEmpty())
		check(image.save(QString::fromLocal8Bit(previewPath)), "neutral Kick preview can be saved");
}

} // namespace

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	qputenv("DSK_UI_TEST_LOCALE", QByteArrayLiteral("en-US"));

	const auto run = [](const char *name, auto test) {
		std::cerr << "RUN: " << name << '\n';
		test();
		std::cerr << "DONE: " << name << '\n';
	};
	run("target add defaults", testTargetEditDialogAddDefaults);
	run("target localization and advanced settings", testTargetEditDialogLocalizationAndAdvancedSettings);
	run("clear vertical preview selection", testListSelectionCanBeClearedForPreviewBackgroundClick);
	run("scene routing hidden by default", testSceneRoutingControlsAreHiddenByDefault);
	run("custom server preservation", testTargetEditDialogPreservesCustomServerAcrossPlatformChange);
	run("target round trips", testTargetEditDialogRoundTrips);
	run("clear unused OAuth secrets", testTargetEditDialogClearsUnusedOAuthSecrets);
	run("repeated dialog use", testTargetEditDialogRepeatedUse);
	run("accept caches result", testTargetEditDialogAcceptCachesResult);
	run("save validation", testTargetEditDialogValidatesBeforeSave);
	run("OAuth JSON extraction", testTargetEditDialogOAuthJsonExtraction);
	run("client change invalidates login", testTargetEditDialogClientChangeInvalidatesOldLogin);
	run("saved stream key reference", testTargetEditDialogKeepsSavedStreamKeyReference);
	run("scrollable form", testTargetEditDialogUsesScrollableForm);
	run("refresh rows hide before deferred delete", testRefreshRowsHideBeforeDeferredDelete);
	run("unsupported OAuth mode", testOAuthConnectorRejectsUnsupportedMode);
	run("OAuth interactive timeout", testOAuthConnectorAllowsInteractiveVerificationTime);
	run("Twitch publisher OAuth UI", testTwitchPublisherOAuthUiAndMigration);
	run("Kick publisher OAuth UI", testKickPublisherOAuthUiAndMigration);
	run("neutral Kick badge rendering", testNeutralKickBadgeRendering);
	run("YouTube bundled OAuth UI", testYouTubeBundledOAuthUi);
	run("YouTube OAuth legal consent", testYouTubeOAuthRequiresLegalConsent);
	run("YouTube stream selection", testYouTubeStreamSelectionAfterLogin);

	if (failures > 0) {
		std::cerr << failures << " UI smoke test checks failed.\n";
		return 1;
	}

	std::cout << "All UI smoke tests passed.\n";
	return 0;
}
