#include "ui/stream-controls-dock.hpp"

#include "core/diagnostics.hpp"
#include "core/oauth-provider.hpp"
#include "core/youtube-api-warning.hpp"

#include <obs.h>
#include <obs-frontend-api.h>
#include <util/bmem.h>

#include <QFile>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
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

QString buttonStyle(const OutputTarget &target, const TargetRuntimeStatus &runtime)
{
	if (target.state == TargetState::Stopping || runtime.transport == TransportState::Stopping)
		return "QPushButton { min-width: 68px; min-height: 26px; padding: 4px 10px; font-weight: 700; color: #d6d8dc; "
		       "background-color: #4a4d54; border: 1px solid #5b6068; border-radius: 3px; }";
	if (runtime.transport == TransportState::Reconnecting)
		return "QPushButton { min-width: 68px; min-height: 26px; padding: 4px 10px; font-weight: 800; color: #f4eefc; "
		       "background-color: #5b3a82; border: 1px solid #7a57a6; border-radius: 3px; }";
	if (isRunning(target, runtime))
		return "QPushButton { min-width: 68px; min-height: 26px; padding: 4px 10px; font-weight: 800; color: white; "
		       "background-color: #8b3232; border: 1px solid #aa4444; border-radius: 3px; }"
		       "QPushButton:hover { background-color: #9b3838; }";
	return "QPushButton { min-width: 68px; min-height: 26px; padding: 4px 10px; font-weight: 800; color: white; "
	       "background-color: #26763d; border: 1px solid #3f9458; border-radius: 3px; }"
	       "QPushButton:hover { background-color: #2d8547; }";
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

	info.serviceName = !serviceName.isEmpty() ? serviceName : (!name.isEmpty() ? name : QStringLiteral("OBS Native Stream"));
	info.available = !info.serviceName.trimmed().isEmpty() || !type.trimmed().isEmpty();
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
	layout->setContentsMargins(6, 6, 6, 6);
	layout->setSpacing(5);

	buttons_ = new QVBoxLayout();
	buttons_->setContentsMargins(0, 0, 0, 0);
	buttons_->setSpacing(4);
	layout->addLayout(buttons_);

	auto *footer = new QHBoxLayout();
	footer->setContentsMargins(0, 0, 0, 0);
	footer->setSpacing(5);
	startChecked_ = new QPushButton("Start All", this);
	stopAll_ = new QPushButton("Stop All", this);
	startChecked_->setMinimumHeight(28);
	stopAll_->setMinimumHeight(28);
	startChecked_->setStyleSheet("QPushButton { padding: 5px 9px; font-weight: 800; color: #f3f7f4; "
				     "background-color: #2b6f3f; border: 1px solid #438a56; border-radius: 3px; }"
				     "QPushButton:hover { background-color: #327e49; }");
	stopAll_->setStyleSheet("QPushButton { padding: 5px 9px; font-weight: 800; color: #f7f0f0; "
				"background-color: #673437; border: 1px solid #87494d; border-radius: 3px; }"
				"QPushButton:hover { background-color: #783d42; }");
	connect(startChecked_, &QPushButton::clicked, this, &StreamControlsDock::handleStartEnabled);
	connect(stopAll_, &QPushButton::clicked, this, &StreamControlsDock::handleStopAll);
	footer->addWidget(startChecked_);
	footer->addWidget(stopAll_);
	layout->addLayout(footer);
	layout->addStretch(1);

	setStyleSheet("StreamControlsDock { background-color: #1f1f1f; }");

	if (manager_) {
		connect(manager_, &OutputManager::targetsChanged, this, &StreamControlsDock::refresh, Qt::QueuedConnection);
		connect(manager_, &OutputManager::targetRuntimeChanged, this, &StreamControlsDock::refresh, Qt::QueuedConnection);
		connect(manager_, &OutputManager::statusMessage, this, &StreamControlsDock::refresh, Qt::QueuedConnection);
	}

	logInfo("Stream Controls dock initial refresh begin");
	refresh();
	logInfo("Stream Controls dock initial refresh complete");
	QTimer::singleShot(2500, this, [this]() {
		obsNativeProbeReady_ = true;
		logInfo("Stream Controls dock OBS native probe enabled");
		refresh();
	});
	logInfo("Stream Controls dock constructor complete");
}

