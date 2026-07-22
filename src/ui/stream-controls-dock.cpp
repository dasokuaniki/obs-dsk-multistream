#include "ui/stream-controls-dock.hpp"

#include "ui/stream-controls-state.hpp"

#include "core/diagnostics.hpp"
#include "core/oauth-provider.hpp"
#include "core/youtube-api-warning.hpp"

#include <obs.h>
#include <obs-frontend-api.h>
#include <util/bmem.h>

#include <QDateTime>
#include <QFile>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSet>
#include <QShowEvent>
#include <QTimer>
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

class PlatformBadge : public QWidget {
public:
	explicit PlatformBadge(QString platformId, QString fallbackText, QWidget *parent = nullptr)
		: QWidget(parent),
		  platformId_(platformId.toLower()),
		  fallbackText_(fallbackText.trimmed().isEmpty() ? QStringLiteral("?") : fallbackText.trimmed().left(2).toUpper())
	{
		setFixedSize(30, 30);
	}

	void setIdentity(const QString &platformId, const QString &fallbackText)
	{
		const QString nextPlatformId = platformId.toLower();
		const QString nextFallback = fallbackText.trimmed().isEmpty()
					     ? QStringLiteral("?")
					     : fallbackText.trimmed().left(2).toUpper();
		if (platformId_ == nextPlatformId && fallbackText_ == nextFallback)
			return;
		platformId_ = nextPlatformId;
		fallbackText_ = nextFallback;
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing, true);

		const QRectF r = rect().adjusted(1, 1, -1, -1);
		if (platformId_ == "youtube") {
			p.setPen(Qt::NoPen);
			p.setBrush(QColor("#ff0033"));
			p.drawRoundedRect(r, 4, 4);
			QPainterPath play;
			play.moveTo(12, 9);
			play.lineTo(12, 21);
			play.lineTo(22, 15);
			play.closeSubpath();
			p.setBrush(Qt::white);
			p.drawPath(play);
			return;
		}

		if (platformId_ == "kick") {
			p.setPen(Qt::NoPen);
			p.setBrush(QColor("#53fc18"));
			p.drawRoundedRect(r, 4, 4);
			p.setPen(QColor("#111318"));
			QFont f = p.font();
			f.setBold(true);
			f.setPixelSize(20);
			p.setFont(f);
			p.drawText(rect(), Qt::AlignCenter, "K");
			return;
		}

