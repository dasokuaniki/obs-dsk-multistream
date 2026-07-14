#include "ui/main-dock.hpp"

#include "core/oauth-provider.hpp"
#include "core/diagnostics.hpp"
#include "core/output-target.hpp"
#include "core/youtube-api-warning.hpp"
#include "ui/target-edit-dialog.hpp"

#include <QAbstractItemView>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QTime>
#include <QVBoxLayout>

#include <utility>

namespace dsk {
namespace {

constexpr int EnabledColumn = 0;
constexpr int NameColumn = 1;
constexpr int PlatformColumn = 2;
constexpr int EncoderColumn = 3;
constexpr int StateColumn = 4;

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
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(4);

	auto *topbar = new QHBoxLayout();
	topbar->setContentsMargins(0, 0, 0, 0);
	topbar->setSpacing(6);

	summary_ = new QLabel(this);
	summary_->setText("No targets");
	auto summaryFont = summary_->font();
	summaryFont.setBold(true);
	summary_->setFont(summaryFont);

	topbar->addWidget(summary_);
	topbar->addStretch(1);

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
	checkButton_ = makeDockButton(this, "Check");
	editButton_ = makeDockButton(this, "Edit");
	removeButton_ = makeDockButton(this, "Remove");
	activityToggle_ = makeDockButton(this, "Activity");
	add->setToolTip("Add a stream target.");
	checkButton_->setToolTip("Check which routes are ready before starting OBS streaming.");
	editButton_->setToolTip("Edit the selected target.");
	removeButton_->setToolTip("Remove the selected target.");
	activityToggle_->setToolTip("Open recent DSK Multistream activity.");
	connect(add, &QPushButton::clicked, this, &MainDock::addTarget);
	connect(checkButton_, &QPushButton::clicked, this, &MainDock::checkRoutes);
	connect(editButton_, &QPushButton::clicked, this, &MainDock::editSelectedTarget);
	connect(removeButton_, &QPushButton::clicked, this, &MainDock::removeSelectedTarget);
	connect(activityToggle_, &QPushButton::clicked, this, &MainDock::toggleActivityLog);

	status_ = new QLabel(this);
	status_->setText("Ready");
	status_->setMinimumWidth(120);
	status_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

	bottombar->addWidget(add);
	bottombar->addWidget(checkButton_);
	bottombar->addWidget(editButton_);
	bottombar->addWidget(removeButton_);
	bottombar->addWidget(activityToggle_);
	bottombar->addStretch(1);
	bottombar->addWidget(status_);

	activityLog_ = new QPlainTextEdit(this);
	activityLog_->setReadOnly(true);
	activityLog_->setMaximumBlockCount(80);
	activityLog_->setPlaceholderText("Activity log");
	activityLog_->appendPlainText("Ready");

	tabs_ = new QTabWidget(this);
	tabs_->addTab(table_, "Routes");
	tabs_->addTab(activityLog_, "Activity");

	statsTimer_ = new QTimer(this);
	statsTimer_->setInterval(1000);
	connect(statsTimer_, &QTimer::timeout, this, &MainDock::updateStats);
	statsTimer_->start();

	layout->addLayout(topbar);
	layout->addWidget(tabs_);
	layout->addLayout(bottombar);

	connect(manager_, &OutputManager::targetsChanged, this, &MainDock::refresh, Qt::QueuedConnection);
	connect(manager_, &OutputManager::targetRuntimeChanged, this, &MainDock::refresh, Qt::QueuedConnection);
	connect(manager_, &OutputManager::statusMessage, status_, &QLabel::setText);
	connect(manager_, &OutputManager::statusMessage, this, &MainDock::appendActivity);

	QTimer::singleShot(0, this, [this]() { refresh(); });
	QTimer::singleShot(1000, this, [this]() {
		editArmed_ = true;
		updateActionStates();
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
	updateSummary();
	updateActionStates();
}

void MainDock::updateStats()
{
	updateSummary();
}

void MainDock::updateSummary()
{
	if (!manager_ || !summary_)
		return;

	int live = 0;
	int enabled = 0;
	int ready = 0;
	int errors = 0;
	const int total = manager_->targets().size();
	for (const auto &target : manager_->targets()) {
		const TargetRuntimeStatus runtime = manager_->runtimeStatusForTarget(target.id);
		if (target.enabled)
			++enabled;
		if (target.state == TargetState::Live || runtimeTransportIsRunning(runtime))
			++live;
		else if (target.state == TargetState::Error || runtime.transport == TransportState::Failed ||
			 (!target.lastError.isEmpty() && !targetHasYouTubeApiWarning(target)))
			++errors;
		else if (target.enabled) {
			QString error;
			if (validateOutputTargetConfig(target, &error))
				++ready;
			else
				++errors;
		}
	}

	if (total == 0) {
		summary_->setText("No stream targets");
		return;
	}

	summary_->setText(QString("%1 live  /  %2 ready  /  %3 issues  /  %4 auto  /  %5 targets")
				  .arg(live)
				  .arg(ready)
				  .arg(errors)
				  .arg(enabled)
				  .arg(total));
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

void MainDock::appendActivity(const QString &message)
{
	if (!activityLog_ || message.trimmed().isEmpty())
		return;

	activityLog_->appendPlainText(QString("%1  %2").arg(QTime::currentTime().toString("HH:mm:ss"), message));
}

void MainDock::toggleActivityLog()
{
	if (!activityLog_ || !activityToggle_ || !tabs_)
		return;

	const int activityIndex = tabs_->indexOf(activityLog_);
	if (tabs_->currentIndex() == activityIndex) {
		tabs_->setCurrentIndex(0);
	} else if (activityIndex >= 0) {
		tabs_->setCurrentIndex(activityIndex);
	}
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
		if (status_)
			status_->setText(QStringLiteral("Stop the target before editing."));
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
	appendActivity(QString("Route check: %1").arg(summary));
	for (const QString &line : lines)
		appendActivity(QString("  %1").arg(line));
	if (status_)
		status_->setText(QString("Check: %1").arg(summary));
	if (tabs_ && activityLog_) {
		const int activityIndex = tabs_->indexOf(activityLog_);
		if (activityIndex >= 0)
			tabs_->setCurrentIndex(activityIndex);
	}
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
