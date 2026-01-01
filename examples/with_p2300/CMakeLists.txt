file(GLOB EXAMPLE_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.cpp")

foreach(EXAMPLE_FILE ${EXAMPLE_FILES})
    get_filename_component(EXAMPLE_FILE_NAME ${EXAMPLE_FILE} NAME_WE)

    add_executable(
        example-${EXAMPLE_FILE_NAME}-with-p2300
        ${EXAMPLE_FILE}
    )

    target_link_libraries(
        example-${EXAMPLE_FILE_NAME}-with-p2300
        PRIVATE
        coio
    )
endforeach()