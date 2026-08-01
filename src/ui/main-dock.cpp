#include "ui/main-dock.hpp"

#include "core/oauth-provider.hpp"
#include "core/diagnostics.hpp"
#include "core/experimental-features.hpp"
#include "core/output-target.hpp"
#include "core/youtube-api-warning.hpp"
#include "ui/scene-router-dock.hpp"
#include "ui/localized-text.hpp"
#include "ui/stream-control-assets.hpp"
#include "ui/stream-controls-dock.hpp"
#include "ui/target-edit-dialog.hpp"

#include <QAbstractItemView>
#include <QActionGroup>
#include <QDockWidget>
#include <QDesktopServices>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QStackedWidget>
#include <QStringList>
#include <QStyle>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <utility>

namespace dsk {
namespace {

constexpr int EnabledColumn = 0;
constexpr int NameColumn = 1;
constexpr int PlatformColumn = 2;
constexpr int EncoderColumn = 3;
constexpr int StateColumn = 4;

class GearMenuButton final : public QToolButton {
public:
	explicit GearMenuButton(QWidget *parent = nullptr) : QToolButton(parent)
	{
		setObjectName(QStringLiteral("dskStreamingMenu"));
		setFixedSize(22, 22);
		setCursor(Qt::PointingHandCursor);
		texture_.load(streamControlAssetPath("ui/settings-button.png"));
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing, true);
		if (!texture_.isNull()) {
			painter.drawPixmap(rect(), texture_);
			return;
		}
		const QRectF frame = rect().adjusted(2, 2, -2, -2);
		QLinearGradient surface(0, frame.top(), 0, frame.bottom());
		surface.setColorAt(0.0, underMouse() ? QColor(QStringLiteral("#273239")) : QColor(QStringLiteral("#20282e")));
		surface.setColorAt(1.0, QColor(QStringLiteral("#0d1215")));
		painter.setPen(QPen(underMouse() ? QColor(QStringLiteral("#35e6f2")) : QColor(QStringLiteral("#4b5962")), 2));
		painter.setBrush(surface);
		painter.drawRoundedRect(frame, 5, 5);

