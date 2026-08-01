foreach(required_path IN ITEMS MAIN_DOCK_CPP MAIN_DOCK_HPP PLUGIN_MAIN)
  if(NOT DEFINED ${required_path})
    message(FATAL_ERROR "${required_path} is required")
  endif()
endforeach()

file(READ "${MAIN_DOCK_CPP}" main_dock_cpp)
file(READ "${MAIN_DOCK_HPP}" main_dock_hpp)
file(READ "${PLUGIN_MAIN}" plugin_main_source)

foreach(forbidden_text IN ITEMS
    "Comment Viewer integration"
    "dskCommentViewerIntegrationToggle"
    "commentViewerIntegrationToggled")
  string(FIND "${main_dock_cpp}${main_dock_hpp}" "${forbidden_text}" forbidden_position)
  if(NOT forbidden_position EQUAL -1)
    message(FATAL_ERROR "DSK Streaming settings still expose Comment Viewer integration: ${forbidden_text}")
  endif()
endforeach()

string(FIND "${plugin_main_source}" "commentViewerIntegration->initialize();" integration_initialization)
if(integration_initialization EQUAL -1)
  message(FATAL_ERROR "Independent Comment Viewer integration initialization was removed")
endif()
