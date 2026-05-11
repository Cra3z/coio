project(coio-tests)

file(GLOB COIO_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/test_*.cpp")

CPMFindPackage(
    NAME doctest
    GITHUB_REPOSITORY doctest/doctest
    GIT_TAG v2.5.2
    EXCLUDE_FROM_ALL YES
    SYSTEM YES
)

foreach(COIO_TEST_SOURCE IN LISTS COIO_TEST_SOURCES)
    get_filename_component(COIO_TEST_NAME "${COIO_TEST_SOURCE}" NAME_WE)

    add_executable(
        ${COIO_TEST_NAME}
    )

    target_sources(
        ${COIO_TEST_NAME}
        PRIVATE
        ${COIO_TEST_SOURCE}
    )

    target_link_libraries(
        ${COIO_TEST_NAME}
        PRIVATE
        doctest::doctest
        coio
    )
    target_compile_definitions(
        ${COIO_TEST_NAME}
        PRIVATE
        -DDOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
    )

    add_test(
        NAME ${COIO_TEST_NAME}
        COMMAND $<TARGET_FILE:${COIO_TEST_NAME}>
    )
endforeach()
