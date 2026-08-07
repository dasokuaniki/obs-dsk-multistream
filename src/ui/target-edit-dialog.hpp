#pragma once

#include "core/output-target.hpp"
#include "core/oauth-connector.hpp"
#include "core/platform-preset-registry.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QToolButton;

namespace dsk {

class TargetEditDialog : public QDialog {
	Q_OBJECT

public:
	TargetEditDialog(const PlatformPresetRegistry &platforms, QWidget *parent = nullptr);

	void setTarget(const OutputTarget &target);
	void setNewTargetDefaults(const QString &targetId);
	void fillTarget(OutputTarget &target) const;
	OutputTarget acceptedTarget() const;
	bool validateForSave(QString *errorMessage = nullptr) const;

public slots:
	void accept() override;

private slots:
	void applyPlatformDefaults(int index);
	void usePresetServer();
	void updateAuthModes();
	void updatePlatformHint();
	void updateStreamKeyVisibility(bool show);
	void toggleOAuthConnection();
	void connectOAuthAccount();
	void disconnectOAuthAccount();
	void handleOAuthFinished(const dsk::OAuthConnectionResult &result);
	void applySelectedYouTubeStream(int index);

private:
	bool hasConnectedOAuthAccount(TargetAuthMode authMode) const;
	void resetYouTubeStreamOptions();
	void setYouTubeStreamOptions(QVector<YouTubeStreamOption> streams, bool preserveExistingKey);

	const PlatformPresetRegistry &platforms_;
	QString targetId_;
	QString authAccountName_;
	QString authCredentialRef_;
	QString oauthClientSecretRef_;
	QString oauthRefreshToken_;
	QString oauthRefreshTokenRef_;
	QString videoEncoderId_;
	QString audioEncoderId_;
	QString lastError_;
	QString nameValue_;
	QString platformIdValue_ = QStringLiteral("custom");
	QString authModeValue_ = targetAuthModeToString(TargetAuthMode::ManualRtmp);
	QString youtubeBroadcastModeValue_ = youtubeBroadcastModeToString(YouTubeBroadcastMode::Normal);
	QString oauthClientIdValue_;
	QString loadedOAuthClientId_;
	QString oauthClientSecretValue_;
	QString serverUrlValue_;
	QString streamKeyValue_;
	QString encoderGroupValue_ = encoderGroupToString(EncoderGroup::DskHorizontal);
	QString sceneModeValue_ = targetSceneModeToString(TargetSceneMode::FollowObs);
	QString sceneNameValue_;
	QString sceneUuidValue_;
	QVector<TargetSceneRoute> sceneRoutesValue_;
	TargetState targetState_ = TargetState::Stopped;
	TargetAuthMode loadedAuthMode_ = TargetAuthMode::ManualRtmp;
	TargetAuthMode connectedAuthMode_ = TargetAuthMode::ManualRtmp;
	bool oauthConnectedThisEdit_ = false;
	bool useSharedEncoderValue_ = true;
	bool autoStartWithObsValue_ = false;
	bool autoStopWithObsValue_ = true;
	bool reconnectEnabledValue_ = true;
	int reconnectMaxRetriesValue_ = 20;
	int reconnectDelaySecondsValue_ = 2;
	int videoBitrateKbpsValue_ = 0;
	int audioBitrateKbpsValue_ = 0;
	int keyframeSecondsValue_ = 2;
	bool enabledValue_ = true;
	bool startWithAllValue_ = true;
	bool serverFollowsPlatform_ = false;
	bool loading_ = false;
	QLineEdit *name_ = nullptr;
	QComboBox *platform_ = nullptr;
	QComboBox *authMode_ = nullptr;
	QComboBox *youtubeBroadcastMode_ = nullptr;
	QWidget *youtubeBroadcastModeLabel_ = nullptr;
	QComboBox *youtubeStream_ = nullptr;
	QWidget *youtubeStreamLabel_ = nullptr;
	QLabel *authStatus_ = nullptr;
	QLabel *youtubeLegalLinks_ = nullptr;
	QCheckBox *useCustomOAuthApp_ = nullptr;
	QLineEdit *oauthClientId_ = nullptr;
	QWidget *oauthClientIdLabel_ = nullptr;
	QLineEdit *oauthClientSecret_ = nullptr;
	QWidget *oauthClientSecretLabel_ = nullptr;
	QPushButton *connectOAuthButton_ = nullptr;
	QLabel *platformHint_ = nullptr;
	QLineEdit *server_ = nullptr;
	QPushButton *presetServerButton_ = nullptr;
	QLineEdit *streamKey_ = nullptr;
	QCheckBox *showStreamKey_ = nullptr;
	QComboBox *encoderGroup_ = nullptr;
	QComboBox *videoEncoder_ = nullptr;
	QToolButton *advancedSettingsToggle_ = nullptr;
	QWidget *advancedSettingsPanel_ = nullptr;
	QComboBox *sceneMode_ = nullptr;
	QLineEdit *sceneName_ = nullptr;
	QCheckBox *useSharedEncoder_ = nullptr;
	QCheckBox *autoStartWithObs_ = nullptr;
	QCheckBox *autoStopWithObs_ = nullptr;
	QCheckBox *reconnectEnabled_ = nullptr;
	QSpinBox *reconnectMaxRetries_ = nullptr;
	QSpinBox *reconnectDelaySeconds_ = nullptr;
	QSpinBox *videoBitrateKbps_ = nullptr;
	QSpinBox *audioBitrateKbps_ = nullptr;
	QSpinBox *keyframeSeconds_ = nullptr;
	QCheckBox *enabled_ = nullptr;
	QCheckBox *startWithAll_ = nullptr;
	QVector<YouTubeStreamOption> youtubeStreams_;
	OAuthConnector *oauthConnector_ = nullptr;
};

} // namespace dsk
