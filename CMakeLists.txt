cmake_minimum_required(VERSION 3.24)

# Windows: link the C/C++ runtime into the .exe (/MT), so it runs on a clean PC without the
# Visual C++ Redistributable. Set before any target exists, so the fetched libraries (SDL3,
# spdlog, Emerald) use the same runtime.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(Ascension
    VERSION 0.1.0
    DESCRIPTION "LitRPG vertical slice built on the Emerald engine"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# The executable, its compiled shaders/ and data/ end up in build/<preset>/bin.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---------------------------------------------------------------------------
# Emerald engine, fetched from GitHub and pinned to a commit.
#
# To work on the engine and the game at the same time, point this at a local Emerald checkout:
#   cmake --preset debug -DASCENSION_EMERALD_SOURCE_DIR=C:/dev/Emerald
# (the same as CMake's own -DFETCHCONTENT_SOURCE_DIR_EMERALD=...). Engine edits are then picked
# up by the next game build, and the checkout's build/_shadercross is reused, so the shader
# compiler is not built a second time.
# ---------------------------------------------------------------------------
set(ASCENSION_EMERALD_SOURCE_DIR "" CACHE PATH
    "Local Emerald checkout to use instead of the pinned commit (empty = fetch from GitHub)")
if(ASCENSION_EMERALD_SOURCE_DIR)
    get_filename_component(emerald_dir "${ASCENSION_EMERALD_SOURCE_DIR}" ABSOLUTE)
    set(FETCHCONTENT_SOURCE_DIR_EMERALD "${emerald_dir}")
    set(EMERALD_SHADERCROSS_BUILD_DIR "${emerald_dir}/build/_shadercross" CACHE PATH
        "Build directory for the shadercross host tool")
    message(STATUS "Ascension: using local Emerald at ${emerald_dir}")
endif()

