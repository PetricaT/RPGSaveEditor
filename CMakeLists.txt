cmake_minimum_required(VERSION 3.20)
project(RPGSaveEditor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Dependencies ---
include(FetchContent)

# Qt6 must be installed on the system
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
qt_standard_project_setup()

# nlohmann_json
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3)
FetchContent_MakeAvailable(nlohmann_json)

# spdlog
FetchContent_Declare(spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.15.2)
FetchContent_MakeAvailable(spdlog)

# rapidfuzz-cpp
FetchContent_Declare(rapidfuzz
  GIT_REPOSITORY https://github.com/rapidfuzz/rapidfuzz-cpp.git
  GIT_TAG v3.3.3)
FetchContent_MakeAvailable(rapidfuzz)

# zlib (for RPG Maker MZ pako deflate)
find_package(ZLIB QUIET)
if(NOT ZLIB_FOUND)
  FetchContent_Declare(zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG v1.3.1
  )
  FetchContent_MakeAvailable(zlib)
  if(NOT TARGET ZLIB::ZLIB)
    if(TARGET zlibstatic)
      add_library(ZLIB::ZLIB ALIAS zlibstatic)
    elseif(TARGET zlib)
      add_library(ZLIB::ZLIB ALIAS zlib)
    endif()
  endif()
endif()

# --- Sources ---
set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/tabs.cpp
    src/fileops.cpp
    src/helpers.cpp
    src/rpgsave.cpp
    src/loadsave.cpp
    src/writesave.cpp
    src/json_model.cpp
)

set(HEADERS
    src/mainwindow.h
    src/helpers.h
    src/rpgsave.h
    src/json_model.h
    src/lz_string.hpp
    src/logger.h
)

set(RESOURCES
    resources.qrc
)

if(WIN32)
    set(APP_RC app.rc)
endif()

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
    set(APP_ICNS assets/icon.icns)
endif()

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${RESOURCES} ${APP_RC} ${APP_ICNS})

if(APPLE)
  set_target_properties(${PROJECT_NAME} PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "${PROJECT_NAME}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.rpgsaveeditor.app"
    MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    MACOSX_BUNDLE_ICON_FILE "icon.icns"
  )
  set_source_files_properties(assets/icon.icns PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources
  )
endif()

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    spdlog::spdlog
    rapidfuzz::rapidfuzz
    nlohmann_json::nlohmann_json
    ZLIB::ZLIB
)

if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT WIN32)
    target_link_options(${PROJECT_NAME} PRIVATE -s)
endif()

# Install rules
install(TARGETS ${PROJECT_NAME} DESTINATION bin)

if(UNIX AND NOT APPLE)
    install(FILES assets/RPGSaveEditor.desktop DESTINATION share/applications)
    install(FILES assets/icon.png DESTINATION share/icons/hicolor/256x256/apps RENAME RPGSaveEditor.png)
endif()
