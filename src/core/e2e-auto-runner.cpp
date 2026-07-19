#include "core/e2e-auto-runner.hpp"

#include "core/diagnostics.hpp"
#include "core/output-manager.hpp"

#include <obs-frontend-api.h>
#include <obs.h>

#include <QCoreApplication>
#include <QStringList>
#include <QTimer>
#include <QWidget>

namespace dsk {
namespace {

bool envFlag(const char *name)
{
	const QByteArray value = qgetenv(name).trimmed().toLower();
	return value == "1" || value == "true" || value == "yes" || value == "on";
}

QString envString(const char *name, const QString &fallback)
{
	const QByteArray value = qgetenv(name);
	if (value.trimmed().isEmpty())
		return fallback;
	return QString::fromUtf8(value);
}

int envInt(const char *name, int fallback, int minimum)
{
	bool ok = false;
	const int value = QString::fromUtf8(qgetenv(name)).toInt(&ok);
	if (!ok || value < minimum)
		return fallback;
	return value;
}

} // namespace

E2eAutoRunner::E2eAutoRunner(OutputManager *manager, QObject *parent)
	: QObject(parent),
	  manager_(manager)
{
	enabled_ = envFlag("DSK_E2E_AUTORUN");
	if (!enabled_)
		return;

	if (!parent) {
		if (QCoreApplication *app = QCoreApplication::instance(); app && thread() != app->thread())
			moveToThread(app->thread());
	}

	target_.id = envString("DSK_E2E_TARGET_ID", QStringLiteral("dsk-e2e-") + newTargetId());
	target_.name = envString("DSK_E2E_TARGET_NAME", QStringLiteral("DSK E2E"));
	target_.platformId = QStringLiteral("custom");
	target_.serverUrl = envString("DSK_E2E_SERVER", QStringLiteral("rtmp://127.0.0.1:19354/live"));
	target_.streamKey = envString("DSK_E2E_KEY", QStringLiteral("dsk-e2e"));
	target_.encoderGroup = encoderGroupFromString(envString("DSK_E2E_ENCODER_GROUP", QStringLiteral("dsk-horizontal")));
	if (envFlag("DSK_E2E_DISABLE_RECONNECT"))
		target_.reconnectEnabled = false;
	target_.enabled = true;
	target_.startWithAll = false;
	const QString secondServer = envString("DSK_E2E_SECOND_SERVER", QString());
	if (!secondServer.isEmpty()) {
		secondTarget_ = target_;
		secondTarget_.id = envString("DSK_E2E_SECOND_TARGET_ID", QStringLiteral("dsk-e2e-secondary-") + newTargetId());
		secondTarget_.name = envString("DSK_E2E_SECOND_TARGET_NAME", QStringLiteral("DSK E2E Secondary"));
		secondTarget_.serverUrl = secondServer;
		secondTarget_.streamKey = envString("DSK_E2E_SECOND_KEY", QStringLiteral("dsk-e2e-secondary"));
		secondTarget_.encoderGroup = encoderGroupFromString(envString("DSK_E2E_SECOND_ENCODER_GROUP", encoderGroupToString(target_.encoderGroup)));
		if (envFlag("DSK_E2E_DISABLE_RECONNECT"))
			secondTarget_.reconnectEnabled = false;
		secondTarget_.startWithAll = false;
		secondTargetEnabled_ = true;
		logInfo(QString("E2E secondary target enabled for %1 encoder=%2").arg(secondTarget_.serverUrl, encoderGroupToString(secondTarget_.encoderGroup)));
	}
	e2eSourceName_ = QStringLiteral("DSK E2E Vertical Source ") + newTargetId();

	startDelayMs_ = envInt("DSK_E2E_START_DELAY_MS", startDelayMs_, 0);
	runMs_ = envInt("DSK_E2E_RUN_MS", runMs_, 1000);
	quitObs_ = envFlag("DSK_E2E_QUIT_OBS");
	useCurrentVerticalLayout_ = envFlag("DSK_E2E_USE_CURRENT_VERTICAL_LAYOUT");
	routeMode_ = envString("DSK_E2E_ROUTE_MODE", QString()).trimmed().toLower();
	if (routeMode_ == "initial-disabled" && secondTargetEnabled_)
		secondTarget_.enabled = false;
}

E2eAutoRunner::~E2eAutoRunner()
{
	stopRuntimeTarget(&secondTargetId_);
	stopRuntimeTarget(&targetId_);
	restoreVerticalLayout();
	removeE2eSource();
}

bool E2eAutoRunner::enabled() const
{
	return enabled_;
}

void E2eAutoRunner::schedule()
{
	if (!enabled_)
		return;

	logInfo(QString("E2E auto-run enabled for %1 with direct OBS UI start, requested %2 ms start delay, and %3 ms run time")
			.arg(target_.serverUrl)
			.arg(startDelayMs_)
			.arg(runMs_));
	QTimer::singleShot(startDelayMs_, timerContext(), [this]() { start(); });
	logInfo("E2E start scheduled.");
}

QObject *E2eAutoRunner::timerContext() const
{
	return const_cast<E2eAutoRunner *>(this);
}

void E2eAutoRunner::start()
{
	if (started_)
		return;

	started_ = true;
	logInfo("E2E direct UI start fired.");
	if (!routeMode_.isEmpty()) {
		startRouteMode();
		return;
	}
	target_.enabled = true;
	if (secondTargetEnabled_)
		secondTarget_.enabled = true;

	const OutputTarget primaryTarget = target_;
	const OutputTarget secondaryTarget = secondTarget_;
	logInfo(QString("E2E start config server=%1 encoder=%2").arg(target_.serverUrl, encoderGroupToString(target_.encoderGroup)));

	if (!manager_) {
		logError("E2E start failed: output manager is unavailable.");
		finish(false);
		return;
	}

	QString error;
	if (!validateOutputTargetConfig(primaryTarget, &error)) {
		logError(QString("E2E start failed: %1").arg(error));
		finish(false);
		return;
	}
	if (secondTargetEnabled_ && !validateOutputTargetConfig(secondaryTarget, &error)) {
		logError(QString("E2E secondary start failed: %1").arg(error));
		finish(false);
		return;
	}

	const bool needsVerticalLayout = primaryTarget.encoderGroup == EncoderGroup::DskVertical ||
					 (secondTargetEnabled_ && secondaryTarget.encoderGroup == EncoderGroup::DskVertical);
	if (needsVerticalLayout && !prepareVerticalLayout()) {
		finish(false);
		return;
	}

	if (!startRuntimeTarget(primaryTarget, &targetId_)) {
		finish(false);
		return;
	}
	if (secondTargetEnabled_) {
		logInfo(QString("E2E starting secondary target for %1").arg(secondaryTarget.serverUrl));
		if (!startRuntimeTarget(secondaryTarget, &secondTargetId_)) {
			finish(false);
			return;
		}
	}

	QTimer::singleShot(runMs_, timerContext(), [this]() { stop(); });
}

void E2eAutoRunner::startRouteMode()
{
	if (!manager_) {
		logError("E2E route start failed: output manager is unavailable.");
		finish(false);
		return;
	}
	if (routeMode_ != "initial-disabled" && routeMode_ != "toggle") {
		logError(QString("E2E route start failed: unsupported route mode %1.").arg(routeMode_));
		finish(false);
		return;
	}

	target_.enabled = true;
	target_.autoStartWithObs = true;
	if (secondTargetEnabled_)
		secondTarget_.enabled = routeMode_ != "initial-disabled" && routeMode_ != "toggle";
	if (secondTargetEnabled_)
		secondTarget_.autoStartWithObs = true;

	const bool needsVerticalLayout = target_.encoderGroup == EncoderGroup::DskVertical ||
					 (secondTargetEnabled_ && secondTarget_.encoderGroup == EncoderGroup::DskVertical);
	if (needsVerticalLayout && !prepareVerticalLayout()) {
		finish(false);
		return;
	}

	targetId_ = manager_->addRuntimeTarget(target_);
	logInfo(QString("E2E start requested for runtime target %1").arg(targetId_));
	if (secondTargetEnabled_) {
		secondTargetId_ = manager_->addRuntimeTarget(secondTarget_);
		logInfo(QString("E2E secondary route target added as %1 enabled=%2")
				.arg(secondTargetId_, secondTarget_.enabled ? QStringLiteral("true") : QStringLiteral("false")));
	}

	logInfo(QString("E2E route mode %1 invoking OBS streaming start handler.").arg(routeMode_));
	manager_->handleObsStreamingStarted();
	if (routeMode_ == "initial-disabled")
		logInfo("E2E route secondary target intentionally disabled.");

	if (routeMode_ == "toggle" && secondTargetEnabled_) {
		const int toggleMs = qMax(1000, runMs_ / 2);
		QTimer::singleShot(toggleMs, timerContext(), [this]() { toggleRouteTargets(); });
	}
	QTimer::singleShot(runMs_, timerContext(), [this]() { stopRouteMode(); });
}

void E2eAutoRunner::toggleRouteTargets()
{
	if (!manager_ || targetId_.isEmpty() || secondTargetId_.isEmpty())
		return;

	logInfo("E2E route toggle: secondary on, primary off.");
	manager_->setTargetEnabled(secondTargetId_, true);
	manager_->handleObsStreamingStarted();
	manager_->setTargetEnabled(targetId_, false);
	manager_->stopTarget(targetId_);
}

void E2eAutoRunner::stopRouteMode()
{
	if (!manager_ || (targetId_.isEmpty() && secondTargetId_.isEmpty())) {
		finish(false);
		return;
	}

	stopRuntimeTarget(&secondTargetId_);
	stopRuntimeTarget(&targetId_);
	restoreVerticalLayout();
	removeE2eSource();
	finish(true);
}

void E2eAutoRunner::stop()
{
	if (!manager_ || (targetId_.isEmpty() && secondTargetId_.isEmpty())) {
		finish(false);
		return;
	}

	stopRuntimeTarget(&secondTargetId_);
	stopRuntimeTarget(&targetId_);
	restoreVerticalLayout();
	removeE2eSource();
	finish(true);
}

void E2eAutoRunner::finish(bool success)
{
	stopRuntimeTarget(&secondTargetId_);
	stopRuntimeTarget(&targetId_);
	restoreVerticalLayout();
	removeE2eSource();

	logInfo(success ? QStringLiteral("E2E auto-run complete.") : QStringLiteral("E2E auto-run failed."));
	if (quitObs_) {
		QTimer::singleShot(1000, timerContext(), []() {
			if (auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window())) {
				mainWindow->close();
			} else if (QCoreApplication *app = QCoreApplication::instance()) {
				app->quit();
			}
		});
	}
}

