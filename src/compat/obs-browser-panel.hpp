#pragma once

// ABI declarations from obsproject/obs-browser panel/browser-panel.hpp at
// ea04212e4bbadd077f9e6038758c4e4779c24fa3, the commit pinned by OBS 32.1.2.
// obs-browser is loaded dynamically so DSK still loads when browser support is absent.

#include <obs-module.h>
#include <util/platform.h>
#include <util/util.hpp>

#include <QWidget>

#include <functional>
#include <string>

struct QCefCookieManager {
	virtual ~QCefCookieManager() = default;

	virtual bool DeleteCookies(const std::string &url, const std::string &name) = 0;
	virtual bool SetStoragePath(const std::string &storagePath, bool persistSessionCookies = false) = 0;
	virtual bool FlushStore() = 0;

	using CookieExistsCallback = std::function<void(bool)>;
	virtual void CheckForCookie(const std::string &site, const std::string &cookie,
				    CookieExistsCallback callback) = 0;
};

class QCefWidget : public QWidget {
	Q_OBJECT

protected:
	explicit QCefWidget(QWidget *parent) : QWidget(parent) {}

public:
	virtual void setURL(const std::string &url) = 0;
	virtual void setStartupScript(const std::string &script) = 0;
	virtual void allowAllPopups(bool allow) = 0;
	virtual void closeBrowser() = 0;
	virtual void reloadPage() = 0;
	virtual bool zoomPage(int direction) = 0;
	virtual void executeJavaScript(const std::string &script) = 0;

signals:
	void titleChanged(const QString &title);
	void urlChanged(const QString &url);
};

struct QCef {
	virtual ~QCef() = default;

	virtual bool init_browser() = 0;
	virtual bool initialized() = 0;
	virtual bool wait_for_browser_init() = 0;
	virtual QCefWidget *create_widget(QWidget *parent, const std::string &url,
					 QCefCookieManager *cookieManager = nullptr) = 0;
	virtual QCefCookieManager *create_cookie_manager(const std::string &storagePath,
							 bool persistSessionCookies = false) = 0;
	virtual BPtr<char> get_cookie_path(const std::string &storagePath) = 0;
	virtual void add_popup_whitelist_url(const std::string &url, QObject *object) = 0;
	virtual void add_force_popup_url(const std::string &url, QObject *object) = 0;
};

inline void *dskObsBrowserLibrary()
{
	obs_module_t *browserModule = obs_get_module("obs-browser");
	return browserModule ? obs_get_module_lib(browserModule) : nullptr;
}

inline QCef *dskCreateObsBrowserPanel()
{
	void *library = dskObsBrowserLibrary();
	if (!library)
		return nullptr;

	using CreateQcef = QCef *(*)();
	const auto createQcef = reinterpret_cast<CreateQcef>(os_dlsym(library, "obs_browser_create_qcef"));
	return createQcef ? createQcef() : nullptr;
}

inline int dskObsBrowserPanelVersion()
{
	void *library = dskObsBrowserLibrary();
	if (!library)
		return 0;

	using QcefVersion = int (*)();
	const auto qcefVersion = reinterpret_cast<QcefVersion>(os_dlsym(library, "obs_browser_qcef_version_export"));
	return qcefVersion ? qcefVersion() : 0;
}
