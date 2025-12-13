cmake_minimum_required(VERSION 3.26)

project(coio)

set(CMAKE_CXX_STANDARD 20)

add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/permissive->")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor>")

option(COIO_BUILD_EXAMPLES "whether to build examples" OFF)

file(GLOB_RECURSE COIO_PUBLIC_HEADERS CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/include/*.h")
add_library(
    ${PROJECT_NAME}
    ${COIO_PUBLIC_HEADERS}
)

add_library(
    ${PROJECT_NAME}::${PROJECT_NAME}
    ALIAS
    ${PROJECT_NAME}
)

target_sources(
    ${PROJECT_NAME}
    INTERFACE
    FILE_SET coio_public_headers
    TYPE HEADERS
    BASE_DIRS ${PROJECT_SOURCE_DIR}/include
    FILES
    ${COIO_PUBLIC_HEADERS}
)

if (LINUX)
    file(GLOB_RECURSE COIO_LINUX_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/linux/*.cpp")
    target_sources(
        ${PROJECT_NAME}
        PRIVATE
        ${COIO_LINUX_SOURCES}
    )
endif()

target_include_directories(
    ${PROJECT_NAME}
    PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${PROJECT_NAME}/include>
)

install(
    TARGETS ${PROJECT_NAME}
    EXPORT ${PROJECT_NAME}-targets
    FILE_SET coio_public_headers DESTINATION ${PROJECT_NAME}/include
    DESTINATION ${PROJECT_NAME}/lib/${CMAKE_BUILD_TYPE}
)

install(
    EXPORT ${PROJECT_NAME}-targets
    FILE ${PROJECT_NAME}-targets.cmake
    NAMESPACE ${PROJECT_NAME}::
    DESTINATION ${PROJECT_NAME}/lib/cmake/${PROJECT_NAME}
)

export(
    EXPORT ${PROJECT_NAME}-targets
    NAMESPACE ${PROJECT_NAME}::
    FILE ${PROJECT_BINARY_DIR}/${PROJECT_NAME}-targets.cmake
)

include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${PROJECT_SOURCE_DIR}/cmake/${PROJECT_NAME}-config.cmake.in
    ${PROJECT_NAME}-config.cmake
    INSTALL_DESTINATION ${PROJECT_NAME}/lib/cmake/${PROJECT_NAME}
)

install(
    FILES ${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config.cmake
    DESTINATION ${PROJECT_NAME}/lib/cmake/${PROJECT_NAME}
)

if (COIO_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()