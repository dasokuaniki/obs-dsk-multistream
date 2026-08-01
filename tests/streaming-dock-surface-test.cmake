foreach(required
    MAIN_DOCK_CPP
    MAIN_DOCK_HPP
    STREAM_CONTROLS_CPP
    STREAM_CONTROLS_HPP
    STREAM_CONTROL_ASSETS_HPP
    STREAM_CONTROL_VISUALS_HPP)
  if(NOT DEFINED ${required} OR NOT EXISTS "${${required}}")
    message(FATAL_ERROR "${required} must point to an existing source file")
  endif()
endforeach()

file(READ "${MAIN_DOCK_CPP}" main_dock_cpp)
file(READ "${MAIN_DOCK_HPP}" main_dock_hpp)
file(READ "${STREAM_CONTROLS_CPP}" controls_cpp)
file(READ "${STREAM_CONTROLS_HPP}" controls_hpp)
file(READ "${STREAM_CONTROL_ASSETS_HPP}" assets_hpp)
file(READ "${STREAM_CONTROL_VISUALS_HPP}" visuals_hpp)

if(NOT main_dock_cpp MATCHES "dskStreamingToolbar" OR
   NOT main_dock_cpp MATCHES "dskStreamingPageLabel" OR
   NOT main_dock_cpp MATCHES "toolbarLayout->addWidget\\(menuButton_\\)" OR
   NOT main_dock_cpp MATCHES "toolbarLayout->addWidget\\(streamControls_->allToggleButton\\(\\)\\)")
  message(FATAL_ERROR "Settings, the page label, and Start/Stop All must share the compact control bar")
endif()
string(FIND "${main_dock_cpp}" "layout->addWidget(pages_, 1);" pages_layout_position)
string(FIND "${main_dock_cpp}" "layout->addWidget(toolbarHost);" toolbar_layout_position)
if(pages_layout_position EQUAL -1 OR toolbar_layout_position EQUAL -1 OR
   toolbar_layout_position LESS pages_layout_position)
  message(FATAL_ERROR "The Controls and Start/Stop All bar must sit below the streaming target cards")
endif()
if(NOT main_dock_cpp MATCHES "QStringLiteral\\(\"DSK Streaming\"\\)" OR
   NOT main_dock_cpp MATCHES "dock->setWindowTitle\\(dockTitle\\)")
  message(FATAL_ERROR "The actual OBS dock title must remain DSK Streaming on every page")
endif()
foreach(required_legal_url
    "https://dsk.dasoku.org/privacy"
    "https://dsk.dasoku.org/terms"
    "https://security.google.com/settings/security/permissions"
    "https://www.twitch.tv/p/terms-of-service#simulcasting")
  string(FIND "${main_dock_cpp}" "${required_legal_url}" legal_url_position)
  if(legal_url_position EQUAL -1)
    message(FATAL_ERROR "The settings menu must keep the legal/support URL available: ${required_legal_url}")
  endif()
endforeach()
if(controls_cpp MATCHES "stopAll_" OR controls_hpp MATCHES "stopAll_")
  message(FATAL_ERROR "Start All and Stop All must be one state-dependent control")
endif()
if(NOT controls_cpp MATCHES "allToggle_->setText" OR NOT controls_hpp MATCHES "allToggle_")
  message(FATAL_ERROR "The all-stream control must update its label from current state")
endif()
if(NOT controls_cpp MATCHES "setFixedSize\\(42, 30\\)" OR
   controls_cpp MATCHES "setMinimumSize\\(164, 84\\)")
  message(FATAL_ERROR "Per-stream action controls must use the compact icon-button footprint")
endif()
if(controls_cpp MATCHES "dskTargetOverflow" OR controls_hpp MATCHES "QToolButton \\*menu")
  message(FATAL_ERROR "The new card design must consolidate actions into the chevron menu")
endif()

foreach(required_surface
    dskStreamingControlSurface
    dskStreamingRowsViewport
    dskTargetPrimaryAction
    dskTargetActionMenu
    dskTargetName
    dskTargetState)
  if(NOT controls_cpp MATCHES "${required_surface}")
    message(FATAL_ERROR "Reference streaming design is missing ${required_surface}")
  endif()
endforeach()
if(NOT controls_cpp MATCHES "QScrollArea" OR
   NOT controls_cpp MATCHES "setWidgetResizable\\(true\\)")
  message(FATAL_ERROR "The compact card stack must remain usable when the target count exceeds the dock height")
endif()
if(NOT controls_cpp MATCHES "#2ed47a" OR
   NOT controls_cpp MATCHES "Bahnschrift")
  message(FATAL_ERROR "The reference green status accent and display typography must be present")
endif()
if(main_dock_cpp MATCHES "StreamingDockTitleBar" OR
   main_dock_cpp MATCHES "dskStreamingTitleBar" OR
   main_dock_cpp MATCHES "dock-menu-button.png" OR
   main_dock_cpp MATCHES "Float dock" OR
   main_dock_cpp MATCHES "Hide dock")
  message(FATAL_ERROR "OBS already supplies the dock title and float menu, so the streaming surface must not duplicate them")
endif()
if(controls_cpp MATCHES "PerforatedRail" OR
   controls_cpp MATCHES "dskTargetCyanDivider" OR
   NOT controls_cpp MATCHES "setFixedHeight\\(50\\)" OR
   controls_cpp MATCHES "setMinimumHeight\\(254\\)")
  message(FATAL_ERROR "Target rows must use the compact rounded 50px card layout without the old rail or divider")
