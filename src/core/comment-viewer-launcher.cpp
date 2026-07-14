#include "comment-viewer-launcher.hpp"

#include "diagnostics.hpp"

#include <obs-module.h>

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace dsk {
namespace {

constexpr auto kDefaultBaseUrl = "http://127.0.0.1:17321";
constexpr auto kViewerDirEnv = "DSK_COMMENT_VIEWER_DIR";

QString commentViewerDir()
{
	const QString configured = qEnvironmentVariable(kViewerDirEnv).trimmed();
	if (!configured.isEmpty() && QFileInfo::exists(QDir(configured).filePath(QStringLiteral("start-server-hidden.vbs"))))
		return configured;

	QStringList candidates;
	const char *moduleDataPath = obs_get_module_data_path(obs_current_module());
	if (moduleDataPath)
		candidates.push_back(QDir(QString::fromUtf8(moduleDataPath)).filePath(QStringLiteral("comment-viewer")));
	const QString localAppData = qEnvironmentVariable("LOCALAPPDATA").trimmed();
	if (!localAppData.isEmpty())
		candidates.push_back(QDir(localAppData).filePath(QStringLiteral("DSKCommentViewer")));
	const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
	if (!documents.isEmpty())
		candidates.push_back(QDir(documents).filePath(QStringLiteral("DSK/CommentViewer")));
	const QString userProfile = qEnvironmentVariable("USERPROFILE").trimmed();
	if (!userProfile.isEmpty())
		candidates.push_back(QDir(userProfile).filePath(QStringLiteral("Documents/DSK/CommentViewer")));

	// Development checkout fallback for this workstation.
	candidates.push_back(QDir(documents).filePath(QStringLiteral("Codex/2026-05-13/new-chat")));
	if (!userProfile.isEmpty())
		candidates.push_back(QDir(userProfile).filePath(QStringLiteral("Documents/Codex/2026-05-13/new-chat")));
	for (const QString &candidate : candidates) {
		if (QFileInfo::exists(QDir(candidate).filePath(QStringLiteral("start-server-hidden.vbs"))))
			return candidate;
	}

	return {};
}

bool runViewerScript(const QString &scriptName)
{
	const QString dir = commentViewerDir();
	if (dir.isEmpty()) {
		logWarning(QString("DSK Comment Viewer directory not found. Set %1.").arg(QString::fromLatin1(kViewerDirEnv)));
		return false;
	}

	const QString scriptPath = QDir(dir).filePath(scriptName);
	if (!QFileInfo::exists(scriptPath)) {
		logWarning(QString("DSK Comment Viewer script not found: %1").arg(scriptPath));
		return false;
	}

	const bool started =
		QProcess::startDetached(QStringLiteral("wscript.exe"), {QDir::toNativeSeparators(scriptPath)}, dir);
	if (!started)
		logWarning(QString("Failed to launch DSK Comment Viewer script: %1").arg(scriptPath));
	return started;
}

} // namespace

QUrl commentViewerBaseUrl()
{
	return QUrl(QString::fromLatin1(kDefaultBaseUrl));
}

QUrl commentViewerPageUrl()
{
	return QUrl(QStringLiteral("%1/viewer").arg(QString::fromLatin1(kDefaultBaseUrl)));
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
