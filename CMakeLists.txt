cmake_minimum_required(VERSION 3.28...3.30)

project(obs-dsk-multistream VERSION 0.4.1 LANGUAGES CXX)

# CMake 4.2 can mojibake the UTF-8 /showIncludes prefix from Japanese MSVC,
# leaving Ninja with no header dependencies and stale incremental objects.
if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
  set(CMAKE_CL_SHOWINCLUDES_PREFIX "メモ: インクルード ファイル: ")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DSK_BOOTSTRAP_OBS_DEPS "Download pinned OBS build dependencies for clean CI builds" OFF)
if(DSK_BOOTSTRAP_OBS_DEPS)
  if(NOT WIN32 OR NOT CMAKE_GENERATOR MATCHES "Visual Studio")
    message(FATAL_ERROR "DSK_BOOTSTRAP_OBS_DEPS requires Windows and a Visual Studio generator")
  endif()
  list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ci")
  include(dsk_obs_dependencies)
  dsk_bootstrap_obs_dependencies()
  set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)
endif()

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Network Widgets)
find_package(CURL CONFIG QUIET)
if(NOT CURL_FOUND)
  find_package(CURL REQUIRED)
endif()

option(DSK_ENABLE_OBS_CANVAS_API "Enable OBS canvas API path for real vertical video output" ON)
option(DSK_BUILD_TESTS "Build DSK non-OBS smoke tests" ON)
option(DSK_INCLUDE_E2E_HOOKS "Compile OBS E2E automation hooks into the plugin" OFF)
option(DSK_PUBLISHER_RELEASE "Require publisher OAuth credentials for release builds" OFF)
set(DSK_TEST_RUNTIME_DIR "" CACHE PATH "Signed OBS runtime directory used by Windows tests")
set(DSK_OAUTH_APP_CONFIG "" CACHE FILEPATH "Publisher OAuth application JSON used at build time")
if(WIN32 AND NOT DSK_TEST_RUNTIME_DIR AND IS_DIRECTORY "C:/Program Files/obs-studio/bin/64bit")
  set(DSK_TEST_RUNTIME_DIR "C:/Program Files/obs-studio/bin/64bit" CACHE PATH "Signed OBS runtime directory used by Windows tests" FORCE)
endif()

set(DSK_BUNDLED_YOUTUBE_CLIENT_ID "")
set(DSK_BUNDLED_YOUTUBE_CLIENT_SECRET "")
if(DSK_OAUTH_APP_CONFIG)
  include(cmake/dsk-oauth-config.cmake)
  dsk_load_youtube_oauth_config(
    "${DSK_OAUTH_APP_CONFIG}"
    DSK_BUNDLED_YOUTUBE_CLIENT_ID
    DSK_BUNDLED_YOUTUBE_CLIENT_SECRET
    dsk_oauth_config_format
  )
  message(STATUS "DSK publisher YouTube OAuth application: configured (${dsk_oauth_config_format})")
else()
  message(STATUS "DSK publisher YouTube OAuth application: not configured")
endif()

include(cmake/dsk-release-validation.cmake)
dsk_validate_release_configuration(
  "${DSK_PUBLISHER_RELEASE}" "${DSK_OAUTH_APP_CONFIG}"
  "${DSK_BUNDLED_YOUTUBE_CLIENT_ID}" "${DSK_BUNDLED_YOUTUBE_CLIENT_SECRET}"
  "${DSK_INCLUDE_E2E_HOOKS}"
)

