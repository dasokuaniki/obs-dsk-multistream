foreach(required_input IN ITEMS
    INNO_SCRIPT
    BUILD_SCRIPT
    INSTALLER_E2E_SCRIPT
    VALIDATOR
    BETA_GUIDE
    PRIVACY
    NOTICE
    REMOVAL_SCRIPT
    SIGNING_SCRIPT
    EXTERNAL_SIGNING_SCRIPT)
  if(NOT DEFINED ${required_input} OR NOT EXISTS "${${required_input}}")
    message(FATAL_ERROR "Missing distribution compliance test input: ${required_input}")
  endif()
endforeach()

file(READ "${INNO_SCRIPT}" inno)
file(READ "${BUILD_SCRIPT}" build_script)
file(READ "${INSTALLER_E2E_SCRIPT}" installer_e2e_script)
file(READ "${VALIDATOR}" validator)
file(READ "${BETA_GUIDE}" beta_guide)
file(READ "${PRIVACY}" privacy)
file(READ "${NOTICE}" notice)
file(READ "${REMOVAL_SCRIPT}" removal_script)
file(READ "${SIGNING_SCRIPT}" signing_script)
file(READ "${EXTERNAL_SIGNING_SCRIPT}" external_signing_script)
get_filename_component(external_signing_dir "${EXTERNAL_SIGNING_SCRIPT}" DIRECTORY)
file(READ "${external_signing_dir}/dsk-release-validation.ps1" release_validation_script)
foreach(required_helper_wiring IN ITEMS
    ". (Join-Path $PSScriptRoot \"dsk-release-validation.ps1\")"
    "Assert-ReleaseSignature -Signature $signature -ExpectedSignerThumbprint $ExpectedSignerThumbprint")
  string(FIND "${external_signing_script}" "${required_helper_wiring}" helper_position)
  if(helper_position LESS 0)
    message(FATAL_ERROR "External signing must invoke pinned signature validation: ${required_helper_wiring}")
  endif()
endforeach()
# Check existing signature requirements across the caller and its mandatory helper.
# release-validation-test.ps1 separately proves rejection of invalid signatures,
# mismatched signer pins, missing timestamps, and unsafe build configurations.
string(APPEND external_signing_script "\n${release_validation_script}")
string(REPLACE "\\" "/" build_script_paths "${build_script}")
string(REPLACE "\\" "/" validator_paths "${validator}")

foreach(required_inno_token IN ITEMS
    "remove-user-data.ps1"
    "DSKCOMPLETE"
    "CompleteRemovalPrompt"
    "InitializeUninstall"
    "以降が見つかりません"
    "完全削除")
  string(FIND "${inno}" "${required_inno_token}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Installer is missing complete-removal behavior: ${required_inno_token}")
  endif()
endforeach()

foreach(required_inno_signing_token IN ITEMS
    "SignedUninstaller=yes"
    "SignTool=dsk_release"
    "ExternalSignedUninstallerDir"
    "SignedUninstallerDir={#ExternalSignedUninstallerDir}")
  string(FIND "${inno}" "${required_inno_signing_token}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Installer must Authenticode-sign its generated uninstaller: ${required_inno_signing_token}")
  endif()
endforeach()

foreach(required_build_signing_token IN ITEMS
    "InnoSignToolCommand"
    "ExternalSignedUninstallerDir"
    "PrepareExternalSignedUninstaller"
    "Invoke-InnoCompiler"
    "previousErrorActionPreference"
    "/Sdsk_release="
    "RequireValidUninstallerSignature"
    "Creating new signed uninstaller file"
    "Using existing signed uninstaller file"
    "TimeStamperCertificate")
  string(FIND "${build_script}" "${required_build_signing_token}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Installer build must wire and enforce generated-uninstaller signing: ${required_build_signing_token}")
  endif()
endforeach()

