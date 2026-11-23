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
  src/data/TypeRegistry.cpp
  src/data/FortFile.cpp
  src/data/MissionInfo.cpp
  src/data/Gif.cpp
  src/world/Island.cpp
  src/world/Mission.cpp
  src/game/BridgePieces.cpp
  src/game/Sim.cpp
  src/game/Ai.cpp
  src/game/MissionScript.cpp
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
  src/app/WorldView.cpp
  src/app/Frontend.cpp
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

# ---- tests (need a NetStorm install: -DNETSTORM_DATA=/path/to/Netstorm) ----
enable_testing()
add_executable(test_sacrifice tests/test_sacrifice.cpp src/game/Sim.cpp)
add_executable(test_knowledge tests/test_knowledge.cpp src/game/Sim.cpp)
target_link_libraries(test_knowledge PRIVATE nscore)
target_include_directories(test_knowledge PRIVATE src)
target_link_libraries(test_sacrifice PRIVATE nscore)
target_include_directories(test_sacrifice PRIVATE src)
if(NETSTORM_DATA)
  add_test(NAME sacrifice COMMAND test_sacrifice ${NETSTORM_DATA})
  add_test(NAME knowledge COMMAND test_knowledge ${NETSTORM_DATA})
endif()
