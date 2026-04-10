find_path(Liburing_INCLUDE_DIR
    NAMES liburing.h
)

find_library(Liburing_LIBRARY
    NAMES uring
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Liburing
    REQUIRED_VARS Liburing_LIBRARY Liburing_INCLUDE_DIR
)

if(Liburing_FOUND)
    set(Liburing_LIBRARIES ${Liburing_LIBRARY})
    set(Liburing_INCLUDE_DIRS ${Liburing_INCLUDE_DIR})

    if(NOT TARGET Liburing::uring)
        add_library(Liburing::uring UNKNOWN IMPORTED)
        set_target_properties(Liburing::uring PROPERTIES
            IMPORTED_LOCATION "${Liburing_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${Liburing_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(Liburing_INCLUDE_DIR Liburing_LIBRARY)
