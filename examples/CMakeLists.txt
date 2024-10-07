add_executable(
    example-start
    start.cpp
)

target_link_libraries(
    example-start
    PRIVATE
    coio
)
