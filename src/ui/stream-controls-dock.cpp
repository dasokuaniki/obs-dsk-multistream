#include "ui/stream-controls-dock.hpp"

#include "ui/stream-control-visuals.hpp"
#include "ui/stream-control-assets.hpp"
#include "ui/stream-controls-state.hpp"

#include "core/diagnostics.hpp"
#include "core/oauth-provider.hpp"
#include "core/youtube-api-warning.hpp"

#include <obs.h>
#include <obs-frontend-api.h>

#include <QDateTime>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QShowEvent>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace dsk {
namespace {

struct ObsNativeStreamInfo {
	bool available = false;
	QString platformId = "custom";
	QString serviceName = "OBS Native Stream";
	QString accountLabel;
	QString detail = "OBS native stream";
};

bool isRunning(const OutputTarget &target)
{
	return target.state == TargetState::Live || target.state == TargetState::Starting;
}

bool isRunning(const OutputTarget &target, const TargetRuntimeStatus &runtime)
{
	return isRunning(target) || runtimeTransportIsRunning(runtime);
}

bool isBusy(const OutputTarget &target, const TargetRuntimeStatus &runtime)
{
	return target.state == TargetState::Starting || target.state == TargetState::Stopping || runtimeTransportIsBusy(runtime);
}

bool hasYouTubeApiLogin(const OutputTarget &target)
{
	return target.authMode == TargetAuthMode::YouTubeOAuth &&
	       oauthHasUsableClientCredentials(target.authMode, target.oauthClientId, target.oauthClientSecret,
					     target.oauthClientSecretRef) &&
	       (!target.oauthRefreshToken.trimmed().isEmpty() || !target.oauthRefreshTokenRef.trimmed().isEmpty());
}

QString rowDetailText(const OutputTarget &target, const TargetRuntimeStatus &runtime, bool suppressIndependentTwitch = false)
{
	if (shouldSuppressIndependentTwitchTarget(target, suppressIndependentTwitch) && !isRunning(target, runtime))
		return QStringLiteral("Handled by OBS Twitch Dual Format");
	if (runtimeHasSession(runtime) || runtimeTransportIsRunning(runtime)) {
		if (isYouTubeTarget(target) && runtimeTransportIsRunning(runtime) &&
		    runtimePlatformIsWarning(runtime.platform)) {
			switch (runtime.platform) {
			case PlatformLiveState::AuthExpired:
				return QStringLiteral("YouTube preparing - login expired");
			case PlatformLiveState::NeedsManualStart:
				return QStringLiteral("YouTube preparing - start in Studio");
			case PlatformLiveState::QuotaBlocked:
				return QStringLiteral("YouTube preparing - API quota");
			case PlatformLiveState::BroadcastMismatch:
				return QStringLiteral("YouTube preparing - key mismatch");
			case PlatformLiveState::MultipleBroadcasts:
				return QStringLiteral("YouTube preparing - multiple broadcasts");
			case PlatformLiveState::WaitingForSignal:
				return QStringLiteral("YouTube preparing - waiting for signal");
			case PlatformLiveState::Failed:
				return QStringLiteral("YouTube preparing - API warning");
			case PlatformLiveState::Preparing:
				return QStringLiteral("YouTube preparing");
			case PlatformLiveState::RtmpSignalOnly:
			case PlatformLiveState::Unknown:
				return QStringLiteral("Checking YouTube Live");
			case PlatformLiveState::NotApplicable:
			case PlatformLiveState::Testing:
			case PlatformLiveState::LiveStarting:
			case PlatformLiveState::Live:
				break;
			}
		}
		return runtimeStatusDetail(target, runtime);
	}
	if (!target.enabled)
		return "Auto start off";
	if (targetHasLiveYouTubeApiWarning(target) || targetHasYouTubeApiWarning(target))
		return liveYouTubeApiWarningRowText(target.lastError.trimmed());
	if (!target.startWithAll && target.state == TargetState::Stopped)
		return "Start All off";
	if (target.state == TargetState::Stopped) {
		QString error;
		return validateOutputTargetConfig(target, &error) ? QStringLiteral("Ready") : QStringLiteral("Needs setup");
	}
	return targetStateToString(target.state);
}

QString actionText(const OutputTarget &target, const TargetRuntimeStatus &runtime)
{
	if (target.state == TargetState::Starting || runtime.transport == TransportState::Starting)
		return "Starting";
	if (target.state == TargetState::Stopping || runtime.transport == TransportState::Stopping)
		return "Stopping";
	return isRunning(target, runtime) ? QStringLiteral("Stop") : QStringLiteral("Start");
}

QString youtubePrivacyLabel(const QString &privacy)
{
	if (privacy == QStringLiteral("public"))
		return QStringLiteral("公開");
	if (privacy == QStringLiteral("unlisted"))
		return QStringLiteral("限定公開");
	if (privacy == QStringLiteral("private"))
		return QStringLiteral("非公開");
	return privacy;
}

QString youtubeBroadcastChoiceLabel(const QJsonObject &broadcast)
{
	const QJsonObject snippet = broadcast.value(QStringLiteral("snippet")).toObject();
	const QJsonObject status = broadcast.value(QStringLiteral("status")).toObject();
	QString title = snippet.value(QStringLiteral("title")).toString().trimmed();
	if (title.isEmpty())
		title = QStringLiteral("タイトルなし");

	QStringList details;
	const QDateTime scheduled = QDateTime::fromString(
		snippet.value(QStringLiteral("scheduledStartTime")).toString(), Qt::ISODate);
	if (scheduled.isValid())
		details.push_back(scheduled.toLocalTime().toString(QStringLiteral("M/d HH:mm")));
	const QString privacy = youtubePrivacyLabel(status.value(QStringLiteral("privacyStatus")).toString());
	if (!privacy.isEmpty())
		details.push_back(privacy);

	return details.isEmpty() ? title : QStringLiteral("%1 — %2").arg(title, details.join(QStringLiteral(" / ")));
}

QString buttonStyle(const OutputTarget &target, const TargetRuntimeStatus &runtime, bool suppressIndependentTwitch = false)
{
	const bool transitioning = target.state == TargetState::Starting || target.state == TargetState::Stopping ||
				   runtimeTransportIsBusy(runtime);
	const QString color = suppressIndependentTwitch ? QStringLiteral("#66717a")
				     : transitioning ? QStringLiteral("#8e979f")
				     : (isRunning(target, runtime) ? QStringLiteral("#ff6c72")
								   : QStringLiteral("#e6e9ec"));
	return QStringLiteral("QPushButton#dskTargetPrimaryAction { color: %1; background: transparent; border: 0; padding: 0; }")
		.arg(color);
}

QString allControlStyle(bool stopMode)
{
	if (stopMode)
		return QStringLiteral("QPushButton#dskAllStreamsToggle { padding: 0; font-family: 'Bahnschrift SemiBold Condensed'; "
				      "font-size: 12px; font-weight: 800; letter-spacing: 1px; background: transparent; border: 0; }");
	return QStringLiteral("QPushButton#dskAllStreamsToggle { padding: 0 7px; font-family: 'Bahnschrift SemiBold Condensed'; "
			      "font-size: 12px; font-weight: 800; letter-spacing: 1px; background: transparent; border: 0; }");
}

QColor statusColor(const OutputTarget &target, const TargetRuntimeStatus &runtime)
{
	if (target.state == TargetState::Error || !target.lastError.isEmpty())
		return QColor(QStringLiteral("#ff646d"));
	if (runtimePlatformIsWarning(runtime.platform) || runtimeTransportIsBusy(runtime))
		return QColor(QStringLiteral("#e8bd59"));
	if (isRunning(target, runtime))
		return QColor(QStringLiteral("#58e58a"));
	if (!target.enabled)
		return QColor(QStringLiteral("#78828a"));
	return QColor(QStringLiteral("#2ed47a"));
}

QString platformFromService(const QString &serviceName, const QString &server, const QString &type)
{
	const QString probe = QString("%1 %2 %3").arg(serviceName, server, type).toLower();
	if (probe.contains("twitch"))
		return "twitch";
	if (probe.contains("youtube") || probe.contains("youtu.be") || probe.contains("google"))
		return "youtube";
	if (probe.contains("kick"))
		return "kick";
	if (probe.contains("tiktok"))
		return "tiktok";
	return "custom";
}

ObsNativeStreamInfo obsNativeStreamInfo()
{
	ObsNativeStreamInfo info;
	// The frontend API returns a borrowed service pointer. Do not release it.
	obs_service_t *service = obs_frontend_get_streaming_service();
	if (!service)
		return info;

	obs_data_t *settings = obs_service_get_settings(service);
	const auto setting = [settings](const char *name) {
		return settings ? QString::fromUtf8(obs_data_get_string(settings, name)).trimmed() : QString();
	};
	const QString serviceName = setting("service");
	const QString server = setting("server");
	const QString name = setting("name");
	const QString username = setting("username");
	const QString account = setting("account");
	const char *typeRaw = obs_service_get_type(service);
	const char *idRaw = obs_service_get_id(service);
	const QString type = typeRaw ? QString::fromUtf8(typeRaw).trimmed() : QString();
	const QString id = idRaw ? QString::fromUtf8(idRaw).trimmed() : QString();
	if (settings)
		obs_data_release(settings);

	info.available = obsNativeServiceConfigured(!serviceName.isEmpty(), !name.isEmpty(), !server.isEmpty(),
						 !type.isEmpty());
	info.serviceName = !serviceName.isEmpty() ? serviceName : (!name.isEmpty() ? name : QStringLiteral("OBS Native Stream"));
	info.platformId = platformFromService(info.serviceName, server, QStringLiteral("%1 %2").arg(type, id));
	info.accountLabel = !username.isEmpty() ? username : account;
	info.detail = info.accountLabel.isEmpty() ? QStringLiteral("OBS native stream")
						 : QString("OBS native stream - %1").arg(info.accountLabel);
	return info;
}

QString twitchDualFormatRowStatus(TwitchDualFormatState state)
{
	switch (state) {
	case TwitchDualFormatState::NotTwitch:
		return QStringLiteral("Ready");
	case TwitchDualFormatState::VerticalCanvasUnavailable:
		return QStringLiteral("DSK Vertical unavailable");
	case TwitchDualFormatState::EnhancedBroadcastingDisabled:
		return QStringLiteral("Enable Enhanced Broadcasting");
	case TwitchDualFormatState::VerticalCanvasNotSelected:
		return QStringLiteral("Select DSK Vertical canvas");
	case TwitchDualFormatState::Ready:
		return QStringLiteral("Dual Format ready");
	}
	return QStringLiteral("Ready");
}

} // namespace

