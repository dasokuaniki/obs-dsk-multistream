#include "core/diagnostics.hpp"
#include "core/e2e-auto-runner.hpp"
#include "core/comment-viewer-launcher.hpp"
#include "core/output-manager.hpp"
#include "ui/main-dock.hpp"
#include "ui/scene-router-dock.hpp"
#include "ui/stream-controls-dock.hpp"
#include "ui/vertical-layout-editor.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/config-file.h>

#include <QDesktopServices>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QObject>
#include <QPointer>
#include <QScreen>
#include <QShowEvent>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <memory>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-dsk-multistream", "en-US")

namespace {

std::unique_ptr<dsk::OutputManager> manager;
std::unique_ptr<dsk::E2eAutoRunner> e2eRunner;
QPointer<QWidget> mainDock;
QPointer<dsk::StreamControlsDock> controlsDock;
QPointer<dsk::SceneRouterDock> sceneRouterDock;
QObject *timerContext = nullptr;
QPointer<QTimer> verticalEditorLoadTimer;
QPointer<QObject> mainWindowCloseFilter;
bool frontendCallbackRegistered = false;
bool frontendUiRemoved = false;
bool shutdownPrepared = false;
bool frontendExiting = false;
bool toolsMenuRegistered = false;

void cancelVerticalEditorLoad();
void scheduleVerticalEditorLoad(int delayMs = 0);

class MainWindowCloseFilter final : public QObject {
public:
	using QObject::QObject;

protected:
	bool eventFilter(QObject *watched, QEvent *event) override
	{
		if (event && event->type() == QEvent::Close)
			cancelVerticalEditorLoad();
		return QObject::eventFilter(watched, event);
	}
};

constexpr auto kCommentBrowserDockTitle = "DSK Comments";
constexpr auto kCommentBrowserDockUuid = "dskcommentsviewer";
constexpr auto kCommentBrowserDockUrl = "http://127.0.0.1:17321/viewer?dock=chat&send=1";

class VerticalLayoutDockHost : public QWidget {
public:
	explicit VerticalLayoutDockHost(dsk::OutputManager *outputManager, QWidget *parent = nullptr)
		: QWidget(parent),
		  manager_(outputManager),
		  layout_(new QVBoxLayout(this)),
		  placeholder_(new QLabel("DSK Vertical Layout is loading.", this))
	{
		layout_->setContentsMargins(0, 0, 0, 0);
		placeholder_->setAlignment(Qt::AlignCenter);
		placeholder_->setStyleSheet(QStringLiteral("QLabel { color: #cfd4dc; padding: 12px; }"));
		layout_->addWidget(placeholder_, 1);
	}

	void loadEditor()
	{
		if (editor_ || !manager_)
			return;

		editor_ = new dsk::VerticalLayoutEditor(manager_, this);
		if (placeholder_) {
			layout_->removeWidget(placeholder_);
			placeholder_->deleteLater();
			placeholder_ = nullptr;
		}
		layout_->addWidget(editor_, 1);
		QTimer::singleShot(0, this, [this]() { fitFloatingDockToScreen(); });
	}

	void prepareForUnload()
	{
		if (editor_)
			editor_->prepareForUnload();
	}

	void handleSceneCollectionChanged()
	{
		if (editor_)
			editor_->handleSceneCollectionChanged();
	}

