cmake_minimum_required (VERSION 3.20)

project("Asset Server"
VERSION 0.1
DESCRIPTION "Server for upload, processing and deduplicating of images"
LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Boost REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBVIPS REQUIRED IMPORTED_TARGET vips-cpp)

# MAIN
set(MAIN_EXE "asset-server")
add_executable(${MAIN_EXE} "src/main.cpp" "vendor/ada.cpp")
target_compile_options(${MAIN_EXE} PRIVATE "-Wall" "-Wextra")

target_include_directories(${MAIN_EXE} PRIVATE ${Boost_INCLUDE_DIRS})
target_link_libraries(${MAIN_EXE} PRIVATE ${Boost_LIBRARIES})

target_include_directories(${MAIN_EXE} PRIVATE PkgConfig::LIBVIPS)
target_link_libraries(${MAIN_EXE} PRIVATE PkgConfig::LIBVIPS)

# TESTS
set(TEST_EXE "test")
add_executable(${TEST_EXE} "test/test.cpp" "vendor/ada.cpp")
target_compile_options(${TEST_EXE} PRIVATE "-Wall" "-Wextra")

add_executable("playground" "playground.cpp")
target_compile_options("playground" PRIVATE "-Wall" "-Wextra")
target_include_directories("playground" PRIVATE PkgConfig::LIBVIPS)
target_link_libraries("playground" PRIVATE PkgConfig::LIBVIPS)
