#include "core/settings-store.hpp"
#include "core/diagnostics.hpp"
#include "core/settings-codec.hpp"
#include "core/secret-store.hpp"

#ifndef DSK_SETTINGS_STORE_STANDALONE
#include <obs-frontend-api.h>
#include <obs-module.h>
#endif

#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <cmath>
#include <initializer_list>
#include <utility>

namespace dsk {
namespace {

struct PendingSecretWrite {
	QString credentialRef;
	QString secret;
	QString previousSecret;
	bool hadPreviousSecret = false;
	bool applied = false;
};

void rollbackSecretWrites(const QVector<PendingSecretWrite> &writes)
{
	SecretStore secrets;
	for (auto it = writes.crbegin(); it != writes.crend(); ++it) {
		if (!it->applied)
			continue;

		QString error;
		if (it->hadPreviousSecret) {
			if (!secrets.writeSecret(it->credentialRef, it->previousSecret, &error))
				logWarning(QString("Failed to restore DSK credential %1 after settings save failure: %2")
						   .arg(it->credentialRef, error));
		} else if (!secrets.deleteSecret(it->credentialRef, &error)) {
			logWarning(QString("Failed to remove newly written DSK credential %1 after settings save failure: %2")
					   .arg(it->credentialRef, error));
		}
	}
}

bool applySecretWrites(QVector<PendingSecretWrite> &writes, QString *errorMessage)
{
	SecretStore secrets;
	for (auto &write : writes) {
		QString readError;
		const SecretReadResult readResult =
			secrets.readSecretResult(write.credentialRef, &write.previousSecret, &readError);
		if (readResult == SecretReadResult::Error) {
			rollbackSecretWrites(writes);
			if (errorMessage)
				*errorMessage = QStringLiteral("Could not read the existing credential before update: %1")
						.arg(readError);
			return false;
		}
		write.hadPreviousSecret = readResult == SecretReadResult::Found;
		if (write.hadPreviousSecret && write.previousSecret == write.secret)
			continue;

		QString writeError;
		if (!secrets.writeSecret(write.credentialRef, write.secret, &writeError)) {
			rollbackSecretWrites(writes);
			if (errorMessage)
				*errorMessage = writeError;
			return false;
		}
		write.applied = true;
	}
	return true;
}

bool hasWrongType(const QJsonObject &object, QJsonValue::Type type,
		  std::initializer_list<const char *> keys)
{
	for (const char *key : keys) {
		const QJsonValue value = object.value(QString::fromLatin1(key));
		if (!value.isUndefined() && value.type() != type)
			return true;
	}
	return false;
}

bool hasNonIntegerNumber(const QJsonObject &object, std::initializer_list<const char *> keys)
{
	for (const char *key : keys) {
		const QJsonValue value = object.value(QString::fromLatin1(key));
		if (value.isUndefined())
			continue;
		if (!value.isDouble() || std::floor(value.toDouble()) != value.toDouble())
			return true;
	}
	return false;
}

QString validateLayoutObject(const QJsonObject &layout)
{
	if (hasNonIntegerNumber(layout, {"width", "height"}) ||
	    hasWrongType(layout, QJsonValue::String, {"templateId"}))
		return QStringLiteral("vertical layout fields have invalid types");

	const QJsonValue itemsValue = layout.value(QStringLiteral("items"));
	if (!itemsValue.isUndefined() && !itemsValue.isArray())
		return QStringLiteral("vertical layout items are not an array");
	for (const QJsonValue &value : itemsValue.toArray()) {
		if (!value.isObject())
			return QStringLiteral("vertical layout items contain a non-object value");
		const QJsonObject item = value.toObject();
		if (hasWrongType(item, QJsonValue::String, {"id", "sourceName", "fitMode"}) ||
		    hasWrongType(item, QJsonValue::Bool, {"visible"}) ||
		    hasWrongType(item, QJsonValue::Object, {"rect", "crop"}))
			return QStringLiteral("vertical layout item fields have invalid types");
		for (const char *field : {"rect", "crop"}) {
			const QJsonValue geometryValue = item.value(QString::fromLatin1(field));
			if (geometryValue.isUndefined())
				continue;
			const QJsonObject geometry = geometryValue.toObject();
			if (hasWrongType(geometry, QJsonValue::Double,
					 QString::fromLatin1(field) == QStringLiteral("rect")
						 ? std::initializer_list<const char *>{"x", "y", "w", "h"}
						 : std::initializer_list<const char *>{"left", "top", "right", "bottom"}))
				return QStringLiteral("vertical layout geometry has invalid types");
		}
	}
	return {};
}

} // namespace

SettingsStore::SettingsStore(QString pathOverride) : pathOverride_(std::move(pathOverride))
{
}

QString SettingsStore::settingsPath() const
{
	if (!pathOverride_.trimmed().isEmpty())
		return QDir::toNativeSeparators(pathOverride_);

#ifdef DSK_SETTINGS_STORE_STANDALONE
	return QDir::toNativeSeparators(QDir::temp().filePath(QStringLiteral("dsk-multistream-test.json")));
#else
	char *profilePath = obs_frontend_get_current_profile_path();
	if (profilePath) {
		QString path = QString::fromUtf8(profilePath) + "/dsk-multistream.json";
		bfree(profilePath);
		return QDir::toNativeSeparators(path);
	}

	char *modulePath = obs_module_config_path("settings.json");
	QString path = modulePath ? QString::fromUtf8(modulePath) : QStringLiteral("dsk-multistream.json");
	if (modulePath)
		bfree(modulePath);
	return QDir::toNativeSeparators(path);
#endif
}

PluginSettings SettingsStore::load()
{
	PluginSettings settings;
	saveBlocked_ = false;
	lastLoadWarning_.clear();

	const QString path = settingsPath();
	const auto quarantineInvalidSettings = [this, &path](const QString &detail) {
		const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
		QString backupPath = QStringLiteral("%1.corrupt-%2.json").arg(path, stamp);
		for (int suffix = 2; QFileInfo::exists(backupPath); ++suffix)
			backupPath = QStringLiteral("%1.corrupt-%2-%3.json").arg(path, stamp).arg(suffix);

		PluginSettings empty;
		if (QFile::rename(path, backupPath)) {
			lastLoadWarning_ = QStringLiteral("Invalid DSK settings (%1) were preserved at %2. DSK started with empty settings.")
						   .arg(detail, backupPath);
			logWarning(lastLoadWarning_);
		} else {
			saveBlocked_ = true;
			lastLoadWarning_ = QStringLiteral("DSK settings are invalid (%1) and could not be preserved. Saving is disabled: %2")
						   .arg(detail, path);
			logError(lastLoadWarning_);
		}
		return empty;
	};

	QFile file(path);
	if (!file.exists())
		return settings;
	if (!file.open(QIODevice::ReadOnly)) {
		saveBlocked_ = true;
		lastLoadWarning_ = QStringLiteral("Could not read DSK settings at %1: %2. Saving is disabled to protect the existing file.")
					   .arg(path, file.errorString());
		logError(lastLoadWarning_);
		return settings;
	}

	QJsonParseError parseError{};
	const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
	file.close();
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		const QString detail = parseError.error == QJsonParseError::NoError
			? QStringLiteral("root is not an object")
			: parseError.errorString();
		return quarantineInvalidSettings(detail);
	}

