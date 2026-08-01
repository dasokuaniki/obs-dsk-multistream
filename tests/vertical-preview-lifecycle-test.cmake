if(NOT DEFINED VERTICAL_EDITOR_CPP)
  message(FATAL_ERROR "VERTICAL_EDITOR_CPP is required")
endif()

file(READ "${VERTICAL_EDITOR_CPP}" vertical_editor_source)

foreach(required_text IN ITEMS
    "void suspendPreview()"
    "void releasePreviewCanvas()"
    "suspendPreview();\n\t\tQWidget::hideEvent(event);"
    "if (!obs_canvas_removed(previewCanvas_))\n\t\t\tobs_canvas_remove(previewCanvas_);"
    "obs_canvas_release(previewCanvas_);")
  string(FIND "${vertical_editor_source}" "${required_text}" required_position)
  if(required_position EQUAL -1)
    message(FATAL_ERROR "Vertical preview lifecycle safety is missing: ${required_text}")
  endif()
endforeach()

string(FIND
  "${vertical_editor_source}"
  "prepareForUnload();\n\t\tQWidget::hideEvent(event);"
  unsafe_hide_position)
if(NOT unsafe_hide_position EQUAL -1)
  message(FATAL_ERROR "Hiding the Vertical dock must not fully release its private video canvas")
endif()
