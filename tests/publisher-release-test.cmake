cmake_minimum_required(VERSION 3.28)
if(NOT DEFINED VALIDATION_MODULE OR NOT EXISTS "${VALIDATION_MODULE}")
  message(FATAL_ERROR "VALIDATION_MODULE is required")
endif()
foreach(case IN ITEMS complete empty missing_id missing_secret whitespace hooks oss)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DVALIDATION_MODULE=${VALIDATION_MODULE}" "-DCASE=${case}"
      -P "${CMAKE_CURRENT_LIST_DIR}/publisher-release-fixture.cmake"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
  )
  if(case STREQUAL "complete" OR case STREQUAL "oss")
    if(NOT result EQUAL 0)
      message(FATAL_ERROR "Valid release fixture was rejected: ${case}")
    endif()
  elseif(result EQUAL 0)
    message(FATAL_ERROR "Invalid release fixture was accepted: ${case}")
  endif()
endforeach()
message(STATUS "Publisher release behavior tests passed: 7")
