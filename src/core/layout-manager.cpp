#include "core/layout-manager.hpp"

#include "core/output-target.hpp"
#include "core/vertical-layout-geometry.hpp"

#include <algorithm>

namespace dsk {

namespace {

QString uniqueSceneName(const QVector<VerticalLayoutScene> &scenes, const QString &requested,
			const QString &excludedSceneId = {})
{
	const QString base = requested.trimmed().isEmpty() ? QStringLiteral("Vertical Scene") : requested.trimmed();
	auto exists = [&scenes, &excludedSceneId](const QString &name) {
		for (const auto &scene : scenes) {
			if (!excludedSceneId.isEmpty() && scene.id == excludedSceneId)
				continue;
			if (scene.name.compare(name, Qt::CaseInsensitive) == 0)
				return true;
		}
		return false;
	};
	if (!exists(base))
		return base;

	for (int i = 2; i < 1000; ++i) {
		const QString candidate = QString("%1 %2").arg(base).arg(i);
		if (!exists(candidate))
			return candidate;
	}
	return QString("%1 %2").arg(base, newTargetId().left(8));
}

VerticalLayout sanitizedVerticalLayout(VerticalLayout layout)
{
	normalizeVerticalLayoutGeometry(layout);
	for (int i = layout.items.size() - 1; i >= 0; --i) {
		const QString sourceName = layout.items[i].sourceName.trimmed();
		if (sourceName == QStringLiteral("DSK Vertical Scene") ||
		    sourceName == QStringLiteral("DSK Vertical Layout") ||
		    sourceName == QStringLiteral("DSK Vertical Program") ||
		    sourceName == QStringLiteral("DSK Vertical Preview")) {
			layout.items.removeAt(i);
		}
	}
	return layout;
}

} // namespace

QString fitModeToString(FitMode mode)
{
	switch (mode) {
	case FitMode::Fit:
		return "fit";
	case FitMode::Fill:
		return "fill";
	case FitMode::Stretch:
		return "stretch";
	}
	return "fill";
}

FitMode fitModeFromString(const QString &value)
{
	if (value == "fit")
		return FitMode::Fit;
	if (value == "stretch")
		return FitMode::Stretch;
	return FitMode::Fill;
}

const VerticalLayout &LayoutManager::verticalLayout() const
{
	return vertical_;
}

void LayoutManager::initializeVerticalScenes(const QVector<VerticalLayoutScene> &scenes,
					     const QString &activeSceneId,
					     const VerticalLayout &fallbackLayout)
{
	verticalScenes_ = scenes;
	for (auto &scene : verticalScenes_)
		scene.layout = sanitizedVerticalLayout(scene.layout);
	const VerticalLayout cleanFallback = sanitizedVerticalLayout(fallbackLayout);

	if (verticalScenes_.isEmpty()) {
		VerticalLayoutScene scene;
		scene.id = QStringLiteral("default");
		scene.name = QStringLiteral("Default");
		scene.layout = cleanFallback;
		verticalScenes_.push_back(scene);
	}

	activeVerticalSceneId_ = activeSceneId;
	if (activeVerticalSceneId_.isEmpty())
		activeVerticalSceneId_ = verticalScenes_.first().id;
	if (!selectVerticalScene(activeVerticalSceneId_)) {
		activeVerticalSceneId_ = verticalScenes_.first().id;
		vertical_ = verticalScenes_.first().layout;
	}
}

void LayoutManager::setVerticalLayout(const VerticalLayout &layout)
{
	vertical_ = sanitizedVerticalLayout(layout);
	syncActiveSceneLayout();
}

void LayoutManager::applyTemplate(const QString &templateId)
{
	vertical_.templateId = templateId;

	if (vertical_.items.isEmpty())
		return;

	auto applySlot = [this](int index, const QRectF &rect, FitMode fitMode) {
		if (index >= vertical_.items.size())
			return;
		vertical_.items[index].rect = rect;
		vertical_.items[index].fitMode = fitMode;
		vertical_.items[index].visible = true;
	};

	if (templateId == "game-camera") {
		applySlot(0, QRectF(0, 0, 1080, 1920), FitMode::Fill);
		applySlot(1, QRectF(670, 1240, 360, 360), FitMode::Fill);
		syncActiveSceneLayout();
		return;
	}

	if (templateId == "camera-first") {
		applySlot(0, QRectF(0, 0, 1080, 1420), FitMode::Fill);
		applySlot(1, QRectF(80, 1480, 920, 360), FitMode::Fit);
		syncActiveSceneLayout();
		return;
	}

	if (templateId == "center-crop") {
		applySlot(0, QRectF(0, 0, 1080, 1920), FitMode::Fill);
		syncActiveSceneLayout();
		return;
	}

	vertical_.templateId = "full-screen";
	applySlot(0, QRectF(0, 0, 1080, 1920), FitMode::Fit);
	syncActiveSceneLayout();
}

const QVector<VerticalLayoutScene> &LayoutManager::verticalScenes() const
{
	return verticalScenes_;
}

const QString &LayoutManager::activeVerticalSceneId() const
{
	return activeVerticalSceneId_;
}

QString LayoutManager::activeVerticalSceneName() const
{
	return verticalSceneName(activeVerticalSceneId_);
}

QString LayoutManager::verticalSceneName(const QString &id) const
{
	for (const auto &scene : verticalScenes_) {
		if (scene.id == id)
			return scene.name;
	}
	return {};
}

QString LayoutManager::createVerticalScene(const QString &name)
{
	VerticalLayoutScene scene;
	scene.id = newTargetId();
	scene.name = uniqueSceneName(verticalScenes_, name);
	scene.layout = VerticalLayout{};
	verticalScenes_.push_back(scene);
	activeVerticalSceneId_ = scene.id;
	vertical_ = scene.layout;
	return scene.id;
}

bool LayoutManager::removeVerticalScene(const QString &id)
{
	if (verticalScenes_.size() <= 1)
		return false;

	for (int i = 0; i < verticalScenes_.size(); ++i) {
		if (verticalScenes_[i].id != id)
			continue;
		const bool removedActiveScene = activeVerticalSceneId_ == id;
		verticalScenes_.removeAt(i);
		if (removedActiveScene) {
			const int replacementIndex = std::min(i, int(verticalScenes_.size()) - 1);
			activeVerticalSceneId_ = verticalScenes_[replacementIndex].id;
			vertical_ = verticalScenes_[replacementIndex].layout;
		}
		return true;
	}
	return false;
}

bool LayoutManager::renameVerticalScene(const QString &id, const QString &name)
{
	const QString requested = name.trimmed();
	if (requested.isEmpty())
		return false;
	for (auto &scene : verticalScenes_) {
		if (scene.id != id)
			continue;
		scene.name = uniqueSceneName(verticalScenes_, requested, id);
		return true;
	}
	return false;
}

bool LayoutManager::moveVerticalScene(const QString &id, int offset)
{
	if (offset == 0)
		return false;
	for (int i = 0; i < verticalScenes_.size(); ++i) {
		if (verticalScenes_[i].id != id)
			continue;
		const int target = std::clamp(i + offset, 0, int(verticalScenes_.size()) - 1);
		if (target == i)
			return false;
		verticalScenes_.move(i, target);
		return true;
	}
	return false;
}

bool LayoutManager::selectVerticalScene(const QString &id)
{
	for (const auto &scene : verticalScenes_) {
		if (scene.id != id)
			continue;
		activeVerticalSceneId_ = scene.id;
		vertical_ = scene.layout;
		return true;
	}
	return false;
}

void LayoutManager::ensureDefaultScene()
{
	if (!verticalScenes_.isEmpty())
		return;
	VerticalLayoutScene scene;
	scene.id = QStringLiteral("default");
	scene.name = QStringLiteral("Default");
	scene.layout = vertical_;
	verticalScenes_.push_back(scene);
	activeVerticalSceneId_ = scene.id;
}

void LayoutManager::syncActiveSceneLayout()
{
	ensureDefaultScene();
	for (auto &scene : verticalScenes_) {
		if (scene.id == activeVerticalSceneId_) {
			scene.layout = vertical_;
			return;
		}
	}
	verticalScenes_.first().layout = vertical_;
	activeVerticalSceneId_ = verticalScenes_.first().id;
}

} // namespace dsk
