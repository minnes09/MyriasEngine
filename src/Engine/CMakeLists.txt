file(GLOB_RECURSE MYRIAS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/private/*.cpp")
file(GLOB_RECURSE MYRIAS_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/public/*.h")

add_library(Myrias STATIC ${MYRIAS_SOURCES} ${MYRIAS_HEADERS})

target_include_directories(Myrias
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/private")

# Public headers expose sf:: types, so SFML is part of the interface.
target_link_libraries(Myrias
    PUBLIC  SFML::Graphics SFML::Window SFML::System
    PRIVATE MyriasWarnings)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${MYRIAS_SOURCES} ${MYRIAS_HEADERS})