		painter.save();
		painter.translate(width() / 2.0, height() / 2.0);
		painter.scale(width() / 92.0, height() / 92.0);
		painter.setPen(Qt::NoPen);
		painter.setBrush(QColor(QStringLiteral("#d4dbe0")));
		for (int i = 0; i < 8; ++i) {
			painter.save();
			painter.rotate(i * 45.0);
			painter.drawRoundedRect(QRectF(-5.5, -29, 11, 17), 2, 2);
			painter.restore();
		}
		painter.drawEllipse(QRectF(-20, -20, 40, 40));
		painter.setBrush(QColor(QStringLiteral("#151c20")));
		painter.drawEllipse(QRectF(-8, -8, 16, 16));
		painter.restore();
	}

private:
	QPixmap texture_;
};

QColor stateColor(TargetState state, bool enabled, bool hasError, bool hasWarning = false)
{
	if (hasError || state == TargetState::Error)
		return QColor(224, 80, 80);
	if (hasWarning)
		return QColor(214, 166, 64);
	if (state == TargetState::Live)
		return QColor(72, 186, 105);
	if (state == TargetState::Starting || state == TargetState::Stopping)
		return QColor(214, 166, 64);
	if (!enabled)
		return QColor(145, 145, 145);
	return QColor(190, 190, 190);
}

bool hasYouTubeApiLogin(const OutputTarget &target)
{
	return target.authMode == TargetAuthMode::YouTubeOAuth &&
	       oauthHasUsableClientCredentials(target.authMode, target.oauthClientId, target.oauthClientSecret,
					     target.oauthClientSecretRef) &&
	       (!target.oauthRefreshToken.trimmed().isEmpty() || !target.oauthRefreshTokenRef.trimmed().isEmpty());
}

bool isEditingBlockedByState(const OutputTarget &target, const TargetRuntimeStatus &runtime)
{
	return target.state == TargetState::Starting || target.state == TargetState::Live || target.state == TargetState::Stopping ||
	       runtimeTransportIsRunning(runtime) || runtime.transport == TransportState::Stopping;
}

QString stateLabel(const OutputTarget &target, const TargetRuntimeStatus &runtime)
{
	if (runtimeHasSession(runtime))
		return runtimeStatusLabel(target, runtime);
	if (targetHasLiveYouTubeApiWarning(target))
		return QStringLiteral("LIVE");
	if (targetHasExpiredYouTubeLoginWarning(target))
		return QStringLiteral("Needs login");
	if (targetHasYouTubeApiWarning(target))
		return QStringLiteral("API warning");
	if (!target.lastError.isEmpty())
		return QStringLiteral("Error");
	if (target.state == TargetState::Live)
		return QStringLiteral("LIVE");
	if (target.state == TargetState::Starting)
		return QStringLiteral("STARTING");
	if (target.state == TargetState::Stopping)
		return QStringLiteral("STOPPING");
	if (!target.enabled)
		return QStringLiteral("Manual");

	QString error;
	if (validateOutputTargetConfig(target, &error)) {
		if (isYouTubeTarget(target) && target.authMode == TargetAuthMode::YouTubeOAuth && !hasYouTubeApiLogin(target))
			return QStringLiteral("Needs login");
		if (isYouTubeTarget(target) && target.authMode == TargetAuthMode::ManualRtmp)
			return QStringLiteral("RTMP ready");
		return QStringLiteral("Ready");
	}
	if (error == "Stream key is empty.")
		return QStringLiteral("Needs key");
	if (error == "Server URL is empty." || error.startsWith("Server URL"))
		return QStringLiteral("Needs server");
	if (error == "Login mode does not match the selected platform.")
		return QStringLiteral("Login mismatch");
	if (error == "Reconnect settings are invalid.")
		return QStringLiteral("Reconnect issue");
	return QStringLiteral("Needs setup");
}

QString stateToolTip(const OutputTarget &target, const TargetRuntimeStatus &runtime)
{
	if (runtimeHasSession(runtime))
		return runtimeStatusDetail(target, runtime);
	if (targetHasLiveYouTubeApiWarning(target))
		return QString("This target is live. YouTube API warning: %1").arg(target.lastError.trimmed());
	if (targetHasExpiredYouTubeLoginWarning(target))
		return QStringLiteral("Reconnect YouTube login to enable automatic YouTube Live start. Manual RTMP can still start.");
	if (targetHasYouTubeApiWarning(target))
		return QString("YouTube API start needs attention. RTMP can still start. Detail: %1").arg(target.lastError.trimmed());
	if (!target.lastError.isEmpty())
		return target.lastError;
	if (!target.enabled)
		return QStringLiteral("Automatic and bulk starts are off. Individual Start remains available.");
	if (target.state == TargetState::Live)
		return QStringLiteral("This target is live.");
	if (target.state == TargetState::Starting || target.state == TargetState::Stopping)
		return targetStateToString(target.state);

	QString error;
	if (validateOutputTargetConfig(target, &error)) {
		if (isYouTubeTarget(target) && target.authMode == TargetAuthMode::YouTubeOAuth && !hasYouTubeApiLogin(target))
			return QStringLiteral("Connect YouTube login to enable automatic YouTube Live start.");
		if (isYouTubeTarget(target) && target.authMode == TargetAuthMode::ManualRtmp)
			return QStringLiteral("Ready to send RTMP signal. YouTube Studio or YouTube login is needed for automatic broadcast start.");
		return QStringLiteral("Ready to start.");
	}
	return error;
}

QTableWidgetItem *newTextItem(const QString &text)
{
	auto *item = new QTableWidgetItem(text);
	item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
	return item;
}

QTableWidgetItem *newIconTextItem(const QIcon &icon, const QString &text)
{
	auto *item = newTextItem(text);
	item->setIcon(icon);
	return item;
}

QColor platformColor(const QString &platformId)
{
	if (platformId == "twitch")
		return QColor(145, 70, 255);
	if (platformId == "youtube")
		return QColor(230, 33, 23);
	if (platformId == "kick")
		return QColor(83, 252, 24);
	if (platformId == "tiktok")
		return QColor(17, 17, 24);
	return QColor(92, 126, 166);
}

QString platformBadgeText(const QString &platformId)
{
	if (platformId == "youtube")
		return QStringLiteral("YT");
	if (platformId == "tiktok")
		return QStringLiteral("TT");
	if (platformId == "custom")
		return QStringLiteral("RT");
	return platformId.left(1).toUpper();
}

QIcon badgeIcon(const QString &text, const QColor &background, const QColor &foreground = Qt::white)
{
	QPixmap pixmap(24, 24);
	pixmap.fill(Qt::transparent);

	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(Qt::NoPen);
	painter.setBrush(background);
	painter.drawRoundedRect(QRectF(2, 2, 20, 20), 4, 4);

	QFont font = painter.font();
	font.setBold(true);
	font.setPixelSize(text.size() > 1 ? 8 : 12);
	painter.setFont(font);
	painter.setPen(foreground);
	painter.drawText(pixmap.rect(), Qt::AlignCenter, text);
	return QIcon(pixmap);
}

QIcon platformIcon(const QString &platformId)
{
	const QColor foreground = platformId == "kick" ? QColor(12, 12, 12) : QColor(Qt::white);
	return badgeIcon(platformBadgeText(platformId), platformColor(platformId), foreground);
}

QIcon outputIcon(EncoderGroup group)
{
	QPixmap pixmap(24, 24);
	pixmap.fill(Qt::transparent);

	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(QPen(QColor(185, 185, 185), 2));
	painter.setBrush(QColor(70, 70, 70));
	if (group == EncoderGroup::DskVertical)
		painter.drawRoundedRect(QRectF(8, 3, 8, 18), 2, 2);
	else
		painter.drawRoundedRect(QRectF(3, 7, 18, 10), 2, 2);
	return QIcon(pixmap);
}

QPushButton *makeDockButton(QWidget *parent, const QString &text)
{
	auto *button = new QPushButton(parent);
	button->setText(text);
	button->setMinimumHeight(24);
	button->setMinimumWidth(72);
	return button;
}

} // namespace

