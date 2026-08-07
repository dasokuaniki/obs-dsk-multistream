#pragma once

#include <QObject>
#include <QPointer>

#include <memory>

class QCefWidget;
class QAction;
class QString;
class QUrl;
class QWidget;
struct QCef;

namespace dsk {

class HttpClient;

class CommentViewerIntegration final : public QObject {
	Q_OBJECT

public:
	explicit CommentViewerIntegration(QObject *parent = nullptr);
	~CommentViewerIntegration() override;

	void initialize();
	void shutdown();
	void setEnabled(bool enabled);
	bool enabled() const;
	bool registerDockShell();

private:
	static void openViewer(void *);

	quint64 advanceProbeGeneration();
	void registerOpenViewerMenu();
	void beginProbe();
	void scheduleProbe(quint64 generation, bool launchAttempted, int attempt);
	bool createDock(const QUrl &viewerUrl);
	void removeDock();
	bool hideLegacyDock() const;
	bool removeLegacyDockConfig() const;

private slots:
	void onBrowserUrlChanged(const QString &url);
	void onBrowserTitleChanged(const QString &title);

private:
	std::unique_ptr<HttpClient> http_;
	std::unique_ptr<QCef> browserPanel_;
	QPointer<QAction> openViewerMenuAction_;
	QPointer<QWidget> dockContents_;
	QPointer<QWidget> dockPlaceholder_;
	QPointer<QCefWidget> browser_;
	bool enabled_ = false;
	bool shuttingDown_ = false;
	quint64 probeGeneration_ = 0;
};

} // namespace dsk
