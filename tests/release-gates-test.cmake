foreach(required_file IN ITEMS CMAKE_LIST CONFIGURE_SCRIPT BUILD_SCRIPT EXTERNAL_SIGNING_SCRIPT)
  if(NOT DEFINED ${required_file} OR NOT EXISTS "${${required_file}}")
    message(FATAL_ERROR "${required_file} must point to an existing file")
  endif()
endforeach()

file(READ "${CMAKE_LIST}" cmake_source)
if(NOT cmake_source MATCHES "option\\(DSK_PUBLISHER_RELEASE \"Require publisher OAuth credentials for release builds\" OFF\\)")
  message(FATAL_ERROR "CMake must expose the opt-in publisher release gate")
endif()
if(NOT cmake_source MATCHES "DSK_PUBLISHER_RELEASE")
  message(FATAL_ERROR "CMake must enforce publisher release requirements")
endif()

file(READ "${CONFIGURE_SCRIPT}" configure_source)
if(NOT configure_source MATCHES "\\[switch\\]\\$PublisherRelease")
  message(FATAL_ERROR "Windows configure script must expose PublisherRelease")
endif()
if(NOT configure_source MATCHES "DDSK_PUBLISHER_RELEASE")
  message(FATAL_ERROR "Windows configure script must pass DSK_PUBLISHER_RELEASE")
endif()

string(FIND "${configure_source}" "DDSK_PUBLISHER_RELEASE=$publisherReleaseFlag" publisher_flag_index)
if(publisher_flag_index LESS 0)
  message(FATAL_ERROR "PublisherRelease switch must use a distinct publisherReleaseFlag value")
endif()
if(configure_source MATCHES "[Pp]ublisher[Rr]elease[ \t]*=")
  message(FATAL_ERROR "Do not assign a string to the case-insensitive PublisherRelease switch")
endif()

file(READ "${BUILD_SCRIPT}" build_source)
if(NOT build_source MATCHES "\\[switch\\]\\$RequirePublisherRelease")
  message(FATAL_ERROR "Installer script must expose RequirePublisherRelease")
endif()
if(NOT build_source MATCHES "oauth-publisher-config")
  message(FATAL_ERROR "Installer script must validate generated publisher OAuth config")
endif()

# The runner image is not a toolchain pin: windows-2022 can carry an older ISCC.
get_filename_component(source_root "${CMAKE_LIST}" DIRECTORY)
file(READ "${source_root}/.github/workflows/windows-release.yml" workflow_source)
string(REGEX MATCH "ExpectedIsccVersion = \"([0-9]+\\.[0-9]+\\.[0-9]+)\"" compiler_pin "${build_source}")
set(expected_inno_version "${CMAKE_MATCH_1}")
if(expected_inno_version STREQUAL "")
  message(FATAL_ERROR "Installer script must declare its required Inno Setup version")
endif()
foreach(required_guard IN ITEMS
    "- name: Install pinned Inno Setup"
    "id: inno"
    "$version = '${expected_inno_version}'"
    "https://github.com/jrsoftware/issrc/releases/download/is-6_7_3/innosetup-6.7.3.exe"
    "9C73C3BAE7ED48D44112A0F48E66742C00090BDB5BEF71D9D3C056C66E97B732"
    "Get-FileHash -LiteralPath $downloadPath -Algorithm SHA256"
    "Get-AuthenticodeSignature -LiteralPath $downloadPath"
    "[Management.Automation.SignatureStatus]::Valid"
    "'Pyrsys B.V.'"
    "$signature.TimeStamperCertificate"
    "Compiler engine version: Inno Setup"
    "$compilerVersionMatch.Groups[1].Value -ne $version"
    "'Output=no'"
    "$probe | & $iscc '-'"
    "'/CURRENTUSER'"
    "-WindowStyle Hidden"
    "Join-Path $env:RUNNER_TEMP"
    [=[-IsccPath '${{ steps.inno.outputs.iscc }}']=])
  string(FIND "${workflow_source}" "${required_guard}" guard_position)
  if(guard_position LESS 0)
    message(FATAL_ERROR "Windows CI must provision and explicitly use the verified compiler: ${required_guard}")
  endif()
endforeach()
foreach(verification IN ITEMS "Get-FileHash" "Get-AuthenticodeSignature")
  string(FIND "${workflow_source}" "${verification}" verification_position)
  string(FIND "${workflow_source}" "Start-Process -FilePath $downloadPath" execution_position)
  if(execution_position LESS 0 OR verification_position GREATER execution_position)
    message(FATAL_ERROR "Windows CI must verify the Inno download before executing it")
  endif()
endforeach()
string(FIND "${workflow_source}" "- name: Install pinned Inno Setup" provisioning_position)
string(FIND "${workflow_source}" "- name: Build installer" packaging_position)
if(provisioning_position GREATER packaging_position)
  message(FATAL_ERROR "Windows CI must provision Inno Setup before packaging")
endif()

file(READ "${EXTERNAL_SIGNING_SCRIPT}" signing_source)
if(NOT signing_source MATCHES "ExpectedSignerThumbprint")
  message(FATAL_ERROR "External signing requires an expected signer thumbprint")
endif()
if(signing_source MATCHES "\\$expectedSigner = \\$pluginSignature")
  message(FATAL_ERROR "External signing must not derive the expected signer from the plugin")
endif()
if(NOT signing_source MATCHES "same signer")
  message(FATAL_ERROR "External signing must retain same-signer validation")
endif()

message(STATUS "Publisher OAuth and pinned signer release gates are present")
