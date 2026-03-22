add_executable(
    example-stmp_client
    client.h
    main.cpp
)

target_link_libraries(
    example-stmp_client
    PRIVATE
    coio
)