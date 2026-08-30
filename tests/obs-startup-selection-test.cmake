if(NOT DEFINED PLUGIN_MAIN)
  message(FATAL_ERROR "PLUGIN_MAIN is required")
endif()

file(READ "${PLUGIN_MAIN}" plugin_main_source)

foreach(required_text IN ITEMS
    "bool deselectObsSceneItem(obs_scene_t *, obs_sceneitem_t *item, void *)"
    "obs_sceneitem_group_enum_items(item, deselectObsSceneItem, nullptr);"
    "obs_sceneitem_select(item, false);"
    "void clearObsMainCanvasSelection()"
    "obs_frontend_get_current_preview_scene()"
    "obs_frontend_get_current_scene()"
    "obs_source_release(sceneSource);"
    "initializeFrontendUi();\n\t\tclearObsMainCanvasSelection();")
  string(FIND "${plugin_main_source}" "${required_text}" required_position)
  if(required_position EQUAL -1)
    message(FATAL_ERROR "OBS must start without a selected main-canvas source: ${required_text}")
  endif()
endforeach()

string(REGEX MATCHALL "clearObsMainCanvasSelection" startup_clear_calls "${plugin_main_source}")
list(LENGTH startup_clear_calls startup_clear_call_count)
if(NOT startup_clear_call_count EQUAL 2)
  message(FATAL_ERROR
    "Main-canvas selection must be cleared exactly once at startup, not on later scene changes")
endif()
