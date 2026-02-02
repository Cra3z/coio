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

target_compile_definitions(
    example-http_server
    PRIVATE
    HTTP_SERVER_STATIC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/static"
)