bool E2eAutoRunner::prepareVerticalLayout()
{
	if (!manager_)
		return false;

	if (useCurrentVerticalLayout_) {
		const VerticalLayout &layout = manager_->layouts().verticalLayout();
		if (layout.width <= 0 || layout.height <= 0) {
			logError(QString("E2E current vertical layout is invalid: %1x%2.")
					 .arg(layout.width)
					 .arg(layout.height));
			return false;
		}

		int visibleCount = 0;
		int availableCount = 0;
		QStringList visibleSources;
		for (const VerticalLayoutItem &item : layout.items) {
			if (!item.visible)
				continue;

			++visibleCount;
			obs_source_t *source = obs_get_source_by_name(item.sourceName.toUtf8().constData());
			const bool available = source != nullptr;
			if (available) {
				++availableCount;
				obs_source_release(source);
			}
			visibleSources.push_back(QString("%1:%2")
							 .arg(item.sourceName, available ? QStringLiteral("available")
											 : QStringLiteral("missing")));
		}

		if (visibleCount == 0 || availableCount == 0) {
			logError(QString("E2E current vertical layout has no available visible sources (visible=%1, available=%2).")
					 .arg(visibleCount)
					 .arg(availableCount));
			return false;
		}

		logInfo(QString("E2E using current vertical layout %1x%2 visible=%3 available=%4 sources=[%5]")
				.arg(layout.width)
				.arg(layout.height)
				.arg(visibleCount)
				.arg(availableCount)
				.arg(visibleSources.join(QStringLiteral(", "))));
		return true;
	}

	obs_data_t *settings = obs_data_create();
	obs_data_set_int(settings, "color", 0xFF2D8CFF);
	obs_data_set_int(settings, "width", 1080);
	obs_data_set_int(settings, "height", 1920);

	e2eSource_ = obs_source_create("color_source", e2eSourceName_.toUtf8().constData(), settings, nullptr);
	obs_data_release(settings);
	if (!e2eSource_) {
		logError("E2E vertical setup failed: unable to create color_source.");
		return false;
	}

	previousVerticalLayout_ = manager_->layouts().verticalLayout();
	VerticalLayout layout;
	layout.width = 1080;
	layout.height = 1920;
	layout.templateId = QStringLiteral("e2e-vertical");
	layout.items.push_back({newTargetId(), e2eSourceName_, QRectF(0, 0, 1080, 1920), QRectF(), FitMode::Stretch, true});
	manager_->layouts().setVerticalLayout(layout);
	verticalLayoutOverridden_ = true;
	logInfo(QString("E2E vertical layout prepared with source %1").arg(e2eSourceName_));
	return true;
}