StreamControlsDock::StreamControlsDock(OutputManager *manager, QWidget *parent)
	: QWidget(parent),
	  manager_(manager)
{
	logInfo("Stream Controls dock constructor begin");
	setObjectName(QStringLiteral("dskStreamingControlSurface"));
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	auto *scrollArea = new QScrollArea(this);
	scrollArea->setObjectName(QStringLiteral("dskStreamingRowsViewport"));
	scrollArea->setWidgetResizable(true);
	scrollArea->setFrameShape(QFrame::NoFrame);
	scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

	auto *rows = new QWidget(scrollArea);
	rows->setObjectName(QStringLiteral("dskStreamingRows"));

	buttons_ = new QVBoxLayout(rows);
	buttons_->setContentsMargins(5, 5, 5, 0);
	buttons_->setSpacing(5);
	buttons_->setAlignment(Qt::AlignTop);
	scrollArea->setWidget(rows);
	layout->addWidget(scrollArea, 1);

	allToggle_ = new ReferenceAllButton(this);
	allToggle_->setText(QStringLiteral("START ALL"));
	allToggle_->setObjectName(QStringLiteral("dskAllStreamsToggle"));
	allToggle_->setFixedSize(90, 28);
	allToggle_->setAccessibleName(QStringLiteral("Start all streams"));
	allToggle_->setStyleSheet(allControlStyle(false));
	connect(allToggle_, &QPushButton::clicked, this, &StreamControlsDock::handleAllToggle);

	setStyleSheet(QStringLiteral(
		"QWidget#dskStreamingControlSurface, QWidget#dskStreamingRows { background-color: #0f141d; }"
		"QScrollArea#dskStreamingRowsViewport { background-color: #0f141d; border: 0; }"
		"QScrollBar:vertical { width: 8px; background: #0c1114; margin: 0; }"
		"QScrollBar::handle:vertical { min-height: 28px; background: #34434b; border-radius: 4px; }"
		"QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"));

	if (manager_) {
		connect(manager_, &OutputManager::targetsChanged, this, &StreamControlsDock::scheduleRefresh,
			Qt::QueuedConnection);
		connect(manager_, &OutputManager::targetRuntimeChanged, this, &StreamControlsDock::scheduleRefresh,
			Qt::QueuedConnection);
		connect(manager_, &OutputManager::youtubeBroadcastSelectionRequired,
			this, &StreamControlsDock::handleYouTubeBroadcastSelection, Qt::QueuedConnection);
	}

	logInfo("Stream Controls dock initial refresh deferred until visible");
	QTimer::singleShot(2500, this, [this]() {
		obsNativeProbeReady_ = true;
		logInfo("Stream Controls dock OBS native probe enabled");
		scheduleRefresh();
	});
	logInfo("Stream Controls dock constructor complete");
}

