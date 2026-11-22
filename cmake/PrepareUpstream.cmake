# Copy the pristine upstream tree into the build directory and apply the
# Linux compatibility patches. Keeping the source tree read-only makes the
# upstream provenance and patch series easy to audit.
if(NOT DEFINED SEVENZIP_SOURCE_DIR OR NOT DEFINED UPSTREAM_DIR)
  message(FATAL_ERROR "PrepareUpstream.cmake requires SEVENZIP_SOURCE_DIR and UPSTREAM_DIR")
endif()

file(REMOVE_RECURSE "${UPSTREAM_DIR}")
file(COPY "${SEVENZIP_SOURCE_DIR}/" DESTINATION "${UPSTREAM_DIR}")

# The patch series is stored with LF endings. GitHub's source archive is already
# LF-normalized, while the official 7-Zip archive uses CRLF. Normalize the files
# touched by the patch so either source distribution can be used.
set(_patched_files
  CPP/7zip/UI/Agent/Agent.cpp
  CPP/7zip/UI/Agent/Agent.h
  CPP/7zip/UI/Agent/AgentOut.cpp
  CPP/7zip/UI/Agent/ArchiveFolderOpen.cpp
  CPP/7zip/UI/Agent/ArchiveFolderOut.cpp
  CPP/7zip/UI/Agent/UpdateCallbackAgent.cpp
)
foreach(_file IN LISTS _patched_files)
  set(_path "${UPSTREAM_DIR}/${_file}")
  if(EXISTS "${_path}")
    execute_process(
      COMMAND sed -i "s/\r$//" "${_path}"
      RESULT_VARIABLE _normalize_result
    )
    if(NOT _normalize_result EQUAL 0)
      message(FATAL_ERROR "Failed to normalize line endings in ${_path}")
    endif()
  endif()
endforeach()

set(_patch "${CMAKE_CURRENT_LIST_DIR}/../patches/7zip/0001-agent-posix.patch")
execute_process(
  COMMAND patch -p1 -d "${UPSTREAM_DIR}" -i "${_patch}"
  RESULT_VARIABLE _patch_result
  OUTPUT_VARIABLE _patch_output
  ERROR_VARIABLE _patch_error
)
if(NOT _patch_result EQUAL 0)
  message(FATAL_ERROR "Failed to apply ${_patch}:\n${_patch_output}\n${_patch_error}")
endif()
