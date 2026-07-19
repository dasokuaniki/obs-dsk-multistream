#include "ui/target-edit-dialog.hpp"

#include "core/diagnostics.hpp"
#include "core/experimental-features.hpp"
#include "core/oauth-provider.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QVariant>

namespace dsk {

namespace {

constexpr int WindowsCredentialValueLimitBytes = 2560;

QString comboDataOr(const QComboBox *combo, const QString &fallback)
{
	if (!combo)
		return fallback;
	const QVariant data = combo->currentData();
	if (!data.isValid())
		return fallback;
	const QString value = data.toString();
	return value.isEmpty() ? fallback : value;
}

QString lineTextOrEmpty(const QLineEdit *line)
{
	return line ? line->text() : QString();
}

QString detachedString(QString value)
{
	value.detach();
	return value;
}

struct OAuthClientFields {
	QString clientId;
	QString clientSecret;
	bool parsedJson = false;
	bool droppedOversizedSecret = false;
};

QJsonObject oauthClientObjectFromJson(const QString &text)
{
	const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
	if (!document.isObject())
		return {};

	const QJsonObject root = document.object();
	const QJsonObject installed = root.value(QStringLiteral("installed")).toObject();
	if (!installed.isEmpty())
		return installed;
	const QJsonObject web = root.value(QStringLiteral("web")).toObject();
	if (!web.isEmpty())
		return web;
	return root;
}

OAuthClientFields normalizeOAuthClientFields(QString clientId, QString clientSecret)
{
	clientId = clientId.trimmed();
	clientSecret = clientSecret.trimmed();

	for (const QString &candidate : {clientId, clientSecret}) {
		if (!candidate.trimmed().startsWith(QLatin1Char('{')))
			continue;
		const QJsonObject object = oauthClientObjectFromJson(candidate);
		const QString parsedId = object.value(QStringLiteral("client_id")).toString().trimmed();
		const QString parsedSecret = object.value(QStringLiteral("client_secret")).toString().trimmed();
		if (parsedId.isEmpty() && parsedSecret.isEmpty())
			continue;
		if (!parsedId.isEmpty())
			clientId = parsedId;
		if (!parsedSecret.isEmpty())
			clientSecret = parsedSecret;
		return {detachedString(clientId), detachedString(clientSecret), true, false};
	}

	const bool oversized =
		!clientSecret.isEmpty() && clientSecret.toUtf8().size() > WindowsCredentialValueLimitBytes;
	if (oversized)
		clientSecret.clear();
	return {detachedString(clientId), detachedString(clientSecret), false, oversized};
}

bool checkedOr(const QCheckBox *box, bool fallback)
{
	return box ? box->isChecked() : fallback;
}

int spinValueOr(const QSpinBox *spin, int fallback)
{
	return spin ? spin->value() : fallback;
}

int boundedDialogHeight(QWidget *parent)
{
	QScreen *screen = parent && parent->screen() ? parent->screen() : QGuiApplication::primaryScreen();
	const int availableHeight = screen ? screen->availableGeometry().height() : 720;
	return qBound(420, availableHeight - 120, 760);
}

bool isPublisherManagedAuth(TargetAuthMode mode)
{
	return mode == TargetAuthMode::TwitchOAuth || mode == TargetAuthMode::KickOAuth;
}

QString outputDisplayName(const QString &outputId)
{
	return outputId == QStringLiteral("dsk-vertical") ? QStringLiteral("DSK Vertical")
							 : QStringLiteral("DSK Horizontal");
}

QString validationMessageForDisplay(const QString &message)
{
	if (message == QStringLiteral("Login mode does not match the selected platform."))
		return QStringLiteral("選択したプラットフォームと接続方式が一致していません。");
	if (message == QStringLiteral("Server URL is empty."))
		return QStringLiteral("サーバーURLを入力してください。");
	if (message == QStringLiteral("Server URL must start with rtmp:// or rtmps://."))
		return QStringLiteral("サーバーURLは rtmp:// または rtmps:// で始めてください。");
	if (message == QStringLiteral("Server URL must include a host name."))
		return QStringLiteral("サーバーURLにホスト名がありません。");
	if (message == QStringLiteral("TikTok needs the server URL shown in TikTok LIVE setup."))
		return QStringLiteral("以前のTikTok汎用URLは使用できません。TikTok LIVEに表示されたサーバーURLを入力してください。");
	if (message == QStringLiteral("Stream key is empty."))
		return QStringLiteral("ストリームキーを入力してください。");
	if (message == QStringLiteral("Reconnect settings are invalid."))
		return QStringLiteral("再接続設定の値を確認してください。");
	if (message == QStringLiteral("Encoder settings are outside the supported range."))
		return QStringLiteral("エンコーダー設定が対応範囲外です。");
	if (message == QStringLiteral("Fixed scene mode needs an OBS scene."))
		return QStringLiteral("固定シーンを使用する場合はOBSシーンを指定してください。");
	return message;
}

} // namespace

TargetEditDialog::TargetEditDialog(const PlatformPresetRegistry &platforms, QWidget *parent)
	: QDialog(parent),
	  platforms_(platforms)
{
	setWindowTitle("Stream Target");
	setMinimumWidth(540);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(10, 10, 10, 10);
	layout->setSpacing(8);

	auto *content = new QWidget(this);
	auto *contentLayout = new QVBoxLayout(content);
	contentLayout->setContentsMargins(0, 0, 0, 0);
	contentLayout->setSpacing(8);

	auto *intro = new QLabel("Configure one DSK-managed destination. Start and stop outputs from the Controls tab in DSK Streaming.", this);
	intro->setWordWrap(true);
	contentLayout->addWidget(intro);

	auto *form = new QFormLayout();

	name_ = new QLineEdit(this);
	name_->setPlaceholderText("Example: YouTube vertical");
	platform_ = new QComboBox(this);
	platform_->setObjectName(QStringLiteral("dskTargetPlatform"));
	for (const auto &preset : platforms_.presets())
		platform_->addItem(preset.displayName, preset.id);
	authMode_ = new QComboBox(this);
	authStatus_ = new QLabel(this);
	authStatus_->setWordWrap(true);
	useCustomOAuthApp_ = new QCheckBox("Use custom Google OAuth app", this);
	useCustomOAuthApp_->setObjectName(QStringLiteral("dskUseCustomOAuthApp"));
	useCustomOAuthApp_->setToolTip("Use your own Google OAuth Client ID and Client Secret instead of the DSK publisher app.");
	oauthClientId_ = new QLineEdit(this);
	oauthClientId_->setObjectName(QStringLiteral("dskOAuthClientId"));
	oauthClientId_->setPlaceholderText("OAuth app Client ID");
	oauthClientSecret_ = new QLineEdit(this);
	oauthClientSecret_->setObjectName(QStringLiteral("dskOAuthClientSecret"));
	oauthClientSecret_->setEchoMode(QLineEdit::Password);
	oauthClientSecret_->setPlaceholderText("Optional client secret");
	connectOAuthButton_ = new QPushButton("Connect", this);
	connectOAuthButton_->setObjectName(QStringLiteral("dskConnectOAuth"));
	connect(connectOAuthButton_, &QPushButton::clicked, this, &TargetEditDialog::connectOAuthAccount);
	disconnectOAuthButton_ = new QPushButton("Disconnect", this);
	disconnectOAuthButton_->setObjectName(QStringLiteral("dskDisconnectOAuth"));
	disconnectOAuthButton_->setVisible(false);
	connect(disconnectOAuthButton_, &QPushButton::clicked, this, &TargetEditDialog::disconnectOAuthAccount);
	oauthConnector_ = new OAuthConnector(this);
	connect(oauthConnector_, &OAuthConnector::finished, this, &TargetEditDialog::handleOAuthFinished);
	platformHint_ = new QLabel(this);
	platformHint_->setObjectName(QStringLiteral("dskPlatformHint"));
	platformHint_->setWordWrap(true);

	server_ = new QLineEdit(this);
	server_->setObjectName(QStringLiteral("dskTargetServer"));
	server_->setPlaceholderText("rtmp:// or rtmps:// server URL");
	presetServerButton_ = new QPushButton("Use Preset", this);
	presetServerButton_->setObjectName(QStringLiteral("dskUsePresetServer"));
	connect(presetServerButton_, &QPushButton::clicked, this, &TargetEditDialog::usePresetServer);
	auto *serverRow = new QHBoxLayout();
	serverRow->setContentsMargins(0, 0, 0, 0);
	serverRow->addWidget(server_, 1);
	serverRow->addWidget(presetServerButton_);

	streamKey_ = new QLineEdit(this);
	streamKey_->setObjectName(QStringLiteral("dskTargetStreamKey"));
	streamKey_->setEchoMode(QLineEdit::Password);
	streamKey_->setPlaceholderText("Enter stream key");
	showStreamKey_ = new QCheckBox("Show key", this);
	connect(showStreamKey_, &QCheckBox::toggled, this, &TargetEditDialog::updateStreamKeyVisibility);
	auto *keyRow = new QHBoxLayout();
	keyRow->setContentsMargins(0, 0, 0, 0);
	keyRow->addWidget(streamKey_, 1);
	keyRow->addWidget(showStreamKey_);

	encoderGroup_ = new QComboBox(this);
	encoderGroup_->addItem("DSK Horizontal - 16:9 independent encoder", encoderGroupToString(EncoderGroup::DskHorizontal));
	encoderGroup_->addItem("DSK Vertical - 9:16 independent encoder", encoderGroupToString(EncoderGroup::DskVertical));

	videoEncoder_ = new QComboBox(this);
	videoEncoder_->addItem("Auto - OBS hardware (H.264)", QString());
	videoEncoder_->addItem("NVIDIA NVENC H.264", QStringLiteral("obs_nvenc_h264_tex"));
	videoEncoder_->addItem("NVIDIA NVENC HEVC", QStringLiteral("obs_nvenc_hevc_tex"));
	videoEncoder_->addItem("NVIDIA NVENC H.264 (fallback)", QStringLiteral("ffmpeg_nvenc"));
	videoEncoder_->addItem("AMD AMF H.264", QStringLiteral("h264_texture_amf"));
	videoEncoder_->addItem("Intel QSV H.264", QStringLiteral("obs_qsv11_v2"));
	videoEncoder_->addItem("x264 (CPU)", QStringLiteral("obs_x264"));
	videoEncoder_->setToolTip(
		"Auto uses H.264 on the same hardware family as OBS when possible. DSK still creates its own encoder instance.");

	sceneMode_ = new QComboBox(this);
	sceneMode_->addItem(targetSceneModeDisplayName(TargetSceneMode::FollowObs),
			   targetSceneModeToString(TargetSceneMode::FollowObs));
	sceneMode_->addItem(targetSceneModeDisplayName(TargetSceneMode::FixedScene),
			   targetSceneModeToString(TargetSceneMode::FixedScene));
	sceneMode_->addItem(targetSceneModeDisplayName(TargetSceneMode::LinkedScene),
			   targetSceneModeToString(TargetSceneMode::LinkedScene));
	sceneMode_->setToolTip("Choose whether this DSK output follows OBS Program or uses a separate OBS scene.");
	sceneName_ = new QLineEdit(this);
	sceneName_->setPlaceholderText("OBS scene name for Fixed mode, or fallback scene for Linked mode");

	useSharedEncoder_ = new QCheckBox("Share encoder with matching DSK targets", this);
	useSharedEncoder_->setToolTip("Targets with the same DSK output mode and bitrate can reuse one encoder.");
	autoStartWithObs_ = new QCheckBox("Start this target when OBS starts streaming", this);
	autoStopWithObs_ = new QCheckBox("Stop this target when OBS stops streaming", this);
	reconnectEnabled_ = new QCheckBox("Auto reconnect", this);

	reconnectMaxRetries_ = new QSpinBox(this);
	reconnectMaxRetries_->setRange(0, 100);
	reconnectMaxRetries_->setSuffix(" retries");
	reconnectDelaySeconds_ = new QSpinBox(this);
	reconnectDelaySeconds_->setRange(1, 60);
	reconnectDelaySeconds_->setSuffix(" sec");
	videoBitrateKbps_ = new QSpinBox(this);
	videoBitrateKbps_->setRange(0, 100000);
	videoBitrateKbps_->setSingleStep(500);
	videoBitrateKbps_->setSuffix(" kbps");
	videoBitrateKbps_->setSpecialValueText("Default");
	audioBitrateKbps_ = new QSpinBox(this);
	audioBitrateKbps_->setRange(0, 1024);
	audioBitrateKbps_->setSingleStep(32);
	audioBitrateKbps_->setSuffix(" kbps");
	audioBitrateKbps_->setSpecialValueText("Default");
	keyframeSeconds_ = new QSpinBox(this);
	keyframeSeconds_->setRange(1, 10);
	keyframeSeconds_->setSuffix(" sec");

	enabled_ = new QCheckBox("Allow automatic and bulk starts", this);
	enabled_->setToolTip("Controls Start All and OBS-linked automatic starts. Individual Start remains available.");
	startWithAll_ = new QCheckBox("Include in Start All", this);
	startWithAll_->setToolTip("When enabled, the DSK Streaming Controls tab includes this target in Start All.");
	enabled_->setChecked(true);
	startWithAll_->setChecked(true);
	useSharedEncoder_->setChecked(true);
	autoStartWithObs_->setChecked(false);
	autoStopWithObs_->setChecked(true);
	reconnectEnabled_->setChecked(true);
	reconnectMaxRetries_->setValue(20);
	reconnectDelaySeconds_->setValue(2);
	keyframeSeconds_->setValue(2);

	connect(platform_, &QComboBox::currentIndexChanged, this, &TargetEditDialog::applyPlatformDefaults);
	connect(platform_, &QComboBox::currentIndexChanged, this, &TargetEditDialog::updateAuthModes);
	connect(platform_, &QComboBox::currentIndexChanged, this, &TargetEditDialog::updatePlatformHint);
	connect(authMode_, &QComboBox::currentIndexChanged, this, &TargetEditDialog::updatePlatformHint);
	connect(useCustomOAuthApp_, &QCheckBox::toggled, this, &TargetEditDialog::updatePlatformHint);
	connect(oauthClientId_, &QLineEdit::textChanged, this, &TargetEditDialog::updatePlatformHint);
	connect(name_, &QLineEdit::textChanged, this, [this](const QString &text) {
		nameValue_ = text.trimmed();
	});
	connect(platform_, &QComboBox::currentIndexChanged, this, [this]() {
		platformIdValue_ = comboDataOr(platform_, QStringLiteral("custom"));
	});
	connect(authMode_, &QComboBox::currentIndexChanged, this, [this]() {
		authModeValue_ = comboDataOr(authMode_, targetAuthModeToString(TargetAuthMode::ManualRtmp));
	});
	connect(oauthClientId_, &QLineEdit::textChanged, this, [this](const QString &text) {
		oauthClientIdValue_ = text.trimmed();
	});
	connect(oauthClientSecret_, &QLineEdit::textChanged, this, [this](const QString &text) {
		oauthClientSecretValue_ = text;
	});
	connect(server_, &QLineEdit::textChanged, this, [this](const QString &text) {
		serverUrlValue_ = text.trimmed();
	});
	connect(server_, &QLineEdit::textEdited, this, [this]() {
		serverFollowsPlatform_ = false;
	});
	connect(streamKey_, &QLineEdit::textChanged, this, [this](const QString &text) {
		streamKeyValue_ = text;
	});
	connect(encoderGroup_, &QComboBox::currentIndexChanged, this, [this]() {
		encoderGroupValue_ = comboDataOr(encoderGroup_, encoderGroupToString(EncoderGroup::DskHorizontal));
	});
	connect(videoEncoder_, &QComboBox::currentIndexChanged, this, [this]() {
		videoEncoderId_ = comboDataOr(videoEncoder_, QString());
	});
	connect(sceneMode_, &QComboBox::currentIndexChanged, this, [this]() {
		sceneModeValue_ = comboDataOr(sceneMode_, targetSceneModeToString(TargetSceneMode::FollowObs));
	});
	connect(sceneName_, &QLineEdit::textChanged, this, [this](const QString &text) {
		sceneNameValue_ = text.trimmed();
	});
	connect(useSharedEncoder_, &QCheckBox::toggled, this, [this](bool checked) { useSharedEncoderValue_ = checked; });
	connect(autoStartWithObs_, &QCheckBox::toggled, this, [this](bool checked) { autoStartWithObsValue_ = checked; });
	connect(autoStopWithObs_, &QCheckBox::toggled, this, [this](bool checked) { autoStopWithObsValue_ = checked; });
	connect(reconnectEnabled_, &QCheckBox::toggled, this, [this](bool checked) { reconnectEnabledValue_ = checked; });
	connect(reconnectMaxRetries_, &QSpinBox::valueChanged, this, [this](int value) { reconnectMaxRetriesValue_ = value; });
	connect(reconnectDelaySeconds_, &QSpinBox::valueChanged, this, [this](int value) { reconnectDelaySecondsValue_ = value; });
	connect(videoBitrateKbps_, &QSpinBox::valueChanged, this, [this](int value) { videoBitrateKbpsValue_ = value; });
	connect(audioBitrateKbps_, &QSpinBox::valueChanged, this, [this](int value) { audioBitrateKbpsValue_ = value; });
	connect(keyframeSeconds_, &QSpinBox::valueChanged, this, [this](int value) { keyframeSecondsValue_ = value; });
	connect(enabled_, &QCheckBox::toggled, this, [this](bool checked) { enabledValue_ = checked; });
	connect(startWithAll_, &QCheckBox::toggled, this, [this](bool checked) { startWithAllValue_ = checked; });

	auto *connectionRow = new QHBoxLayout();
	connectionRow->setContentsMargins(0, 0, 0, 0);
	connectionRow->addWidget(authMode_, 1);
	connectionRow->addWidget(connectOAuthButton_);
	connectionRow->addWidget(disconnectOAuthButton_);

	form->addRow("Name", name_);
	form->addRow("Platform", platform_);
	form->addRow("Connection", connectionRow);
	form->addRow("", authStatus_);
	form->addRow("", useCustomOAuthApp_);
	form->addRow("OAuth Client ID", oauthClientId_);
	oauthClientIdLabel_ = form->labelForField(oauthClientId_);
	form->addRow("OAuth Client Secret", oauthClientSecret_);
	oauthClientSecretLabel_ = form->labelForField(oauthClientSecret_);
	form->addRow("", platformHint_);
	form->addRow("Server", serverRow);
	form->addRow("Stream key", keyRow);
	form->addRow("Output mode", encoderGroup_);
	form->addRow("Video encoder", videoEncoder_);
	form->addRow("Scene routing", sceneMode_);
	form->addRow("Scene", sceneName_);
	const bool showExperimentalSceneRouting = experimentalSceneRoutingEnabled();
	form->setRowVisible(sceneMode_, showExperimentalSceneRouting);
	form->setRowVisible(sceneName_, showExperimentalSceneRouting);
	form->addRow("", useSharedEncoder_);
	form->addRow("", autoStartWithObs_);
	form->addRow("", autoStopWithObs_);
	form->addRow("", reconnectEnabled_);
	form->addRow("Reconnect retries", reconnectMaxRetries_);
	form->addRow("Reconnect delay", reconnectDelaySeconds_);
	form->addRow("Video bitrate", videoBitrateKbps_);
	form->addRow("Audio bitrate", audioBitrateKbps_);
	form->addRow("Keyframe interval", keyframeSeconds_);
	form->addRow("", enabled_);
	form->addRow("", startWithAll_);

	contentLayout->addLayout(form);

	auto *scrollArea = new QScrollArea(this);
	scrollArea->setWidgetResizable(true);
	scrollArea->setFrameShape(QFrame::NoFrame);
	scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	scrollArea->setWidget(content);
	const int dialogHeight = boundedDialogHeight(parent);
	scrollArea->setMaximumHeight(qMax(280, dialogHeight - 96));
	layout->addWidget(scrollArea, 1);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	if (auto *saveButton = buttons->button(QDialogButtonBox::Ok))
		saveButton->setText("Save");
	connect(buttons, &QDialogButtonBox::accepted, this, &TargetEditDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	auto *line = new QFrame(this);
	line->setFrameShape(QFrame::HLine);
	layout->addWidget(line);
	layout->addWidget(buttons);
	resize(qMax(620, minimumWidth()), dialogHeight);
}

void TargetEditDialog::setTarget(const OutputTarget &target)
{
	loading_ = true;
	targetId_ = target.id;
	authAccountName_ = target.authAccountName;
	authCredentialRef_ = target.authCredentialRef;
	oauthClientSecretRef_ = target.oauthClientSecretRef;
	oauthRefreshToken_ = target.oauthRefreshToken;
	oauthRefreshTokenRef_ = target.oauthRefreshTokenRef;
	oauthClientIdValue_ = target.oauthClientId.trimmed();
	loadedOAuthClientId_ = target.oauthClientId.trimmed();
	oauthClientSecretValue_ = target.oauthClientSecret;
	useCustomOAuthApp_->setChecked(!target.oauthClientId.trimmed().isEmpty() ||
				      !target.oauthClientSecret.trimmed().isEmpty() ||
				      !target.oauthClientSecretRef.trimmed().isEmpty());
	oauthClientId_->setText(target.oauthClientId);
	oauthClientSecret_->setText(target.oauthClientSecret);
	videoEncoderId_ = target.videoEncoderId;
	const int videoEncoderIndex = videoEncoder_->findData(videoEncoderId_);
	if (videoEncoderIndex < 0 && !videoEncoderId_.trimmed().isEmpty())
		videoEncoder_->addItem(QString("Custom: %1").arg(videoEncoderId_), videoEncoderId_);
	videoEncoder_->setCurrentIndex(videoEncoder_->findData(videoEncoderId_));
	audioEncoderId_ = target.audioEncoderId;
	sceneModeValue_ = targetSceneModeToString(target.sceneMode);
	const int sceneModeIndex = sceneMode_->findData(sceneModeValue_);
	sceneMode_->setCurrentIndex(sceneModeIndex >= 0 ? sceneModeIndex : 0);
	sceneModeValue_ = comboDataOr(sceneMode_, targetSceneModeToString(TargetSceneMode::FollowObs));
	sceneNameValue_ = target.sceneName.trimmed();
	sceneUuidValue_ = target.sceneUuid.trimmed();
	sceneName_->setText(target.sceneName);
	sceneRoutesValue_ = target.sceneRoutes;
	targetState_ = target.state;
	loadedAuthMode_ = target.authMode;
	connectedAuthMode_ = TargetAuthMode::ManualRtmp;
	oauthConnectedThisEdit_ = false;
	lastError_ = target.lastError;
	nameValue_ = target.name.trimmed();
	name_->setText(target.name);
	serverUrlValue_ = target.serverUrl.trimmed();
	server_->setText(target.serverUrl);
	const PlatformPreset loadedPreset = platforms_.presetById(target.platformId);
	serverFollowsPlatform_ = serverUrlValue_.isEmpty() || serverUrlValue_ == loadedPreset.defaultServer;
	streamKeyValue_ = target.streamKey;
	streamKey_->setText(target.streamKey);
	streamKey_->setPlaceholderText(target.streamKey.trimmed().isEmpty() && !target.authCredentialRef.trimmed().isEmpty()
					       ? QStringLiteral("Saved stream key exists - leave blank to keep")
					       : QStringLiteral("Enter stream key"));
	useSharedEncoder_->setChecked(target.useSharedEncoder);
	useSharedEncoderValue_ = target.useSharedEncoder;
	autoStartWithObs_->setChecked(target.autoStartWithObs);
	autoStartWithObsValue_ = target.autoStartWithObs;
	autoStopWithObs_->setChecked(target.autoStopWithObs);
	autoStopWithObsValue_ = target.autoStopWithObs;
	reconnectEnabled_->setChecked(target.reconnectEnabled);
	reconnectEnabledValue_ = target.reconnectEnabled;
	reconnectMaxRetries_->setValue(target.reconnectMaxRetries);
	reconnectMaxRetriesValue_ = target.reconnectMaxRetries;
	reconnectDelaySeconds_->setValue(target.reconnectDelaySeconds);
	reconnectDelaySecondsValue_ = target.reconnectDelaySeconds;
	videoBitrateKbps_->setValue(target.videoBitrateKbps);
	videoBitrateKbpsValue_ = target.videoBitrateKbps;
	audioBitrateKbps_->setValue(target.audioBitrateKbps);
	audioBitrateKbpsValue_ = target.audioBitrateKbps;
	keyframeSeconds_->setValue(target.keyframeSeconds);
	keyframeSecondsValue_ = target.keyframeSeconds;
	enabled_->setChecked(target.enabled);
	enabledValue_ = target.enabled;
	startWithAll_->setChecked(target.startWithAll);
	startWithAllValue_ = target.startWithAll;

	const int platformIndex = platform_->findData(target.platformId);
	platform_->setCurrentIndex(platformIndex >= 0 ? platformIndex : platform_->findData("custom"));
	platformIdValue_ = comboDataOr(platform_, QStringLiteral("custom"));
	updateAuthModes();
	const int authIndex = authMode_->findData(targetAuthModeToString(target.authMode));
	authMode_->setCurrentIndex(authIndex >= 0 ? authIndex : 0);
	authModeValue_ = comboDataOr(authMode_, targetAuthModeToString(TargetAuthMode::ManualRtmp));

	const int encoderIndex = encoderGroup_->findData(encoderGroupToString(target.encoderGroup));
	encoderGroup_->setCurrentIndex(encoderIndex >= 0 ? encoderIndex : 0);
	encoderGroupValue_ = comboDataOr(encoderGroup_, encoderGroupToString(EncoderGroup::DskHorizontal));
	updatePlatformHint();
	loading_ = false;
}

void TargetEditDialog::setNewTargetDefaults(const QString &targetId)
{
	loading_ = true;
	targetId_ = targetId;
	authAccountName_.clear();
	authCredentialRef_.clear();
	oauthClientSecretRef_.clear();
	oauthRefreshToken_.clear();
	oauthRefreshTokenRef_.clear();
	oauthClientIdValue_.clear();
	loadedOAuthClientId_.clear();
	oauthClientSecretValue_.clear();
	useCustomOAuthApp_->setChecked(false);
	oauthClientId_->clear();
	oauthClientSecret_->clear();
	videoEncoderId_.clear();
	videoEncoder_->setCurrentIndex(videoEncoder_->findData(QString()));
	audioEncoderId_.clear();
	sceneModeValue_ = targetSceneModeToString(TargetSceneMode::FollowObs);
	sceneMode_->setCurrentIndex(sceneMode_->findData(sceneModeValue_));
	sceneNameValue_.clear();
	sceneUuidValue_.clear();
	sceneName_->clear();
	sceneRoutesValue_.clear();
	lastError_.clear();
	targetState_ = TargetState::Stopped;
	loadedAuthMode_ = TargetAuthMode::ManualRtmp;
	connectedAuthMode_ = TargetAuthMode::ManualRtmp;
	oauthConnectedThisEdit_ = false;
	nameValue_ = QStringLiteral("New Target");
	name_->setText("New Target");
	serverUrlValue_ = QStringLiteral("rtmp://live.twitch.tv/app");
	server_->setText("rtmp://live.twitch.tv/app");
	serverFollowsPlatform_ = true;
	streamKeyValue_.clear();
	streamKey_->clear();
	streamKey_->setPlaceholderText("Enter stream key");
	useSharedEncoder_->setChecked(true);
	useSharedEncoderValue_ = true;
	autoStartWithObs_->setChecked(false);
	autoStartWithObsValue_ = false;
	autoStopWithObs_->setChecked(true);
	autoStopWithObsValue_ = true;
	reconnectEnabled_->setChecked(true);
	reconnectEnabledValue_ = true;
	reconnectMaxRetries_->setValue(20);
	reconnectMaxRetriesValue_ = 20;
	reconnectDelaySeconds_->setValue(2);
	reconnectDelaySecondsValue_ = 2;
	videoBitrateKbps_->setValue(0);
	videoBitrateKbpsValue_ = 0;
	audioBitrateKbps_->setValue(0);
	audioBitrateKbpsValue_ = 0;
	keyframeSeconds_->setValue(2);
	keyframeSecondsValue_ = 2;
	enabled_->setChecked(true);
	enabledValue_ = true;
	startWithAll_->setChecked(true);
	startWithAllValue_ = true;

	const int platformIndex = platform_->findData("twitch");
	platform_->setCurrentIndex(platformIndex >= 0 ? platformIndex : 0);
	platformIdValue_ = comboDataOr(platform_, QStringLiteral("custom"));
	updateAuthModes();
	const int authIndex = authMode_->findData(targetAuthModeToString(TargetAuthMode::ManualRtmp));
	authMode_->setCurrentIndex(authIndex >= 0 ? authIndex : 0);
	authModeValue_ = comboDataOr(authMode_, targetAuthModeToString(TargetAuthMode::ManualRtmp));
	const int encoderIndex = encoderGroup_->findData(encoderGroupToString(EncoderGroup::DskHorizontal));
	encoderGroup_->setCurrentIndex(encoderIndex >= 0 ? encoderIndex : 0);
	encoderGroupValue_ = comboDataOr(encoderGroup_, encoderGroupToString(EncoderGroup::DskHorizontal));
	updatePlatformHint();
	loading_ = false;
}

void TargetEditDialog::fillTarget(OutputTarget &copy) const
{
	const QString nameText = detachedString(lineTextOrEmpty(name_).trimmed());
	const QString authModeText = detachedString(comboDataOr(authMode_, targetAuthModeToString(TargetAuthMode::ManualRtmp)));
	const TargetAuthMode selectedAuthMode = targetAuthModeFromString(
		authModeText.isEmpty() ? targetAuthModeToString(TargetAuthMode::ManualRtmp) : authModeText);
	const bool customYouTubeOAuth = selectedAuthMode == TargetAuthMode::YouTubeOAuth &&
		(!oauthHasBundledClientCredentials(TargetAuthMode::YouTubeOAuth) ||
		 checkedOr(useCustomOAuthApp_, false));
	OAuthClientFields oauthFields;
	if (customYouTubeOAuth) {
		const QString rawOAuthClientIdText = detachedString(lineTextOrEmpty(oauthClientId_).trimmed());
		const QString rawOAuthClientSecretText = detachedString(lineTextOrEmpty(oauthClientSecret_));
		oauthFields = normalizeOAuthClientFields(rawOAuthClientIdText, rawOAuthClientSecretText);
	}
	if (oauthFields.parsedJson)
		logInfo("OAuth client JSON detected; extracted Client ID and Client Secret.");
	if (oauthFields.droppedOversizedSecret)
		logWarning("OAuth Client Secret was too large for Windows Credential Manager; not saving it.");
	const QString serverText = detachedString(lineTextOrEmpty(server_).trimmed());
	const QString streamKeyText = detachedString(lineTextOrEmpty(streamKey_));
	const bool keepSavedStreamKey = streamKeyText.trimmed().isEmpty() && !authCredentialRef_.trimmed().isEmpty();
	const QString platformIdText = detachedString(comboDataOr(platform_, QStringLiteral("custom")));
	const QString encoderGroupText = detachedString(comboDataOr(encoderGroup_, encoderGroupToString(EncoderGroup::DskHorizontal)));
	const QString sceneModeText = detachedString(comboDataOr(sceneMode_, targetSceneModeToString(TargetSceneMode::FollowObs)));
	const QString sceneNameText = detachedString(lineTextOrEmpty(sceneName_).trimmed());

	copy.serverUrl = serverText;
	copy.streamKey = keepSavedStreamKey ? QString() : streamKeyText;
	copy.id = detachedString(targetId_);
	copy.name = nameText;
	copy.platformId = platformIdText.isEmpty() ? QStringLiteral("custom") : platformIdText;
	copy.authMode = selectedAuthMode;
	if (!platformSupportsAuthMode(copy.platformId, copy.authMode))
		copy.authMode = TargetAuthMode::ManualRtmp;
	copy.authAccountName = detachedString(authAccountName_);
	copy.authCredentialRef = detachedString(authCredentialRef_);
	copy.oauthClientId = oauthFields.clientId;
	copy.oauthClientSecret = oauthFields.clientSecret;
	copy.oauthClientSecretRef = customYouTubeOAuth ? detachedString(oauthClientSecretRef_) : QString();
	copy.oauthRefreshToken = detachedString(oauthRefreshToken_);
	copy.oauthRefreshTokenRef = detachedString(oauthRefreshTokenRef_);
	const bool publisherManagedAuth = isPublisherManagedAuth(copy.authMode);
	const bool authMatchesLoadedAccount = copy.authMode == loadedAuthMode_ &&
					      (publisherManagedAuth ||
					       copy.oauthClientId.trimmed() == loadedOAuthClientId_.trimmed());
	const bool authConnectedNow = oauthConnectedThisEdit_ && copy.authMode == connectedAuthMode_;
	if (!authMatchesLoadedAccount && !authConnectedNow)
		copy.authAccountName.clear();
	if (copy.authMode == TargetAuthMode::ManualRtmp) {
		copy.authAccountName.clear();
		copy.oauthClientId.clear();
		copy.oauthClientSecret.clear();
		copy.oauthClientSecretRef.clear();
		copy.oauthRefreshToken.clear();
		copy.oauthRefreshTokenRef.clear();
	} else if (isPublisherManagedAuth(copy.authMode)) {
		migratePublisherManagedOAuthCredentials(copy);
	} else if (!authMatchesLoadedAccount && !authConnectedNow) {
		copy.oauthRefreshToken.clear();
		copy.oauthRefreshTokenRef.clear();
	}
	copy.encoderGroup = encoderGroupFromString(encoderGroupText.isEmpty() ? encoderGroupToString(EncoderGroup::DskHorizontal) : encoderGroupText);
	copy.sceneMode = targetSceneModeFromString(sceneModeText);
	copy.sceneName = sceneNameText;
	copy.sceneUuid = detachedString(sceneUuidValue_);
	copy.sceneRoutes = sceneRoutesValue_;
	copy.useSharedEncoder = useSharedEncoderValue_;
	copy.autoStartWithObs = autoStartWithObsValue_;
	copy.autoStopWithObs = autoStopWithObsValue_;
	copy.reconnectEnabled = reconnectEnabledValue_;
	copy.reconnectMaxRetries = reconnectMaxRetriesValue_;
	copy.reconnectDelaySeconds = reconnectDelaySecondsValue_;
	copy.videoBitrateKbps = videoBitrateKbpsValue_;
	copy.audioBitrateKbps = audioBitrateKbpsValue_;
	copy.keyframeSeconds = keyframeSecondsValue_;
	const QString videoEncoderText = detachedString(comboDataOr(videoEncoder_, QString()));
	copy.videoEncoderId = videoEncoderText;
	copy.audioEncoderId = detachedString(audioEncoderId_);
	copy.enabled = enabledValue_;
	copy.startWithAll = startWithAllValue_;
	copy.state = targetState_;
	copy.lastError = detachedString(lastError_);
	if (copy.name.isEmpty())
		copy.name = platforms_.presetById(copy.platformId).displayName;
	if (copy.name.isEmpty())
		copy.name = QStringLiteral("Stream Target");
}

OutputTarget TargetEditDialog::acceptedTarget() const
{
	OutputTarget copy;
	fillTarget(copy);
	return copy;
}

bool TargetEditDialog::validateForSave(QString *errorMessage) const
{
	OutputTarget candidate;
	fillTarget(candidate);
	return validateOutputTargetConfig(candidate, errorMessage, false);
}

void TargetEditDialog::accept()
{
	logInfo("Target edit accept begin");
	QString errorMessage;
	if (!validateForSave(&errorMessage)) {
		logWarning(QString("Target edit validation failed: %1").arg(errorMessage));
		if ((errorMessage.startsWith(QStringLiteral("Server URL")) || errorMessage.startsWith(QStringLiteral("TikTok"))) && server_)
			server_->setFocus();
		else if (errorMessage == QStringLiteral("Stream key is empty.") && streamKey_)
			streamKey_->setFocus();
		else if (errorMessage == QStringLiteral("Fixed scene mode needs an OBS scene.") && sceneName_)
			sceneName_->setFocus();
		QMessageBox::warning(this, QStringLiteral("配信先の設定を確認"), validationMessageForDisplay(errorMessage));
		return;
	}
	QDialog::accept();
	logInfo("Target edit accept end");
}

void TargetEditDialog::applyPlatformDefaults(int index)
{
	if (loading_ || index < 0 || !server_ || !encoderGroup_ ||
	    (!serverFollowsPlatform_ && !serverUrlValue_.trimmed().isEmpty()))
		return;

	const PlatformPreset preset = platforms_.presetById(platform_->itemData(index).toString());
	serverFollowsPlatform_ = true;
	server_->setText(preset.defaultServer);
	if (preset.verticalCommon) {
		const int verticalIndex = encoderGroup_->findData(encoderGroupToString(EncoderGroup::DskVertical));
		if (verticalIndex >= 0)
			encoderGroup_->setCurrentIndex(verticalIndex);
	}
}

void TargetEditDialog::usePresetServer()
{
	if (!server_ || !name_ || !encoderGroup_)
		return;

	const PlatformPreset preset = platforms_.presetById(platform_->currentData().toString());
	serverFollowsPlatform_ = true;
	server_->setText(preset.defaultServer);
	if (name_->text().trimmed().isEmpty())
		name_->setText(preset.displayName);
	if (preset.verticalCommon) {
		const int verticalIndex = encoderGroup_->findData(encoderGroupToString(EncoderGroup::DskVertical));
		if (verticalIndex >= 0)
			encoderGroup_->setCurrentIndex(verticalIndex);
	}
}

void TargetEditDialog::updateAuthModes()
{
	if (!authMode_)
		return;

	const QString platformId = platform_->currentData().toString();
	const QString previous = authMode_->currentData().toString();
	authMode_->blockSignals(true);
	authMode_->clear();
	authMode_->addItem(targetAuthModeDisplayName(TargetAuthMode::ManualRtmp), targetAuthModeToString(TargetAuthMode::ManualRtmp));
	if (platformSupportsAuthMode(platformId, TargetAuthMode::TwitchOAuth))
		authMode_->addItem(targetAuthModeDisplayName(TargetAuthMode::TwitchOAuth), targetAuthModeToString(TargetAuthMode::TwitchOAuth));
	if (platformSupportsAuthMode(platformId, TargetAuthMode::YouTubeOAuth))
		authMode_->addItem(targetAuthModeDisplayName(TargetAuthMode::YouTubeOAuth), targetAuthModeToString(TargetAuthMode::YouTubeOAuth));
	if (platformSupportsAuthMode(platformId, TargetAuthMode::KickOAuth))
		authMode_->addItem(targetAuthModeDisplayName(TargetAuthMode::KickOAuth), targetAuthModeToString(TargetAuthMode::KickOAuth));
	const int previousIndex = authMode_->findData(previous);
	authMode_->setCurrentIndex(previousIndex >= 0 ? previousIndex : 0);
	authMode_->blockSignals(false);
	authModeValue_ = comboDataOr(authMode_, targetAuthModeToString(TargetAuthMode::ManualRtmp));
}

void TargetEditDialog::updatePlatformHint()
{
	if (!platformHint_)
		return;

	const PlatformPreset preset = platforms_.presetById(platform_->currentData().toString());
	const TargetAuthMode authMode = targetAuthModeFromString(authMode_ ? authMode_->currentData().toString() : QString());
	const bool manualServer = preset.defaultServer.trimmed().isEmpty();
	QString text;
	if (preset.id == QStringLiteral("tiktok"))
		text = QStringLiteral("Manual RTMP only. Paste the server URL and stream key shown in TikTok LIVE setup.");
	else if (manualServer)
		text = QStringLiteral("Paste the RTMP or RTMPS server URL supplied by the platform.");
	else
		text = QString("Server preset: %1").arg(preset.defaultServer);
	const int recommendedBitrate = preset.recommendedOutput == QStringLiteral("dsk-vertical")
					 ? preset.verticalBitrateKbps
					 : preset.horizontalBitrateKbps;
	text += QString(" Recommended: %1 at %2 kbps.")
			.arg(outputDisplayName(preset.recommendedOutput))
			.arg(recommendedBitrate);
	if (!preset.note.isEmpty())
		text += QString(" %1").arg(preset.note);
	platformHint_->setText(text);
	if (presetServerButton_) {
		presetServerButton_->setEnabled(!manualServer);
		presetServerButton_->setToolTip(manualServer ? QStringLiteral("This platform does not have a safe built-in server URL.")
								 : QStringLiteral("Restore the built-in server URL."));
	}
	if (streamKey_) {
		if (streamKey_->text().trimmed().isEmpty() && !authCredentialRef_.trimmed().isEmpty())
			streamKey_->setPlaceholderText(QStringLiteral("Saved stream key exists - leave blank to keep"));
		else if (preset.id == QStringLiteral("tiktok"))
			streamKey_->setPlaceholderText(QStringLiteral("Paste TikTok stream key"));
		else
			streamKey_->setPlaceholderText(QStringLiteral("Enter stream key"));
	}

	if (!authStatus_)
		return;

	const bool connected = hasConnectedOAuthAccount(authMode);
	const bool loginRunning = oauthConnector_->isRunning();
	const bool hasBundledYouTubeOAuth = oauthHasBundledClientCredentials(TargetAuthMode::YouTubeOAuth);
	const bool useCustomYouTubeOAuth = authMode == TargetAuthMode::YouTubeOAuth &&
		(!hasBundledYouTubeOAuth || checkedOr(useCustomOAuthApp_, false));
	if (loginRunning && authMode == TargetAuthMode::TwitchOAuth) {
		authStatus_->setText("Waiting for Twitch login. The saved connection remains unchanged until this login succeeds and you press Save.");
	} else if (loginRunning && authMode == TargetAuthMode::YouTubeOAuth) {
		authStatus_->setText("Waiting for YouTube login. The saved connection remains unchanged until this login succeeds and you press Save.");
	} else if (loginRunning && authMode == TargetAuthMode::KickOAuth) {
		authStatus_->setText("Waiting for Kick login. The saved connection remains unchanged until this login succeeds and you press Save.");
	} else if (authMode == TargetAuthMode::TwitchOAuth) {
		if (connected)
			authStatus_->setText(QString("Connected as %1. Reconnect refreshes the saved Twitch stream key.")
					     .arg(authAccountName_.isEmpty() ? QStringLiteral("Twitch") : authAccountName_));
		else
			authStatus_->setText("Press Connect Twitch. DSK signs in through its publisher service and retrieves this account's stream key automatically.");
	} else if (authMode == TargetAuthMode::YouTubeOAuth) {
		if (connected)
			authStatus_->setText(QString("Connected as %1. Reconnect replaces the saved YouTube authorization.")
					     .arg(authAccountName_.isEmpty() ? QStringLiteral("YouTube") : authAccountName_));
		else if (!useCustomYouTubeOAuth)
			authStatus_->setText("Press Connect YouTube. DSK uses its bundled desktop OAuth app; no Google developer credentials are required. Enter or keep the YouTube stream key below.");
		else
			authStatus_->setText("Enter a Google OAuth Client ID and Client Secret, then press Connect YouTube. DSK uses this login to start YouTube Live after the RTMP signal is active. Redirect URI: http://localhost:17371/callback");
	} else if (authMode == TargetAuthMode::KickOAuth) {
		if (connected)
			authStatus_->setText(QString("Connected as %1. Reconnect refreshes the saved Kick stream URL and key.")
					     .arg(authAccountName_.isEmpty() ? QStringLiteral("Kick") : authAccountName_));
		else
			authStatus_->setText("Press Connect Kick. DSK signs in through its publisher service and retrieves this account's stream URL and key automatically.");
	} else
		authStatus_->setText("Manual RTMP uses the server URL and stream key entered below. YouTube manual RTMP sends signal only; start the broadcast in YouTube Studio or switch to Login with YouTube.");

	const bool oauthMode = authMode == TargetAuthMode::TwitchOAuth || authMode == TargetAuthMode::YouTubeOAuth ||
			       authMode == TargetAuthMode::KickOAuth;
	if (platform_)
		platform_->setEnabled(!loginRunning);
	if (authMode_)
		authMode_->setEnabled(!loginRunning);
	if (useCustomOAuthApp_) {
		useCustomOAuthApp_->setVisible(authMode == TargetAuthMode::YouTubeOAuth && hasBundledYouTubeOAuth);
		useCustomOAuthApp_->setEnabled(!loginRunning);
	}
	const bool showDeveloperCredentials = useCustomYouTubeOAuth;
	if (oauthClientId_) {
		oauthClientId_->setVisible(showDeveloperCredentials);
		oauthClientId_->setEnabled(!loginRunning);
	}
	if (oauthClientIdLabel_)
		oauthClientIdLabel_->setVisible(showDeveloperCredentials);
	if (oauthClientSecret_) {
		oauthClientSecret_->setVisible(showDeveloperCredentials);
		oauthClientSecret_->setEnabled(!loginRunning);
	}
	if (oauthClientSecretLabel_)
		oauthClientSecretLabel_->setVisible(showDeveloperCredentials);
	if (connectOAuthButton_) {
		connectOAuthButton_->setEnabled(oauthMode && !loginRunning);
		if (loginRunning)
			connectOAuthButton_->setText("Waiting...");
		else if (authMode == TargetAuthMode::TwitchOAuth)
			connectOAuthButton_->setText(connected ? "Reconnect Twitch" : "Connect Twitch");
		else if (authMode == TargetAuthMode::YouTubeOAuth)
			connectOAuthButton_->setText(connected ? "Reconnect YouTube" : "Connect YouTube");
		else if (authMode == TargetAuthMode::KickOAuth)
			connectOAuthButton_->setText(connected ? "Reconnect Kick" : "Connect Kick");
		else
			connectOAuthButton_->setText("Connect");
	}
	if (disconnectOAuthButton_) {
		disconnectOAuthButton_->setVisible(oauthMode && connected);
		disconnectOAuthButton_->setEnabled(!loginRunning);
	}
}

void TargetEditDialog::updateStreamKeyVisibility(bool show)
{
	streamKey_->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
}

void TargetEditDialog::connectOAuthAccount()
{
	const TargetAuthMode authMode = targetAuthModeFromString(authMode_->currentData().toString());
	if (authMode != TargetAuthMode::TwitchOAuth && authMode != TargetAuthMode::YouTubeOAuth &&
	    authMode != TargetAuthMode::KickOAuth) {
		authStatus_->setText("Select a supported account login first.");
		return;
	}
	if (isPublisherManagedAuth(authMode)) {
		const QString providerName = authMode == TargetAuthMode::TwitchOAuth ? QStringLiteral("Twitch")
									       : QStringLiteral("Kick");
		authStatus_->setText(hasConnectedOAuthAccount(authMode)
					     ? QString("Opening %1 login. The saved connection stays unchanged unless the new login succeeds and you press Save.").arg(providerName)
					     : QString("Opening %1 login in your browser...").arg(providerName));
		if (oauthConnector_->begin(authMode, QString(), QString()))
			updatePlatformHint();
		return;
	}
	const bool useCustomYouTubeOAuth = !oauthHasBundledClientCredentials(TargetAuthMode::YouTubeOAuth) ||
		checkedOr(useCustomOAuthApp_, false);
	OAuthClientCredentials credentials;
	if (useCustomYouTubeOAuth) {
		const OAuthClientFields oauthFields = normalizeOAuthClientFields(oauthClientId_->text(), oauthClientSecret_->text());
		if (oauthFields.parsedJson) {
			oauthClientId_->setText(oauthFields.clientId);
			oauthClientSecret_->setText(oauthFields.clientSecret);
		}
		if (oauthFields.droppedOversizedSecret) {
			authStatus_->setText("OAuth Client Secret is too large. Paste only the client_secret value, or paste the Google OAuth JSON so DSK can extract it.");
			oauthClientSecret_->setFocus();
			return;
		}
		credentials = {oauthFields.clientId, oauthFields.clientSecret};
		if (!credentials.isComplete()) {
			authStatus_->setText("Google Client ID and Client Secret are both required for a custom OAuth app. Redirect URI: http://localhost:17371/callback.");
			(credentials.clientId.isEmpty() ? oauthClientId_ : oauthClientSecret_)->setFocus();
			return;
		}
	} else {
		credentials = oauthBundledClientCredentials(TargetAuthMode::YouTubeOAuth);
		if (!credentials.isComplete()) {
			authStatus_->setText("This DSK build does not contain a YouTube OAuth application. Enable the custom Google OAuth app option and enter its credentials.");
			return;
		}
	}

	authStatus_->setText(hasConnectedOAuthAccount(authMode)
				     ? "Opening YouTube login. The saved connection stays unchanged unless the new login succeeds and you press Save."
				     : "Opening YouTube login in your browser...");
	if (oauthConnector_->begin(authMode, credentials.clientId, credentials.clientSecret))
		updatePlatformHint();
}

void TargetEditDialog::disconnectOAuthAccount()
{
	if (oauthConnector_->isRunning())
		return;

	const TargetAuthMode authMode = targetAuthModeFromString(authMode_->currentData().toString());
	if (authMode != TargetAuthMode::TwitchOAuth && authMode != TargetAuthMode::YouTubeOAuth &&
	    authMode != TargetAuthMode::KickOAuth)
		return;

	authAccountName_.clear();
	oauthRefreshToken_.clear();
	oauthRefreshTokenRef_.clear();
	oauthConnectedThisEdit_ = false;
	connectedAuthMode_ = TargetAuthMode::ManualRtmp;
	if (isPublisherManagedAuth(authMode)) {
		authCredentialRef_.clear();
		oauthClientIdValue_.clear();
		oauthClientSecretValue_.clear();
		oauthClientSecretRef_.clear();
		oauthClientId_->clear();
		oauthClientSecret_->clear();
		streamKeyValue_.clear();
		streamKey_->clear();
		streamKey_->setPlaceholderText(authMode == TargetAuthMode::TwitchOAuth
					       ? QStringLiteral("Connect Twitch to retrieve the stream key")
					       : QStringLiteral("Connect Kick to retrieve the stream key"));
	}

	updatePlatformHint();
	if (authMode == TargetAuthMode::TwitchOAuth)
		authStatus_->setText("Twitch disconnected in this editor. Press Save to remove the saved key, or Cancel to keep it.");
	else if (authMode == TargetAuthMode::KickOAuth)
		authStatus_->setText("Kick disconnected in this editor. Press Save to remove the saved key, or Cancel to keep it.");
	else
		authStatus_->setText("YouTube disconnected in this editor. Press Save to remove the saved authorization, or Cancel to keep it.");
}

void TargetEditDialog::handleOAuthFinished(const OAuthConnectionResult &result)
{
	const TargetAuthMode currentAuthMode = targetAuthModeFromString(authMode_->currentData().toString());
	const QString currentPlatformId = platform_->currentData().toString();
	if (result.authMode != currentAuthMode || !platformSupportsAuthMode(currentPlatformId, result.authMode)) {
		updatePlatformHint();
		authStatus_->setText("Ignored an OAuth result because the selected platform or login mode changed.");
		return;
	}
	if (!result.errorMessage.isEmpty()) {
		const bool preserved = hasConnectedOAuthAccount(result.authMode);
		updatePlatformHint();
		authStatus_->setText(preserved
					     ? QString("Reconnect failed; the saved connection was not changed. %1").arg(result.errorMessage)
					     : result.errorMessage);
		return;
	}

	authAccountName_ = result.accountName;
	oauthConnectedThisEdit_ = true;
	connectedAuthMode_ = result.authMode;
	if (result.authMode == TargetAuthMode::YouTubeOAuth)
		oauthRefreshToken_ = result.refreshToken;
	if (!result.serverUrl.isEmpty()) {
		serverFollowsPlatform_ = true;
		server_->setText(result.serverUrl);
	}
	if (!result.streamKey.isEmpty())
		streamKey_->setText(result.streamKey);
	if (name_->text().trimmed().isEmpty() || name_->text().trimmed() == "New Target")
		name_->setText(result.accountName.isEmpty() ? platforms_.presetById(platform_->currentData().toString()).displayName : result.accountName);
	if (enabled_)
		enabled_->setChecked(true);
	if (startWithAll_)
		startWithAll_->setChecked(true);
	updatePlatformHint();
	const bool hasStreamKey = !lineTextOrEmpty(streamKey_).trimmed().isEmpty() || !authCredentialRef_.trimmed().isEmpty();
	if (result.authMode == TargetAuthMode::YouTubeOAuth && !hasStreamKey) {
		authStatus_->setText(QString("Connected as %1. Enter the YouTube stream key, then Save.").arg(result.accountName));
		return;
	}

	authStatus_->setText(QString("Connected as %1. Review the settings, then press Save.").arg(result.accountName));
}

bool TargetEditDialog::hasConnectedOAuthAccount(TargetAuthMode authMode) const
{
	if (authMode == TargetAuthMode::TwitchOAuth)
		return !authAccountName_.trimmed().isEmpty() || !authCredentialRef_.trimmed().isEmpty();
	if (authMode == TargetAuthMode::KickOAuth)
		return !authAccountName_.trimmed().isEmpty() || !authCredentialRef_.trimmed().isEmpty();
	if (authMode == TargetAuthMode::YouTubeOAuth)
		return !authAccountName_.trimmed().isEmpty() || !oauthRefreshToken_.trimmed().isEmpty() ||
		       !oauthRefreshTokenRef_.trimmed().isEmpty();
	return false;
}

} // namespace dsk
