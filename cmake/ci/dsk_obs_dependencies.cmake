include_guard(GLOBAL)

function(_dsk_download_pinned url output_path sha256 label)
  if(EXISTS "${output_path}")
    file(SHA256 "${output_path}" existing_hash)
    if(existing_hash STREQUAL sha256)
      message(STATUS "Using cached ${label}: ${output_path}")
      return()
    endif()
    file(REMOVE "${output_path}")
  endif()

  message(STATUS "Downloading ${label}: ${url}")
  file(
    DOWNLOAD "${url}" "${output_path}"
    EXPECTED_HASH "SHA256=${sha256}"
    STATUS download_status
    SHOW_PROGRESS
    TLS_VERIFY ON
  )
  list(GET download_status 0 download_code)
  list(GET download_status 1 download_message)
  if(NOT download_code EQUAL 0)
    file(REMOVE "${output_path}")
    message(FATAL_ERROR "Failed to download ${label}: ${download_message}")
  endif()
endfunction()

function(_dsk_extract_pinned archive destination stamp_path sha256 label)
  if(EXISTS "${stamp_path}" AND EXISTS "${destination}")
    file(READ "${stamp_path}" installed_hash)
    string(STRIP "${installed_hash}" installed_hash)
    if(installed_hash STREQUAL sha256)
      message(STATUS "Using extracted ${label}: ${destination}")
      return()
    endif()
  endif()

  file(REMOVE_RECURSE "${destination}")
  file(MAKE_DIRECTORY "${destination}")
  message(STATUS "Extracting ${label}")
  file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${destination}")
  file(WRITE "${stamp_path}" "${sha256}\n")
endfunction()

