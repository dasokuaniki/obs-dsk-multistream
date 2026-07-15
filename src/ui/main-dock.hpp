#pragma once

#include "core/output-manager.hpp"

#include <QWidget>

class QLabel;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTableWidgetItem;
class QToolButton;

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

private:
	QString targetIdForRow(int row) const;
	void showPage(QWidget *page, const QString &title);

	OutputManager *manager_ = nullptr;
	QTableWidget *table_ = nullptr;
	QStackedWidget *pages_ = nullptr;
	StreamControlsDock *streamControls_ = nullptr;
	SceneRouterDock *sceneRouter_ = nullptr;
	QLabel *pageTitle_ = nullptr;
	QToolButton *menuButton_ = nullptr;
	QPushButton *editButton_ = nullptr;
	QPushButton *removeButton_ = nullptr;
	bool refreshing_ = false;
	bool editArmed_ = false;
};

} // namespace dsk
