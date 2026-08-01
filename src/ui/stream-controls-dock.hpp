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
class QToolButton;
class QJsonArray;
class QInputDialog;
class QShowEvent;

namespace dsk {

class StreamControlsDock : public QWidget {
	Q_OBJECT

public:
	explicit StreamControlsDock(OutputManager *manager, QWidget *parent = nullptr);
	void handleObsNativeStreamingStateChanged(bool active);
	QPushButton *allToggleButton() const;

signals:
	void editTargetRequested(const QString &targetId);

private slots:
	void refresh();
	void scheduleRefresh();
	void handleButtonClicked();
	void handleObsNativeClicked();
	void handleAllToggle();
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

	struct RowWidgets {
		QWidget *row = nullptr;
		QWidget *badge = nullptr;
		QLabel *name = nullptr;
		QLabel *details = nullptr;
		QWidget *statusLight = nullptr;
		QPushButton *button = nullptr;
		QToolButton *actionMenu = nullptr;
	};

	RowWidgets createVisualRow(const QString &platformId, const QString &name);
	RowWidgets createTargetRow(const OutputTarget &target);
	RowWidgets createObsNativeRow();
	void updateTargetRow(const OutputTarget &target, const TargetRuntimeStatus &runtime, RowWidgets &widgets);
	void updateObsNativeRow(RowWidgets &widgets, const QString &platformId, const QString &serviceName,
				const QString &detail);
	void removeStaleTargetRows(const QVector<OutputTarget> &targets);
	void placeRow(QWidget *row, int index);
	void removeRow(RowWidgets &widgets);
	void setEmptyStateVisible(bool visible);
	void requestStartTargets(const QVector<QString> &ids);
	void handleStartEnabled();
	void handleStopAll();
	void beginObsNativeTransition(bool expectedActive);
	bool isYouTubeBroadcastSessionCurrent(const YouTubeBroadcastSelectionRequest &request) const;
	bool isYouTubeBroadcastRequestCurrent(const YouTubeBroadcastSelectionRequest &request) const;
	void discardStaleYouTubeBroadcastSelections();
	void showNextYouTubeBroadcastSelection();
	void refreshYouTubeBroadcastDialog(const YouTubeBroadcastSelectionRequest &request);

	QPointer<OutputManager> manager_;
	QVBoxLayout *buttons_ = nullptr;
	QPushButton *allToggle_ = nullptr;
	QPointer<QInputDialog> youtubeBroadcastDialog_;
	YouTubeBroadcastSelectionRequest activeYouTubeBroadcastSelection_;
	QVector<YouTubeBroadcastSelectionRequest> pendingYouTubeBroadcastSelections_;
	QHash<QString, YouTubeBroadcastGenerationState> youtubeBroadcastLatestGenerations_;
	QHash<QString, RowWidgets> targetRows_;
	RowWidgets obsNativeRow_;
	QLabel *emptyState_ = nullptr;
	int obsNativeTransitionGeneration_ = 0;
	bool obsNativeTransitioning_ = false;
	bool obsNativeExpectedActive_ = false;
	bool obsNativeProbeReady_ = false;
	bool allToggleStops_ = false;
	VisibleRefreshGate refreshGate_;
	bool refreshScheduled_ = false;
	bool refreshing_ = false;
};

} // namespace dsk