		if (platformId_ == "tiktok") {
			p.setPen(Qt::NoPen);
			p.setBrush(QColor("#111318"));
			p.drawRoundedRect(r, 4, 4);
			QPen cyan(QColor("#25f4ee"), 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
			QPen red(QColor("#fe2c55"), 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
			QPen white(Qt::white, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
			auto drawNote = [&](const QPen &pen, qreal dx, qreal dy) {
				p.setPen(pen);
				p.drawLine(QPointF(18 + dx, 7 + dy), QPointF(18 + dx, 20 + dy));
				p.drawLine(QPointF(18 + dx, 7 + dy), QPointF(24 + dx, 11 + dy));
				p.drawEllipse(QPointF(13 + dx, 21 + dy), 4, 4);
			};
			drawNote(cyan, -2, 1);
			drawNote(red, 2, -1);
			drawNote(white, 0, 0);
			return;
		}

		if (platformId_ == "twitch") {
			p.setPen(Qt::NoPen);
			p.setBrush(QColor("#9146ff"));
			p.drawRoundedRect(r, 4, 4);
			QPainterPath bubble;
			bubble.moveTo(7, 6);
			bubble.lineTo(24, 6);
			bubble.lineTo(24, 19);
			bubble.lineTo(19, 24);
			bubble.lineTo(15, 24);
			bubble.lineTo(15, 21);
			bubble.lineTo(7, 21);
			bubble.closeSubpath();
			p.setBrush(Qt::white);
			p.drawPath(bubble);
			p.setBrush(QColor("#9146ff"));
			p.drawRect(QRectF(12, 10, 2.5, 7));
			p.drawRect(QRectF(18, 10, 2.5, 7));
			return;
		}

		p.setPen(Qt::NoPen);
		p.setBrush(QColor("#7a8494"));
		p.drawRoundedRect(r, 4, 4);
		p.setPen(QColor("#111318"));
		QFont f = p.font();
		f.setBold(true);
		f.setPixelSize(12);
		p.setFont(f);
		p.drawText(rect(), Qt::AlignCenter, fallbackText_);
	}

private:
	QString platformId_;
	QString fallbackText_;
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

QString rowDetailText(const OutputTarget &target, const TargetRuntimeStatus &runtime)
{
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

QString buttonStyle(const OutputTarget &target, const TargetRuntimeStatus &runtime)
{
	if (target.state == TargetState::Stopping || runtime.transport == TransportState::Stopping)
		return "QPushButton { min-height: 24px; padding: 2px 6px; font-weight: 700; color: #d6d8dc; "
		       "background-color: #4a4d54; border: 1px solid #5b6068; border-radius: 3px; }";
	if (runtime.transport == TransportState::Reconnecting)
		return "QPushButton { min-height: 24px; padding: 2px 6px; font-weight: 800; color: #f4eefc; "
		       "background-color: #5b3a82; border: 1px solid #7a57a6; border-radius: 3px; }";
	if (isRunning(target, runtime))
		return "QPushButton { min-height: 24px; padding: 2px 6px; font-weight: 800; color: white; "
		       "background-color: #8b3232; border: 1px solid #aa4444; border-radius: 3px; }"
		       "QPushButton:hover { background-color: #9b3838; }";
	return "QPushButton { min-height: 24px; padding: 2px 6px; font-weight: 800; color: white; "
	       "background-color: #26763d; border: 1px solid #3f9458; border-radius: 3px; }"
	       "QPushButton:hover { background-color: #2d8547; }";
}

QString allControlStyle(bool stopMode)
{
	if (stopMode)
		return QStringLiteral("QPushButton { padding: 4px 8px; font-weight: 800; color: #f7f0f0; "
				      "background-color: #673437; border: 1px solid #87494d; border-radius: 3px; }"
				      "QPushButton:hover { background-color: #783d42; }"
				      "QPushButton:disabled { color: #85898f; background-color: #3d4046; "
				      "border-color: #5b6068; }");
	return QStringLiteral("QPushButton { padding: 4px 8px; font-weight: 800; color: #f3f7f4; "
			      "background-color: #2b6f3f; border: 1px solid #438a56; border-radius: 3px; }"
			      "QPushButton:hover { background-color: #327e49; }"
			      "QPushButton:disabled { color: #85898f; background-color: #3d4046; "
			      "border-color: #5b6068; }");
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
	char *profilePathRaw = obs_frontend_get_current_profile_path();
	if (!profilePathRaw)
		return info;

	const QString servicePath = QString::fromUtf8(profilePathRaw) + QStringLiteral("/service.json");
	bfree(profilePathRaw);

	QFile file(servicePath);
	if (!file.open(QIODevice::ReadOnly))
		return info;

	const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
	if (!document.isObject())
		return info;

	const QJsonObject root = document.object();
	const QJsonObject settings = root.value("settings").toObject();
	const QString type = root.value("type").toString();
	const QString serviceName = settings.value("service").toString().trimmed();
	const QString server = settings.value("server").toString().trimmed();
	const QString name = settings.value("name").toString().trimmed();
	const QString username = settings.value("username").toString().trimmed();
	const QString account = settings.value("account").toString().trimmed();

	info.available = obsNativeServiceConfigured(!serviceName.isEmpty(), !name.isEmpty(), !server.isEmpty(),
						 !type.trimmed().isEmpty());
	info.serviceName = !serviceName.isEmpty() ? serviceName : (!name.isEmpty() ? name : QStringLiteral("OBS Native Stream"));
	info.platformId = platformFromService(info.serviceName, server, type);
	info.accountLabel = !username.isEmpty() ? username : account;
	info.detail = info.accountLabel.isEmpty() ? QStringLiteral("OBS native stream")
						 : QString("OBS native stream - %1").arg(info.accountLabel);
	return info;
}

} // namespace

StreamControlsDock::StreamControlsDock(OutputManager *manager, QWidget *parent)
	: QWidget(parent),
	  manager_(manager)
{
	logInfo("Stream Controls dock constructor begin");
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(4, 2, 4, 4);
	layout->setSpacing(5);

	buttons_ = new QVBoxLayout();
	buttons_->setContentsMargins(0, 0, 0, 0);
	buttons_->setSpacing(4);
	layout->addLayout(buttons_);

	auto *footer = new QHBoxLayout();
	footer->setContentsMargins(0, 0, 0, 0);
	footer->setSpacing(5);
	allToggle_ = new QPushButton("Start All", this);
	allToggle_->setMinimumHeight(26);
	allToggle_->setStyleSheet(allControlStyle(false));
	connect(allToggle_, &QPushButton::clicked, this, &StreamControlsDock::handleAllToggle);
	footer->addWidget(allToggle_);
	layout->addLayout(footer);
	layout->addStretch(1);

	setStyleSheet("StreamControlsDock { background-color: #1f1f1f; }");

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
	const bool hasObsNative =
		obsNativeRowAvailable(obsNativeProbeReady_, nativeInfo.available, obsNativeActive, obsNativeTransitioning_);
	removeStaleTargetRows(targets);
	if (!hasObsNative)
		removeRow(obsNativeRow_);
	if (targets.isEmpty() && !hasObsNative) {
		setEmptyStateVisible(true);
		allToggleStops_ = false;
		allToggle_->setText(QStringLiteral("Start All"));
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
		updateObsNativeRow(obsNativeRow_, nativeInfo.platformId, nativeInfo.serviceName, nativeInfo.detail);
		placeRow(obsNativeRow_.row, rowIndex++);
	}

	QHash<QString, TargetRuntimeStatus> runtimes;
	runtimes.reserve(targets.size());
	for (const auto &target : targets) {
		const TargetRuntimeStatus runtime = manager_->runtimeStatusForTarget(target.id);
		runtimes.insert(target.id, runtime);
		if (targetCanStartWithAll(target, runtime))
			++eligibleStartAllCount;
		if (targetBlocksStartAll(target, runtime))
			startAllBlockedByTransition = true;
		auto row = targetRows_.find(target.id);
		if (row == targetRows_.end())
			row = targetRows_.insert(target.id, createTargetRow(target));
		updateTargetRow(target, runtime, row.value());
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
	allToggle_->setText(state.stopMode ? QStringLiteral("Stop All") : QStringLiteral("Start All"));
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
	for (const OutputTarget &target : targets) {
		const TargetRuntimeStatus runtime = manager_->runtimeStatusForTarget(target.id);
		if (targetCanStartWithAll(target, runtime))
			ids.push_back(target.id);
		if (targetBlocksStartAll(target, runtime))
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

StreamControlsDock::RowWidgets StreamControlsDock::createTargetRow(const OutputTarget &target)
{
	auto *row = new QWidget(this);
	row->setObjectName("targetRow");
	row->setStyleSheet("QWidget#targetRow { background-color: #292929; border: 1px solid #3a3a3a; border-radius: 4px; }");
	auto *layout = new QHBoxLayout(row);
	layout->setContentsMargins(6, 5, 6, 5);
	layout->setSpacing(6);

	auto *badge = new PlatformBadge(target.platformId, target.name, row);

	auto *textColumn = new QVBoxLayout();
	textColumn->setContentsMargins(0, 0, 0, 0);
	textColumn->setSpacing(0);
	auto *name = new QLabel(target.name.isEmpty() ? QStringLiteral("Untitled") : target.name, row);
	name->setStyleSheet("QLabel { color: #ececec; font-size: 12px; font-weight: 800; border: 0; background: transparent; }");
	auto *details = new QLabel(row);
	details->setTextInteractionFlags(Qt::TextSelectableByMouse);
	details->setMaximumHeight(14);
	textColumn->addWidget(name);
	textColumn->addWidget(details);

	auto *button = new QPushButton(row);
	button->setFixedWidth(64);
	button->setProperty("targetId", target.id);
	connect(button, &QPushButton::clicked, this, &StreamControlsDock::handleButtonClicked);

	layout->addWidget(badge);
	layout->addLayout(textColumn, 1);
	layout->addWidget(button);
	return {row, badge, name, details, button};
}

void StreamControlsDock::updateTargetRow(const OutputTarget &target, const TargetRuntimeStatus &runtime,
					 RowWidgets &widgets)
{
	if (!widgets.row)
		return;
	static_cast<PlatformBadge *>(widgets.badge)->setIdentity(target.platformId, target.name);
	widgets.name->setText(target.name.isEmpty() ? QStringLiteral("Untitled") : target.name);
	widgets.details->setText(rowDetailText(target, runtime));
	const bool liveWarning = isYouTubeTarget(target) && runtimePlatformIsWarning(runtime.platform) &&
				 runtimeTransportIsRunning(runtime);
	const QString detailStyle = liveWarning
		? QStringLiteral("QLabel { color: #d0a84f; font-size: 10px; border: 0; background: transparent; }")
		: QStringLiteral("QLabel { color: #a4a4a4; font-size: 10px; border: 0; background: transparent; }");
	if (widgets.details->styleSheet() != detailStyle)
		widgets.details->setStyleSheet(detailStyle);
	widgets.button->setProperty("targetId", target.id);
	widgets.button->setText(actionText(target, runtime));
	widgets.button->setToolTip(isRunning(target, runtime) ? QString("Stop only %1.").arg(target.name)
							      : QString("Start only %1.").arg(target.name));
	widgets.button->setEnabled(!isBusy(target, runtime));
	const QString style = buttonStyle(target, runtime);
	if (widgets.button->styleSheet() != style)
		widgets.button->setStyleSheet(style);
}

StreamControlsDock::RowWidgets StreamControlsDock::createObsNativeRow()
{
	auto *row = new QWidget(this);
	row->setObjectName("targetRow");
	row->setStyleSheet("QWidget#targetRow { background-color: #24262a; border: 1px solid #464b53; border-radius: 4px; }");
	auto *layout = new QHBoxLayout(row);
	layout->setContentsMargins(6, 5, 6, 5);
	layout->setSpacing(6);

	auto *badge = new PlatformBadge(QStringLiteral("custom"), QStringLiteral("OBS"), row);

	auto *textColumn = new QVBoxLayout();
	textColumn->setContentsMargins(0, 0, 0, 0);
	textColumn->setSpacing(0);
	auto *name = new QLabel(row);
	name->setStyleSheet("QLabel { color: #f0f0f0; font-size: 12px; font-weight: 800; border: 0; background: transparent; }");
	auto *details = new QLabel(row);
	details->setTextInteractionFlags(Qt::TextSelectableByMouse);
	details->setStyleSheet("QLabel { color: #aeb4bd; font-size: 10px; border: 0; background: transparent; }");
	details->setMaximumHeight(14);
	textColumn->addWidget(name);
	textColumn->addWidget(details);

	auto *button = new QPushButton(row);
	button->setFixedWidth(64);
	connect(button, &QPushButton::clicked, this, &StreamControlsDock::handleObsNativeClicked);

	layout->addWidget(badge);
	layout->addLayout(textColumn, 1);
	layout->addWidget(button);
	return {row, badge, name, details, button};
}

void StreamControlsDock::updateObsNativeRow(RowWidgets &widgets, const QString &platformId,
					    const QString &serviceName, const QString &detail)
{
	if (!widgets.row)
		return;
	const bool active = obs_frontend_streaming_active();
	static_cast<PlatformBadge *>(widgets.badge)->setIdentity(platformId, serviceName);
	widgets.name->setText(QString("%1 (OBS)").arg(serviceName));
	widgets.details->setText(obsNativeTransitioning_
				 ? (obsNativeExpectedActive_ ? QStringLiteral("Starting OBS native stream")
							     : QStringLiteral("Stopping OBS native stream"))
				 : (active ? QStringLiteral("Live - OBS native stream") : detail));
	widgets.button->setText(obsNativeTransitioning_ ? (obsNativeExpectedActive_ ? QStringLiteral("Starting")
									    : QStringLiteral("Stopping"))
							     : (active ? QStringLiteral("Stop") : QStringLiteral("Start")));
	widgets.button->setToolTip(obsNativeTransitioning_
				   ? QStringLiteral("OBS native streaming is changing state.")
				   : (active ? QStringLiteral("Stop OBS native streaming.")
					     : QStringLiteral("Start OBS native streaming with the OBS account/settings.")));
	widgets.button->setEnabled(!obsNativeTransitioning_);
	const QString style = obsNativeTransitioning_
		? QStringLiteral("QPushButton { min-height: 24px; padding: 2px 6px; font-weight: 700; color: #d6d8dc; background-color: #4a4d54; border: 1px solid #5b6068; border-radius: 3px; }")
		: (active ? QStringLiteral("QPushButton { min-height: 24px; padding: 2px 6px; font-weight: 800; color: white; background-color: #8b3232; border: 1px solid #aa4444; border-radius: 3px; } QPushButton:hover { background-color: #9b3838; }")
			  : QStringLiteral("QPushButton { min-height: 24px; padding: 2px 6px; font-weight: 800; color: white; background-color: #2f6f9f; border: 1px solid #4c87b8; border-radius: 3px; } QPushButton:hover { background-color: #397ead; }"));
	if (widgets.button->styleSheet() != style)
		widgets.button->setStyleSheet(style);
}

} // namespace dsk
