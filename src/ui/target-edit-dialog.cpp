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
		return QStringLiteral("é¸æŠžã—ãŸãƒ—ãƒ©ãƒƒãƒˆãƒ•ã‚©ãƒ¼ãƒ ã¨æŽ¥ç¶šæ–¹å¼ãŒä¸€è‡´ã—ã¦ã„ã¾ã›ã‚“ã€‚");
	if (message == QStringLiteral("Server URL is empty."))
		return QStringLiteral("ã‚µãƒ¼ãƒãƒ¼URLã‚’å…¥åŠ›ã—ã¦ãã ã•ã„ã€‚");
	if (message == QStringLiteral("Server URL must start with rtmp:// or rtmps://."))
		return QStringLiteral("ã‚µãƒ¼ãƒãƒ¼URLã¯ rtmp:// ã¾ãŸã¯ rtmps:// ã§å§‹ã‚ã¦ãã ã•ã„ã€‚");
	if (message == QStringLiteral("Server URL must include a host name."))
		return QStringLiteral("ã‚µãƒ¼ãƒãƒ¼URLã«ãƒ›ã‚¹ãƒˆåãŒã‚ã‚Šã¾ã›ã‚“ã€‚");
	if (message == QStringLiteral("TikTok needs the server URL shown in TikTok LIVE setup."))
		return QStringLiteral("ä»¥å‰ã®TikTokæ±Žç”¨URLã¯ä½¿ç”¨ã§ãã¾ã›ã‚“ã€‚TikTok LIVEã«è¡¨ç¤ºã•ã‚ŒãŸã‚µãƒ¼ãƒãƒ¼URLã‚’å…¥åŠ›ã—ã¦ãã ã•ã„ã€‚");
	if (message == QStringLiteral("Stream key is empty."))
		return QStringLiteral("ã‚¹ãƒˆãƒªãƒ¼ãƒ ã‚­ãƒ¼ã‚’å…¥åŠ›ã—ã¦ãã ã•ã„ã€‚");
	if (message == QStringLiteral("Reconnect settings are invalid."))
		return QStringLiteral("å†æŽ¥ç¶šè¨­å®šã®å€¤ã‚’ç¢ºèªã—ã¦ãã ã•ã„ã€‚");
	if (message == QStringLiteral("Encoder settings are outside the supported range."))
		return QStringLiteral("ã‚¨ãƒ³ã‚³ãƒ¼ãƒ€ãƒ¼è¨­å®šãŒå¯¾å¿œç¯„å›²å¤–ã§ã™ã€‚");
	if (message == QStringLiteral("Fixed scene mode needs an OBS scene."))
		return QStringLiteral("å›ºå®šã‚·ãƒ¼ãƒ³ã‚’ä½¿ç”¨ã™ã‚‹å ´åˆã¯OBSã‚·ãƒ¼ãƒ³ã‚’æŒ‡å®šã—ã¦ãã ã•ã„ã€‚");
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
	connect(reconnecçÍ8¶‰žËkºwµçI•Ñ‘¥Ñ¥…±½œèéÕÍ•AÉ•Í•ÑM•ÉÙ•È ¤)ì(%¥˜€ …Í•ÉÙ•É|ñð€…¹…µ•|ñð€…•¹½‘•ÉÉ½ÕÁ|¤($%É•ÑÕÉ¸ì((%½¹ÍÐA±…Ñ™½ÉµAÉ•Í•ÐÁÉ•Í•Ð€ôÁ±…Ñ™½ÉµÍ|¹ÁÉ•Í•Ñ	å%¡Á±…Ñ™½Éµ|´ùÕÉÉ•¹Ñ…Ñ„ ¤¹Ñ½MÑÉ¥¹œ ¤¤ì(%Í•ÉÙ•É½±±½ÝÍA±…Ñ™½Éµ|€ôÑÉÕ”ì(%Í•ÉÙ•É|´ùÍ•ÑQ•áÐ¡ÁÉ•Í•Ð¹‘•™…Õ±ÑM•ÉÙ•È¤ì(%¥˜€¡¹…µ•|´ùÑ•áÐ ¤¹ÑÉ¥µµ• ¤¹¥ÍµÁÑä ¤¤($%¹…µ•|´ùÍ•ÑQ•áÐ¡ÁÉ•Í•Ð¹‘¥ÍÁ±…å9…µ”¤ì(%¥˜€¡ÁÉ•Í•Ð¹Ù•ÉÑ¥…±½µµ½¸¤ì($%½¹ÍÐ¥¹ÐÙ•ÉÑ¥…±%¹‘•à€ô•¹½‘•ÉÉ½ÕÁ|´ù™¥¹‘…Ñ„¡•¹½‘•ÉÉ½ÕÁQ½MÑÉ¥¹œ¡¹½‘•ÉÉ½ÕÀèéÍ­Y•ÉÑ¥…°¤¤ì($%¥˜€¡Ù•ÉÑ¥…±%¹‘•à€øô€À¤($$%•¹½‘•ÉÉ½ÕÁ|´ùÍ•ÑÕÉÉ•¹Ñ%¹‘•à¡Ù•ÉÑ¥…±%¹‘•à¤ì(%ô)ô()Ù½¥Q…É•Ñ‘¥Ñ¥…±½œèéÕÁ‘…Ñ•ÕÑ¡5½‘•Ì ¤)ì(%¥˜€ ……ÕÑ¡5½‘•|¤($%É•ÑÕÉ¸ì((%½¹ÍÐEMÑÉ¥¹œÁ±…Ñ™½Éµ%€ôÁ±…Ñ™½Éµ|´ùÕÉÉ•¹Ñ…Ñ„ ¤¹Ñ½MÑÉ¥¹œ ¤ì(%½¹ÍÐEMÑÉ¥¹œÁÉ•Ù¥½ÕÌ€ô…ÕÑ¡5½‘•|´ùÕÉÉ•¹Ñ…Ñ„ ¤¹Ñ½MÑÉ¥¹œ ¤ì(%…ÕÑ¡5½‘•|´ù‰±½­M¥¹…±Ì¡ÑÉÕ”¤ì(%…ÕÑ¡5½‘•|´ù±•…È ¤ì(%…ÕÑ¡5½‘•|´ù…‘‘%Ñ•´¡Ñ…É•ÑÕÑ¡5½‘•¥ÍÁ±…å9…µ”¡Q…É•ÑÕÑ¡5½‘”èé5…¹Õ…±IÑµÀ¤°Ñ…É•ÑÕÑ¡5½‘•Q½MÑÉ¥¹œ¡Q…É•ÑÕÑ¡5½‘”èé5…¹Õ…±IÑµÀ¤¤ì(%¥˜€¡Á±…Ñ™½ÉµMÕÁÁ½ÉÑÍÕÑ¡5½‘”¡Á±…Ñ™½Éµ%°Q…É•ÑÕÑ¡5½‘”èéQÝ¥Ñ¡=ÕÑ ¤¤($%…ÕÑ¡5½‘•|´ù…‘‘%Ñ•´¡Ñ…É•ÑÕÑ¡5½‘•¥ÍÁ±…å9…µ”¡Q…É•ÑÕÑ¡5½‘”èéQÝ¥Ñ¡=ÕÑ ¤°Ñ…É•ÑÕÑ¡5½‘•Q½MÑÉ¥¹œ¡Q…É•ÑÕÑ¡5½‘”èéQÝ¥Ñ¡=ÕÑ ¤¤ì(%¥˜€¡Á±…Ñ™½ÉµMÕÁÁ½ÉÑÍÕÑ¡5½‘”¡Á±…Ñ™½Éµ%°Q…É•ÑÕÑ¡5½‘”èée½ÕQÕ‰•=ÕÑ ¤¤($%…ÕÑ¡5½‘•|´ù…‘‘%Ñ•´¡Ñ…É•ÑÕÑ¡5½‘•¥ÍÁ±…å9…µ”¡Q…É•ÑÕÑ¡5½‘”èée½ÕQÕ‰•=ÕÑ ¤°Ñ…É•ÑÕÑ¡5½‘•Q½MÑÉ¥¹œ¡Q…É•ÑÕÑ¡5½‘”èée½ÕQÕ‰•=ÕÑ ¤¤ì(%¥˜€¡Á±…Ñ™½ÉµMÕÁÁ½ÉÑÍÕÑ¡5½‘”¡Á±…Ñ™½Éµ%°Q…É•ÑÕÑ¡5½‘”èé-¥­=ÕÑ ¤¤($%…ÕÑ¡5½‘•|´ù…‘‘%Ñ•´¡Ñ…É•ÑÕÑ¡5½‘•¥ÍÁ±…å9…µ”¡Q…É•ÑÕÑ¡5½‘”èé-¥­=ÕÑ ¤°Ñ…É•ÑÕÑ¡5½‘•Q½MÑÉ¥¹œ¡Q…É•ÑÕÑ¡5½‘”èé-¥­=ÕÑ ¤¤ì(%½¹ÍÐ¥¹ÐÁÉ•Ù¥½ÕÍ%¹‘•à€ô…ÕÑ¡5½‘•|´ù™¥¹‘…Ñ„¡ÁÉ•Ù¥½ÕÌ¤ì(%…ÕÑ¡5½‘•|´ùÍ•ÑÕÉÉ•¹Ñ%¹‘•à¡ÁÉ•Ù¥½ÕÍ%¹‘•à€øô€À€üÁÉ•Ù¥½ÕÍ%¹‘•à€è€À¤ì(%…ÕÑ¡5½‘•|´ù‰±½­M¥¹…±Ì¡™…±Í”¤ì(%…ÕÑ¡5½‘•Y…±Õ•|€ô½µ‰½…Ñ…=È¡…ÕÑ¡5½‘•|°Ñ…É•ÑÕÑ¡5½‘•Q½MÑÉ¥¹œ¡Q…É•ÑÕÑ¡5½‘”èé5…¹Õ…±IÑµÀ¤¤ì)ô()Ù½¥Q…É•Ñ‘¥Ñ¥…±½œèéÕÁ‘…Ñ•A±…Ñ™½Éµ!¥¹Ð ¤)ì(%¥˜€ …Á±…Ñ™½Éµ!¥¹Ñ|¤($%É•ÑÕÉ¸ì((%½¹ÍÐA±…Ñ™½ÉµAÉ•Í•ÐÁÉ•Í•Ð€ôÁ±…Ñ™½ÉµÍ|¹ÁÉ•Í•Ñ	å%¡Á±…Ñ™½Éµ|´ùÕÉÉ•¹Ñ…Ñ„ ¤¹Ñ½MÑÉ¥¹œ ¤¤ì(%½¹ÍÐQ…É•ÑÕÑ¡5½‘”…ÕÑ¡5½‘”€ôÑ…É•ÑÕÑ¡5½‘•É½µMÑÉ¥¹œ¡…ÕÑ¡5½‘•|€ü…ÕÑ¡5½‘•|´ùÕÉÉ•¹Ñ…Ñ„ ¤¹Ñ½MÑÉ¥¹œ ¤€èEMÑÉ¥¹œ ¤¤ì(%½¹ÍÐ‰½½°µ…¹Õ…±M•ÉÙ•È€ôÁÉ•Í•Ð¹‘•™…Õ±ÑM•ÉÙ•È¹ÑÉ¥µµ• ¤¹¥ÍµÁÑä ¤ì(%EMÑÉ¥¹œÑ•áÐì(%¥˜€¡ÁÉ•Í•Ð¹¥€ôôEMÑÉ¥¹1¥Ñ•É…° ‰Ñ¥­Ñ½¬ˆ¤¤($%Ñ•áÐ€ôEMÑÉ¥¹1¥Ñ•É…° ‰5…¹Õ…°IQ5@½¹±ä¸A…ÍÑ”Ñ¡”Í•ÉÙ•ÈUI0…¹ÍÑÉ•…´­•äÍ¡½Ý¸¥¸Q¥­Q½¬1%YÍ•ÑÕÀ¸ˆ¤ì(%•±Í”¥˜€¡µ…¹Õ…±M•ÉÙ•È¤($%Ñ•áÐ€ôEMÑÉ¥¹1¥Ñ•É…° ‰A…ÍÑ”Ñ¡”IQ5@½ÈIQ5ALÍ•ÉÙ•ÈUI0ÍÕÁÁ±¥•‰äÑ¡”Á±…Ñ™½É´¸ˆ¤ì(%•±Í”($%Ñ•áÐ€ôEMÑÉ¥¹œ ‰M•ÉÙ•ÈÁÉ•Í•Ðè€”Äˆ¤¹…Éœ¡ÁÉ•Í•Ð¹‘•™…Õ±ÑM•ÉÙ•È¤ì(%½¹ÍÐ¥¹ÐÉ•½µµ•¹‘•‘	¥ÑÉ…Ñ”€ôÁÉ•Í•Ð¹É•½µµ•¹‘•‘=ÕÑÁÕÐ€ôôEMÑÉ¥¹1¥Ñ•É…° ‰‘Í¬µÙ•ÉÑ¥…°ˆ¤($$$$$€üÁÉ•Í•Ð¹Ù•ÉÑ¥…±	¥ÑÉ…Ñ•-‰ÁÌ($$$$$€èÁÉ•Í•Ð¹¡½É¥é½¹Ñ…±	¥ÑÉ…Ñ•-‰ÁÌì(%Ñ•áÐ€¬ôEMÑÉ¥¹œ ˆI•½µµ•¹‘•è€”Ä…Ð€”È­‰ÁÌ¸ˆ¤($$$¹…Éœ¡½ÕÑÁÕÑ¥ÍÁ±…å9…µ”¡ÁÉ•Í•Ð¹É•½µµ•¹‘•‘=ÕÑÁÕÐ¤¤($$$¹…Éœ¡É•½µµ•¹‘•‘	¥ÑÉ…Ñ”¤ì(%¥˜€ …ÁÉ•Í•Ð¹¹½Ñ”¹¥ÍµÁÑä ¤¤($%Ñ•áÐ€¬ôEMÑÉ¥¹œ ˆ€”Äˆ¤¹…Éœ¡ÁÉ•Í•Ð¹¹½Ñ”¤ì(%Á±…Ñ™½Éµ!¥¹Ñ|´ùÍ•ÑQ•áÐ¡Ñ•áÐ¤ì(%¥˜€¡ÁÉ•Í•ÑM•ÉÙ•É	ÕÑÑ½¹|¤ì($%ÁÉ•Í•ÑM•ÉÙ•É	ÕÑÑ½¹|´ùÍ•Ñ¹…‰±• …µ…¹Õ…±M•ÉÙ•È¤ì($%ÁÉ•Í•ÑM•ÉÙ•É	ÕÑÑ½¹|´ùÍ•ÑQ½½±Q¥À¡µ…¹Õ…±M•ÉÙ•È€üEMÑÉ¥¹1¥Ñ•É…° ‰Q¡¥ÌÁ±…Ñ™½É´‘½•Ì¹½Ð¡…Ù”„Í…™”‰Õ¥±Ðµ¥¸Í•ÉÙ•ÈUI0¸ˆ¤($$$$$$$$€èEMÑÉ¥¹1¥Ñ•É…° ‰I•ÍÑ½É”Ñ¡”‰Õ¥±Ðµ¥¸Í•ÉÙ•ÈUI0¸ˆ¤¤ì(%ô(%¥˜€¡ÍÑÉ•…µ-•å|¤ì($%¥˜€¡ÍÑÉ•…µ-•å|´ùÑ•áÐ ¤¹ÑÉ¥µµ• ¤¹¥ÍµÁÑä ¤€˜˜€……ÕÑ¡É•‘•¹Ñ¥…±I•™|¹ÑÉ¥µµ• ¤¹¥ÍµÁÑä ¤¤($$%ÍÑÉ•…µ-•å|´ùÍ•ÑA±…•¡½±‘•ÉQ•áÐ¡EMÑÉ¥¹1¥Ñ•É…° ‰M…Ù•ÍÑÉ•…´­•ä•á¥ÍÑÌ€´±•…Ù”‰±…¹¬Ñ¼­••Àˆ¤¤ì($%•±Í”¥˜€¡ÁÉ•Í•Ð¹¥€ôôEMÑÉ¥¹1¥Ñ•É…° ‰Ñ¥­Ñ½¬ˆ¤¤($$%ÍÑÉ•…µ-•å|´ùÍ•ÑA±…•¡½±‘•ÉQ•áÐ¡EMÑÉ¥¹1¥Ñ•É…° ‰A…ÍÑ”Q¥­Q½¬ÍÑÉ•…´­•äˆ¤¤ì($%•±Í”($$%ÍÑÉ•…µ-•å|´ùÍ•ÑA±…•¡½±‘•ÉQ•áÐ¡EMÑÉ¥¹1¥Ñ•É…° ‰¹Ñ•ÈÍÑÉ•…´­•äˆ¤¤ì(%ô((%¥˜€ ……ÕÑ¡MÑ…ÑÕÍ|¤($%É•ÑÕÉ¸ì((%½¹ÍÐ‰½½°½¹¹•Ñ•€ô¡…Í½¹¹•Ñ•‘=ÕÑ¡½Õ¹Ð¡…ÕÑ¡5½‘”¤ì(%½¹ÍÐ‰½½°±½¥¹IÕ¹¹¥¹œ€ô½…ÕÑ¡½¹¹•Ñ½É|´ù¥ÍIÕ¹¹¥¹œ ¤ì(%½¹ÍÐ‰½½°¡…Í	Õ¹‘±•‘e½ÕQÕ‰•=ÕÑ €ô½…ÕÑ¡!…Í	Õ¹‘±•‘±¥•¹ÑÉ•‘•¹Ñ¥…±Ì¡Q…É•ÑÕÑ¡5½‘”èée½ÕQÕ‰•=ÕÑ ¤ì(%½¹ÍÐ‰½½°ÕÍ•ÕÍÑ½µe½ÕQÕ‰•=ÕÑ €ô…ÕÑ¡5½‘”€ôôQ…É•ÑÕÑ¡5½‘”èée½ÕQÕ‰•=ÕÑ €˜˜($$ …¡…Í	Õ¹‘±•‘e½ÕQÕ‰•=ÕÑ ñð¡•­•‘=È¡ÕÍ•ÕÍÑ½µ=ÕÑ¡ÁÁ|°™…±Í”¤¤ì(%¥˜€¡±½¥¹IÕ¹¹¥¹œ€˜˜…ÕÑ¡5½‘”€ôôQ…É•ÑÕÑ¡5½‘”èéQÝ¥Ñ¡=ÕÑ ¤ì($%…ÕÑ¡MÑ…ÑÕÍ|´ùÍ•ÑQ•áÐ ‰]…¥Ñ¥¹œ™½ÈQÝ¥Ñ ±½¥¸¸Q¡”Í…Ù•½¹¹•Ñ¥½¸É•µ…¥¹ÌÕ¹¡…¹•Õ¹Ñ¥°Ñ¡¥Ì±½¥¸ÍÕ••‘Ì…¹å½ÔÁÉ•ÍÌM…Ù”¸ˆ¤ì(%ô•±Í”¥˜€¡±½¥¹IÕ¹¹¥¹œ€˜˜…ÕÑ¡5½‘”€ôôQ…É•ÑÕÑ¡5½‘”èée½ÕQÕ‰•=ÕÑ ¤ì($%…ÕÑ¡MÑ…ÑÕÍ|´ùÍ•ÑQ•áÐ ‰]…¥Ñ¥¹œ™½Èe½ÕQÕ‰”±½¥¸¸Q¡”Í…Ù•½¹¹•Ñ¥½¸É•µ…¥¹ÌÕ¹¡…¹•Õ¹Ñ¥°Ñ¡¥Ì±½¥¸ÍÕ••‘Ì…¹å½ÔÁÉ•ÍÌM…Ù”¸ˆ¤ì(%ô•±Í”¥˜€¡±½¥¹IÕ¹¹¥¹œ€˜˜…ÕÑ¡5½‘”€ôôQ…É•ÑÕÑ¡5½‘”èé-¥­=ÕÑ ¤ì($%…ÕÑ¡MÑ…ÑÕÍ|´ùÍ•ÑQ•áÐ ‰]…¥Ñ¥¹œ™½È-¥¬±½¥¸¸Q¡”Í…Ù•½¹¹•Ñ¥½¸É•µ…¥¹ÌÕ¹¡…¹•Õ¹Ñ¥°Ñ¡¥Ì±½¥¸ÍÕ••‘Ì…¹å½ÔÁÉ•ÍÌM…Ù”¸ˆ¤ì(%ô•±Í”¥˜€¡…ÕÑ¡5½‘”€ôôQ…É•ÑÕÑ¡5½‘”èéQÝ¥Ñ¡=ÕÑ ¤ì($%¥˜€¡½¹¹•Ñ•¤($$%…ÕÑ¡MÑ…ÑÕÍ|´ùÍ•ÑQ•áÐ¡EMÑÉ¥¹œ ‰½¹¹•Ñ•…Ì€”Ä¸I•½¹¹•ÐÉ•™É•Í¡•ÌÑ¡”Í…Ù•QÝ¥Ñ ÍÑÉ•…´­•ä¸ˆ¤($$$$$€€€€€¹…Éœ¡…ÕÑ¡½Õ¹Ñ9…µ•|¹¥ÍµÁÑä ¤€üEMÑÉ¥¹1¥Ñ•É…° ‰QÝ¥Ñ ˆ¤€è…ÕÑ¡½Õ¹Ñ9…µ•|¤¤ì($%•±Í”($$%…ÕÑ¡MÑ…ÑÕÍ|´ùÍ•ÑQ•áÐ ‰AÉ•ÍÌ½¹¹•ÐQÝ¥Ñ ¸M,Í¥¹Ì¥¸Ñ¡É½Õ ¥ÑÌÁÕ‰±¥Í¡•ÈÍ•ÉÙ¥”…¹É•ÑÉ¥•Ù•ÌÑ¡¥Ì…½Õ¹ÐÌÍÑÉ•…´­•ä…ÕÑ½µ…Ñ¥…±±ä¸ˆ¤ì(%ô•±Í”¥˜€¡…ÕÑ¡5½‘”€ôôQ…É•ÑÕÑ¡5½‘”èée½ÕQÕ‰•=ÕÑ ¤ì($%¥˜€¡½¹¹•Ñ•¤($$%…ÕÑ¡MÑ…ÑÕÍ|´ùÍ•ÑQ•áÐ¡EMÑÉ¥¹œ ‰½¹¹•Ñ•…Ì€”Ä¸I•½¹¹•ÐÉ•Á±…•ÌÑ¡”Í…Ù•e½ÕQÕ‰”…ÕÑ¡½É¥é…Ñ¥½¸¸ˆ¤($$$$$€€€€€¹…Éœ¡…ÕÑ¡½Õ¹Ñ9…µ•|¹¥ÍµÁÑä ¤€üEMÑÉ¥¹1¥Ñ•É…° ‰e½ÕQÕ‰”ˆ¤€è…ÕÑ¡½Õ¹Ñ9…µ•|¤¤ì($%•±Í”¥˜€ …ÕÍ•ÕÍÑ½µe½ÕQÕ‰•=ÕÑ ¤($$%…ÕÑ¡MÑ…ÑÕÍ|´ùÍ•ÑQ•áÐ ‰AÉ•ÍÌ½¹¹•Ðe½ÕQÕ‰”¸M,ÕÍ•Ì¥ÑÌ‰Õ¹‘±•‘•Í­Ñ½À=ÕÑ …ÁÀì¹¼½½±”‘•Ù•±½Á•ÈÉ•‘•¹Ñ¥…±Ì…É”É•ÅÕ¥É•¸¹Ñ•È½È­••ÀÑ¡”e½ÕQÕ‰”ÍÑÉ•…´­•ä‰•±½Ü¸ˆ¤ì($%•±Í”($$%…ÕÑ¡MÑ…ÑÕÍ|´ùÍ•ÑQ•áÐ ‰¹Ñ•È„½½±”=ÕÑ ±¥•¹Ð%…¹±¥•¹ÐM•É•Ð°Ñ¡•¸ÁÉ•ÍÌ½¹¹•Ðe½ÕQÕ‰”¸M,ÕÍ•ÌÑ¡¥Ì±½¥¸Ñ¼ÍÑ…ÉÐe½ÕQÕ‰”1¥Ù”…™Ñ•ÈÑ¡”IQ5@Í¥¹…°¥Ì…Ñ¥Ù”¸I•‘¥É•ÐUI$è¡ÑÑÀè¼½±½…±¡½ÍÐèÄÜÌÜÄ½…±±‰…¬ˆ¤ì(%ô•±Í”¥˜€¡…ÕÑ¡5½‘”€ôôQ…É•ÑÕÑ¡5½‘”èé-¥­=ÕÑ ¤ì($%¥˜€¡½¹¹•Ñ•¤($$%…ÕÑ¡MÑ…ÑÕÍ|´ùÍ•ÑQ•áÐ¡EMÑÉ¥¹œ ‰½¹¹•Ñ•…Ì€”Ä¸I•½¹¹•ÐÉ•™É•Í¡•ÌÑ¡”Í…Ù•-¥¬ÍÑÉ•…´UI0…¹­•ä¸ˆ¤($$$$$€€€€€¹…Éœ¡…ÕÑ¡½Õ¹Ñ9…µ•|¹¥ÍµÁÑä ¤€üEMÑÉ¥¹1¥Ñ•É…° ‰-¥¬ˆ¤€è…ÕÑ¡½Õ¹Ñ9…µ•|¤¤ì($%•±Í”($$%…ÕÑ¡MÑ…ÑÕÍ|´ùÍ•ÑQ•áÐ ‰AÉ•ÍÌ½¹¹•Ð-¥¬¸M,Í¥¹Ì¥¸Ñ¡É½Õ ¥ÑÌÁÕ‰±¥Í¡•ÈÍ•ÉÙ¥”…¹É•ÑÉ¥•Ù•ÌÑ¡¥Ì…½Õ¹ÐÌÍÑÉ•…´UI0…¹­•ä…ÕÑ½µ…Ñ¥…±±ä¸ˆ¤ì(%ô•±Í”($%…ÕÑ¡MÑ…ÑÕÍ|´ùÍ•ÑQ•áÐ ‰5…¹Õ…°IQ5@ÕÍ•ÌÑ¡”Í•ÉÙ•ÈUI0…¹ÍÑÉ•…´­•ä•¹Ñ•É•‰•±½Ü¸e½ÕQÕ‰”µ…¹Õ…°IQ5@Í•¹‘ÌÍ¥¹…°½¹±äìÍÑ…ÉÐÑ¡”‰É½…‘…ÍÐ¥¸e½ÕQÕ‰”MÑÕ‘¥¼½ÈÍÝ¥Ñ Ñ¼1½¥¸Ý¥Ñ e½ÕQÕ‰”¸ˆ¤ì((%½¹ÍÐ‰½½°½…ÕÑ¡5½‘”€ô…ÕÑ¡5½‘”€ôôQ…É•ÑÕÑ¡5½‘”èéQÝ¥Ñ¡=ÕÑ ñð…ÕÑ¡5½‘”€ôôQ…É•ÑÕÑ¡5½‘”èée½ÕQÕ‰•=ÕÑ ñð($$$€€€€€€…ÕÑ¡5½‘”€ôôQ…É•ÑÕÑ¡5½‘”èé-¥­=ÕÑ ì(%¥˜€¡Á±…Ñ™½Éµ|¤($%Á±…Ñ™½Éµ|´ùÍ•Ñ¹…‰±• …±½¥¹IÕ¹¹¥¹œ¤ì(%¥˜€¡…ÕÑ¡5½‘•|¤($%…ÕÑ¡5½‘•|´ùÍ•Ñ¹…‰±• …±½¥¹IÕ¹¹¥¹œ¤ì(%¥˜€¡ÕÍ•ÕÍÑ½µ=ÕÑ¡ÁÁ|¤ì($%ÕÍ•ÕÍÑ½µ=ÕÑ¡ÁÁ|´ùÍ•ÑY¥Í¥‰±”¡…ÕÑ¡5½‘”€ôôQ…É•ÑÕÑ¡5½‘”èée½ÕQÕ‰•=ÕÑ €˜˜¡…Í	Õ¹‘±•‘e½ÕQÕ‰•=ÕÑ ¤ì($%ÕÍ•ÕÍÑ½µ=ÕÑ¡ÁÁ|´ùÍ•Ñ¹…‰±• …±½¥¹IÕ¹¹¥¹œ¤ì(%ô(%½¹ÍÐ‰½½°Í¡½Ý•Ù•±½Á•ÉÉ•‘•¹Ñ¥…±Ì€ôÕÍ•ÕÍÑ½µe½ÕQÕ‰•=ÕÑ ì(%¥˜€¡½…ÕÑ¡±¥•¹Ñ%‘|¤ì($%½…ÕÑ¡±¥•¹Ñ%‘|´ùÍ•ÑY¥Í¥‰±”¡Í¡½Ý•Ù•±½Á•ÉÉ•‘•¹Ñ¥…±Ì¤ì($%½…ÕÑ¡±¥•¹Ñ%‘|´ùÍ•Ñ¹…‰±• …±½¥¹IÕ¹¹¥¹œ¤ì(%ô(%¥˜€¡½…ÕÑ¡±¥•¹Ñ%‘1…‰•±|¤($%½…ÕÑ¡±¥•¹Ñ%‘1…‰•±|´ùÍ•ÑY¥Í¥‰±”¡Í¡½Ý•Ù•±½Á•ÉÉ•‘•¹Ñ¥…±Ì¤ì(%¥˜€¡½…ÕÑ¡±¥•¹ÑM•É•Ñ|¤ì($%½…ÕÑ¡±¥•¹ÑM•É•Ñ|´ùÍ•ÑY¥Í¥‰±”¡Í¡½Ý•Ù•±½Á•ÉÉ•‘•¹Ñ¥…±Ì¤ì($%½…ÕÑ¡±¥•¹ÑM•É•Ñ|´ùÍ•Ñ¹…‰±• …±½¥¹IÕ¹¹¥¹œ¤ì(%ô(%¥˜€¡½…ÕÑ¡±¥•¹ÑM•É•Ñ1…‰•±|¤($%½…ÕÑ¡±¥•¹ÑM•É•Ñ1…‰•±|´ùÍ•ÑY¥Í¥‰±”¡Í¡½Ý•Ù•±½Á•ÉÉ•‘•¹Ñ¥…±Ì¤ì(%¥˜€¡½¹¹•Ñ=ÕÑ¡	ÕÑÑ½¹|¤ì($%½¹¹•Ñ=ÕÑ¡	ÕÑÑ½¹|´ùÍ•Ñ¹…‰±•¡½…ÕÑ¡5½‘”€˜˜€…±½¥¹IÕ¹¹¥¹œ¤ì($%¥˜€¡±½¥¹IÕ¹¹¥¹œ¤($$%½¹¹•Ñ=ÕÑ¡	ÕÑÑ½¹|´ùÍ•ÑQ•áÐ ‰]…¥Ñ¥¹œ¸¸¸ˆ¤ì($%•±Í”¥˜€¡…ÕÑ¡5½‘”€ôôQ…É•ÑÕÑ¡5½‘”èéQÝ¥Ñ¡=ÕÑ ¤($$%½¹¹•Ñ=ÕÑ¡	ÕÑÑ½¹|´ùÍ•ÑQ•áÐ¡½¹¹•Ñ•€ü€‰I•½¹¹•ÐQÝ¥Ñ ˆ€è€‰½¹¹•ÐQÝ¥Ñ ˆ¤ì($%•±Í”¥˜€¡…ÕÑ¡5½‘”€ôôQ…É•ÑÕÑ¡5½‘”èée½ÕQÕ‰•=ÕÑ ¤($$%½¹¹•Ñ=ÕÑ¡	ÕÑÑ½¹|´ùÍ•ÑQ•áÐ¡½¹¹•Ñ•€ü€‰I•½¹¹•Ðe½ÕQÕ‰”ˆ€è€‰½¹¹•Ðe½ÕQÕ‰”ˆ¤ì($%•±Í”¥˜€¡…ÕÑ¡5½‘”€ôôQ…É•ÑÕÑ¡5½‘”èé-¥­=ÕÑ ¤($$%½¹¹•Ñ=ÕÑ¡	ÕÑÑ½¹|´ùÍ•ÑQ•áÐ¡½¹¹•Ñ•€ü€‰I•½¹¹•Ð-¥¬ˆ€è€‰½¹¹•Ð-¥¬ˆ¤ì($%•±Í”($$%½¹¹•Ñ=ÕÑ¡	ÕÑÑ½¹|´ùÍ•ÑQ•áÐ ‰½¹¹•Ðˆ¤ì(%ô(%¥˜€¡‘¥Í½¹¹•Ñ=ÕÑ¡	ÕÑÑ½¹|¤ì($%‘¥Í½¹¹•Ñ=ÕÑ¡	ÕÑÑ½¹|´ùÍ•ÑY¥Í¥‰±”¡½…ÕÑ¡5½‘”€˜˜½¹¹•Ñ•¤ì($%‘¥Í½¹¹•Ñ=ÕÑ¡	ÕÑÑ½¹|´ùÍ•Ñ¹…‰±• …±½¥¹IÕ¹¹¥¹œ¤ì(%ô)ô()Ù½¥Q…É•Ñ‘¥Ñ¥…±½œèéÕÁ‘…Ñ•MÑÉ•…µ-•åY¥Í¥‰¥±¥Ñä¡‰½½°Í¡½Ü¤)ì(%ÍÑÉ•…µ-•å|´ùÍ•Ñ¡½5½‘”¡Í¡½Ü€üE1¥¹•‘¥Ðèé9½Éµ…°€èE1¥¹•‘¥ÐèéA…ÍÍÝ½É¤ì)ô()Ù½¥Q…É•Ñ‘¥Ñ¥…±½œèé½¹¹•Ñ=ÕÑ¡½Õ¹Ð ¤)ì(%½¹ÍÐQ…É•ÑÕÑ¡5½‘”…ÕÑ¡5½‘”€ôÑ…É•ÑÕÑ¡5½‘•É½µMÑÉ¥¹œ¡…ÕÑ¡5½‘•|´ùÕÉÉ•¹Ñ…Ñ„ ¤¹Ñ½MÑÉ¥¹œ ¤¤ì(%¥˜€¡…ÕÑ¡5½‘”€„ôQ…É•ÑÕÑ¡5½‘”èéQÝ¥Ñ¡=ÕÑ €˜˜…ÕÑ¡5½‘”€„ôQ…É•ÑÕÑ¡5½‘”èée½ÕQÕ‰•=ÕÑ €˜˜($€€€…ÕÑ¡5½‘”€„ôQ…É•ÑÕÑ¡5½‘”èé-¥­=ÕÑ ¤ì($%…ÕÑ¡MÑ…ÑÕÍ|´ùÍ•ÑQ•áÐ ‰M•±•Ð„ÍÕÁÁ½ÉÑ•…½Õ¹Ð±½¥¸™¥ÉÍÐ¸ˆ¤ì($%É•ÑÕÉ¸ì(%ô(%¥˜€¡¥ÍAÕ‰±¥Í¡•É5…¹…•‘ÕÑ ¡…ÕÑ¡5½‘”¤¤ì($%½¹ÍÐEMÑÉ¥¹œÁÉ½Ù¥‘•É9…µ”€ô…ÕÑ¡5½‘”€ôôQ…É•ÑÕÑ¡5½‘”èéQÝ¥Ñ¡=ÕÑ €üEMÑÉ¥¹1¥Ñ•É…° ‰QÝ¥Ñ ˆ¤($$$$$$$$$€€€€€€€èEMÑÉ¥¹1¥Ñ•É…° ‰-¥¬ˆ¤ì($%…ÕÑ¡MÑ…ÑÕÍ|´ùÍ•ÑQ•áÐ¡¡…Í½¹¹•Ñ•‘=ÕÑ¡½Õ¹Ð¡…ÕÑ¡5½‘”¤($$$$$€€€€€üEMÑÉ¥¹œ ‰=Á•¹¥¹œ€”Ä±½¥¸¸Q¡”Í…Ù•½¹¹•Ñ¥½¸ÍÑ…åÌÕ¹¡…¹•Õ¹±•ÍÌÑ¡”¹•Ü±½¥¸ÍÕ••‘Ì…¹å½ÔÁÉ•ÍÌM…Ù”¸ˆ¤¹…Éœ¡ÁÉ½Ù¥‘•É9…µ”¤($$$$$€€€€€èEMÑÉ¥¹œ ‰=Á•¹¥¹œ€”Ä±½¥¸¥¸å½ÕÈ‰É½ÝÍ•È¸¸¸ˆ¤¹…Éœ¡ÁÉ½Ù¥‘•É9…µ”¤¤ì($%¥˜€¡½…ÕÑ¡½¹¹•Ñ½É|´ù‰•¥¸¡…ÕÑ¡5½‘”°EMÑÉ¥¹œ ¤°EMÑÉ¥¹œ ¤¤¤($$%ÕÁ‘…Ñ•A±…Ñ™½Éµ!¥¹Ð ¤ì($%É•ÑÕÉ¸ì(%ô(%½¹ÍÐ‰½½°ÕÍ•ÕÍÑ½µe½ÕQÕ‰•=ÕÑ €ô€…½…ÕÑ¡!…Í	Õ¹‘±•‘±¥•¹ÑÉ•‘•¹Ñ¥…±Ì¡Q…É•ÑÕÑ¡5½‘”èée½ÕQÕ‰•=ÕÑ ¤ñð($%¡•­•‘=È¡ÕÍ•ÕÍÑ½µ=ÕÑ¡ÁÁ|°™…±Í”¤ì(%=ÕÑ¡±¥•¹ÑÉ•‘•¹Ñ¥…±ÌÉ•‘•¹Ñ¥…±Ìì(%¥˜€¡ÕÍ•ÕÍÑ½µe½ÕQÕ‰•=ÕÑ ¤ì($%½¹ÍÐ=ÕÑ¡±¥•¹Ñ¥•±‘Ì½…ÕÑ¡¥•±‘Ì€ô¹½Éµ…±¥é•=ÕÑ¡±¥•¹Ñ¥•±‘Ì¡½…ÕÑ¡±¥•¹Ñ%‘|´ùÑ•áÐ ¤°½…ÕÑ¡±¥•¹ÑM•É•Ñ|´ùÑ•áÐ ¤¤ì($%¥˜€¡½…ÕÑ¡¥•±‘Ì¹Á…ÉÍ•‘)Í½¸¤ì($$%½…ÕÑ¡±¥•¹Ñ%‘|´ùÍ•ÑQ•áÐ¡½…ÕÑ¡¥•±‘Ì¹±¥•¹Ñ%¤ì($$%½…ÕÑ¡±¥•¹ÑM•É•Ñ|´ùÍ•ÑQ•áÐ¡½…ÕÑ¡¥•±‘Ì¹±¥•¹ÑM•É•Ð¤ì($%ô($%¥˜€¡½…ÕÑ¡¥•±‘Ì¹‘É½ÁÁ•‘=Ù•ÉÍ¥é•‘M•É•Ð¤ì($$%…ÕÑ¡MÑ…ÑÕÍ|´ùÍ•ÑQ•áÐ ‰=ÕÑ ±¥•¹ÐM•É•Ð¥ÌÑ½¼±…É”¸A…ÍÑ”½¹±äÑ¡”±¥•¹Ñ}Í•É•ÐÙ…±Õ”°½ÈÁ…ÍÑ”Ñ¡”½½±”=ÕÑ )M=8Í¼M,…¸•áÑÉ…Ð¥Ð¸ˆ¤ì($$%½…ÕÑ¡±¥•¹ÑM•É•Ñ|´ùÍ•Ñ½ÕÌ ¤ì($$%É•ÑÕÉ¸ì($%ô($%É•‘•¹Ñ¥…±Ì€ôí½…ÕÑ¡¥•±‘Ì¹±¥•¹Ñ%°½…ÕÑ¡¥•±‘Ì¹±¥•¹ÑM•É•Ñôì($%¥˜€ …É•‘•¹Ñ¥…±Ì¹¥Í½µÁ±•Ñ” ¤¤ì($$%…ÕÑ¡MÑ…ÑÕÍ|´ùÍ•ÑQ•áÐ ‰½½±”±¥•¹Ð%…¹±¥•¹ÐM•É•Ð…É”‰½Ñ É•ÅÕ¥É•™½È„ÕÍÑ½´=ÕÑ …ÁÀ¸I•‘¥É•ÐUI$è¡ÑÑÀè¼½±½…±¡½ÍÐèÄÜÌÜÄ½…±±‰…¬¸ˆ¤ì($$$¡É•‘•¹Ñ¥…±Ì¹±¥•¹Ñ%¹¥ÍµÁÑä ¤€ü½…ÕÑ¡±¥•¹Ñ%‘|€è½…ÕÑ¡±¥•¹ÑM•É•Ñ|¤´ùÍ•Ñ½ÕÌ ¤ì($$%É•ÑÕÉ¸ì($%ô(%ô•±Í”ì($%É•‘•¹Ñ¥…±Ì€ô½…ÕÑ¡	Õ¹‘±•‘±¥•¹ÑÉ•‘•¹Ñ¥…±Ì¡Q…É•ÑÕÑ¡5½‘”èée½ÕQÕ‰•=ÕÑ ¤ì($%¥˜€ …É•‘•¹Ñ¥…±Ì¹¥Í½µÁ±•Ñ” ¤¤ì($$%…ÕÑ¡MÑ…ÑÕÍ|´ùÍ•ÑQ•áÐ ‰Q¡¥ÌM,‰Õ¥±‘½•Ì¹½Ð½¹Ñ…¥¸„e½ÕQÕ‰”=ÕÑ …ÁÁ±¥…Ñ¥½¸¸¹…‰±”Ñ¡”ÕÍÑ½´½½±”=ÕÑ …ÁÀ½ÁÑ¥½¸…¹•¹Ñ•È¥ÑÌÉ•‘•¹Ñ¥…±Ì¸ˆ¤ì($$%É•ÑÕÉ¸ì($%ô(%ô((%…ÕÑ¡MÑ…ÑÕÍ|´ùÍ•ÑQ•áÐ¡¡…Í½¹¹•Ñ•‘=ÕÑ¡½Õ¹Ð¡…ÕÑ¡5½‘”¤($$$$€€€€€ü€‰=Á•¹¥¹œe½ÕQÕ‰”±½¥¸¸Q¡”Í…Ù•½¹¹•Ñ¥½¸ÍÑ…åÌÕ¹¡…¹•Õ¹±•ÍÌÑ¡”¹•Ü±½¥¸ÍÕ••‘Ì…¹å½ÔÁÉ•ÍÌM…Ù”¸ˆ($$$$€€€€€è€‰=Á•¹¥¹œe½ÕQÕ‰”±½¥¸¥¸å½ÕÈ‰É½ÝÍ•È¸¸¸ˆ¤ì(%¥˜€¡½…ÕÑ¡½¹¹•Ñ½É|´ù‰•¥¸¡…ÕÑ¡5½‘”°É•‘•¹Ñ¥…±Ì¹±¥•¹Ñ%°É•‘•¹Ñ¥…±Ì¹±¥•¹ÑM•É•Ð¤¤($%ÕÁ‘…Ñ•A±…Ñ™½Éµ!¥¹Ð ¤ì)ô()Ù½¥Q…É•Ñ‘¥Ñ¥…±½œèé‘¥Í½¹¹•Ñ=ÕÑ¡½Õ¹Ð ¤)ì(%¥˜€¡½…ÕÑ¡½¹¹•Ñ½É|´ù¥ÍIÕ¹¹¥¹œ ¤¤($%É•ÑÕÉ¸ì((%½¹ÍÐQ…É•ÑÕÑ¡5½‘”…ÕÑ¡5½‘”€ôÑ…É•ÑÕÑ¡5½‘•É½µMÑÉ¥¹œ¡…ÕÑ¡5½‘•|´ùÕÉÉ•¹Ñ…Ñ„ ¤¹Ñ½MÑÉ¥¹œ ¤¤ì(%¥˜€¡…ÕÑ¡5½‘”€„ôQ…É•ÑÕÑ¡5½‘”èéQÝ¥Ñ¡=ÕÑ €˜˜…ÕÑ¡5½‘”€„ôQ…É•ÑÕÑ¡5½‘”èée½ÕQÕ‰•=ÕÑ €˜˜($€€€…ÕÑ¡5½‘”€„ôQ…É•ÑÕÑ¡5½‘”èé-¥­=ÕÑ ¤($%É•ÑÕÉ¸ì((%…ÕÑ¡½Õ¹Ñ9…µ•|¹±•…È ¤ì(%½…ÕÑ¡I•™É•Í¡Q½­•¹|¹±•…È ¤ì(%½…ÕÑ¡I•™É•Í¡Q½­•¹I•™|¹±•…È ¤ì(%½…ÕÑ¡½¹¹•Ñ•‘Q¡¥Í‘¥Ñ|€ô™…±Í”ì(%½¹¹•Ñ•‘ÕÑ¡5½‘•|€ôQ…É•ÑÕÑ¡5½‘”èé5…¹Õ…±IÑµÀì(%¥˜€¡¥ÍAÕ‰±¥Í¡•É5…¹…•‘ÕÑ ¡…ÕÑ¡5½‘”¤¤ì($%…ÕÑ¡É•‘•¹Ñ¥…±I•™|¹±•…È ¤ì($%½…ÕÑ¡±¥•¹Ñ%‘Y…±Õ•|¹±•…È ¤ì($%½…ÕÑ¡±¥•¹ÑM•É•ÑY…±Õ•|¹±•…È ¤ì($%½…ÕÑ¡±¥•¹ÑM•É•ÑI•™|¹±•…È ¤ì($%½…ÕÑ¡±¥•¹Ñ%‘|´ù±•…È ¤ì($%½…ÕÑ¡±¥•¹ÑM•É•Ñ|´ù±•…È ¤ì($%ÍÑÉ•…µ-•åY…±Õ•|¹±•…È ¤ì($%ÍÑÉ•…µ-•å|´ù±•…È ¤ì($%ÍÑÉ•…µ-•å|´ùÍ•ÑA±…•¡½±‘•ÉQ•áÐ¡…ÕÑ¡5½‘”€ôôQ…É•ÑÕÑ¡5½‘”èéQÝ¥Ñ¡=ÕÑ ($$$$$€€€€€€€üEMÑÉ¥¹1¥Ñ•É…° ‰½¹¹•ÐQÝ¥Ñ Ñ¼É•ÑÉ¥•Ù”Ñ¡”ÍÑÉ•…´­•äˆ¤($$$$$€€€€€€€èEMÑÉ¥¹1¥Ñ•É…° ‰½¹¹•Ð-¥¬Ñ¼É•ÑÉ¥•Ù”Ñ¡”ÍÑÉ•…´­•äˆ¤¤ì(%ô((%ÕÁ‘…Ñ•A±…Ñ™½Éµ!¥¹Ð ¤ì(%¥˜€¡…ÕÑ¡5½‘”€ôôQ…É•ÑÕÑ¡5½‘”èéQÝ¥Ñ¡=ÕÑ ¤($%…ÕÑ¡MÑ…ÑÕÍ|´ùÍ•ÑQ•áÐ ‰QÝ¥Ñ ‘¥Í½¹¹•Ñ•¥¸Ñ¡¥Ì•‘¥Ñ½È¸AÉ•ÍÌM…Ù”Ñ¼É•µ½Ù”Ñ¡”Í…Ù•­•ä°½È…¹•°Ñ¼­••À¥Ð¸ˆ¤ì(%•±Í”¥˜€¡…ÕÑ¡5½‘”€ôôQ…É•ÑÕÑ¡5½‘”èé-¥­=ÕÑ ¤($%…ÕÑ¡MÑ…ÑÕÍ|´ùÍ•ÑQ•áÐ ‰-¥¬‘¥Í½¹¹•Ñ•¥¸Ñ¡¥Ì•‘¥Ñ½È¸AÉ•ÍÌM…Ù”Ñ¼É•µ½Ù”Ñ¡”Í…Ù•­•ä°½È…¹•°Ñ¼­••À¥Ð¸ˆ¤ì(%•±Í”($%…ÕÑ¡MÑ…ÑÕÍ|´ùÍ•ÑQ•áÐ ‰e½ÕQÕ‰”‘¥Í½¹¹•Ñ•¥¸Ñ¡¥Ì•‘¥Ñ½È¸AÉ•ÍÌM…Ù”Ñ¼É•µ½Ù”Ñ¡”Í…Ù•…ÕÑ¡½É¥é…Ñ¥½¸°½È…¹•°Ñ¼­••À¥Ð¸ˆ¤ì)ô()Ù½¥Q…É•Ñ‘¥Ñ¥…±½œèé¡…¹‘±•=ÕÑ¡¥¹¥Í¡•¡½¹ÍÐ=ÕÑ¡½¹¹•Ñ¥½¹I•ÍÕ±Ð€™É•ÍÕ±Ð¤)ì(%½¹ÍÐQ…É•ÑÕÑ¡5½‘”ÕÉÉ•¹ÑÕÑ¡5½‘”€ôÑ…É•ÑÕÑ¡5½‘•É½µMÑÉ¥¹œ¡…ÕÑ¡5½‘•|´ùÕÉÉ•¹Ñ…Ñ„ ¤¹Ñ½MÑÉ¥¹œ ¤¤ì(%½¹ÍÐEMÑÉ¥¹œÕÉÉ•¹ÑA±…Ñ™½Éµ%€ôÁ±…Ñ™½Éµ|´ùÕÉÉ•¹Ñ…Ñ„ ¤¹Ñ½MÑÉ¥¹œ ¤ì(%¥˜€¡É•ÍÕ±Ð¹…ÕÑ¡5½‘”€„ôÕÉÉ•¹ÑÕÑ¡5½‘”ñð€…Á±…Ñ™½ÉµMÕÁÁ½ÉÑÍÕÑ¡5½‘”¡ÕÉÉ•¹ÑA±…Ñ™½Éµ%°É•ÍÕ±Ð¹…ÕÑ¡5½‘”¤¤ì($%ÕÁ‘…Ñ•A±…Ñ™½Éµ!¥¹Ð ¤ì($%…ÕÑ¡MÑ…ÑÕÍ|´ùÍ•ÑQ•áÐ ‰%¹½É•…¸=ÕÑ É•ÍÕ±Ð‰•…ÕÍ”Ñ¡”Í•±•Ñ•Á±…Ñ™½É´½È±½¥¸µ½‘”¡…¹•¸ˆ¤ì($%É•ÑÕÉ¸ì(%ô(%¥˜€ …É•ÍÕ±Ð¹•ÉÉ½É5•ÍÍ…”¹¥ÍµÁÑä ¤¤ì($%½¹ÍÐ‰½½°ÁÉ•Í•ÉÙ•€ô¡…Í½¹¹•Ñ•‘=ÕÑ¡½Õ¹Ð¡É•ÍÕ±Ð¹…ÕÑ¡5½‘”¤ì($%ÕÁ‘…Ñ•A±…Ñ™½Éµ!¥¹Ð ¤ì($%…ÕÑ¡MÑ…ÑÕÍ|´ùÍ•ÑQ•áÐ¡ÁÉ•Í•ÉÙ•($$$$$€€€€€üEMÑÉ¥¹œ ‰I•½¹¹•Ð™…¥±•ìÑ¡”Í…Ù•½¹¹•Ñ¥½¸Ý…Ì¹½Ð¡…¹•¸€”Äˆ¤¹…Éœ¡É•ÍÕ±Ð¹•ÉÉ½É5•ÍÍ…”¤($$$$$€€€€€èÉ•ÍÕ±Ð¹•ÉÉ½É5•ÍÍ…”¤ì($%É•ÑÕÉ¸ì(%ô((%…ÕÑ¡½Õ¹Ñ9…µ•|€ôÉ•ÍÕ±Ð¹…½Õ¹Ñ9…µ”ì(%½…ÕÑ¡½¹¹•Ñ•‘Q¡¥Í‘¥Ñ|€ôÑÉÕ”ì(%½¹¹•Ñ•‘ÕÑ¡5½‘•|€ôÉ•ÍÕ±Ð¹…ÕÑ¡5½‘”ì(%¥˜€¡É•ÍÕ±Ð¹…ÕÑ¡5½‘”€ôôQ…É•ÑÕÑ¡5½‘”èée½ÕQÕ‰•=ÕÑ ¤($%½…ÕÑ¡I•™É•Í¡Q½­•¹|€ôÉ•ÍÕ±Ð¹É•™É•Í¡Q½­•¸ì(%¥˜€ …É•ÍÕ±Ð¹Í•ÉÙ•ÉUÉ°¹¥ÍµÁÑä ¤¤ì($%Í•ÉÙ•É½±±½ÝÍA±…Ñ™½Éµ|€ôÑÉÕ”ì($%Í•ÉÙ•É|´ùÍ•ÑQ•áÐ¡É•ÍÕ±Ð¹Í•ÉÙ•ÉUÉ°¤ì(%ô(%¥˜€ …É•ÍÕ±Ð¹ÍÑÉ•…µ-•ä¹¥ÍµÁÑä ¤¤($%ÍÑÉ•…µ-•å|´ùÍ•ÑQ•áÐ¡É•ÍÕ±Ð¹ÍÑÉ•…µ-•ä¤ì(%¥˜€¡¹…µ•|´ùÑ•áÐ ¤¹ÑÉ¥µµ• ¤¹¥ÍµÁÑä ¤ñð¹…µ•|´ùÑ•áÐ ¤¹ÑÉ¥µµ• ¤€ôô€‰9•ÜQ…É•Ðˆ¤($%¹…µ•|´ùÍ•ÑQ•áÐ¡É•ÍÕ±Ð¹…½Õ¹Ñ9…µ”¹¥ÍµÁÑä ¤€üÁ±…Ñ™½ÉµÍ|¹ÁÉ•Í•Ñ	å%¡Á±…Ñ™½Éµ|´ùÕÉÉ•¹Ñ…Ñ„ ¤¹Ñ½MÑÉ¥¹œ ¤¤¹‘¥ÍÁ±…å9…µ”€èÉ•ÍÕ±Ð¹…½Õ¹Ñ9…µ”¤ì(%¥˜€¡•¹…‰±•‘|¤($%•¹…‰±•‘|´ùÍ•Ñ¡•­•¡ÑÉÕ”¤ì(%¥˜€¡ÍÑ…ÉÑ]¥Ñ¡±±|¤($%ÍÑ…ÉÑ]¥Ñ¡±±|´ùÍ•Ñ¡•­•¡ÑÉÕ”¤ì(%ÕÁ‘…Ñ•A±…Ñ™½Éµ!¥¹Ð ¤ì(%½¹ÍÐ‰½½°¡…ÍMÑÉ•…µ-•ä€ô€…±¥¹•Q•áÑ=ÉµÁÑä¡ÍÑÉ•…µ-•å|¤¹ÑÉ¥µµ• ¤¹¥ÍµÁÑä ¤ñð€……ÕÑ¡É•‘•¹Ñ¥…±I•™|¹ÑÉ¥µµ• ¤¹¥ÍµÁÑä ¤ì(%¥˜€¡É•ÍÕ±Ð¹…ÕÑ¡5½‘”€ôôQ…É•ÑÕÑ¡5½‘”èée½ÕQÕ‰•=ÕÑ €˜˜€…¡…ÍMÑÉ•…µ-•ä¤ì($%…ÕÑ¡MÑ…ÑÕÍ|´ùÍ•ÑQ•áÐ¡EMÑÉ¥¹œ ‰½¹¹•Ñ•…Ì€”Ä¸¹Ñ•ÈÑ¡”e½ÕQÕ‰”ÍÑÉ•…´­•ä°Ñ¡•¸M…Ù”¸ˆ¤¹…Éœ¡É•ÍÕ±Ð¹…½Õ¹Ñ9…µ”¤¤ì($%É•ÑÕÉ¸ì(%ô((%…ÕÑ¡MÑ…ÑÕÍ|´ùÍ•ÑQ•áÐ¡EMÑÉ¥¹œ ‰½¹¹•Ñ•…Ì€”Ä¸I•Ù¥•ÜÑ¡”Í•ÑÑ¥¹Ì°Ñ¡•¸ÁÉ•ÍÌM…Ù”¸ˆ¤¹…Éœ¡É•ÍÕ±Ð¹…½Õ¹Ñ9…µ”¤¤ì)ô()‰½½°Q…É•Ñ‘¥Ñ¥…±½œèé¡…Í½¹¹•Ñ•‘=ÕÑ¡½Õ¹Ð¡Q…É•ÑÕÑ¡5½‘”…ÕÑ¡5½‘”¤½¹ÍÐ)ì(%¥˜€¡…ÕÑ¡5½‘”€ôôQ…É•ÑÕÑ¡5½‘”èéQÝ¥Ñ¡=ÕÑ ¤($%É•ÑÕÉ¸€……ÕÑ¡½Õ¹Ñ9…µ•|¹ÑÉ¥µµ• ¤¹¥ÍµÁÑä ¤ñð€……ÕÑ¡É•‘•¹Ñ¥…±I•™|¹ÑÉ¥µµ• ¤¹¥ÍµÁÑä ¤ì(%¥˜€¡…ÕÑ¡5½‘”€ôôQ…É•ÑÕÑ¡5½‘”èé-¥­=ÕÑ ¤($%É•ÑÕÉ¸€……ÕÑ¡½Õ¹Ñ9…µ•|¹ÑÉ¥µµ• ¤¹¥ÍµÁÑä ¤ñð€……ÕÑ¡É•‘•¹Ñ¥…±I•™|¹ÑÉ¥µµ• ¤¹¥ÍµÁÑä ¤ì(%¥˜€¡…ÕÑ¡5½‘”€ôôQ…É•ÑÕÑ¡5½‘”èée½ÕQÕ‰•=ÕÑ ¤($%É•ÑÕÉ¸€……ÕÑ¡½Õ¹Ñ9…µ•|¹ÑÉ¥µµ• ¤¹¥ÍµÁÑä ¤ñð€…½…ÕÑ¡I•™É•Í¡Q½­•¹|¹ÑÉ¥µµ• ¤¹¥ÍµÁÑä ¤ñð($$€€€€€€€…½…ÕÑ¡I•™É•Í¡Q½­•¹I•™|¹ÑÉ¥µµ• ¤¹¥ÍµÁÑä ¤ì(%É•ÑÕÉ¸™…±Í”ì)ô()ô€¼¼¹…µ•ÍÁ…”‘Í¬(