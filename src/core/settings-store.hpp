#pragma once

#include "core/layout-manager.hpp"
#include "core/output-target.hpp"

#include <QString>
#include <QVector>

namespace dsk {

struct PluginSettings {
	QVector<OutputTarget> targets;
	VerticalLayout verticalLayout;
	QVector<VerticalLayoutScene> verticalScenes;
	QString activeVerticalSceneId;
	bool followObsScene = false;
	QVector<SceneLayoutLink> sceneLinks;
};

class SettingsStore {
public:
	explicit SettingsStore(QString pathOverride = {});
	PluginSettings load();
	bool save(const PluginSettings &settings, QString *errorMessage = nullptr) const;
	bool save(const QVector<OutputTarget> &targets,
		  const VerticalLayout &verticalLayout,
		  const QVector<VerticalLayoutScene> &verticalScenes,
		  const QString &activeVerticalSceneId,
		  bool followObsScene,
		  const QVector<SceneLayoutLink> &sceneLinks,
		  QString *errorMessage = nullptr) const;
	QString settingsPath() const;
	QString lastLoadWarning() const;
	bool saveBlocked() const;

private:
	QString pathOverride_;
	QString lastLoadWarning_;
	bool saveBlocked_ = false;
};

} // namespace dsk
