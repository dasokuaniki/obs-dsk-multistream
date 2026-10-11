if(NOT DEFINED SOURCE_ROOT)
  message(FATAL_ERROR "SOURCE_ROOT is required")
endif()
file(READ "${SOURCE_ROOT}/src/core/output-manager.cpp" manager)
file(READ "${SOURCE_ROOT}/src/plugin-main.cpp" plugin)
file(READ "${SOURCE_ROOT}/src/ui/vertical-layout-editor.cpp" editor)
file(READ "${SOURCE_ROOT}/src/core/e2e-auto-runner.cpp" e2e)
file(READ "${SOURCE_ROOT}/scripts/run-obs-rtmp-e2e.ps1" e2e_script)

foreach(required IN ITEMS
  "obs_canvas_create_private(\"DSK Horizontal\", &info, DskPrivateCanvasFlags)"
  "if (group == EncoderGroup::DskHorizontal)\n\t\treturn obs_get_video();"
  "QStringLiteral(\"dsk-horizontal-canvas\")"
  "horizontalScene_.release();"
  "obs_canvas_set_channel(horizontalCanvas_, 0, nullptr);"
  "obs_canvas_release(horizontalCanvas_);"
  "settings.horizontalLayout = horizontalLayouts_.verticalLayout();"
  "sceneCanvasKeyForTarget(target)")
  string(FIND "${manager}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Independent canvas routing/lifecycle contract is missing: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
  "obs_frontend_add_dock_by_id(\"dsk_horizontal_layout\", \"DSK Horizontal\", horizontalDock)"
  "horizontalDock->prepareForUnload();"
  "horizontalDock->handleSceneCollectionChanged();")
  string(FIND "${plugin}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Horizontal dock lifecycle contract is missing: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
  "previewCounters[group == EncoderGroup::DskHorizontalCanvas ? 1 : 0]"
  "manager_->layouts(outputGroup_)"
  "manager_->saveVerticalLayout(outputGroup_)"
  "manager_->sceneLinks(outputGroup_)"
  "horizontalLayoutChanged"
  "\"DSKHorizontalLayout\"")
  string(FIND "${editor}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Horizontal/vertical editor isolation is missing: ${required}")
  endif()
endforeach()
if(NOT e2e MATCHES "prepareHorizontalLayout" OR NOT e2e MATCHES "restoreHorizontalLayout")
  message(FATAL_ERROR "The local RTMP harness must prepare and restore an independent horizontal source")
endif()
if(NOT e2e_script MATCHES "ExpectedEncoderGroup -eq \"dsk-horizontal-canvas\"")
  message(FATAL_ERROR "The RTMP harness must validate the new horizontal output's resolution")
endif()

# A native canary reproduced this after clearing selection and switching the
# OBS main scene with Follow OBS scene disabled. A refresh must not select the
# first source just because the layout is nonempty, for either editor direction.
string(FIND "${editor}" "void VerticalLayoutEditor::refreshItems()" refresh_start)
string(FIND "${editor}" "int VerticalLayoutEditor::selectedIndex() const" refresh_end)
if(refresh_start EQUAL -1 OR refresh_end LESS refresh_start)
  message(FATAL_ERROR "Could not locate the editor selection-refresh boundary")
endif()
math(EXPR refresh_length "${refresh_end} - ${refresh_start}")
string(SUBSTRING "${editor}" ${refresh_start} ${refresh_length} refresh_body)
if(refresh_body MATCHES "items_->setCurrentRow\\(0\\)")
  message(FATAL_ERROR "Refreshing either canvas editor must preserve cleared source selection")
endif()
if(NOT refresh_body MATCHES "clearSourceSelection\\(\\)")
  message(FATAL_ERROR "Editor refresh must clear invalid or absent source selections")
endif()
