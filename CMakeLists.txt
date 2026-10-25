cmake_minimum_required(VERSION 3.16)
project(interactive_session_protector
    VERSION 1.0.0
    DESCRIPTION "Linux user-space daemon to dynamically protect interactive sessions using cgroups v2"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Strict warning flags
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Sanitizer options
option(ENABLE_ASAN "Enable AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan)" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer (TSan)" OFF)

if (ENABLE_ASAN AND ENABLE_TSAN)
    message(FATAL_ERROR "ENABLE_ASAN and ENABLE_TSAN are mutually exclusive.")
endif()

if (ENABLE_ASAN)
    message(STATUS "Configuring build with AddressSanitizer and UndefinedBehaviorSanitizer")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

if (ENABLE_TSAN)
    message(STATUS "Configuring build with ThreadSanitizer")
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=thread)
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# ------------------------------------------------------------------------------
# Core Library: isp_core
# ------------------------------------------------------------------------------
add_library(isp_core STATIC
    src/config.cpp
    src/logger.cpp
    src/session_detector.cpp
    src/proc_table.cpp
    src/cgroup_manager.cpp
    src/ioprio.cpp
    src/resource_shaper.cpp
    src/daemon.cpp
    src/fake_call.cpp
    src/overlay/overlay_model.cpp
)

target_include_directories(isp_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(Threads REQUIRED)
target_link_libraries(isp_core PUBLIC Threads::Threads)

# ------------------------------------------------------------------------------
# Target 1: protector (Main daemon)
# ------------------------------------------------------------------------------
add_executable(protector
    src/protector_main.cpp
)
target_link_libraries(protector PRIVATE isp_core)

# ------------------------------------------------------------------------------
# Target 2: fake_call (Synthetic benchmark workload app)
# ------------------------------------------------------------------------------
add_executable(fake_call
    src/fake_call_main.cpp
)
target_link_libraries(fake_call PRIVATE isp_core)

# ------------------------------------------------------------------------------
# Target 3: unit_tests (Unit test suite)
# ------------------------------------------------------------------------------
add_executable(unit_tests
    tests/test_main.cpp
    tests/test_config.cpp
    tests/test_session_detector.cpp
    tests/test_proc_table.cpp
    tests/test_cgroup_manager.cpp
    tests/test_resource_shaper.cpp
    tests/test_daemon.cpp
    tests/test_fake_call.cpp
    tests/test_hardening.cpp
    tests/test_overlay.cpp
)
target_include_directories(unit_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_link_libraries(unit_tests PRIVATE isp_core)

# Enable CTest integration
enable_testing()
add_test(NAME ConfigUnitTests COMMAND unit_tests)

# ------------------------------------------------------------------------------
# Target 4: isp_overlay (Dear ImGui Dynamic HUD)
# ------------------------------------------------------------------------------
find_package(OpenGL REQUIRED)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glfw EXCLUDE_FROM_ALL)

add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_glfw.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui SYSTEM PUBLIC
    third_party/imgui
    third_party/imgui/backends
    third_party/glfw/include
)
target_link_libraries(imgui PUBLIC
    glfw
    OpenGL::GL
)

add_executable(isp_overlay
    src/overlay/overlay_model.cpp
    src/overlay/overlay_ui.cpp
    src/overlay/overlay_main.cpp
)
target_include_directories(isp_overlay PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/include
)
target_link_libraries(isp_overlay PRIVATE
    imgui
    glfw
    OpenGL::GL
    Threads::Threads
)
