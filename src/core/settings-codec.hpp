#pragma once

#include "core/layout-manager.hpp"
#include "core/output-target.hpp"

class QJsonObject;

namespace dsk {

OutputTarget outputTargetFromJson(const QJsonObject &object);
void outputTargetFromJsonInto(const QJsonObject &object, OutputTarget &target);
QJsonObject outputTargetToJson(const OutputTarget &target);
SceneLayoutLink sceneLayoutLinkFromJson(const QJsonObject &object);
QJsonObject sceneLayoutLinkToJson(const SceneLayoutLink &link);
VerticalLayout verticalLayoutFromJson(const QJsonObject &object);
QJsonObject verticalLayoutToJson(const VerticalLayout &layout);
VerticalLayoutScene verticalLayoutSceneFromJson(const QJsonObject &object);
QJsonObject verticalLayoutSceneToJson(const VerticalLayoutScene &scene);
void normalizeLoadedVerticalLayout(VerticalLayout &layout);

} // namespace dsk
