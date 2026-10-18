# Release package:
#
#   cmake --preset release
#   cmake --build --preset release --target package
#   -> build/release/Ascension-<version>-windows-x64.zip   (or linux-x64)
#
# The zip holds the executable, its compiled shaders, data/, assets/, README.txt, LICENSE.txt,
# THIRD_PARTY_LICENSES.txt and .itch.toml (Play launches the executable).

set(package_dir "${CMAKE_BINARY_DIR}/package")

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/packaging/README.txt.in"
               "${package_dir}/README.txt" @ONLY NEWLINE_STYLE CRLF)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "${package_dir}/LICENSE.txt" COPYONLY)

# .itch.toml at the root of the zip, so the itch.io app's Play button starts the executable
# (https://itch.io/docs/itch/integrating/manifest.html). Publishing is off until an itch slug
# is set; the manifest is still what the Windows package check looks for.
file(GENERATE OUTPUT "${package_dir}/.itch.toml"
     CONTENT "# itch.io app manifest: what the Play button launches.\n[[actions]]\nname = \"play\"\npath = \"$<TARGET_FILE_NAME:Ascension>\"\n"
     NEWLINE_STYLE UNIX)

# THIRD_PARTY_LICENSES.txt: the license texts of everything linked into the .exe, taken from the
# fetched sources, so they always match the pinned versions.
set(notices "${package_dir}/THIRD_PARTY_LICENSES.txt")
file(WRITE "${notices}"
     "This game uses the following libraries. Their licenses follow.\n\n")
function(_add_notice title file)
    if(NOT EXISTS "${file}")
        message(FATAL_ERROR "License file not found: ${file}")
    endif()
    file(READ "${file}" text)
    file(APPEND "${notices}"
         "==============================================================================\n"
         "${title}\n"
         "==============================================================================\n\n"
         "${text}\n\n")
endfunction()

foreach(dep Emerald SDL3 spdlog stb dr_libs nlohmann_json EnTT)
    FetchContent_GetProperties(${dep})
endforeach()
_add_notice("Emerald engine (https://github.com/Ridejock/Emerald)" "${emerald_SOURCE_DIR}/LICENSE")
_add_notice("SDL3 (https://libsdl.org)" "${sdl3_SOURCE_DIR}/LICENSE.txt")
_add_notice("HIDAPI, part of SDL3 (used under its BSD-style license)"
            "${sdl3_SOURCE_DIR}/src/hidapi/LICENSE-bsd.txt")
_add_notice("spdlog (https://github.com/gabime/spdlog)" "${spdlog_SOURCE_DIR}/LICENSE")
_add_notice("{fmt}, bundled with spdlog (https://github.com/fmtlib/fmt)"
            "${spdlog_SOURCE_DIR}/include/spdlog/fmt/bundled/fmt.license.rst")
_add_notice("stb (https://github.com/nothings/stb)" "${stb_SOURCE_DIR}/LICENSE")
_add_notice("nlohmann/json (https://github.com/nlohmann/json)"
            "${nlohmann_json_SOURCE_DIR}/LICENSE.MIT")
_add_notice("EnTT (https://github.com/skypjack/entt)" "${entt_SOURCE_DIR}/LICENSE")

# dr_mp3 keeps its license at the end of the header.
file(READ "${dr_libs_SOURCE_DIR}/dr_mp3.h" dr_mp3)
string(FIND "${dr_mp3}" "This software is available as a choice of the following licenses"
       license_start)
if(license_start LESS 0)
    message(FATAL_ERROR "Could not find the license text in dr_mp3.h")
endif()
string(SUBSTRING "${dr_mp3}" ${license_start} -1 dr_mp3_license)
string(REGEX REPLACE "(^|\n)[ \t]*(/\\*|\\*/)[ \t]*" "\\1" dr_mp3_license "${dr_mp3_license}")
file(WRITE "${package_dir}/dr_mp3-license.txt" "${dr_mp3_license}")
_add_notice("dr_mp3 (https://github.com/mackron/dr_libs)" "${package_dir}/dr_mp3-license.txt")

install(TARGETS Ascension RUNTIME DESTINATION . COMPONENT Game)
install(DIRECTORY "$<TARGET_FILE_DIR:Ascension>/shaders/" DESTINATION shaders COMPONENT Game)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/" DESTINATION data COMPONENT Game)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/" DESTINATION assets COMPONENT Game)
install(FILES "${package_dir}/README.txt" "${package_dir}/LICENSE.txt" "${notices}"
              "${package_dir}/.itch.toml"
        DESTINATION . COMPONENT Game)

if(WIN32)
    set(package_platform windows)
elseif(APPLE)
    set(package_platform macos)
else()
    set(package_platform linux)
endif()
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    string(APPEND package_platform "-x64")
endif()

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME "Ascension")
set(CPACK_PACKAGE_VENDOR "Ervin Ashley")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "Ascension-${PROJECT_VERSION}-${package_platform}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_COMPONENTS_ALL Game)
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
include(CPack)