QPushButton *StreamControlsDock::allToggleButton() const
{
	return allToggle_;
}

void StreamControlsDock::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	if (refreshGate_.takeIfVisible(true))
		refresh();
}

bool StreamControlsDock::isYouTubeBroadcastSessionCurrent(const YouTubeBroadcastSelectionRequest &request) const
{
	if (!manager_ || request.targetId.isEmpty() || request.sessionSerial == 0)
		return false;
	const TargetRuntimeStatus runtime = manager_->runtimeStatusForTarget(request.targetId);
	return runtime.sessionSerial == request.sessionSerial && runtimeTransportIsRunning(runtime);
}

bool StreamControlsDock::isYouTubeBroadcastRequestCurrent(const YouTubeBroadcastSelectionRequest &request) const
{
	if (!isYouTubeBroadcastSessionCurrent(request) || request.selectionGeneration == 0)
		return false;
	const TargetRuntimeStatus runtime = manager_->runtimeStatusForTarget(request.targetId);
	const bool selectionStillRequired =
		runtime.platform == PlatformLiveState::MultipleBroadcasts ||
		(runtime.platform == PlatformLiveState::Failed &&
		 runtime.lastTechnicalError.startsWith(
			 QStringLiteral("YouTube broadcast start blocked: selected broadcast unavailable.")));
	if (!selectionStillRequired)
		return false;
	const auto state = youtubeBroadcastLatestGenerations_.constFind(request.targetId);
	return state != youtubeBroadcastLatestGenerations_.constEnd() && state->sessionSerial == request.sessionSerial &&
	       state->selectionGeneration == request.selectionGeneration;
}

void StreamControlsDock::discardStaleYouTubeBroadcastSelections()
{
	for (auto state = youtubeBroadcastLatestGenerations_.begin(); state != youtubeBroadcastLatestGenerations_.end();) {
		YouTubeBroadcastSelectionRequest request;
		request.targetId = state.key();
		request.sessionSerial = state->sessionSerial;
		if (!isYouTubeBroadcastSessionCurrent(request))
			state = youtubeBroadcastLatestGenerations_.erase(state);
		else
			++state;
	}

	for (int i = pendingYouTubeBroadcastSelections_.size() - 1; i >= 0; --i) {
		if (!isYouTubeBroadcastRequestCurrent(pendingYouTubeBroadcastSelections_.at(i)))
			pendingYouTubeBroadcastSelections_.removeAt(i);
	}

	if (youtubeBroadcastDialog_ && !isYouTubeBroadcastRequestCurrent(activeYouTubeBroadcastSelection_))
		youtubeBroadcastDialog_->reject();
}

void StreamControlsDock::refreshYouTubeBroadcastDialog(const YouTubeBroadcastSelectionRequest &request)
{
	if (!youtubeBroadcastDialog_ || request.labels.isEmpty())
		return;
	const QString selected = youtubeBroadcastDialog_->textValue();
	activeYouTubeBroadcastSelection_ = request;
	youtubeBroadcastDialog_->setComboBoxItems(request.labels);
	youtubeBroadcastDialog_->setTextValue(request.labels.contains(selected) ? selected : request.labels.first());
	youtubeBroadcastDialog_->raise();
	youtubeBroadcastDialog_->activateWindow();
}

