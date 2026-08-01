#include "core/settings-codec.hpp"
#include "core/vertical-layout-geometry.hpp"

#include <QJsonArray>
#include <QJsonObject>

namespace dsk {

OutputTarget outputTargetFromJson(const QJsonObject &object)
{
	OutputTarget target;
	outputTargetFromJsonInto(object, target);
	return target;
}

void outputTargetFromJsonInto(const QJsonObject &object, OutputTarget &target)
{
	target.id = object.value("id").toString(newTargetId());
	target.name = object.value("name").toString("Untitled");
	target.platformId = object.value("platformId").toString("custom");
	target.authMode = targetAuthModeFromString(object.value("authMode").toString("manual-rtmp"));
	target.youtubeBroadcastMode = youtubeBroadcastModeFromString(
		object.value("youtubeBroadcastMode").toString("normal"));
	target.authAccountName = object.value("authAccountName").toString();
	target.authCredentialRef = object.value("authCredentialRef").toString();
	target.oauthClientId = object.value("oauthClientId").toString();
	target.oauthClientSecret = object.value("oauthClientSecret").toString();
	target.oauthClientSecretRef = object.value("oauthClientSecretRef").toString();
	target.oauthRefreshToken = object.value("oauthRefreshToken").toString();
	target.oauthRefreshTokenRef = object.value("oauthRefreshTokenRef").toString();
	target.serverUrl = object.value("serverUrl").toString();
	target.streamKey = object.value("streamKey").toString();
	target.encoderGroup = encoderGroupFromString(object.value("encoderGroup").toString("dsk-horizontal"));
	target.useSharedEncoder = object.value("useSharedEncoder").toBool(true);
	target.autoStartWithObs = object.value("autoStartWithObs").toBool(false);
	target.autoStopWithObs = object.value("autoStopWithObs").toBool(true);
	target.reconnectEnabled = object.value("reconnectEnabled").toBool(true);
	target.reconnectMaxRetries = object.value("reconnectMaxRetries").toInt(20);
	target.reconnectDelaySeconds = object.value("reconnectDelaySeconds").toInt(2);
	target.videoBitrateKbps = object.value("videoBitrateKbps").toInt(0);
	target.audioBitrateKbps = object.value("audioBitrateKbps").toInt(0);
	target.keyframeSeconds = object.value("keyframeSeconds").toInt(2);
	target.videoEncoderId = object.value("videoEncoderId").toString();
	target.audioEncoderId = object.value("audioEncoderId").toString();
	target.sceneMode = targetSceneModeFromString(object.value("sceneMode").toString("follow-obs"));
	target.sceneName = object.value("sceneName").toString();
	target.sceneUuid = object.value("sceneUuid").toString();
	const QJsonArray routes = object.value("sceneRoutes").toArray();
	target.sceneRoutes.clear();
	for (const QJsonValue &value : routes) {
		if (!value.isObject())
			continue;
		const QJsonObject route = value.toObject();
		TargetSceneRoute decoded;
		decoded.obsSceneName = route.value("obsSceneName").toString().trimmed();
		decoded.obsSceneUuid = route.value("obsSceneUuid").toString().trimmed();
		decoded.outputSceneName = route.value("outputSceneName").toString().trimmed();
		decoded.outputSceneUuid = route.value("outputSceneUuid").toString().trimmed();
		if (!decoded.obsSceneName.isEmpty() && !decoded.outputSceneName.isEmpty())
			target.sceneRoutes.push_back(decoded);
	}
	target.enabled = object.value("enabled").toBool(true);
	target.startWithAll = object.value("startWithAll").toBool(true);
}

QJsonObject outputTargetToJson(const OutputTarget &target)
{
	QJsonObject object;
	object.insert("id", target.id);
	object.insert("name", target.name);
	object.insert("platformId", target.platformId);
	object.insert("authMode", targetAuthModeToString(target.authMode));
	object.insert("youtubeBroadcastMode", youtubeBroadcastModeToString(target.youtubeBroadcastMode));
	object.insert("authAccountName", target.authAccountName);
	object.insert("authCredentialRef", target.authCredentialRef);
	object.insert("oauthClientId", target.oauthClientId);
	object.insert("oauthClientSecret", target.oauthClientSecret);
	object.insert("oauthClientSecretRef", target.oauthClientSecretRef);
	object.insert("oauthRefreshToken", target.oauthRefreshToken);
	object.insert("oauthRefreshTokenRef", target.oauthRefreshTokenRef);
	object.insert("serverUrl", target.serverUrl);
	object.insert("streamKey", target.streamKey);
	object.insert("encoderGroup", encoderGroupToString(target.encoderGroup));
	object.insert("useSharedEncoder", target.useSharedEncoder);
	object.insert("autoStartWithObs", target.autoStartWithObs);
	object.insert("autoStopWithObs", target.autoStopWithObs);
	object.insert("reconnectEnabled", target.reconnectEnabled);
	object.insert("reconnectMaxRetries", target.reconnectMaxRetries);
	object.insert("reconnectDelaySeconds", target.reconnectDelaySeconds);
	object.insert("videoBitrateKbps", target.videoBitrateKbps);
	object.insert("audioBitrateKbps", target.audioBitrateKbps);
	object.insert("keyframeSeconds", target.keyframeSeconds);
	object.insert("videoEncoderId", target.videoEncoderId);
	object.insert("audioEncoderId", target.audioEncoderId);
	object.insert("sceneMode", targetSceneModeToString(target.sceneMode));
	object.insert("sceneName", target.sceneName);
	object.insert("sceneUuid", target.sceneUuid);
	QJsonArray routes;
	for (const auto &route : target.sceneRoutes) {
		if (route.obsSceneName.trimmed().isEmpty() || route.outputSceneName.trimmed().isEmpty())
			continue;
		QJsonObject routeObject;
		routeObject.insert("obsSceneName", route.obsSceneName.trimmed());
		routeObject.insert("obsSceneUuid", route.obsSceneUuid.trimmed());
		routeObject.insert("outputSceneName", route.outputSceneName.trimmed());
		routeObject.insert("outputSceneUuid", route.outputSceneUuid.trimmed());
		routes.push_back(routeObject);
	}
	object.insert("sceneRoutes", routes);
	object.insert("enabled", target.enabled);
	object.insert("startWithAll", target.startWithAll);
	return object;
}

SceneLayoutLink sceneLayoutLinkFromJson(const QJsonObject &object)
{
	SceneLayoutLink link;
	link.sceneName = object.value("sceneName").toString();
	link.sceneUuid = object.value("sceneUuid").toString();
	link.verticalSceneId = object.value("verticalSceneId").toString();
	link.legacyTemplateId = object.value("templateId").toString();
	return link;
}

QJsonObject sceneLayoutLinkToJson(const SceneLayoutLink &link)
{
	QJsonObject object;
	object.insert("sceneName", link.sceneName);
	object.insert("sceneUuid", link.sceneUuid);
	object.insert("verticalSceneId", link.verticalSceneId);
	return object;
}

VerticalLayout verticalLayoutFromJson(const QJsonObject &object)
{
	VerticalLayout layout;
	layout.width = object.value("width").toInt(1080);
	layout.height = object.value("height").toInt(1920);
	layout.templateId = object.value("templateId").toString("full-screen");

	const QJsonArray items = object.value("items").toArray();
	for (const QJsonValue &value : items) {
		if (!value.isObject())
			continue;
		const QJsonObject itemObject = value.toObject();
		const QJsonObject rect = itemObject.value("rect").toObject();
		const QJsonObject crop = itemObject.value("crop").toObject();
		layout.items.push_back({
			itemObject.value("id").toString(newTargetId()),
			itemObject.value("sourceName").toString(),
			QRectF(rect.value("x").toDouble(), rect.value("y").toDouble(), rect.value("w").toDouble(1080), rect.value("h").toDouble(1920)),
			QRectF(crop.value("left").toDouble(), crop.value("top").toDouble(), crop.value("right").toDouble(), crop.value("bottom").toDouble()),
			fitModeFromString(itemObject.value("fitMode").toString("fill")),
			itemObject.value("visible").toBool(true),
		});
	}
	return layout;
}

QJsonObject verticalLayoutToJson(const VerticalLayout &layout)
{
	QJsonObject object;
	object.insert("width", layout.width);
	object.insert("height", layout.height);
	object.insert("templateId", layout.templateId);

	QJsonArray items;
	for (const auto &item : layout.items) {
		QJsonObject rect;
		rect.insert("x", item.rect.x());
		rect.insert("y", item.rect.y());
		rect.insert("w", item.rect.width());
		rect.insert("h", item.rect.height());

		QJsonObject crop;
		crop.insert("left", item.crop.x());
		crop.insert("top", item.crop.y());
		crop.insert("right", item.crop.width());
		crop.insert("bottom", item.crop.height());

		QJsonObject itemObject;
		itemObject.insert("id", item.id);
		itemObject.insert("sourceName", item.sourceName);
		itemObject.insert("rect", rect);
		itemObject.insert("crop", crop);
		itemObject.insert("fitMode", fitModeToString(item.fitMode));
		itemObject.insert("visible", item.visible);
		items.push_back(itemObject);
	}
	object.insert("items", items);
	return object;
}

VerticalLayoutScene verticalLayoutSceneFromJson(const QJsonObject &object)
{
	VerticalLayoutScene scene;
	scene.id = object.value("id").toString(newTargetId());
	scene.name = object.value("name").toString("Vertical Scene");
	scene.layout = verticalLayoutFromJson(object.value("layout").toObject());
	normalizeLoadedVerticalLayout(scene.layout);
	return scene;
}

QJsonObject verticalLayoutSceneToJson(const VerticalLayoutScene &scene)
{
	QJsonObject object;
	object.insert("id", scene.id);
	object.insert("name", scene.name);
	object.insert("layout", verticalLayoutToJson(scene.layout));
	return object;
}

void normalizeLoadedVerticalLayout(VerticalLayout &layout)
{
	normalizeVerticalLayoutGeometry(layout);
	for (int i = layout.items.size() - 1; i >= 0; --i) {
		if (layout.items[i].sourceName.trimmed().isEmpty())
			layout.items.removeAt(i);
	}

	if (layout.items.size() != 1)
		return;

	const auto &item = layout.items[0];
	if (item.sourceName.isEmpty() && item.visible && item.fitMode == FitMode::Fit &&
	    item.rect == QRectF(0, 0, 1080, 1920) && item.crop == QRectF(0, 0, 0, 0))
		layout.items.clear();
}

} // namespace dsk