	bool hasEditor() const { return editor_ != nullptr; }
	bool exercisePreviewCanvasReplacementForTest()
	{
		return editor_ && editor_->exercisePreviewCanvasReplacementForTest();
	}
	bool exerciseSourceVisibilityToggleForTest()
	{
		return editor_ && editor_->exerciseSourceVisibilityToggleForTest();
	}
	bool exerciseSetupVisibilityToggleForTest()
	{
		return editor_ && editor_->exerciseSetupVisibilityToggleForTest();
	}

protected:
	void showEvent(QShowEvent *event) override
	{
		QWidget::showEvent(event);
		if (!editor_)
			scheduleVerticalEditorLoad(0);
	}

private:
	void fitFloatingDockToScreen()
	{
		QDockWidget *dock = nullptr;
		for (QWidget *widget = parentWidget(); widget; widget = widget->parentWidget()) {
			if ((dock = qobject_cast<QDockWidget *>(widget)))
				break;
		}
		if (!dock || !dock->isFloating() || !dock->screen())
			return;

		const QRect available = dock->screen()->availableGeometry().adjusted(12, 12, -12, -12);
		const QRect frame = dock->frameGeometry();
		if (available.contains(frame))
			return;

		QSize size = dock->size();
		size.setWidth(std::min(size.width(), available.width()));
		size.setHeight(std::min(size.height(), std::min(760, available.height())));
		dock->resize(size);
		const int x = std::clamp(dock->x(), available.left(), available.right() - size.width() + 1);
		const int y = std::clamp(dock->y(), available.top(), available.bottom() - size.height() + 1);
		dock->move(x, y);
	}

