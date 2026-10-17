if(NOT DEFINED WORKFLOW OR NOT EXISTS "${WORKFLOW}")
  message(FATAL_ERROR "WORKFLOW must point to windows-release.yml")
endif()

file(READ "${WORKFLOW}" workflow_source)

foreach(required_guard IN ITEMS
    "- name: Install pinned Inno Setup"
    "id: inno"
    "$version = '6.7.3'"
    "https://github.com/jrsoftware/issrc/releases/download/is-6_7_3/innosetup-6.7.3.exe"
    "9C73C3BAE7ED48D44112A0F48E66742C00090BDB5BEF71D9D3C056C66E97B732"
    "Get-FileHash -LiteralPath $downloadPath -Algorithm SHA256"
    ".Hash -ne $expectedSha256"
    "Get-AuthenticodeSignature -LiteralPath $downloadPath"
    "[Management.Automation.SignatureStatus]::Valid"
    "'Pyrsys B.V.'"
    "$signature.TimeStamperCertificate"
    "Compiler engine version: Inno Setup"
    "$compilerVersionMatch.Groups[1].Value -ne $version"
    "'Output=no'"
    "$probe | & $iscc '-'"
    "'/CURRENTUSER'"
    "-WindowStyle Hidden"
    "Join-Path $env:RUNNER_TEMP"
    [=[-IsccPath '${{ steps.inno.outputs.iscc }}']=])
  string(FIND "${workflow_source}" "${required_guard}" guard_position)
  if(guard_position LESS 0)
    message(FATAL_ERROR "Windows CI must provision and explicitly use the verified compiler: ${required_guard}")
  endif()
endforeach()

foreach(verification IN ITEMS "Get-FileHash" "Get-AuthenticodeSignature")
  string(FIND "${workflow_source}" "${verification}" verification_position)
  string(FIND "${workflow_source}" "Start-Process -FilePath $downloadPath" execution_position)
  if(execution_position LESS 0 OR verification_position GREATER execution_position)
    message(FATAL_ERROR "Windows CI must verify the Inno download before executing it")
  endif()
endforeach()

string(FIND "${workflow_source}" "choco install innosetup" unpinned_install)
if(NOT unpinned_install LESS 0)
  message(FATAL_ERROR "Windows CI must not fall back to an unpinned Inno Setup package")
endif()

string(FIND "${workflow_source}" "- name: Install pinned Inno Setup" install_step)
string(FIND "${workflow_source}" "- name: Build installer" build_step)
if(build_step LESS 0)
  message(FATAL_ERROR "The Windows release workflow is missing the installer build step")
endif()

if(install_step GREATER build_step)
  message(FATAL_ERROR "Inno Setup must be installed before the installer build step")
endif()

message(STATUS "Windows release workflow verifies pinned Inno Setup before packaging")
