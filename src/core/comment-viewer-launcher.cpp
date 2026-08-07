#include "comment-viewer-launcher.hpp"

#include "comment-viewer-contract.hpp"
#include "diagnostics.hpp"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

namespace dsk {
namespace {

constexpr auto kDefaultBaseUrl = "http://127.0.0.1:17321";
constexpr auto kViewerDirEnv = "DSK_COMMENT_VIEWER_DIR";

std::optional<CommentViewerInstallation> commentViewerInstallation()
{
	const QString configured = qEnvironmentVariable(kViewerDirEnv).trimmed();
	const QString localAppData = qEnvironmentVariable("LOCALAPPDATA").trimmed();
	return detectCommentViewerInstallation(configured, localAppData);
}

bool runViewerScript(const QString &scriptName)
{
	const auto installation = commentViewerInstallation();
	if (!installation) {
		logWarning(QString("DSK Comment Viewer directory not found. Set %1.").arg(QString::fromLatin1(kViewerDirEnv)));
		return false;
	}
	const QString &dir = installation->directory;

	const QString scriptPath = QDir(dir).filePath(scriptName);
	if (!QFileInfo::exists(scriptPath)) {
		logWarning(QString("DSK Comment Viewer script not found: %1").arg(scriptPath));
		return false;
	}

	const QString systemRoot = qEnvironmentVariable("SystemRoot").trimmed();
	const QString scriptHost = QDir(systemRoot).filePath(QStringLiteral("System32/wscript.exe"));
	if (systemRoot.isEmpty() || !QFileInfo(scriptHost).isFile()) {
		logWarning("Windows Script Host was not found; DSK Comment Viewer was not launched.");
		return false;
	}

	const bool started = QProcess::startDetached(QDir::toNativeSeparators(scriptHost),
						     {QDir::toNativeSeparators(scriptPath)}, dir);
	if (!started)
		logWarning(QString("Failed to launch DSK Comment Viewer script: %1").arg(scriptPath));
	return started;
}

} // namespace

QUrl commentViewerBaseUrl()
{
	const auto installation = commentViewerInstallation();
	return installation ? installation->baseUrl : QUrl(QString::fromLatin1(kDefaultBaseUrl));
}

QUrl commentViewerPageUrl()
{
	return commentViewerBaseUrl().resolved(QUrl(QStringLiteral("/viewer")));
}

QUrl commentViewerObsIntegrationUrl()
{
	return commentViewerObsIntegrationUrlForBase(commentViewerBaseUrl());
}

bool isCommentViewerInstalled()
{
	return commentViewerInstallation().has_value();
}

bool startCommentViewerServer()
{
	return runViewerScript(QStringLiteral("start-server-hidden.vbs"));
}

bool openCommentViewerApp()
{
	return runViewerScript(QStringLiteral("start-hidden.vbs"));
}

} // namespace dsk
