#include "core/encoder-profile-manager.hpp"

namespace dsk {

EncoderProfile EncoderProfileManager::profileFor(EncoderGroup group) const
{
	if (group == EncoderGroup::DskVertical)
		return vertical_;
	return horizontal_;
}

void EncoderProfileManager::setProfile(const EncoderProfile &profile)
{
	if (profile.group == EncoderGroup::DskVertical) {
		vertical_ = profile;
		return;
	}
	if (profile.group == EncoderGroup::DskHorizontal)
		horizontal_ = profile;
}

} // namespace dsk