	const QJsonObject root = document.object();
	const QJsonValue schemaValue = root.value(QStringLiteral("schemaVersion"));
	if (!schemaValue.isUndefined() && !schemaValue.isDouble())
		return quarantineInvalidSettings(QStringLiteral("schemaVersion is not a number"));
	const double schemaNumber = schemaValue.isUndefined() ? 1.0 : schemaValue.toDouble();
	if (schemaNumber < 1.0 || std::floor(schemaNumber) != schemaNumber)
		return quarantineInvalidSettings(QStringLiteral("schemaVersion is not a positive integer"));
	if (schemaNumber > 1.0) {
		saveBlocked_ = true;
		lastLoadWarning_ = QStringLiteral("DSK settings use newer schema version %1. Saving is disabled to avoid data loss.")
					   .arg(schemaNumber, 0, 'g', 16);
		logError(lastLoadWarning_);
		return settings;
	}

	const auto arrayHasNonObject = [](const QJsonArray &array) {
		for (const QJsonValue &value : array) {
			if (!value.isObject())
				return true;
		}
		return false;
	};
	if ((root.contains(QStringLiteral("targets")) && !root.value(QStringLiteral("targets")).isArray()) ||
	    (root.contains(QStringLiteral("verticalLayout")) && !root.value(QStringLiteral("verticalLayout")).isObject()) ||
	    (root.contains(QStringLiteral("verticalScenes")) && !root.value(QStringLiteral("verticalScenes")).isArray()) ||
	    (root.contains(QStringLiteral("activeVerticalSceneId")) && !root.value(QStringLiteral("activeVerticalSceneId")).isString()) ||
	    (root.contains(QStringLiteral("followObsScene")) && !root.value(QStringLiteral("followObsScene")).isBool()) ||
	    (root.contains(QStringLiteral("sceneLinks")) && !root.value(QStringLiteral("sceneLinks")).isArray())) {
		return quarantineInvalidSettings(QStringLiteral("schema 1 field types are invalid"));
	}

