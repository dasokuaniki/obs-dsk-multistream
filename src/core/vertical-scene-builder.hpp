#pragma once

#include "core/layout-manager.hpp"

#include <QHash>
#include <QString>

struct obs_scene;
typedef struct obs_scene obs_scene_t;
struct obs_scene_item;
typedef struct obs_scene_item obs_sceneitem_t;
struct obs_source;
typedef struct obs_source obs_source_t;
struct obs_canvas;
typedef struct obs_canvas obs_canvas_t;

namespace dsk {

class VerticalSceneBuilder {
public:
	explicit VerticalSceneBuilder(QString sceneName = QStringLiteral("DSK Vertical Program"));
	~VerticalSceneBuilder();

	VerticalSceneBuilder(const VerticalSceneBuilder &) = delete;
	VerticalSceneBuilder &operator=(const VerticalSceneBuilder &) = delete;

	obs_source_t *rebuild(const VerticalLayout &layout, obs_canvas_t *canvas = nullptr, QString *errorMessage = nullptr);
	bool updateItemTransforms(const VerticalLayout &layout);
	obs_source_t *source() const;
	void clear();
	void release(bool removeFromCanvas = true);

private:
	QString sceneName_;
	obs_scene_t *scene_ = nullptr;
	QHash<QString, obs_sceneitem_t *> sceneItems_;
	bool sceneUsesCanvas_ = false;
};

} // namespace dsk