void StreamControlsDock::showNextYouTubeBroadcastSelection()
{
	if (youtubeBroadcastDialog_)
		return;

	while (!pendingYouTubeBroadcastSelections_.isEmpty()) {
		const YouTubeBroadcastSelectionRequest request = pendingYouTubeBroadcastSelections_.first();
		pendingYouTubeBroadcastSelections_.removeAt(0);
		if (!isYouTubeBroadcastRequestCurrent(request))
			continue;

		activeYouTubeBroadcastSelection_ = request;
		auto *dialog = new QInputDialog(this);
		dialog->setAttribute(Qt::WA_DeleteOnClose);
		dialog->setWindowTitle(QStringLiteral("YouTube配信枠を選択"));
		dialog->setLabelText(QStringLiteral("開始する配信枠を選んでください。予約枠は削除されません。"));
		dialog->setComboBoxItems(request.labels);
		dialog->setComboBoxEditable(false);
		dialog->setTextValue(request.labels.first());
		youtubeBroadcastDialog_ = dialog;

		connect(dialog, &QInputDialog::finished, this, [this, dialog](int result) {
			if (youtubeBroadcastDialog_ != dialog)
				return;

			const YouTubeBroadcastSelectionRequest request = activeYouTubeBroadcastSelection_;
			const QString selected = dialog->textValue();
			youtubeBroadcastDialog_.clear();
			activeYouTubeBroadcastSelection_ = {};

			if (result == QDialog::Accepted && isYouTubeBroadcastRequestCurrent(request)) {
				const int index = request.labels.indexOf(selected);
				if (index >= 0 && index < request.ids.size()) {
					manager_->applyYouTubeBroadcastSelection(request.targetId,
									 request.sessionSerial,
									 request.selectionGeneration,
									 request.ids.at(index));
				}
			} else if (result != QDialog::Accepted && isYouTubeBroadcastRequestCurrent(request)) {
				manager_->cancelYouTubeBroadcastSelection(request.targetId,
									  request.sessionSerial,
									  request.selectionGeneration);
			}

			QTimer::singleShot(0, this, [this]() {
				discardStaleYouTubeBroadcastSelections();
				showNextYouTubeBroadcastSelection();
			});
		});
		dialog->open();
		return;
	}

	activeYouTubeBroadcastSelection_ = {};
}

void StreamControlsDock::handleYouTubeBroadcastSelection(const QString &targetId, quint64 sessionSerial,
						  quint64 selectionGeneration, const QJsonArray &broadcasts)
{
	discardStaleYouTubeBroadcastSelections();
	if (!manager_ || targetId.trimmed().isEmpty() || sessionSerial == 0 || selectionGeneration == 0 ||
	    broadcasts.isEmpty()) {
		showNextYouTubeBroadcastSelection();
		return;
	}

	YouTubeBroadcastSelectionRequest request;
	request.targetId = targetId;
	request.sessionSerial = sessionSerial;
	request.selectionGeneration = selectionGeneration;

	for (const QJsonValue &value : broadcasts) {
		const QJsonObject broadcast = value.toObject();
		const QString id = broadcast.value(QStringLiteral("id")).toString().trimmed();
		if (id.isEmpty())
			continue;
		QString label = youtubeBroadcastChoiceLabel(broadcast);
		if (request.labels.contains(label))
			label += QStringLiteral(" [%1]").arg(id.right(8));
		request.labels.push_back(label);
		request.ids.push_back(id);
	}
	if (request.labels.isEmpty() || !isYouTubeBroadcastSessionCurrent(request)) {
		showNextYouTubeBroadcastSelection();
		return;
	}

	const auto known = youtubeBroadcastLatestGenerations_.constFind(targetId);
	if (known != youtubeBroadcastLatestGenerations_.constEnd() && known->sessionSerial == sessionSerial &&
	    selectionGeneration <= known->selectionGeneration) {
		showNextYouTubeBroadcastSelection();
		return;
	}
	youtubeBroadcastLatestGenerations_.insert(targetId, {sessionSerial, selectionGeneration});

	if (youtubeBroadcastDialog_ && activeYouTubeBroadcastSelection_.targetId == targetId &&
	    activeYouTubeBroadcastSelection_.sessionSerial == sessionSerial) {
		refreshYouTubeBroadcastDialog(request);
		return;
	}

	for (YouTubeBroadcastSelectionRequest &pending : pendingYouTubeBroadcastSelections_) {
		if (pending.targetId == targetId && pending.sessionSerial == sessionSerial) {
			pending = request;
			showNextYouTubeBroadcastSelection();
			return;
		}
	}

	pendingYouTubeBroadcastSelections_.push_back(request);
	showNextYouTubeBroadcastSelection();
}

void StreamControlsDock::scheduleRefresh()
{
	refreshGate_.markDirty();
	if (!isVisible())
		return;
	if (refreshScheduled_)
		return;

	refreshScheduled_ = true;
	QTimer::singleShot(0, this, [this]() {
		refreshScheduled_ = false;
		if (refreshGate_.takeIfVisible(isVisible()))
			refresh();
	});
}

