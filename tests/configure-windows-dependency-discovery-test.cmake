if(NOT DEFINED CONFIGURE_WINDOWS_SCRIPT)
  message(FATAL_ERROR "CONFIGURE_WINDOWS_SCRIPT was not provided")
endif()

file(READ "${CONFIGURE_WINDOWS_SCRIPT}" configure_windows_text)

foreach(required_fragment IN ITEMS
    "deps\\obs-sdk"
    ".deps\\obs-sdk"
    "obs-deps-2025-08-23-x64"
    "obs-deps-qt6-2025-08-23-x64")
  string(FIND "${configure_windows_text}" "${required_fragment}" fragment_index)
  if(fragment_index EQUAL -1)
    message(FATAL_ERROR
      "configure-windows.ps1 no longer searches the checked-out dependency root: ${required_fragment}")
  endif()
endforeach()
