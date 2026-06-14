cmake_minimum_required(VERSION 3.16)
project(anvil CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(nlohmann_json)

add_executable(anvil src/main.cpp)
target_link_libraries(anvil PRIVATE nlohmann_json::nlohmann_json)

if(WIN32)
  target_link_libraries(anvil PRIVATE winhttp)
endif()

if(MSVC)
  target_compile_options(anvil PRIVATE /W3 /utf-8 /EHsc)
endif()