function(dsk_bootstrap_obs_dependencies)
  if(NOT WIN32)
    message(FATAL_ERROR "Pinned OBS dependency bootstrap currently supports Windows only")
  endif()

  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/buildspec.json" buildspec)
  string(JSON obs_version GET "${buildspec}" dependencies obs-studio version)
  string(JSON obs_base_url GET "${buildspec}" dependencies obs-studio baseUrl)
  string(JSON obs_hash GET "${buildspec}" dependencies obs-studio hashes windows-x64)
  string(JSON prebuilt_version GET "${buildspec}" dependencies prebuilt version)
  string(JSON prebuilt_base_url GET "${buildspec}" dependencies prebuilt baseUrl)
  string(JSON prebuilt_hash GET "${buildspec}" dependencies prebuilt hashes windows-x64)
  string(JSON qt_version GET "${buildspec}" dependencies qt6 version)
  string(JSON qt_base_url GET "${buildspec}" dependencies qt6 baseUrl)
  string(JSON qt_hash GET "${buildspec}" dependencies qt6 hashes windows-x64)

  set(deps_root "${CMAKE_CURRENT_SOURCE_DIR}/.deps")
  file(MAKE_DIRECTORY "${deps_root}")

  set(prebuilt_archive "${deps_root}/windows-deps-${prebuilt_version}-x64.zip")
  set(prebuilt_root "${deps_root}/obs-deps-${prebuilt_version}-x64")
  _dsk_download_pinned(
    "${prebuilt_base_url}/${prebuilt_version}/windows-deps-${prebuilt_version}-x64.zip"
    "${prebuilt_archive}" "${prebuilt_hash}" "OBS build dependencies"
  )
  _dsk_extract_pinned(
    "${prebuilt_archive}" "${prebuilt_root}"
    "${deps_root}/.dsk-prebuilt-x64.sha256" "${prebuilt_hash}" "OBS build dependencies"
  )

  set(qt_archive "${deps_root}/windows-deps-qt6-${qt_version}-x64.zip")
  set(qt_root "${deps_root}/obs-deps-qt6-${qt_version}-x64")
  _dsk_download_pinned(
    "${qt_base_url}/${qt_version}/windows-deps-qt6-${qt_version}-x64.zip"
    "${qt_archive}" "${qt_hash}" "OBS Qt dependencies"
  )
  _dsk_extract_pinned(
    "${qt_archive}" "${qt_root}"
    "${deps_root}/.dsk-qt6-x64.sha256" "${qt_hash}" "OBS Qt dependencies"
  )

  set(obs_archive "${deps_root}/${obs_version}.zip")
  set(obs_source_root "${deps_root}/obs-studio-${obs_version}")
  _dsk_download_pinned(
    "${obs_base_url}/${obs_version}.zip" "${obs_archive}" "${obs_hash}" "OBS Studio sources"
  )
  if(NOT EXISTS "${deps_root}/.dsk-obs-source.sha256")
    file(ARCHIVE_EXTRACT INPUT "${obs_archive}" DESTINATION "${deps_root}")
    file(WRITE "${deps_root}/.dsk-obs-source.sha256" "${obs_hash}\n")
  else()
    file(READ "${deps_root}/.dsk-obs-source.sha256" installed_obs_hash)
    string(STRIP "${installed_obs_hash}" installed_obs_hash)
    if(NOT installed_obs_hash STREQUAL obs_hash OR NOT EXISTS "${obs_source_root}/CMakeLists.txt")
      file(REMOVE_RECURSE "${obs_source_root}")
      file(ARCHIVE_EXTRACT INPUT "${obs_archive}" DESTINATION "${deps_root}")
      file(WRITE "${deps_root}/.dsk-obs-source.sha256" "${obs_hash}\n")
    endif()
  endif()

  set(obs_sdk_root "${deps_root}/obs-sdk")
  set(obs_build_root "${obs_source_root}/build_x64")
  set(obs_sdk_stamp "${obs_hash};${prebuilt_hash};${qt_hash}")
  set(obs_sdk_stamp_path "${obs_sdk_root}/.dsk-build-inputs")
  set(rebuild_obs_sdk TRUE)
  if(EXISTS "${obs_sdk_stamp_path}" AND
     EXISTS "${obs_sdk_root}/cmake/libobsConfig.cmake" AND
     EXISTS "${obs_sdk_root}/cmake/obs-frontend-apiConfig.cmake")
    file(READ "${obs_sdk_stamp_path}" installed_sdk_stamp)
    string(STRIP "${installed_sdk_stamp}" installed_sdk_stamp)
    if(installed_sdk_stamp STREQUAL obs_sdk_stamp)
      set(rebuild_obs_sdk FALSE)
    endif()
  endif()

  set(obs_dependency_prefixes "${prebuilt_root};${qt_root}")
  if(rebuild_obs_sdk)
    file(REMOVE_RECURSE "${obs_build_root}" "${obs_sdk_root}")
    message(STATUS "Configuring the pinned OBS development SDK")
    execute_process(
      COMMAND
        "${CMAKE_COMMAND}" -S "${obs_source_root}" -B "${obs_build_root}"
        -G "${CMAKE_GENERATOR}" -A "${CMAKE_GENERATOR_PLATFORM}"
        "-DOBS_CMAKE_VERSION:STRING=3.0.0"
        "-DENABLE_PLUGINS:BOOL=OFF"
        "-DENABLE_FRONTEND:BOOL=OFF"
        "-DOBS_VERSION_OVERRIDE:STRING=${obs_version}"
        "-DCMAKE_PREFIX_PATH=${obs_dependency_prefixes}"
        "-DCMAKE_SYSTEM_VERSION=${CMAKE_SYSTEM_VERSION}"
        --fresh
      COMMAND_ERROR_IS_FATAL ANY
    )

    message(STATUS "Building the pinned OBS development SDK")
    execute_process(
      COMMAND "${CMAKE_COMMAND}" --build "${obs_build_root}" --target obs-frontend-api --config Release --parallel
      COMMAND_ERROR_IS_FATAL ANY
    )

    message(STATUS "Installing the pinned OBS development SDK")
    execute_process(
      COMMAND
        "${CMAKE_COMMAND}" --install "${obs_build_root}" --component Development
        --config Release --prefix "${obs_sdk_root}"
      COMMAND_ERROR_IS_FATAL ANY
    )
    file(WRITE "${obs_sdk_stamp_path}" "${obs_sdk_stamp}\n")
  else()
    message(STATUS "Using cached pinned OBS development SDK: ${obs_sdk_root}")
  endif()

  list(PREPEND CMAKE_PREFIX_PATH "${obs_sdk_root}" "${prebuilt_root}" "${qt_root}")
  list(REMOVE_DUPLICATES CMAKE_PREFIX_PATH)
  set(ENV{PATH} "${qt_root}/bin;${prebuilt_root}/bin;$ENV{PATH}")
  set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" CACHE PATH "Pinned OBS dependency prefixes" FORCE)
  set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" PARENT_SCOPE)
endfunction()