MainDock::MainDock(OutputManager *manager, QWidget *parent)
	: QWidget(parent),
	  manager_(manager)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	setStyleSheet(QStringLiteral("MainDock { background-color: #080c0f; border: 0; }"));
	menuButton_ = new GearMenuButton(this);
	menuButton_->setToolTip(QStringLiteral("Choose DSK Streaming page"));
	menuButton_->setAccessibleName(QStringLiteral("DSK Streaming menu"));
	menuButton_->setPopupMode(QToolButton::InstantPopup);

	table_ = new QTableWidget(this);
	table_->setColumnCount(5);
	table_->setHorizontalHeaderLabels({"Auto", "Target", "Service", "Output", "Status"});
	table_->verticalHeader()->setVisible(false);
	table_->verticalHeader()->setDefaultSectionSize(28);
	table_->setSelectionBehavior(QAbstractItemView::SelectRows);
	table_->setSelectionMode(QAbstractItemView::SingleSelection);
	table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table_->setShowGrid(false);
	table_->setAlternatingRowColors(true);
	table_->setWordWrap(false);
	table_->setFrameShape(QFrame::StyledPanel);
	table_->horizontalHeader()->setStretchLastSection(false);
	table_->horizontalHeader()->setHighlightSections(false);
	table_->horizontalHeader()->setSectionResizeMode(NameColumn, QHeaderView::Stretch);
	table_->horizontalHeader()->setSectionResizeMode(PlatformColumn, QHeaderView::ResizeToContents);
	table_->horizontalHeader()->setSectionResizeMode(EncoderColumn, QHeaderView::ResizeToContents);
	table_->horizontalHeader()->setSectionResizeMode(StateColumn, QHeaderView::ResizeToContents);
	table_->setColumnWidth(EnabledColumn, 42);
	connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int, int) { editSelectedTarget(); });
	connect(table_, &QTableWidget::currentCellChanged, this, [this](int, int, int, int) { updateActionStates(); });
	connect(table_, &QTableWidget::itemChanged, this, &MainDock::handleRouteCheckChanged);

	auto *bottombar = new QHBoxLayout();
	bottombar->setContentsMargins(0, 0, 0, 0);
	bottombar->setSpacing(4);

	auto *add = makeDockButton(this, "Add");
	auto *check = makeDockButton(this, "Check");
	editButton_ = makeDockButton(this, "Edit");
	removeButton_ = makeDockButton(this, "Remove");
	add->setToolTip("Add a stream target.");
	check->setToolTip("Check which targets are ready before starting OBS streaming.");
	editButton_->setToolTip("Edit the selected target.");
	removeButton_->setToolTip("Remove the selected target.");
	connect(add, &QPushButton::clicked, this, &MainDock::addTarget);
	connect(check, &QPushButton::clicked, this, &MainDock::checkRoutes);
	connect(editButton_, &QPushButton::clicked, this, &MainDock::editSelectedTarget);
	connect(removeButton_, &QPushButton::clicked, this, &MainDock::removeSelectedTarget);

	bottombar->addWidget(add);
	bottombar->addWidget(check);
	bottombar->addWidget(editButton_);
	bottombar->addWidget(removeButton_);
	bottombar->addStretch(1);

	streamControls_ = new StreamControlsDock(manager_, this);
	streamControls_->setObjectName(QStringLiteral("dskStreamingControls"));
	connect(streamControls_, &StreamControlsDock::editTargetRequested,
		this, &MainDock::editTargetById);
	if (experimentalSceneRoutingEnabled()) {
		sceneRouter_ = new SceneRouterDock(manager_, this);
		sceneRouter_->setObjectName(QStringLiteral("dskStreamingScenes"));
	}
	table_->setObjectName(QStringLiteral("dskStreamingRoutes"));

	targetsPage_ = new QWidget(this);
	targetsPage_->setObjectName(QStringLiteral("dskStreamingTargetsPage"));
	auto *targetsLayout = new QVBoxLayout(targetsPage_);
	targetsLayout->setContentsMargins(0, 0, 0, 0);
	targetsLayout->setSpacing(4);
	targetsLayout->addWidget(table_, 1);
	targetsLayout->addLayout(bottombar);

	pages_ = new QStackedWidget(this);
	pages_->setObjectName(QStringLiteral("dskStreamingPages"));
	pages_->addWidget(streamControls_);
	pages_->addWidget(targetsPage_);
	if (sceneRouter_)
		pages_->addWidget(sceneRouter_);

	auto *pageMenu = new QMenu(menuButton_);
	auto *pageActions = new QActionGroup(pageMenu);
	pageActions->setExclusive(true);
	auto addPageAction = [this, pageMenu, pageActions](const QString &label, QWidget *page, bool checked = false) {
		auto *action = pageMenu->addAction(label);
		action->setCheckable(true);
		action->setChecked(checked);
		pageActions->addAction(action);
		connect(action, &QAction::triggered, this, [this, page, label]() { showPage(page, label); });
	};
	addPageAction(QStringLiteral("Controls"), streamControls_, true);
	addPageAction(QStringLiteral("Targets"), targetsPage_);
	if (sceneRouter_)
		addPageAction(QStringLiteral("Scene Routing (Experimental)"), sceneRouter_);
	pageMenu->addSeparator();
	auto addExternalAction = [this, pageMenu](const QString &label, const char *url) {
		auto *action = pageMenu->addAction(label);
		connect(action, &QAction::triggered, this,
			[url]() { QDesktopServices::openUrl(QUrl(QString::fromUtf8(url))); });
	};
	addExternalAction(localizedText("DSKMenu.Privacy", "Privacy Policy"),
			  "https://dsk.dasoku.org/privacy");
	addExternalAction(localizedText("DSKMenu.Terms", "Terms of Use"),
			  "https://dsk.dasoku.org/terms");
	addExternalAction(localizedText("DSKMenu.GooglePermissions", "Google permissions / revoke access"),
			  "https://security.google.com/settings/security/permissions");
	addExternalAction(localizedText("DSKMenu.TwitchSimulcasting", "Twitch simulcasting terms"),
			  "https://www.twitch.tv/p/terms-of-service#simulcasting");
	menuButton_->setMenu(pageMenu);

	auto *toolbarHost = new QWidget(this);
	toolbarHost->setObjectName(QStringLiteral("dskStreamingToolbarHost"));
	toolbarHost->setFixedHeight(60);
	toolbarHost->setStyleSheet(QStringLiteral(
		"QWidget#dskStreamingToolbarHost { background-color: #080c0f; border: 0; }"));
	auto *toolbarHostLayout = new QHBoxLayout(toolbarHost);
	toolbarHostLayout->setContentsMargins(5, 5, 5, 5);
	toolbarHostLayout->setSpacing(0);

	auto *toolbar = new QWidget(toolbarHost);
	toolbar->setObjectName(QStringLiteral("dskStreamingToolbar"));
	toolbar->setFixedHeight(50);
	toolbar->setStyleSheet(QStringLiteral(
		"QWidget#dskStreamingToolbar { background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
		"stop:0 #1a202b, stop:0.5 #141a24, stop:1 #1b212c); border: 1px solid #2d3541; "
		"border-radius: 9px; }"));
	auto *toolbarLayout = new QHBoxLayout(toolbar);
	toolbarLayout->setContentsMargins(6, 4, 8, 4);
	toolbarLayout->setSpacing(8);
	pageLabel_ = new QLabel(QStringLiteral("Controls"), toolbar);
	pageLabel_->setObjectName(QStringLiteral("dskStreamingPageLabel"));
	pageLabel_->setStyleSheet(QStringLiteral(
		"QLabel#dskStreamingPageLabel { color: #b8c0c7; border: 0; background: transparent; "
		"font-family: 'Bahnschrift Light Condensed'; font-size: 15px; font-weight: 350; letter-spacing: 1px; }"));
	toolbarLayout->addWidget(menuButton_);
	toolbarLayout->addWidget(pageLabel_);
	toolbarLayout->addStretch(1);
	toolbarLayout->addWidget(streamControls_->allToggleButton());
	toolbarHostLayout->addWidget(toolbar);
	layout->addWidget(pages_, 1);
	layout->addWidget(toolbarHost);

	connect(manager_, &OutputManager::targetsChanged, this, &MainDock::scheduleRefresh, Qt::QueuedConnection);
	connect(manager_, &OutputManager::targetRuntimeChanged, this, &MainDock::scheduleRefresh, Qt::QueuedConnection);

	QTimer::singleShot(0, this, [this]() {
		updateDockTitle(currentPageTitle_);
		scheduleRefresh();
	});
	QTimer::singleShot(1000, this, [this]() {
		editArmed_ = true;
		updateActionStates();
	});
}