	dsk::OutputManager *manager_ = nullptr;
	QVBoxLayout *layout_ = nullptr;
	QLabel *placeholder_ = nullptr;
	QPointer<dsk::VerticalLayoutEditor> editor_;
};

QPointer<VerticalLayoutDockHost> verticalDock;

void showDockForWidget(QWidget *widget)
{
	if (!widget)
		return;

	for (QWidget *parent = widget->parentWidget(); parent; parent = parent->parentWidget()) {
		if (auto *dock = qobject_cast<QDockWidget *>(parent)) {
			dock->setVisible(true);
			dock->show();
			dock->raise();
			dock->activateWindow();
			return;
		}
	}

	widget->show();
	widget->raise();
	widget->activateWindow();
}

bool shouldCreateDocks()
{
	return !frontendExiting && !shutdownPrepared;
}

void ensureMainDockRegistered()
{
	if (!shouldCreateDocks())
		return;

	if (!manager)
		manager = std::make_unique<dsk::OutputManager>();

	if (!mainDock) {
		dsk::logInfo("Creating DSK Multistream dock");
		mainDock = new dsk::MainDock(manager.get());
		dsk::logInfo("Registering DSK Multistream dock");
		const bool mainDockAdded = obs_frontend_add_dock_by_id("dsk_multistream", "DSK Multistream", mainDock);
		dsk::logInfo(QString("Registered DSK Multistream dock: %1").arg(mainDockAdded ? "true" : "false"));
		if (!mainDockAdded) {
			delete mainDock.data();
			mainDock = nullptr;
		}
	}
}

void ensureControlsDockRegistered()
{
	if (!shouldCreateDocks())
		return;

	if (!manager)
		manager = std::make_unique<dsk::OutputManager>();

	if (!controlsDock) {
		dsk::logInfo("Creating DSK Stream Controls dock");
		controlsDock = new dsk::StreamControlsDock(manager.get());
		dsk::logInfo("Registering DSK Stream Controls dock");
		const bool controlsDockAdded =
			obs_frontend_add_dock_by_id("dsk_stream_controls", "DSK Stream Controls", controlsDock);
		dsk::logInfo(QString("Registered DSK Stream Controls dock: %1").arg(controlsDockAdded ? "true" : "false"));
		if (!controlsDockAdded) {
			delete controlsDock.data();
			controlsDock = nullptr;
		}
	}
}

void ensureVerticalDockRegistered()
{
	if (!shouldCreateDocks())
		return;

	if (!manager)
		manager = std::make_unique<dsk::OutputManager>();

	if (!verticalDock) {
		dsk::logInfo("Creating DSK Vertical Layout dock host");
		verticalDock = new VerticalLayoutDockHost(manager.get());
		dsk::logInfo("Registering DSK Vertical Layout dock");
		const bool verticalDockAdded =
			obs_frontend_add_dock_by_id("dsk_vertical_layout", "DSK Vertical Layout", verticalDock);
		dsk::logInfo(QString("Registered DSK Vertical Layout dock: %1").arg(verticalDockAdded ? "true" : "false"));
		if (!verticalDockAdded) {
			delete verticalDock.data();
			verticalDock = nullptr;
		}
	}
}

void ensureVerticalDockEditorLoaded()
{
	if (!shouldCreateDocks())
		return;

	ensureVerticalDockRegistered();
	if (!verticalDock || verticalDock->hasEditor())
		return;

	QElapsedTimer loadTimer;
	loadTimer.start();
	dsk::logInfo("Loading DSK Vertical Layout editor");
	verticalDock->loadEditor();
	dsk::logInfo(QStringLiteral("Loaded DSK Vertical Layout editor in %1 ms").arg(loadTimer.elapsed()));
}

void ensureSceneRouterDockRegistered()
{
	if (!shouldCreateDocks())
		return;

	if (!manager)
		manager = std::make_unique<dsk::OutputManager>();

	if (!sceneRouterDock) {
		dsk::logInfo("Creating DSK Output Scenes dock");
		sceneRouterDock = new dsk::SceneRouterDock(manager.get());
		dsk::logInfo("Registering DSK Output Scenes dock");
		const bool sceneRouterDockAdded =
			obs_frontend_add_dock_by_id("dsk_scene_router", "DSK Output Scenes", sceneRouterDock);
		dsk::logInfo(QString("Registered DSK Output Scenes dock: %1").arg(sceneRouterDockAdded ? "true" : "false"));
		if (!sceneRouterDockAdded) {
			delete sceneRouterDock.data();
			sceneRouterDock = nullptr;
		}
	}
}

void showMainDock(void *);

QObject *pluginTimerContext()
{
	if (!timerContext)
		timerContext = new QObject();
	return timerContext;
}

void cancelVerticalEditorLoad()
{
	if (!verticalEditorLoadTimer)
		return;
	verticalEditorLoadTimer->stop();
	verticalEditorLoadTimer->deleteLater();
	verticalEditorLoadTimer = nullptr;
	dsk::logInfo("Cancelled delayed DSK Vertical Layout editor load.");
}

void scheduleVerticalEditorLoad(int delayMs)
{
	if (!shouldCreateDocks() || (verticalDock && verticalDock->hasEditor()))
		return;

	cancelVerticalEditorLoad();
	auto *timer = new QTimer(pluginTimerContext());
	timer->setSingleShot(true);
	verticalEditorLoadTimer = timer;
	QObject::connect(timer, &QTimer::timeout, timer, [timer]() {
		if (verticalEditorLoadTimer == timer)
			verticalEditorLoadTimer = nullptr;
		timer->deleteLater();
		if (!shouldCreateDocks())
			return;
		if (!verticalDock || !verticalDock->isVisible())
			return;
		ensureVerticalDockEditorLoaded();
	});
	timer->start(std::max(0, delayMs));
}

void showMainDock(void *)
{
	ensureMainDockRegistered();

	showDockForWidget(mainDock);
}

void showControlsDock(void *)
{
	ensureControlsDockRegistered();

	showDockForWidget(controlsDock);
}

void showVerticalDock(void *)
{
	ensureVerticalDockRegistered();
	ensureVerticalDockEditorLoaded();

	showDockForWidget(verticalDock);
}

void showSceneRouterDock(void *)
{
	ensureSceneRouterDockRegistered();

	showDockForWidget(sceneRouterDock);
}

void openCommentViewer(void *)
{
	if (!dsk::openCommentViewerApp())
		QDesktopServices::openUrl(dsk::commentViewerPageUrl());
}

void ensureCommentBrowserDockConfig()
{
	config_t *config = obs_frontend_get_user_config();
	if (!config)
		return;

	const char *raw = config_get_string(config, "BasicWindow", "ExtraBrowserDocks");
	const QByteArray rawPayload(raw ? raw : "");
	QJsonParseError parseError{};
	QJsonDocument document;
	QJsonArray docks;
	if (!rawPayload.trimmed().isEmpty()) {
		document = QJsonDocument::fromJson(rawPayload, &parseError);
		if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
			dsk::logWarning("OBS ExtraBrowserDocks is not valid JSON. DSK left it unchanged to protect existing docks.");
			return;
		}
		docks = document.array();
	}