void StreamControlsDock::refresh()
{
	if (refreshing_ || !buttons_ || !startChecked_ || !stopAll_)
		return;

	refreshing_ = true;
	clearButtons();

	int eligibleStartAllCount = 0;

	const QVector<OutputTarget> targets = manager_ ? manager_->targets() : QVector<OutputTarget>{};
	const bool hasObsNative = obsNativeProbeReady_ && obsNativeStreamInfo().available;
	if (!manager_ || (targets.isEmpty() && !hasObsNative)) {
		auto *empty = new QLabel("No stream targets configured.", this);
		empty->setAlignment(Qt::AlignCenter);
		empty->setMinimumHeight(48);
		buttons_->addWidget(empty);
		startChecked_->setEnabled(false);
		stopAll_->setEnabled(false);
		refreshing_ = false;
		return;
	}

	if (hasObsNative)
		buttons_->addWidget(createObsNativeRow());

	for (const auto &target : targets) {
		if (target.enabled && target.startWithAll)
			++eligibleStartAllCount;
		buttons_->addWidget(createTargetRow(target));
	}

	bool hasRunningTarget = false;
	if (manager_) {
		for (const auto &target : targets) {
			if (isRunning(target, manager_->runtimeStatusForTarget(target.id))) {
				hasRunningTarget = true;
				break;
			}
		}
	}
	const bool startObsNative = hasObsNative && !obs_frontend_streaming_active();
	startChecked_->setEnabled(eligibleStartAllCount > 0 || startObsNative);
	startChecked_->setToolTip(QStringLiteral("Start OBS native streaming and %1 included DSK target(s).")
						.arg(eligibleStartAllCount));
	stopAll_->setEnabled(obs_frontend_streaming_active() || hasRunningTarget);
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
	refresh();
}

