#include "ui/scene-router-dock.hpp"

#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QVBoxLayout>

namespace dsk {
namespace {

QString outputModeText(EncoderGroup group)
{
	if (group == EncoderGroup::DskVertical)
		return QStringLiteral("Vertical layout");
	return QStringLiteral("Horizontal output");
}

QString targetDisplayName(const OutputTarget &target)
{
	return target.name.trimmed().isEmpty() ? QStringLiteral("Untitled") : target.name.trimmed();
}

bool canRouteTarget(const OutputTarget &target)
{
	return target.encoderGroup == EncoderGroup::DskHorizontal;
}

QString targetNoteText(const OutputTarget &target)
{
	if (target.encoderGroup == EncoderGroup::DskVertical)
		return QStringLiteral("Uses DSK Vertical Layout");
	return QStringLiteral("Separate scene output");
}

} // namespace

SceneRouterDock::SceneRouterDock(OutputManager *manager, QWidget *parent)
	: QWidget(parent),
	  manager_(manager)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(6, 6, 6, 6);
	layout->setSpacing(6);

	currentScene_ = new QLabel(this);
	currentScene_->setStyleSheet(QStringLiteral("QLabel { color: #d8d8d8; font-weight: 700; }"));
	currentScene_->setText(QStringLiteral("Current OBS scene: -"));
	layout->addWidget(currentScene_);

	summary_ = new QLabel(this);
	summary_->setStyleSheet(QStringLiteral("QLabel { color: #9fa4ac; font-size: 11px; }"));
	summary_->setText(QStringLiteral("No stream targets configured."));
	layout->addWidget(summary_);

	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *scrollBody = new QWidget(scroll);
	rows_ = new QVBoxLayout(scrollBody);
	rows_->setContentsMargins(0, 0, 0, 0);
	rows_->setSpacing(5);
	rows_->addStretch(1);
	scroll->setWidget(scrollBody);
	layout->addWidget(scroll, 1);

	auto *refreshButton = new QPushButton(QStringLiteral("Refresh Scenes"), this);
	refreshButton->setMinimumHeight(26);
	refreshButton->setStyleSheet(QStringLiteral(
		"QPushButton { padding: 4px 10px; color: #e8e8e8; background-color: #34383f; "
		"border: 1px solid #4b515a; border-radius: 3px; font-weight: 700; }"
		"QPushButton:hover { background-color: #3e444d; }"));
	connect(refreshButton, &QPushButton::clicked, this, &SceneRouterDock::refresh);
	layout->addWidget(refreshButton, 0, Qt::AlignLeft);

	setStyleSheet(QStringLiteral("SceneRouterDock { background-color: #1f1f1f; }"));

	if (manager_) {
		connect(manager_, &OutputManager::targetsChanged, this, &SceneRouterDock::refresh, Qt::QueuedConnection);
		connect(manager_, &OutputManager::verticalLayoutChanged, this, &SceneRouterDock::refresh, Qt::QueuedConnection);
	}

	refresh();
}

void SceneRouterDock::populateSceneCombo(QComboBox *combo,
					 const QStringList &scenes,
					 const QString &selected,
					 bool includeNone) const
{
	if (!combo)
		return;

	const QSignalBlocker blocker(combo);
	combo->clear();
	if (includeNone)
		combo->addItem(QStringLiteral("-"), QString());
	for (const QString &scene : scenes)
		combo->addItem(scene, scene);
	if (!selected.trimmed().isEmpty() && combo->findData(selected.trimmed()) < 0)
		combo->addItem(QStringLiteral("Missing: %1").arg(selected.trimmed()), selected.trimmed());

	const int index = combo->findData(selected.trimmed());
	combo->setCurrentIndex(index >= 0 ? index : 0);
}

QString SceneRouterDock::routeForScene(const OutputTarget &target, const QString &obsSceneName, const QString &obsSceneUuid) const
{
	const QString current = obsSceneName.trimmed();
	if (current.isEmpty())
		return {};

	for (const auto &route : target.sceneRoutes) {
		const bool matches = route.obsSceneUuid.trimmed().isEmpty()
					     ? route.obsSceneName.trimmed() == current
					     : !obsSceneUuid.isEmpty() && route.obsSceneUuid.trimmed() == obsSceneUuid;
		if (matches) {
			const QString resolved = manager_ ? manager_->resolvedObsSceneName(route.outputSceneUuid, route.outputSceneName)
							  : route.outputSceneName.trimmed();
			return resolved.isEmpty() ? route.outputSceneName.trimmed() : resolved;
		}
	}
	return {};
}