	const QJsonArray targetValues = root.value(QStringLiteral("targets")).toArray();
	const QJsonArray verticalSceneValues = root.value(QStringLiteral("verticalScenes")).toArray();
	const QJsonArray sceneLinkValues = root.value(QStringLiteral("sceneLinks")).toArray();
	if (arrayHasNonObject(targetValues) || arrayHasNonObject(verticalSceneValues) || arrayHasNonObject(sceneLinkValues))
		return quarantineInvalidSettings(QStringLiteral("schema 1 arrays contain non-object values"));
	const QJsonObject verticalLayoutObject = root.value(QStringLiteral("verticalLayout")).toObject();
	const QString verticalLayoutError = validateLayoutObject(verticalLayoutObject);
	if (!verticalLayoutError.isEmpty())
		return quarantineInvalidSettings(verticalLayoutError);
	for (const QJsonValue &value : targetValues) {
		const QJsonObject target = value.toObject();
		if (hasWrongType(target,
				 QJsonValue::String,
				 {"id", "name", "platformId", "authMode", "authAccountName",
				  "authCredentialRef", "oauthClientId", "oauthClientSecret",
				  "oauthClientSecretRef", "oauthRefreshToken", "oauthRefreshTokenRef",
				  "serverUrl", "streamKey", "encoderGroup", "videoEncoderId",
				  "audioEncoderId", "sceneMode", "sceneName", "sceneUuid"}) ||
		    hasWrongType(target,
				 QJsonValue::Bool,
				 {"useSharedEncoder", "autoStartWithObs", "autoStopWithObs",
				  "reconnectEnabled", "enabled", "startWithAll"}) ||
		    hasNonIntegerNumber(target,
					{"reconnectMaxRetries", "reconnectDelaySeconds", "videoBitrateKbps",
					 "audioBitrateKbps", "keyframeSeconds"}))
			return quarantineInvalidSettings(QStringLiteral("target fields have invalid types"));
		if (target.contains(QStringLiteral("sceneRoutes")) &&
		    (!target.value(QStringLiteral("sceneRoutes")).isArray() ||
		     arrayHasNonObject(target.value(QStringLiteral("sceneRoutes")).toArray()))) {
			return quarantineInvalidSettings(QStringLiteral("target scene routes are invalid"));
		}
		for (const QJsonValue &routeValue : target.value(QStringLiteral("sceneRoutes")).toArray()) {
			if (hasWrongType(routeValue.toObject(), QJsonValue::String,
					 {"obsSceneName", "obsSceneUuid", "outputSceneName", "outputSceneUuid"}))
				return quarantineInvalidSettings(QStringLiteral("target scene route fields have invalid types"));
		}
	}
	for (const QJsonValue &value : verticalSceneValues) {
		const QJsonObject scene = value.toObject();
		if (hasWrongType(scene, QJsonValue::String, {"id", "name"}))
			return quarantineInvalidSettings(QStringLiteral("vertical scene fields have invalid types"));
		if (!scene.value(QStringLiteral("layout")).isObject())
			return quarantineInvalidSettings(QStringLiteral("vertical scene layout is invalid"));
		const QString sceneLayoutError = validateLayoutObject(scene.value(QStringLiteral("layout")).toObject());
		if (!sceneLayoutError.isEmpty())
			return quarantineInvalidSettings(sceneLayoutError);
	}
	for (const QJsonValue &value : sceneLinkValues) {
		if (hasWrongType(value.toObject(), QJsonValue::String,
				 {"sceneName", "sceneUuid", "verticalSceneId", "templateId"}))
			return quarantineInvalidSettings(QStringLiteral("scene link fields have invalid types"));
	}
	const QJsonArray targets = targetValues;
	settings.targets.reserve(targets.size());
	for (const QJsonValue &value : targets) {
		if (!value.isObject())
			continue;

		settings.targets.resize(settings.targets.size() + 1);
		outputTargetFromJsonInto(value.toObject(), settings.targets.last());
	}

