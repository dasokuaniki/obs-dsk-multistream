#include "ui/target-edit-dialog.hpp"

#include "core/diagnostics.hpp"
#include "core/experimental-features.hpp"
#include "core/oauth-provider.hpp"
#include "ui/localized-text.hpp"

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
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>

namespace dsk {

namespace {

constexpr int WindowsCredentialValueLimitBytes = 2560;
constexpr int TargetFormHorizontalSpacing = 12;
constexpr int TargetFormVerticalSpacing = 8;
constexpr int TargetDialogPreferredHeight = 680;

void configureTargetForm(QFormLayout *form)
{
	if (!form)
		return;
	form->setContentsMargins(0, 0, 0, 0);
	form->setHorizontalSpacing(TargetFormHorizontalSpacing);
	form->setVerticalSpacing(TargetFormVerticalSpacing);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	form->setFormAlignment(Qt::AlignTop);
	form->setRowWrapPolicy(QFormLayout::DontWrapRows);
}

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

QString text(const char *key, const char *englishFallback)
{
	return localizedText(key, englishFallback);
}

QString outputDisplayName(const QString &outputId)
{
	return outputId == QStringLiteral("dsk-vertical")
		       ? text("TargetEdit.OutputVerticalName", "DSK Vertical")
		       : text("TargetEdit.OutputHorizontalName", "DSK Horizontal");
}

QString authModeDisplayName(TargetAuthMode mode)
{
	switch (mode) {
	case TargetAuthMode::ManualRtmp:
		return text("TargetEdit.ManualRtmp", "Manual RTMP");
	case TargetAuthMode::TwitchOAuth:
		return text("TargetEdit.LoginTwitch", "Log in with Twitch");
	case TargetAuthMode::YouTubeOAuth:
		return text("TargetEdit.LoginYouTube", "Log in with YouTube");
	case TargetAuthMode::KickOAuth:
		return text("TargetEdit.LoginKick", "Log in with Kick");
	}
	return targetAuthModeDisplayName(mode);
}

QString broadcastModeDisplayName(YouTubeBroadcastMode mode)
{
	return mode == YouTubeBroadcastMode::ArchiveRotation
		       ? text("TargetEdit.BroadcastArchive", "Split every 11 h 30 min")
		       : text("TargetEdit.BroadcastNormal", "Normal");
}

QString sceneModeDisplayName(TargetSceneMode mode)
{
	switch (mode) {
	case TargetSceneMode::FollowObs:
		return text("TargetEdit.SceneFollowObs", "Follow OBS Program");
	case TargetSceneMode::FixedScene:
		return text("TargetEdit.SceneFixed", "Fixed OBS scene");
	case TargetSceneMode::LinkedScene:
		return text("TargetEdit.SceneLinked", "Linked OBS scenes");
	}
	return targetSceneModeDisplayName(mode);
}

QString platformDisplayName(const PlatformPreset &preset)
{
	return preset.id == QStringLiteral("custom")
		       ? text("TargetEdit.PlatformManualRtmp", "Manual RTMP")
		       : preset.displayName;
}

QString platformNote(const PlatformPreset &preset)
{
	if (preset.id == QStringLiteral("twitch"))
		return text("TargetEdit.PlatformNoteTwitch",
			    "Use Horizontal unless you are intentionally producing a mobile-only layout.");
	if (preset.id == QStringLiteral("youtube"))
		return text("TargetEdit.PlatformNoteYouTube",
			    "YouTube works well for both horizontal and vertical outputs.");
	if (preset.id == QStringLiteral("kick"))
		return text("TargetEdit.PlatformNoteKick", "Kick ingest commonly uses RTMPS.");
	if (preset.id == QStringLiteral("custom"))
		return text("TargetEdit.PlatformNoteCustom",
			    "Paste the RTMP URL and stream key issued by the streaming service.");
	return preset.note;
}

QString validationMessageForDisplay(const QString &message)
{
	if (message == QStringLiteral("Login mode does not match the selected platform."))
		return text("TargetEdit.ValidationAuthMode", "Login mode does not match the selected platform.");
	if (message == QStringLiteral("Server URL is empty."))
		return text("TargetEdit.ValidationServerEmpty", "Enter a server URL.");
	if (message == QStringLiteral("Server URL must start with rtmp:// or rtmps://."))
		return text("TargetEdit.ValidationServerScheme",
			    "The server URL must start with rtmp:// or rtmps://.");
	if (message == QStringLiteral("Server URL must include a host name."))
		return text("TargetEdit.ValidationServerHost", "The server URL does not contain a host name.");
	if (message == QStringLiteral("The legacy generic RTMP URL cannot be used. Paste the server URL issued for this stream."))
		return text("TargetEdit.ValidationLegacyUrl",
			    "The legacy generic RTMP URL cannot be used. Paste the server URL issued for this stream.");
	if (message == QStringLiteral("Stream key is empty."))
		return text("TargetEdit.ValidationStreamKey", "Enter a stream key.");
	if (message == QStringLiteral("Reconnect settings are invalid."))
		return text("TargetEdit.ValidationReconnect", "Check the reconnect settings.");
	if (message == QStringLiteral("Encoder settings are outside the supported range."))
		return text("TargetEdit.ValidationEncoder", "The encoder settings are outside the supported range.");
	if (message == QStringLiteral("Fixed scene mode needs an OBS scene."))
		return text("TargetEdit.ValidationFixedScene",
			    "Select an OBS scene when using Fixed scene mode.");
	return message;
}

} // namespace

TargetEditDialog::TargetEditDialog(const PlatformPresetRegistry &platforms, QWidget *parent)
	: QDialog(parent),
	  platforms_(platforms)
{
	setWindowTitle(text("TargetEdit.Title", "Edit Destination"));
	setMinimumWidth(540);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(10, 10, 10, 10);
	layout->setSpacing(8);

	auto *content = new QWidget(this);
	content->setObjectName(QStringLiteral("dskTargetContent"));
	auto *contentLayout = new QVBoxLayout(content);
	contentLayout->setContentsMargins(0, 0, 0, 0);
	contentLayout->setSpacing(8);
	contentLayout->setAlignment(Qt::AlignTop);

	auto *intro = new QLabel(text("TargetEdit.Intro",
				     "Configure one streaming destination. Start and stop it from Controls in DSK Streaming."),
				 this);
	intro->setObjectName(QStringLiteral("dskTargetIntro"));
	intro->setWordWrap(true);
	intro->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	intro->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	contentLayout->addWidget(intro);

	auto *basicForm = new QFormLayout();
	basicForm->setObjectName(QStringLiteral("dskTargetBasicForm"));
	configureTargetForm(basicForm);

	name_ = new QLineEdit(this);
	name_->setPlaceholderText(text("TargetEdit.NamePlaceholder", "Example: YouTube vertical"));
	platform_ = new QComboBox(this);
	platform_->setObjectName(QStringLiteral("dskTargetPlatform"));
	for (const auto &preset : platforms_.presets())
		platform_->addItem(platformDisplayName(preset), preset.id);
	authMode_ = new QComboBox(this);
	youtubeBroadcastMode_ = new QComboBox(this);
	youtubeBroadcastMode_->setObjectName(QStringLiteral("dskYouTubeBroadcastMode"));
	youtubeBroadcastMode_->addItem(broadcastModeDisplayName(YouTubeBroadcastMode::Normal),
				       youtubeBroadcastModeToString(YouTubeBroadcastMode::Normal));
	youtubeBroadcastMode_->addItem(broadcastModeDisplayName(YouTubeBroadcastMode::ArchiveRotation),
				       youtubeBroadcastModeToString(YouTubeBroadcastMode::ArchiveRotation));
	youtubeBroadcastMode_->setToolTip(text(
		"TargetEdit.YouTubeBroadcastTooltip",
		"Archive splitting keeps the RTMP upload running and changes YouTube broadcasts every 11 hours 30 minutes."));
	youtubeStream_ = new QComboBox(this);
	youtubeStream_->setObjectName(QStringLiteral("dskYouTubeStream"));
	youtubeStream_->addItem(text("TargetEdit.YouTubeManualKey",
				     "Manual key (including single-use streams)"),
			       -1);
	youtubeStream_->setToolTip(text(
		"TargetEdit.YouTubeStreamTooltip",
		"YouTube returns reusable saved RTMP streams here. Single-use streams can still be entered in the Stream key field."));
	authStatus_ = new QLabel(this);
	authStatus_->setObjectName(QStringLiteral("dskTargetAuthStatus"));
	authStatus_->setWordWrap(true);
	authStatus_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	authStatus_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
	youtubeLegalLinks_ = new QLabel(
		text("TargetEdit.YouTubeLegalLinks",
		     "DSK reads and manages YouTube Live broadcasts and streams for this account. Before connecting, "
		     "review the <a href=\"https://dsk.dasoku.org/privacy\">DSK Privacy Policy</a>, "
		     "<a href=\"https://www.youtube.com/t/terms\">YouTube Terms of Service</a>, "
		     "<a href=\"https://policies.google.com/privacy\">Google Privacy Policy</a>, and "
		     "<a href=\"https://security.google.com/settings/security/permissions\">Google permissions</a>."),
		this);
	youtubeLegalLinks_->setObjectName(QStringLiteral("dskYouTubeLegalLinks"));
	youtubeLegalLinks_->setTextFormat(Qt::RichText);
	youtubeLegalLinks_->setTextInteractionFlags(Qt::TextBrowserInteraction);
	youtubeLegalLinks_->setOpenExternalLinks(true);
	youtubeLegalLinks_->setWordWrap(true);
	youtubeLegalLinks_->setVisible(false);
	useCustomOAuthApp_ =
		new QCheckBox(text("TargetEdit.CustomOAuth", "Use custom Google OAuth app"), this);
	useCustomOAuthApp_->setObjectName(QStringLiteral("dskUseCustomOAuthApp"));
	useCustomOAuthApp_->setToolTip(text(
		"TargetEdit.CustomOAuthTooltip",
		"Use your own Google OAuth Client ID and Client Secret instead of the DSK publisher app."));
	oauthClientId_ = new QLineEdit(this);
	oauthClientId_->setObjectName(QStringLiteral("dskOAuthClientId"));
	oauthClientId_->setPlaceholderText(text("TargetEdit.OAuthClientIdPlaceholder",
						"OAuth app Client ID"));
	oauthClientSecret_ = new QLineEdit(this);
	oauthClientSecret_->setObjectName(QStringLiteral("dskOAuthClientSecret"));
	oauthClientSecret_->setEchoMode(QLineEdit::Password);
	oauthClientSecret_->setPlaceholderText(text("TargetEdit.OAuthClientSecretPlaceholder",
						    "Optional client secret"));
	connectOAuthButton_ = new QPushButton(text("TargetEdit.Connect", "Connect"), this);
	connectOAuthButton_->setObjectName(QStringLiteral("dskConnectOAuth"));
	connect(connectOAuthButton_, &QPushButton::clicked, this, &TargetEditDialog::toggleOAuthConnection);
	oauthConnector_ = new OAuthConnector(this);
	connect(oauthConnector_, &OAuthConnector::finished, this, &TargetEditDialog::handleOAuthFinished);
	platformHint_ = new QLabel(this);
	platformHint_->setObjectName(QStringLiteral("dskPlatformHint"));
	platformHint_->setWordWrap(true);
	platformHint_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	platformHint_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

	server_ = new QLineEdit(this);
	server_->setObjectName(QStringLiteral("dskTargetServer"));
	server_->setPlaceholderText(text("TargetEdit.ServerPlaceholder",
					 "rtmp:// or rtmps:// server URL"));
	presetServerButton_ = new QPushButton(text("TargetEdit.UsePreset", "Use preset"), this);
	presetServerButton_->setObjectName(QStringLiteral("dskUsePresetServer"));
	connect(presetServerButton_, &QPushButton::clicked, this, &TargetEditDialog::usePresetServer);
	auto *serverRow = new QHBoxLayout();
	serverRow->setContentsMargins(0, 0, 0, 0);
	serverRow->addWidget(server_, 1);
	serverRow->addWidget(presetServerButton_);

	streamKey_ = new QLineEdit(this);
	streamKey_->setObjectName(QStringLiteral("dskTargetStreamKey"));
	streamKey_->setEchoMode(QLineEdit::Password);
	streamKey_->setPlaceholderText(text("TargetEdit.StreamKeyPlaceholder", "Enter stream key"));
	showStreamKey_ = new QCheckBox(text("TargetEdit.ShowKey", "Show key"), this);
	connect(showStreamKey_, &QCheckBox::toggled, this, &TargetEditDialog::updateStreamKeyVisibility);
	auto *keyRow = new QHBoxLayout();
	keyRow->setContentsMargins(0, 0, 0, 0);
	keyRow->addWidget(streamKey_, 1);
	keyRow->addWidget(showStreamKey_);

	encoderGroup_ = new QComboBox(this);
	encoderGroup_->addItem(text("TargetEdit.OutputHorizontal", "Horizontal (16:9)"),
			      encoderGroupToString(EncoderGroup::DskHorizontal));
	encoderGroup_->addItem(text("TargetEdit.OutputVertical", "Vertical (9:16)"),
			      encoderGroupToString(EncoderGroup::DskVertical));

	videoEncoder_ = new QComboBox(this);
	videoEncoder_->setObjectName(QStringLiteral("dskTargetVideoEncoder"));
	videoEncoder_->addItem(text("TargetEdit.AutoEncoder", "Auto (recommended - H.264)"), QString());
	videoEncoder_->addItem("NVIDIA NVENC H.264", QStringLiteral("obs_nvenc_h264_tex"));
	videoEncoder_->addItem("NVIDIA NVENC HEVC", QStringLiteral("obs_nvenc_hevc_tex"));
	videoEncoder_->addItem("NVIDIA NVENC H.264 (fallback)", QStringLiteral("ffmpeg_nvenc"));
	videoEncoder_->addItem("AMD AMF H.264", QStringLiteral("h264_texture_amf"));
	videoEncoder_->addItem("Intel QSV H.264", QStringLiteral("obs_qsv11_v2"));
	videoEncoder_->addItem("x264 (CPU)", QStringLiteral("obs_x264"));
	videoEncoder_->setToolTip(text(
		"TargetEdit.AutoEncoderTooltip",
		"Auto selects a compatible H.264 hardware encoder when possible. DSK creates a separate DSK encoder for this output, including Vertical; it does not reuse OBS's active streaming encoder."));

	sceneMode_ = new QComboBox(this);
	sceneMode_->addItem(sceneModeDisplayName(TargetSceneMode::FollowObs),
			   targetSceneModeToString(TargetSceneMode::FollowObs));
	sceneMode_->addItem(sceneModeDisplayName(TargetSceneMode::FixedScene),
			   targetSceneModeToString(TargetSceneMode::FixedScene));
	sceneMode_->addItem(sceneModeDisplayName(TargetSceneMode::LinkedScene),
			   targetSceneModeToString(TargetSceneMode::LinkedScene));
	sceneMode_->setToolTip(text(
		"TargetEdit.SceneRoutingTooltip",
		"Choose whether this DSK output follows OBS Program or uses a separate OBS scene."));
	sceneName_ = new QLineEdit(this);
	sceneName_->setPlaceholderText(text(
		"TargetEdit.ScenePlaceholder",
		"OBS scene name for Fixed mode, or fallback scene for Linked mode"));

	useSharedEncoder_ = new QCheckBox(
		text("TargetEdit.ShareEncoder", "Share encoder with matching DSK destinations"), this);
	useSharedEncoder_->setToolTip(text(
		"TargetEdit.ShareEncoderTooltip",
		"Destinations with the same DSK output direction, canvas and bitrate can reuse one DSK encoder. The active OBS streaming encoder is not shared."));
	autoStartWithObs_ = new QCheckBox(
		text("TargetEdit.AutoStartObs", "Start this destination when OBS starts streaming"), this);
	autoStopWithObs_ = new QCheckBox(
		text("TargetEdit.AutoStopObs", "Stop this destination when OBS stops streaming"), this);
	reconnectEnabled_ =
		new QCheckBox(text("TargetEdit.AutoReconnect", "Auto reconnect"), this);

	reconnectMaxRetries_ = new QSpinBox(this);
	reconnectMaxRetries_->setRange(0, 100);
	reconnectMaxRetries_->setSuffix(text("TargetEdit.ReconnectRetrySuffix", " retries"));
	reconnectDelaySeconds_ = new QSpinBox(this);
	reconnectDelaySeconds_->setRange(1, 60);
	reconnectDelaySeconds_->setSuffix(text("TargetEdit.SecondsSuffix", " sec"));
	videoBitrateKbps_ = new QSpinBox(this);
	videoBitrateKbps_->setRange(0, 100000);
	videoBitrateKbps_->setSingleStep(500);
	videoBitrateKbps_->setSuffix(text("TargetEdit.KbpsSuffix", " kbps"));
	videoBitrateKbps_->setSpecialValueText(text("TargetEdit.DefaultValue", "Default"));
	audioBitrateKbps_ = new QSpinBox(this);
	audioBitrateKbps_->setRange(0, 1024);
	audioBitrateKbps_->setSingleStep(32);
	audioBitrateKbps_->setSuffix(text("TargetEdit.KbpsSuffix", " kbps"));
	audioBitrateKbps_->setSpecialValueText(text("TargetEdit.DefaultValue", "Default"));
	keyframeSeconds_ = new QSpinBox(this);
	keyframeSeconds_->setRange(1, 10);
	keyframeSeconds_->setSuffix(text("TargetEdit.SecondsSuffix", " sec"));

	enabled_ =
		new QCheckBox(text("TargetEdit.AllowBulk", "Allow automatic and bulk starts"), this);
	enabled_->setToolTip(text(
		"TargetEdit.AllowBulkTooltip",
		"Controls Start All and OBS-linked automatic starts. Individual Start remains available."));
	startWithAll_ =
		new QCheckBox(text("TargetEdit.IncludeStartAll", "Include in Start All"), this);
	startWithAll_->setToolTip(text(
		"TargetEdit.IncludeStartAllTooltip",
		"When enabled, DSK Streaming includes this destination in Start All."));
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
	connect(youtubeBroadcastMode_, &QComboBox::currentIndexChanged, this, [this]() {
		youtubeBroadcastModeValue_ = comboDataOr(
			youtubeBroadcastMode_, youtubeBroadcastModeToString(YouTubeBroadcastMode::Normal));
	});
	connect(youtubeStream_, &QComboBox::currentIndexChanged, this, &TargetEditDialog::applySelectedYouTubeStream);
	connect(useCustomOAuthApp_, &QCheckBox::toggled, this, &TargetEditDialog::updatePlatformHint);
	connect(useCustomOAuthApp_, &QCheckBox::toggled, this, [this](bool checked) {
		if (checked && advancedSettingsToggle_)
			advancedSettingsToggle_->setChecked(true);
	});
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
	connect(streamKey_, &QLineEdit::textEdited, this, [this]() {
		if (youtubeStream_ && youtubeStream_->currentIndex() > 0) {
			const QSignalBlocker blocker(youtubeStream_);
			youtubeStream_->setCurrentIndex(0);
		}
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

	basicForm->addRow(text("TargetEdit.Name", "Destination name"), name_);
	basicForm->addRow(text("TargetEdit.Platform", "Platform"), platform_);
	basicForm->addRow(text("TargetEdit.Connection", "Connection"), connectionRow);
	basicForm->addRow(text("TargetEdit.YouTubeBroadcast", "YouTube broadcast"),
			  youtubeBroadcastMode_);
	youtubeBroadcastModeLabel_ = basicForm->labelForField(youtubeBroadcastMode_);
	basicForm->addRow(text("TargetEdit.YouTubeStream", "YouTube stream"), youtubeStream_);
	youtubeStreamLabel_ = basicForm->labelForField(youtubeStream_);
	basicForm->addRow(authStatus_);
	basicForm->addRow(youtubeLegalLinks_);
	basicForm->addRow(platformHint_);
	basicForm->addRow(text("TargetEdit.Server", "Server"), serverRow);
	basicForm->addRow(text("TargetEdit.StreamKey", "Stream key"), keyRow);
	basicForm->addRow(text("TargetEdit.OutputMode", "Output direction"), encoderGroup_);
	basicForm->addRow(QString(), startWithAll_);
	contentLayout->addLayout(basicForm);

	advancedSettingsToggle_ = new QToolButton(content);
	advancedSettingsToggle_->setObjectName(QStringLiteral("dskAdvancedSettingsToggle"));
	advancedSettingsToggle_->setText(text("TargetEdit.Advanced", "Advanced settings"));
	advancedSettingsToggle_->setCheckable(true);
	advancedSettingsToggle_->setChecked(false);
	advancedSettingsToggle_->setArrowType(Qt::RightArrow);
	advancedSettingsToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	contentLayout->addWidget(advancedSettingsToggle_);

	advancedSettingsPanel_ = new QWidget(content);
	advancedSettingsPanel_->setObjectName(QStringLiteral("dskAdvancedSettingsPanel"));
	auto *advancedForm = new QFormLayout(advancedSettingsPanel_);
	advancedForm->setObjectName(QStringLiteral("dskTargetAdvancedForm"));
	configureTargetForm(advancedForm);
	advancedForm->addRow(QString(), useCustomOAuthApp_);
	advancedForm->addRow(text("TargetEdit.OAuthClientId", "OAuth Client ID"), oauthClientId_);
	oauthClientIdLabel_ = advancedForm->labelForField(oauthClientId_);
	advancedForm->addRow(text("TargetEdit.OAuthClientSecret", "OAuth Client Secret"),
			     oauthClientSecret_);
	oauthClientSecretLabel_ = advancedForm->labelForField(oauthClientSecret_);
	advancedForm->addRow(text("TargetEdit.VideoEncoder", "Video encoder"), videoEncoder_);
	advancedForm->addRow(text("TargetEdit.SceneRouting", "Scene routing"), sceneMode_);
	advancedForm->addRow(text("TargetEdit.Scene", "Scene"), sceneName_);
	const bool showExperimentalSceneRouting = experimentalSceneRoutingEnabled();
	advancedForm->setRowVisible(sceneMode_, showExperimentalSceneRouting);
	advancedForm->setRowVisible(sceneName_, showExperimentalSceneRouting);
	advancedForm->addRow(QString(), useSharedEncoder_);
	advancedForm->addRow(QString(), enabled_);
	advancedForm->addRow(QString(), autoStartWithObs_);
	advancedForm->addRow(QString(), autoStopWithObs_);
	advancedForm->addRow(QString(), reconnectEnabled_);
	advancedForm->addRow(text("TargetEdit.ReconnectRetries", "Reconnect retries"),
			     reconnectMaxRetries_);
	advancedForm->addRow(text("TargetEdit.ReconnectDelay", "Reconnect delay"),
			     reconnectDelaySeconds_);
	advancedForm->addRow(text("TargetEdit.VideoBitrate", "Video bitrate"),
			     videoBitrateKbps_);
	advancedForm->addRow(text("TargetEdit.AudioBitrate", "Audio bitrate"),
			     audioBitrateKbps_);
	advancedForm->addRow(text("TargetEdit.Keyframe", "Keyframe interval"),
			     keyframeSeconds_);
	advancedSettingsPanel_->setVisible(false);
	contentLayout->addWidget(advancedSettingsPanel_);
	connect(advancedSettingsToggle_, &QToolButton::toggled, this, [this](bool expanded) {
		advancedSettingsPanel_->setVisible(expanded);
		advancedSettingsToggle_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
	});

	auto *scrollArea = new QScrollArea(this);
	scrollArea->setObjectName(QStringLiteral("dskTargetScrollArea"));
	scrollArea->setWidgetResizable(true);
	scrollArea->setFrameShape(QFrame::NoFrame);
	scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	scrollArea->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	scrollArea->setWidget(content);
	const int dialogHeight = boundedDialogHeight(parent);
	scrollArea->setMaximumHeight(qMax(280, dialogHeight - 96));
	layout->addWidget(scrollArea, 1);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	if (auto *saveButton = buttons->button(QDialogButtonBox::Ok))
		saveButton->setText(text("TargetEdit.Save", "Save"));
	if (auto *cancelButton = buttons->button(QDialogButtonBox::Cancel))
		cancelButton->setText(text("TargetEdit.Cancel", "Cancel"));
	connect(buttons, &QDialogButtonBox::accepted, this, &TargetEditDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	auto *line = new QFrame(this);
	line->setFrameShape(QFrame::HLine);
	layout->addWidget(line);
	layout->addWidget(buttons);
	resize(qMax(620, minimumWidth()), qMin(dialogHeight, TargetDialogPreferredHeight));
}

void TargetEditDialog::setTarget(const OutputTarget &target)
{
	loading_ = true;
	resetYouTubeStreamOptions();
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
	if (advancedSettingsToggle_)
		advancedSettingsToggle_->setChecked(useCustomOAuthApp_->isChecked());
	oauthClientId_->setText(target.oauthClientId);
	oauthClientSecret_->setText(target.oauthClientSecret);
	videoEncoderId_ = target.videoEncoderId;
	const int videoEncoderIndex = videoEncoder_->findData(videoEncoderId_);
	if (videoEncoderIndex < 0 && !videoEncoderId_.trimmed().isEmpty())
		videoEncoder_->addItem(text("TargetEdit.CustomEncoder", "Custom: %1").arg(videoEncoderId_),
				      videoEncoderId_);
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
					       ? text("TargetEdit.SavedStreamKeyPlaceholder",
						      "Saved stream key exists - leave blank to keep")
					       : text("TargetEdit.StreamKeyPlaceholder",
						      "Enter stream key"));
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
	const int youtubeModeIndex = youtubeBroadcastMode_->findData(
		youtubeBroadcastModeToString(target.youtubeBroadcastMode));
	youtubeBroadcastMode_->setCurrentIndex(youtubeModeIndex >= 0 ? youtubeModeIndex : 0);
	youtubeBroadcastModeValue_ = comboDataOr(
		youtubeBroadcastMode_, youtubeBroadcastModeToString(YouTubeBroadcastMode::Normal));

	const int encoderIndex = encoderGroup_->findData(encoderGroupToString(target.encoderGroup));
	encoderGroup_->setCurrentIndex(encoderIndex >= 0 ? encoderIndex : 0);
	encoderGroupValue_ = comboDataOr(encoderGroup_, encoderGroupToString(EncoderGroup::DskHorizontal));
	updatePlatformHint();
	loading_ = false;
}

void TargetEditDialog::setNewTargetDefaults(const QString &targetId)
{
	loading_ = true;
	resetYouTubeStreamOptions();
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
	if (advancedSettingsToggle_)
		advancedSettingsToggle_->setChecked(false);
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
	youtubeBroadcastModeValue_ = youtubeBroadcastModeToString(YouTubeBroadcastMode::Normal);
	youtubeBroadcastMode_->setCurrentIndex(
		youtubeBroadcastMode_->findData(youtubeBroadcastModeValue_));
	nameValue_ = text("TargetEdit.NewTarget", "New Target");
	name_->setText(nameValue_);
	serverUrlValue_ = QStringLiteral("rtmp://live.twitch.tv/app");
	server_->setText("rtmp://live.twitch.tv/app");
	serverFollowsPlatform_ = true;
	streamKeyValue_.clear();
	streamKey_->clear();
	streamKey_->setPlaceholderText(text("TargetEdit.StreamKeyPlaceholder", "Enter stream key"));
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
	copy.youtubeBroadcastMode = copy.platformId == QStringLiteral("youtube") &&
					    copy.authMode == TargetAuthMode::YouTubeOAuth
				    ? youtubeBroadcastModeFromString(detachedString(comboDataOr(
					      youtubeBroadcastMode_, youtubeBroadcastModeValue_)))
				    : YouTubeBroadcastMode::Normal;
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
		copy.name = platformDisplayName(platforms_.presetById(copy.platformId));
	if (copy.name.isEmpty())
		copy.name = text("TargetEdit.NewTarget", "New Target");
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
		if ((errorMessage.startsWith(QStringLiteral("Server URL")) ||
		     errorMessage.startsWith(QStringLiteral("The legacy generic RTMP URL"))) &&
		    server_)
			server_->setFocus();
		else if (errorMessage == QStringLiteral("Stream key is empty.") && streamKey_)
			streamKey_->setFocus();
		else if (errorMessage == QStringLiteral("Fixed scene mode needs an OBS scene.") && sceneName_)
			sceneName_->setFocus();
		QMessageBox::warning(this, text("TargetEdit.ValidationTitle", "Check destination settings"),
				     validationMessageForDisplay(errorMessage));
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
		name_->setText(platformDisplayName(preset));
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
	authMode_->addItem(authModeDisplayName(TargetAuthMode::ManualRtmp),
			   targetAuthModeToString(TargetAuthMode::ManualRtmp));
	if (platformSupportsAuthMode(platformId, TargetAuthMode::TwitchOAuth))
		authMode_->addItem(authModeDisplayName(TargetAuthMode::TwitchOAuth),
				   targetAuthModeToString(TargetAuthMode::TwitchOAuth));
	if (platformSupportsAuthMode(platformId, TargetAuthMode::YouTubeOAuth))
		authMode_->addItem(authModeDisplayName(TargetAuthMode::YouTubeOAuth),
				   targetAuthModeToString(TargetAuthMode::YouTubeOAuth));
	if (platformSupportsAuthMode(platformId, TargetAuthMode::KickOAuth))
		authMode_->addItem(authModeDisplayName(TargetAuthMode::KickOAuth),
				   targetAuthModeToString(TargetAuthMode::KickOAuth));
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
	const bool showYouTubeBroadcastMode = preset.id == QStringLiteral("youtube") &&
					  authMode == TargetAuthMode::YouTubeOAuth;
	if (youtubeBroadcastMode_)
		youtubeBroadcastMode_->setVisible(showYouTubeBroadcastMode);
	if (youtubeBroadcastModeLabel_)
		youtubeBroadcastModeLabel_->setVisible(showYouTubeBroadcastMode);
	if (youtubeStream_)
		youtubeStream_->setVisible(showYouTubeBroadcastMode);
	if (youtubeStreamLabel_)
		youtubeStreamLabel_->setVisible(showYouTubeBroadcastMode);
	const bool manualServer = preset.defaultServer.trimmed().isEmpty();
	QString hintText;
	if (manualServer)
		hintText = text("TargetEdit.PlatformHintManual",
				"Paste the RTMP or RTMPS server URL supplied by the platform.");
	else
		hintText =
			text("TargetEdit.PlatformHintPreset", "Server preset: %1").arg(preset.defaultServer);
	const int recommendedBitrate = preset.recommendedOutput == QStringLiteral("dsk-vertical")
					 ? preset.verticalBitrateKbps
					 : preset.horizontalBitrateKbps;
	hintText += text("TargetEdit.PlatformHintRecommended", " Recommended: %1 at %2 kbps.")
			    .arg(outputDisplayName(preset.recommendedOutput))
			    .arg(recommendedBitrate);
	const QString localizedNote = platformNote(preset);
	if (!localizedNote.isEmpty())
		hintText += QStringLiteral(" ") + localizedNote;
	platformHint_->setText(hintText);
	if (presetServerButton_) {
		presetServerButton_->setEnabled(!manualServer);
		presetServerButton_->setToolTip(
			manualServer
				? text("TargetEdit.NoPresetTooltip",
				       "This platform does not have a safe built-in server URL.")
				: text("TargetEdit.RestorePresetTooltip",
				       "Restore the built-in server URL."));
	}
	if (streamKey_) {
		if (streamKey_->text().trimmed().isEmpty() && !authCredentialRef_.trimmed().isEmpty())
			streamKey_->setPlaceholderText(text(
				"TargetEdit.SavedStreamKeyPlaceholder",
				"Saved stream key exists - leave blank to keep"));
		else
			streamKey_->setPlaceholderText(
				text("TargetEdit.StreamKeyPlaceholder", "Enter stream key"));
	}

	if (!authStatus_)
		return;

	const bool connected = hasConnectedOAuthAccount(authMode);
	const bool loginRunning = oauthConnector_->isRunning();
	const bool hasBundledYouTubeOAuth = oauthHasBundledClientCredentials(TargetAuthMode::YouTubeOAuth);
	const bool useCustomYouTubeOAuth = authMode == TargetAuthMode::YouTubeOAuth &&
		(!hasBundledYouTubeOAuth || checkedOr(useCustomOAuthApp_, false));
	if (loginRunning && authMode == TargetAuthMode::TwitchOAuth) {
		authStatus_->setText(text(
			"TargetEdit.AuthWaitingTwitch",
			"Waiting for Twitch login. The saved connection remains unchanged until this login succeeds and you press Save."));
	} else if (loginRunning && authMode == TargetAuthMode::YouTubeOAuth) {
		authStatus_->setText(text(
			"TargetEdit.AuthWaitingYouTube",
			"Waiting for YouTube login. The saved connection remains unchanged until this login succeeds and you press Save."));
	} else if (loginRunning && authMode == TargetAuthMode::KickOAuth) {
		authStatus_->setText(text(
			"TargetEdit.AuthWaitingKick",
			"Waiting for Kick login. The saved connection remains unchanged until this login succeeds and you press Save."));
	} else if (authMode == TargetAuthMode::TwitchOAuth) {
		if (connected)
			authStatus_->setText(text(
						     "TargetEdit.AuthConnectedTwitch",
						     "Connected as %1. Use Disconnect to remove this connection.")
					     .arg(authAccountName_.isEmpty() ? QStringLiteral("Twitch") : authAccountName_));
		else
			authStatus_->setText(text(
				"TargetEdit.AuthConnectTwitchHelp",
				"Press Connect. DSK signs in to Twitch through its publisher service and retrieves this account's stream key automatically."));
	} else if (authMode == TargetAuthMode::YouTubeOAuth) {
		if (connected)
			authStatus_->setText(text(
						     "TargetEdit.AuthConnectedYouTube",
						     "Connected as %1. Use Disconnect to remove this connection.")
					     .arg(authAccountName_.isEmpty() ? QStringLiteral("YouTube") : authAccountName_));
		else if (!useCustomYouTubeOAuth)
			authStatus_->setText(text(
				"TargetEdit.AuthConnectYouTubeHelp",
				"Press Connect. DSK signs in to YouTube and retrieves reusable saved streams for selection. Single-use streams can still be entered manually."));
		else
			authStatus_->setText(text(
				"TargetEdit.AuthCustomYouTubeHelp",
				"Enter a Google OAuth Client ID and Client Secret, then press Connect. DSK retrieves reusable saved streams and uses this login to start YouTube Live. Redirect URI: http://localhost:17371/callback"));
	} else if (authMode == TargetAuthMode::KickOAuth) {
		if (connected)
			authStatus_->setText(text(
						     "TargetEdit.AuthConnectedKick",
						     "Connected as %1. Use Disconnect to remove this connection.")
					     .arg(authAccountName_.isEmpty() ? QStringLiteral("Kick") : authAccountName_));
		else
			authStatus_->setText(text(
				"TargetEdit.AuthConnectKickHelp",
				"Press Connect. DSK signs in to Kick through its publisher service and retrieves this account's stream URL and key automatically."));
	} else
		authStatus_->setText(text(
			"TargetEdit.AuthManualHelp",
			"Manual RTMP uses the server URL and stream key entered below. YouTube manual RTMP sends signal only; start the broadcast in YouTube Studio or switch to Login with YouTube."));

	const bool oauthMode = authMode == TargetAuthMode::TwitchOAuth || authMode == TargetAuthMode::YouTubeOAuth ||
			       authMode == TargetAuthMode::KickOAuth;
	const bool showYouTubeDisclosure = authMode == TargetAuthMode::YouTubeOAuth;
	if (youtubeLegalLinks_)
		youtubeLegalLinks_->setVisible(showYouTubeDisclosure);
	if (platform_)
		platform_->setEnabled(!loginRunning);
	if (authMode_)
		authMode_->setEnabled(!loginRunning);
	if (useCustomOAuthApp_) {
		useCustomOAuthApp_->setVisible(authMode == TargetAuthMode::YouTubeOAuth && hasBundledYouTubeOAuth);
		useCustomOAuthApp_->setEnabled(!loginRunning);
	}
	const bool showDeveloperCredentials = useCustomYouTubeOAuth;
	if (showDeveloperCredentials && !hasBundledYouTubeOAuth && advancedSettingsToggle_)
		advancedSettingsToggle_->setChecked(true);
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
			connectOAuthButton_->setText(text("TargetEdit.Waiting", "Waiting..."));
		else
			connectOAuthButton_->setText(connected ? text("TargetEdit.Disconnect", "Disconnect")
							       : text("TargetEdit.Connect", "Connect"));
	}
}

void TargetEditDialog::updateStreamKeyVisibility(bool show)
{
	streamKey_->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
}

void TargetEditDialog::toggleOAuthConnection()
{
	if (oauthConnector_->isRunning())
		return;

	const TargetAuthMode authMode = targetAuthModeFromString(authMode_->currentData().toString());
	if (hasConnectedOAuthAccount(authMode))
		disconnectOAuthAccount();
	else
		connectOAuthAccount();
}

void TargetEditDialog::connectOAuthAccount()
{
	const TargetAuthMode authMode = targetAuthModeFromString(authMode_->currentData().toString());
	if (authMode != TargetAuthMode::TwitchOAuth && authMode != TargetAuthMode::YouTubeOAuth &&
	    authMode != TargetAuthMode::KickOAuth) {
		authStatus_->setText(text("TargetEdit.AuthSelectSupported",
					  "Select a supported account login first."));
		return;
	}
	if (isPublisherManagedAuth(authMode)) {
		const QString providerName = authMode == TargetAuthMode::TwitchOAuth ? QStringLiteral("Twitch")
									       : QStringLiteral("Kick");
		authStatus_->setText(hasConnectedOAuthAccount(authMode)
					     ? text("TargetEdit.AuthOpeningExisting",
						    "Opening %1 login. The saved connection stays unchanged unless the new login succeeds and you press Save.")
						       .arg(providerName)
					     : text("TargetEdit.AuthOpeningNew",
						    "Opening %1 login in your browser...")
						       .arg(providerName));
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
			authStatus_->setText(text(
				"TargetEdit.AuthSecretTooLarge",
				"OAuth Client Secret is too large. Paste only the client_secret value, or paste the Google OAuth JSON so DSK can extract it."));
			oauthClientSecret_->setFocus();
			return;
		}
		credentials = {oauthFields.clientId, oauthFields.clientSecret};
		if (!credentials.isComplete()) {
			authStatus_->setText(text(
				"TargetEdit.AuthCustomCredentialsRequired",
				"Google Client ID and Client Secret are both required for a custom OAuth app. Redirect URI: http://localhost:17371/callback."));
			(credentials.clientId.isEmpty() ? oauthClientId_ : oauthClientSecret_)->setFocus();
			return;
		}
	} else {
		credentials = oauthBundledClientCredentials(TargetAuthMode::YouTubeOAuth);
		if (!credentials.isComplete()) {
			authStatus_->setText(text(
				"TargetEdit.AuthNoBundledYouTube",
				"This DSK build does not contain a YouTube OAuth application. Enable the custom Google OAuth app option and enter its credentials."));
			return;
		}
	}

	authStatus_->setText(hasConnectedOAuthAccount(authMode)
				     ? text("TargetEdit.AuthOpeningYouTubeExisting",
					    "Opening YouTube login. The saved connection stays unchanged unless the new login succeeds and you press Save.")
				     : text("TargetEdit.AuthOpeningYouTubeNew",
					    "Opening YouTube login in your browser..."));
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
	if (authMode == TargetAuthMode::YouTubeOAuth)
		resetYouTubeStreamOptions();
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
					       ? text("TargetEdit.ConnectTwitchKeyPlaceholder",
						      "Connect Twitch to retrieve the stream key")
					       : text("TargetEdit.ConnectKickKeyPlaceholder",
						      "Connect Kick to retrieve the stream key"));
	}

	updatePlatformHint();
	if (authMode == TargetAuthMode::TwitchOAuth)
		authStatus_->setText(text(
			"TargetEdit.AuthDisconnectedTwitch",
			"Twitch was disconnected in this editor. Press Save to remove the saved key, or Cancel to keep it."));
	else if (authMode == TargetAuthMode::KickOAuth)
		authStatus_->setText(text(
			"TargetEdit.AuthDisconnectedKick",
			"Kick was disconnected in this editor. Press Save to remove the saved key, or Cancel to keep it."));
	else
		authStatus_->setText(text(
			"TargetEdit.AuthDisconnectedYouTube",
			"YouTube was disconnected in this editor. Press Save to remove the saved authorization, or Cancel to keep it."));
}