void StreamControlsDock::beginObsNativeTransition(bool expectedActive)
{
	obsNativeExpectedActive_ = expectedActive;
	obsNativeTransitioning_ = true;
	const int generation = ++obsNativeTransitionGeneration_;
	refresh();

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
		refresh();
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

void StreamControlsDock::handleStartEnabled()
{
	if (!manager_)
		return;

	const QVector<QString> ids = startAllTargetIds(manager_->targets());
	requestStartTargets(ids);

	const bool hasObsNative = obsNativeProbeReady_ && obsNativeStreamInfo().available;
	if (hasObsNative && !obs_frontend_streaming_active() && !obsNativeTransitioning_) {
		beginObsNativeTransition(true);
		manager_->suppressNextObsAutoStart();
		obs_frontend_streaming_start();
	}
}

void StreamControlsDock::handleStopAll()
{
	if (!manager_)
		return;
	manager_->stopAll();
	const bool obsStopAlreadyPending = obsNativeTransitioning_ && !obsNativeExpectedActive_;
	if (obs_frontend_streaming_active() && !obsStopAlreadyPending) {
		beginObsNativeTransition(false);
		manager_->suppressNextObsAutoStop();
		obs_frontend_streaming_stop();
	}
}

void StreamControlsDock::clearButtons()
{
	if (!buttons_)
		return;

	while (QLayoutItem *item = buttons_->takeAt(0)) {
		if (QWidget *widget = item->widget())
			widget->deleteLater();
		delete item;
	}
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

QWidget *StreamControlsDock::createTargetRow(OutputTarget target)
{
	const TargetRuntimeStatus runtime = manager_ ? manager_->runtimeStatusForTarget(target.id) : TargetRuntimeStatus{};
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
	auto *details = new QLabel(rowDetailText(target, runtime), row);
	details->setTextInteractionFlags(Qt::TextSelectableByMouse);
	const bool liveWarning = isYouTubeTarget(target) && runtimePlatformIsWarning(runtime.platform) &&
				 runtimeTransportIsRunning(runtime);
	details->setStyleSheet(liveWarning ? "QLabel { color: #d0a84f; font-size: 10px; border: 0; background: transparent; }"
					   : "QLabel { color: #a4a4a4; font-size: 10px; border: 0; background: transparent; }");
	details->setMaximumHeight(14);
	textColumn->addWidget(name);
	textColumn->addWidget(details);

	auto *button = new QPushButton(actionText(target, runtime), row);
	button->setProperty("targetId", target.id);
	button->setToolTip(isRunning(target, runtime) ? QString("Stop only %1.").arg(target.name)
						      : QString("Start only %1.").arg(target.name));
	button->setEnabled(!isBusy(target, runtime));
	button->setStyleSheet(buttonStyle(target, runtime));
	connect(button, &QPushButton::clicked, this, &StreamControlsDock::handleButtonClicked);

	layout->addWidget(badge);
	layout->addLayout(textColumn, 1);
	layout->addWidget(button);
	return row;
}

QWidget *StreamControlsDock::createObsNativeRow()
{
	const ObsNativeStreamInfo info = obsNativeStreamInfo();
	const bool active = obs_frontend_streaming_active();
	const QString detailText = obsNativeTransitioning_
		? (obsNativeExpectedActive_ ? QStringLiteral("Starting OBS native stream")
					    : QStringLiteral("Stopping OBS native stream"))
		: (active ? QStringLiteral("Live - OBS native stream") : info.detail);
	const QString buttonText = obsNativeTransitioning_ ? (obsNativeExpectedActive_ ? QStringLiteral("Starting")
										 : QStringLiteral("Stopping"))
							  : (active ? QStringLiteral("Stop") : QStringLiteral("Start"));
	const QString buttonToolTip = obsNativeTransitioning_
		? QStringLiteral("OBS native streaming is changing state.")
		: (active ? QStringLiteral("Stop OBS native streaming.")
			  : QStringLiteral("Start OBS native streaming with the OBS account/settings."));
	const QString buttonStyle = obsNativeTransitioning_
		? QStringLiteral("QPushButton { min-width: 68px; min-height: 26px; padding: 4px 10px; font-weight: 700; color: #d6d8dc; background-color: #4a4d54; border: 1px solid #5b6068; border-radius: 3px; }")
		: (active ? QStringLiteral("QPushButton { min-width: 68px; min-height: 26px; padding: 4px 10px; font-weight: 800; color: white; background-color: #8b3232; border: 1px solid #aa4444; border-radius: 3px; } QPushButton:hover { background-color: #9b3838; }")
			  : QStringLiteral("QPushButton { min-width: 68px; min-height: 26px; padding: 4px 10px; font-weight: 800; color: white; background-color: #2f6f9f; border: 1px solid #4c87b8; border-radius: 3px; } QPushButton:hover { background-color: #397ead; }"));
	OutputTarget fake;
	fake.name = info.serviceName;
	fake.platformId = info.platformId;
	fake.enabled = true;

	auto *row = new QWidget(this);
	row->setObjectName("targetRow");
	row->setStyleSheet("QWidget#targetRow { background-color: #24262a; border: 1px solid #464b53; border-radius: 4px; }");
	auto *layout = new QHBoxLayout(row);
	layout->setContentsMargins(6, 5, 6, 5);
	layout->setSpacing(6);

	auto *badge = new PlatformBadge(fake.platformId, fake.name, row);

	auto *textColumn = new QVBoxLayout();
	textColumn->setContentsMargins(0, 0, 0, 0);
	textColumn->setSpacing(0);
	auto *name = new QLabel(QString("%1 (OBS)").arg(info.serviceName), row);
	name->setStyleSheet("QLabel { color: #f0f0f0; font-size: 12px; font-weight: 800; border: 0; background: transparent; }");
	auto *details = new QLabel(detailText, row);
	details->setTextInteractionFlags(Qt::TextSelectableByMouse);
	details->setStyleSheet("QLabel { color: #aeb4bd; font-size: 10px; border: 0; background: transparent; }");
	details->setMaximumHeight(14);
	textColumn->addWidget(name);
	textColumn->addWidget(details);

	auto *button = new QPushButton(buttonText, row);
	button->setToolTip(buttonToolTip);
	button->setEnabled(!obsNativeTransitioning_);
	button->setStyleSheet(buttonStyle);
	connect(button, &QPushButton::clicked, this, &StreamControlsDock::handleObsNativeClicked);

	layout->addWidget(badge);
	layout->addLayout(textColumn, 1);
	layout->addWidget(button);
	return row;
}

} // namespace dsk
