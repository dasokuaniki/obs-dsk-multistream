#pragma once

#include "core/output-manager.hpp"
#include "ui/visible-refresh-gate.hpp"

#include <QWidget>

class QPushButton;
class QLabel;
class QStackedWidget;
class QShowEvent;
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
	void scheduleRefresh();
	void refresh();
	void addTarget();
	void editSelectedTarget();
	void removeSelectedTarget();
	void checkRoutes();
	void handleRouteCheckChanged(QTableWidgetItem *item);
	void updateActionStates();

protected:
	void showEvent(QShowEvent *event) override;

private:
	QString targetIdForRow(int row) const;
	void showPage(QWidget *page, const QString &title);
	void updateDockTitle(const QString &pageTitle);
	void editTargetById(const QString &id);

	OutputManager *manager_ = nullptr;
	QTableWidget *table_ = nullptr;
	QStackedWidget *pages_ = nullptr;
	QWidget *targetsPage_ = nullptr;
	StreamControlsDock *streamControls_ = nullptr;
	SceneRouterDock *sceneRouter_ = nullptr;
	QToolButton *menuButton_ = nullptr;
	QLabel *pageLabel_ = nullptr;
	QPushButton *editButton_ = nullptr;
	QPushButton *removeButton_ = nullptr;
	VisibleRefreshGate targetRefreshGate_;
	bool refreshPending_ = false;
	bool refreshing_ = false;
	bool editArmed_ = false;
	QString currentPageTitle_ = QStringLiteral("Controls");
};

} // namespace dsk