void E2eAutoRunner::restoreVerticalLayout()
{
	if (!manager_ || !verticalLayoutOverridden_)
		return;

	manager_->layouts().setVerticalLayout(previousVerticalLayout_);
	verticalLayoutOverridden_ = false;
	logInfo("E2E vertical layout restored.");
}

void E2eAutoRunner::removeE2eSource()
{
	if (!e2eSource_)
		return;

	obs_source_remove(e2eSource_);
	obs_source_release(e2eSource_);
	e2eSource_ = nullptr;
}

bool E2eAutoRunner::startRuntimeTarget(const OutputTarget &target, QString *targetId)
{
	if (!manager_ || !targetId)
		return false;

	*targetId = manager_->addRuntimeTarget(target);
	logInfo(QString("E2E start requested for runtime target %1").arg(*targetId));
	if (!manager_->startTarget(*targetId)) {
		logError(QString("E2E start failed for runtime target %1").arg(*targetId));
		manager_->removeRuntimeTarget(*targetId);
		targetId->clear();
		return false;
	}
	return true;
}

void E2eAutoRunner::stopRuntimeTarget(QString *targetId)
{
	if (!manager_ || !targetId || targetId->isEmpty())
		return;

	logInfo(QString("E2E stop requested for runtime target %1").arg(*targetId));
	manager_->stopTarget(*targetId);
	manager_->removeRuntimeTarget(*targetId);
	targetId->clear();
}

} // namespace dsk
