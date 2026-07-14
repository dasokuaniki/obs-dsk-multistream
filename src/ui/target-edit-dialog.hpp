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

namespace dsk {

class TargetEditDialog : public QDialog {
	Q_OBJECT

public:
	TargetEditDialog(const PlatformPresetRegistry &platforms, QWidget *parent = nullptr);

	void setTarget(const OutputTarget &target);
	void setNewTargetDefaults(const QString &targetId);
	void fillTarget(OutputTarget &target) const;
	OutputTarget acceptedTarget() const;

public slots:
	void accept() override;

private slots:
	void applyPlatformDefaults(int index);
	void usePresetServer();
	void updateAuthModes();
	void updatePlatformHint();
	void updateStreamKeyVisibility(bool show);
	void connectOAuthAccount();
	void disconnectOAuthAccount();
	void handleOAuthFinished(const dsk::OAuthConnectionResult &result);

private:
	bool hasConnectedOAuthAccount(TargetAuthMode authMode) const;

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
	QLabel *authStatus_ = nullptr;
	QCheckBox *useCustomOAuthApp_ = nullptr;
	QLineEdit *oauthClientId_ = nullptr;
	QWidget *oauthClientIdLabel_ = nullptr;
	QLineEdit *oauthClientSecret_ = nullptr;
	QWidget *oauthClientSecretLabel_ = nullptr;
	QPushButton *connectOAuthButton_ = nullptr;
	QPushButton *disconnectOAuthButton_ = nullptr;
	QLabel *platformHint_ = nullptr;
	QLineEdit *server_ = nullptr;
	QPushButton *presetServerButton_ = nullptr;
	QLineEdit *streamKey_ = nullptr;
	QCheckBox *showStreamKey_ = nullptr;
	QComboBox *encoderGroup_ = nullptr;
	QComboBox *videoEncoder_ = nullptr;
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
	OAuthConnector *oauthConnector_ = nullptr;
};

} // namespace dsk