void StreamControlsDock::refresh()
{
	discardStaleYouTubeBroadcastSelections();
	showNextYouTubeBroadcastSelection();
	if (refreshing_ || !buttons_ || !allToggle_)
		return;

	refreshing_ = true;

	int eligibleStartAllCount = 0;
	bool startAllBlockedByTransition = false;

	const QVector<OutputTarget> targets = manager_ ? manager_->targets() : QVector<OutputTarget>{};
	const bool obsNativeActive = obs_frontend_streaming_active();
	const ObsNativeStreamInfo nativeInfo = obsNativeProbeReady_ ? obsNativeStreamInfo() : ObsNativeStreamInfo{};
	const TwitchDualFormatState dualFormatState = manager_ ? manager_->twitchDualFormatState()
								     : TwitchDualFormatState::NotTwitch;
	const bool dualFormatActive = twitchDualFormatActive(dualFormatState);
	const bool hasObsNative =
		obsNativeRowAvailable(obsNativeProbeReady_, nativeInfo.available, obsNativeActive, obsNativeTransitioning_);
	removeStaleTargetRows(targets);
	if (!hasObsNative)
		removeRow(obsNativeRow_);
	if (targets.isEmpty() && !hasObsNative) {
		setEmptyStateVisible(true);
		allToggleStops_ = false;
		allToggle_->setText(QStringLiteral("START ALL"));
		allToggle_->setAccessibleName(QStringLiteral("Start all streams"));
		allToggle_->setStyleSheet(allControlStyle(false));
		allToggle_->setEnabled(false);
		refreshing_ = false;
		return;
	}
	setEmptyStateVisible(false);

	int rowIndex = 0;
	if (hasObsNative) {
		if (!obsNativeRow_.row)
			obsNativeRow_ = createObsNativeRow();
		updateObsNativeRow(obsNativeRow_, nativeInfo.platformId, nativeInfo.serviceName,
				   twitchDualFormatRowStatus(dualFormatState), nativeInfo.detail);
		placeRow(obsNativeRow_.row, rowIndex++);
	}

	QHash<QString, TargetRuntimeStatus> runtimes;
	runtimes.reserve(targets.size());
	for (const auto &target : targets) {
		const TargetRuntimeStatus runtime = manager_->runtimeStatusForTarget(target.id);
		runtimes.insert(target.id, runtime);
		if (targetCanStartWithAll(target, runtime, dualFormatActive))
			++eligibleStartAllCount;
		if (targetBlocksStartAll(target, runtime, dualFormatActive))
			startAllBlockedByTransition = true;
		auto row = targetRows_.find(target.id);
		if (row == targetRows_.end())
			row = targetRows_.insert(target.id, createTargetRow(target));
		updateTargetRow(target, runtime, dualFormatActive, row.value());
		placeRow(row->row, rowIndex++);
	}

	bool hasRunningTarget = false;
	bool hasStoppingTarget = false;
	if (manager_) {
		for (const auto &target : targets) {
			const TargetRuntimeStatus runtime = runtimes.value(target.id);
			if (isRunning(target, runtime))
				hasRunningTarget = true;
			if (target.state == TargetState::Stopping || runtime.transport == TransportState::Stopping)
				hasStoppingTarget = true;
		}
	}
	const bool startObsNative =
		obsNativeCanStartWithAll(nativeInfo.available, obsNativeActive, obsNativeTransitioning_);
	const AllControlState state = allControlState(eligibleStartAllCount, startAllBlockedByTransition,
						      startObsNative, obsNativeActive, obsNativeTransitioning_,
						      obsNativeExpectedActive_, hasRunningTarget, hasStoppingTarget);
	allToggleStops_ = state.stopMode;
	allToggle_->setText(state.stopMode ? QStringLiteral("STOP ALL") : QStringLiteral("START ALL"));
	allToggle_->setAccessibleName(state.stopMode ? QStringLiteral("Stop all streams")
						      : QStringLiteral("Start all streams"));
	allToggle_->setStyleSheet(allControlStyle(state.stopMode));
	allToggle_->setEnabled(state.enabled);
	allToggle_->setToolTip(state.stopMode
				       ? QStringLiteral("Stop OBS native streaming and all running DSK targets.")
				       : QStringLiteral("Start OBS native streaming and %1 included DSK target(s).")
						 .arg(eligibleStartAllCount));
	refreshing_ = false;
}

void StreamControlsDock::handleObsNativeStreamingStateChanged(bool active)
{
	const bool expectedStateReached = !obsNativeTransitioning_ || active == obsNativeExpectedActive_;
	if (obsNativeTransitioning_ && !expectedStateReached && manager_) {
		if (obsNativeExpectedActive_)
			manager_->clearNextObsAutoStartSuppression();
		else
			manager_->clearNextObsAutoStopSuppression();
	}
	obsNativeExpectedActive_ = active;
	if (obsNativeTransitioning_) {
		obsNativeTransitioning_ = false;
		++obsNativeTransitionGeneration_;
	}
	scheduleRefresh();
}

void StreamControlsDock::beginObsNativeTransition(bool expectedActive)
{
	obsNativeExpectedActive_ = expectedActive;
	obsNativeTransitioning_ = true;
	const int generation = ++obsNativeTransitionGeneration_;
	scheduleRefresh();

	QTimer::singleShot(15000, this, [this, generation]() {
		if (!obsNativeTransitioning_ || obsNativeTransitionGeneration_ != generation)
			return;
		if (manager_) {
			if (obsNativeExpectedActive_)
				manager_->clearNextObsAutoStartSuppression();
			else
				manager_->clearNextObsAutoStopSuppression();
		}
		obsNativeTransitioning_ = false;
		scheduleRefresh();
	});
}

