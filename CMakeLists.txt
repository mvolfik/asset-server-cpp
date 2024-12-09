cmake_minimum_required (VERSION 3.20)

project("Asset Serve"
VERSION 0.1
DESCRIPTION "Server for upload, processing and deduplicating of images"
LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL REQUIRED)

find_package(Boost REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBVIPS REQUIRED IMPORTED_TARGET vips-cpp)
pkg_check_modules(LIBMAGIC REQUIRED IMPORTED_TARGET libmagic)

SET(ADA_TOOLS OFF)
SET(ADA_TESTING OFF)
SET(ADA_BENCHMARKS OFF)
include(FetchContent)
FetchContent_Declare(
    unidecode
    GIT_REPOSITORY "https://github.com/mvolfik/unidecode.git"
    GIT_TAG "efab8ae6e38c317e1a91465becabb97f20007abf"
)
FetchContent_Declare(
    ada
    GIT_REPOSITORY "https://github.com/ada-url/ada.git"
    GIT_TAG "v2.9.2"
)
FetchContent_MakeAvailable(unidecode ada)

set(MAIN_EXE "asset-server")
add_executable(${MAIN_EXE} "src/main.cpp")

set(BINARIES ${MAIN_EXE})

set(ASSET_SERVER_EXTRA_DEBUG FALSE CACHE BOOL "Extra debug features for asset-server: build targets test,playground,sandbox, build with -fsanitize=address")

if(ASSET_SERVER_EXTRA_DEBUG)
    set(TEST_EXE "test")
    add_executable(${TEST_EXE} "test/test.cpp")

    set(PLAYGROUND_EXE "playground")
    add_executable(${PLAYGROUND_EXE} "playground.cpp")

    set(SANDBOX_EXE "sandbox")
    add_executable(${SANDBOX_EXE} "sandbox.cpp")

    list(APPEND BINARIES ${TEST_EXE} ${PLAYGROUND_EXE} ${SANDBOX_EXE})
endif()


foreach(EXE IN LISTS BINARIES)
    target_compile_options(${EXE} PRIVATE "-Wall" "-Wextra")

    target_include_directories(${EXE} PRIVATE ${Boost_INCLUDE_DIRS})
    target_link_libraries(${EXE} PRIVATE ${Boost_LIBRARIES})

    target_include_directories(${EXE} PRIVATE PkgConfig::LIBVIPS)
    target_link_libraries(${EXE} PRIVATE PkgConfig::LIBVIPS)

    target_include_directories(${EXE} PRIVATE PkgConfig::LIBMAGIC)
    target_link_libraries(${EXE} PRIVATE PkgConfig::LIBMAGIC)

    target_include_directories(${EXE} PRIVATE OpenSSL::Crypto)
    target_link_libraries(${EXE} PRIVATE OpenSSL::Crypto)

    target_include_directories(${EXE} PRIVATE ${unidecode_SOURCE_DIR}/include)
    target_link_libraries(${EXE} PRIVATE unidecode)

    target_include_directories(${EXE} PRIVATE ada::ada)
    target_link_libraries(${EXE} PRIVATE ada::ada)

    if(ASSET_SERVER_EXTRA_DEBUG)
        # this also requires libasan
        target_compile_options(${EXE} PRIVATE -fsanitize=address)
        target_link_options(${EXE} PRIVATE -fsanitize=address)
    endif()
endforeach()
