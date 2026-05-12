// ReSharper disable once CppMissingIncludeGuard

#ifdef COIO_SUPPRESS_PUSH
#error "<coio/detail/suppress_pop.h> shall be included before <coio/detail/suppress_push.h> is included again"
#else
#define COIO_SUPPRESS_PUSH
#endif

#if defined(_MSC_VER) and not defined(__clang__)
    #pragma warning(push)
#elif defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wcovered-switch-default"
    #pragma clang diagnostic ignored "-Wctad-maybe-unsupported"
    #pragma clang diagnostic ignored "-Wexit-time-destructors"
    #pragma clang diagnostic ignored "-Wmissing-field-initializers"
    #pragma clang diagnostic ignored "-Wnon-virtual-dtor"
    #pragma clang diagnostic ignored "-Wshadow-field-in-constructor"
    #pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#elif defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    #pragma GCC diagnostic ignored "-Wunused-local-typedefs"
#endif
