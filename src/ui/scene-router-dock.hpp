#pragma once

#include "core/output-manager.hpp"
#include "ui/visible-refresh-gate.hpp"

#include <QWidget>

class QLabel;
class QComboBox;
class QShowEvent;
class QVBoxLayout;

namespace dsk {

class SceneRouterDock : public QWidget {
	Q_OBJECT

public:
	explicit SceneRouterDock(OutputManager *manager, QWidget *parent = nullptr);

private slots:
	void scheduleRefresh();
	void refresh();

protected:
	void showEvent(QShowEvent *event) override;

private:
	void populateSceneCombo(QComboBox *combo, const QStringList &scenes, const QString &selected, bool includeNone) const;
	QString routeForScene(const OutputTarget &target, const QString &obsSceneName, const QString &obsSceneUuid) const;
	QWidget *createTargetRow(const OutputTarget &target,
				 const QStringList &scenes,
				 const QString &currentObsScene,
				 const QString &currentObsSceneUuid);
	void clearRows();

	OutputManager *manager_ = nullptr;
	QLabel *currentScene_ = nullptr;
	QLabel *summary_ = nullptr;
	QVBoxLayout *rows_ = nullptr;
	VisibleRefreshGate refreshGate_;
	bool refreshScheduled_ = false;
	bool refreshing_ = false;
};

} // namespace dsk
