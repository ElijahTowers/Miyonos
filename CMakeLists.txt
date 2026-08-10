cmake_minimum_required(VERSION 3.16)
project(Miyonos VERSION 0.1.26 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MIYONOS_BUILD_APP "Build the SDL2 application" ON)
option(MIYONOS_BUILD_TESTS "Build protocol and state tests" ON)

set(MIYONOS_CORE_SOURCES
    src/network/http.cpp
    src/network/https_artwork.cpp
    src/sonos/xml.cpp
    src/sonos/protocol.cpp
    src/storage/settings.cpp
    src/storage/artwork_cache.cpp
    src/platform/logger.cpp
    src/platform/battery.cpp
    src/app/controller.cpp)

add_library(miyonos_core STATIC ${MIYONOS_CORE_SOURCES})
target_include_directories(miyonos_core PUBLIC src)
target_compile_definitions(miyonos_core PUBLIC MIYONOS_VERSION="${PROJECT_VERSION}")
target_compile_options(miyonos_core PRIVATE -Wall -Wextra -Wpedantic)
find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
target_link_libraries(miyonos_core PUBLIC Threads::Threads CURL::libcurl)

if(MIYONOS_BUILD_APP)
  find_package(SDL2 REQUIRED)
  add_executable(miyonos
      src/main.cpp
      src/app/runtime.cpp
      src/platform/frame_presenter.cpp
      src/platform/input.cpp
      src/simulator/mock_sonos.cpp
      src/simulator/simulator_shell.cpp
      src/ui/bitmap_font.cpp
      src/ui/renderer.cpp)
  target_compile_definitions(miyonos PRIVATE MIYONOS_ENABLE_SIMULATOR=1)
  target_include_directories(miyonos PRIVATE ${SDL2_INCLUDE_DIRS})
  target_link_libraries(miyonos PRIVATE miyonos_core ${SDL2_LIBRARIES} ${CMAKE_DL_LIBS})
  if(APPLE)
    target_link_libraries(miyonos PRIVATE "-framework ImageIO"
        "-framework CoreGraphics" "-framework CoreFoundation")
  endif()
  target_compile_options(miyonos PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MIYONOS_BUILD_TESTS)
  enable_testing()
  add_executable(miyonos_tests tests/unit/tests.cpp)
  target_link_libraries(miyonos_tests PRIVATE miyonos_core)
  target_compile_definitions(miyonos_tests PRIVATE
      MIYONOS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME miyonos_tests COMMAND miyonos_tests)
endif()