void MainDock::handleObsNativeStreamingStateChanged(bool active)
{
	if (streamControls_)
		streamControls_->handleObsNativeStreamingStateChanged(active);
}

void MainDock::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	updateDockTitle(currentPageTitle_);
	if (pages_ && pages_->currentWidget() == targetsPage_ && targetRefreshGate_.isDirty())
		scheduleRefresh();
}

void MainDock::scheduleRefresh()
{
	targetRefreshGate_.markDirty();
	if (!pages_ || pages_->currentWidget() != targetsPage_ || !targetsPage_ || !targetsPage_->isVisible())
		return;
	if (refreshPending_)
		return;

	refreshPending_ = true;
	QTimer::singleShot(0, this, [this]() {
		refreshPending_ = false;
		if (targetRefreshGate_.takeIfVisible(
			    pages_ && pages_->currentWidget() == targetsPage_ && targetsPage_ && targetsPage_->isVisible()))
			refresh();
	});
}

void MainDock::refresh()
{
	if (refreshing_ || !manager_ || !table_)
		return;

	const QString selectedId = targetIdForRow(table_ ? table_->currentRow() : -1);
	refreshing_ = true;
	const QSignalBlocker tableBlocker(table_);
	const QVector<OutputTarget> targets = manager_->targets();

	table_->setUpdatesEnabled(false);
	table_->clearContents();
	table_->setRowCount(targets.size());
	int selectedRow = -1;
	for (int row = 0; row < targets.size(); ++row) {
		const auto &target = targets[row];
		const TargetRuntimeStatus runtime = manager_->runtimeStatusForTarget(target.id);
		if (target.id == selectedId)
			selectedRow = row;
		auto *enabled = new QTableWidgetItem();
		enabled->setData(Qt::UserRole, target.id);
		enabled->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
		enabled->setCheckState(target.enabled ? Qt::Checked : Qt::Unchecked);
		enabled->setTextAlignment(Qt::AlignCenter);
		enabled->setToolTip(target.enabled
				    ? "Allow Start All and OBS-linked automatic starts."
				    : "Automatic starts are off; individual Start remains available.");
		table_->setItem(row, EnabledColumn, enabled);

		table_->setItem(row, NameColumn, newTextItem(target.name));
		const auto preset = manager_->platforms().presetById(target.platformId);
		auto *platform = newIconTextItem(platformIcon(target.platformId), preset.displayName);
		platform->setToolTip(QString("Stream service: %1").arg(preset.displayName));
		table_->setItem(row, PlatformColumn, platform);
		const EncoderGroup displayGroup = target.encoderGroup;
		auto *encoder = newIconTextItem(outputIcon(displayGroup), encoderGroupToString(displayGroup));
		encoder->setToolTip(displayGroup == EncoderGroup::DskVertical ? "Vertical DSK output" : "Horizontal DSK output");
		table_->setItem(row, EncoderColumn, encoder);

		auto *state = newTextItem(stateLabel(target, runtime));
		QString readinessError;
		const bool hasReadinessIssue = target.enabled && target.state == TargetState::Stopped &&
					       !validateOutputTargetConfig(target, &readinessError);
		const bool hasWarning = targetHasYouTubeApiWarning(target) ||
					(isYouTubeTarget(target) && runtimePlatformIsWarning(runtime.platform) &&
					 runtimeTransportIsRunning(runtime));
		const bool hasBlockingIssue = ((!target.lastError.isEmpty() && !hasWarning) || hasReadinessIssue) &&
					      !runtimeTransportIsRunning(runtime);
		state->setForeground(stateColor(target.state, target.enabled, hasBlockingIssue, hasWarning));
		state->setTextAlignment(Qt::AlignCenter);
		state->setToolTip(stateToolTip(target, runtime));
		table_->setItem(row, StateColumn, state);

	}
	table_->resizeColumnsToContents();
	table_->horizontalHeader()->setSectionResizeMode(NameColumn, QHeaderView::Stretch);
	table_->setColumnWidth(EnabledColumn, 42);
	if (selectedRow >= 0)
		table_->setCurrentCell(selectedRow, NameColumn);
	table_->setUpdatesEnabled(true);
	refreshing_ = false;
	updateActionStates();
}

