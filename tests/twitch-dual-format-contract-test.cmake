foreach(required_variable OUTPUT_MANAGER_CPP STREAM_CONTROLS_DOCK_CPP VERTICAL_SCENE_BUILDER_CPP PLUGIN_MAIN)
  if(NOT DEFINED ${required_variable})
    message(FATAL_ERROR "${required_variable} is required")
  endif()
endforeach()

file(READ "${OUTPUT_MANAGER_CPP}" output_manager_cpp)
file(READ "${STREAM_CONTROLS_DOCK_CPP}" stream_controls_dock_cpp)
file(READ "${VERTICAL_SCENE_BUILDER_CPP}" vertical_scene_builder_cpp)
file(READ "${PLUGIN_MAIN}" plugin_main_cpp)

if(NOT output_manager_cpp MATCHES
   "DskVerticalCanvasFlags[ \t]*=[ \t]*ACTIVATE[ \t]*\\|[ \t]*SCENE_REF[ \t]*;")
  message(FATAL_ERROR "DSK Vertical must use a non-ephemeral canvas for OBS Additional Canvas")
endif()

if(NOT output_manager_cpp MATCHES
   "DskPrivateCanvasFlags[ \t]*=[ \t]*ACTIVATE[ \t]*\\|[ \t]*SCENE_REF[ \t]*\\|[ \t]*EPHEMERAL[ \t]*;")
  message(FATAL_ERROR "Per-target DSK canvases must remain ephemeral")
endif()

if(NOT output_manager_cpp MATCHES
   "obs_frontend_add_canvas\\(DskVerticalCanvasName,[ \t\r\n]*&info,[ \t\r\n]*DskVerticalCanvasFlags\\)")
  message(FATAL_ERROR "DSK Vertical must be registered with its dedicated persistent flags")
endif()

if(NOT output_manager_cpp MATCHES
   "obs_frontend_add_canvas\\(canvasName.toUtf8\\(\\).constData\\(\\),[ \t\r\n]*&info,[ \t\r\n]*DskPrivateCanvasFlags\\)")
  message(FATAL_ERROR "Per-target scene canvases must not leak into Additional Canvas")
endif()

if(NOT vertical_scene_builder_cpp MATCHES "obs_canvas_get_scene_by_name")
  message(FATAL_ERROR "A restored DSK Vertical scene must be adopted after an abnormal shutdown")
endif()

if(NOT plugin_main_cpp MATCHES "prepareVerticalCanvas")
  message(FATAL_ERROR "The DSK Vertical canvas must be prepared before OBS stream settings are used")
endif()

if(NOT output_manager_cpp MATCHES "obs_frontend_streaming_active\\(\\)" OR
   NOT output_manager_cpp MATCHES "MultitrackExtraCanvas")
  message(FATAL_ERROR "An active OBS Dual Format canvas must be protected from replacement")
endif()

if(NOT output_manager_cpp MATCHES "isOrMigratePluginOwnedVerticalCanvas" OR
	NOT output_manager_cpp MATCHES "obs_canvas_get_scene_by_name" OR
	NOT output_manager_cpp MATCHES "obs_source_get_private_settings" OR
	NOT output_manager_cpp MATCHES "DskVerticalSceneOwnerKey")
  message(FATAL_ERROR "A canvas display name alone must not establish DSK ownership")
endif()

if(NOT output_manager_cpp MATCHES "obsNativeUsingVerticalCanvas_" OR
   NOT output_manager_cpp MATCHES "obsContextReloadPending_")
  message(FATAL_ERROR "Profile and scene-collection changes must defer vertical teardown during native streaming")
endif()

if(NOT output_manager_cpp MATCHES "verticalScene_\\.release\\(false\\)")
  message(FATAL_ERROR "The persistent DSK ownership scene must survive normal reference release")
endif()

# obs_frontend_get_streaming_service() returns a borrowed pointer. Releasing it can
# destroy OBS's active service after repeated status refreshes.
if(output_manager_cpp MATCHES
   "obsNativeServiceIsTwitch\\(\\)[ \\t\\r\\n]*\\{[^}]*obs_service_release")
  message(FATAL_ERROR "The borrowed OBS frontend streaming service must not be released")
endif()

if(NOT stream_controls_dock_cpp MATCHES "obs_frontend_get_streaming_service\\(\\)")
  message(FATAL_ERROR "The OBS native row must inspect the live frontend service object")
endif()

if(stream_controls_dock_cpp MATCHES "service\\.json" OR
   stream_controls_dock_cpp MATCHES
   "obsNativeStreamInfo\\(\\)[ \\t\\r\\n]*\\{[^}]*obs_service_release")
  message(FATAL_ERROR "The OBS native row must not depend on profile JSON or release the borrowed service")
endif()
