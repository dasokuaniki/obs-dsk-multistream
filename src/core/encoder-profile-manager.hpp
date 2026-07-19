#pragma once

#include "core/output-target.hpp"

#include <QString>

namespace dsk {

struct EncoderProfile {
	EncoderGroup group = EncoderGroup::DskHorizontal;
	int width = 1920;
	int height = 1080;
	int fpsNumerator = 60;
	int fpsDenominator = 1;
	int videoBitrateKbps = 6000;
	int audioBitrateKbps = 160;
	int keyframeSeconds = 2;
	QString videoEncoderId;
	QString audioEncoderId;
};

class EncoderProfileManager {
public:
	EncoderProfile profileFor(EncoderGroup group) const;
	void setProfile(const EncoderProfile &profile);

private:
	EncoderProfile horizontal_{EncoderGroup::DskHorizontal, 1920, 1080, 60, 1, 6000, 160, 2, {}, {}};
	EncoderProfile vertical_{EncoderGroup::DskVertical, 1080, 1920, 60, 1, 6000, 160, 2, {}, {}};
};

} // namespace dsk