endif()
if(NOT main_dock_cpp MATCHES "layout->setContentsMargins\\(0, 0, 0, 0\\)" OR
   NOT main_dock_cpp MATCHES "MainDock \{ background-color: #080c0f; border: 0; \}" OR
   NOT main_dock_cpp MATCHES "dskStreamingToolbarHost" OR
   NOT main_dock_cpp MATCHES "toolbarHost->setFixedHeight\\(60\\)" OR
   NOT main_dock_cpp MATCHES "toolbarHostLayout->setContentsMargins\\(5, 5, 5, 5\\)" OR
   NOT main_dock_cpp MATCHES "QWidget#dskStreamingToolbarHost \\{ background-color: #080c0f; border: 0; \\}" OR
   NOT main_dock_cpp MATCHES "toolbarLayout->setContentsMargins\\(6, 4, 8, 4\\)" OR
   NOT main_dock_cpp MATCHES "setFixedSize\\(22, 22\\)" OR
   NOT main_dock_cpp MATCHES "toolbar->setFixedHeight\\(50\\)" OR
   NOT controls_cpp MATCHES "setFixedSize\\(90, 28\\)" OR
   NOT controls_cpp MATCHES "layout->setContentsMargins\\(10, 4, 10, 4\\)" OR
   NOT controls_cpp MATCHES "layout->setHorizontalSpacing\\(8\\)" OR
   NOT controls_cpp MATCHES "actionLayout->setSpacing\\(6\\)")
  message(FATAL_ERROR "Card gutters must not be confused with dock or card-content padding")
endif()
if(NOT controls_cpp MATCHES "buttons_->setContentsMargins\\(5, 5, 5, 0\\)" OR
   NOT controls_cpp MATCHES "buttons_->setSpacing\\(5\\)")
  message(FATAL_ERROR "Target-card horizontal gutters must equal their 5px vertical spacing")
endif()
if(NOT main_dock_cpp MATCHES "stop:0 #1a202b, stop:0.5 #141a24, stop:1 #1b212c" OR
   NOT main_dock_cpp MATCHES "border: 1px solid #2d3541" OR
   NOT main_dock_cpp MATCHES "border-radius: 9px")
  message(FATAL_ERROR "The Controls bar must use the same framed card surface as each target")
endif()
if(NOT main_dock_cpp MATCHES "Bahnschrift[^']*Condensed" OR
   NOT controls_cpp MATCHES "Bahnschrift[^']*Condensed")
  message(FATAL_ERROR "The reference design requires the narrower condensed display face")
endif()

get_filename_component(source_ui_dir "${STREAM_CONTROLS_CPP}" DIRECTORY)
get_filename_component(source_dir "${source_ui_dir}" DIRECTORY)
get_filename_component(repo_dir "${source_dir}" DIRECTORY)
foreach(asset
    settings-button.png
    youtube-icons-2x.png
    twitch-glitch-purple.png)
  if(NOT EXISTS "${repo_dir}/data/ui/${asset}")
    message(FATAL_ERROR "Reference button asset is missing: ${asset}")
  endif()
endforeach()
if(NOT visuals_hpp MATCHES "youtube-icons-2x.png" OR
   NOT visuals_hpp MATCHES "twitch-glitch-purple.png" OR
   visuals_hpp MATCHES "kick-logo.png" OR
   NOT visuals_hpp MATCHES "drawNeutralKickGlyph" OR
   NOT visuals_hpp MATCHES "tiktok-personal.png" OR
   NOT visuals_hpp MATCHES "normalizedPlatformId" OR
   NOT visuals_hpp MATCHES "drawNeutralTikTokGlyph" OR
   NOT visuals_hpp MATCHES "QDesktopServices::openUrl" OR
   NOT visuals_hpp MATCHES "Qt::KeepAspectRatio" OR
   visuals_hpp MATCHES "#ff2028" OR
   visuals_hpp MATCHES "#9146ff" OR
   visuals_hpp MATCHES "#53fc18" OR
   visuals_hpp MATCHES "#25f4ee")
  message(FATAL_ERROR
    "Platform badges must use first-party YouTube/Twitch assets and neutral public Kick/TikTok glyphs")
endif()
foreach(asset_reference
    settings-button.png)
  if(NOT main_dock_cpp MATCHES "${asset_reference}")
    message(FATAL_ERROR "Main dock must render the generated asset: ${asset_reference}")
  endif()
endforeach()
if(NOT controls_cpp MATCHES "new ReferenceAllButton" OR
   NOT controls_cpp MATCHES "new ReferenceActionButton" OR
   NOT controls_cpp MATCHES "new ReferenceMenuButton" OR
   NOT controls_cpp MATCHES "actionLayout->addWidget\\(actionMenu\\).*actionLayout->addWidget\\(button" OR
   NOT controls_cpp MATCHES "addWidget\\(badge, 0, 1, 2, 1, Qt::AlignCenter\\)" OR
   NOT assets_hpp MATCHES "class ReferenceAllButton" OR
   NOT assets_hpp MATCHES "class ReferenceActionButton" OR
   NOT assets_hpp MATCHES "class ReferenceMenuButton")
  message(FATAL_ERROR "The new reference requires centered icons, a chevron-first action group, and custom painted controls")
endif()
message(STATUS "Streaming dock uses the reference state-dependent control surface")
