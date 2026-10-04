cmake_minimum_required(VERSION 3.21)
project(DCMF VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
endif()

option(DCMF_BUILD_PLUGIN "Build the SKSE plugin (needs CommonLibSSE-NG)" OFF)

if(DCMF_BUILD_PLUGIN)
    # CommonLibSSE-NG builds its own test suite by default; we only want the library.
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(lib/commonlibsse-ng)

    # CommonLibSSE-NG only includes this helper on its prebuilt-binary path, so a
    # from-source build never defines add_commonlibsse_plugin(). Include it here.
    include("${CMAKE_CURRENT_SOURCE_DIR}/lib/commonlibsse-ng/cmake/CommonLibSSE.cmake")

    # The plugin.
    add_subdirectory(src)

endif()
