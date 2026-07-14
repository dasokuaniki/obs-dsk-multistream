#pragma once

#include <QString>
#include <QVector>

namespace dsk {

enum class EncoderGroup {
	DskHorizontal,
	DskVertical,
};

enum class TargetState {
	Stopped,
	Starting,
	Live,
	Stopping,
	Error,
};

enum class TargetAuthMode {
	ManualRtmp,
	TwitchOAuth,
	YouTubeOAuth,
	KickOAuth,
};

enum class TargetSceneMode {
	FollowObs,
	FixedScene,
	LinkedScene,
};

struct TargetSceneRoute {
	QString obsSceneName;
	QString obsSceneUuid;
	QString outputSceneName;
	QString outputSceneUuid;
};

struct OutputTarget {
	QString id;
	QString name;
	QString platformId;
	TargetAuthMode authMode = TargetAuthMode::ManualRtmp;
	QString authAccountName;
	QString authCredentialRef;
	QString oauthClientId;
	QString oauthClientSecret;
	QString oauthClientSecretRef;
	QString oauthRefreshToken;
	QString oauthRefreshTokenRef;
	QString serverUrl;
	QString streamKey;
	EncoderGroup encoderGroup = EncoderGroup::DskHorizontal;
	bool useSharedEncoder = true;
	bool autoStartWithObs = false;
	bool autoStopWithObs = true;
	bool reconnectEnabled = true;
	int reconnectMaxRetries = 20;
	int reconnectDelaySeconds = 2;
	int videoBitrateKbps = 0;
	int audioBitrateKbps = 0;
	int keyframeSeconds = 2;
	QString videoEncoderId;
	QString audioEncoderId;
	TargetSceneMode sceneMode = TargetSceneMode::FollowObs;
	QString sceneName;
	QString sceneUuid;
	QVector<TargetSceneRoute> sceneRoutes;
	bool enabled = true;
	bool startWithAll = true;
	TargetState state = TargetState::Stopped;
	QString lastError;
};

QString encoderGroupToString(EncoderGroup group);
EncoderGroup encoderGroupFromString(const QString &value);
QString targetStateToString(TargetState state);
QString targetAuthModeToString(TargetAuthMode mode);
TargetAuthMode targetAuthModeFromString(const QString &value);
QString targetAuthModeDisplayName(TargetAuthMode mode);
QString targetSceneModeToString(TargetSceneMode mode);
TargetSceneMode targetSceneModeFromString(const QString &value);
QString targetSceneModeDisplayName(TargetSceneMode mode);
bool platformSupportsAuthMode(const QString &platformId, TargetAuthMode mode);
bool migratePublisherManagedOAuthCredentials(OutputTarget &target);
QString maskedKey(const QString &streamKey);
QString newTargetId();
bool validateOutputTargetConfig(const OutputTarget &target, QString *errorMessage = nullptr, bool requireEnabled = true);
QVector<QString> startAllTargetIds(const QVector<OutputTarget> &targets);

} // namespace dsk