	bool found = false;
	bool changed = rawPayload.trimmed().isEmpty();
	for (QJsonValueRef value : docks) {
		if (!value.isObject())
			continue;

		QJsonObject dock = value.toObject();
		const QString title = dock.value(QStringLiteral("title")).toString();
		const QString uuid = dock.value(QStringLiteral("uuid")).toString();
		if (uuid != QString::fromLatin1(kCommentBrowserDockUuid) && title != QString::fromLatin1(kCommentBrowserDockTitle))
			continue;

		found = true;
		if (dock.value(QStringLiteral("title")).toString() != QString::fromLatin1(kCommentBrowserDockTitle)) {
			dock.insert(QStringLiteral("title"), QString::fromLatin1(kCommentBrowserDockTitle));
			changed = true;
		}
		if (dock.value(QStringLiteral("url")).toString() != QString::fromLatin1(kCommentBrowserDockUrl)) {
			dock.insert(QStringLiteral("url"), QString::fromLatin1(kCommentBrowserDockUrl));
			changed = true;
		}
		if (dock.value(QStringLiteral("uuid")).toString() != QString::fromLatin1(kCommentBrowserDockUuid)) {
			dock.insert(QStringLiteral("uuid"), QString::fromLatin1(kCommentBrowserDockUuid));
			changed = true;
		}
		value = dock;
	}

	if (!found) {
		QJsonObject dock;
		dock.insert(QStringLiteral("title"), QString::fromLatin1(kCommentBrowserDockTitle));
		dock.insert(QStringLiteral("url"), QString::fromLatin1(kCommentBrowserDockUrl));
		dock.insert(QStringLiteral("uuid"), QString::fromLatin1(kCommentBrowserDockUuid));
		docks.append(dock);
		changed = true;
	}

	if (!changed)
		return;

	const QByteArray payload = QJsonDocument(docks).toJson(QJsonDocument::Compact);
	config_set_string(config, "BasicWindow", "ExtraBrowserDocks", payload.constData());
	config_save_safe(config, "tmp", "bak");
	dsk::logInfo("Ensured OBS browser dock for DSK Comments. It uses the DSK Comment Viewer page directly.");
}

void startCommentViewerServiceDelayed()
{
	QTimer::singleShot(2500, pluginTimerContext(), []() {
		dsk::logInfo("Starting DSK Comment Viewer service for OBS browser dock");
		if (!dsk::startCommentViewerServer())
			dsk::logWarning("DSK Comment Viewer service launch was not available");
	});
}

void registerDskDocksDelayed()
{
	QTimer::singleShot(0, pluginTimerContext(), []() {
		if (!shouldCreateDocks())
			return;

		dsk::logInfo("Registering DSK dock shells for OBS restore/menu");
		ensureMainDockRegistered();
		ensureControlsDockRegistered();
		ensureSceneRouterDockRegistered();
		ensureVerticalDockRegistered();
		dsk::logInfo("DSK dock shells registered for OBS restore/menu");
		// Give OBS one event-cycle window to restore dock visibility. The host's
		// showEvent path loads immediately when the dock is already visible.
		scheduleVerticalEditorLoad(250);
	});
}

