#pragma once

#include "core/encoder-profile-manager.hpp"
#include "core/http-client.hpp"
#include "core/layout-manager.hpp"
#include "core/output-runtime-status.hpp"
#include "core/output-target.hpp"
#include "core/platform-preset-registry.hpp"
#include "core/settings-store.hpp"
#include "core/vertical-scene-builder.hpp"

#include <QObject>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QVector>

#include <functional>

class QJsonArray;
class QJsonObject;

struct obs_encoder;
struct obs_output;
struct obs_service;
struct obs_source;
struct video_output;
typedef struct obs_encoder obs_encoder_t;
typedef struct obs_output obs_output_t;
typedef struct obs_service obs_service_t;
typedef struct obs_source obs_source_t;
typedef struct video_output video_t;

#ifdef DSK_ENABLE_OBS_CANVAS_API
struct obs_canvas;
typedef struct obs_canvas obs_canvas_t;
#endif

namespace dsk {

struct OutputStats {
	bool active = false;
	qint64 durationMs = 0;
	uint64_t totalBytes = 0;
	int totalFrames = 0;
	int droppedFrames = 0;
	float congestion = 0.0f;
};

class OutputManager : public QObject {
	Q_OBJECT

public:
	explicit OutputManager(QObject *parent = nullptr);
	~OutputManager() override;

	const QVector<OutputTarget> &targets() const;
	const PlatformPresetRegistry &platforms() const;
	LayoutManager &layouts();
	const LayoutManager &layouts() const;
	bool followObsScene() const;
	const QVector<SceneLayoutLink> &sceneLinks() const;
	QStringList obsSceneNames() const;
	QString currentObsSceneName() const;
	QString currentObsSceneUuid() const;
	QString resolvedObsSceneName(const QString &sceneUuid, const QString &fallbackSceneName = {}) const;
	QString effectiveOutputSceneName(const OutputTarget &target) const;

	bool addTarget(const OutputTarget &target);
	bool mutateTargetForUi(const QString &id, const std::function<void(OutputTarget &)> &mutator);
	void removeTarget(const QString &id);
	void setTargetEnabled(const QString &id, bool enabled);
	QString addRuntimeTarget(const OutputTarget &target);
	void removeRuntimeTarget(const QString &id);

	bool startTarget(const QString &id);
	void stopTarget(const QString &id);
	void applyYouTubeBroadcastSelection(const QString &targetId, quint64 sessionSerial, quint64 selectionGeneration,
					    const QString &broadcastId);
	void cancelYouTubeBroadcastSelection(const QString &targetId, quint64 sessionSerial,
					     quint64 selectionGeneration);
	void startAll();
	void stopAll();
	void suppressNextObsAutoStart();
	void suppressNextObsAutoStop();
	void clearNextObsAutoStartSuppression();
	void clearNextObsAutoStopSuppression();
	void handleObsStreamingStarted();
	void handleObsStreamingStopped();
	void handleObsSceneChanged();
	void refreshSceneIdentities();
	void reloadForCurrentProfile();
	void prepareForSceneCollectionChange();
	void handleOutputSignal(obs_output_t *output, quint64 expectedSerial, const QString &signalName,
				int reconnectDelaySeconds = 0);
	void handleOutputStopped(obs_output_t *output, quint64 expectedSerial, int code, const QString &lastError);
	void setFollowObsScene(bool follow);
	void upsertSceneLink(const SceneLayoutLink &link);
	void removeSceneLink(const QString &sceneName, const QString &sceneUuid = {});
	bool setTargetSceneMode(const QString &id, TargetSceneMode mode, const QString &fallbackSceneName);
	bool upsertTargetSceneRoute(const QString &id, const QString &obsSceneName, const QString &outputSceneName);
	OutputStats statsForTarget(const QString &id) const;
	TargetRuntimeStatus runtimeStatusForTarget(const QString &id) const;
	bool save();
	bool saveVerticalLayout();
	QString createVerticalScene(const QString &name);
	bool removeVerticalScene(const QString &id);
	bool renameVerticalScene(const QString &id, const QString &name);
	bool moveVerticalScene(const QString &id, int offset);
	bool reorderVerticalScenes(const QVector<QString> &orderedIds);
	void prepareForUnload();
	void releaseObsSceneReferences();

signals:
	void targetsChanged();
	void targetRuntimeChanged(const QString &targetId);
	void statusMessage(const QString &message);
	void youtubeBroadcastSelectionRequired(const QString &targetId, quint64 sessionSerial, quint64 selectionGeneration,
					       const QJsonArray &broadcasts);
	void verticalLayoutChanged();

private:
	struct Session;
	struct SharedEncoderSet;