void StreamControlsDock::handleObsNativeClicked()
{
	if (obsNativeTransitioning_)
		return;

	if (obs_frontend_streaming_active()) {
		beginObsNativeTransition(false);
		if (manager_)
			manager_->suppressNextObsAutoStop();
		obs_frontend_streaming_stop();
	} else {
		beginObsNativeTransition(true);
		if (manager_)
			manager_->suppressNextObsAutoStart();
		obs_frontend_streaming_start();
	}
}

void StreamControlsDock::handleButtonClicked()
{
	auto *button = qobject_cast<QPushButton *>(sender());
	if (!button || !manager_)
		return;

	const QString id = button->property("targetId").toString();
	if (id.isEmpty())
		return;

	for (const auto &target : manager_->targets()) {
		if (target.id != id)
			continue;

		const TargetRuntimeStatus runtime = manager_->runtimeStatusForTarget(id);
		if (target.state == TargetState::Stopping || runtime.transport == TransportState::Stopping)
			return;

		if (isRunning(target, runtime)) {
			manager_->stopTarget(id);
		} else {
			if (shouldBlockIndependentTwitchStart(
				    target, twitchDualFormatActive(manager_->twitchDualFormatState()), false))
				return;
			requestStartTargets({id});
		}
		return;
	}
}

void StreamControlsDock::handleAllToggle()
{
	if (allToggleStops_)
		handleStopAll();
	else
		handleStartEnabled();
}

void StreamControlsDock::handleStartEnabled()
{
	QVector<QString> ids;
	bool startAllBlockedByTransition = false;
	const QVector<OutputTarget> targets = manager_ ? manager_->targets() : QVector<OutputTarget>{};
	const bool dualFormatActive = manager_ && twitchDualFormatActive(manager_->twitchDualFormatState());
	for (const OutputTarget &target : targets) {
		const TargetRuntimeStatus runtime = manager_->runtimeStatusForTarget(target.id);
		if (targetCanStartWithAll(target, runtime, dualFormatActive))
			ids.push_back(target.id);
		if (targetBlocksStartAll(target, runtime, dualFormatActive))
			startAllBlockedByTransition = true;
	}

	const bool hasObsNative = obsNativeProbeReady_ && obsNativeStreamInfo().available;
	const bool startObsNative =
		obsNativeCanStartWithAll(hasObsNative, obs_frontend_streaming_active(), obsNativeTransitioning_);
	if (startAllBlockedByTransition || (ids.isEmpty() && !startObsNative))
		return;

	requestStartTargets(ids);

	if (startObsNative) {
		beginObsNativeTransition(true);
		if (manager_)
			manager_->suppressNextObsAutoStart();
		obs_frontend_streaming_start();
	}
}

void StreamControlsDock::handleStopAll()
{
	if (allToggle_)
		allToggle_->setEnabled(false);
	if (manager_)
		manager_->stopAll();
	const bool obsStopAlreadyPending = obsNativeTransitioning_ && !obsNativeExpectedActive_;
	if (obs_frontend_streaming_active() && !obsStopAlreadyPending) {
		beginObsNativeTransition(false);
		if (manager_)
			manager_->suppressNextObsAutoStop();
		obs_frontend_streaming_stop();
	}
}

void StreamControlsDock::placeRow(QWidget *row, int index)
{
	if (!buttons_ || !row)
		return;
	if (buttons_->indexOf(row) != index) {
		buttons_->removeWidget(row);
		buttons_->insertWidget(index, row);
	}
	row->show();
}

void StreamControlsDock::removeRow(RowWidgets &widgets)
{
	if (!widgets.row)
		return;
	if (buttons_)
		buttons_->removeWidget(widgets.row);
	if (widgets.button) {
		widgets.button->setEnabled(false);
		widgets.button->setProperty("targetId", QVariant());
		QObject::disconnect(widgets.button, nullptr, this, nullptr);
	}
	widgets.row->hide();
	widgets.row->deleteLater();
	widgets = {};
}

void StreamControlsDock::removeStaleTargetRows(const QVector<OutputTarget> &targets)
{
	QSet<QString> currentIds;
	currentIds.reserve(targets.size());
	for (const auto &target : targets)
		currentIds.insert(target.id);

	for (auto row = targetRows_.begin(); row != targetRows_.end();) {
		if (currentIds.contains(row.key())) {
			++row;
			continue;
		}
		removeRow(row.value());
		row = targetRows_.erase(row);
	}

}

void StreamControlsDock::setEmptyStateVisible(bool visible)
{
	if (!visible) {
		if (emptyState_) {
			buttons_->removeWidget(emptyState_);
			emptyState_->hide();
		}
		return;
	}
	if (!emptyState_) {
		emptyState_ = new QLabel("No stream targets configured.", this);
		emptyState_->setAlignment(Qt::AlignCenter);
		emptyState_->setMinimumHeight(48);
	}
	placeRow(emptyState_, 0);
}

void StreamControlsDock::requestStartTargets(const QVector<QString> &ids)
{
	if (!manager_)
		return;

	int started = 0;
	int failed = 0;
	for (const auto &id : ids) {
		if (manager_->startTarget(id))
			++started;
		else
			++failed;
	}

	if (failed > 0)
		logWarning(QStringLiteral("Start All completed with %1 started and %2 failed target(s).").arg(started).arg(failed));
}

