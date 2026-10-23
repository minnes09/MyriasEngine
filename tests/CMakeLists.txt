include(FetchContent)

FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM)
# Match the project's dynamic CRT (/MD).
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# GoogleTest builds its own targets with warnings-as-error. Clang 22 added
# -Wcharacter-conversion, which fires inside gtest's own headers (not our
# code). Silence it only for gtest/gmock targets, only on Clang, without
# touching the vendored sources.
foreach(gtest_target IN ITEMS gtest gtest_main gmock gmock_main)
    if(TARGET ${gtest_target})
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(${gtest_target} PRIVATE -Wno-character-conversion)
        endif()
    endif()
endforeach()

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(MyriasTests ${TEST_SOURCES})
# ECS tests also exercise internal types (Chunk, ComponentTypeRegistry), hence the private include path.
target_include_directories(MyriasTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src/Engine/private")
target_link_libraries(MyriasTests PRIVATE Myrias GTest::gtest_main MyriasWarnings)

include(GoogleTest)
gtest_discover_tests(MyriasTests DISCOVERY_MODE PRE_TEST)
