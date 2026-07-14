#include "core/platform-preset-registry.hpp"

namespace dsk {

PlatformPresetRegistry::PlatformPresetRegistry()
	: presets_({
		  {"twitch", "Twitch", "rtmp://live.twitch.tv/app", "https://help.twitch.tv/", "dsk-horizontal", 6000, 4500,
		   "Use DSK Horizontal unless you are intentionally producing a mobile-only layout.", false},
		  {"youtube", "YouTube", "rtmp://a.rtmp.youtube.com/live2", "https://support.google.com/youtube/", "dsk-horizontal", 9000,
		   6000, "YouTube works well for both horizontal and vertical outputs.", false},
		  {"kick", "Kick", "rtmps://fa-live-cf.kick.com/app", "https://help.kick.com/", "dsk-horizontal", 6000, 4500,
		   "Kick ingest commonly uses RTMPS.", false},
		  {"tiktok", "TikTok (Manual RTMP)", "", "https://www.tiktok.com/live", "dsk-vertical", 6000, 4500,
		   "Get the RTMP server URL and stream key from TikTok, then paste both here. TikTok is usually best paired with the DSK Vertical output.", true},
		  {"custom", "Custom RTMP", "", "", "dsk-horizontal", 6000, 4500, "Paste the RTMP server URL from the platform.", false},
	  })
{
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
	return presets_.last();
}

} // namespace dsk
