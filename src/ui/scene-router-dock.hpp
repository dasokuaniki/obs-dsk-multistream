#pragma once

#include "core/output-manager.hpp"

#include <QWidget>

class QLabel;
class QComboBox;
class QVBoxLayout;

namespace dsk {

class SceneRouterDock : public QWidget {
	Q_OBJECT

public:
	explicit SceneRouterDock(OutputManager *manager, QWidget *parent = nullptr);

private slots:
	void refresh();

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
	bool refreshing_ = false;
};

} // namespace dsk
