add_executable(
    example-http_server
    connection.h
    connection.cpp
    define.h
    io_context_pool.h
    io_context_pool.cpp
    response.h
    response.cpp
    request.h
    router.h
    router.cpp
    main.cpp
)

target_link_libraries(
    example-http_server
    PRIVATE
    coio
)

# Define the source directory so the server can find static files
target_compile_definitions(
    example-http_server
    PRIVATE
    HTTP_SERVER_STATIC_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

# Copy static files to build directory for convenience
file(GLOB STATIC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/static/*")
file(COPY ${STATIC_FILES} DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/static")
