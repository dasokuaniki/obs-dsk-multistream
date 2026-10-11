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
	VerticalLayout horizontalLayout{1920, 1080, "full-screen", {}};
	QVector<VerticalLayoutScene> horizontalScenes;
	QString activeHorizontalSceneId;
	bool horizontalFollowObsScene = false;
	QVector<SceneLayoutLink> horizontalSceneLinks;
};

class SettingsStore {
public:
	explicit SettingsStore(QString pathOverride = {});
	PluginSettings load();
	bool save(const PluginSettings &settings, QString *errorMessage = nullptr) const;
	QString settingsPath() const;
	QString lastLoadWarning() const;
	bool saveBlocked() const;

private:
	QString pathOverride_;
	QString lastLoadWarning_;
	bool saveBlocked_ = false;
};

} // namespace dsk
