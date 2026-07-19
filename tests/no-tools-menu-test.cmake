if(NOT DEFINED PLUGIN_MAIN)
  message(FATAL_ERROR "PLUGIN_MAIN is required")
endif()

file(READ "${PLUGIN_MAIN}" plugin_main_source)

string(FIND "${plugin_main_source}" "obs_frontend_add_tools_menu_item" tools_menu_registration)
if(NOT tools_menu_registration EQUAL -1)
  message(FATAL_ERROR "DSK must not add shortcuts to the OBS Tools menu")
endif()

foreach(required_dock_id IN ITEMS dsk_multistream dsk_vertical_layout)
  string(FIND "${plugin_main_source}" "obs_frontend_add_dock_by_id(\"${required_dock_id}\"" dock_registration)
  if(dock_registration EQUAL -1)
    message(FATAL_ERROR "Required OBS dock registration is missing: ${required_dock_id}")
  endif()
endforeach()
