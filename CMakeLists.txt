cmake_minimum_required(VERSION 3.16)
project(NetStormPort LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SDL2 REQUIRED)

# ---- engine library (no SDL dependency: data + game logic) ----
add_library(nscore STATIC
  src/core/BinaryFile.cpp
  src/data/Taff.cpp
  src/data/ShapeDb.cpp
  src/data/Palette.cpp
  src/data/PeResources.cpp
  src/data/TypeFile.cpp
  src/data/GameData.cpp
)
target_include_directories(nscore PUBLIC src)
if(MSVC)
  target_compile_options(nscore PRIVATE /W4)
else()
  target_compile_options(nscore PRIVATE -Wall -Wextra)
endif()

# ---- client (SDL2) ----
add_executable(netstorm
  src/render/Renderer.cpp
  src/render/SpriteCache.cpp
  src/app/Viewer.cpp
  src/app/main.cpp
)
target_link_libraries(netstorm PRIVATE nscore)
if(TARGET SDL2::SDL2main)
  target_link_libraries(netstorm PRIVATE SDL2::SDL2main)
endif()
if(TARGET SDL2::SDL2)
  target_link_libraries(netstorm PRIVATE SDL2::SDL2)
else()
  target_include_directories(netstorm PRIVATE ${SDL2_INCLUDE_DIRS})
  target_link_libraries(netstorm PRIVATE ${SDL2_LIBRARIES})
endif()

# ---- command line data tool (no SDL) ----
add_executable(nstool src/app/nstool.cpp)
target_link_libraries(nstool PRIVATE nscore)
