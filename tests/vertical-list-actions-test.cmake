if(NOT DEFINED VERTICAL_EDITOR_CPP)
  message(FATAL_ERROR "VERTICAL_EDITOR_CPP is required")
endif()

file(READ "${VERTICAL_EDITOR_CPP}" vertical_editor_source)

foreach(forbidden_text IN ITEMS
    "Rename vertical scene"
    "Edit source transform")
  string(FIND "${vertical_editor_source}" "${forbidden_text}" forbidden_position)
  if(NOT forbidden_position EQUAL -1)
    message(FATAL_ERROR "Vertical list toolbar still exposes a redundant action: ${forbidden_text}")
  endif()
endforeach()

foreach(required_text IN ITEMS
    "verticalScenes_->setContextMenuPolicy(Qt::CustomContextMenu)"
    "&QWidget::customContextMenuRequested"
    "contextMenu.addAction(QStringLiteral(\"Rename\"))")
  string(FIND "${vertical_editor_source}" "${required_text}" required_position)
  if(required_position EQUAL -1)
    message(FATAL_ERROR "Vertical Scenes right-click rename behavior is missing: ${required_text}")
  endif()
endforeach()