void TargetEditDialog::handleOAuthFinished(const OAuthConnectionResult &result)
{
	const TargetAuthMode currentAuthMode = targetAuthModeFromString(authMode_->currentData().toString());
	const QString currentPlatformId = platform_->currentData().toString();
	if (result.authMode != currentAuthMode || !platformSupportsAuthMode(currentPlatformId, result.authMode)) {
		updatePlatformHint();
		authStatus_->setText(text(
			"TargetEdit.AuthIgnoredResult",
			"An OAuth result was ignored because the selected platform or login mode changed."));
		return;
	}
	if (!result.errorMessage.isEmpty()) {
		const bool preserved = hasConnectedOAuthAccount(result.authMode);
		updatePlatformHint();
		authStatus_->setText(preserved
					     ? text("TargetEdit.AuthReconnectFailed",
						    "Reconnect failed; the saved connection was not changed. %1")
						       .arg(result.errorMessage)
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
	if (result.authMode == TargetAuthMode::YouTubeOAuth) {
		const bool preserveExistingKey = !lineTextOrEmpty(streamKey_).trimmed().isEmpty() ||
						 !authCredentialRef_.trimmed().isEmpty();
		setYouTubeStreamOptions(result.youtubeStreams, preserveExistingKey);
	}
	if (name_->text().trimmed().isEmpty() ||
	    name_->text().trimmed() == text("TargetEdit.NewTarget", "New Target"))
		name_->setText(result.accountName.isEmpty()
				       ? platformDisplayName(
						 platforms_.presetById(platform_->currentData().toString()))
				       : result.accountName);
	if (enabled_)
		enabled_->setChecked(true);
	if (startWithAll_)
		startWithAll_->setChecked(true);
	updatePlatformHint();
	const bool hasStreamKey = !lineTextOrEmpty(streamKey_).trimmed().isEmpty() || !authCredentialRef_.trimmed().isEmpty();
	if (result.authMode == TargetAuthMode::YouTubeOAuth && !hasStreamKey) {
		QString message = result.youtubeStreams.isEmpty()
				  ? text("TargetEdit.AuthConnectedNoStreams",
					 "Connected as %1. No reusable RTMP streams were returned; enter a YouTube stream key manually, then Save.")
					    .arg(result.accountName)
				  : text("TargetEdit.AuthConnectedChooseStream",
					 "Connected as %1. Choose a reusable YouTube stream or enter a key manually, then Save.")
					    .arg(result.accountName);
		if (!result.youtubeStreamLookupWarning.isEmpty())
			message += QStringLiteral(" ") + result.youtubeStreamLookupWarning;
		authStatus_->setText(message);
		return;
	}

	QString message = result.authMode == TargetAuthMode::YouTubeOAuth && !result.youtubeStreams.isEmpty()
				  ? text("TargetEdit.AuthConnectedStreamsLoaded",
					 "Connected as %1. %2 reusable YouTube stream(s) loaded; review the selection, then press Save.")
					    .arg(result.accountName)
					    .arg(result.youtubeStreams.size())
				  : text("TargetEdit.AuthConnectedReview",
					 "Connected as %1. Review the settings, then press Save.")
					    .arg(result.accountName);
	if (!result.youtubeStreamLookupWarning.isEmpty())
		message += QStringLiteral(" ") + result.youtubeStreamLookupWarning;
	authStatus_->setText(message);
}

void TargetEditDialog::resetYouTubeStreamOptions()
{
	youtubeStreams_.clear();
	if (!youtubeStream_)
		return;
	const QSignalBlocker blocker(youtubeStream_);
	youtubeStream_->clear();
	youtubeStream_->addItem(text("TargetEdit.YouTubeManualKey",
				     "Manual key (including single-use streams)"),
			       -1);
	youtubeStream_->setCurrentIndex(0);
}

void TargetEditDialog::setYouTubeStreamOptions(QVector<YouTubeStreamOption> streams, bool preserveExistingKey)
{
	const QString existingKey = lineTextOrEmpty(streamKey_);
	resetYouTubeStreamOptions();
	youtubeStreams_ = streams;
	int matchingIndex = -1;
	{
		const QSignalBlocker blocker(youtubeStream_);
		for (int i = 0; i < youtubeStreams_.size(); ++i) {
			const YouTubeStreamOption &stream = youtubeStreams_.at(i);
			youtubeStream_->addItem(youtubeStreamOptionLabel(stream), i);
			if (!existingKey.isEmpty() && stream.streamKey == existingKey)
				matchingIndex = i + 1;
		}
		if (matchingIndex > 0)
			youtubeStream_->setCurrentIndex(matchingIndex);
		else if (!preserveExistingKey && !youtubeStreams_.isEmpty())
			youtubeStream_->setCurrentIndex(1);
		else
			youtubeStream_->setCurrentIndex(0);
	}
	if (youtubeStream_->currentIndex() > 0)
		applySelectedYouTubeStream(youtubeStream_->currentIndex());
}

void TargetEditDialog::applySelectedYouTubeStream(int index)
{
	if (index <= 0 || !youtubeStream_)
		return;
	const int streamIndex = youtubeStream_->itemData(index).toInt();
	if (streamIndex < 0 || streamIndex >= youtubeStreams_.size())
		return;
	const YouTubeStreamOption &stream = youtubeStreams_.at(streamIndex);
	serverFollowsPlatform_ = true;
	server_->setText(stream.serverUrl);
	streamKey_->setText(stream.streamKey);
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