foreach(required_e2e_signing_token IN ITEMS
    "Get-AuthenticodeSignature -LiteralPath $uninstaller"
    "TimeStamperCertificate"
    "The generated uninstaller must have a valid Authenticode signature")
  string(FIND "${installer_e2e_script}" "${required_e2e_signing_token}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Installer E2E must verify the installed uninstaller signature: ${required_e2e_signing_token}")
  endif()
endforeach()

foreach(required_payload IN ITEMS
    "docs/beta-distribution.md"
    "docs/privacy.md"
    "docs/third-party-notices.md"
    "docs/simulcast-guidelines.md"
    "tools/remove-user-data.ps1"
    "LICENSE")
  string(FIND "${build_script_paths}" "${required_payload}" build_position)
  string(FIND "${validator_paths}" "${required_payload}" validator_position)
  if(build_position EQUAL -1 OR validator_position EQUAL -1)
    message(FATAL_ERROR "Packaging and validation must both cover ${required_payload}")
  endif()
endforeach()

foreach(required_beta_text IN ITEMS
    "public beta"
    "generated uninstaller"
    "valid, timestamped Authenticode signatures"
    "Google OAuth verification is approved"
    "Do not disable Microsoft Defender"
    "complete removal")
  string(FIND "${beta_guide}" "${required_beta_text}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Limited beta guide is missing: ${required_beta_text}")
  endif()
endforeach()

foreach(required_privacy_url IN ITEMS
    "https://www.youtube.com/t/terms"
    "https://policies.google.com/privacy"
    "https://security.google.com/settings/security/permissions")
  string(FIND "${privacy}" "${required_privacy_url}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Privacy policy is missing ${required_privacy_url}")
  endif()
endforeach()

foreach(required_notice_text IN ITEMS "OBS Studio" "Twitch" "YouTube" "Kick")
  string(FIND "${notice}" "${required_notice_text}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Third-party notice is missing ${required_notice_text}")
  endif()
endforeach()
foreach(required_kick_notice IN ITEMS
    "https://brandfolder.com/s/rn8r76txqxvc4vcjhf6km6w"
    "not licensed under DSK Multistream's GPL-2.0-or-later license"
    "owned by Kick or its licensors")
  string(FIND "${notice}" "${required_kick_notice}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Kick trademark notice is missing: ${required_kick_notice}")
  endif()
endforeach()

foreach(required_removal_guard IN ITEMS
    "DSK Multistream/"
    "dsk-multistream.json"
    "obs-dsk-multistream"
    "Refusing")
  string(FIND "${removal_script}" "${required_removal_guard}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Complete-removal helper is missing ownership guard: ${required_removal_guard}")
  endif()
endforeach()

foreach(required_signing_control IN ITEMS
    "DSK_INCLUDE_E2E_HOOKS:BOOL=OFF"
    "Get-CodeSigningCertificate"
    "Invoke-AuthenticodeSigning"
    "Test-IsChildPath"
    "The clean release DLL must be unsigned"
    "A signing output already exists"
    "/fd SHA256"
    "/tr"
    "/td SHA256"
    "RequireValidPluginSignature"
    "RequireValidUninstallerSignature"
    "TimeStamperCertificate"
    "Get-FileHash")
  string(FIND "${signing_script}" "${required_signing_control}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Release signing helper is missing: ${required_signing_control}")
  endif()
endforeach()

foreach(required_external_signing_control IN ITEMS
    "StagePlugin"
    "PrepareUninstaller"
    "BuildInstaller"
    "VerifyRelease"
    "Get-AuthenticodeSignature"
    "TimeStamperCertificate"
    "SignerCertificate.Thumbprint"
    "RequireValidUninstallerSignature"
    "ExternalSignedUninstallerDir"
    "Get-FileHash")
  string(FIND "${external_signing_script}" "${required_external_signing_control}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "External signing workflow is missing: ${required_external_signing_control}")
  endif()
endforeach()

message(STATUS "Distribution compliance surface is complete")