include(FetchContent)
FetchContent_Declare(Emerald
    GIT_REPOSITORY https://github.com/Ridejock/Emerald.git
    GIT_TAG        697f356256d7657817e72cbdf27b23731b4821d1
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(Emerald) # also brings SDL3, spdlog, stb, nlohmann/json (and ImGui if enabled)

# ---------------------------------------------------------------------------
# Data library: typed definitions loaded from data/*.json. nlohmann/json stays
# inside the .cpp (Emerald already fetched it). Public headers have no JSON.
# ---------------------------------------------------------------------------
add_library(AscensionData STATIC
    src/Data/Database.cpp
    src/Data/Talk.cpp
    include/Ascension/Data/Database.h
    include/Ascension/Data/Map.h
    include/Ascension/Data/Talk.h
    include/Ascension/Data/Types.h
)
target_include_directories(AscensionData PUBLIC include)
target_link_libraries(AscensionData
    PUBLIC Emerald::Emerald
    PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(AscensionData PRIVATE
    ASCENSION_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
emerald_set_warnings(AscensionData)

# Tile steps, walls, and exits. Drawing stays in the app.
add_library(AscensionMap STATIC
    src/Map/Walk.cpp
    include/Ascension/Map/Walk.h
)
target_link_libraries(AscensionMap PUBLIC AscensionData)
emerald_set_warnings(AscensionMap)

# ---------------------------------------------------------------------------
# Combat. The turn model is swappable; the sandbox uses one agility round.
# ---------------------------------------------------------------------------
add_library(AscensionCombat STATIC
    src/Combat/Battle.cpp
    include/Ascension/Combat/Battle.h
)
target_link_libraries(AscensionCombat PUBLIC AscensionData)
emerald_set_warnings(AscensionCombat)

# ---------------------------------------------------------------------------
# Career: XP, skill ranks, and the evolution choice. Combat does not include this.
# ---------------------------------------------------------------------------
add_library(AscensionProgress STATIC
    src/Progress/Hero.cpp
    src/Progress/Inventory.cpp
    src/Progress/RunSave.cpp
    src/Progress/Vitals.cpp
    include/Ascension/Progress/Hero.h
    include/Ascension/Progress/Inventory.h
    include/Ascension/Progress/RunSave.h
    include/Ascension/Progress/Vitals.h
)
target_link_libraries(AscensionProgress PUBLIC AscensionData)
emerald_set_warnings(AscensionProgress)

# ---------------------------------------------------------------------------
# The combat sandbox. It has no shaders of its own; emerald_add_shaders compiles
# the engine's with shadercross (the only shader compiler; .hlsl is not handed to fxc).
# ---------------------------------------------------------------------------
add_executable(Ascension
    src/App/Main.cpp
    src/App/AscensionApp.cpp
    src/App/AscensionApp.h
    src/App/MapView.cpp
    src/App/MapView.h
    src/App/FightSim.cpp
    src/App/FightSim.h
    src/App/GearLine.h
    src/App/Playtest.cpp
    src/App/Playtest.h
)
target_link_libraries(Ascension PRIVATE AscensionCombat AscensionProgress AscensionMap)
# A dev build watches the source data/ so saving a JSON file updates the running
# game. The directory is missing in a packaged build, and the app then reads the
# data/ copied next to the executable.
target_compile_definitions(Ascension PRIVATE ASCENSION_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
emerald_add_shaders(Ascension)
emerald_set_warnings(Ascension)

# data/ is copied next to the executable. A custom target instead of a POST_BUILD step, so
# files added later are copied on the next build even when nothing needed relinking.
add_custom_target(AscensionContent
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Ascension>/data"
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/data" "$<TARGET_FILE_DIR:Ascension>/data"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Ascension>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:Ascension>/assets"
    COMMENT "Copying data/ and assets/ next to the executable"
    VERBATIM
)
add_dependencies(Ascension AscensionContent)

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT Ascension)

# ---------------------------------------------------------------------------
# Tests (ctest): the JSON loaders, combat, the XP / evolution career, and playtest
# flags. No window or GPU.
#
# They read tests/data, a frozen copy of the numbers the checks assert. Tuning
# the shipped data/ does not move those checks. The game executable still
# watches data/.
# ---------------------------------------------------------------------------
option(ASCENSION_BUILD_TESTS "Build the Ascension data tests" ON)
if(ASCENSION_BUILD_TESTS)
    enable_testing()
    set(ASCENSION_TEST_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/data")

    add_executable(AscensionDataTests tests/DataTests.cpp tests/Check.h)
    target_link_libraries(AscensionDataTests PRIVATE AscensionData)
    target_include_directories(AscensionDataTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/App")
    target_compile_definitions(AscensionDataTests PRIVATE
        ASCENSION_DATA_DIR="${ASCENSION_TEST_DATA_DIR}"
        ASCENSION_SHIPPED_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    emerald_set_warnings(AscensionDataTests)
    add_test(NAME Data COMMAND AscensionDataTests)

    add_executable(AscensionWalkTests tests/WalkTests.cpp tests/Check.h)
    target_link_libraries(AscensionWalkTests PRIVATE AscensionMap)
    target_compile_definitions(AscensionWalkTests PRIVATE
        ASCENSION_DATA_DIR="${ASCENSION_TEST_DATA_DIR}"
        ASCENSION_SHIPPED_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    emerald_set_warnings(AscensionWalkTests)
    add_test(NAME Walk COMMAND AscensionWalkTests)

    add_executable(AscensionCombatTests tests/CombatTests.cpp tests/Check.h)
    target_link_libraries(AscensionCombatTests PRIVATE AscensionCombat)
    target_include_directories(AscensionCombatTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/App")
    target_compile_definitions(AscensionCombatTests PRIVATE
        ASCENSION_DATA_DIR="${ASCENSION_TEST_DATA_DIR}"
        ASCENSION_SHIPPED_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    emerald_set_warnings(AscensionCombatTests)
    add_test(NAME Combat COMMAND AscensionCombatTests)

    add_executable(AscensionProgressTests tests/ProgressTests.cpp tests/Check.h)
    target_link_libraries(AscensionProgressTests PRIVATE AscensionProgress AscensionCombat)
    target_compile_definitions(AscensionProgressTests PRIVATE
        ASCENSION_DATA_DIR="${ASCENSION_TEST_DATA_DIR}")
    emerald_set_warnings(AscensionProgressTests)
    add_test(NAME Progress COMMAND AscensionProgressTests)

    add_executable(AscensionInventoryTests tests/InventoryTests.cpp tests/Check.h)
    target_link_libraries(AscensionInventoryTests PRIVATE AscensionProgress)
    target_compile_definitions(AscensionInventoryTests PRIVATE
        ASCENSION_DATA_DIR="${ASCENSION_TEST_DATA_DIR}")
    emerald_set_warnings(AscensionInventoryTests)
    add_test(NAME Inventory COMMAND AscensionInventoryTests)

    add_executable(AscensionSaveTests tests/SaveTests.cpp tests/Check.h)
    target_link_libraries(AscensionSaveTests PRIVATE AscensionProgress AscensionCombat)
    target_compile_definitions(AscensionSaveTests PRIVATE
        ASCENSION_DATA_DIR="${ASCENSION_TEST_DATA_DIR}"
        ASCENSION_SHIPPED_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    emerald_set_warnings(AscensionSaveTests)
    add_test(NAME Save COMMAND AscensionSaveTests)

    add_executable(AscensionPlaytestTests
        tests/PlaytestTests.cpp
        tests/Check.h
        src/App/Playtest.cpp
        src/App/Playtest.h
        src/App/FightSim.cpp
        src/App/FightSim.h
    )
    target_include_directories(AscensionPlaytestTests PRIVATE src/App)
    target_link_libraries(AscensionPlaytestTests PRIVATE AscensionProgress AscensionCombat)
    target_compile_definitions(AscensionPlaytestTests PRIVATE
        ASCENSION_DATA_DIR="${ASCENSION_TEST_DATA_DIR}"
        ASCENSION_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    emerald_set_warnings(AscensionPlaytestTests)
    add_test(NAME Playtest COMMAND AscensionPlaytestTests)

    add_executable(AscensionDialogueTests tests/DialogueTests.cpp tests/Check.h)
    target_link_libraries(AscensionDialogueTests PRIVATE AscensionData)
    target_compile_definitions(AscensionDialogueTests PRIVATE
        ASCENSION_DATA_DIR="${ASCENSION_TEST_DATA_DIR}"
        ASCENSION_SHIPPED_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
        ASCENSION_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    emerald_set_warnings(AscensionDialogueTests)
    add_test(NAME Dialogue COMMAND AscensionDialogueTests)
endif()

# ---------------------------------------------------------------------------
# Release package (zip): see cmake/Packaging.cmake
# ---------------------------------------------------------------------------
include(cmake/Packaging.cmake)
