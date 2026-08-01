if(NOT DEFINED COMMENT_VIEWER_INTEGRATION_CPP OR
   NOT EXISTS "${COMMENT_VIEWER_INTEGRATION_CPP}")
  message(FATAL_ERROR "COMMENT_VIEWER_INTEGRATION_CPP must point to the integration source")
endif()

file(READ "${COMMENT_VIEWER_INTEGRATION_CPP}" integration_source)

if(integration_source MATCHES "wait_for_browser_init\\(\\)")
  message(FATAL_ERROR
          "Comment Viewer startup must not block the OBS UI thread while CEF initializes")
endif()

if(NOT integration_source MATCHES
   "QTimer::singleShot\\(250, this, \\[this, viewerUrl\\]")
  message(FATAL_ERROR
          "Comment Viewer startup must retry dock creation without blocking the OBS UI thread")
endif()

string(FIND "${integration_source}"
       "create_widget(dockHost, encodedUrl, nullptr)" host_parent_position)
if(host_parent_position EQUAL -1)
  message(FATAL_ERROR
          "The Comment Viewer browser must be created under the registered dock host")
endif()

string(FIND "${integration_source}"
       "layout->addWidget(browser)" layout_position)
if(layout_position EQUAL -1)
  message(FATAL_ERROR
          "The registered dock host must own its QCefWidget")
endif()

string(FIND "${integration_source}"
       "obs_frontend_add_dock_by_id(DockId, DockTitle, dockHost)" normal_dock_position)
if(normal_dock_position EQUAL -1)
  message(FATAL_ERROR
          "The Comment Viewer must use OBS's normal plugin dock registration")
endif()

if(host_parent_position GREATER normal_dock_position)
  message(FATAL_ERROR
          "The browser hierarchy must be complete before OBS shows the dock and triggers QCefWidget initialization")
endif()

string(FIND "${integration_source}"
       "const QUrl dockUrl(QStringLiteral(\"http://localhost:17321/viewer?dock=chat&send=1\"))" isolated_url_position)
if(isolated_url_position EQUAL -1)
  message(FATAL_ERROR
          "The Comment Viewer dock must use a separate Chromium connection pool")
endif()

if(NOT integration_source MATCHES
   "const std::string encodedUrl = dockUrl\\.toEncoded")
  message(FATAL_ERROR
          "The browser must navigate to the isolated localhost dock URL")
endif()

if(integration_source MATCHES "QTimer::singleShot\\(6000, this" OR
   integration_source MATCHES "Recreated the DSK Comments browser")
  message(FATAL_ERROR
          "The fixed dock must not retain the diagnostic browser replacement workaround")
endif()

string(FIND "${integration_source}"
       "browser_->setURL(dockUrl.toEncoded" reconnect_navigation_position)
if(reconnect_navigation_position EQUAL -1)
  message(FATAL_ERROR
          "Reconnecting an existing Comment Viewer dock must navigate through the isolated host")
endif()

if(integration_source MATCHES "scheduleBrowserNavigation")
  message(FATAL_ERROR
          "The fixed dock must not repeatedly reload and consume more Chromium connections")
endif()

message(STATUS "Comment Viewer isolates its dock from long-lived 127.0.0.1 browser connections")