StreamControlsDock::RowWidgets StreamControlsDock::createVisualRow(const QString &platformId, const QString &displayName)
{
	auto *row = new QWidget(this);
	row->setObjectName(QStringLiteral("dskTargetRow"));
	row->setFixedHeight(50);
	row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	row->setStyleSheet(QStringLiteral(
		"QWidget#dskTargetRow { background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
		"stop:0 #1a202b, stop:0.5 #141a24, stop:1 #1b212c); border: 1px solid #2d3541; "
		"border-radius: 9px; }"));

	auto *layout = new QGridLayout(row);
	layout->setContentsMargins(10, 4, 10, 4);
	layout->setHorizontalSpacing(8);
	layout->setVerticalSpacing(0);
	layout->setColumnStretch(2, 1);

	auto *badge = new PlatformBadge(platformId, displayName, row);
	layout->addWidget(badge, 0, 1, 2, 1, Qt::AlignCenter);

	auto *name = new QLabel(displayName.isEmpty() ? QStringLiteral("Untitled") : displayName, row);
	name->setObjectName(QStringLiteral("dskTargetName"));
	name->setMinimumWidth(70);
	name->setStyleSheet(QStringLiteral(
		"QLabel#dskTargetName { color: #f1f3f5; font-family: 'Bahnschrift SemiBold Condensed'; "
		"font-size: 15px; font-weight: 650; letter-spacing: 0.5px; border: 0; background: transparent; }"));
	auto *nameShadow = new QGraphicsDropShadowEffect(name);
	nameShadow->setBlurRadius(1.2);
	nameShadow->setOffset(1, 1);
	nameShadow->setColor(QColor(0, 0, 0, 220));
	name->setGraphicsEffect(nameShadow);
	layout->addWidget(name, 0, 2, Qt::AlignLeft | Qt::AlignBottom);

	auto *statusLine = new QWidget(row);
	statusLine->setObjectName(QStringLiteral("dskTargetStatusLine"));
	statusLine->setStyleSheet(QStringLiteral("QWidget#dskTargetStatusLine { background: transparent; border: 0; }"));
	auto *statusLayout = new QHBoxLayout(statusLine);
	statusLayout->setContentsMargins(0, 0, 0, 0);
	statusLayout->setSpacing(5);
	auto *statusLight = new StatusLight(statusLine);
	auto *details = new QLabel(row);
	details->setObjectName(QStringLiteral("dskTargetState"));
	details->setTextInteractionFlags(Qt::TextSelectableByMouse);
	details->setStyleSheet(QStringLiteral(
		"QLabel#dskTargetState { color: #aeb7c1; font-family: 'Bahnschrift Light Condensed'; "
		"font-size: 12px; font-weight: 350; letter-spacing: 0.5px; border: 0; background: transparent; }"));
	statusLayout->addWidget(statusLight);
	statusLayout->addWidget(details, 1);
	layout->addWidget(statusLine, 1, 2, Qt::AlignLeft | Qt::AlignTop);

	auto *button = new ReferenceActionButton(row);
	button->setObjectName(QStringLiteral("dskTargetPrimaryAction"));
	button->setFixedSize(42, 30);

	auto *actionMenu = new ReferenceMenuButton(row);
	actionMenu->setObjectName(QStringLiteral("dskTargetActionMenu"));
	actionMenu->setPopupMode(QToolButton::InstantPopup);
	actionMenu->setFixedSize(32, 30);
	actionMenu->setStyleSheet(QStringLiteral(
		"QToolButton#dskTargetActionMenu { background: transparent; border: 0; padding: 0; }"
		"QToolButton#dskTargetActionMenu::menu-indicator { image: none; width: 0; height: 0; }"
		"QToolButton#dskTargetActionMenu:hover { background: transparent; }"));

	auto *actionBar = new QWidget(row);
	auto *actionLayout = new QHBoxLayout(actionBar);
	actionLayout->setContentsMargins(0, 0, 0, 0);
	actionLayout->setSpacing(6);
	actionLayout->addWidget(actionMenu);
	actionLayout->addWidget(button);
	layout->addWidget(actionBar, 0, 3, 2, 1, Qt::AlignRight | Qt::AlignCenter);

	return {row, badge, name, details, statusLight, button, actionMenu};
}

StreamControlsDock::RowWidgets StreamControlsDock::createTargetRow(const OutputTarget &target)
{
	RowWidgets widgets = createVisualRow(target.platformId, target.name);
	widgets.button->setProperty("targetId", target.id);
	connect(widgets.button, &QPushButton::clicked, this, &StreamControlsDock::handleButtonClicked);

	auto *actionMenu = new QMenu(widgets.actionMenu);
	auto *toggleAction = actionMenu->addAction(QStringLiteral("Start or stop this target"));
	connect(toggleAction, &QAction::triggered, widgets.button, &QPushButton::click);
	auto *actionEdit = actionMenu->addAction(QStringLiteral("Edit target"));
	connect(actionEdit, &QAction::triggered, this, [this, id = target.id]() { emit editTargetRequested(id); });
	widgets.actionMenu->setMenu(actionMenu);

	return widgets;
}