void scheduleVerticalUiStress()
{
	const QByteArray enabled = qgetenv("DSK_E2E_VERTICAL_UI_STRESS").trimmed().toLower();
	if (enabled.isEmpty() || enabled == "0" || enabled == "false")
		return;

	bool parsed = false;
	const int requestedIterations = qEnvironmentVariableIntValue("DSK_E2E_VERTICAL_UI_STRESS_ITERATIONS", &parsed);
	const int iterations = parsed ? qBound(1, requestedIterations, 1000) : 100;
	bool startDelayParsed = false;
	const int requestedStartDelay = qEnvironmentVariableIntValue("DSK_E2E_START_DELAY_MS", &startDelayParsed);
	const int stressDelayMs = startDelayParsed ? qBound(0, requestedStartDelay, 600000) + 500 : 500;
	dsk::logInfo(QStringLiteral("E2E vertical UI stress scheduled: %1 scene switches.").arg(iterations));
	QTimer::singleShot(stressDelayMs, pluginTimerContext(), [iterations]() {
		if (!shouldCreateDocks() || !manager) {
			dsk::logError("E2E vertical UI stress failed: frontend UI is unavailable.");
			return;
		}

		dsk::VerticalLayout testLayout = manager->layouts().verticalLayout();
		if (testLayout.items.isEmpty()) {
			const QString sourceName = qEnvironmentVariable("DSK_E2E_VERTICAL_SOURCE").trimmed();
			obs_source_t *source = sourceName.isEmpty() ? nullptr : obs_get_source_by_name(sourceName.toUtf8().constData());
			const bool hasVideo = source && (obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO) != 0;
			if (source)
				obs_source_release(source);
			if (!hasVideo) {
				dsk::logError("E2E vertical UI stress failed: configured vertical source is unavailable.");
				return;
			}

			dsk::VerticalLayoutItem item;
			item.id = dsk::newTargetId();
			item.sourceName = sourceName;
			item.rect = QRectF(0, 0, testLayout.width, testLayout.height);
			item.fitMode = dsk::FitMode::Fill;
			testLayout.items.push_back(item);
			manager->layouts().setVerticalLayout(testLayout);
			if (!manager->saveVerticalLayout()) {
				dsk::logError("E2E vertical UI stress failed: test source could not be saved.");
				return;
			}
			dsk::logInfo(QStringLiteral("E2E vertical preview source prepared: %1.").arg(sourceName));
		}

		ensureVerticalDockRegistered();
		ensureVerticalDockEditorLoaded();
		if (auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window()))
			mainWindow->show();
		showDockForWidget(verticalDock);
		dsk::resetVerticalPreviewDiagnostics();
		if (!verticalDock->exerciseSourceVisibilityToggleForTest()) {
			dsk::logError("E2E vertical UI stress failed: source visibility toggle failed.");
			return;
		}
		dsk::logInfo("E2E vertical source visibility toggle complete.");
		if (!verticalDock->exerciseSetupVisibilityToggleForTest()) {
			dsk::logError("E2E vertical UI stress failed: setup visibility toggle failed.");
			return;
		}
		dsk::logInfo("E2E vertical setup visibility toggle complete.");

		const QString originalSceneId = manager->layouts().activeVerticalSceneId();
		const QString temporarySceneId = manager->createVerticalScene(QStringLiteral("DSK E2E UI CRUD"));
		if (temporarySceneId.isEmpty() ||
		    !manager->renameVerticalScene(temporarySceneId, QStringLiteral("DSK E2E UI CRUD Renamed")) ||
		    !manager->moveVerticalScene(temporarySceneId, -1) || !manager->removeVerticalScene(temporarySceneId) ||
		    (!originalSceneId.isEmpty() && !manager->layouts().selectVerticalScene(originalSceneId)) ||
		    !manager->saveVerticalLayout()) {
			dsk::logError("E2E vertical UI stress failed: scene CRUD operations failed.");
			return;
		}
		dsk::logInfo("E2E vertical scene CRUD complete.");

		QStringList sceneIds;
		for (const auto &scene : manager->layouts().verticalScenes())
			sceneIds.push_back(scene.id);
		while (sceneIds.size() < 2) {
			const QString id = manager->layouts().createVerticalScene(
				QStringLiteral("DSK E2E Stress %1").arg(sceneIds.size() + 1));
			if (id.isEmpty())
				break;
			sceneIds.push_back(id);
		}
		if (sceneIds.size() < 2) {
			dsk::logError("E2E vertical UI stress failed: two DSK scenes were not available.");
			return;
		}

		auto *timer = new QTimer(pluginTimerContext());
		timer->setInterval(15);
		auto completed = std::make_shared<int>(0);
		auto canvasReplacementExercised = std::make_shared<bool>(false);
		dsk::logInfo(QStringLiteral("E2E vertical UI stress started: %1 scene switches.").arg(iterations));
		QObject::connect(timer, &QTimer::timeout, timer,
				 [timer, sceneIds, iterations, completed, canvasReplacementExercised]() {
			if (!manager || shutdownPrepared || frontendExiting) {
				dsk::logError(QStringLiteral("E2E vertical UI stress aborted after %1 scene switches.")
						      .arg(*completed));
				timer->stop();
				timer->deleteLater();
				return;
			}
			if (!*canvasReplacementExercised && *completed >= iterations / 2) {
				if (!verticalDock || !verticalDock->exercisePreviewCanvasReplacementForTest()) {
					dsk::logError("E2E vertical UI stress failed: preview canvas replacement failed.");
					timer->stop();
					timer->deleteLater();
					return;
				}
				*canvasReplacementExercised = true;
				dsk::logInfo("E2E vertical preview canvas replacement complete.");
			}

			const QString &sceneId = sceneIds.at(*completed % sceneIds.size());
			if (!manager->layouts().selectVerticalScene(sceneId) || !manager->saveVerticalLayout()) {
				dsk::logError(QStringLiteral("E2E vertical UI stress failed at iteration %1.")
						      .arg(*completed + 1));
				timer->stop();
				timer->deleteLater();
				return;
			}

			++(*completed);
			if (*completed < iterations && *completed % 50 == 0) {
				dsk::logInfo(QStringLiteral("E2E vertical UI stress progress: %1/%2 scene switches.")
						     .arg(*completed)
						     .arg(iterations));
			}
			if (*completed >= iterations) {
				timer->stop();
				timer->deleteLater();
				QTimer::singleShot(250, pluginTimerContext(), [iterations]() {
					const dsk::VerticalPreviewDiagnostics diagnostics = dsk::verticalPreviewDiagnostics();
					if (diagnostics.callbackCount == 0 || diagnostics.renderedSnapshotCount == 0 ||
					    diagnostics.renderedGenerationCount < 2 || diagnostics.sceneWidth == 0 ||
					    diagnostics.sceneHeight == 0) {
						dsk::logError(
							QStringLiteral("E2E vertical UI stress failed: preview rendering was not exercised "
								       "(callbacks=%1, snapshots=%2, generations=%3, scene=%4x%5).")
								.arg(diagnostics.callbackCount)
								.arg(diagnostics.renderedSnapshotCount)
								.arg(diagnostics.renderedGenerationCount)
								.arg(diagnostics.sceneWidth)
								.arg(diagnostics.sceneHeight));
						return;
					}
					dsk::logInfo(QStringLiteral("E2E vertical preview rendered: callbacks=%1, snapshots=%2, "
							    "generations=%3.")
							 .arg(diagnostics.callbackCount)
							 .arg(diagnostics.renderedSnapshotCount)
							 .arg(diagnostics.renderedGenerationCount));
					dsk::logInfo(QStringLiteral("E2E vertical preview scene dimensions: %1x%2.")
							 .arg(diagnostics.sceneWidth)
							 .arg(diagnostics.sceneHeight));
					dsk::logInfo(QStringLiteral("E2E vertical UI stress complete: %1 scene switches.")
							 .arg(iterations));
				});
			}
		});
		timer->start();
	});
}