	OutputTarget *findTarget(const QString &id);
	const OutputTarget *findTarget(const QString &id) const;
	EncoderProfile effectiveProfileForTarget(const OutputTarget &target) const;
	QString defaultVideoEncoderId() const;
	QString sharedEncoderKey(const OutputTarget &target, const EncoderProfile &profile) const;
	SharedEncoderSet *acquireSharedEncoders(const OutputTarget &target, const EncoderProfile &profile, video_t *video, QString *errorMessage);
	void releaseSharedEncoders(const QString &key);
	bool validateTarget(OutputTarget &target);
	bool hydrateTargetSecrets(OutputTarget &target);
	bool beginYouTubeStartPreflight(OutputTarget &target);
	bool startIndependentTarget(OutputTarget &target, Session *existingSession = nullptr);
	bool setTargetError(OutputTarget &target, const QString &message);
	Session *sessionForTarget(const QString &id) const;
	void releaseSession(const QString &id, bool requestStop);
	void releaseSession(const QString &id, bool requestStop, quint64 expectedSerial);
	void releaseSession(Session *session, bool requestStop);
	void releaseAllSessionsNow();
	void abortPlatformRequests();
	void releaseAllSharedEncoders();
	void loadSettingsFromCurrentProfile();
	bool finalizePendingRemoval(const QString &id);
	bool finalizeTargetRemoval(const QString &id, bool runtimeTarget);
	obs_service_t *createService(const OutputTarget &target);
	obs_output_t *createOutput(const OutputTarget &target, obs_service_t *service, quint64 sessionSerial);
	obs_encoder_t *createVideoEncoder(const OutputTarget &target, const EncoderProfile &profile);
	obs_encoder_t *createAudioEncoder(const OutputTarget &target, const EncoderProfile &profile);
	bool ensureVerticalCanvasVideo(QString *errorMessage);
	bool refreshVerticalCanvasScene(QString *errorMessage);
	video_t *videoForTarget(const OutputTarget &target, const EncoderProfile &profile, QString *errorMessage);
	video_t *videoForEncoderGroup(EncoderGroup group, QString *errorMessage);
	bool targetUsesSceneCanvas(const OutputTarget &target) const;
	QString sceneCanvasKeyForTarget(const OutputTarget &target) const;
	bool ensureSceneCanvasForTarget(const OutputTarget &target, const EncoderProfile &profile, QString *errorMessage);
	bool sessionUsesCanvasKey(const QString &key) const;
	void releaseTargetSceneCanvas(const QString &targetId);
	void releaseAllSceneCanvases();
	void refreshLinkedSceneCanvases();
	bool applyLinkedScene(const QString &sceneName);
	QString obsSceneUuidForName(const QString &sceneName) const;
	void applyProfileDelay(obs_output_t *output);
	bool shouldSuppressObsAutoStart();
	bool shouldSuppressObsAutoStop();
	bool sessionMatches(const QString &targetId, quint64 sessionSerial) const;
	TargetRuntimeStatus &ensureRuntimeStatus(const QString &targetId);
	void resetRuntimeStatus(const QString &targetId);
	void updateRuntimeStats(const QString &targetId);
	void setRuntimeTransport(const QString &targetId, quint64 sessionSerial, TransportState state, const QString &message = {},
				 int reconnectDelaySeconds = 0);
	void setRuntimePlatform(const QString &targetId, quint64 sessionSerial, PlatformLiveState state, const QString &message = {},
			       const QString &technicalError = {});
	void notifyCommentViewerYouTubeStarted(const QString &targetId, quint64 sessionSerial,
					       const QString &broadcastId);
	void resolveCommentViewerYouTubeBroadcastSelection(const QString &targetId, quint64 sessionSerial);
	void maybeStartYouTubeBroadcast(const QString &targetId, quint64 sessionSerial, int attempt = 0);
	bool youtubeOperationMatches(const QString &targetId, quint64 sessionSerial, quint64 operationGeneration) const;
	void completeYouTubeOperation(const QString &targetId, quint64 sessionSerial, quint64 operationGeneration);
	void scheduleYouTubePoll(const QString &targetId, quint64 sessionSerial, quint64 operationGeneration,
				 int attempt, int delayMs);
	bool stopYouTubeAutoStartIfTimedOut(const QString &targetId, quint64 sessionSerial,
					   quint64 operationGeneration);
	bool scheduleYouTubeRequestRetry(const QString &targetId, quint64 sessionSerial, quint64 operationGeneration,
					int attempt, const HttpResponse &response, const QString &stage);
	void refreshYouTubeAccessToken(const QString &targetId, quint64 sessionSerial, int attempt, quint64 operationGeneration);
	void listYouTubeBroadcasts(const QString &targetId, quint64 sessionSerial, const QString &accessToken, int attempt,
				   quint64 operationGeneration);
	void listYouTubeBroadcastPage(const QString &targetId, quint64 sessionSerial, const QString &accessToken, int attempt,
				      quint64 operationGeneration, const QString &broadcastStatus, const QString &pageToken, int pageNumber,
				      const QJsonArray &broadcasts);
	void listYouTubeStreams(const QString &targetId, quint64 sessionSerial, const QString &accessToken, int attempt,
				quint64 operationGeneration, const QJsonArray &broadcasts);
	void listYouTubeStreamBatch(const QString &targetId, quint64 sessionSerial, const QString &accessToken, int attempt,
				    quint64 operationGeneration, const QJsonArray &broadcasts, const QStringList &streamIds, int offset,
				    const QJsonArray &streams);
	void processYouTubeBroadcastSelection(const QString &targetId, quint64 sessionSerial, const QString &accessToken,
					      int attempt, quint64 operationGeneration, const QJsonArray &broadcasts,
					      const QJsonArray &streams);
	void transitionYouTubeBroadcast(const QString &targetId, const QString &accessToken, const QString &broadcastId,
					const QString &broadcastStatus, quint64 sessionSerial, int attempt,
					quint64 operationGeneration);
	void armYouTubeArchiveRotation(const QString &targetId, quint64 sessionSerial,
				      const QJsonObject &currentBroadcast);
	void beginYouTubeArchiveRotation(const QString &targetId, quint64 sessionSerial,
					quint64 timerGeneration);
	void refreshYouTubeRotationAccessToken(const QString &targetId, quint64 sessionSerial,
					      quint64 operationGeneration);
	void createYouTubeRotationBroadcast(const QString &targetId, quint64 sessionSerial,
					    quint64 operationGeneration, const QString &accessToken);
	void bindYouTubeRotationBroadcast(const QString &targetId, quint64 sessionSerial,
					  quint64 operationGeneration, const QString &accessToken,
					  const QString &nextBroadcastId);
	void completeCurrentYouTubeRotationBroadcast(const QString &targetId, quint64 sessionSerial,
						     quint64 operationGeneration,
						     const QString &accessToken);
	void pollYouTubeRotationBroadcast(const QString &targetId, quint64 sessionSerial,
					 quint64 operationGeneration, const QString &accessToken,
					 const QString &broadcastId, bool waitingForCurrentComplete);
	void startNextYouTubeRotationBroadcast(const QString &targetId, quint64 sessionSerial,
					       quint64 operationGeneration,
					       const QString &accessToken);
	void failYouTubeArchiveRotation(const QString &targetId, quint64 sessionSerial,
					quint64 operationGeneration, const QString &message,
					bool currentBroadcastMayBeComplete = false,
					bool discardPreparedBroadcast = true);
	void setTargetApiWarning(const QString &targetId, const QString &message, quint64 sessionSerial = 0);
	void clearTargetApiWarning(const QString &targetId);

