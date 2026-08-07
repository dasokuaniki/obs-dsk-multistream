foreach(required_input IN ITEMS INNO_SCRIPT BUILD_SCRIPT VALIDATOR BETA_GUIDE PRIVACY NOTICE REMOVAL_SCRIPT)
  if(NOT DEFINED ${required_input} OR NOT EXISTS "${${required_input}}")
    message(FATAL_ERROR "Missing distribution compliance test input: ${required_input}")
  endif()
endforeach()

file(READ "${INNO_SCRIPT}" inno)
file(READ "${BUILD_SCRIPT}" build_script)
file(READ "${VALIDATOR}" validator)
file(READ "${BETA_GUIDE}" beta_guide)
file(READ "${PRIVACY}" privacy)
file(READ "${NOTICE}" notice)
file(READ "${REMOVAL_SCRIPT}" removal_script)
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
    "currently unsigned"
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

message(STATUS "Distribution compliance surface is complete")
