if(NOT DEFINED INSTALL_SCRIPT OR NOT EXISTS "${INSTALL_SCRIPT}")
  message(FATAL_ERROR "INSTALL_SCRIPT must point to install-user-plugin.ps1")
endif()
if(NOT DEFINED INSTALLER_DEFINITION OR NOT EXISTS "${INSTALLER_DEFINITION}")
  message(FATAL_ERROR "INSTALLER_DEFINITION must point to dsk-multistream.iss")
endif()
if(NOT DEFINED VALIDATE_SCRIPT OR NOT EXISTS "${VALIDATE_SCRIPT}")
  message(FATAL_ERROR "VALIDATE_SCRIPT must point to validate-package.ps1")
endif()

file(READ "${INSTALL_SCRIPT}" install_script)

foreach(required_text
    preservedInstallerFiles
    unins000.exe
    unins000.dat
    uiSource
    InstalledDllName
    dsk-package-manifest.json
    Get-FileHash
    ConvertTo-Json)
  if(NOT install_script MATCHES "${required_text}")
    message(FATAL_ERROR "Developer installation must preserve uninstall support and regenerate its manifest: ${required_text}")
  endif()
endforeach()

if(NOT install_script MATCHES "Copy-Item -LiteralPath \\$uiSource")
  message(FATAL_ERROR "Developer installation must copy generated UI assets")
endif()
if(NOT install_script MATCHES "Join-Path \\$stageBinDir \\$InstalledDllName")
  message(FATAL_ERROR "Developer installation must preserve an explicitly selected development DLL name")
endif()
if(NOT install_script MATCHES "InstalledDllName -notmatch")
  message(FATAL_ERROR "Developer installation must reject unsafe DLL file names")
endif()

file(READ "${INSTALLER_DEFINITION}" installer_definition)
if(NOT installer_definition MATCHES "data\\\\ui\\\\\\*")
  message(FATAL_ERROR "Distribution installer must include generated UI assets")
endif()

file(READ "${VALIDATE_SCRIPT}" validate_script)
foreach(required_text
    "InstalledDllName = \"obs-dsk-multistream.dll\""
    "InstalledDllName -notmatch"
    "-Filter \\$InstalledDllName"
    "@\\(\"twitch\", \"youtube\", \"kick\", \"custom\"\\)")
  if(NOT validate_script MATCHES "${required_text}")
    message(FATAL_ERROR "Package validation must support safe development slots and public presets: ${required_text}")
  endif()
endforeach()

if(validate_script MATCHES "@\\(\"twitch\", \"youtube\", \"kick\", \"tiktok\", \"custom\"\\)")
  message(FATAL_ERROR "Public package validation must not require the private TikTok preset")
endif()

message(STATUS "Developer installation preserves uninstall support and refreshes its manifest")
