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

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${RESOURCES} ${APP_RC})

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    spdlog::spdlog
    rapidfuzz::rapidfuzz
    nlohmann_json::nlohmann_json
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
