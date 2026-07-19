#pragma once

#include <QString>
#include <QVector>

namespace dsk {

struct PlatformPreset {
	QString id;
	QString displayName;
	QString defaultServer;
	QString helpUrl;
	QString recommendedOutput;
	int horizontalBitrateKbps = 6000;
	int verticalBitrateKbps = 4500;
	QString note;
	bool verticalCommon = false;
};

class PlatformPresetRegistry {
public:
	PlatformPresetRegistry();

	const QVector<PlatformPreset> &presets() const;
	PlatformPreset presetById(const QString &id) const;

private:
	QVector<PlatformPreset> presets_;
};

} // namespace dsk
