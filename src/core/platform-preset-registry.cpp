#include "core/platform-preset-registry.hpp"

#include "platform-presets-json.hpp"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <utility>

namespace dsk {
namespace {

PlatformPreset presetFromJson(const QJsonObject &object)
{
	PlatformPreset preset;
	preset.id = object.value(QStringLiteral("id")).toString().trimmed();
	preset.displayName = object.value(QStringLiteral("name")).toString().trimmed();
	preset.defaultServer = object.value(QStringLiteral("defaultServer")).toString().trimmed();
	preset.helpUrl = object.value(QStringLiteral("helpUrl")).toString().trimmed();
	preset.recommendedOutput = object.value(QStringLiteral("recommendedOutput")).toString().trimmed();
	preset.horizontalBitrateKbps = object.value(QStringLiteral("horizontalBitrateKbps")).toInt(6000);
	preset.verticalBitrateKbps = object.value(QStringLiteral("verticalBitrateKbps")).toInt(4500);
	preset.note = object.value(QStringLiteral("note")).toString().trimmed();
	preset.verticalCommon = object.value(QStringLiteral("verticalCommon")).toBool(false);
	return preset;
}

PlatformPreset customFallbackPreset()
{
	return {QStringLiteral("custom"),
		QStringLiteral("Custom RTMP"),
		QString(),
		QString(),
		QStringLiteral("dsk-horizontal"),
		6000,
		4500,
		QStringLiteral("Paste the RTMP server URL from the platform."),
		false};
}

} // namespace

PlatformPresetRegistry::PlatformPresetRegistry()
{
	const QJsonDocument document = QJsonDocument::fromJson(QByteArray(kPlatformPresetsJson));
	const QJsonArray platforms = document.object().value(QStringLiteral("platforms")).toArray();
	for (const QJsonValue &value : platforms) {
		if (!value.isObject())
			continue;
		PlatformPreset preset = presetFromJson(value.toObject());
		if (preset.id.isEmpty() || preset.displayName.isEmpty())
			continue;
		presets_.push_back(std::move(preset));
	}
	if (presets_.isEmpty())
		presets_.push_back(customFallbackPreset());
}

const QVector<PlatformPreset> &PlatformPresetRegistry::presets() const
{
	return presets_;
}

PlatformPreset PlatformPresetRegistry::presetById(const QString &id) const
{
	for (const auto &preset : presets_) {
		if (preset.id == id)
			return preset;
	}
	for (const auto &preset : presets_) {
		if (preset.id == QStringLiteral("custom"))
			return preset;
	}
	return presets_.last();
}

} // namespace dsk
