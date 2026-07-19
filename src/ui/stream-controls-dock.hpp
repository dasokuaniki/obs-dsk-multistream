#pragma once

#include "core/output-manager.hpp"
#include "ui/visible-refresh-gate.hpp"

#include <QHash>
#include <QPointer>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QVBoxLayout;
class QPushButton;
class QLabel;
class QCheckBox;
class QJsonArray;
class QInputDialog;
class QShowEvent;

namespace dsk {

class StreamControlsDock : public QWidget {
	Q_OBJECT

public:
	explicit StreamControlsDock(OutputManager *manager, QWidget *parent = nullptr);
	void handleObsNativeStreamingStateChanged(bool active);

private slots:
	void refresh();
	void scheduleRefresh();
	void handleButtonClicked();
	void handleObsNativeClicked();
	void handleStartEnabled();
	void handleStopAll();
	void handleYouTubeBroadcastSelection(const QString &targetId, quint64 sessionSerial, quint64 selectionGeneration,
					     const QJsonArray &broadcasts);

protected:
	void showEvent(QShowEvent *event) override;

private:
	struct YouTubeBroadcastSelectionRequest {
		QString targetId;
		quint64 sessionSerial = 0;
		quint64 selectionGeneration = 0;
		QStringList labels;
		QStringList ids;
	};

	struct YouTubeBroadcastGenerationState {
		quint64 sessionSerial = 0;
		quint64 selectionGeneration = 0;
	};

	void clearButtons();
	QWidget *createTargetRow(OutputTarget target);
	QWidget *createObsNativeRow();
	void requestStartTargets(const QVector<QString> &ids);
	void beginObsNativeTransition(bool expectedActive);
	bool isYouTubeBroadcastSessionCurrent(const YouTubeBroadcastSelectionRequest &request) const;
	bool isYouTubeBroadcastRequestCurrent(const YouTubeBroadcastSelectionRequest &request) const;
	void discardStaleYouTubeBroadcastSelections();
	void showNextYouTubeBroadcastSelection();
	void refreshYouTubeBroadcastDialog(const YouTubeBroadcastSelectionRequest &request);

	OutputManager *manager_ = nullptr;
	QVBoxLayout *buttons_ = nullptr;
	QPushButton *startChecked_ = nullptr;
	QPushButton *stopAll_ = nullptr;
	QPointer<QInputDialog> youtubeBroadcastDialog_;
	YouTubeBroadcastSelectionRequest activeYouTubeBroadcastSelection_;
	QVector<YouTubeBroadcastSelectionRequest> pendingYouTubeBroadcastSelections_;
	QHash<QString, YouTubeBroadcastGenerationState> youtubeBroadcastLatestGenerations_;
	int obsNativeTransitionGeneration_ = 0;
	bool obsNativeTransitioning_ = false;
	bool obsNativeExpectedActive_ = false;
	bool obsNativeProbeReady_ = false;
	VisibleRefreshGate refreshGate_;
	bool refreshScheduled_ = false;
	bool refreshing_ = false;
};

} // namespace dsk
