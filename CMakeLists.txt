cmake_minimum_required(VERSION 3.26)

project(coio)

add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/permissive->")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor>")

option(COIO_BUILD_EXAMPLES "whether to build examples" OFF)
set(COIO_SENDERS_BACKEND "NVIDIA" CACHE STRING "the backend to use for std::execution support.
    available options are: NVIDIA, BEMAN, CXX26.
    NVIDIA - use NVIDIA/stdexec implementation,
    BEMAN - use bemanproject/execution implementation,
    CXX26 - use the standard library implementation"
)

include("${PROJECT_SOURCE_DIR}/cmake/CPM.cmake")

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

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

target_compile_definitions(
    ${PROJECT_NAME}
    PUBLIC
    -DCOIO_EXECUTION_USE_${COIO_SENDERS_BACKEND}
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

file(GLOB_RECURSE COIO_PORTABLE_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/portable/*.cpp")
target_sources(
    ${PROJECT_NAME}
    PRIVATE
    ${COIO_PORTABLE_SOURCES}
)

if (LINUX)
    file(GLOB_RECURSE COIO_LINUX_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/linux/*.cpp")
    target_sources(
        ${PROJECT_NAME}
        PRIVATE
        ${COIO_LINUX_SOURCES}
    )
elseif (WIN32)
    file(GLOB_RECURSE COIO_WIN_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/win/*.cpp")
    target_sources(
        ${PROJECT_NAME}
        PRIVATE
        ${COIO_WIN_SOURCES}
    )

    target_link_libraries(
        ${PROJECT_NAME}
        PUBLIC
        ws2_32
        mswsock
    )

    target_compile_definitions(
        ${PROJECT_NAME}
        PRIVATE
        -DWIN32_LEAN_AND_MEAN
        -DNOMINMAX
    )
endif()

target_include_directories(
    ${PROJECT_NAME}
    PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${PROJECT_NAME}/include>
)

find_package(Threads REQUIRED)

target_link_libraries(
    ${PROJECT_NAME}
    PUBLIC
    Threads::Threads
)

if (COIO_SENDERS_BACKEND STREQUAL "NVIDIA")
    target_compile_features(
        ${PROJECT_NAME}
        PUBLIC
        cxx_std_20
    )
    CPMFindPackage(
        NAME stdexec
        GITHUB_REPOSITORY NVIDIA/stdexec
        GIT_TAG main
        EXCLUDE_FROM_ALL YES
        SYSTEM YES
        OPTIONS
        "STDEXEC_BUILD_EXAMPLES OFF"
        "STDEXEC_BUILD_TESTS OFF"
    )
    target_link_libraries(${PROJECT_NAME} PUBLIC $<BUILD_LOCAL_INTERFACE:STDEXEC::stdexec>)
elseif (COIO_SENDERS_BACKEND STREQUAL "BEMAN")
    target_compile_features(
        ${PROJECT_NAME}
        PUBLIC
        cxx_std_23
    )
    CPMFindPackage(
        NAME beman.execution
        GITHUB_REPOSITORY bemanproject/execution
        GIT_TAG main
        EXCLUDE_FROM_ALL YES
        SYSTEM YES
        OPTIONS
        "BEMAN_EXECUTION_ENABLE_TESTING OFF"
        "BEMAN_EXECUTION_BUILD_EXAMPLES OFF"
        "BEMAN_USE_MODULES OFF"
    )
    target_link_libraries(${PROJECT_NAME} PUBLIC $<BUILD_LOCAL_INTERFACE:beman::execution_headers>)
endif()

if (LINUX)
    find_package(Liburing QUIET)
    if (Liburing_FOUND)
        message(STATUS "found liburing: ${Liburing_LIBRARY}")
        target_link_libraries(${PROJECT_NAME} PUBLIC Liburing::uring)
        install(
            FILES ${PROJECT_SOURCE_DIR}/cmake/FindLiburing.cmake
            DESTINATION ${PROJECT_NAME}/lib/cmake/${PROJECT_NAME}
        )
    else()
        message(STATUS "liburing not found, io_uring support will be unavailable")
    endif()
endif()

install(
    TARGETS ${PROJECT_NAME}
    EXPORT ${PROJECT_NAME}-targets
    DESTINATION ${PROJECT_NAME}/lib/${CMAKE_BUILD_TYPE}
    FILE_SET coio_public_headers DESTINATION ${PROJECT_NAME}/include
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