	SettingsStore store_;
	QString loadedSettingsPath_;
	PlatformPresetRegistry platforms_;
	EncoderProfileManager encoders_;
	LayoutManager layouts_;
	VerticalLayout persistedVerticalLayout_;
	QVector<VerticalLayoutScene> persistedVerticalScenes_;
	QString persistedActiveVerticalSceneId_;
	VerticalSceneBuilder verticalScene_;
	bool followObsScene_ = false;
	QVector<SceneLayoutLink> sceneLinks_;
#ifdef DSK_ENABLE_OBS_CANVAS_API
	obs_canvas_t *verticalCanvas_ = nullptr;
	QHash<QString, obs_canvas_t *> sceneCanvases_;
#endif
	QVector<OutputTarget> targets_;
	QVector<Session *> sessions_;
	QHash<QString, TargetRuntimeStatus> runtimeStatuses_;
	QHash<QString, SharedEncoderSet *> sharedEncoders_;
	HttpClient *http_ = nullptr;
	QSet<QString> runtimeTargetIds_;
	QSet<QString> pendingPersistentRemovalIds_;
	QSet<QString> pendingRuntimeRemovalIds_;
	bool shuttingDown_ = false;
	bool suppressNextObsAutoStart_ = false;
	bool suppressNextObsAutoStop_ = false;
	bool unloadPrepared_ = false;
	quint64 nextSessionSerial_ = 1;
	qint64 suppressObsAutoStartUntilMs_ = 0;
	qint64 suppressObsAutoStopUntilMs_ = 0;
};

} // namespace dsk
