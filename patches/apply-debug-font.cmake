# Apply the debug-font patch when the fetched Emerald tree does not already have it.
# A second configure sees a tree the patch no longer matches and leaves it alone.
if(NOT DEFINED EMERALD_SOURCE OR NOT DEFINED EMERALD_PATCH)
    message(FATAL_ERROR "apply-debug-font.cmake needs EMERALD_SOURCE and EMERALD_PATCH")
endif()

execute_process(
    COMMAND git apply --check --whitespace=nowarn "${EMERALD_PATCH}"
    WORKING_DIRECTORY "${EMERALD_SOURCE}"
    RESULT_VARIABLE check
    OUTPUT_QUIET
    ERROR_QUIET
)
if(NOT check EQUAL 0)
    return()
endif()

execute_process(
    COMMAND git apply --whitespace=nowarn "${EMERALD_PATCH}"
    WORKING_DIRECTORY "${EMERALD_SOURCE}"
    RESULT_VARIABLE applied
)
if(NOT applied EQUAL 0)
    message(FATAL_ERROR "The Emerald debug font patch failed")
endif()