void MainDock::showPage(QWidget *page, const QString &title)
{
	if (!pages_ || !page)
		return;
	pages_->setCurrentWidget(page);
	currentPageTitle_ = title;
	if (pageLabel_)
		pageLabel_->setText(title);
	if (streamControls_ && streamControls_->allToggleButton())
		streamControls_->allToggleButton()->setVisible(page == streamControls_);
	updateDockTitle(title);
	if (page == targetsPage_)
		scheduleRefresh();
}

void MainDock::updateDockTitle(const QString &pageTitle)
{
	Q_UNUSED(pageTitle);
	const QString dockTitle = QStringLiteral("DSK Streaming");
	for (QWidget *ancestor = parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
		if (auto *dock = qobject_cast<QDockWidget *>(ancestor)) {
			dock->setWindowTitle(dockTitle);
			return;
		}
	}
}

void MainDock::updateActionStates()
{
	if (editButton_)
		editButton_->setEnabled(false);
	if (removeButton_)
		removeButton_->setEnabled(false);

	if (refreshing_ || !manager_ || !table_)
		return;
	if (!editArmed_)
		return;

	const int currentRow = table_->currentRow();
	if (currentRow < 0 || currentRow >= table_->rowCount())
		return;

	const QString selectedId = targetIdForRow(currentRow);
	if (selectedId.isEmpty())
		return;

	const OutputTarget *selectedTarget = nullptr;
	for (const auto &target : manager_->targets()) {
		if (target.id == selectedId) {
			selectedTarget = &target;
			break;
		}
	}
	const bool hasSelection = selectedTarget;
	const bool canEdit = hasSelection && !isEditingBlockedByState(*selectedTarget, manager_->runtimeStatusForTarget(selectedTarget->id));
	if (editButton_)
		editButton_->setEnabled(canEdit);
	if (removeButton_)
		removeButton_->setEnabled(hasSelection);
}

