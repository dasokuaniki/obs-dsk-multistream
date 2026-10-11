#include "core/layout-manager.hpp"
#include "core/encoder-profile-manager.hpp"
#include "core/output-target.hpp"
#include "core/settings-codec.hpp"

#include <QCoreApplication>
#include <QJsonObject>

#include <iostream>

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	int failures = 0;
	const auto check = [&failures](bool passed, const char *message) {
		if (!passed) {
			std::cerr << "FAIL: " << message << '\n';
			++failures;
		}
	};
	using namespace dsk;
	EncoderProfileManager profiles;
	auto horizontalProfile = profiles.profileFor(EncoderGroup::DskHorizontalCanvas);
	check(horizontalProfile.group == EncoderGroup::DskHorizontalCanvas && horizontalProfile.width == 1920 &&
	      horizontalProfile.height == 1080, "independent landscape output has its own encoder profile");
	horizontalProfile.videoBitrateKbps = 9000;
	profiles.setProfile(horizontalProfile);
	check(profiles.profileFor(EncoderGroup::DskHorizontalCanvas).videoBitrateKbps == 9000 &&
	      profiles.profileFor(EncoderGroup::DskHorizontal).videoBitrateKbps == 6000 &&
	      profiles.profileFor(EncoderGroup::DskVertical).videoBitrateKbps == 6000,
	      "changing landscape encoding does not change OBS-main or portrait profiles");
	check(encoderGroupToString(encoderGroupFromString("dsk-horizontal-canvas")) == "dsk-horizontal-canvas",
	      "additional horizontal output has a distinct persistent identity");
	check(encoderGroupFromString("dsk-horizontal") == EncoderGroup::DskHorizontal,
	      "legacy horizontal destinations still use OBS main");
	LayoutManager horizontal;
	LayoutManager vertical;
	VerticalLayout landscape;
	landscape.width = 1920;
	landscape.height = 1080;
	horizontal.initializeVerticalScenes({}, {}, landscape);
	vertical.initializeVerticalScenes({}, {}, VerticalLayout{});
	horizontal.createVerticalScene("Horizontal scene B");
	check(horizontal.verticalLayout().width == 1920 && horizontal.verticalLayout().height == 1080,
	      "new horizontal scenes preserve canvas dimensions");
	VerticalLayout layout = landscape;
	layout.items.push_back({"camera", "Camera", QRectF(0, 0, 100, 100)});
	horizontal.setVerticalLayout(layout);
	horizontal.applyTemplate("full-screen");
	check(horizontal.verticalLayout().items.first().rect == QRectF(0, 0, 1920, 1080),
	      "full-screen template uses horizontal canvas dimensions");
	check(vertical.verticalLayout().width == 1080 && vertical.verticalLayout().height == 1920 &&
	      vertical.verticalLayout().items.isEmpty(), "editing horizontal output does not alter vertical output");
	const auto decoded = verticalLayoutFromJson(verticalLayoutToJson(horizontal.verticalLayout()));
	check(decoded.width == 1920 && decoded.height == 1080 && decoded.items.first().rect == QRectF(0, 0, 1920, 1080),
	      "horizontal layout geometry round-trips");
	layout.items.push_back({"camera2", "Camera 2", QRectF(0, 0, 100, 100)});
	horizontal.setVerticalLayout(layout);
	for (const QString templateId : {QStringLiteral("game-camera"), QStringLiteral("camera-first"),
					QStringLiteral("center-crop"), QStringLiteral("full-screen")}) {
		horizontal.applyTemplate(templateId);
		for (const auto &item : horizontal.verticalLayout().items)
			check(QRectF(0, 0, 1920, 1080).contains(item.rect), "landscape template slots stay inside the canvas");
	}
	const auto firstSceneId = horizontal.activeVerticalSceneId();
	const auto secondSceneId = horizontal.createVerticalScene("Horizontal C");
	check(horizontal.renameVerticalScene(secondSceneId, "Horizontal renamed") &&
	      horizontal.moveVerticalScene(secondSceneId, -1) && horizontal.selectVerticalScene(firstSceneId) &&
	      horizontal.removeVerticalScene(secondSceneId), "scene CRUD and ordering work in landscape mode");
	check(vertical.verticalScenes().size() == 1 && vertical.verticalLayout().items.isEmpty(),
	      "horizontal scene CRUD leaves portrait scenes untouched");
	layout.items.push_back({"recursive", "DSK Horizontal Program", QRectF(0, 0, 100, 100)});
	horizontal.setVerticalLayout(layout);
	check(horizontal.verticalLayout().items.size() == 2, "the program canvas cannot be nested recursively as its own source");
	return failures == 0 ? 0 : 1;
}
