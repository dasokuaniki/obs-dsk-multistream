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
#include <QAction>
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
	if (openViewerMenuAction_) {
		delete openViewerMenuAction_.data();
		openViewerMenuAction_ = nullptr;
	}
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

bool CommentViewerIntegration::registerDockShell()
{
	if (shuttingDown_ || !enabled_)
		return false;
	if (dockContents_)
		return true;

	hideLegacyDock();
	removeLegacyDockConfig();
	auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
	if (!mainWindow) {
		logWarning("OBS main window is unavailable; the DSK Comments dock shell was not registered.");
		return false;
	}

	auto *dockHost = new QWidget(mainWindow);
	dockHost->setObjectName(QStringLiteral("dskCommentsHost"));
	auto *layout = new QVBoxLayout(dockHost);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	auto *placeholder = new QWidget(dockHost);
	placeholder->setObjectName(QStringLiteral("dskCommentsPlaceholder"));
	layout->addWidget(placeholder);

	if (!obs_frontend_add_dock_by_id(DockId, DockTitle, dockHost)) {
		delete dockHost;
		logWarning("OBS refused to register the DSK Comments dock shell.");
		return false;
	}

	dockContents_ = dockHost;
	dockPlaceholder_ = placeholder;
	logInfo("Registered the stable DSK Comments dock shell before browser discovery.");
	return true;
}

void CommentViewerIntegration::onBrowserUrlChanged(const QString &url)
{
	logInfo(QStringLiteral("DSK Comments browser navigated to: %1").arg(url));
}

void CommentViewerIntegration::onBrowserTitleChanged(const QString &title)
{
	logInfo(QStringLiteral("DSK Comments browser title changed to: %1").arg(title));
}

void CommentViewerIntegration::openViewer(void *privateData)
{
	auto *integration = static_cast<CommentViewerIntegration *>(privateData);
	if (!integration || integration->shuttingDown_)
		return;

	if (!openCommentViewerApp())
		QDesktopServices::openUrl(commentViewerPageUrl());

	if (shouldReconnectCommentViewerAfterOpen(integration->enabled_, integration->shuttingDown_,
						  isCommentViewerInstalled())) {
		logInfo("Open DSK Comment Viewer requested; restarting the OBS integration probe.");
		integration->beginProbe();
	}
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
	if (!isCommentViewerInstalled() || openViewerMenuAction_)
		return;

	auto *action = static_cast<QAction *>(
		obs_frontend_add_tools_menu_qaction("Open DSK Comment Viewer"));
	if (!action) {
		logWarning("OBS refused to create the DSK Comment Viewer Tools action.");
		return;
	}

	openViewerMenuAction_ = action;
	connect(action, &QAction::triggered, this, [this]() { openViewer(this); });
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
	const QUrl baseUrl = commentViewerBaseUrl();
	request.url = commentViewerObsIntegrationUrlForBase(baseUrl);
	request.timeoutMs = 750;
	request.maxResponseBytes = 16 * 1024;
	request.headers.push_back({QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json")});
	http_->send(std::move(request), [this, generation, launchAttempted, attempt, baseUrl](HttpResponse response) {
		const auto integration = response.isSuccess() ? parseCommentViewerObsIntegration(response.body, baseUrl)
							      : std::nullopt;
		const auto action = commentViewerProbeAction(enabled_, probeGeneration_, generation,
							   integration.has_value(), launchAttempted,
							   attempt, CommentViewerMaxProbeAttempts);
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
			logWarning("DSK Comment Viewer is installed but its OBS integration API v1 is unavailable.");
			return;
		}
	});
}

bool CommentViewerIntegration::createDock(const QUrl &viewerUrl)
{
	const QUrl dockUrl = commentViewerBrowserDockUrl(viewerUrl);
	if (dockUrl.isEmpty()) {
		logWarning("Refused an unexpected DSK Comment Viewer dock URL.");
		return false;
	}
	if (!dockContents_ && !registerDockShell())
		return false;
	if (browser_) {
		browser_->setURL(dockUrl.toEncoded(QUrl::FullyEncoded).toStdString());
		return true;
	}

	if (dskObsBrowserPanelVersion() <= 0) {
		logWarning("OBS Browser is unavailable; the DSK Comments dock page was not created.");
		return false;
	}

	if (!browserPanel_) {
		browserPanel_ = std::unique_ptr<QCef>(dskCreateObsBrowserPanel());
		if (!browserPanel_) {
			logWarning("Could not load the OBS Browser panel API; DSK Comments dock was not created.");
			return false;
		}
	}
	if (!browserPanel_->initialized() && !browserPanel_->init_browser()) {
		QTimer::singleShot(250, this, [this, viewerUrl]() {
			if (!shuttingDown_ && enabled_)
				createDock(viewerUrl);
		});
		logInfo("Waiting for OBS Browser initialization without blocking the OBS UI thread.");
		return true;
	}

	// REGRESSION GUARD: Do not change dockUrl back to 127.0.0.1.
	// OBS Browser shares one Chromium connection pool for all panels and browser
	// sources. The Comment Viewer has several long-lived 127.0.0.1 connections,
	// which can consume Chromium's per-host limit before this dock loads. The
	// server is unchanged; using the equivalent localhost name gives the dock an
	// independent pool and removes the startup-order race. See
	// docs/comment-viewer-integration.md.
	const std::string encodedUrl = dockUrl.toEncoded(QUrl::FullyEncoded).toStdString();
	auto *layout = qobject_cast<QVBoxLayout *>(dockContents_->layout());
	if (!layout) {
		logWarning("The DSK Comments dock shell has no compatible layout.");
		return false;
	}

	QCefWidget *browser = browserPanel_->create_widget(dockContents_, encodedUrl, nullptr);
	if (!browser) {
		logWarning("OBS Browser could not create the DSK Comments page.");
		return false;
	}
	QObject::connect(browser, SIGNAL(urlChanged(QString)), this,
			 SLOT(onBrowserUrlChanged(QString)));
	QObject::connect(browser, SIGNAL(titleChanged(QString)), this,
			 SLOT(onBrowserTitleChanged(QString)));
	browser->setObjectName(QStringLiteral("dskCommentsBrowser"));
	if (dockPlaceholder_) {
		layout->removeWidget(dockPlaceholder_);
		dockPlaceholder_->deleteLater();
		dockPlaceholder_ = nullptr;
	}
	layout->addWidget(browser);
	browser_ = browser;
	logInfo(QStringLiteral("Attached the DSK Comments browser using an isolated connection pool: %1")
			.arg(QString::fromStdString(encodedUrl)));
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
	dockPlaceholder_ = nullptr;
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
