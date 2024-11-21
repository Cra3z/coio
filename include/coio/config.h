#pragma once

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

#define COIO_MODULE_EXPORT export
#define COIO_MODULE_EXPORT_BEGIN export {
#define COIO_MODULE_EXPORT_END }

#if COIO_CXX_STANDARD >= COIO_CXX_STD23 and defined(__cpp_lib_start_lifetime_as)
#define COIO_START_LIFETIME_AS(type, address) void(std::start_lifetime_as<type>(address))
#define COIO_START_LIFETIME_AS_ARRAY(type, address, size) void(std::start_lifetime_as_array<type>(address, size))
#else
#define COIO_START_LIFETIME_AS(type, address) void(0)
#define COIO_START_LIFETIME_AS_ARRAY(type, address, size) void(0)
#endif

#if COIO_CXX_COMPILER_MSVC
#define COIO_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#define COIO_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif