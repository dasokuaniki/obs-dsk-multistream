#pragma once

#include <QObject>
#include <QPointer>

#include <memory>

class QCefWidget;
class QAction;
class QUrl;
class QWidget;
struct QCef;

namespace dsk {

class HttpClient;

class CommentViewerIntegration final : public QObject {
public:
	explicit CommentViewerIntegration(QObject *parent = nullptr);
	~CommentViewerIntegration() override;

	void initialize();
	void shutdown();
	void setEnabled(bool enabled);
	bool enabled() const;

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

	std::unique_ptr<HttpClient> http_;
	std::unique_ptr<QCef> browserPanel_;
	QPointer<QAction> openViewerMenuAction_;
	QPointer<QWidget> dockContents_;
	QPointer<QCefWidget> browser_;
	bool enabled_ = false;
	bool shuttingDown_ = false;
	quint64 probeGeneration_ = 0;
};

} // namespace dsk
