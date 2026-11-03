cmake_minimum_required(VERSION 3.16)
# ---- version: the one place to change it. See CHANGELOG.md.
#   0.1.0-dev: the editor and test bench worked out (dev1 to dev25).
#   0.2.x-alpha: barrels, posing, and first-person shooting.
project(Gunsmith3D VERSION 0.2.3 LANGUAGES C CXX)
set(GUNSMITH_STAGE "alpha" CACHE STRING "Release stage: dev, alpha, beta, rc, or empty for a release")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(TP ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
set(RL ${TP}/raylib/src)

# ---- platform
# Gunsmith3D ships for Windows only. On Linux it runs through Wine-GE or Proton-GE, so there is no Linux build of
# the game. Building on Linux gives the headless core library and its tests (the physics needs no graphics);
# GUNSMITH_NATIVE_APP=ON also builds the app natively (X11), a developer convenience for automated screenshots.
if(WIN32)
  set(GUNSMITH_BUILD_APP ON)
else()
  option(GUNSMITH_NATIVE_APP "Developer only: also build the app natively (X11)" OFF)
  set(GUNSMITH_BUILD_APP ${GUNSMITH_NATIVE_APP})
endif()

if(GUNSMITH_BUILD_APP)
# ---- raylib (compiled from source)
add_library(raylib STATIC
  ${RL}/rcore.c ${RL}/rshapes.c ${RL}/rtextures.c ${RL}/rtext.c
  ${RL}/rmodels.c ${RL}/utils.c ${RL}/raudio.c ${RL}/rglfw.c)
target_include_directories(raylib PUBLIC ${RL} PRIVATE ${RL}/external/glfw/include)
target_compile_definitions(raylib PUBLIC PLATFORM_DESKTOP GRAPHICS_API_OPENGL_33)
# pictures players add as textures: JPG, BMP and TGA as well as PNG
target_compile_definitions(raylib PRIVATE SUPPORT_FILEFORMAT_JPG=1 SUPPORT_FILEFORMAT_BMP=1 SUPPORT_FILEFORMAT_TGA=1)
if(WIN32)
  target_link_libraries(raylib PUBLIC opengl32 gdi32 winmm)
else()
  # developer build: X11 only, system libraries loaded at runtime; fallback headers where the system ones are missing
  target_compile_definitions(raylib PRIVATE _GLFW_X11)
  target_compile_options(raylib PRIVATE "SHELL:-idirafter ${TP}/x11-fallback")
  target_link_libraries(raylib PUBLIC m dl pthread)
endif()
endif()

# ---- the layers, each its own library. Dependencies only point downward:
#   core  design data, blueprints, cartridges, physics, the reliability test. No graphics, no UI.
#   app   app state, 3D rendering, editor tools, test bench, input (needs raylib).
#   ui    the editor panels (needs Dear ImGui).
#   main  startup and the main loop.
file(GLOB CORE_SRC CONFIGURE_DEPENDS src/core/*.cpp)
file(GLOB APP_SRC  CONFIGURE_DEPENDS src/app/*.cpp)
file(GLOB UI_SRC   CONFIGURE_DEPENDS src/ui/*.cpp)

set(GUNSMITH_VERSION ${PROJECT_VERSION})
if(GUNSMITH_STAGE)
  set(GUNSMITH_VERSION ${GUNSMITH_VERSION}-${GUNSMITH_STAGE})
endif()
set(GUNSMITH_BUILD "")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.git)
  execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                  OUTPUT_VARIABLE GUNSMITH_BUILD OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
configure_file(src/core/version.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/version.hpp @ONLY)
message(STATUS "Gunsmith3D ${GUNSMITH_VERSION} ${GUNSMITH_BUILD}")

find_package(Threads REQUIRED)   # the reliability test runs on a worker thread
add_library(gunsmith_core STATIC ${CORE_SRC})
target_include_directories(gunsmith_core PUBLIC src/core ${TP}/json ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(gunsmith_core PUBLIC Threads::Threads)

if(GUNSMITH_BUILD_APP)
# ---- Dear ImGui + rlImGui
add_library(imgui STATIC
  ${TP}/imgui/imgui.cpp ${TP}/imgui/imgui_draw.cpp ${TP}/imgui/imgui_tables.cpp
  ${TP}/imgui/imgui_widgets.cpp ${TP}/rlImGui/rlImGui.cpp)
target_include_directories(imgui PUBLIC ${TP}/imgui ${TP}/rlImGui)
target_link_libraries(imgui PUBLIC raylib)
target_compile_definitions(imgui PUBLIC FONT_AWESOME_ICON_SIZE=13)

add_library(gunsmith_app STATIC ${APP_SRC})
target_include_directories(gunsmith_app PUBLIC src)
target_link_libraries(gunsmith_app PUBLIC gunsmith_core raylib imgui)
if(WIN32)
  target_link_libraries(gunsmith_app PUBLIC comdlg32 shell32)  # the open-file box and "open folder"
endif()

add_library(gunsmith_ui STATIC ${UI_SRC})
target_link_libraries(gunsmith_ui PUBLIC gunsmith_app imgui)

# ---- the program
set(GUNSMITH_EXTRA_SRC "")
if(WIN32)  # version and description shown in the .exe's Properties > Details
  enable_language(RC)
  set(GUNSMITH_RC_FLAGS 0x0L)
  if(GUNSMITH_STAGE)
    set(GUNSMITH_RC_FLAGS 0x2L)  # VS_FF_PRERELEASE
  endif()
  configure_file(src/gunsmith3d.rc.in ${CMAKE_CURRENT_BINARY_DIR}/generated/gunsmith3d.rc @ONLY)
  set(GUNSMITH_EXTRA_SRC ${CMAKE_CURRENT_BINARY_DIR}/generated/gunsmith3d.rc)
endif()
add_executable(gunsmith3d src/main.cpp ${GUNSMITH_EXTRA_SRC})
target_link_libraries(gunsmith3d PRIVATE gunsmith_ui gunsmith_app gunsmith_core imgui raylib)
if(WIN32)
  set_target_properties(gunsmith3d PROPERTIES WIN32_EXECUTABLE ON)
  if(MSVC)
    target_link_options(gunsmith3d PRIVATE /ENTRY:mainCRTStartup /STACK:16777216)
  else()
    target_link_options(gunsmith3d PRIVATE -Wl,--stack,16777216)  # 16 MB: carving cuts solids recursively
  endif()
endif()
if(NOT MSVC)
  target_link_options(gunsmith3d PRIVATE -static-libstdc++ -static-libgcc)
endif()
endif()  # GUNSMITH_BUILD_APP

# ---- tests
add_executable(gunsmith3d_tests tests/solver_tests.cpp tests/map_tests.cpp)
target_link_libraries(gunsmith3d_tests PRIVATE gunsmith_core)
enable_testing()
add_test(NAME solver COMMAND gunsmith3d_tests)
