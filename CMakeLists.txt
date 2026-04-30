cmake_minimum_required(VERSION 3.20)
project(SnowLoader)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# === MinHook — fetched automatically at cmake configure time (needs internet on first run) ===
# Update GIT_TAG to the latest release: https://github.com/TsudaKageyu/minhook/releases
include(FetchContent)
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(minhook)

# === ImGui — fetched automatically at cmake configure time (needs internet on first run) ===
# Update GIT_TAG to the latest release: https://github.com/ocornut/imgui/releases
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.6
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui SHARED
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
)
set_target_properties(imgui PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    OUTPUT_NAME "imgui"
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_compile_definitions(imgui PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(imgui PUBLIC d3d11 dxgi user32)

set(COMMON_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)

add_library(snowloader_core STATIC
    src/core/config_service.cpp
    src/core/game_memory_service.cpp
    src/core/game_state_service.cpp
    src/core/hooks.cpp
    src/core/overlay_service.cpp
    src/core/config_manager.cpp
    src/core/keybinding_manager.cpp
    src/runtime/script_manager.cpp
)

target_include_directories(snowloader_core PUBLIC ${COMMON_INCLUDE_DIR} ${imgui_SOURCE_DIR})
target_link_libraries(snowloader_core PUBLIC Psapi user32 kernel32 minhook)
target_compile_definitions(snowloader_core PUBLIC USE_MINHOOK)

add_library(snowloader_asi SHARED src/host/dllmain.cpp)

set_target_properties(snowloader_asi PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    OUTPUT_NAME "SnowLoader"
    SUFFIX ".asi"
)

target_include_directories(snowloader_asi PRIVATE ${COMMON_INCLUDE_DIR})
target_link_libraries(snowloader_asi PRIVATE snowloader_core imgui d3d11 dxgi Psapi user32 kernel32)
if(MSVC)
    target_link_options(snowloader_asi PRIVATE /DELAYLOAD:imgui.dll)
    target_link_libraries(snowloader_asi PRIVATE delayimp)
endif()

# === Auto-discovery and build of scripts ===
macro(add_script_library SCRIPT_NAME)
    # Discover all .cpp files in the script directory
    file(GLOB CONFIGURE_DEPENDS SCRIPT_SOURCES "${CMAKE_SOURCE_DIR}/scripts/${SCRIPT_NAME}/*.cpp")
    
    if(NOT SCRIPT_SOURCES)
        message(WARNING "No .cpp files found in scripts/${SCRIPT_NAME}")
        return()
    endif()
    
    add_library(${SCRIPT_NAME}_script SHARED ${SCRIPT_SOURCES})
    
    set_target_properties(${SCRIPT_NAME}_script PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/scripts"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/scripts"
        OUTPUT_NAME "${SCRIPT_NAME}"
    )
    
    target_include_directories(${SCRIPT_NAME}_script PRIVATE
        ${COMMON_INCLUDE_DIR}
        ${CMAKE_SOURCE_DIR}/scripts/${SCRIPT_NAME}
    )

    target_link_libraries(${SCRIPT_NAME}_script PRIVATE snowloader_core Psapi user32 kernel32)
    if(SCRIPT_NAME STREQUAL "speed" OR SCRIPT_NAME STREQUAL "timelapse" OR SCRIPT_NAME STREQUAL "smt")
        target_link_libraries(${SCRIPT_NAME}_script PRIVATE imgui)
    endif()
endmacro()

# Auto-discover and build all scripts
file(GLOB CONFIGURE_DEPENDS SCRIPT_DIRS "${CMAKE_SOURCE_DIR}/scripts/*/")
foreach(SCRIPT_DIR ${SCRIPT_DIRS})
    get_filename_component(SCRIPT_NAME ${SCRIPT_DIR} NAME)
    # Skip if it doesn't look like a script directory (e.g., CMakeLists.txt, .gitkeep)
    if(NOT SCRIPT_NAME MATCHES "^[.]")
        add_script_library(${SCRIPT_NAME})
    endif()
endforeach()

# MinHook is now fetched via FetchContent above and linked into snowloader_core.
# Scripts that link snowloader_core automatically get MinHook transitively.

# Compiler-specific settings
if(MSVC)
    target_compile_options(imgui PRIVATE /W0)            # third-party: suppress all warnings
    target_compile_options(snowloader_core PRIVATE /W4 /Ox)
    target_compile_options(snowloader_asi PRIVATE /W4 /Ox)
    foreach(SCRIPT_DIR ${SCRIPT_DIRS})
        get_filename_component(SCRIPT_NAME ${SCRIPT_DIR} NAME)
        if(NOT SCRIPT_NAME MATCHES "^[.]" AND TARGET ${SCRIPT_NAME}_script)
            target_compile_options(${SCRIPT_NAME}_script PRIVATE /W4 /Ox)
        endif()
    endforeach()
else()
    target_compile_options(snowloader_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(snowloader_asi PRIVATE -Wall -Wextra -Wpedantic)
    foreach(SCRIPT_DIR ${SCRIPT_DIRS})
        get_filename_component(SCRIPT_NAME ${SCRIPT_DIR} NAME)
        if(NOT SCRIPT_NAME MATCHES "^[.]" AND TARGET ${SCRIPT_NAME}_script)
            target_compile_options(${SCRIPT_NAME}_script PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endforeach()
endif()

# Deployment
if(WIN32)
    add_custom_command(TARGET imgui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/assets/SnowLoader"
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:imgui> "${CMAKE_SOURCE_DIR}/assets/SnowLoader/"
        COMMENT "Copying imgui.dll to assets/SnowLoader"
    )
    add_custom_command(TARGET snowloader_asi POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:snowloader_asi> "${CMAKE_SOURCE_DIR}/assets/"
        COMMENT "Copying SnowLoader.asi to assets"
    )
    
    # Auto-deploy all discovered scripts
    foreach(SCRIPT_DIR ${SCRIPT_DIRS})
        get_filename_component(SCRIPT_NAME ${SCRIPT_DIR} NAME)
        if(NOT SCRIPT_NAME MATCHES "^[.]" AND TARGET ${SCRIPT_NAME}_script)
            add_custom_command(TARGET ${SCRIPT_NAME}_script POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/assets/SnowLoader/scripts"
                COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${SCRIPT_NAME}_script> "${CMAKE_SOURCE_DIR}/assets/SnowLoader/scripts/"
                COMMENT "Copying ${SCRIPT_NAME} script to assets/SnowLoader/scripts"
            )
        endif()
    endforeach()
endif()
