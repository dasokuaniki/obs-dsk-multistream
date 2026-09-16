if(NOT DEFINED WORKFLOW OR NOT EXISTS "${WORKFLOW}")
  message(FATAL_ERROR "WORKFLOW must point to windows-release.yml")
endif()

file(READ "${WORKFLOW}" workflow_source)

string(FIND "${workflow_source}" "- name: Install Inno Setup" install_step)
if(install_step LESS 0)
  message(FATAL_ERROR "The Windows release workflow must install Inno Setup before building the installer")
endif()

string(FIND "${workflow_source}" "choco install innosetup" install_command)
if(install_command LESS 0)
  message(FATAL_ERROR "The Inno Setup step must install the innosetup package through Chocolatey")
endif()

string(FIND "${workflow_source}" "- name: Build installer" build_step)
if(build_step LESS 0)
  message(FATAL_ERROR "The Windows release workflow is missing the installer build step")
endif()

if(install_step GREATER build_step)
  message(FATAL_ERROR "Inno Setup must be installed before the installer build step")
endif()

message(STATUS "Windows release workflow provides Inno Setup before packaging")