void MainDock::addTarget()
{
	TargetEditDialog dialog(manager_->platforms(), this);
	dialog.setNewTargetDefaults(newTargetId());
	if (dialog.exec() == QDialog::Accepted) {
		OutputTarget accepted;
		dialog.fillTarget(accepted);
		if (!manager_->addTarget(accepted))
			QMessageBox::warning(this, QStringLiteral("Add Target"),
					     QStringLiteral("DSK could not save the new target. No target was added."));
	}
}

void MainDock::editSelectedTarget()
{
	logInfo("Edit target requested");
	if (!editArmed_ || !manager_ || !table_)
		return;

	const int row = table_->currentRow();
	const QString id = targetIdForRow(row);
	logInfo(QString("Edit target row=%1 id=%2").arg(row).arg(id));
	editTargetById(id);
}

void MainDock::editTargetById(const QString &id)
{
	if (!editArmed_ || !manager_)
		return;
	if (id.isEmpty())
		return;

	const OutputTarget *selected = nullptr;
	for (const auto &target : manager_->targets()) {
		if (target.id == id) {
			selected = &target;
			break;
		}
	}
	if (!selected)
		return;
	if (isEditingBlockedByState(*selected, manager_->runtimeStatusForTarget(selected->id))) {
		logInfo(QString("Edit target blocked while state=%1 id=%2").arg(targetStateToString(selected->state), id));
		QMessageBox::information(this,
					 QStringLiteral("Edit Target"),
					 QStringLiteral("Stop the target before editing it."));
		return;
	}

	logInfo(QString("Edit target source ready: %1 platform=%2").arg(selected->id, selected->platformId));
	TargetEditDialog dialog(manager_->platforms(), this);
	logInfo("Edit target dialog constructed");
	dialog.setTarget(*selected);
	logInfo("Edit target dialog populated");
	if (dialog.exec() == QDialog::Accepted) {
		logInfo("Edit target dialog accepted");
		const bool saved = manager_->mutateTargetForUi(id, [&](OutputTarget &updated) {
			dialog.fillTarget(updated);
		});
		if (!saved) {
			QMessageBox::warning(this,
					     QStringLiteral("Save Target"),
					     QStringLiteral("DSK could not save the target settings. The edit was not applied."));
		}
		logInfo("Edit target updated");
	}
	logInfo("Edit target finished");
}

