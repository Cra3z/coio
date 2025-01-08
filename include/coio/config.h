#pragma once
#include <cassert>
#include <version>
#ifdef __linux__
#include <linux/version.h>
#endif


#define COIO_CXX_STD98 199711L
#define COIO_CXX_STD11 201103L
#define COIO_CXX_STD14 201402L
#define COIO_CXX_STD17 201703L
#define COIO_CXX_STD20 202002L
#define COIO_CXX_STD23 202302L

#ifdef _MSVC_LANG
#define COIO_CXX_STANDARD _MSVC_LANG
#else
#define COIO_CXX_STANDARD __cplusplus
#endif

#if COIO_CXX_STANDARD < COIO_CXX_STD20 or not defined(__cpp_impl_coroutine) or not defined(__cpp_lib_coroutine)
#error "coio requires a C++20 compiler that supports coroutine."
#endif

#ifdef __clang__
#define COIO_CXX_COMPILER_CLANG 1
#define COIO_CXX_COMPILER_GCC 0
#define COIO_CXX_COMPILER_MSVC 0
#elif defined(__GNUC__)
#define COIO_CXX_COMPILER_CLANG 0
#define COIO_CXX_COMPILER_GCC 1
#define COIO_CXX_COMPILER_MSVC 0
#elif defined(_MSC_VER)
#define COIO_CXX_COMPILER_CLANG 0
#define COIO_CXX_COMPILER_GCC 0
#define COIO_CXX_COMPILER_MSVC 1
#else
#error "coio requires the C++ compiler is clang, gcc or msvc."
#endif

#if COIO_CXX_COMPILER_CLANG
#define COIO_ALWAYS_INLINE [[clang::always_inline]]
#elif COIO_CXX_COMPILER_GCC
#define COIO_ALWAYS_INLINE [[gnu::always_inline]]
#elif COIO_CXX_COMPILER_MSVC
#define COIO_ALWAYS_INLINE [[msvc::forceinline]]
#endif

#if COIO_CXX_STANDARD >= COIO_CXX_STD23 and defined(__cpp_static_call_operator)
#define COIO_STATIC_CALL_OP static
#define COIO_STATIC_CALL_OP_CONST
#else
#define COIO_STATIC_CALL_OP
#define COIO_STATIC_CALL_OP_CONST const
#endif

#define COIO_ASSERT(...) assert(__VA_ARGS__)
#define COIO_PRECONDITION(...)
#define COIO_POSTCONDITION(r, ...)

#if __has_cpp_attribute(assume)
#define COIO_ASSUME(expr) [[assume(expr)]]
#elif COIO_CXX_COMPILER_CLANG
#define COIO_ASSUME(expr) __builtin_assume(expr)
#elif COIO_CXX_COMPILER_GCC
#define COIO_ASSUME(expr) __attribute__((assume(expr)))
#elif COIO_CXX_COMPILER_MSVC
#define COIO_ASSUME(expr) __assume(expr)
#endif

#ifdef NDEBUG
#define COIO_DCHECK(expr) COIO_ASSERT(expr)
#else
#define COIO_DCHECK(expr) COIO_ASSUME(expr)
#endif

#ifdef COIO_USE_MODULE
#define COIO_MODULE_EXPORT export
#define COIO_MODULE_EXPORT_BEGIN export {
#define COIO_MODULE_EXPORT_END }
#else
#define COIO_MODULE_EXPORT
#define COIO_MODULE_EXPORT_BEGIN
#define COIO_MODULE_EXPORT_END
#endif

#if COIO_CXX_STANDARD >= COIO_CXX_STD23 and defined(__cpp_lib_start_lifetime_as)
#define COIO_START_LIFETIME_AS(type, address) void(std::start_lifetime_as<type>(address))
#define COIO_START_LIFETIME_AS_ARRAY(type, address, size) void(std::start_lifetime_as_array<type>(address, size))
#else
#define COIO_START_LIFETIME_AS(type, address) void(0)
#define COIO_START_LIFETIME_AS_ARRAY(type, address, size) void(0)
#endif

#if __has_cpp_attribute(no_unique_address)
#define COIO_NO_UNIQUE_ADDRESS [[no_unique_address]]
#elif COIO_CXX_COMPILER_MSVC and __has_cpp_attribute(msvc::no_unique_address)
#define COIO_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#define COIO_NO_UNIQUE_ADDRESS
#endif

#if defined(_WIN32) or defined(_WIN64)
#define COIO_OS_WINDOWS 1
#define COIO_OS_LINUX 0
#elif defined(__linux__)
#define COIO_OS_WINDOWS 0
#define COIO_OS_LINUX 1
#else
#error "unsupported operation system."
#endif

#if COIO_OS_WINDOWS
    #define COIO_HAS_EPOLL 0
    #define COIO_HAS_IO_URING 0
    #if __has_include(<ioapiset.h>)
    #define COIO_HAS_IOCP 1
    #else
    #define COIO_HAS_IOCP 0
    #endif
#elif COIO_OS_LINUX
    #define COIO_HAS_IOCP 0
    #if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 45) and __has_include(<sys/epoll.h>)
    #define COIO_HAS_EPOLL 1
    #else
    #define COIO_HAS_EPOLL 0
    #endif
    #if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0) and __has_include(<linux/io_uring.h>)
    #define COIO_HAS_IO_URING 1
    #else
    #define COIO_HAS_IO_URING 0
    #endif
#endif

#if not COIO_HAS_IOCP and not COIO_HAS_EPOLL and not COIO_HAS_IO_URING
#error "there must be epoll or iocp or io_uring."
#endif