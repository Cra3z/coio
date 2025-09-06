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

    target_compile_options(
        example-${EXAMPLE_FILE_NAME}
        PRIVATE
        $<$<AND:$<PLATFORM_ID:Linux>,$<CXX_COMPILER_ID:GNU,Clang>>:-fsanitize=address,undefined -fno-omit-frame-pointer>
    )

    target_link_options(
        example-${EXAMPLE_FILE_NAME}
        PRIVATE
        $<$<AND:$<PLATFORM_ID:Linux>,$<CXX_COMPILER_ID:GNU,Clang>>:-fsanitize=address,undefined>
    )
endforeach()