void MainDock::removeSelectedTarget()
{
	const int row = table_->currentRow();
	const QString id = targetIdForRow(row);
	if (id.isEmpty())
		return;
	if (QMessageBox::question(this, "Remove Target", "Remove selected stream target?") == QMessageBox::Yes)
		manager_->removeTarget(id);
}

void MainDock::checkRoutes()
{
	if (manager_->targets().isEmpty()) {
		QMessageBox::information(this, "Check Routes", "No stream targets are configured.");
		return;
	}

	QStringList lines;
	int ready = 0;
	int issues = 0;
	int off = 0;
	int live = 0;

	for (const auto &target : manager_->targets()) {
		const TargetRuntimeStatus runtime = manager_->runtimeStatusForTarget(target.id);
		const auto preset = manager_->platforms().presetById(target.platformId);
		const EncoderGroup displayGroup = target.encoderGroup;
		const QString output = encoderGroupToString(displayGroup);
		const QString prefix = QString("%1 - %2 - %3").arg(target.name, preset.displayName, output);

		if (!target.enabled) {
			++off;
			lines.push_back(QString("Off: %1").arg(prefix));
			continue;
		}
		if (target.state == TargetState::Live || runtimeTransportIsRunning(runtime)) {
			++live;
			const QString detail = runtimeStatusDetail(target, runtime);
			const bool showDetail = isYouTubeTarget(target) &&
						(runtimePlatformIsWarning(runtime.platform) || targetHasYouTubeApiWarning(target));
			lines.push_back(showDetail ? QString("%1: %2 - %3").arg(runtimeStatusLabel(target, runtime), prefix, detail)
						   : QString("%1: %2").arg(runtimeStatusLabel(target, runtime), prefix));
			continue;
		}
		if (!target.lastError.isEmpty() && targetHasYouTubeApiWarning(target)) {
			++ready;
			lines.push_back(targetHasExpiredYouTubeLoginWarning(target)
						? QString("Ready with warning: %1: reconnect YouTube login for automatic Live start.").arg(prefix)
						: QString("Ready with warning: %1: YouTube API start needs attention.").arg(prefix));
			continue;
		}
		if (!target.lastError.isEmpty()) {
			++issues;
			lines.push_back(QString("Issue: %1: %2").arg(prefix, target.lastError));
			continue;
		}

		QString error;
		if (validateOutputTargetConfig(target, &error)) {
			++ready;
			lines.push_back(QString("Ready: %1").arg(prefix));
		} else {
			++issues;
			lines.push_back(QString("Issue: %1: %2").arg(prefix, error));
		}
	}

	const QString summary = QString("%1 ready / %2 live / %3 issues / %4 off").arg(ready).arg(live).arg(issues).arg(off);
	const QString details = lines.isEmpty() ? summary : QString("%1\n\n%2").arg(summary, lines.join('\n'));
	QMessageBox::information(this, QStringLiteral("Target Check"), details);
}

void MainDock::handleRouteCheckChanged(QTableWidgetItem *item)
{
	if (refreshing_ || !item || item->column() != EnabledColumn)
		return;

	const QString id = item->data(Qt::UserRole).toString();
	if (!id.isEmpty())
		manager_->setTargetEnabled(id, item->checkState() == Qt::Checked);
}

QString MainDock::targetIdForRow(int row) const
{
	if (!table_ || row < 0 || row >= table_->rowCount() || !table_->item(row, EnabledColumn))
		return {};
	return table_->item(row, EnabledColumn)->data(Qt::UserRole).toString();
}

} // namespace dsk
