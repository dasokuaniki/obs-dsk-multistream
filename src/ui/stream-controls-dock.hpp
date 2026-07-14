#pragma once

#include "core/output-manager.hpp"

#include <QVector>
#include <QWidget>

class QVBoxLayout;
class QPushButton;
class QLabel;
class QCheckBox;

namespace dsk {

class StreamControlsDock : public QWidget {
	Q_OBJECT

public:
	explicit StreamControlsDock(OutputManager *manager, QWidget *parent = nullptr);
	void handleObsNativeStreamingStateChanged(bool active);

private slots:
	void refresh();
	void handleButtonClicked();
	void handleObsNativeClicked();
	void handleStartEnabled();
	void handleStopAll();

private:
	void clearButtons();
	QWidget *createTargetRow(OutputTarget target);
	QWidget *createObsNativeRow();
	void requestStartTargets(const QVector<QString> &ids);
	void beginObsNativeTransition(bool expectedActive);

	OutputManager *manager_ = nullptr;
	QVBoxLayout *buttons_ = nullptr;
	QPushButton *startChecked_ = nullptr;
	QPushButton *stopAll_ = nullptr;
	int obsNativeTransitionGeneration_ = 0;
	bool obsNativeTransitioning_ = false;
	bool obsNativeExpectedActive_ = false;
	bool obsNativeProbeReady_ = false;
	bool refreshing_ = false;
};

} // namespace dsk