set(DSK_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${DSK_GENERATED_INCLUDE_DIR}")
set_property(
  DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/data/presets/platforms.json"
)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/data/presets/platforms.json" DSK_PLATFORM_PRESETS_JSON)
configure_file(
  cmake/platform-presets-json.hpp.in
  "${DSK_GENERATED_INCLUDE_DIR}/platform-presets-json.hpp"
  @ONLY
)
configure_file(
  cmake/oauth-publisher-config.hpp.in
  "${DSK_GENERATED_INCLUDE_DIR}/oauth-publisher-config.hpp"
  @ONLY
)

add_library(obs-dsk-multistream MODULE)
add_library(OBS::dsk-multistream ALIAS obs-dsk-multistream)

target_sources(
  obs-dsk-multistream
  PRIVATE
    src/plugin-main.cpp
    src/core/diagnostics.cpp
    src/core/diagnostics.hpp
    src/core/encoder-profile-manager.cpp
    src/core/encoder-profile-manager.hpp
    src/core/experimental-features.hpp
    src/core/http-client.cpp
    src/core/http-client.hpp
    src/core/layout-manager.cpp
    src/core/layout-manager.hpp
    src/core/output-manager.cpp
    src/core/output-manager.hpp
    src/core/output-runtime-status.hpp
    src/core/output-signal-policy.hpp
    src/core/oauth-connector.cpp
    src/core/oauth-connector.hpp
    src/core/oauth-provider.cpp
    src/core/oauth-provider.hpp
    src/core/comment-viewer-contract.hpp
    src/core/comment-viewer-integration-policy.hpp
    src/core/comment-viewer-launcher.cpp
    src/core/comment-viewer-launcher.hpp
    src/core/output-target.cpp
    src/core/output-target.hpp
    src/core/platform-preset-registry.cpp
    src/core/platform-preset-registry.hpp
    src/core/settings-codec.cpp
    src/core/settings-codec.hpp
    src/core/secret-store.cpp
    src/core/secret-store.hpp
    src/core/settings-store.cpp
    src/core/settings-store.hpp
    src/core/twitch-dual-format.hpp
    src/core/vertical-scene-builder.cpp
    src/core/vertical-scene-builder.hpp
    src/core/vertical-layout-geometry.hpp
    src/core/youtube-broadcast-selector.cpp
    src/core/youtube-broadcast-selector.hpp
    src/core/youtube-archive-rotation.cpp
    src/core/youtube-archive-rotation.hpp
    src/core/youtube-stream-options.cpp
    src/core/youtube-stream-options.hpp
    src/integration/comment-viewer-integration.cpp
    src/integration/comment-viewer-integration.hpp
    src/ui/main-dock.cpp
    src/ui/main-dock.hpp
    src/ui/layout-widget-utils.hpp
    src/ui/scene-router-dock.cpp
    src/ui/scene-router-dock.hpp
    src/ui/stream-control-assets.hpp
    src/ui/stream-control-visuals.hpp
    src/ui/stream-controls-dock.cpp
    src/ui/stream-controls-dock.hpp
    src/ui/stream-controls-state.hpp
    src/ui/visible-refresh-gate.hpp
    src/ui/target-edit-dialog.cpp
    src/ui/target-edit-dialog.hpp
    src/ui/vertical-layout-editor.cpp
    src/ui/vertical-layout-editor.hpp
    src/core/stable-id-order.hpp
    src/ui/vertical-source-icon.cpp
    src/ui/vertical-source-icon.hpp
    src/ui/vertical-source-icon-loader.hpp
)

if(DSK_INCLUDE_E2E_HOOKS)
  target_sources(
    obs-dsk-multistream
    PRIVATE
      src/core/e2e-auto-runner.cpp
      src/core/e2e-auto-runner.hpp
  )
  target_compile_definitions(obs-dsk-multistream PRIVATE DSK_INCLUDE_E2E_HOOKS)
endif()

target_include_directories(obs-dsk-multistream PRIVATE src "${DSK_GENERATED_INCLUDE_DIR}")

target_link_libraries(
  obs-dsk-multistream
  PRIVATE
    OBS::libobs
    OBS::obs-frontend-api
    Qt6::Core
    Qt6::Network
    Qt6::Widgets
    CURL::libcurl
)

set_target_properties(
  obs-dsk-multistream
  PROPERTIES
    AUTOMOC ON
    AUTOUIC OFF
    AUTORCC OFF
    PREFIX ""
)

if(WIN32)
  configure_file(
    cmake/windows/version.rc.in
    "${CMAKE_CURRENT_BINARY_DIR}/obs-dsk-multistream-version.rc"
    @ONLY
  )
  target_sources(obs-dsk-multistream PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/obs-dsk-multistream-version.rc")
  target_compile_definitions(obs-dsk-multistream PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(obs-dsk-multistream PRIVATE Advapi32)
  if(MSVC)
    target_compile_options(obs-dsk-multistream PRIVATE /guard:cf)
    target_link_options(obs-dsk-multistream PRIVATE /guard:cf)
  endif()
endif()

if(DSK_ENABLE_OBS_CANVAS_API)
  target_compile_definitions(obs-dsk-multistream PRIVATE DSK_ENABLE_OBS_CANVAS_API)
endif()

include(GNUInstallDirs)

install(TARGETS obs-dsk-multistream LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/obs-plugins")
install(DIRECTORY data/ DESTINATION "${CMAKE_INSTALL_DATADIR}/obs/obs-plugins/obs-dsk-multistream")

if(DSK_BUILD_TESTS)
  enable_testing()

  add_test(
    NAME dsk-oauth-config-format-test
    COMMAND
      "${CMAKE_COMMAND}"
      "-DOAUTH_CONFIG_MODULE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/dsk-oauth-config.cmake"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/oauth-config-format-test.cmake"
  )
  set_tests_properties(dsk-oauth-config-format-test PROPERTIES LABELS "unit;oauth;no-obs")

  add_test(
    NAME dsk-configure-windows-dependency-discovery-test
    COMMAND
      "${CMAKE_COMMAND}"
      "-DCONFIGURE_WINDOWS_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/configure-windows.ps1"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/configure-windows-dependency-discovery-test.cmake"
  )
  set_tests_properties(
    dsk-configure-windows-dependency-discovery-test
    PROPERTIES LABELS "unit;build;no-obs"
  )

  add_test(NAME dsk-release-gates-test COMMAND "${CMAKE_COMMAND}" "-DCMAKE_LIST=${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt" "-DCONFIGURE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/configure-windows.ps1" "-DBUILD_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/build-windows-installer.ps1" "-DEXTERNAL_SIGNING_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/external-windows-signing.ps1" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/release-gates-test.cmake")
  set_tests_properties(dsk-release-gates-test PROPERTIES LABELS "unit;packaging;security;no-obs")

  add_test(
    NAME dsk-publisher-release-test
    COMMAND "${CMAKE_COMMAND}"
      "-DVALIDATION_MODULE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/dsk-release-validation.cmake"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/publisher-release-test.cmake"
  )
  set_tests_properties(dsk-publisher-release-test PROPERTIES LABELS "unit;packaging;security;no-obs")
  if(WIN32)
    add_test(
      NAME dsk-release-validation-test
      COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/release-validation-test.ps1"
        -ValidationScript "${CMAKE_CURRENT_SOURCE_DIR}/scripts/dsk-release-validation.ps1"
    )
    set_tests_properties(dsk-release-validation-test PROPERTIES LABELS "unit;packaging;security;no-obs")
  endif()

  function(dsk_configure_test_runtime test_name qt_target)
    set(runtime_modifications "PATH=path_list_prepend:$<TARGET_FILE_DIR:${qt_target}>")
    if(WIN32 AND DSK_TEST_RUNTIME_DIR AND IS_DIRECTORY "${DSK_TEST_RUNTIME_DIR}")
      list(APPEND runtime_modifications "PATH=path_list_prepend:${DSK_TEST_RUNTIME_DIR}")
      if("${qt_target}" STREQUAL "Qt6::Widgets")
        set_tests_properties(
          ${test_name}
          PROPERTIES
            ENVIRONMENT "QT_PLUGIN_PATH=${DSK_TEST_RUNTIME_DIR};QT_QPA_PLATFORM=windows"
        )
      endif()
    endif()
    set_tests_properties(
      ${test_name}
      PROPERTIES
        ENVIRONMENT_MODIFICATION "${runtime_modifications}"
    )
  endfunction()

  add_executable(
    dsk-smoke-tests
    tests/smoke-tests.cpp
    src/core/encoder-profile-manager.cpp
    src/core/encoder-profile-manager.hpp
    src/core/experimental-features.hpp
    src/core/layout-manager.cpp
    src/core/layout-manager.hpp
    src/core/output-target.cpp
    src/core/output-target.hpp
    src/core/oauth-provider.cpp
    src/core/oauth-provider.hpp
    src/core/platform-preset-registry.cpp
    src/core/platform-preset-registry.hpp
    src/core/settings-codec.cpp
    src/core/settings-codec.hpp
    src/core/secret-store.cpp
    src/core/secret-store.hpp
    src/core/youtube-broadcast-selector.cpp
    src/core/youtube-broadcast-selector.hpp
    src/core/youtube-archive-rotation.cpp
    src/core/youtube-archive-rotation.hpp
    src/core/youtube-stream-options.cpp
    src/core/youtube-stream-options.hpp
    src/core/vertical-layout-geometry.hpp
  )

  target_include_directories(dsk-smoke-tests PRIVATE src "${DSK_GENERATED_INCLUDE_DIR}")
  target_link_libraries(dsk-smoke-tests PRIVATE Qt6::Core)
  if(WIN32)
    target_compile_definitions(dsk-smoke-tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(dsk-smoke-tests PRIVATE Advapi32)
  endif()

  add_test(NAME dsk-smoke-tests COMMAND dsk-smoke-tests)
  set_tests_properties(
    dsk-smoke-tests
    PROPERTIES
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      LABELS "unit;no-obs"
  )
  dsk_configure_test_runtime(dsk-smoke-tests Qt6::Core)

  add_executable(
    dsk-youtube-archive-rotation-simulation-tests
    tests/youtube-archive-rotation-simulation-tests.cpp
    src/core/youtube-archive-rotation.cpp
    src/core/youtube-archive-rotation.hpp
  )
  target_include_directories(
    dsk-youtube-archive-rotation-simulation-tests
    PRIVATE src "${DSK_GENERATED_INCLUDE_DIR}"
  )
  target_link_libraries(dsk-youtube-archive-rotation-simulation-tests PRIVATE Qt6::Core)
  if(WIN32)
    target_compile_definitions(
      dsk-youtube-archive-rotation-simulation-tests
      PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
    )
  endif()
  add_test(
    NAME dsk-youtube-archive-rotation-simulation-tests
    COMMAND dsk-youtube-archive-rotation-simulation-tests
  )
  set_tests_properties(
    dsk-youtube-archive-rotation-simulation-tests
    PROPERTIES LABELS "unit;simulation;stability;no-obs"
  )
  dsk_configure_test_runtime(dsk-youtube-archive-rotation-simulation-tests Qt6::Core)

  add_test(
    NAME dsk-no-tools-menu-test
    COMMAND
      "${CMAKE_COMMAND}"
      "-DPLUGIN_MAIN=${CMAKE_CURRENT_SOURCE_DIR}/src/plugin-main.cpp"
      "-DCOMMENT_VIEWER_INTEGRATION_CPP=${CMAKE_CURRENT_SOURCE_DIR}/src/integration/comment-viewer-integration.cpp"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/no-tools-menu-test.cmake"
  )
  set_tests_properties(dsk-no-tools-menu-test PROPERTIES LABELS "unit;ui;no-obs")

  add_test(
    NAME dsk-no-comment-viewer-setting-test
    COMMAND
      "${CMAKE_COMMAND}"
      "-DMAIN_DOCK_CPP=${CMAKE_CURRENT_SOURCE_DIR}/src/ui/main-dock.cpp"
      "-DMAIN_DOCK_HPP=${CMAKE_CURRENT_SOURCE_DIR}/src/ui/main-dock.hpp"
      "-DPLUGIN_MAIN=${CMAKE_CURRENT_SOURCE_DIR}/src/plugin-main.cpp"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/no-comment-viewer-setting-test.cmake"
  )
  set_tests_properties(dsk-no-comment-viewer-setting-test PROPERTIES LABELS "unit;ui;no-obs")

  add_test(
    NAME dsk-comment-viewer-dock-rendering-test
    COMMAND
      "${CMAKE_COMMAND}"
      "-DCOMMENT_VIEWER_INTEGRATION_CPP=${CMAKE_CURRENT_SOURCE_DIR}/src/integration/comment-viewer-integration.cpp"
      "-DCOMMENT_VIEWER_CONTRACT_HPP=${CMAKE_CURRENT_SOURCE_DIR}/src/core/comment-viewer-contract.hpp"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/comment-viewer-dock-rendering-test.cmake"
  )
  set_tests_properties(dsk-comment-viewer-dock-rendering-test PROPERTIES LABELS "unit;ui;no-obs")

  add_test(
    NAME dsk-stream-controls-refresh-test
    COMMAND
      "${CMAKE_COMMAND}"
      "-DSTREAM_CONTROLS_CPP=${CMAKE_CURRENT_SOURCE_DIR}/src/ui/stream-controls-dock.cpp"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/stream-controls-refresh-test.cmake"
  )
  set_tests_properties(dsk-stream-controls-refresh-test PROPERTIES LABELS "unit;ui;performance;no-obs")

  add_test(
    NAME dsk-streaming-dock-surface-test
    COMMAND
      "${CMAKE_COMMAND}"
      "-DMAIN_DOCK_CPP=${CMAKE_CURRENT_SOURCE_DIR}/src/ui/main-dock.cpp"
      "-DMAIN_DOCK_HPP=${CMAKE_CURRENT_SOURCE_DIR}/src/ui/main-dock.hpp"
      "-DSTREAM_CONTROLS_CPP=${CMAKE_CURRENT_SOURCE_DIR}/src/ui/stream-controls-dock.cpp"
      "-DSTREAM_CONTROLS_HPP=${CMAKE_CURRENT_SOURCE_DIR}/src/ui/stream-controls-dock.hpp"
      "-DSTREAM_CONTROL_ASSETS_HPP=${CMAKE_CURRENT_SOURCE_DIR}/src/ui/stream-control-assets.hpp"
      "-DSTREAM_CONTROL_VISUALS_HPP=${CMAKE_CURRENT_SOURCE_DIR}/src/ui/stream-control-visuals.hpp"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/streaming-dock-surface-test.cmake"
  )
  set_tests_properties(dsk-streaming-dock-surface-test PROPERTIES LABELS "unit;ui;no-obs")

  add_test(
    NAME dsk-install-user-plugin-test
    COMMAND
      "${CMAKE_COMMAND}"
      "-DINSTALL_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/install-user-plugin.ps1"
      "-DINSTALLER_DEFINITION=${CMAKE_CURRENT_SOURCE_DIR}/installer/dsk-multistream.iss"
      "-DVALIDATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/validate-package.ps1"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/install-user-plugin-test.cmake"
  )
  set_tests_properties(dsk-install-user-plugin-test PROPERTIES LABELS "unit;packaging;no-obs")

  add_test(
    NAME dsk-vertical-list-actions-test
    COMMAND
      "${CMAKE_COMMAND}"
      "-DVERTICAL_EDITOR_CPP=${CMAKE_CURRENT_SOURCE_DIR}/src/ui/vertical-layout-editor.cpp"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/vertical-list-actions-test.cmake"
  )
  set_tests_properties(dsk-vertical-list-actions-test PROPERTIES LABELS "unit;ui;no-obs")

  add_test(
    NAME dsk-vertical-preview-lifecycle-test
    COMMAND
      "${CMAKE_COMMAND}"
      "-DVERTICAL_EDITOR_CPP=${CMAKE_CURRENT_SOURCE_DIR}/src/ui/vertical-layout-editor.cpp"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/vertical-preview-lifecycle-test.cmake"
  )
  set_tests_properties(dsk-vertical-preview-lifecycle-test PROPERTIES LABELS "unit;ui;stability;no-obs")

  add_test(
    NAME dsk-obs-startup-selection-test
    COMMAND
      "${CMAKE_COMMAND}"
      "-DPLUGIN_MAIN=${CMAKE_CURRENT_SOURCE_DIR}/src/plugin-main.cpp"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/obs-startup-selection-test.cmake"
  )
  set_tests_properties(dsk-obs-startup-selection-test PROPERTIES LABELS "unit;ui;stability;no-obs")

  add_test(
    NAME dsk-twitch-dual-format-contract-test
    COMMAND
      "${CMAKE_COMMAND}"
      "-DOUTPUT_MANAGER_CPP=${CMAKE_CURRENT_SOURCE_DIR}/src/core/output-manager.cpp"
      "-DSTREAM_CONTROLS_DOCK_CPP=${CMAKE_CURRENT_SOURCE_DIR}/src/ui/stream-controls-dock.cpp"
      "-DVERTICAL_SCENE_BUILDER_CPP=${CMAKE_CURRENT_SOURCE_DIR}/src/core/vertical-scene-builder.cpp"
      "-DPLUGIN_MAIN=${CMAKE_CURRENT_SOURCE_DIR}/src/plugin-main.cpp"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/twitch-dual-format-contract-test.cmake"
  )
  set_tests_properties(
    dsk-twitch-dual-format-contract-test
    PROPERTIES LABELS "unit;twitch;streaming;stability;no-obs"
  )

  add_test(
    NAME dsk-release-e2e-hooks-test
    COMMAND
      "${CMAKE_COMMAND}"
      "-DCMAKE_LIST=${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
      "-DPLUGIN_MAIN=${CMAKE_CURRENT_SOURCE_DIR}/src/plugin-main.cpp"
      "-DVERTICAL_EDITOR_HPP=${CMAKE_CURRENT_SOURCE_DIR}/src/ui/vertical-layout-editor.hpp"
      "-DVERTICAL_EDITOR_CPP=${CMAKE_CURRENT_SOURCE_DIR}/src/ui/vertical-layout-editor.cpp"
      "-DINSTALLER_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/build-windows-installer.ps1"
      "-DPLUGIN_DLL=$<TARGET_FILE:obs-dsk-multistream>"
      "-DHOOKS_ENABLED=${DSK_INCLUDE_E2E_HOOKS}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/release-e2e-hooks-test.cmake"
  )
  set_tests_properties(dsk-release-e2e-hooks-test PROPERTIES LABELS "unit;packaging;no-obs")

  add_test(
    NAME dsk-distribution-compliance-test
    COMMAND
      "${CMAKE_COMMAND}"
      "-DINNO_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/installer/dsk-multistream.iss"
      "-DBUILD_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/build-windows-installer.ps1"
      "-DINSTALLER_E2E_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/test-windows-installer.ps1"
      "-DVALIDATOR=${CMAKE_CURRENT_SOURCE_DIR}/scripts/validate-installer-layout.ps1"
      "-DBETA_GUIDE=${CMAKE_CURRENT_SOURCE_DIR}/docs/beta-distribution.md"
      "-DPRIVACY=${CMAKE_CURRENT_SOURCE_DIR}/docs/privacy.md"
      "-DNOTICE=${CMAKE_CURRENT_SOURCE_DIR}/docs/third-party-notices.md"
      "-DREMOVAL_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/remove-user-data.ps1"
      "-DSIGNING_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/sign-windows-release.ps1"
      "-DEXTERNAL_SIGNING_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/external-windows-signing.ps1"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/distribution-compliance-test.cmake"
  )
  set_tests_properties(dsk-distribution-compliance-test PROPERTIES LABELS "unit;packaging;compliance;no-obs")

  if(WIN32)
    add_test(
      NAME dsk-windows-pe-hardening-test
      COMMAND
        "$ENV{SystemRoot}/System32/WindowsPowerShell/v1.0/powershell.exe"
        -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/windows-pe-hardening-test.ps1"
        -PluginDll "$<TARGET_FILE:obs-dsk-multistream>"
    )
    set_tests_properties(dsk-windows-pe-hardening-test PROPERTIES LABELS "unit;security;packaging;no-obs")

    add_test(
      NAME dsk-remove-user-data-test
      COMMAND
        "$ENV{SystemRoot}/System32/WindowsPowerShell/v1.0/powershell.exe"
        -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/remove-user-data-test.ps1"
        -RemovalScript "${CMAKE_CURRENT_SOURCE_DIR}/scripts/remove-user-data.ps1"
        -TestRoot "${CMAKE_CURRENT_BINARY_DIR}/remove-user-data-test"
    )
    set_tests_properties(dsk-remove-user-data-test PROPERTIES LABELS "unit;packaging;security;no-obs")

    add_test(
      NAME dsk-signing-certificate-eku-test
      COMMAND
        "$ENV{SystemRoot}/System32/WindowsPowerShell/v1.0/powershell.exe"
        -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/signing-certificate-eku-test.ps1"
        -SigningCertificateUtils "${CMAKE_CURRENT_SOURCE_DIR}/scripts/signing-certificate-utils.ps1"
    )
    set_tests_properties(dsk-signing-certificate-eku-test PROPERTIES LABELS "unit;security;packaging;no-obs")
  endif()

  add_executable(
    dsk-ui-smoke-tests
    tests/ui-smoke-tests.cpp
    src/core/diagnostics.hpp
    src/core/output-target.cpp
    src/core/output-target.hpp
    src/core/platform-preset-registry.cpp
    src/core/platform-preset-registry.hpp
    src/core/oauth-provider.cpp
    src/core/oauth-provider.hpp
    src/core/http-client.cpp
    src/core/http-client.hpp
    src/core/oauth-connector.cpp
    src/core/oauth-connector.hpp
    src/core/youtube-stream-options.cpp
    src/core/youtube-stream-options.hpp
    src/ui/target-edit-dialog.cpp
    src/ui/target-edit-dialog.hpp
  )

  target_include_directories(dsk-ui-smoke-tests PRIVATE src "${DSK_GENERATED_INCLUDE_DIR}")
  target_link_libraries(dsk-ui-smoke-tests PRIVATE Qt6::Core Qt6::Network Qt6::Widgets CURL::libcurl)
  target_compile_definitions(dsk-ui-smoke-tests PRIVATE DSK_DIAGNOSTICS_QT_FALLBACK)
  if(DSK_PUBLISHER_RELEASE)
    target_compile_definitions(dsk-ui-smoke-tests PRIVATE DSK_PUBLISHER_RELEASE)
  endif()
  if(WIN32)
    target_compile_definitions(dsk-ui-smoke-tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  endif()
  set_target_properties(
    dsk-ui-smoke-tests
    PROPERTIES
      AUTOMOC ON
      AUTOUIC OFF
      AUTORCC OFF
  )

  add_test(NAME dsk-ui-smoke-tests COMMAND dsk-ui-smoke-tests)
  set_tests_properties(
    dsk-ui-smoke-tests
    PROPERTIES
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      LABELS "unit;ui;no-obs"
  )
  dsk_configure_test_runtime(dsk-ui-smoke-tests Qt6::Widgets)

  add_executable(
    dsk-http-client-tests
    tests/http-client-tests.cpp
    src/core/http-client.cpp
    src/core/http-client.hpp
  )

  target_include_directories(dsk-http-client-tests PRIVATE src)
  target_link_libraries(dsk-http-client-tests PRIVATE Qt6::Core Qt6::Network CURL::libcurl)
  if(WIN32)
    target_compile_definitions(dsk-http-client-tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  endif()

  add_test(NAME dsk-http-client-tests COMMAND dsk-http-client-tests)
  set_tests_properties(
    dsk-http-client-tests
    PROPERTIES
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      LABELS "unit;http;no-obs"
  )
  dsk_configure_test_runtime(dsk-http-client-tests Qt6::Network)

  add_executable(
    dsk-settings-store-tests
    tests/settings-store-tests.cpp
    src/core/layout-manager.cpp
    src/core/layout-manager.hpp
    src/core/output-target.cpp
    src/core/output-target.hpp
    src/core/settings-codec.cpp
    src/core/settings-codec.hpp
    src/core/settings-store.cpp
    src/core/settings-store.hpp
    src/core/secret-store.cpp
    src/core/secret-store.hpp
  )

  target_include_directories(dsk-settings-store-tests PRIVATE src)
  target_link_libraries(
    dsk-settings-store-tests
    PRIVATE
      Qt6::Core
  )
  target_compile_definitions(
    dsk-settings-store-tests
    PRIVATE
      DSK_DIAGNOSTICS_QT_FALLBACK
      DSK_SETTINGS_STORE_STANDALONE
  )
  if(WIN32)
    target_compile_definitions(dsk-settings-store-tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(dsk-settings-store-tests PRIVATE Advapi32)
  endif()

  add_test(NAME dsk-settings-store-tests COMMAND dsk-settings-store-tests)
  set_tests_properties(
    dsk-settings-store-tests
    PROPERTIES
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      LABELS "unit;settings;no-obs"
  )
  dsk_configure_test_runtime(dsk-settings-store-tests Qt6::Core)
endif()
