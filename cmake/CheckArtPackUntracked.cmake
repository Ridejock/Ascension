# Fails when git is tracking anything under a directory named artpack.
# The public repo keeps the CC0 art. A private pack stays untracked.
if(NOT DEFINED SOURCE_DIR OR SOURCE_DIR STREQUAL "")
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

find_package(Git QUIET)
if(NOT Git_FOUND)
    message(FATAL_ERROR "git is required to check that no art pack is tracked")
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" ls-files
    OUTPUT_VARIABLE tracked
    ERROR_VARIABLE git_error
    RESULT_VARIABLE code
)
if(NOT code EQUAL 0)
    message(FATAL_ERROR "git ls-files failed: ${git_error}")
endif()

string(REPLACE "\\" "/" tracked "${tracked}")
string(REPLACE "\n" ";" paths "${tracked}")
foreach(path ${paths})
    if(path STREQUAL "")
        continue()
    endif()
    string(REPLACE "/" ";" parts "${path}")
    foreach(part ${parts})
        if(part STREQUAL "artpack")
            message(FATAL_ERROR "tracked art pack file: ${path}")
        endif()
    endforeach()
endforeach()
