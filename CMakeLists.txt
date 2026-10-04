# Build (needs VS 2022 build tools and Qt 6.8 msvc2022_64, e.g. `aqt install-qt windows desktop 6.8.3 win64_msvc2022_64`):
#   cmake -B build -A x64 -DCMAKE_PREFIX_PATH=<qt>/6.8.3/msvc2022_64
#   cmake --build build --config Release
# Output: build/Release/OpenRGBAK820Plugin.dll
cmake_minimum_required(VERSION 3.20)
project(OpenRGBAK820Plugin CXX)

# Must match the OpenRGB you load it into: 1.0 = plugin API 5, Qt 6.8, MSVC x64.
set(OPENRGB_TAG release_1.0 CACHE STRING "OpenRGB tag whose headers to build against")

set(CMAKE_CXX_STANDARD 17)   # Qt 6 needs 17
set(CMAKE_AUTOMOC ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)

include(FetchContent)
FetchContent_Declare(openrgb
    GIT_REPOSITORY https://gitlab.com/CalcProgrammer1/OpenRGB.git
    GIT_TAG ${OPENRGB_TAG}
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(openrgb)   # no CMakeLists upstream, so this only downloads the headers
set(ORGB ${openrgb_SOURCE_DIR})

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)

add_library(OpenRGBAK820Plugin SHARED AK820Plugin.h AK820Plugin.json AK820Plugin.cpp)

target_include_directories(OpenRGBAK820Plugin PRIVATE
    ${ORGB}
    ${ORGB}/RGBController
    ${ORGB}/dependencies/json
    ${ORGB}/dependencies/hidapi-hotplug-win/include)

# Link against the hidapi-hotplug.dll OpenRGB already ships and has loaded.
target_link_libraries(OpenRGBAK820Plugin PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets
    ${ORGB}/dependencies/hidapi-hotplug-win/x64/hidapi-hotplug.lib)
