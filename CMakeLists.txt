cmake_minimum_required(VERSION 3.19)
project(anvil CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# JSON
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(nlohmann_json)

# WebView2 SDK (NuGet package is just a zip)
FetchContent_Declare(
  webview2
  URL https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.2792.45
)
FetchContent_MakeAvailable(webview2)

add_executable(anvil WIN32 src/main.cpp)

target_include_directories(anvil PRIVATE
  ${webview2_SOURCE_DIR}/build/native/include
)

target_link_libraries(anvil PRIVATE
  nlohmann_json::nlohmann_json
  ${webview2_SOURCE_DIR}/build/native/x64/WebView2LoaderStatic.lib
  shlwapi
  version
)

if(MSVC)
  target_compile_options(anvil PRIVATE /utf-8 /EHsc /W3)
endif()
