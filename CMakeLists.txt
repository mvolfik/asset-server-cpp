cmake_minimum_required (VERSION 3.20)

project("Asset Server"
VERSION 0.1
DESCRIPTION "Server for upload, processing and deduplicating of images"
LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ImageMagick REQUIRED COMPONENTS Magick++)

find_package(Boost REQUIRED)

# MAIN
set(MAIN_EXE "asset-server")
add_executable(${MAIN_EXE} "src/main.cpp" "src/main.hpp" "vendor/ada.cpp" "vendor/ada.hpp")
add_compile_definitions("MAGICKCORE_HDRI_ENABLE=0" "MAGICKCORE_QUANTUM_DEPTH=16")

target_include_directories(${MAIN_EXE} PRIVATE ${ImageMagick_INCLUDE_DIRS})
target_link_libraries(${MAIN_EXE} PRIVATE ${ImageMagick_LIBRARIES})

target_include_directories(${MAIN_EXE} PRIVATE ${Boost_INCLUDE_DIRS})
target_link_libraries(${MAIN_EXE} PRIVATE ${Boost_LIBRARIES})
