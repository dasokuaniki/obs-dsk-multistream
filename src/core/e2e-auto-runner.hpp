#pragma once

#include "core/layout-manager.hpp"
#include "core/output-target.hpp"

#include <QObject>
#include <QString>

struct obs_source;
typedef struct obs_source obs_source_t;

namespace dsk {

class OutputManager;

class E2eAutoRunner : public QObject {
public:
	explicit E2eAutoRunner(OutputManager *manager, QObject *parent = nullptr);
	~E2eAutoRunner() override;

	bool enabled() const;
	void schedule();

private:
	QObject *timerContext() const;
	void start();
	void stop();
	void startRouteMode();
	void toggleRouteTargets();
	void stopRouteMode();
	void finish(bool success);
	bool prepareVerticalLayout();
	void restoreVerticalLayout();
	void removeE2eSource();
	bool startRuntimeTarget(const OutputTarget &target, QString *targetId);
	void stopRuntimeTarget(QString *targetId);

	OutputManager *manager_ = nullptr;
	bool enabled_ = false;
	bool secondTargetEnabled_ = false;
	bool quitObs_ = false;
	bool started_ = false;
	bool useCurrentVerticalLayout_ = false;
	bool verticalLayoutOverridden_ = false;
	QString routeMode_;
	int startDelayMs_ = 6000;
	int runMs_ = 7000;
	QString targetId_;
	QString secondTargetId_;
	QString e2eSourceName_;
	obs_source_t *e2eSource_ = nullptr;
	VerticalLayout previousVerticalLayout_;
	OutputTarget target_;
	OutputTarget secondTarget_;
};

} // namespace dsk
