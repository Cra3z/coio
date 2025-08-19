project(coio-examples)

file(GLOB EXAMPLE_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.cpp")

foreach(EXAMPLE_FILE ${EXAMPLE_FILES})
    get_filename_component(EXAMPLE_FILE_NAME ${EXAMPLE_FILE} NAME_WE)

    add_executable(
        example-${EXAMPLE_FILE_NAME}
        ${EXAMPLE_FILE}
    )

    target_link_libraries(
        example-${EXAMPLE_FILE_NAME}
        PRIVATE
        coio
    )
    target_compile_definitions(
        example-${EXAMPLE_FILE_NAME}
        PRIVATE
        -DCOIO_ENABLE_SENDERS
        -DCOIO_EXECUTION_USE_NVIDIA
    )

    target_include_directories(
        example-${EXAMPLE_FILE_NAME}
        PRIVATE
        "/mnt/d/coding/cpp/stdexec/include"
    )
endforeach()

