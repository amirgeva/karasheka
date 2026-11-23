cmake_minimum_required(VERSION 3.16)

project(Karasheka VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(KARA_BUILD_GAME "Build the SDL2 game" ON)
option(KARA_BUILD_TESTS "Build the simulation tests" ON)
option(KARA_FETCH_SDL "Download and build SDL2 when it is not installed" ON)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  add_compile_options(-Wall -Wextra)
endif()

# ---------------------------------------------------------------- core (no SDL)
# Skeleton and poses, moves, furniture and rooms, the fight rules and the
# computer opponents.
add_library(kara_core STATIC
  src/core/pose.cpp
  src/core/room.cpp
  src/core/fight.cpp
  src/core/ai.cpp
)
target_include_directories(kara_core PUBLIC src)

# ---------------------------------------------------------------- art (no SDL)
add_library(kara_art STATIC
  src/gfx/canvas.cpp
  src/gfx/image.cpp
  src/gfx/font.cpp
  src/gfx/art.cpp
)
target_include_directories(kara_art PUBLIC src)
target_link_libraries(kara_art PUBLIC kara_core)

# ---------------------------------------------------------------- tests
if(KARA_BUILD_TESTS)
  enable_testing()
  add_executable(kara_tests tests/test_core.cpp)
  target_link_libraries(kara_tests PRIVATE kara_core)
  add_test(NAME core COMMAND kara_tests)
endif()

# ---------------------------------------------------------------- game
if(KARA_BUILD_GAME)
  # SDL2 from the system / vcpkg / Homebrew if available, otherwise built from source.
  find_package(SDL2 CONFIG QUIET)
  if(NOT SDL2_FOUND)
    if(NOT KARA_FETCH_SDL)
      message(FATAL_ERROR "SDL2 not found. Install it (e.g. libsdl2-dev, brew install sdl2, vcpkg) "
                          "or configure with -DKARA_FETCH_SDL=ON.")
    endif()
    message(STATUS "SDL2 not found - downloading and building it")
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL2
      URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz)
    FetchContent_MakeAvailable(SDL2)
  endif()

  if(TARGET SDL2::SDL2)
    set(KARA_SDL_TARGET SDL2::SDL2)
  elseif(TARGET SDL2::SDL2-static)
    set(KARA_SDL_TARGET SDL2::SDL2-static)
  elseif(DEFINED SDL2_LIBRARIES)
    # Very old sdl2-config.cmake files only provide variables.
    add_library(kara_sdl2 INTERFACE)
    target_include_directories(kara_sdl2 INTERFACE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(kara_sdl2 INTERFACE ${SDL2_LIBRARIES})
    set(KARA_SDL_TARGET kara_sdl2)
  else()
    message(FATAL_ERROR "Found SDL2 but no usable CMake target")
  endif()

  set(GAME_SOURCES
    src/app/main.cpp
    src/app/game.cpp
    src/app/render.cpp
    src/app/audio.cpp
    src/app/save.cpp
    src/app/rawkeys.cpp
  )

  if(WIN32 AND NOT CMAKE_CROSSCOMPILING)
    # Embed the procedurally drawn icon in the .exe.
    add_executable(kara_icongen tools/icongen.cpp)
    target_link_libraries(kara_icongen PRIVATE kara_art)
    set(ICON_FILE ${CMAKE_CURRENT_BINARY_DIR}/karasheka.ico)
    add_custom_command(
      OUTPUT ${ICON_FILE}
      COMMAND kara_icongen ${ICON_FILE}
      DEPENDS kara_icongen
      COMMENT "Drawing application icon")
    set(RC_FILE ${CMAKE_CURRENT_BINARY_DIR}/karasheka.rc)
    file(WRITE ${RC_FILE} "IDI_ICON1 ICON \"${ICON_FILE}\"\n")
    set_source_files_properties(${RC_FILE} PROPERTIES OBJECT_DEPENDS ${ICON_FILE})
    list(APPEND GAME_SOURCES ${RC_FILE} ${ICON_FILE})
  endif()

  add_executable(karasheka WIN32 ${GAME_SOURCES})
  target_compile_definitions(karasheka PRIVATE KARA_VERSION="${PROJECT_VERSION}")
  target_link_libraries(karasheka PRIVATE kara_core kara_art)
  if(TARGET SDL2::SDL2main)
    target_link_libraries(karasheka PRIVATE SDL2::SDL2main)
  endif()
  target_link_libraries(karasheka PRIVATE ${KARA_SDL_TARGET})

  # Put SDL2.dll next to the game on Windows.
  if(WIN32 AND TARGET ${KARA_SDL_TARGET})
    get_target_property(_sdl_type ${KARA_SDL_TARGET} TYPE)
    if(_sdl_type STREQUAL "SHARED_LIBRARY")
      add_custom_command(TARGET karasheka POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${KARA_SDL_TARGET}> $<TARGET_FILE_DIR:karasheka>)
    endif()
  endif()

  # Installation (used by the Debian package and by "cmake --install").
  if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS karasheka RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    configure_file(data/karasheka.desktop.in karasheka.desktop @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/karasheka.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES data/io.github.amirgeva.karasheka.metainfo.xml
            DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    install(FILES data/karasheka.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
    install(FILES data/karasheka.6 DESTINATION ${CMAKE_INSTALL_MANDIR}/man6)
  else()
    install(TARGETS karasheka RUNTIME DESTINATION bin)
  endif()
endif()