	const QJsonValue layout = root.value("verticalLayout");
	if (layout.isObject())
		settings.verticalLayout = verticalLayoutFromJson(layout.toObject());
	normalizeLoadedVerticalLayout(settings.verticalLayout);

	settings.activeVerticalSceneId = root.value("activeVerticalSceneId").toString();
	const QJsonArray verticalScenes = verticalSceneValues;
	for (const QJsonValue &value : verticalScenes) {
		if (value.isObject())
			settings.verticalScenes.push_back(verticalLayoutSceneFromJson(value.toObject()));
	}

	settings.followObsScene = root.value("followObsScene").toBool(false);
	const QJsonArray links = sceneLinkValues;
	for (const QJsonValue &value : links) {
		if (value.isObject())
			settings.sceneLinks.push_back(sceneLayoutLinkFromJson(value.toObject()));
	}

	return settings;
}

QString SettingsStore::lastLoadWarning() const
{
	return lastLoadWarning_;
}

bool SettingsStore::saveBlocked() const
{
	return saveBlocked_;
}

bool SettingsStore::save(const PluginSettings &settings, QString *errorMessage) const
{
	return save(settings.targets,
		    settings.verticalLayout,
		    settings.verticalScenes,
		    settings.activeVerticalSceneId,
		    settings.followObsScene,
		    settings.sceneLinks,
		    errorMessage);
}

