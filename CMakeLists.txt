cmake_minimum_required(VERSION 3.28...3.30)

project(obs-dsk-multistream VERSION 0.1.0 LANGUAGES CXX)

# CMake 4.2 can mojibake the UTF-8 /showIncludes prefix from Japanese MSVC,
# leaving Ninja with no header dependencies and stale incremental objects.
if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
  set(CMAKE_CL_SHOWINCLUDES_PREFIX "メモ: インクルード ファイル: ")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Network Widgets)
find_package(CURL CONFIG QUIET)
if(NOT CURL_FOUND)
  find_package(CURL REQUIRED)
endif()

option(DSK_ENABLE_OBS_CANVAS_API "Enable OBS canvas API path for real vertical video output" ON)
option(DSK_BUILD_TESTS "Build DSK non-OBS smoke tests" ON)
set(DSK_TEST_RUNTIME_DIR "" CACHE PATH "Signed OBS runtime directory used by Windows tests")
set(DSK_OAUTH_APP_CONFIG "" CACHE FILEPATH "Publisher OAuth application JSON used at build time")
if(WIN32 AND NOT DSK_TEST_RUNTIME_DIR AND IS_DIRECTORY "C:/Program Files/obs-studio/bin/64bit")
  set(DSK_TEST_RUNTIME_DIR "C:/Program Files/obs-studio/bin/64bit" CACHE PATH "Signed OBS runtime directory used by Windows tests" FORCE)
endif()

set(DSK_BUNDLED_YOUTUBE_CLIENT_ID "")
set(DSK_BUNDLED_YOUTUBE_CLIENT_SECRET "")
if(DSK_OAUTH_APP_CONFIG)
  if(NOT EXISTS "${DSK_OAUTH_APP_CONFIG}")
    message(FATAL_ERROR "DSK_OAUTH_APP_CONFIG does not exist: ${DSK_OAUTH_APP_CONFIG}")
  endif()

  file(READ "${DSK_OAUTH_APP_CONFIG}" dsk_oauth_app_config_json)
  string(JSON DSK_BUNDLED_YOUTUBE_CLIENT_ID ERROR_VARIABLE dsk_youtube_client_id_error
    GET "${dsk_oauth_app_config_json}" youtube clientId)
  string(JSON DSK_BUNDLED_YOUTUBE_CLIENT_SECRET ERROR_VARIABLE dsk_youtube_client_secret_error
    GET "${dsk_oauth_app_config_json}" youtube clientSecret)
  if(NOT dsk_youtube_client_id_error STREQUAL "NOTFOUND" OR
     NOT dsk_youtube_client_secret_error STREQUAL "NOTFOUND")
    message(FATAL_ERROR "DSK_OAUTH_APP_CONFIG must contain youtube.clientId and youtube.clientSecret")
  endif()

  string(LENGTH "${DSK_BUNDLED_YOUTUBE_CLIENT_ID}" dsk_youtube_client_id_length)
  string(LENGTH "${DSK_BUNDLED_YOUTUBE_CLIENT_SECRET}" dsk_youtube_client_secret_length)
  if(dsk_youtube_client_id_length LESS 20 OR dsk_youtube_client_id_length GREATER 300 OR
     NOT DSK_BUNDLED_YOUTUBE_CLIENT_ID MATCHES "^[A-Za-z0-9._-]+$")
    message(FATAL_ERROR "youtube.clientId in DSK_OAUTH_APP_CONFIG has an invalid format")
  endif()
  if(dsk_youtube_client_secret_length LESS 10 OR dsk_youtube_client_secret_length GREATER 300 OR
     NOT DSK_BUNDLED_YOUTUBE_CLIENT_SECRET MATCHES "^[A-Za-z0-9._-]+$")
    message(FATAL_ERROR "youtube.clientSecret in DSK_OAUTH_APP_CONFIG has an invalid format")
  endif()

  message(STATUS "DSK publisher YouTube OAuth application: configured")
else()
  message(STATUS "DSK publisher YouTube OAuth application: not configured")
endif()

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
    src/core/e2e-auto-runner.cpp
    src/core/e2e-auto-runner.hpp
    src/core/encoder-profile-manager.cpp
    src/core/encoder-profile-manager.hpp
    src/core/http-client.cpp
    src/core/http-client.hpp
    src/core/layout-manager.cpp
    src/core/layout-manager.hpp
    src/core/output-manager.cpp
    src/core/output-manager.hpp
    src/core/output-runtime-status.hpp
    src/core/oauth-connector.cpp
    src/core/oauth-connector.hpp
    src/core/oauth-provider.cpp
    src/core/oauth-provider.hpp
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
    src/core/vertical-scene-builder.cpp
    src/core/vertical-scene-builder.hpp
    src/core/vertical-layout-geometry.hpp
    src/core/youtube-broadcast-selector.cpp
    src/core/youtube-broadcast-selector.hpp
    src/ui/main-dock.cpp
    src/ui/main-dock.hpp
    src/ui/scene-router-dock.cpp
    src/ui/scene-router-dock.hpp
    src/ui/stream-controls-dock.cpp
    src/ui/stream-controls-dock.hpp
    src/ui/target-edit-dialog.cpp
    src/ui/target-edit-dialog.hpp
    src/ui/vertical-layout-editor.cpp
    src/ui/vertical-layout-editor.hpp
)

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
  target_compile_definitions(obs-dsk-multistream PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(obs-dsk-multistream PRIVATE Advapi32)
endif()

if(DSK_ENABLE_OBS_CANVAS_API)
  target_compile_definitions(obs-dsk-multistream PRIVATE DSK_ENABLE_OBS_CANVAS_API)
endif()

include(GNUInstallDirs)

install(TARGETS obs-dsk-multistream LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/obs-plugins")
install(DIRECTORY data/ DESTINATION "${CMAKE_INSTALL_DATADIR}/obs/obs-plugins/obs-dsk-multistream")

if(DSK_BUILD_TESTS)
  enable_testing()

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
    src/ui/target-edit-dialog.cpp
    src/ui/target-edit-dialog.hpp
  )

  target_include_directories(dsk-ui-smoke-tests PRIVATE src "${DSK_GENERATED_INCLUDE_DIR}")
  target_link_libraries(dsk-ui-smoke-tests PRIVATE Qt6::Core Qt6::Network Qt6::Widgets CURL::libcurl)
  target_compile_definitions(dsk-ui-smoke-tests PRIVATE DSK_DIAGNOSTICS_QT_FALLBACK)
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