void StreamControlsDock::updateTargetRow(const OutputTarget &target, const TargetRuntimeStatus &runtime,
					 bool suppressIndependentTwitch, RowWidgets &widgets)
{
	if (!widgets.row)
		return;
	const bool running = isRunning(target, runtime);
	const bool startSuppressed = shouldBlockIndependentTwitchStart(target, suppressIndependentTwitch, running);
	static_cast<PlatformBadge *>(widgets.badge)->setIdentity(target.platformId, target.name);
	widgets.name->setText(target.name.isEmpty() ? QStringLiteral("Untitled") : target.name);
	widgets.details->setText(rowDetailText(target, runtime, startSuppressed));
	const bool liveWarning = isYouTubeTarget(target) && runtimePlatformIsWarning(runtime.platform) &&
				 runtimeTransportIsRunning(runtime);
	const QString detailStyle = liveWarning
		? QStringLiteral("QLabel#dskTargetState { color: #e8bd59; font-family: 'Bahnschrift Light Condensed'; font-size: 12px; letter-spacing: 0.5px; border: 0; background: transparent; }")
		: (isRunning(target, runtime)
			   ? QStringLiteral("QLabel#dskTargetState { color: #b8c4cc; font-family: 'Bahnschrift Light Condensed'; font-size: 12px; letter-spacing: 0.5px; border: 0; background: transparent; }")
			   : QStringLiteral("QLabel#dskTargetState { color: #aeb7c1; font-family: 'Bahnschrift Light Condensed'; font-size: 12px; letter-spacing: 0.5px; border: 0; background: transparent; }"));
	if (widgets.details->styleSheet() != detailStyle)
		widgets.details->setStyleSheet(detailStyle);
	if (widgets.statusLight)
		static_cast<StatusLight *>(widgets.statusLight)
			->setColor(startSuppressed ? QColor(QStringLiteral("#66717a")) : statusColor(target, runtime));
	widgets.button->setProperty("targetId", target.id);
	const QString action = actionText(target, runtime);
	widgets.button->setText(action.toUpper());
	widgets.button->setAccessibleName(QString("%1 %2").arg(action, target.name));
	widgets.button->setToolTip(startSuppressed
					  ? QStringLiteral("Use the OBS Twitch row for Dual Format streaming.")
					  : (running ? QString("Stop only %1.").arg(target.name)
									: QString("Start only %1.").arg(target.name)));
	if (widgets.actionMenu) {
		widgets.actionMenu->setAccessibleName(QString("More stream actions for %1").arg(target.name));
		widgets.actionMenu->setToolTip(QString("More stream actions for %1.").arg(target.name));
	}
	widgets.button->setEnabled(!startSuppressed && !isBusy(target, runtime));
	const QString style = buttonStyle(target, runtime, startSuppressed);
	if (widgets.button->styleSheet() != style)
		widgets.button->setStyleSheet(style);
}

StreamControlsDock::RowWidgets StreamControlsDock::createObsNativeRow()
{
	RowWidgets widgets = createVisualRow(QStringLiteral("custom"), QStringLiteral("OBS Native Stream"));
	connect(widgets.button, &QPushButton::clicked, this, &StreamControlsDock::handleObsNativeClicked);
	auto *actionMenu = new QMenu(widgets.actionMenu);
	auto *toggleAction = actionMenu->addAction(QStringLiteral("Start or stop OBS native stream"));
	connect(toggleAction, &QAction::triggered, widgets.button, &QPushButton::click);
	widgets.actionMenu->setMenu(actionMenu);
	auto *notice = actionMenu->addAction(QStringLiteral("Managed by OBS stream settings"));
	notice->setEnabled(false);
	return widgets;
}

void StreamControlsDock::updateObsNativeRow(RowWidgets &widgets, const QString &platformId,
					    const QString &serviceName, const QString &statusText,
					    const QString &detail)
{
	if (!widgets.row)
		return;
	const bool active = obs_frontend_streaming_active();
	static_cast<PlatformBadge *>(widgets.badge)->setIdentity(platformId, serviceName);
	widgets.name->setText(serviceName);
	widgets.details->setText(obsNativeTransitioning_
				 ? (obsNativeExpectedActive_ ? QStringLiteral("Starting OBS native stream")
							     : QStringLiteral("Stopping OBS native stream"))
				 : (active ? (statusText == QStringLiteral("Dual Format ready")
					      ? QStringLiteral("Live - Twitch Dual Format")
					      : QStringLiteral("Live - OBS native stream"))
					   : statusText));
	widgets.details->setToolTip(detail);
	const QString action = obsNativeTransitioning_ ? (obsNativeExpectedActive_ ? QStringLiteral("Starting")
									     : QStringLiteral("Stopping"))
						      : (active ? QStringLiteral("Stop") : QStringLiteral("Start"));
	widgets.button->setText(action.toUpper());
	widgets.button->setAccessibleName(QString("%1 OBS native streaming").arg(action));
	widgets.button->setToolTip(obsNativeTransitioning_
				   ? QStringLiteral("OBS native streaming is changing state.")
				   : (active ? QStringLiteral("Stop OBS native streaming.")
					     : QStringLiteral("Start OBS native streaming with the OBS account/settings.")));
	widgets.button->setEnabled(!obsNativeTransitioning_);
	if (widgets.statusLight) {
		const QColor color = obsNativeTransitioning_ ? QColor(QStringLiteral("#e8bd59"))
							      : (active ? QColor(QStringLiteral("#58e58a"))
									: QColor(QStringLiteral("#2ed47a")));
		static_cast<StatusLight *>(widgets.statusLight)->setColor(color);
	}
	const QString nativeColor = obsNativeTransitioning_ ? QStringLiteral("#8e979f")
						     : (active ? QStringLiteral("#ff6c72") : QStringLiteral("#e6e9ec"));
	const QString style = QStringLiteral(
		"QPushButton#dskTargetPrimaryAction { color: %1; background: transparent; border: 0; "
		"font-family: 'Bahnschrift SemiBold Condensed'; font-size: 12px; font-weight: 700; }"
		"QPushButton#dskTargetPrimaryAction:hover { color: white; }")
		.arg(nativeColor);
	if (widgets.button->styleSheet() != style)
		widgets.button->setStyleSheet(style);
}

} // namespace dsk
