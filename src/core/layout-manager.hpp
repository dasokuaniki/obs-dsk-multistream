#pragma once

#include <QRectF>
#include <QString>
#include <QVector>

namespace dsk {

enum class FitMode {
	Fit,
	Fill,
	Stretch,
};

struct VerticalLayoutItem {
	QString id;
	QString sourceName;
	QRectF rect{0.0, 0.0, 1080.0, 1920.0};
	QRectF crop{0.0, 0.0, 0.0, 0.0};
	FitMode fitMode = FitMode::Fill;
	bool visible = true;
};

struct VerticalLayout {
	int width = 1080;
	int height = 1920;
	QString templateId = "full-screen";
	QVector<VerticalLayoutItem> items;
};

struct VerticalLayoutScene {
	QString id;
	QString name;
	VerticalLayout layout;
};

struct SceneLayoutLink {
	QString sceneName;
	QString sceneUuid;
	QString verticalSceneId;
	QString legacyTemplateId;
};

QString fitModeToString(FitMode mode);
FitMode fitModeFromString(const QString &value);

class LayoutManager {
public:
	void initializeVerticalScenes(const QVector<VerticalLayoutScene> &scenes,
				      const QString &activeSceneId,
				      const VerticalLayout &fallbackLayout);
	const VerticalLayout &verticalLayout() const;
	void setVerticalLayout(const VerticalLayout &layout);
	void applyTemplate(const QString &templateId);
	const QVector<VerticalLayoutScene> &verticalScenes() const;
	const QString &activeVerticalSceneId() const;
	QString activeVerticalSceneName() const;
	QString verticalSceneName(const QString &id) const;
	QString createVerticalScene(const QString &name);
	bool removeVerticalScene(const QString &id);
	bool renameVerticalScene(const QString &id, const QString &name);
	bool moveVerticalScene(const QString &id, int offset);
	bool reorderVerticalScenes(const QVector<QString> &orderedIds);
	bool selectVerticalScene(const QString &id);

private:
	void ensureDefaultScene();
	void syncActiveSceneLayout();

	VerticalLayout vertical_;
	QVector<VerticalLayoutScene> verticalScenes_;
	QString activeVerticalSceneId_;
};

} // namespace dsk
