#pragma once

#include "core/output-manager.hpp"

#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTableWidget;
class QTableWidgetItem;
class QTimer;

namespace dsk {

class SceneRouterDock;
class StreamControlsDock;

class MainDock : public QWidget {
	Q_OBJECT

public:
	explicit MainDock(OutputManager *manager, QWidget *parent = nullptr);
	void handleObsNativeStreamingStateChanged(bool active);

private slots:
	void refresh();
	void addTarget();
	void editSelectedTarget();
	void removeSelectedTarget();
	void checkRoutes();
	void handleRouteCheckChanged(QTableWidgetItem *item);
	void updateActionStates();
	void appendActivity(const QString &message);
	void toggleActivityLog();

private:
	QString targetIdForRow(int row) const;
	void updateStats();
	void updateSummary();

	OutputManager *manager_ = nullptr;
	QTableWidget *table_ = nullptr;
	QTabWidget *tabs_ = nullptr;
	StreamControlsDock *streamControls_ = nullptr;
	SceneRouterDock *sceneRouter_ = nullptr;
	QLabel *status_ = nullptr;
	QLabel *summary_ = nullptr;
	QPlainTextEdit *activityLog_ = nullptr;
	QPushButton *activityToggle_ = nullptr;
	QPushButton *checkButton_ = nullptr;
	QPushButton *editButton_ = nullptr;
	QPushButton *removeButton_ = nullptr;
	QTimer *statsTimer_ = nullptr;
	bool refreshing_ = false;
	bool editArmed_ = false;
};

} // namespace dsk
