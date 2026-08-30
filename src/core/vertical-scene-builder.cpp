#include "core/vertical-scene-builder.hpp"

#include <graphics/vec2.h>
#include <obs.h>

#include <QSet>
#include <QVector>

#include <utility>

namespace dsk {
namespace {

void applyItemTransform(obs_sceneitem_t *item, const VerticalLayoutItem &layoutItem)
{
	vec2 pos;
	pos.x = float(layoutItem.rect.x());
	pos.y = float(layoutItem.rect.y());
	obs_sceneitem_set_pos(item, &pos);

	vec2 bounds;
	bounds.x = float(layoutItem.rect.width());
	bounds.y = float(layoutItem.rect.height());
	obs_sceneitem_set_bounds(item, &bounds);
	obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
	obs_sceneitem_set_bounds_crop(item, true);

	if (layoutItem.fitMode == FitMode::Fit)
		obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
	else if (layoutItem.fitMode == FitMode::Fill)
		obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_OUTER);
	else
		obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_STRETCH);

	obs_sceneitem_crop crop;
	crop.left = int(layoutItem.crop.x());
	crop.top = int(layoutItem.crop.y());
	crop.right = int(layoutItem.crop.width());
	crop.bottom = int(layoutItem.crop.height());
	obs_sceneitem_set_crop(item, &crop);
}

struct TransformUpdateData {
	const VerticalLayout *layout = nullptr;
	const QHash<QString, obs_sceneitem_t *> *itemsById = nullptr;
	const QSet<QString> *expectedIds = nullptr;
	bool valid = false;
};

void updateTransformsAtomically(void *param, obs_scene_t *scene)
{
	auto *data = static_cast<TransformUpdateData *>(param);
	if (!data || !data->layout || !data->itemsById || !data->expectedIds)
		return;

	QSet<obs_sceneitem_t *> liveItems;
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *itemsParam) {
			static_cast<QSet<obs_sceneitem_t *> *>(itemsParam)->insert(item);
			return true;
		},
		&liveItems);

	for (const QString &id : *data->expectedIds) {
		obs_sceneitem_t *item = data->itemsById->value(id, nullptr);
		if (!item || !liveItems.contains(item))
			return;
	}

	if (data->expectedIds->size() != data->itemsById->size() || liveItems.size() != data->expectedIds->size())
		return;

	for (const auto &layoutItem : data->layout->items) {
		obs_sceneitem_t *item = data->itemsById->value(layoutItem.id, nullptr);
		if (item)
			applyItemTransform(item, layoutItem);
	}
	data->valid = true;
}

} // namespace

VerticalSceneBuilder::VerticalSceneBuilder(QString sceneName)
	: sceneName_(sceneName.trimmed().isEmpty() ? QStringLiteral("DSK Vertical Program") : std::move(sceneName))
{
}

VerticalSceneBuilder::~VerticalSceneBuilder()
{
	release();
}

obs_source_t *VerticalSceneBuilder::rebuild(const VerticalLayout &layout, obs_canvas_t *canvas, QString *errorMessage)
{
	if (scene_ && canvas) {
		obs_source_t *sceneSource = source();
		obs_canvas_t *sceneCanvas = sceneSource ? obs_source_get_canvas(sceneSource) : nullptr;
		const bool matchesCanvas = sceneCanvas == canvas;
		if (sceneCanvas)
			obs_canvas_release(sceneCanvas);
		if (!matchesCanvas)
			release();
	}

	if (!scene_) {
		const QByteArray sceneName = sceneName_.toUtf8();
		if (canvas) {
			// A non-ephemeral canvas may have been saved by OBS before a crash.
			// Adopt the restored scene to keep one stable program scene instead of
			// creating a deduplicated copy on every recovery.
			scene_ = obs_canvas_get_scene_by_name(canvas, sceneName.constData());
			if (!scene_)
				scene_ = obs_canvas_scene_create(canvas, sceneName.constData());
			sceneUsesCanvas_ = scene_ != nullptr;
		} else {
#ifdef DSK_ENABLE_OBS_CANVAS_API
			if (errorMessage)
				*errorMessage = "A canvas is required to create a vertical scene.";
			return nullptr;
#else
			scene_ = obs_scene_create_private(sceneName.constData());
			sceneUsesCanvas_ = false;
#endif
		}
		if (!scene_) {
			if (errorMessage)
				*errorMessage = "Failed to create private vertical scene.";
			return nullptr;
		}
	}

	clear();

	// OBS source lists place the front-most item first. Scene rendering draws
	// later-added items on top, so build from the back of the DSK list.
	for (auto it = layout.items.crbegin(); it != layout.items.crend(); ++it) {
		const auto &layoutItem = *it;
		if (!layoutItem.visible || layoutItem.sourceName.isEmpty())
			continue;

		obs_source_t *source = obs_get_source_by_name(layoutItem.sourceName.toUtf8().constData());
		if (!source)
			continue;

		obs_sceneitem_t *item = obs_scene_add(scene_, source);
		if (!item) {
			obs_source_release(source);
			continue;
		}

		applyItemTransform(item, layoutItem);
		obs_sceneitem_addref(item);
		if (obs_sceneitem_t *previous = sceneItems_.take(layoutItem.id))
			obs_sceneitem_release(previous);
		sceneItems_.insert(layoutItem.id, item);
		obs_source_release(source);
	}

	return source();
}

bool VerticalSceneBuilder::updateItemTransforms(const VerticalLayout &layout)
{
	if (!scene_)
		return false;

	QSet<QString> expectedIds;
	for (const auto &layoutItem : layout.items) {
		if (!layoutItem.visible || layoutItem.sourceName.isEmpty())
			continue;
		obs_source_t *source = obs_get_source_by_name(layoutItem.sourceName.toUtf8().constData());
		if (!source)
			continue;
		obs_source_release(source);
		expectedIds.insert(layoutItem.id);
	}

	TransformUpdateData data{&layout, &sceneItems_, &expectedIds, false};
	obs_scene_atomic_update(scene_, updateTransformsAtomically, &data);
	return data.valid;
}

obs_source_t *VerticalSceneBuilder::source() const
{
	return scene_ ? obs_scene_get_source(scene_) : nullptr;
}

void VerticalSceneBuilder::clear()
{
	for (obs_sceneitem_t *item : std::as_const(sceneItems_))
		obs_sceneitem_release(item);
	sceneItems_.clear();
	if (!scene_)
		return;

	QVector<obs_sceneitem_t *> items;
	obs_scene_enum_items(
		scene_,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			auto *items = static_cast<QVector<obs_sceneitem_t *> *>(param);
			obs_sceneitem_addref(item);
			items->push_back(item);
			return true;
		},
		&items);

	for (auto *item : items) {
		obs_sceneitem_remove(item);
		obs_sceneitem_release(item);
	}
}

void VerticalSceneBuilder::release()
{
	clear();
	if (scene_) {
		if (sceneUsesCanvas_)
			obs_canvas_scene_remove(scene_);
		obs_scene_release(scene_);
		scene_ = nullptr;
		sceneUsesCanvas_ = false;
	}
}

} // namespace dsk
