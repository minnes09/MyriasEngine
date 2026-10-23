include(FetchContent)

FetchContent_Declare(benchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.9.4.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_ASSEMBLY_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_DOWNLOAD_DEPENDENCIES OFF CACHE BOOL "" FORCE)
set(BENCHMARK_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(benchmark)

# google/benchmark builds its own targets with warnings-as-error on some toolchains. Silence
# whatever fires in its own headers/sources (not our code) rather than touching vendored files.
foreach(benchmark_target IN ITEMS benchmark benchmark_main)
    if(TARGET ${benchmark_target})
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(${benchmark_target} PRIVATE -Wno-character-conversion)
        elseif(MSVC)
            target_compile_options(${benchmark_target} PRIVATE /WX-)
        endif()
    endif()
endforeach()

file(GLOB_RECURSE BENCHMARK_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(MyriasBenchmarks ${BENCHMARK_SOURCES})
# ChunkBenchmarks exercises internal types (Chunk), hence the private include path.
target_include_directories(MyriasBenchmarks PRIVATE "${CMAKE_SOURCE_DIR}/src/Engine/private")
target_link_libraries(MyriasBenchmarks PRIVATE Myrias benchmark::benchmark_main MyriasWarnings)