void removeFrontendUi()
{
	if (frontendUiRemoved)
		return;
	frontendUiRemoved = true;
	cancelVerticalEditorLoad();
	e2eRunner.reset();
	if (verticalDock) {
		verticalDock->prepareForUnload();
		obs_frontend_remove_dock("dsk_vertical_layout");
		verticalDock = nullptr;
	}
	if (controlsDock) {
		obs_frontend_remove_dock("dsk_stream_controls");
		controlsDock = nullptr;
	}
	if (sceneRouterDock) {
		obs_frontend_remove_dock("dsk_scene_router");
		sceneRouterDock = nullptr;
	}
	if (mainDock) {
		obs_frontend_remove_dock("dsk_multistream");
		mainDock = nullptr;
	}
}

void prepareShutdown()
{
	if (shutdownPrepared)
		return;
	shutdownPrepared = true;
	cancelVerticalEditorLoad();
	if (manager)
		manager->prepareForUnload();
}

void releaseObsSceneReferences()
{
	cancelVerticalEditorLoad();
	if (verticalDock)
		verticalDock->prepareForUnload();
	if (manager)
		manager->releaseObsSceneReferences();
}

void initializeFrontendUi()
{
	if (toolsMenuRegistered)
		return;
	toolsMenuRegistered = true;

	if (!manager)
		manager = std::make_unique<dsk::OutputManager>();
	manager->refreshSceneIdentities();
	if (!mainWindowCloseFilter) {
		if (auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window())) {
			mainWindowCloseFilter = new MainWindowCloseFilter(mainWindow);
			mainWindow->installEventFilter(mainWindowCloseFilter);
		}
	}

	ensureCommentBrowserDockConfig();
	startCommentViewerServiceDelayed();
	registerDskDocksDelayed();

	obs_frontend_add_tools_menu_item("DSK Multistream", showMainDock, nullptr);
	obs_frontend_add_tools_menu_item("DSK Stream Controls", showControlsDock, nullptr);
	obs_frontend_add_tools_menu_item("DSK Output Scenes", showSceneRouterDock, nullptr);
	obs_frontend_add_tools_menu_item("DSK Vertical Layout", showVerticalDock, nullptr);
	obs_frontend_add_tools_menu_item("Open DSK Comment Viewer", openCommentViewer, nullptr);

	dsk::logInfo("Frontend menu registered; DSK docks will be created on demand.");

	e2eRunner = std::make_unique<dsk::E2eAutoRunner>(manager.get());
	e2eRunner->schedule();
	scheduleVerticalUiStress();

	if (qEnvironmentVariableIsSet("DSK_AUTO_OPEN_SETTINGS")) {
		QTimer::singleShot(3000, pluginTimerContext(), []() { showMainDock(nullptr); });
	}

	dsk::logInfo("Frontend UI loaded");
}

