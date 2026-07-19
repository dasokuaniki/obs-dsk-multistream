#include "integration/comment-viewer-integration.hpp"

#include "compat/obs-browser-panel.hpp"
#include "core/comment-viewer-contract.hpp"
#include "core/comment-viewer-integration-policy.hpp"
#include "core/comment-viewer-launcher.hpp"
#include "core/diagnostics.hpp"
#include "core/http-client.hpp"

#include <obs-frontend-api.h>
#include <util/config-file.h>

#include <QDesktopServices>
#include <QDockWidget>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <optional>
#include <string>
#include <utility>

namespace dsk {
namespace {

constexpr auto DockTitle = "DSK Comments";
constexpr auto DockId = "dskcommentsviewer";
constexpr int MaxProbeAttempts = 10;

void showDock(QWidget *contents)
{
	if (!contents)
		return;
	contents->show();
	if (auto *dock = qobject_cast<QDockWidget *>(contents->parentWidget())) {
		dock->setVisible(true);
		dock->show();
		dock->raise();
	}
}

} // namespace

CommentViewerIntegration::CommentViewerIntegration(QObject *parent) : QObject(parent) {}

CommentViewerIntegration::~CommentViewerIntegration()
{
	shutdown();
}

void CommentViewerIntegration::initialize()
{
	enabled_ = commentViewerIntegrationEnabledAtStartup(isCommentViewerInstalled());
	registerOpenViewerMenu();
	if (enabled_)
		beginProbe();
	else
		removeDock();
}

void CommentViewerIntegration::shutdown()
{
	if (shuttingDown_)
		return;
	shuttingDown_ = true;
	advanceProbeGeneration();
	if (http_)
		http_->abortAll();
	removeDock();
}

void CommentViewerIntegration::setEnabled(bool enabled)
{
	if (shuttingDown_ || enabled_ == enabled)
		return;

	enabled_ = enabled;
	if (!enabled) {
		advanceProbeGeneration();
		if (http_)
			http_->abortAll();
		removeDock();
		logInfo("DSK Comment Viewer integration is off. The Viewer process was left running.");
		return;
	}

	registerOpenViewerMenu();
	beginProbe();
}

bool CommentViewerIntegration::enabled() const
{
	return enabled_;
}

void CommentViewerIntegration::openViewer(void *)
{
	if (!openCommentViewerApp())
		QDesktopServices::openUrl(commentViewerPageUrl());
}

quint64 CommentViewerIntegration::advanceProbeGeneration()
{
	++probeGeneration_;
	if (probeGeneration_ == 0)
		++probeGeneration_;
	return probeGeneration_;
}

void CommentViewerIntegration::registerOpenViewerMenu()
{
	if (!isCommentViewerInstalled() || openViewerMenuRegistered_)
		return;
	obs_frontend_add_tools_menu_item("Open DSK Comment Viewer", openViewer, nullptr);
	openViewerMenuRegistered_ = true;
}

void CommentViewerIntegration::beginProbe()
{
	const quint64 generation = advanceProbeGeneration();
	if (!isCommentViewerInstalled()) {
		removeDock();
		logWarning("DSK Comment Viewer is not installed; the enabled integration is waiting for installation.");
		return;
	}
	scheduleProbe(generation, false, 0);
}

void CommentViewerIntegration::scheduleProbe(quint64 generation, bool launchAttempted, int attempt)
{
	if (shuttingDown_ || !enabled_ || generation != probeGeneration_ || !isCommentViewerInstalled())
		return;
	if (!http_)
		http_ = std::make_unique<HttpClient>();

	HttpRequest request;
	request.url = commentViewerObsIntegrationUrl();
	request.timeoutMs = 750;
	request.maxResponseBytes = 16 * 1024;
	request.headers.push_back({QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json")});
	http_->send(std::move(request), [this, generation, launchAttempted, attempt](HttpResponse response) {
		const auto integration = response.isSuccess() ? parseCommentViewerObsIntegration(response.body)
							      : std::nullopt;
		const auto action = commentViewerProbeAction(enabled_, probeGeneration_, generation,
							   integration.has_value(), launchAttempted,
							   attempt, MaxProbeAttempts);
		switch (action) {
		case CommentViewerProbeAction::Ignore:
			return;
		case CommentViewerProbeAction::Connect:
			if (createDock(integration->viewerUrl)) {
				logInfo(QStringLiteral("Connected DSK Comment Viewer %1 through OBS integration API v1.")
						.arg(integration->appVersion));
			}
			return;
		case CommentViewerProbeAction::Launch:
			logInfo("DSK Comment Viewer is not responding; launching the independent Viewer installation.");
			if (!startCommentViewerServer())
				logWarning("The installed DSK Comment Viewer could not be launched.");
			QTimer::singleShot(350, this, [this, generation]() { scheduleProbe(generation, true, 0); });
			return;
		case CommentViewerProbeAction::Retry:
			QTimer::singleShot(500, this, [this, generation, attempt]() {
				scheduleProbe(generation, true, attempt + 1);
			});
			return;
		case CommentViewerProbeAction::GiveUp:
			removeDock();
			logWarning("DSK Comment Viewer is installed but its OBS integration API v1 is unavailable.");
			return;
		}
	});
}

bool CommentViewerIntegration::createDock(const QUrl &viewerUrl)
{
	const QUrl expectedUrl(QStringLiteral("http://127.0.0.1:17321/viewer?dock=chat&send=1"));
	if (viewerUrl != expectedUrl) {
		logWarning("Refused an unexpected DSK Comment Viewer dock URL.");
		return false;
	}
	if (dockContents_) {
		showDock(dockContents_);
		return true;
	}

	hideLegacyDock();
	removeLegacyDockConfig();
	if (dskObsBrowserPanelVersion() <= 0) {
		logWarning("OBS Browser is unavailable; DSK Comments dock was not created.");
		return false;
	}

	auto browserPanel = std::unique_ptr<QCef>(dskCreateObsBrowserPanel());
	if (!browserPanel) {
		logWarning("Could not load the OBS Browser panel API; DSK Comments dock was not created.");
		return false;
	}
	if (!browserPanel->init_browser() && !browserPanel->wait_for_browser_init()) {
		logWarning("OBS Browser did not initialize; DSK Comments dock was not created.");
		return false;
	}

	auto *contents = new QWidget();
	contents->setObjectName(QStringLiteral("dskCommentsDockContents"));
	auto *layout = new QVBoxLayout(contents);
	layout->setContentsMargins(0, 0, 0, 0);
	const std::string encodedUrl = viewerUrl.toEncoded(QUrl::FullyEncoded).toStdString();
	QCefWidget *browser = browserPanel->create_widget(contents, encodedUrl, nullptr);
	if (!browser) {
		delete contents;
		logWarning("OBS Browser could not create the DSK Comments page.");
		return false;
	}
	browser->setObjectName(QStringLiteral("dskCommentsBrowser"));
	layout->addWidget(browser);

	if (!obs_frontend_add_dock_by_id(DockId, DockTitle, contents)) {
		delete contents;
		logWarning("OBS refused to register the DSK Comments dock.");
		return false;
	}

	browserPanel_ = std::move(browserPanel);
	dockContents_ = contents;
	browser_ = browser;
	showDock(contents);
	logInfo("Created the DSK Comments dock with the validated Comment Viewer URL.");
	return true;
}

void CommentViewerIntegration::removeDock()
{
	const bool hadPluginDock = !dockContents_.isNull();
	if (browser_)
		browser_->closeBrowser();
	if (dockContents_)
		obs_frontend_remove_dock(DockId);
	browser_ = nullptr;
	dockContents_ = nullptr;
	browserPanel_.reset();

	const bool hidLegacyDock = hideLegacyDock();
	const bool removedLegacyConfig = removeLegacyDockConfig();
	if (hadPluginDock || hidLegacyDock || removedLegacyConfig)
		logInfo("Removed only the DSK Comments dock owned by DSK Multistream.");
}

bool CommentViewerIntegration::hideLegacyDock() const
{
	auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
	if (!mainWindow)
		return false;

	bool hidden = false;
	for (QDockWidget *dock : mainWindow->findChildren<QDockWidget *>()) {
		if (dock->property("uuid").toString() != QString::fromLatin1(DockId))
			continue;
		dock->hide();
		dock->toggleViewAction()->setVisible(false);
		hidden = true;
	}
	return hidden;
}

bool CommentViewerIntegration::removeLegacyDockConfig() const
{
	config_t *config = obs_frontend_get_user_config();
	if (!config)
		return false;

	const char *raw = config_get_string(config, "BasicWindow", "ExtraBrowserDocks");
	const QByteArray payload(raw ? raw : "");
	if (payload.trimmed().isEmpty())
		return false;

	QJsonParseError parseError{};
	const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
		logWarning("OBS ExtraBrowserDocks is not valid JSON. DSK left it unchanged to protect existing docks.");
		return false;
	}

	QJsonArray filtered;
	bool removed = false;
	for (const QJsonValue &value : document.array()) {
		if (value.isObject() && value.toObject().value(QStringLiteral("uuid")).toString() ==
					QString::fromLatin1(DockId)) {
			removed = true;
			continue;
		}
		filtered.push_back(value);
	}
	if (!removed)
		return false;

	const QByteArray filteredPayload = QJsonDocument(filtered).toJson(QJsonDocument::Compact);
	config_set_string(config, "BasicWindow", "ExtraBrowserDocks", filteredPayload.constData());
	config_save_safe(config, "tmp", "bak");
	return true;
}

} // namespace dsk