bool SettingsStore::save(const QVector<OutputTarget> &sourceTargets,
			 const VerticalLayout &verticalLayout,
			 const QVector<VerticalLayoutScene> &verticalScenes,
			 const QString &activeVerticalSceneId,
			 bool followObsScene,
			 const QVector<SceneLayoutLink> &sceneLinks,
			 QString *errorMessage) const
{
	if (errorMessage)
		errorMessage->clear();
	if (saveBlocked_) {
		if (errorMessage)
			*errorMessage = lastLoadWarning_.isEmpty() ? QStringLiteral("Saving is disabled to protect the existing settings file.")
								 : lastLoadWarning_;
		return false;
	}

	QJsonObject root;
	root.insert("schemaVersion", 1);

	QJsonArray targets;
	QVector<PendingSecretWrite> pendingSecrets;
	for (const auto &target : sourceTargets) {
		QString credentialRef = target.authCredentialRef;
		QString streamKey = target.streamKey;
		QString oauthClientSecretRef = target.oauthClientSecretRef;
		QString oauthClientSecret = target.oauthClientSecret;
		QString oauthRefreshTokenRef = target.oauthRefreshTokenRef;
		QString oauthRefreshToken = target.oauthRefreshToken;
		if (!streamKey.isEmpty()) {
			if (credentialRef.isEmpty())
				credentialRef = SecretStore::streamKeyCredentialRef(target.id);

			pendingSecrets.push_back({credentialRef, streamKey});
			streamKey.clear();
		}
		if (!oauthClientSecret.isEmpty()) {
			if (oauthClientSecretRef.isEmpty())
				oauthClientSecretRef = SecretStore::oauthClientSecretCredentialRef(target.id);

			pendingSecrets.push_back({oauthClientSecretRef, oauthClientSecret});
			oauthClientSecret.clear();
		}
		if (!oauthRefreshToken.isEmpty()) {
			if (oauthRefreshTokenRef.isEmpty())
				oauthRefreshTokenRef = SecretStore::oauthRefreshTokenCredentialRef(target.id);

			pendingSecrets.push_back({oauthRefreshTokenRef, oauthRefreshToken});
			oauthRefreshToken.clear();
		}

		QJsonObject object;
		object.insert("id", target.id);
		object.insert("name", target.name);
		object.insert("platformId", target.platformId);
		object.insert("authMode", targetAuthModeToString(target.authMode));
		if (!target.authAccountName.isEmpty())
			object.insert("authAccountName", target.authAccountName);
		if (!credentialRef.isEmpty())
			object.insert("authCredentialRef", credentialRef);
		if (!target.oauthClientId.isEmpty())
			object.insert("oauthClientId", target.oauthClientId);
		if (!oauthClientSecretRef.isEmpty())
			object.insert("oauthClientSecretRef", oauthClientSecretRef);
		if (!oauthRefreshTokenRef.isEmpty())
			object.insert("oauthRefreshTokenRef", oauthRefreshTokenRef);
		object.insert("oauthClientSecret", oauthClientSecret);
		object.insert("oauthRefreshToken", oauthRefreshToken);
		object.insert("serverUrl", target.serverUrl);
		object.insert("streamKey", streamKey);
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
		if (!target.videoEncoderId.isEmpty())
			object.insert("videoEncoderId", target.videoEncoderId);
		if (!target.audioEncoderId.isEmpty())
			object.insert("audioEncoderId", target.audioEncoderId);
		object.insert("sceneMode", targetSceneModeToString(target.sceneMode));
		object.insert("sceneName", target.sceneName);
		object.insert("sceneUuid", target.sceneUuid);
		QJsonArray routes;
		for (const auto &route : target.sceneRoutes) {
			const QString obsSceneName = route.obsSceneName.trimmed();
			const QString outputSceneName = route.outputSceneName.trimmed();
			if (obsSceneName.isEmpty() || outputSceneName.isEmpty())
				continue;
			QJsonObject routeObject;
			routeObject.insert("obsSceneName", obsSceneName);
			routeObject.insert("obsSceneUuid", route.obsSceneUuid.trimmed());
			routeObject.insert("outputSceneName", outputSceneName);
			routeObject.insert("outputSceneUuid", route.outputSceneUuid.trimmed());
			routes.push_back(routeObject);
		}
		object.insert("sceneRoutes", routes);
		object.insert("enabled", target.enabled);
		object.insert("startWithAll", target.startWithAll);
		targets.push_back(object);
	}
	root.insert("targets", targets);
	root.insert("verticalLayout", verticalLayoutToJson(verticalLayout));
	QJsonArray dskVerticalScenes;
	for (const auto &scene : verticalScenes)
		dskVerticalScenes.push_back(verticalLayoutSceneToJson(scene));
	root.insert("verticalScenes", dskVerticalScenes);
	root.insert("activeVerticalSceneId", activeVerticalSceneId);
	root.insert("followObsScene", followObsScene);
	QJsonArray links;
	for (const auto &link : sceneLinks)
		links.push_back(sceneLayoutLinkToJson(link));
	root.insert("sceneLinks", links);

	const QString path = settingsPath();
	QDir().mkpath(QFileInfo(path).absolutePath());
	if (QFileInfo::exists(path)) {
		const QString backupPath = path + QStringLiteral(".bak");
		QFile source(path);
		QSaveFile backup(backupPath);
		if (source.open(QIODevice::ReadOnly) && backup.open(QIODevice::WriteOnly)) {
			const QByteArray previousPayload = source.readAll();
			if (backup.write(previousPayload) != previousPayload.size() || !backup.commit()) {
				backup.cancelWriting();
				if (errorMessage)
					*errorMessage = QStringLiteral("Could not preserve the previous DSK settings at %1: %2")
							.arg(backupPath, backup.errorString());
				return false;
			}
		} else {
			if (errorMessage) {
				const QString detail = source.isOpen() ? backup.errorString() : source.errorString();
				*errorMessage = QStringLiteral("Could not create a safety backup for DSK settings at %1: %2")
						.arg(backupPath, detail);
			}
			return false;
		}
	}

	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		if (errorMessage)
			*errorMessage = file.errorString();
		return false;
	}

	const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
	if (file.write(payload) != payload.size()) {
		if (errorMessage)
			*errorMessage = file.errorString();
		file.cancelWriting();
		return false;
	}
	QString secretError;
	if (!applySecretWrites(pendingSecrets, &secretError)) {
		if (errorMessage)
			*errorMessage = QString("Failed to save secret to OS credential storage: %1").arg(secretError);
		file.cancelWriting();
		return false;
	}
	if (!file.commit()) {
		rollbackSecretWrites(pendingSecrets);
		if (errorMessage)
			*errorMessage = file.errorString();
		return false;
	}
	return true;
}

} // namespace dsk