void frontendEvent(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		initializeFrontendUi();
	} else if (event == OBS_FRONTEND_EVENT_PROFILE_CHANGED) {
		if (manager)
			manager->reloadForCurrentProfile();
	} else if (event == OBS_FRONTEND_EVENT_STREAMING_STARTED) {
		if (manager)
			manager->handleObsStreamingStarted();
		if (controlsDock)
			controlsDock->handleObsNativeStreamingStateChanged(true);
	} else if (event == OBS_FRONTEND_EVENT_STREAMING_STOPPED) {
		if (manager)
			manager->handleObsStreamingStopped();
		if (controlsDock)
			controlsDock->handleObsNativeStreamingStateChanged(false);
	} else if (event == OBS_FRONTEND_EVENT_SCENE_CHANGED) {
		if (manager)
			manager->handleObsSceneChanged();
	} else if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING) {
		if (verticalDock)
			verticalDock->prepareForUnload();
		if (manager)
			manager->prepareForSceneCollectionChange();
	} else if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED) {
		if (manager) {
			manager->refreshSceneIdentities();
			manager->handleObsSceneChanged();
		}
		if (verticalDock)
			verticalDock->handleSceneCollectionChanged();
		scheduleVerticalEditorLoad();
	} else if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP ||
		   event == OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN) {
		releaseObsSceneReferences();
	} else if (event == OBS_FRONTEND_EVENT_EXIT) {
		frontendExiting = true;
		prepareShutdown();
		removeFrontendUi();
	}
}

} // namespace

MODULE_EXPORT const char *obs_module_description()
{
	return "DSK Multistream";
}

bool obs_module_load()
{
	obs_frontend_add_event_callback(frontendEvent, nullptr);
	frontendCallbackRegistered = true;

	dsk::logInfo("Loaded");
	return true;
}

void obs_module_unload()
{
	dsk::logInfo("Unloading");
	if (frontendCallbackRegistered)
		obs_frontend_remove_event_callback(frontendEvent, nullptr);
	frontendCallbackRegistered = false;
	cancelVerticalEditorLoad();
	if (mainWindowCloseFilter) {
		if (auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window()))
			mainWindow->removeEventFilter(mainWindowCloseFilter);
		delete mainWindowCloseFilter.data();
		mainWindowCloseFilter = nullptr;
	}
	delete timerContext;
	timerContext = nullptr;
	removeFrontendUi();
	if (manager) {
		prepareShutdown();
		manager.reset();
	}
}