QWidget *SceneRouterDock::createTargetRow(const OutputTarget &target,
					  const QStringList &scenes,
					  const QString &currentObsScene,
					  const QString &currentObsSceneUuid)
{
	auto *row = new QWidget(this);
	row->setObjectName(QStringLiteral("outputSceneRow"));
	row->setStyleSheet(QStringLiteral(
		"QWidget#outputSceneRow { background-color: #292929; border: 1px solid #3a3a3a; border-radius: 4px; }"
		"QLabel { border: 0; background: transparent; }"
		"QComboBox { min-height: 24px; color: #ededed; background-color: #22252a; "
		"border: 1px solid #474d56; border-radius: 3px; padding: 2px 6px; }"
		"QComboBox:disabled { color: #777d85; background-color: #252525; border-color: #333333; }"));

	auto *grid = new QGridLayout(row);
	grid->setContentsMargins(8, 6, 8, 6);
	grid->setHorizontalSpacing(7);
	grid->setVerticalSpacing(4);

	auto *name = new QLabel(targetDisplayName(target), row);
	name->setStyleSheet(QStringLiteral("QLabel { color: #f0f0f0; font-weight: 800; font-size: 12px; }"));
	auto *note = new QLabel(QStringLiteral("%1 - %2").arg(outputModeText(target.encoderGroup), targetNoteText(target)), row);
	note->setStyleSheet(QStringLiteral("QLabel { color: #a4a7ad; font-size: 10px; }"));

	auto *mode = new QComboBox(row);
	mode->addItem(targetSceneModeDisplayName(TargetSceneMode::FollowObs),
		      targetSceneModeToString(TargetSceneMode::FollowObs));
	mode->addItem(targetSceneModeDisplayName(TargetSceneMode::FixedScene),
		      targetSceneModeToString(TargetSceneMode::FixedScene));
	mode->addItem(targetSceneModeDisplayName(TargetSceneMode::LinkedScene),
		      targetSceneModeToString(TargetSceneMode::LinkedScene));
	const int modeIndex = mode->findData(targetSceneModeToString(target.sceneMode));
	mode->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);

	auto *scene = new QComboBox(row);
	const QString resolvedFallback =
		manager_ ? manager_->resolvedObsSceneName(target.sceneUuid, target.sceneName) : target.sceneName;
	populateSceneCombo(scene,
			 scenes,
			 resolvedFallback.isEmpty() ? target.sceneName : resolvedFallback,
			 true);

	auto *currentLink = new QComboBox(row);
	populateSceneCombo(currentLink, scenes, routeForScene(target, currentObsScene, currentObsSceneUuid), true);

	const bool canRoute = canRouteTarget(target);
	const TargetRuntimeStatus runtime = manager_ ? manager_->runtimeStatusForTarget(target.id) : TargetRuntimeStatus{};
	const bool targetRunning = runtimeHasSession(runtime) || runtimeTransportIsRunning(runtime) ||
				   runtimeTransportIsBusy(runtime) || target.state == TargetState::Live ||
				   target.state == TargetState::Starting || target.state == TargetState::Stopping;
	const bool canEditRoute = canRoute && !targetRunning;
	const TargetSceneMode selectedMode = target.sceneMode;
	mode->setEnabled(canEditRoute);
	scene->setEnabled(canEditRoute && selectedMode != TargetSceneMode::FollowObs);
	currentLink->setEnabled(canEditRoute && selectedMode == TargetSceneMode::LinkedScene && !currentObsScene.trimmed().isEmpty());

	auto *modeLabel = new QLabel(QStringLiteral("Mode"), row);
	auto *sceneLabel = new QLabel(QStringLiteral("Scene"), row);
	auto *linkLabel = new QLabel(QStringLiteral("This OBS scene"), row);
	const QString labelStyle = QStringLiteral("QLabel { color: #8f949c; font-size: 10px; }");
	modeLabel->setStyleSheet(labelStyle);
	sceneLabel->setStyleSheet(labelStyle);
	linkLabel->setStyleSheet(labelStyle);

	grid->addWidget(name, 0, 0, 1, 3);
	grid->addWidget(note, 1, 0, 1, 3);
	grid->addWidget(modeLabel, 2, 0);
	grid->addWidget(sceneLabel, 2, 1);
	grid->addWidget(linkLabel, 2, 2);
	grid->addWidget(mode, 3, 0);
	grid->addWidget(scene, 3, 1);
	grid->addWidget(currentLink, 3, 2);
	grid->setColumnStretch(0, 1);
	grid->setColumnStretch(1, 1);
	grid->setColumnStretch(2, 1);

	if (!canEditRoute) {
		const QString reason = targetRunning
			? QStringLiteral("Stop this output before changing its scene routing.")
			: QStringLiteral("Only DSK horizontal independent outputs can stream a separate OBS scene.");
		mode->setToolTip(reason);
		scene->setToolTip(mode->toolTip());
		currentLink->setToolTip(mode->toolTip());
	}

	const QString targetId = target.id;
	connect(mode, &QComboBox::currentIndexChanged, this, [this, mode, scene, currentLink, targetId, currentObsScene]() {
		if (refreshing_ || !manager_)
			return;
		const TargetSceneMode selected = targetSceneModeFromString(mode->currentData().toString());
		scene->setEnabled(selected != TargetSceneMode::FollowObs);
		currentLink->setEnabled(selected == TargetSceneMode::LinkedScene && !currentObsScene.trimmed().isEmpty());
		manager_->setTargetSceneMode(targetId, selected, scene->currentData().toString());
	});
	connect(scene, &QComboBox::currentIndexChanged, this, [this, mode, scene, targetId]() {
		if (refreshing_ || !manager_)
			return;
		manager_->setTargetSceneMode(targetId, targetSceneModeFromString(mode->currentData().toString()),
					     scene->currentData().toString());
	});
	connect(currentLink, &QComboBox::currentIndexChanged, this, [this, mode, scene, currentLink, targetId, currentObsScene]() {
		if (refreshing_ || !manager_ || currentObsScene.trimmed().isEmpty())
			return;
		const QString outputScene = currentLink->currentData().toString();
		if (!outputScene.trimmed().isEmpty())
			manager_->setTargetSceneMode(targetId, TargetSceneMode::LinkedScene, scene->currentData().toString());
		manager_->upsertTargetSceneRoute(targetId, currentObsScene, outputScene);
		mode->setCurrentIndex(mode->findData(targetSceneModeToString(TargetSceneMode::LinkedScene)));
	});

	return row;
}

