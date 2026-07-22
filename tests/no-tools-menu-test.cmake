if(NOT DEFINED PLUGIN_MAIN)
  message(FATAL_ERROR "PLUGIN_MAIN is required")
endif()
if(NOT DEFINED COMMENT_VIEWER_INTEGRATION_CPP)
  message(FATAL_ERROR "COMMENT_VIEWER_INTEGRATION_CPP is required")
endif()

file(READ "${PLUGIN_MAIN}" plugin_main_source)
file(READ "${COMMENT_VIEWER_INTEGRATION_CPP}" comment_viewer_integration_source)

string(FIND "${plugin_main_source}" "obs_frontend_add_tools_menu_item" tools_menu_registration)
if(NOT tools_menu_registration EQUAL -1)
  message(FATAL_ERROR "DSK Streaming and DSK Vertical must use OBS Docks instead of duplicate Tools shortcuts")
endif()

string(FIND "${comment_viewer_integration_source}"
       "obs_frontend_add_tools_menu_item(" unsafe_comment_viewer_callback_position)
if(NOT unsafe_comment_viewer_callback_position EQUAL -1)
  message(FATAL_ERROR
          "The Comment Viewer launcher must not leave an OBS-owned callback pointing at unloaded plugin memory")
endif()

set(comment_viewer_tool_registration
    "obs_frontend_add_tools_menu_qaction(\"Open DSK Comment Viewer\")")
string(FIND "${comment_viewer_integration_source}" "${comment_viewer_tool_registration}" comment_viewer_tool_position)
if(comment_viewer_tool_position EQUAL -1)
  message(FATAL_ERROR "The independent Comment Viewer launcher is missing from the OBS Tools menu")
endif()

string(FIND "${comment_viewer_integration_source}"
       "connect(action, &QAction::triggered, this" comment_viewer_connection_position)
if(comment_viewer_connection_position EQUAL -1)
  message(FATAL_ERROR "The Comment Viewer Tools action must disconnect automatically with its integration owner")
endif()

string(FIND "${comment_viewer_integration_source}"
       "delete openViewerMenuAction_.data()" comment_viewer_cleanup_position)
if(comment_viewer_cleanup_position EQUAL -1)
  message(FATAL_ERROR "The Comment Viewer Tools action must be removed during plugin shutdown")
endif()

foreach(required_dock_id IN ITEMS dsk_multistream dsk_vertical_layout)
  string(FIND "${plugin_main_source}" "obs_frontend_add_dock_by_id(\"${required_dock_id}\"" dock_registration)
  if(dock_registration EQUAL -1)
    message(FATAL_ERROR "Required OBS dock registration is missing: ${required_dock_id}")
  endif()
endforeach()