void SceneRouterDock::clearRows()
{
	if (!rows_)
		return;

	while (rows_->count() > 1) {
		QLayoutItem *item = rows_->takeAt(0);
		if (QWidget *widget = item ? item->widget() : nullptr)
			widget->deleteLater();
		delete item;
	}
}

void SceneRouterDock::refresh()
{
	if (refreshing_ || !manager_ || !rows_)
		return;

	refreshing_ = true;
	clearRows();

	const QString currentObsScene = manager_->currentObsSceneName();
	const QString currentObsSceneUuid = manager_->currentObsSceneUuid();
	currentScene_->setText(currentObsScene.isEmpty()
				       ? QStringLiteral("Current OBS scene: -")
				       : QStringLiteral("Current OBS scene: %1").arg(currentObsScene));

	const QStringList scenes = manager_->obsSceneNames();
	const auto &targets = manager_->targets();
	int routable = 0;
	for (const auto &target : targets) {
		if (canRouteTarget(target))
			++routable;
		rows_->insertWidget(rows_->count() - 1,
				    createTargetRow(target, scenes, currentObsScene, currentObsSceneUuid));
	}

	if (targets.isEmpty()) {
		auto *empty = new QLabel(QStringLiteral("No stream targets configured."), this);
		empty->setAlignment(Qt::AlignCenter);
		empty->setMinimumHeight(42);
		empty->setStyleSheet(QStringLiteral("QLabel { color: #9fa4ac; }"));
		rows_->insertWidget(0, empty);
	}

	summary_->setText(QStringLiteral("%1 output%2 can use separate scenes")
				  .arg(routable)
				  .arg(routable == 1 ? QString() : QStringLiteral("s")));
	refreshing_ = false;
}

} // namespace dsk
